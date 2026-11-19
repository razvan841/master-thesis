# Testbed and implementation plan: picoquic on 4 servers

Written 2026-11-18. This plan replaces the quinn/netns parts of [Double_Connection_Plan.md](Double_Connection_Plan.md):
§4.1 (stack choice), §4.2 (fork patch set, now mapped to picoquic in §9 below), §4.3 (the `quinn/` submodule) and
§5.3.1–5.3.2 (platform and topology). The rest of that plan still applies unchanged:
- research questions;
- arms (§5.4) and workloads W1–W9 (§5.6);
- metric definitions (§5.7);
- the calibration checklist (§5.3.4), with quinn replaced by picoquic.

---

## 1. Decisions

| Topic | Decision |
|-------|----------|
| QUIC stack | picoquic, **forked** and pinned to one upstream commit (§9) |
| Hardware | 4 physical servers as a dumbbell: 2 senders, 1 router/emulator, 1 receiver (§3) |
| Application protocol | the stock `picoquicdemo` server. HTTP/0.9 (hq-interop) with generated content `/<N>` for the measurement grid; HTTP/3 as a realism check (§5) |
| Phase 1 arms | picoquic `newreno`, `cubic`, `bbr`; `bbr1` only for the conformance check against Linux BBR |
| Phase 2 | Dash ([Dash_CC_Design.md](Dash_CC_Design.md)) added to the same fork, measured with the same pipeline |

---

## 2. picoquic facts that shape the design (checked in the source, 2026-11-18)

| Fact | Where | Consequence |
|------|-------|-------------|
| Algorithm names: `newreno` (library default), `cubic`, `dcubic`, `fast`, `bbr`, `bbr1`, `prague`, `c4` | `register_all_cc_algorithms.c` | Select with `-G` on the **server** (the data sender) |
| `bbr` cites draft-cardwell-…-02 and uses BBRv3 constants; `bbr1` is BBRv1 | `bbr.c`, `bbr1.c` | Mainline Linux `tcp_bbr` is v1, so compare it with `bbr1`. Use `bbr` as the BBR arm |
| `newreno` and `cubic` both run picoquic's own HyStart-style delay test in slow start, and both pace | `newreno.c`, `cubic.c` | picoquic "Reno" ≠ textbook/Linux Reno; state this in the methodology. **Open check:** is it RFC 9406 HyStart++? The master plan's main baseline A1 needs HyStart++ (§10) |
| IW = `PICOQUIC_CWIN_INITIAL` = 10 × 1536 = 15,360 B | `picoquic_internal.h` | RFC 9002 gives 14,720 B; Linux ≈ 14.5 KB. Keep it equal across arms |
| quicperf (`-a perf`) prints only totals for batch streams; its report file is per media frame and is not opened by picoquicdemo | `quicperf.c`, `picoquicdemo.c` | Not usable for per-flow FCT as is |
| The demo client prints only "Stream N ended after X bytes" (no timestamps, stream IDs ≤ 64) | `democlient.c` | Per-request timestamps need a small patch (§4.3) |
| The demo server serves files from `-w <dir>`, or **generated content** for numeric paths (`GET /456789`) | `demoserver.c`, `picoquicdemo.c` | No disk I/O: the size is in the URL, matching master plan §4.4 (`/size/<n>`) |
| The demo server sends the body just-in-time (`picoquic_mark_active_stream` + `prepare_to_send`) | `demoserver.c` | The transport does not know how many bytes remain → Dash §7.9 needs a size-hint API (§9) |
| UDP GSO (`UDP_SEGMENT`) is on by default; `-0` disables it | `sockloop.c` | Use `-0` on senders (master plan §4.7, GSO pitfall) |
| Per-connection summary log `-F`: duration, bytes, Mbps, srtt, min RTT, cwnd, CC algorithm, packets sent, retransmissions, spurious, cc_state/cc_param | `performance_log.c` | Cheap; keep it on in every run |
| Session tickets already store RTT and CWIN; CCs receive a `seed_cwin` notification | `picoquic_internal.h`, `picoquic.h` | Reuse for Dash warm start (§7.12 of the design) |
| Extension-frame template: the BDP frame (`0xebd9`) plus its transport parameter; ACK_FREQUENCY (`0xAF`) and time-stamp (757) frames exist | `picoquic_internal.h` | Copy the BDP frame's pattern for PROBE_TRAIN / TRAIN_REPORT |

