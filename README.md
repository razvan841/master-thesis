# Master thesis - Razvan Stefan 5722446

A QUIC congestion control algorithm for short flows, implemented in picoquic.

## Repository layout

| Folder | Contents |
|---|---|
| `thesis/` | LaTeX chapters and bibliography |
| `research_docs/` | Research notes, design and testbed plans |
| `picoquic/` | Fork of [picoquic](https://github.com/private-octopus/picoquic) (git submodule, branch `dash`) with the new algorithm |
| `apps/` | Testbed programs: `wip_server` and `wip_client`, with congestion control logging |
| `testbed/` | Ansible playbooks and the experiment runner for the 4 servers |
| `sim/` | Spec files for the picoquic simulator. qlogs are written to `sim/qlogs/` and are not tracked |

## Getting the code

```sh
git clone --recurse-submodules https://github.com/razvan841/master-thesis.git
cd master-thesis
git -C picoquic checkout dash      # branch with the algorithm work
```

If the repository was cloned without `--recurse-submodules`, run `git submodule update --init` first.

## Building (Linux or WSL)

Install the dependencies once:

```sh
sudo apt update && sudo apt install -y build-essential cmake pkg-config libssl-dev git
```

One build, from the repository root, compiles the picoquic fork, the testbed programs and picoquic's tools:

```sh
cmake -S apps -B build -DCMAKE_BUILD_TYPE=Release -DPICOQUIC_FETCH_PTLS=ON
cmake --build build -j
```

| Program | Path |
|---|---|
| Testbed server and client | `build/wip_server`, `build/wip_client` |
| Simulator | `build/picoquic/pico_sim` |
| picoquic's demo client/server | `build/picoquic/picoquicdemo` |
| Binary log converter | `build/picoquic/picolog_t` |

- **Internet access:** the first configure step downloads picotls, the TLS library picoquic depends on.
- **Rebuilding:** after a code change, run only the second command.
- **Build folder:** `build/` is ignored by the repository's `.gitignore`. Don't run `cmake .` inside `picoquic/`, as picoquic's own README suggests: it builds inside the source tree and makes the submodule look modified.
- **picoquic's test suite:** add `-Dpicoquic_BUILD_TESTS=ON` to the first command to also build `build/picoquic/picoquic_ct`.
- **WSL:** building on the Windows file system (`/mnt/c/...`) works but is slower. You can build into your Linux home instead (`-B ~/thesis-build`) and adjust the paths below.

To check that the new algorithm is compiled in, run `build/picoquic/picoquicdemo -h`. The line under `-G cc_algo` should end with `wip`.

## Running the simulator

`pico_sim` runs real picoquic client and server code over a simulated link, in virtual time. Run it from the repository root:

```sh
mkdir -p sim/qlogs
build/picoquic/pico_sim -S picoquic sim/cubic_1MB.txt
build/picoquic/pico_sim -S picoquic sim/wip_1MB.txt
```

- **`-S picoquic`:** the path to the picoquic source folder, where the simulator finds its test certificates. The run fails without it, unless you run from inside `picoquic/` with `-S .`.
- **`-N <n>`:** repeats the run `n` times with different random seeds.

### Output

Each run prints one line, for example:

```
Success, 0, 326385, 0, 0, wip_1MB
```

| Field | Meaning |
|---|---|
| `Success` / `Fail` | `Fail` if the main connection took longer than `main_target_time`, or ended with an error |
| `0` | Repeat number (with `-N`) |
| `326385` | Completion time of the main connection, in simulated microseconds |
| `0, 0` | Average and maximum media delay (0 for file transfers) |
| `wip_1MB` | Name of the spec file |

The completion time is when the main connection finishes, including its closing exchange. For the exact time of the last byte, read the qlog.

Example: a 1 MB download over 100 Mbit/s with a 50 ms RTT and a 1 BDP buffer takes 374 ms with `cubic` and 326 ms with `wip`.

### Spec files

A spec file has one `name: value` per line. Blank lines are allowed, but comments are not: any other line is an error. Example, [sim/wip_1MB.txt](sim/wip_1MB.txt):

```
main_cc_algo: wip
main_scenario_text: =b1:*1:397:1000000;
main_start_time: 0
main_target_time: 30000000
nb_connections: 1
data_rate_in_gbps: 0.1
latency: 25000
queue_delay_max: 50000
qlog_dir: sim/qlogs
```

All times are in microseconds.

| Parameter | Meaning | Default |
|---|---|---|
| `main_cc_algo` | Algorithm of the main connection, by name (`cubic`, `bbr`, `newreno`, `wip`, ...) | |
| `main_scenario_text` | What the main connection transfers, as a quicperf scenario. `=b1:*1:397:1000000;` is one stream named `b1`, run once, with a 397-byte request and a 1,000,000-byte response. Syntax: `picoquic/doc/quicperf.md` | |
| `main_start_time` | When the main connection starts | 0 |
| `main_target_time` | Pass/fail threshold. **Required**: if it's left at 0, every run reports `Fail` | 0 |
| `nb_connections` | Total connections, 1 to 5 (1 main + up to 4 background) | |
| `background_cc_algo`, `background_scenario_text`, `background_start_time` | The same settings for the background connections, which all share them | |
| `main_cc_options`, `background_cc_options` | Option string passed to the algorithm | |
| `data_rate_in_gbps` | Bottleneck rate in both directions (`0.1` = 100 Mbit/s) | 0.01 |
| `latency` | One-way delay; the RTT is twice this | 10000 |
| `queue_delay_max` | Buffer size, expressed as the longest queueing delay before packets are dropped (buffer in bytes = rate × this). 0 means no limit | 0 |
| `jitter`, `wifi_jitter` | Delay jitter; `wifi_jitter: 1` switches from Gaussian to Wi-Fi-like jitter | 0 |
| `l4s_max` | Turns on an L4S dual queue with this marking threshold | 0 |
| `seed_cwin`, `seed_rtt` | Start the server from a remembered window (bytes) and RTT, as on a resumed connection | |
| `qlog_dir` | Folder for qlogs, relative to where you run `pico_sim`. **It must exist** | none |
| `link_scenario` | Makes the link change during the run (see below) | none |

`link_scenario` takes either a preset (`black_hole`, `drop_and_back`, `low_and_up`, `wifi_fade`, `wifi_suspension`) or your own phases. Phases are separated by `;`, and the list repeats from the start after the last phase. Each phase is `duration:D<rate down>:U<rate up>:L<latency>:J<jitter>:Q<queue_delay_max>:S<l4s_max>:B<losses per burst>:P<packets between bursts>`, with the duration in microseconds and rates in Gbit/s.

### Limits of the simulator

- All connections share one bottleneck link per direction, so all flows have the same RTT.
- At most 5 connections. All background connections use the same algorithm, scenario and start time.
- Drops are tail-drop by queueing delay, or the L4S dual queue. There is no CoDel or FQ-CoDel.
- Losses come as regular bursts (`B` packets every `P`), not at random.
- No UDP batching (GSO), no CPU cost and no timer jitter, so pacing is perfect.
- The run stops when the main connection ends; background connections are cut off.

The simulator is for fast, repeatable development. Results for the thesis come from the testbed.

## Testbed programs: `wip_server` and `wip_client`

`wip_server` is an HTTP/3 server that uses one congestion control algorithm, chosen at startup, for every connection. `wip_client` downloads documents from it and records the timing of every request. Both accept the standard picoquic options (the same as `picoquicdemo`; `-h` lists them) plus the long options below. The source is in `apps/`.

### Quick test on one machine

```sh
mkdir -p logs
build/wip_server -G wip --cc-log logs/server_cc.csv -F logs/server_perf.csv &
build/wip_client --sizes 10000,100000,1000000 --repeat 3 --req-log logs/client_req.csv 127.0.0.1
kill %1     # SIGTERM: the server closes its connections and writes its logs
```

### `wip_server`

```sh
build/wip_server -G <algorithm> [options]
```

| Option | Meaning |
|---|---|
| `-G <name>` or `--cc <name>` | Congestion control algorithm for every connection. Required. An unknown name stops the server and prints the list of algorithms |
| `-p <port>` | UDP port (default 4433) |
| `-w <dir>` | Also serve files from this folder |
| `-c <cert> -k <key>` | Certificate and key (default: picoquic's test certificate in `picoquic/certs/`) |
| `--cc-log <file.csv>` | Congestion control samples of every open connection (see below) |
| `--sample-us <us>` | Sampling interval for `--cc-log` (default 10000 = 10 ms). Use a fraction of the RTT for short flows |
| `--label <text>` | Written in the first column of `--cc-log`, e.g. a run ID |
| `-F <file.csv>` | One summary line per connection (picoquic's performance log) |
| `-q <dir>`, `-b <dir>` | One qlog / binary log per connection, in an existing folder |
| `-0` | Don't use UDP GSO |

What it serves:
- **`GET /<N>`:** N bytes of generated content, e.g. `GET /1000000`. Nothing is read from disk.
- **`GET /<file>`:** a file from the `-w` folder.
- **Any other path:** a 404.

The server uses picoquic's demo ALPN selection, so it also accepts HTTP/0.9 (`hq-interop`) and quicperf (`perf`) connections. It runs until it receives SIGINT or SIGTERM.

### `wip_client`

```sh
build/wip_client [options] <server> [port]
```

| Option | Meaning |
|---|---|
| `--sizes <n1,n2,...>` | Request generated documents of these sizes (`GET /<n>`) |
| `--paths <p1,p2,...>` | Request these paths instead |
| `--repeat <n>` | Repeat the list n times (default 1) |
| `--per-cnx <k>` | Requests per connection. 1 (default): every request on a new connection (a cold start). 0: all requests on one connection |
| `--parallel` | Send the requests of a connection all at once (default: one after the other) |
| `--gap-ms <ms>` | Idle time between connections (default 0) |
| `--timeout-s <s>` | Give up on a connection after this long (default 60) |
| `--req-log <file.csv>` | One line per request with all timings (see below) |
| `--label <text>` | Written in the first column of `--req-log` |
| `-F <file.csv>`, `-q <dir>`, `-b <dir>` | Same as on the server |
| `-t <file>` | Verify the server certificate against this root. Without it, the certificate isn't checked (testbed only) |

- **Cold starts:** every connection uses a fresh QUIC context, so no session ticket or path state carries over between connections.
- **Exit code:** 0 if every request returned status 200 with the expected number of bytes, 1 otherwise.

### Log files

**`--cc-log` (server).** One row per open connection every sampling interval:

| Column | Meaning |
|---|---|
| `label` | The `--label` text |
| `time_us` | Sample time, µs since the epoch (server clock) |
| `cnx_time_us` | Time since the connection started, µs |
| `cnx_id` | Connection ID (see "Matching the logs" below) |
| `state` | picoquic connection state: 12 = handshake, 14 = ready, 15–18 = closing or draining |
| `cc_algo` | Algorithm name |
| `cwin`, `bytes_in_transit` | Congestion window and bytes in flight, in bytes |
| `smoothed_rtt_us`, `rtt_min_us`, `rtt_sample_us`, `rtt_variant_us` | RTT estimates. Before the first RTT sample, picoquic reports its initial value (250 ms) |
| `pacing_rate_Bps` | Pacing rate, bytes/s |
| `bandwidth_estimate_Bps`, `peak_bandwidth_estimate_Bps` | picoquic's delivery-rate estimates, bytes/s |
| `bytes_sent`, `packets_sent` | Totals on the path |
| `packets_lost`, `timer_losses`, `spurious_losses` | Packets declared lost; of those, found by timer, and later found not lost |
| `slow_start_exited` | 1 once the algorithm has left its initial slow start |
| `cc_state`, `cc_param` | Algorithm-specific. For `cubic` and `wip`: `cc_state` 0 = slow start, 1 = recovery, 2 = congestion avoidance; `cc_param` = W_max in packets (a huge value until the first congestion event) |

**`--req-log` (client).** One row per request:

| Column | Meaning |
|---|---|
| `label`, `cnx_index`, `cnx_id`, `req_index`, `stream_id`, `path` | Identification |
| `expected_bytes`, `body_bytes`, `status`, `ok` | Expected size (−1 if unknown), bytes received, HTTP status, and 1 if the request succeeded |
| `t_cnx_start` | Connection created; the first Initial is sent right after |
| `t_handshake` | Handshake done on the client side |
| `t_ready` | Handshake confirmed by the server |
| `t_request` | Request handed to QUIC |
| `t_first_byte`, `t_last_byte` | First and last byte of the response |
| `ttfb_us` | `t_first_byte − t_request` |
| `fct_req_us` | `t_last_byte − t_request` |
| `fct_cnx_us` | `t_last_byte − t_cnx_start`, i.e. including the handshake |
| `error` | Why a request failed |

The `t_*` columns are µs since the epoch on the client's clock.

**`-F` (both sides).** picoquic's performance log, one line per connection. It includes duration, bytes and Mbps sent and received, packets sent, retransmissions (`retrans.`), spurious retransmissions, final smoothed and minimum RTT, final cwnd, and the algorithm number `ccalgo` (1 = newreno, 2 = cubic, 5 = bbr, 7 = bbr1, 9 = wip). The column names are in the file's first line.

**Matching the logs.**
- **Same ID on both sides:** `cnx_id` is the same on the client and the server for a given connection.
- **qlog / binlog files:** their names start with it, e.g. `017a0d6ba8cfc77b.5af1.server.qlog`.
- **`-F` logs:** the `CNX_ID` column shows it with a `0x` prefix.
- **Clocks:** durations are always valid. Absolute times from different machines can only be compared if their clocks are synchronized (chrony or PTP).

## Experiments on the testbed

### Topology

| Host | Role | Management IP | Experiment link |
|---|---|---|---|
| mickey | sender A (`wip_server`) | 10.10.2.37 | `enp101s0f3`, 13.37.1.1 ↔ daisy `enp101s0f1`, 13.37.1.254 |
| minnie | sender B (`wip_server`) | 10.10.2.36 | `enp101s0f3`, 13.37.2.1 ↔ daisy `enp101s0f2`, 13.37.2.254 |
| daisy | router: emulated bottleneck, packet captures | 10.10.2.5 | three links, above and below |
| donald | receiver (`wip_client`), Ansible control node, runner | 10.10.2.22 | `enp101s0f3`, 13.37.3.1 ↔ daisy `enp101s0f3`, 13.37.3.254 |

- **Two networks:** SSH, Ansible and the runner use the management network (10.10.2.x). Test traffic only uses the direct 1 Gbps links through daisy.
- **One description:** [testbed/inventory.yml](testbed/inventory.yml) describes all of this and is read by both Ansible and the runner.
- **The bottleneck, set by the runner before each run on daisy:**
  - toward donald: a rate limit (HTB) plus a drop-tail buffer (`bfifo`);
  - toward the senders: the extra delay (netem).

  The delay never shares a queue with the bottleneck. Every exchange crosses both directions, so the RTT grows by exactly the configured value.

### Reaching the servers from your laptop

You need eduVPN when off campus. With this in `~/.ssh/config`, `ssh donald` goes through both jump hosts:

```
Host tud-jump
    HostName student-linux.tudelft.nl
    User <netid>
Host lichking
    HostName lichking.ewi.tudelft.nl
    User <rack-user>
    ProxyJump tud-jump
Host donald
    HostName 10.10.2.22
    User <rack-user>
    ProxyJump lichking
```

### One-time setup (on donald)

The university's security rules require Ansible to run from one of the 4 servers, so donald is the control node:

```sh
ssh donald
sudo apt update && sudo apt install -y ansible git python3-yaml
git clone https://github.com/razvan841/master-thesis.git ~/master-thesis
ssh-keygen -t ed25519                                       # Enter for the defaults
for h in 10.10.2.37 10.10.2.36 10.10.2.5; do ssh-copy-id $h; done
cd ~/master-thesis/testbed
ansible-playbook site.yml -K                                # -K: your sudo password, asked once
```

`ssh-copy-id` asks for your password on each server once. After that, Ansible and the runner log in with the key.

`site.yml` does the following:
1. Installs the build tools and network utilities.
2. Checks out and builds the code on all 4 hosts.
3. Lets your user run `tc`, `tcpdump` and `pkill` with `sudo -n`: the runner can't type a password. This is a sudoers file, `/etc/sudoers.d/thesis-testbed`.
4. Raises the UDP socket buffer limits and sets the performance CPU governor.
5. Configures the experiment addresses, routes and offloads, and IP forwarding on daisy.
6. Checks with ping that both senders reach donald through daisy.

Steps 4 and 5 are runtime settings: a reboot undoes them, and running `site.yml` again restores them.

### Two checkouts on donald

| Folder | Purpose |
|---|---|
| `~/master-thesis` | Your control checkout: the playbooks, the runner, and `results/` |
| `~/thesis-deploy` (every host) | The code that runs in the experiments, at `thesis_version`. Ansible manages it; don't edit it |

### Deploying a new version

1. On your laptop, commit and push: the picoquic fork's `dash` branch, then the thesis repo with the updated submodule.
2. On donald:
   ```sh
   cd ~/master-thesis && git pull
   cd testbed && ansible-playbook site.yml -K -e thesis_version=<commit or branch>
   ```

Hosts whose version changed rebuild automatically; the others are left alone. You can also set `thesis_version` in `inventory.yml` instead of passing `-e`.

### Running experiments

```sh
cd ~/master-thesis/testbed
python3 runner.py check                          # SSH, sudo, same code on all hosts, experiment paths
python3 runner.py plan campaigns/smoke.yml       # list the runs
python3 runner.py run  campaigns/smoke.yml       # run them
```

A campaign file ([testbed/campaigns/](testbed/campaigns/)) sets:
- **`sender`:** which sender runs the server.
- **`network`:** `rate_mbit`, `rtt_ms`, and `buffer_bdp` (buffer in multiples of the BDP).
- **`algorithms` and `repetitions`.**
- **`workload`:** passed to `wip_client`: `sizes`, `repeat`, `per_cnx`, `gap_ms`.
- **`logging`:** the CC sampling interval, pcaps on daisy, qlogs.
- **`pinning`** (optional): CPU cores for the server and client.

Each repetition runs every algorithm once, in a random order, so slow drifts of the testbed don't line up with one algorithm.

Each run:
1. Stops anything an earlier run left behind.
2. Configures the bottleneck and checks the RTT with ping.
3. Starts captures on daisy (before and after the bottleneck) and `wip_server` on the sender.
4. Runs `wip_client` on donald.
5. Stops everything and copies all files to donald.

Results go to `~/master-thesis/results/<campaign>/` (not tracked by git):

| File | Contents |
|---|---|
| `campaign.yml`, `testbed.json` | The campaign as run; code commits and kernels of the hosts |
| `runs.csv` | One line per run: algorithm, valid, requests OK, median FCT, bottleneck drops, failed checks |
| `<run_id>/manifest.json` | Full configuration, tc commands, ping, UDP error counters, capture drops, validity checks |
| `<run_id>/donald/` | `client_req.csv`, `client_perf.csv`, `client.out` |
| `<run_id>/<sender>/` | `server_cc.csv`, `server_perf.csv`, `server.out`, qlogs if enabled |
| `<run_id>/daisy/` | `bottleneck_out.pcap`, `sender_in.pcap`, tcpdump logs, `tc -s` before and after |

A run is **valid** only if all of these hold:
- the client exited cleanly and every request succeeded;
- the server stopped cleanly;
- the measured RTT matches the campaign within 1 ms;
- no UDP socket buffer drops on the endpoints;
- no packets dropped by the captures;
- the bottleneck qdisc was in place.

The runner skips runs that already have a manifest, so an interrupted campaign continues where it stopped. `--rerun-invalid` repeats the invalid ones, and Ctrl-C stops the server and captures before exiting.

### Undoing the setup

```sh
ansible-playbook teardown.yml -K
```

This removes the qdiscs, experiment addresses and routes, and the sudoers file, and restores IP forwarding on daisy. The deployed code and the packages stay. A reboot resets everything else.

## Changes to picoquic: the `wip` algorithm

`wip` is a placeholder for the new algorithm. It is a copy of picoquic's CUBIC (`cubic.c`, without the `dcubic` variant) with one change. In slow start, each acknowledged byte adds 3 bytes to the window instead of 1, so the window quadruples every RTT instead of doubling. Its purpose is to check that a new algorithm is registered and actually used.

The two main files:

| File | Change |
|---|---|
| `picoquic/picoquic/wip.c` | **New.** The algorithm itself. The algorithm record at the end of the file defines its name `"wip"`, its number and its functions (init, notify, delete, observe) |
| `picoquic/picoquic/register_all_cc_algorithms.c` | Adds `picoquic_wip_algorithm` to the list of registered algorithms (the list grows from 8 to 9). This is what makes `-G wip` in `picoquicdemo` and `main_cc_algo: wip` in `pico_sim` work |

Also needed:

| File | Change |
|---|---|
| `picoquic/picoquic/picoquic_wip.h` | **New.** Declares `picoquic_wip_algorithm` for the registry file |
| `picoquic/picoquic/picoquic_internal.h` | Algorithm number `PICOQUIC_CC_ALGO_NUMBER_WIP = 9` (1–7 are defined here; 8 is C4, defined in `c4.c`) |
| `picoquic/CMakeLists.txt` | Compiles `wip.c` and lists `picoquic_wip.h` |
| `picoquic/picoquic/picoquic.vcxproj`, `.vcxproj.filters` | Visual Studio build only; not needed on Linux |

Adding another algorithm takes the same steps:
1. A new `.c` file with a `picoquic_congestion_algorithm_t` record.
2. A header declaring it.
3. A new algorithm number.
4. An entry in `register_all_cc_algorithms.c`, with the list size raised by one.
5. The `.c` file added to `CMakeLists.txt`.

## Other changes to picoquic

| File | Change |
|---|---|
| `picoquic/picoquic/performance_log.c` | Fixes two bugs in the `-F` performance log. Both lost summary lines whenever connections overlapped, which back-to-back test connections always do. (1) When a line was queued, the end-of-queue pointer was not advanced, so each new line replaced the previous one. (2) Lines still queued at shutdown were thrown away; they are now written. picoquic's own performance log tests still pass |