---

## 3. Topology and roles

```
 ──────────── management network (SSH, orchestration, chrony) — never carries test traffic ────────────
       │                    │                          │                          │
 ┌─────┴──────┐      ┌──────┴─────┐          ┌─────────┴────────┐       ┌─────────┴────────┐
 │ S1 sender A│      │ S2 sender B│          │ S3 router        │       │ S4 receiver      │
 │ picoquic   │      │ picoquic / │          │ tc, netem (IFB), │       │ all clients,     │
 │ server     │      │ iperf3 TCP │          │ tcpdump          │       │ launcher         │
 └─────┬──────┘      └──────┬─────┘          └──┬────────────┬──┘       └─────────┬────────┘
       └──── switch/VLAN ───┘──────────────── $IN            $OUT ────────────────┘
                                                    bottleneck = egress of $OUT (S3 → S4)
```

- **Data direction:** server → client (downloads), as in the master plan. In picoquic the sender's algorithm is the server's `-G`.
- **Two sender hosts:** competing flows (W3, W6) don't share CPU, NIC queues or socket buffers.
- **All clients on S4:** every FCT and every staggered start offset comes from one clock.
- **All emulation on S3:** endpoints run unmodified network stacks.
- **Controller:** runs on the management interface (S4 or a laptop).
- **Cabling:** with two data ports on S3, S1 and S2 sit behind a switch. Their shared uplink must run far above the emulated rate C.

---

## 4. Per-host setup

### 4.1 S3: router and emulator

Same rule as master plan §5.3.2: **netem never sits on the bottleneck interface.** Delay is RTT/2 on each direction, implemented as follows:
- **ACK path:** egress of `$IN`.
- **Data path:** ingress of `$IN`, redirected to an IFB device before the packets reach the bottleneck.

```sh
# Offloads off on both data interfaces, so queues hold real packets
ethtool -K $IN  gro off gso off tso off lro off
ethtool -K $OUT gro off gso off tso off lro off

# Bottleneck on the egress towards S4 (recipe from master plan §5.3.3)
tc qdisc replace dev $OUT root handle 1: htb default 1
tc class add dev $OUT parent 1: classid 1:1 htb rate ${C}mbit ceil ${C}mbit burst 15k cburst 15k
tc qdisc add dev $OUT parent 1:1 handle 10: bfifo limit ${BUF_BYTES}   # or fq_codel ecn | codel ecn | dualpi2

# Delay, RTT/2 on the ACK path (egress towards the senders)
tc qdisc replace dev $IN root netem delay ${HALF_RTT}ms limit 1000000

# Delay, RTT/2 on the data path, before the bottleneck (ingress of $IN via IFB)
modprobe ifb numifbs=1 && ip link set ifb0 up
tc qdisc add dev $IN handle ffff: ingress
tc filter add dev $IN parent ffff: matchall action mirred egress redirect dev ifb0
tc qdisc replace dev ifb0 root netem delay ${HALF_RTT}ms limit 1000000
# Random loss (W7 only): forward path, never on $OUT
#   tc qdisc change dev ifb0 root netem delay ${HALF_RTT}ms loss 0.1% limit 1000000
```

- **Different RTTs per sender** (RTT fairness): replace each root netem with `prio` plus one netem child per sender. Select the child with `u32` filters on the IP address: `src` on `ifb0`, `dst` on `$IN`.
- **Before every run:** delete and recreate all qdiscs, so no backlog or counters carry over.
- **Captures:** tcpdump on `$IN` and `$OUT`:
  - headers only (`-s 128`);
  - `--time-stamp-precision=nano` and `-B 262144`;
  - pinned to its own core;
  - kernel timestamps, so both interfaces share one clock.

### 4.2 S1 / S2: senders

- **picoquic server**, started fresh for every run so each run gets its own state and logs:
  ```sh
  taskset -c 2 picoquicdemo -p 4433 -c certs/cert.pem -k certs/key.pem -G cubic -0 \
      -F /mnt/ramlog/perf_server.csv  [-b /mnt/ramlog/]          # -b only on trace runs
  ```
  (Check exact flags with `picoquicdemo -h`.)
- **Cross traffic with another algorithm:** run a second server on S2 with a different `-G`.
- **Kernel TCP reference** (calibration and W3 cross traffic): `iperf3 -c S4 -C cubic|reno|bbr -t 60 -J` from S1/S2, with `iperf3 -s` on S4.
- **Host settings:**
  - `performance` governor;
  - pinned cores, with NIC IRQs on other cores;
  - large `wmem_max`/`rmem_max`;
  - logs on tmpfs (`/mnt/ramlog`), copied after the run;
  - chrony client.
- **Fixed for every run:** MTU (`-m`) and flow-control limits ≥ 4 × the largest BDP. Check the qlog for `*_BLOCKED` frames.

### 4.3 S4: receiver

- **Patched demo client.** In `democlient.c`, write one CSV line per request (`CLOCK_MONOTONIC`, ns) with the master plan §4.5 fields:
  - `t_conn_start`;
  - `t_handshake_done` (from the `almost_ready`/`ready` callback);
  - `t_request`, `t_first_byte`, `t_last_byte`;
  - `bytes`.

  About 30–50 lines. It is the first commit in the fork.
- **One process per connection** (cold start), started by a launcher that reads the run's flow schedule:
  ```sh
  taskset -c 3 picoquicdemo -a hq-interop -D -F /mnt/ramlog/perf_client.csv 10.0.1.1 4433 "/1000000"
  ```
  - `-D` stops the client writing the body to disk.
  - Scenario syntax: `<stream>:<previous>:<path>;…`. The default is sequential requests on one connection.
- **The client's own CC barely matters**, since it sends only requests and ACKs. Pin it anyway, so arms differ only on the server.
- **Host settings:** large `rmem_max`. Snapshot `nstat` before and after (`UdpRcvbufErrors` must stay 0).

---

## 5. HTTP/0.9 vs HTTP/3 on the server

- **The same server binary handles both.** picoquicdemo answers HTTP/3 and HTTP/0.9, with files or generated content.
- **Measurement grid: HTTP/0.9 with generated `/<N>`.** This keeps master plan §4.4's choice that HTTP/3 adds noise unrelated to CC: QPACK, control streams, SETTINGS and header bytes. It also removes disk I/O.
- **Realism check: HTTP/3.** Run one W1 slice and the web-page-like workload over HTTP/3, to show that conclusions hold with the real web stack. If they do, say so in the thesis.
- **Serving real files** (e.g., a replayed web page): put them on tmpfs (`-w /mnt/www`) so no read touches the disk.

---

## 6. Phase 1 experiments (baselines only)

- **Network grid:** the core grid of master plan §5.5, with these settings:
  - C ∈ {20, 100} Mbit/s;
  - RTT ∈ {20, 50, 100, 200} ms;
  - FIFO 0.5 and 2 BDP, plus the AQMs.
- **Base point for one-factor-at-a-time sweeps:** 100 Mbit/s × 50 ms, FIFO 1 BDP (625 KB), the same path as Dash design §11.

| ID | Maps to | Workload | Main outputs |
|----|---------|----------|--------------|
| E0 | §5.3.4 | Calibration items 1–11, plus kernel TCP `reno`/`cubic`/`bbr` vs picoquic `newreno`/`cubic`/`bbr1` on the same path (conformance) | Pass/fail per item, stored with the campaign |
| E1 | W1 | One request per connection; sizes 10 KB, 50 KB, 100 KB, 250 KB, 500 KB, 1, 2, 5, 10, 20, 50 MB; ≥ 50 requests per size per run, idle gap until backlog = 0 | FCT_req, FCT_conn, slowdown, loss, overshoot |
| E2 | — | One 60 s bulk flow per algorithm; buffer sweep 0.1–4 BDP | Goodput, utilisation, queueing delay, loss |
| E3 | W6 | 2 flows of the same algorithm, second starts at t = 20 s; equal RTTs, then RTT ratios 1:2 and 1:4 | JFI over time, convergence time |
| E4 | W6 | Every pair of {newreno, cubic, bbr}, and each against kernel TCP CUBIC; FIFO 0.5/1/2 BDP | Throughput share, JFI, harm, queueing delay |
| E5 | W3, W4 | Short requests (Poisson) during a long flow of each algorithm; mixed open-loop load ρ ∈ {0.3, 0.6} | Short-FCT CDF (p50/p95/p99), harm to the long flow |

Budget: a 60 s run plus setup and cool-down takes about 75 s, so roughly 1,100 long-flow runs fit in a day.
Running only E1 and E2 on the full grid and E3–E5 on slices keeps phase 1 to about a week of machine time.

---

## 7. Logging and validity

Sources, from most to least trusted:
1. **Router pcaps on `$IN` (after the IFB delay) and `$OUT`, one clock.** QUIC payloads are encrypted, so a hash of the first payload bytes identifies each packet.
   - A packet seen on both interfaces gives its time in the queue.
   - A packet seen only on `$IN` was dropped at the bottleneck.
   - These captures are the ground truth for throughput, queueing delay and drops.
2. **Client request CSV** from the patched demo client on S4.
3. **`-F` perflog** on both ends. Always on.
4. **`-b` binlog** on trace runs only (e.g., 1 in 5), converted to qlog offline with `picolog`.
   - In the pilot, check that traced and untraced runs give the same FCT distribution.
5. **Counters before and after each run on all hosts:**
   - `tc -s -j qdisc`;
   - `nstat -az`;
   - `ethtool -S`, `ip -s link`;
   - `pidstat` for the picoquic processes.
6. **`manifest.json` per run:**
   - config and seed;
   - scheduled and actual start times;
   - fork commit, picotls commit, build flags;
   - kernel and sysctls, `ethtool -k`;
   - full `tc qdisc/class/filter show` dump;
   - `chronyc tracking`;
   - exit codes and validity flags.

A run is **invalid** and goes back in the queue (the count is reported in the thesis) if any of these hold:
- a process failed, or a flow did not complete;
- there was any drop outside the bottleneck qdisc and the configured netem loss;
- tcpdump dropped packets;
- CPU went over the threshold;
- the idle-path RTT is off by more than 0.5 ms.

Clock sync: the headline metrics need one clock each (S4 for FCT, S3 for packets). chrony with S3 as the local server is enough to align sender binlogs with router pcaps.

---

## 8. Orchestration and statistics

**Provisioning:** one Ansible playbook covers:
- packages, and the fork built in Release mode at the pinned commit;
- sysctls, CPU governor, NIC offloads, IRQ affinity;
- routes and chrony.

Re-run it before every campaign; it also documents the setup for the appendix.

**Campaigns:**
- One YAML file per campaign, expanded into runs (factors × repetitions).
- Run order shuffled in blocks that contain every arm once.
- The queue lives in SQLite, so a crashed campaign resumes and skips runs that already have a valid manifest.

**Controller:** Python over the management network (asyncssh or Fabric). Each run goes through these steps:
1. Clean up: kill leftover processes, recreate the qdiscs, clear tmpfs.
2. Verify the tc dump, idle RTT and counters.
3. Start the pcaps.
4. Start the servers.
5. Start the client launcher. Add a few ms of random jitter to start times so flows don't phase-lock with drop-tail.
6. Wait for completion, with a hard timeout.
7. Stop the captures, snapshot counters, rsync files to the controller.
8. Validate and write the manifest.
9. Cool down until the backlog is 0.

**Results:**
- Raw files go in `results/<campaign>/<run_id>/…` and are never edited.
- One script turns them into Parquet tables, then figures (master plan §5.1, principle 5).

**Statistics:**
- The run is the unit of replication.
- Run a pilot of about 30 repetitions at the base point and size repetitions from its variance.
- Report median, p95 and p99 with bootstrap confidence intervals over runs.
- Repeat the base point at the start and end of each campaign to check for drift.

---

## 9. Adding Dash to picoquic: fork plan

**Why a fork.** picoquic can take a CC module without core changes. `picoquic_register_congestion_control_algorithms()` accepts a custom table, and `picoquic_set_default_congestion_algorithm()` sets an algorithm by pointer. But that only covers Dash's sender-only mode (DASH_S). The full design needs core changes:
- new frames and a transport parameter;
- CC-driven ACK_FREQUENCY and IMMEDIATE_ACK;
- per-packet chirp timing;
- a receiver-side estimator;
- a size hint.

**Git setup:**
- Fork `private-octopus/picoquic` and keep an `upstream` remote.
- Branch `dash` from the commit the baselines are measured on.
- Add the fork as a submodule `picoquic/` of this repo, replacing `quinn/` in master plan §4.3. Every thesis commit then pins the exact code.
- Pin picotls (picoquic's TLS dependency) too.
- Never merge upstream during a campaign. If a merge is unavoidable, re-run the baselines.
- **Regression gate:** the fork with Dash not selected must match upstream on E1 and E2 at the base point, within noise.
- Keep each change in its own commit, with Dash logic in new files and only narrow hooks in core files. The patch list becomes a thesis appendix.

**Patch set** (master plan §4.2 mapped to picoquic):

| # | Piece | Files |
|---|-------|-------|
| 0 | Measurement: per-request CSV in the demo client | `picohttp/democlient.c` |
| 1 | CC module: `dash.c` implementing `alg_init` / `alg_notify` / `alg_delete` / `alg_observe` | new `picoquic/dash.c`, `picoquic_dash.h`; register in `register_all_cc_algorithms.c` (array of 8 → 9); next free `PICOQUIC_CC_ALGO_NUMBER_*` in `picoquic_internal.h`; add to `CMakeLists.txt` (and `picoquic.vcxproj` only if building with Visual Studio) |
| 2 | Pacing hook: CC sets per-packet departure times for chirps, with no GSO batching for chirps | `pacing.c`, `sender.c`, `sockloop.c` |
| 3 | ACK-frequency hook: CC chooses threshold, max ACK delay and reordering threshold, and requests IMMEDIATE_ACK on chosen packets | ack-frequency code (frame `0xAF`) in `frames.c` / `sender.c` |
| 4 | Negotiation and frames: TP `dash_feedback`; PROBE_TRAIN, TRAIN_REPORT | `picoquic_internal.h` (enums), `transport.c` (TP), `frames.c` (format/decode/skip), `frame_names.c`, `tp_names.c`, log writers. Template: the BDP frame |
| 5 | Receiver estimator (client): records tagged packets, builds reports, raises alarms | new `picoquic/dash_rx.c`, hooked from frame decoding. Kernel RX timestamps (`SO_TIMESTAMPNS`) need a `sockloop.c` change |
| 6 | Size hint API: the server tells the transport the response length (known for `/<N>`) | `picoquic.h` + stream state; called from `demoserver.c` |
| 7 | Warm start: reuse ticket-stored RTT/CWIN and `seed_cwin` | `dash.c` |
| 8 | Logging: `alg_observe` exposes state and rate in perflog/qlog; Dash events in binlog | `dash.c`, log writers |

**Build order** (master plan §4.6, adapted):
1. **Baseline:** pieces 0, plus HyStart++ if picoquic's version isn't RFC 9406.
2. **DASH_S:** sender-only, piece 1 only. It is also the ablation arm "−receiver".
3. **Hooks:** pieces 2, 3, 6.
4. **Extension:** pieces 4, 5.
5. **Full Dash:** plus 7, 8.

Each step is unit-tested first in picoquic's simulator (`picoquictest/`; `cc_compete_test.c` is a model) and then on the testbed.

**Where to develop:** write code and run unit tests on Windows/WSL. Run experiments only on the Linux servers (master plan §5.3.1).

---

## 10. Open checks

1. Is picoquic's slow-start exit RFC 9406 HyStart++? If not, implementing HyStart++ for `cubic` in the fork is the warm-up task (master plan §4.2, item 6), and arm A1 depends on it.
2. Hardware inventory:
   - data NICs per server and their speeds;
   - whether a switch is available;
   - PTP and hardware timestamp support (`ethtool -T`);
   - core counts;
   - kernel version (≥ 6.17 for `dualpi2`).
3. picoquic's default flow-control limits vs the largest BDP (2.5 MB at 100 Mbit/s × 200 ms; more in the 1 Gbit/s sweep).
4. How closely picoquic `bbr` follows BBRv3 (draft-ietf-ccwg-bbr), so the thesis names it correctly.
5. Whether `-0` (no GSO) costs too much CPU at the highest C (calibration item 7).
