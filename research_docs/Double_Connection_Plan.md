# Thesis master plan

Working title: **Sprint: an ECN-assisted, receiver-informed startup for short QUIC flows on a dual-lane
connection architecture**

"Sprint" (the congestion control algorithm, CCA) and "dual-lane" (the two-connection architecture) are working
names. The research notes in [README.md](README.md) are the background this plan builds on, and are referred to
as "README §X".

---

## 0. Summary

**Problem.** Most transfers are short. A 100 KB–5 MB object on a high-BDP path spends almost its whole lifetime
in slow start, which needs O(log2(BDP/IW)) round trips to reach the bottleneck rate. The model in §1.3 puts
slow-start FCT at **3–7x** the ideal for 100 KB–5 MB on 100 Mbit/s × 200 ms and 1 Gbit/s × 100 ms paths.

**Approach.**

1. **Architecture (dual-lane).** Each client keeps two QUIC connections to the server:
   - an **express lane** for short objects, running the new CCA (Sprint);
   - a **bulk lane** for long objects, running unmodified CUBIC + HyStart++.

   The server knows each file's size, so it steers every response to the right lane without an extra RTT.
   The two lanes share path state, and optionally a coupled congestion budget, so the pair does not take
   twice its fair share.
2. **Algorithm (Sprint).** Sprint replaces slow start with rate-based probing:
   - From **ROS** it takes the receiver-side bandwidth measurement (probe trains of fixed duration, measured
     where ACK thinning cannot distort them), the cautious 40 % first step, and a receiver cap.
   - From **Nimble Start** it takes the probe rounds split into a slow half and a fast half, the plateau exit,
     BBR's loss criterion (2 % and ≥ 6 ranges) with a 0.7 cut, and the fallback to slow start.
   - **ECN** gives the earliest and most precise exit and correction signal: CE in the fast half means the
     bottleneck is reached, and CE in the slow half triggers a proportional, DCTCP-style cut. ECN also helps
     tell congestion loss from random loss. Optionally, the express lane uses ECT(1) to enter the L4S
     low-latency queue.
   - Startup takes **O(1)** rounds instead of O(log BDP).
3. **Evaluation.** An emulated Linux dumbbell built from network namespaces, tc and netem. It covers four
   queue types (FIFO, FQ-CoDel, CoDel, DualPI2), with and without ECN, BDP-scaled buffers, eight workloads and
   strong baselines (HyStart++, BBR, a single connection with stream priorities). Measurements include FCT,
   overshoot, loss, queueing delay, harm to competing flows, and robustness when ECN is missing or broken,
   which is the normal case on the Internet.

**Implementation.** A fork of **quinn** (Rust), plus a minimal file server and client in Rust and a Python
harness. Sprint is built in stages (v0 sender-only, then v1 receiver feedback, v2 fine-grained ECN and L4S,
v3 coupling), so every stage produces publishable results even if time runs out.

---

## 1. Problem, scope, research questions

### 1.1 Problem statement

- Slow start doubles cwnd per RTT from IW = 10 packets (README §Congestion control). Time to fill the pipe is
  about log2(BDP/IW) RTTs, independent of how small the transfer is. A short flow finishes *before* reaching
  the bottleneck rate, so its FCT is set by RTT count, not by bandwidth.
- Existing fixes either:
  - stay in the doubling regime and only change the exit (HyStart, HyStart++, SEARCH);
  - accelerate growth but inherit HyStart's thresholds (SUSS, Rapid Start);
  - need history from a previous connection (Careful Resume, hStart, Stateful-TCP);
  - or need router support (Quick-Start, XCP, ABC).
- QUIC is the right place to try something new (README §Relevance to thesis):
  - user-space deployment;
  - exact ECN counters in ACK frames;
  - extension frames and transport parameters for new signalling;
  - per-connection CC, so two connections can run two different CCAs.

### 1.2 Thesis claim (to be validated, not assumed)

> Separating short and long transfers into two QUIC connections lets each class run a CCA tuned for it. A
> startup that estimates bottleneck bandwidth at the receiver and uses ECN as its primary congestion signal can
> bring short-flow FCT close to the ideal (1 RTT + size/C) on high-BDP paths. It does so without more loss,
> more queueing or more harm to competing traffic than standard slow start, and degrades gracefully to
> HyStart++-like behaviour when ECN or receiver support is missing.

### 1.3 Headroom analysis (motivation and grid design)

Round-based model (scratch script, easy to reproduce):
- slow start from IW = 12 000 B, ×2 per RTT, no loss, on an established connection;
- compared with ideal = RTT + S/C.

Each cell is FCT_slowstart / FCT_ideal.

| size \ path | 10 Mbit/20 ms | 50 Mbit/50 ms | 100 Mbit/50 ms | 100 Mbit/200 ms | 1 Gbit/100 ms |
|-------------|---------------|---------------|----------------|-----------------|---------------|
| BDP         | 25 KB         | 312 KB        | 625 KB         | 2.5 MB          | 12.5 MB       |
| 100 KB      | 1.1x          | 3.1x          | 3.5x           | 3.9x            | 4.0x          |
| 250 KB      | 1.1x          | 2.9x          | 3.7x           | 4.6x            | 4.9x          |
| 500 KB      | 1.0x          | 2.5x          | 3.4x           | 5.0x            | 5.8x          |
| 1 MB        | 1.0x          | 1.9x          | 2.8x           | 5.1x            | 6.5x          |
| 2 MB        | 1.0x          | 1.5x          | 2.1x           | 4.6x            | 6.9x          |
| 5 MB        | 1.0x          | 1.2x          | 1.5x           | 3.3x            | 6.5x          |
| 10 MB       | 1.0x          | 1.1x          | 1.3x           | 2.4x            | 5.7x          |
| 20 MB       | 1.0x          | 1.1x          | 1.1x           | 1.8x            | 4.5x          |
| 50 MB       | 1.0x          | 1.0x          | 1.1x           | 1.3x            | 2.8x          |

Takeaways that shape the whole thesis:
- **Gains only exist where size ≲ a few × BDP.** Low-BDP paths (first column) are a *negative control*: Sprint
  must not hurt there.
- **A fixed byte threshold for "short" is wrong.** The short/long boundary moves from about 2 MB to above
  50 MB across these paths. A BDP-relative threshold is a research question (RQ5).
- The model is idealized (no loss, no delayed ACKs, no HyStart early exit). Real slow start with overshoot and
  loss is usually *worse*, so this table understates the headroom for mid-size flows.

### 1.4 Research questions and hypotheses

| RQ | Question | Hypothesis (calibrate the numbers after the pilot, W14) |
|----|----------|------------------------------------------------------------|
| RQ1 | Does splitting short and long transfers into two QUIC connections reduce short-flow FCT, under which bottleneck queue disciplines, and at what cost (fairness, overhead)? | H1: with a concurrent bulk transfer, dual-lane cuts median short-flow FCT clearly under FQ-CoDel and DualPI2, but barely under FIFO unless the lanes are coupled. |
| RQ2 | Can a receiver-informed, ECN-assisted startup (Sprint) reduce FCT of 100 KB–few MB transfers compared with CUBIC + HyStart++ and BBR Startup, without more loss or queueing? | H2: on idle paths with BDP ≥ 100 packets, Sprint captures ≥ 50 % of the headroom in §1.3 for 0.5–5 MB, with loss ≤ HyStart++. |
| RQ3 | How much does ECN (classic CE or L4S CE) improve exit accuracy and overshoot compared with delay/loss signals only? How does Sprint degrade without ECN? | H3: with a marking AQM, peak startup queue is ≤ 50 % of slow start's. Without ECN, Sprint is no worse than HyStart++. |
| RQ4 | What is Sprint's harm to competing CUBIC and BBR flows? Does lane coupling bound the aggregate aggressiveness of a dual-lane client? | H4: Sprint's throughput and delay harm is ≤ the harm a CUBIC flow of the same size causes. With coupling, a dual-lane client harms others no more than one connection does. |
| RQ5 | Where should the short/long threshold lie: fixed bytes or BDP-relative? | H5: T = c × BDP_est beats any fixed T across the RTT × BW grid. |

### 1.5 Expected contributions

1. A dual-lane QUIC architecture that steers responses on the server side (no redirect RTT), shares path state
   across lanes, and can couple the lanes' congestion budgets.
2. Sprint, a startup algorithm for QUIC that combines:
   - receiver-side probe-train rate estimation (ROS);
   - two-rate probing rounds with plateau and loss exits (Nimble Start);
   - ECN as a proportional exit, correction and loss-differentiation signal.

   It comes with a QUIC extension (transport parameter plus two frames) and a sender-only fallback.
3. An open-source implementation as a quinn fork, plus a reusable, calibrated QUIC CC testbed and harness.
4. A systematic evaluation that separates the effects of the **architecture**, the **algorithm** and the
   **network** (FQ vs FIFO, ECN vs no ECN, L4S), including harm and robustness.

### 1.6 Scope and non-goals

- In scope: download direction (server → client), QUIC v1, emulated networks, objects from 100 KB to 50 MB.
- Out of scope (future work): HTTP/3 prioritization semantics, multipath QUIC, real deployment at scale,
  learning-based CC, upload direction, datacenter RTTs.
- Optional, if time allows: an L4S mode (ECT(1) plus a scalable response), real-Internet validation runs.

---

## 2. Literature: what is covered and what to add

Already covered in README: QUIC RFCs 9000/9001/9002, Reno, CUBIC, Vegas, BBRv3 draft, Aurora, HyStart, SUSS,
Fair Queueing, Sizing Router Buffers, Nimble Start, ROS.

**Note on ROS and Nimble Start.** Both are unpublished. Agree with your supervisor how to cite them (for
example "under submission" or "personal communication"). The thesis must describe every component reused from
them in enough detail to stand on its own (§3.6 does this).

Reading list. Priority A = needed for design or evaluation decisions; B = needed for related work;
C = nice to have.

| Pri | Topic | Work | Why it matters here |
|-----|-------|------|---------------------|
| A | Baseline startup | RFC 9406 HyStart++ | The baseline to beat. quinn's CUBIC does **not** implement it, so you must add it (§4.2). |
| A | Evaluating a new CCA | RFC 9743 *Specifying New Congestion Control Algorithms* (BCP 133, 2025) | The IETF checklist for what a new CCA must be tested against. Use it to justify the evaluation scope (§3.10). |
| A | ECN in practice | Sander et al., *ECN with QUIC: Challenges in the Wild*, IMC 2023 | < 2 % of QUIC hosts pass ECN validation. So no-ECN behaviour is the common case and must be first-class (RQ3). |
| A | ECN semantics | RFC 3168, RFC 8311 (relaxed ECN rules for experiments), RFC 8257 DCTCP, RFC 8511 ABE | What a CE response may and must do. RFC 8311 is what allows a non-halving CE response. |
| A | L4S | RFC 9330 / 9331 / 9332 (architecture, ECT(1) identifier, DualQ Coupled AQM) | ECT(1) on the express lane, DualPI2 in the testbed (Linux ≥ 6.17 ships `sch_dualpi2`). |
| A | ECN-based fast start | Misund & Briscoe, *Paced Chirping*, INFOCOM WKSHPS 2019 | Closest prior idea to "probe with chirps, read the queue via (L4S) marks". Must be cited and contrasted. |
| A | Cached-state startup | RFC 9959 *Careful Resume* (May 2026); Fischer et al., *Getting up to Speed with Neqo and Careful Resume*, ANRW 2025 | Warm-start comparison and safety design (jump to a fraction of the cached state, then validate or retreat). Neqo is also Rust. |
| A | Fairness metric | Ware et al., *Beyond Jain's Fairness Index*, HotNets 2019 | The "harm" framework used for RQ4. |
| A | Bandwidth estimation | Keshav, packet-pair (SIGCOMM 1991); Dovrolis et al., *Packet-Dispersion Techniques*, ToN 2004; Ribeiro et al., *pathChirp*, PAM 2003 | Theory behind the receiver-side estimate: what train dispersion measures (capacity vs available bandwidth) and why FQ makes packet-pair report the fair share. |
| B | Startup in IETF | draft-chung-ccwg-search-09 (SEARCH); draft-kazuho-ccwg-rapid-start-02 (expired) | Competing slow-start replacements. Candidate extra baselines. |
| B | QUIC extensions | draft-ietf-quic-ack-frequency-14; draft-ietf-quic-receive-ts-00; draft-seemann-quic-accurate-ack-ecn-01 | ACK thinning control (quinn implements ack-frequency, which is useful for emulating ACK suppression); receive timestamps as an alternative to ROS's receiver-side estimate; per-packet ECN attribution. |
| B | Short-flow aggression | RC3 (NSDI 2014), Halfback (CoNEXT 2015), Jump Start (PFLDnet 2007), RFC 4782 Quick-Start, Rüth et al. *Blitz-starting QUIC Connections* (arXiv 2019) | Prior answers to "short flows should go faster". |
| B | Receiver-driven transport | Homa (SIGCOMM 2018), NDP (SIGCOMM 2017), ExpressPass (SIGCOMM 2017) | Related to ROS's "the receiver knows best" idea (datacenter context). |
| B | Size-aware scheduling | pFabric (SIGCOMM 2013), PIAS (NSDI 2015) | The SRPT intuition behind express/bulk separation, and the slowdown-per-size-bucket evaluation style. |
| B | Sharing CC state across connections | RFC 9040 (TCP Control Block Interdependence), RFC 3124 (Congestion Manager), RFC 8699 (coupled CC for RTP) | Basis of lane coupling (§3.8). |
| B | Explicit feedback | Goyal et al., *ABC*, NSDI 2020 | Uses the ECN field as an accelerate/brake signal. Closest "creative ECN" prior work. |
| C | History-based startup at scale | hStart, *Multi-CDN as a Collective Service: Towards Hot Start in CC at Scale*, APNet 2026 | Very recent; QUIC-based. |
| C | QUIC implementation behaviour | Marx et al., *Same Standards, Different Decisions*, EPIQ 2020; Jaeger et al., *QUIC on the Highway*, IFIP Networking 2023 | Implementation diversity; CPU limits of user-space QUIC at high rates (a testbed threat). |
| C | Testbeds | Mahimahi (ATC 2015), Pantheon (ATC 2018), RFC 7928 (AQM characterization) | Methodology references. |

---

## 3. Design

### 3.1 Overview

```
 CLIENT                                                         SERVER
 +-------------------------+                      +---------------------------------------+
 | app: fetch(name)        |                      | app: file store (knows sizes)         |
 |  lane selector          |   express lane       |  steering: size <= T ? express : bulk |
 |  + session id           |==== QUIC conn E =====|  PathState(session) shared by lanes   |
 |                         |  :4434, ALPN fsx/1   |   - min_rtt, bw_est, ecn_ok, ...      |
 |  receiver-side Sprint   |  CC = Sprint         |   - coupling budget                   |
 |  estimator (ROS part)   |                      |                                       |
 |                         |   bulk lane          |                                       |
 |                         |==== QUIC conn B =====|                                       |
 |                         |  :4433, ALPN fsb/1   |                                       |
 +-------------------------+  CC = CUBIC+HyStart++ +---------------------------------------+
```

### 3.2 Dual-lane architecture

- **Two QUIC connections per client session.**
  - E-conn (express): server UDP port 4434, ALPN `fsx/1`, CC = Sprint.
  - B-conn (bulk): server UDP port 4433, ALPN `fsb/1`, CC = CUBIC + HyStart++ (or BBR as a variant).
- **Why separate server ports.** Each lane gets its own quinn `Endpoint`, so its own `ServerConfig`,
  `TransportConfig` and CC factory, with no need to work out the lane per connection inside one endpoint. It
  also gives distinct 5-tuples, which FQ bottlenecks isolate.
- **Session binding.** After the handshake, the client sends `HELLO{session_id}` (128-bit random) on a control
  stream of each connection. The server keys a `PathState` record on `session_id` (§3.8). Do not key on the
  client IP:port, which breaks behind NAT.
- **Establishment policy.**
  - *Eager* (default): open both connections at session start, in parallel. This costs one extra handshake
    (CPU) but adds no latency. The E-conn handshake also gives an RTT sample before the first request.
  - *Lazy* (variant): open B-conn on the first large object. This costs one RTT, or zero with 0-RTT, because
    the session ticket from E-conn resumes B-conn to the same server.
- **Why two connections and not one connection with two streams.** README §Relevance to thesis already flags
  this; the thesis must argue it explicitly:
  - (+) QUIC has one congestion controller per path. Running two CCAs on one connection needs deep stack
    changes, and the lane separation *is* the CCA separation.
  - (+) The network can tell the lanes apart: separate FQ queues, and a different ECN codepoint per lane
    (ECT(1) for express means the L4S queue under DualPI2).
  - (+) Sender-side isolation: a burst of bulk data cannot starve express data inside one send loop.
  - (−) Two controllers mean twice the fair share at an FQ bottleneck while both lanes are active, and two
    handshakes. Coupling (§3.8) answers the first; overhead is measured.
  - (−) A single connection with stream priorities inherits the bulk lane's already-open cwnd, which is a
    strong competitor in the "short object during a bulk download" case. It is therefore baseline **A1p**
    (§5.4), not a strawman.

### 3.3 Lane selection and steering

The client usually does not know object sizes; the server always does. Three policies, compared in W8:

| Policy | Mechanism | Cost of a misroute |
|--------|-----------|--------------------|
| P1 redirect (README sketch) | Every request goes on E. If size > T, the server answers `REDIRECT{id}` and the client re-requests on B. | +1 RTT for every large object |
| **P2 server steering (default)** | Every request goes on E. If size > T, the server answers `MOVED{id, size}` on E and *immediately* opens a server-initiated unidirectional stream on B carrying `{id, size}` and the body. The client matches responses by `id`. | ~0 RTT (B-conn is already open) |
| P3 hinted | The client guesses from a hint (extension, manifest, earlier HEAD), with P2 as the fallback. | ~0 RTT |

Optional extension, P4 "head on express, tail on bulk": the first T bytes of every object go on E and the
remainder on B. This gives a fast first byte for large objects. Treat it as a stretch goal.

### 3.4 Threshold T

- Fixed T in {256 KB, 1 MB, 4 MB, 16 MB}, against **BDP-relative T = c × BDP_est** with c in {1, 2, 4}.
- BDP_est = min_rtt × bw_est from PathState. Before any estimate exists, use a default (2 MB).
- Rationale: slow start delivers about 2 × BDP before it fills the pipe (§1.3). Objects below a few × BDP live
  entirely in startup, so they are "short" with respect to this path.

### 3.5 Bulk lane

Unmodified CUBIC + HyStart++ (the "cautious CC for big files" from the README sketch). BBR is a variant arm.
The only changes are optional: it publishes its bandwidth and RTT estimates to PathState, and it obeys the
coupling budget (§3.8).

### 3.6 Sprint: the express-lane CCA

#### 3.6.1 Where each part comes from

| Component | From | In Sprint |
|-----------|------|-----------|
| Receiver measures the arrival rate of marked probe packets; immune to ACK thinning and aggregation | ROS | The receiver timestamps PROBE-tagged packets and returns `RATE_REPORT` frames (§3.7). TCP's header bit becomes a QUIC frame. |
| Probe window of fixed duration (20 ms), IW excepted | ROS | Fast-half trains last τ_p = 20 ms (or ≥ the timestamp-precision floor, §4.7). The IW (10 packets) is the first, coarse train. |
| Receiver's answer encoded as rwnd = bw × ROS_RTT, ROS_RTT = r × RTT, r in 2..5 | ROS | A receiver cap: inflight ≤ r × R_rx × min_rtt. Carried explicitly in `RATE_REPORT`. A MAX_DATA-only variant is optional (§3.7). |
| After IW, a cautious phase at 40 % of the first BDP estimate, with cautious probing | ROS | κ = 0.4: R ← κ × R_iw on a cold start. |
| Exit when increase stagnates or on congestion loss | ROS + Nimble | Plateau exit (growth < 25 % for N rounds) and loss exit. |
| Each round split into halves: first slower, second faster, the fast half measuring the new estimate | Nimble Start | H1 at g_lo × R (cruise, useful data), H2 at g_hi × R (probe, also useful data). |
| Wire loss vs congestion loss: loss rate > 2 % and ≥ 6 discontiguous lost ranges | Nimble (from BBR) | The same criterion; congestion loss means R ← 0.7 × R. |
| After a congestion loss, keep probing until the exit condition holds | Nimble | Default behaviour. "Exit on loss" (ROS) is an ablation. |
| Extreme ACK suppression means fall back to slow start | Nimble | FALLBACK state (HyStart++ slow start). |
| Push data beyond rwnd to force receive-window autotuning | Nimble (TCP-specific) | **Not needed.** QUIC flow-control credit is explicit and configured ≫ BDP. |
| CE in the fast half means the bottleneck is reached; CE in the slow half means R is too high, cut proportionally | **new (ECN)** | Primary exit and correction signal (§3.6.4). |
| Loss without CE on a path known to mark is probably not congestion | **new (ECN)** | Loss differentiation, a hypothesis to test. |
| Server knows the object size | README sketch | Never probe beyond what is left (S_rem). If S_rem fits in this round at R, finish in one round. |

#### 3.6.2 State machine

```
              cold start                                         warm start (fresh PathState)
                  |                                                     |
                  v                                                     v
           +-------------+  first RATE_REPORT (or ACK fallback)  +-------------+
  start -->|  IW_PROBE   | ------------------------------------> |    RAMP     |<--+ round clean,
           | IW burst =  |  R := κ × R_iw   (κ = 0.4)            | H1 @ g_lo R |   | growth >= 25 %
           | train #0    |                                       | H2 @ g_hi R |---+
           +-------------+                                       +-------------+
                  | no usable feedback /                               |  plateau (N rounds < 25 %)
                  | extreme ACK thinning                               |  CE in H1 (R := R(1 - F/2))
                  v                                                    |  congestion loss (if exit-on-loss)
           +-------------+                                             |  S_rem = 0 (done)
           |  FALLBACK   |-----> HyStart++ exit ----+                   v
           | (slow start)|                         +------------> +-------------+
           +-------------+                                        |   CRUISE    |  classic: hand over to
                                                                  |             |  CUBIC CA, cwnd = R × min_rtt
   persistent congestion (any state) --> IW_PROBE                 +-------------+  L4S mode: DCTCP/Prague-like
```

#### 3.6.3 Per-round logic (pseudocode)

```
state: R (bytes/s), min_rtt, S_rem, plateau_cnt, ecn_marking_seen (path ever produced CE)

on_round_start():
    budget = min(S_rem, cwnd_gain × R × min_rtt)              # cwnd_gain = 2
    if S_rem <= R × min_rtt: send S_rem paced at R; return    # fits in one round: finish
    H1 = first part of budget,  paced at g_lo × R             # g_lo = 1.0 (cruise)
    H2 = data for τ_p seconds,   paced at g_hi × R, PROBE-tagged (train id)   # g_hi = 2.0
    # schedule so the round average stays <= ~1.25-1.5 × R; the gap after H2 lets any probe queue drain

on_rate_report(rep):                                          # rep from receiver for train H2
    F1 = rep.ce_slow / rep.pkts_slow ; F2 = rep.ce_fast / rep.pkts_fast
    if F1 > 0:                                  # queue above the AQM target even at R
        R := R × (1 - F1/2);  exit("CE")        # DCTCP-style proportional correction
    elif F2 > 0 or rep.rate_fast < (1-ε) × g_hi × R or rep.owd_trend > delay_thr:
        R := max(R, rep.rate_fast)              # bottleneck reached: measured rate ~ capacity/share
        plateau_cnt += (growth < 25 %)
    else:
        R := g_hi × R                           # fast half passed cleanly: bottleneck >= g_hi R
        plateau_cnt := 0
    R := min(R, receiver_cap, 2 × ack_delivery_rate_max)      # sanity caps (§3.10)
    if plateau_cnt >= N_plateau: exit("plateau")

on_loss_round(stats):
    if stats.loss_rate > 2 % and stats.lost_ranges >= 6:      # Nimble/BBR congestion-loss test
        R := 0.7 × R                                           # keep probing (ablation: exit)
    elif ecn_marking_seen and stats.ce == 0:
        pass                                                   # likely non-congestive: retransmit only
    # otherwise: standard RFC 9002 loss recovery; no rate change

delay backstop (no ECN): round_min_rtt > min_rtt + clamp(min_rtt/8, 4 ms, 16 ms)   # HyStart++ thresholds
    -> treated like "F2 > 0"
```

Why O(1) rounds: after the IW train, R = 0.4 × C. One clean probe round at g_hi = 2 gives R = 0.8 × C, and the
next probe is dispersed to about C. So Sprint reaches C in about 3 rounds *regardless of BDP*, against 12 rounds
of slow start at 1 Gbit/s × 100 ms. The **sender-only (v0) variant** gets the same information from ACKs:
delivery-rate samples taken as min(ack_rate, send_rate), BBR-style, together with per-ACK CE deltas attributed
to the H1 or H2 packets that the ACK newly acknowledges.

#### 3.6.4 Uses of ECN (the "ECN bit")

The IP ECN field is two bits: Not-ECT 00, ECT(1) 01, ECT(0) 10, CE 11. QUIC echoes exact per-space counts of
ECT(0), ECT(1) and CE (README §ECN in QUIC). Uses, in order of priority:

| # | Use | Needs | Version |
|---|-----|-------|---------|
| E1 | CE in fast half H2 means the bottleneck is reached: stop growing, set R to the measured rate | any ECN-marking AQM | v0 (sender attributes CE deltas), v1 (receiver counts exactly) |
| E2 | CE in cruise half H1 means R is above capacity: R × (1 − F/2) and exit | ECN AQM | v0 |
| E3 | Loss differentiation: loss without CE, on a path that has marked before, is treated as non-congestive | AQM that marks before it drops | v1 |
| E4 | The position of the first CE mark inside a probe train locates where the queue crossed the AQM target (Paced-Chirping-style estimate) | ECN AQM, receiver per-packet view | v2 |
| E5 | The express lane sends **ECT(1)**: the L4S queue of DualPI2 gives low latency and immediate shallow marking. Requires a scalable, DCTCP/Prague-like response after startup (RFC 9331 §4.3) | DualPI2 | v2 (optional) |
| E6 | ECN validation result and "path marks" flag are shared through PathState, so a new E-conn can trust ECN immediately | — | v3 |

Constraints:
- ECN is used only after QUIC's ECN validation passes (RFC 9000 §13.4.2). Before that, and on bleached paths,
  Sprint runs on rate, delay and loss signals only.
- In CRUISE, classic mode keeps a standard response to CE events (CUBIC's β = 0.7, as RFC 9002 treats CE like
  loss). Only the *startup* uses the proportional response. RFC 8311 permits this kind of experimentation.

#### 3.6.5 Parameters (initial values, to be tuned on a training subset, §5.12)

| Parameter | Default | Sweep | Source |
|-----------|---------|-------|--------|
| κ, first-step fraction of R_iw | 0.4 | 0.25, 0.4, 0.6, 1.0 | ROS |
| g_lo / g_hi, half-round pacing gains | 1.0 / 2.0 | g_hi in 1.5, 2, 3, 4 | Nimble |
| τ_p, probe train duration | 20 ms | 5, 10, 20 ms; min(20 ms, RTT/2) | ROS |
| N_plateau, rounds with < 25 % growth before exit | 2 | 1, 2, 3 | Nimble/BBR (BBR uses 3) |
| Congestion-loss test | > 2 % and ≥ 6 ranges | — | Nimble/BBR |
| Cut on congestion loss | 0.7 | 0.5, 0.7, 0.85 | Nimble/BBR/CUBIC |
| r, receiver cap multiplier | 2 | 2, 3, 5 | ROS |
| cwnd_gain | 2 | — | BBR |
| Delay backstop | HyStart++ RttThresh | — | RFC 9406 |
| κ_w, warm-start fraction of cached bw | 0.5 | 0.5, 0.75, 1.0 | Careful Resume-like |

### 3.7 Receiver feedback extension (ROS, the QUIC way)

- **Negotiation.** A transport parameter `sprint_feedback` with an experimental codepoint (a large random
  value, e.g. `0x73707274`, "sprt"). If the peer does not send it, Sprint runs sender-only (v0 logic).
  Unmodified clients still work.
- **`PROBE` frame** (sender to receiver, in each H2 packet, ≈ 4–12 bytes):
  `{train_id, idx, flags(first|last), [send_time_us]}`.
  The send time is optional. It enables an OWD-trend signal that is independent of clock offset (last minus
  first), which matters on non-ECN paths.
- **`RATE_REPORT` frame** (receiver to sender, sent as soon as a train ends, bundled with an ACK):
  `{train_id, pkts, bytes, duration_us, rate, owd_trend_us, ce_pkts, first_ce_idx, pkts_slow, ce_slow, rcv_cap}`.
  It is ack-eliciting but **not retransmitted**. If it is lost, the sender falls back to the ACK-based estimate
  for that round.
- **Why a frame and not a header bit.** QUIC short-header reserved bits are under header protection and MUST be
  zero unless an extension negotiates them. Frames plus a transport parameter are QUIC's idiomatic extension
  point (RFC 9000 §18, §19.21).
- **Optional, ROS-faithful variant (no new frame for the answer).** The receiver encodes R_rx × ROS_RTT into its
  MAX_DATA / MAX_STREAM_DATA credit. This is interesting for a "deployable with fewer wire changes" discussion,
  but it is coarser.
- **Alternative to compare against** (related work, not required): draft-ietf-quic-receive-ts. The receiver
  reports raw receive timestamps and the sender computes dispersion. Same information, but the computation
  sits at the sender.

### 3.8 Shared path state and lane coupling (server side)

```
struct PathState {            // one per client session, shared by both lanes' controllers (sketch)
    min_rtt, srtt, rttvar,    // merged from both lanes
    bw_est, bw_est_at,        // last non-app-limited delivery rate (B lane) or Sprint R (E lane)
    ecn_validated, marks_seen,
    express_inflight,         // for coupling
}
```

- **Warm start.** If PathState has a fresh bandwidth estimate (age < max(1 s, 10 × srtt)), E-conn skips
  IW_PROBE: R = κ_w × bw_est, then RAMP. This is Careful Resume inside a session, where the cached state is
  seconds old rather than hours old.
- **Coupling, or "yield".** While E-conn has data in flight, the B-lane window becomes
  max(min_window, cwnd_B − express_inflight). The client's aggregate stays at about one flow's share, and short
  objects get priority, which is SRPT at the sender (RFC 9040 / Congestion Manager / RFC 8699 style).
- **What coupling cannot fix.** Third-party flows from *other* hosts. There Sprint stands alone, and RQ4
  measures its harm.

### 3.9 Expected interplay with the network (the result matrix to confirm or refute)

| Bottleneck | Architecture alone (A3) | + Sprint (A4) | + coupling / warm start (A4c) |
|------------|-------------------------|---------------|-------------------------------|
| FIFO, no ECN | little gain: same queue | faster ramp on an idle path; delay/loss exits only | yielding the B lane drains the shared queue, which is what helps |
| FQ-CoDel, ECN | isolation from the bulk queue, but 2 × share | fast ramp to fair share; CE-based exit | removes the 2 × share; keeps isolation |
| DualPI2 / L4S | depends on codepoint | ECT(1) express lane: L-queue plus 1 ms marking, the best case | as left |

### 3.10 Safety argument (RFC 9743 checklist)

- **Severe congestion.** Persistent congestion resets to IW_PROBE at the minimum window. PTO behaviour is
  unchanged.
- **Bounded aggression.**
  - Growth per round is ≤ g_hi and always backed by evidence: a clean fast half or a measured rate.
  - The round-average rate is ≤ 1.5 × R.
  - R is capped by 2 × the maximum ACK-confirmed delivery rate. A lying receiver therefore cannot inflate R
    beyond what ACKs confirm.
- **Misbehaving receivers** (optimistic `RATE_REPORT`s): handled by the ACK cross-check above. Discuss it next
  to the optimistic-ACK attack.
- **Coexistence.** Harm vs CUBIC and BBR is measured (RQ4). CRUISE uses standard responses.
- **Failure modes.** No ECN, bleached ECN, CE on every packet, no receiver support, ACK thinning, random loss:
  every one is an explicit test case (W7).
- **Deployability.** Sender-only mode works with any QUIC client. The receiver part needs a client update,
  which is plausible for browsers and apps that ship their own QUIC stack.

---

## 4. Implementation

### 4.1 QUIC stack choice

| | **quinn** (recommended) | s2n-quic (fallback) | quiche (Cloudflare) | neqo (Mozilla) | quic-go (Go) |
|---|---|---|---|---|---|
| Custom CC without forking | yes: `Controller` + `ControllerFactory` in `TransportConfig` | yes, behind the `unstable-congestion-controller` feature | no | no | no: internal `SendAlgorithm` interface (pluggable CC open since #776) |
| Controller sets the pacing rate | **no**: the pacer derives the rate from cwnd/srtt × 1.25 with ≥ 10-packet bursts, so a fork is needed | yes: `earliest_departure_time()` | internal | internal | **yes**: the pacer lives inside the CC (`TimeUntilSend`, `HasPacingBudget`) |
| CE counts reach the CC | only as a congestion event (`lost_bytes = 0`, `is_ecn` on `main`), so a fork is needed for counts | `on_explicit_congestion(ce_count)` | fork | fork | only as a congestion event (`OnCongestionEvent(pn, 0, …)`), treated like loss; fork for counts |
| ECN marking and validation | yes (ECT(0); no `TransportConfig` toggle found) | yes | verify | yes | yes (ECT(0), full validation); `QUIC_GO_DISABLE_ECN` switch |
| New frames / transport parameters | fork `quinn-proto` (moderate, single crate) | fork (large codebase) | fork | fork | fork `internal/wire` + packet packer (moderate) |
| qlog | `qlog` feature | event subscribers | mature | yes | yes (`QLOGDIR`, JSON-SEQ) |
| ACK-frequency extension | yes (`ack_frequency_config`) | verify | verify | verify | no (not upstream) |
| Startup prior art in the stack | none (no HyStart++) | — | HyStart++ | Careful Resume (ANRW'25) | Chromium-style HyStart; **NewReno is the default**; IW = 32 packets |
| Testability | `quinn-proto` is sans-IO and has an in-process test harness | good | sans-IO | sans-IO | `simnet` + Go `synctest` (virtual time; no bottleneck model) |

Decision: **fork quinn** and pin it to one commit of `main`, which has the newer CC API
(`on_spurious_congestion_event`, the `is_ecn` flag, a pacer rate cap). Never rebase during the evaluation
phase. Fall back to s2n-quic if the pacer and GSO changes prove intractable (go/no-go at the end of W8).
If "the Rust implementation" you had in mind is quiche, the table shows what changes: everything needs a fork.

**quic-go (Go)** is the strongest non-Rust alternative for this plan:
- Its controller already owns pacing, which removes fork item 2 (the pacer).
- ECN validation and an ECN-off switch already exist.
- The costs: the same kind of CC fork, weaker built-in baselines (NewReno default, CUBIC switched off, no
  HyStart++ or BBR), and IW = 32 packets to pin.

The full assessment is in [THESIS_PLAN_SINGLE_CONNECTION.md](THESIS_PLAN_SINGLE_CONNECTION.md) §4.1.1.

### 4.2 Fork patch set (keep it small and well-documented; it becomes a thesis appendix)

1. **Controller trait extensions.** Every new method has a default no-op, so built-in CCAs are unaffected.
   ```rust
   fn pacing_rate(&self) -> Option<u64> { None }          // bytes/s; None = legacy cwnd/srtt pacing
   fn max_burst_packets(&self) -> Option<u64> { None }    // probe trains need 1-2, not >= 10
   fn on_ecn_feedback(&mut self, now: Instant, ect0: u64, ect1: u64, ce: u64, newly_acked_ect: u64) {}
   fn on_rate_report(&mut self, now: Instant, rep: &RateReport) {}
   fn on_app_signal(&mut self, now: Instant, sig: &dyn Any) {}   // size hint, PathState handle
   // and: pass the packet number to on_ack (needed for per-packet rate samples and H1/H2 attribution)
   ```
2. **Pacer.** Use `pacing_rate()` when it returns a value, and honour `max_burst_packets()`. Cap the number of
   GSO segments per batch while probing.
3. **ECN.** Pass per-ACK count deltas to the controller. Add a toggle for ECT(0), ECT(1) or off, per connection.
4. **Extension.** The transport parameter, the `PROBE` and `RATE_REPORT` frames, and the receiver-side
   estimator, which records per-packet arrival time and ECN codepoint for tagged packets.
5. **App API.** `Connection::congestion_signal(Box<dyn Any + Send>)`, which calls `on_app_signal`. The server
   uses it for size hints and to attach PathState once both lanes are bound to a session.
6. **Baselines inside the fork.** HyStart++ for CUBIC (RFC 9406, about 150 lines). This is also a good first
   task to learn the `Controller` API.
7. **qlog.** Custom events: `sprint:state`, `sprint:round` (R, H1/H2 counts, CE), `sprint:rate_report`,
   `sprint:exit` (reason).

### 4.3 Repository layout

```
master-thesis/
  README.md                research notes
  THESIS_PLAN.md           this plan
  quinn/                   git submodule: fork of quinn-rs/quinn, branch `sprint`, pinned commit
  crates/
    sprint-cc/             Sprint controller, HyStart++ wrapper, PathState, coupling
    lane-proto/            express/bulk protocol messages (HELLO, REQ, RESP, MOVED, REDIRECT)
    fileserver/            server binary: two endpoints, steering, synthetic or real files
    fileclient/            client binary: workload driver (single object, Poisson, burst, ...)
    sprint-sim/            deterministic simulator on quinn-proto (sans-IO) with a bottleneck model
  testbed/
    netns/                 topology setup/teardown scripts
    profiles/              tc profiles: fifo, fq_codel, codel, dualpi2, netem delay/loss/slot, bleach
    runner/                Python orchestrator, experiment YAML files, preflight calibration
  analysis/                parsing (logs/qlog to Parquet), statistics, plots
  thesis/                  LaTeX
```

### 4.4 Application protocol (minimal, HTTP/0.9-like; HTTP/3 adds noise unrelated to CC)

- One bidirectional stream per request. Client: `REQ{id, name}` then FIN. Server: `RESP{id, size}` + body, or
  `MOVED{id, size, lane=B}`.
- Bulk responses travel on a server-initiated unidirectional stream on B: `{id, size}` + body.
- The server serves **synthetic bytes from memory** by default (`/size/<n>`), which removes disk I/O from the
  measurement. Real files are supported for demonstrations.
- The server writes whole objects into a large `send_window`, so the transfer is never app-limited by the
  server loop.

### 4.5 Instrumentation

- Timestamps: `CLOCK_MONOTONIC` in ns on both sides. All network namespaces share one kernel clock, so one-way
  delays can be computed exactly.
- Per-transfer JSON lines, client side:
  `run_id, arm, lane, size, t_conn_start, t_req, t_first_byte, t_last_byte, bytes, moved/redirected`.
- Per-transfer JSON lines, server side: Sprint summary (exit reason and time, rounds, R trajectory, max
  inflight, losses, CE, ECN validation state, fallbacks).
- qlog for **diagnostic runs** (sampled, e.g. 1 in 10). Measure the overhead of qlog before trusting
  measurement runs that have it enabled.
- Bottleneck counters: `tc -s -j qdisc` before and after each run (drops, marks, max backlog). For diagnostic
  runs, a netlink poller (pyroute2 or a small Rust tool) every 5–10 ms.
- pcap (headers only, `-s 128`) on both sides of the bottleneck for diagnostic runs. ECN is visible in IP
  headers. Set `SSLKEYLOGFILE` through rustls to decrypt in Wireshark when debugging.

### 4.6 Build order (each step is a usable result)

| Version | Contents | Fork pieces needed |
|---------|----------|--------------------|
| baseline | stock CUBIC, BBR, HyStart++; single connection | 6, 7 |
| dual-lane | two endpoints, steering P1/P2, PathState skeleton | — (app only) |
| Sprint v0 | sender-only: IW train via ACK dispersion, H1/H2 probing, plateau/loss/CE/delay exits, fallback | 1, 2, 3 |
| Sprint v1 | receiver estimator + `PROBE`/`RATE_REPORT` + transport parameter | 4 |
| Sprint v2 | per-packet CE in trains (E4); L4S mode with ECT(1) and a scalable CRUISE (E5, optional) | 3, 4 |
| Sprint v3 | warm start + coupling via PathState; size hints | 5 |

### 4.7 Known implementation pitfalls (check each one before trusting any number)

- **Flow control.** quinn's default `stream_receive_window` is **1.25 MB** (sized for 100 Mbit/s × 100 ms). It
  will cap a single stream on the 100 Mbit/s × 200 ms and 1 Gbit/s paths. Set `stream_receive_window`,
  `receive_window` and `send_window` ≥ 4 × the largest BDP, and check that no `*_BLOCKED` frames appear in qlog
  (README §Flow control).
- **MTU.** Pin `initial_mtu` and disable MTU discovery (or pin both lanes to the same value). Otherwise IW
  differs between arms (README §Datagram size and PMTU).
- **GSO.** quinn uses UDP GSO by default (`enable_segmentation_offload = true`). A GSO batch leaves as one
  super-packet. Disable GSO on the veths (`ethtool -K <veth> tx-udp-segmentation off gso off tso off`) or in
  quinn, or tc and netem will treat the batch as one packet (wrong queue accounting, whole-batch loss).
- **Pacing granularity.** The tokio timer has about 1 ms resolution, and quinn targets 2 ms bursts of ≥ 10
  packets. Two-rate probing needs finer control. Verify inter-packet gaps in pcap. Kernel pacing (`SO_TXTIME` +
  `fq`) is a possible later improvement.
- **Receiver timestamps.** Userspace `Instant::now()` after `recvmmsg` plus GRO gives per-batch, jittery
  timestamps. Measure the error against pcap timestamps early (W12). Prefer kernel `SO_TIMESTAMPNS` and disable
  GRO on the client socket. A 20 ms train makes ~50 µs of error negligible (0.25 %), which is the reason ROS
  uses time-based windows.
- **ECN on receive.** Confirm per-packet ECN delivery with GRO enabled. Linux GRO does not merge packets with
  different TOS, but verify it anyway.
- **App-limited.** quinn's CUBIC does not grow cwnd while app-limited. If the server writes in small chunks,
  Sprint sees app-limited rounds and makes wrong exit decisions.
- **Migration** resets CC state (README §Connection IDs and migration). Keep client addresses fixed.

---

## 5. Testing and evaluation

### 5.1 Principles

1. **Separate the effects.** Every claim must be attributable to the architecture, the algorithm or the network.
   This drives the arms (§5.4) and the queue factorial (FQ × ECN, §5.5).
2. **Strong baselines.** HyStart++ (not plain slow start), BBR, and a single connection with stream priority.
3. **Calibrate before measuring.** No experiment runs on an unverified path (§5.3.4).
4. **Pre-register.** Fix RQs, hypotheses, metrics, exclusion rules and the analysis script *before* the main
   campaign (W14). Tune parameters on a training subset and report on held-out scenarios.
5. **Automate everything.** One command reproduces every figure from raw logs.

### 5.2 Correctness testing (before performance testing)

| Level | What | Tool |
|-------|------|------|
| Unit | Sprint state machine fed with synthetic events (ACKs, reports, CE deltas, losses), with expected transitions and values. Example: "CE in H1 at F = 0.2 gives R × 0.9 and CRUISE". | `cargo test` |
| Property | Invariants over random event sequences: cwnd ≥ min window; R > 0; R never grows without evidence; R ≤ caps; exit is always reached under persistent CE; persistent congestion gives IW_PROBE. | `proptest` |
| Deterministic simulation | `sprint-sim`: two `quinn-proto` endpoints joined by a modelled bottleneck (rate, buffer, step or CoDel-style marking, loss). Fast, reproducible, CI-friendly; used for exploration and parameter sweeps. Results must be confirmed in emulation. | own crate |
| Frame codec | Encode/decode round-trip and fuzzing of `PROBE` and `RATE_REPORT`; behaviour when the parameter is not negotiated. | `cargo fuzz` (optional) |
| Integration | Real client and server through netns on fixed paths, with a golden-trace check: an idle 100 Mbit/s × 50 ms path must exit Sprint by "plateau" within 3–5 rounds. | runner |
| Interop | The fork with the extension disabled still talks to unmodified QUIC (stock quinn; optionally another stack). | runner |
| Visual | qvis on sampled qlogs: cwnd, rate, RTT, CE over time. | qvis |

### 5.3 Testbed

#### 5.3.1 Platform

- **Linux, bare metal preferred** (≥ 8 cores; disable turbo and frequency scaling; pin processes with
  `taskset`). A VM is fine for development but adds timer jitter, so use bare metal for final runs.
- **Kernel ≥ 6.17** (e.g. Ubuntu 26.04 LTS) for `sch_dualpi2`. `fq_codel`, `codel`, `pie`, `htb` and `netem`
  are standard.
- **Windows and WSL2**: use them for coding, unit tests and `sprint-sim`. Do not run experiments there. The
  WSL2 kernel may lack `netem` and `dualpi2`, and the timing is not trustworthy.
- Tools: `iproute2`, `ethtool`, `tcpdump`, `iperf3` (calibration and TCP cross traffic), Python 3 with
  `pyroute2`, `polars`/`pandas`, `scipy` and `matplotlib`.

#### 5.3.2 Topology (dumbbell in network namespaces)

```
  srv ──┐                                                                  ┌── cli   (dual-lane client)
        ├── br-s ── [dly: netem delay RTT/2 per direction, loss, slot] ──┐ │
 xsrv ──┘                                                                │ │
                                       [rtr: HTB rate C + leaf AQM/FIFO] ┴─ br-c ─┤
                                        (egress towards clients =                └── xcli  (cross-traffic client)
                                         forward-path bottleneck)
```

- The data direction is server to client, so the bottleneck sits on the router's **egress towards the
  clients**.
- **Never put netem on the same interface (or namespace) as the AQM.** netem's internal queue distorts AQM
  behaviour. Delay lives in its own namespace with a large `limit`, so netem itself never drops.
- Reverse-path effects (ACK aggregation, a reverse bottleneck) are added explicitly in W7 only.

#### 5.3.3 tc recipes (starting point; verify each one with `tc -s` and tcpdump)

```sh
# Bottleneck: 100 Mbit/s, leaf qdisc holds the queue
tc qdisc add dev rtr-c root handle 1: htb default 1
tc class add dev rtr-c parent 1: classid 1:1 htb rate 100mbit ceil 100mbit burst 15k cburst 15k
# Leaf variants (choose one):
tc qdisc add dev rtr-c parent 1:1 handle 10: bfifo limit 625000       # FIFO, 1 BDP at 100M/50ms
tc qdisc add dev rtr-c parent 1:1 handle 10: fq_codel ecn             # FQ + ECN   (fq_codel noecn = FQ only)
tc qdisc add dev rtr-c parent 1:1 handle 10: codel ecn                # single queue + ECN
tc qdisc add dev rtr-c parent 1:1 handle 10: dualpi2                  # L4S DualQ (classic + L4S)

# Delay: RTT/2 each direction, in the dly namespace; huge limit so netem never drops
tc qdisc add dev dly-c root netem delay 25ms limit 1000000
tc qdisc add dev dly-s root netem delay 25ms limit 1000000
# Random (non-congestive) loss on the forward path only
tc qdisc change dev dly-c root netem delay 25ms loss 0.1% limit 1000000
# ACK aggregation on the reverse path (Wi-Fi-like bursts)
tc qdisc change dev dly-s root netem delay 25ms slot 2ms 6ms packets 16 limit 1000000
# ECN bleaching (clear both ECN bits) before the bottleneck; "set 3 retain 3" = mark every packet CE
tc qdisc add dev dly-c clsact
tc filter add dev dly-c egress matchall action pedit ex munge ip tos set 0 retain 3 pipe action csum ip
# Bandwidth step during a run
tc class change dev rtr-c parent 1: classid 1:1 htb rate 20mbit ceil 20mbit
```

#### 5.3.4 Calibration checklist (automated as `runner preflight`; store the output with every campaign)

1. `ping`: RTT equals the configured value ± 0.5 ms, no loss.
2. `iperf3` TCP and UDP: throughput equals C ± 2 %. UDP at 1.2 × C shows the expected bottleneck drops.
3. Queue: a UDP blast at 1.5 × C fills `bfifo` to its limit, giving queueing delay ≈ buffer/C. AQMs hold the
   queue near their target.
4. ECN: an ECT(0) flow through `fq_codel ecn` produces `ecn_mark > 0` in `tc -s`, and tcpdump at the client
   shows CE. Bleach and all-CE profiles behave as intended.
5. No GSO super-packets: every captured packet at the bottleneck is ≤ MTU.
6. Pacing: the inter-packet gap histogram of a quinn flow matches the expected rate and burst size.
7. No CPU limit: quinn bulk at the highest C reaches C with < 70 % CPU per core. Otherwise lower the maximum C.
8. No flow-control limit: no `DATA_BLOCKED` or `STREAM_DATA_BLOCKED` in qlog on the largest-BDP path.
9. Clock sanity: OWD from logs equals RTT/2 on an idle path.
10. Repeatability: two identical batches give medians within 2 % of each other.
11. Sanity reference: quinn CUBIC and Linux TCP CUBIC (iperf3) have similar slow-start traces on the same path.

### 5.4 Arms (what is compared)

| ID | Lanes | Express CC / bulk CC | Purpose |
|----|-------|----------------------|---------|
| A0 | single | CUBIC (stock quinn, no HyStart) | stock reference |
| **A1** | single | CUBIC + HyStart++ | **main baseline** |
| A1p | single + stream priority (short objects higher) | CUBIC + HyStart++ | strongest single-connection competitor in W2 |
| A2 | single | BBR (quinn's experimental port of Google quiche's BBR) | model-based baseline |
| A3 | dual | CUBIC+HS++ / CUBIC+HS++ | **architecture only** |
| **A4** | dual | Sprint / CUBIC+HS++ | **full proposal** |
| A4c | dual + coupling + warm start | Sprint / CUBIC+HS++ | full proposal + coupling |
| A4l | dual, express on ECT(1) | Sprint-L4S / CUBIC+HS++ | L4S variant (optional, DualPI2 only) |
| A5 | single | Sprint for the whole connection | **algorithm only** |

- **Ablations of A4.**
  - Components: −ECN, −receiver (v0 sender-only), −size hint, −warm start, exit-on-loss vs keep-probing.
  - Parameters (§3.6.5): κ, g_hi, τ_p, N_plateau, r.
- **Optional external baselines** (only if time allows; implement in the same fork for a fair comparison):
  large IW (32/64), SEARCH, Rapid Start, a Careful-Resume-like jump.

### 5.5 Network scenarios

Core grid, used by W1–W3:

| Factor | Levels |
|--------|--------|
| Bottleneck rate C | 20, 100 Mbit/s (+ 500 Mbit/s–1 Gbit/s sweep if CPU allows, preflight item 7) |
| Base RTT | 20, 50, 100, 200 ms (+ 600 ms GEO-like, optional) |
| Queue | FIFO 0.5 BDP, FIFO 2 BDP, CoDel+ECN, FQ-CoDel+ECN, DualPI2 |
| Loss | 0 (random loss only in W7) |

- To separate *isolation* effects from *marking* effects, hold the AQM constant and vary the other two
  factors in a 2 × 2 factorial (FQ: no/yes) × (ECN: no/yes): CoDel noecn, CoDel ecn, FQ-CoDel noecn,
  FQ-CoDel ecn. The core grid keeps the two ECN cells; the noecn cells run as a sweep. FIFO is the no-AQM
  reference, and DualPI2 adds L4S.
- Buffer sizes are expressed in BDP, covering both regimes (1 BDP and BDP/√n), as in README §Sizing Router
  Buffers.
- One-factor-at-a-time sweeps outside the core: buffer (0.1, 0.25, 0.5, 1, 2, 4 BDP), ECN modes (validated,
  bleached, all-CE, ECN but no marking AQM), ACK frequency (quinn `ack_frequency_config`: ack every 2, 8, 32
  packets).

### 5.6 Workloads

| ID | Name | Description | Arms | Main metrics | RQ |
|----|------|-------------|------|--------------|----|
| W1 | Single object, idle path | Sizes 100 KB, 250 KB, 500 KB, 1, 2, 5, 10, 20, 50 MB; one request on an established connection; full core grid | A1, A2, A4, A5 (+ ablations) | FCT, slowdown, headroom capture, overshoot, loss | RQ2, RQ3 |
| W2 | Short objects during own bulk download | B-lane (or the single connection) downloads 200 MB; after 5 s warm-up, 20 short requests (100 KB–2 MB), Poisson at 1/s | A1, A1p, A3, A4, A4c, (A4l) | short-FCT CDF, bulk throughput, queueing delay | RQ1 |
| W3 | Short objects vs third-party long flow | xsrv→xcli long CUBIC or BBR flow (quinn or iperf3 TCP); cli fetches short objects with exp(1 s) think time | A1, A4 | short FCT; competitor throughput and RTT, with vs without us (harm) | RQ2, RQ4 |
| W4 | Mixed open-loop workload | 4 or 16 clients; Poisson arrivals; sizes bounded-Pareto (α ≈ 1.2) on [100 KB, 50 MB] or an empirical web distribution; load ρ in 0.3, 0.6, 0.8; 120 s + warm-up | A1, A3, A4, A4c | mean and p99 slowdown per size bucket (≤ 1 MB, 1–10 MB, > 10 MB) | RQ1, RQ2, RQ5 |
| W5 | Synchronized burst ("flash crowd") | K in 2, 4, 8, 16, 32 clients start a 1 MB express transfer at the same instant on an idle bottleneck | A1, A4 | loss, peak queue, FCT spread, JFI among them | RQ4 (overshoot safety) |
| W6 | Fairness and convergence | A long Sprint flow (misclassified) vs CUBIC; staggered starts in both orders; 60 s | A1 vs A1, A4 vs A1 | throughput share over time, JFI, convergence time, harm | RQ4 |
| W7 | Robustness | Random loss 0.01/0.1/1/2 %; ECN bleached; all-CE; FIFO (no marks); bandwidth step 100→20 and 20→100; RTT step; reordering; ACK aggregation (netem slot) and ACK thinning (ack-frequency); shallow buffer; reverse-path bottleneck | A1, A4 (+ ablations) | Δ median FCT vs A1, loss, correct fallback (exit reasons in logs) | RQ3 |
| W8 | Cold vs warm, establishment, steering | First request on a fresh E-conn vs after B-lane activity (warm start); FCT including handshake (1-RTT and 0-RTT); eager vs lazy lanes; steering P1 vs P2 | A3, A4, A4c | FCT_conn, TTFB, misroute cost | RQ1 |
| W9 | Threshold study | W4 at different BDPs with fixed T in {256 KB, 1, 4, 16 MB} vs T = c × BDP, c in {1, 2, 4} | A4c | mean slowdown, bulk throughput | RQ5 |

### 5.7 Metrics (definitions and data sources)

| Metric | Definition | Source |
|--------|------------|--------|
| **FCT_req** (primary) | t_last_byte − t_request (client application view) | client log |
| FCT_conn | t_last_byte − t_connect_start (includes handshake; W8) | client log |
| TTFB | t_first_byte − t_request | client log |
| **Slowdown** | FCT_req / (RTT_base + S/C). Allows pooling across sizes and paths. | derived |
| **Headroom capture** | (FCT_A1 − FCT_arm) / (FCT_A1 − FCT_ideal): the fraction of §1.3's headroom realized | derived |
| Startup duration and exit reason | time from first data byte to leaving RAMP; reason in {plateau, CE, loss, delay, done, fallback} | server log / qlog |
| Estimation error | (R_at_exit − C_avail) / C_avail, where C_avail is C or the fair share | server log + scenario |
| **Overshoot** | max bytes_in_flight / BDP; max and p95 queueing delay during startup | qlog; OWD; tc |
| Loss | sender-declared lost packets / sent; bottleneck drops | server log; `tc -s` |
| Retransmission overhead | retransmitted bytes / S; spurious-loss count | server log |
| CE fraction | CE-marked / ECT-sent; bottleneck mark counter | ACK ECN counts; `tc -s` |
| Queueing delay | per-packet OWD − min OWD (p50, p95, p99) | qlog sent and received events (same clock) |
| Utilization | delivered bytes / (C × transfer duration) | client log |
| Competitor and bulk throughput | goodput in 100 ms bins; mean during short-flow activity vs a solo run | logs / iperf3 JSON |
| **Harm** (Ware et al.) | throughput: (x_solo − x_with) / x_solo; delay and loss: (d_with − d_solo) / d_with. Compare harm(Sprint) with harm(CUBIC) in the same position. | derived |
| JFI | (Σx)² / (n Σx²) over 1 s windows | derived |
| Overhead | `PROBE`/`RATE_REPORT` bytes per transfer; server CPU % (`pidstat`); the second handshake (time, CPU) | logs |
| Misroute cost | extra time for MOVED or REDIRECT requests | client log |

### 5.8 Per-run procedure (runner)

1. If the path changed, apply the profile and run a quick preflight subset (ping, `tc -s` snapshot).
2. Start the server (pinned cores), wait for ready, then start cross traffic if the workload needs it, then
   warm up.
3. Run the client workload, using a **fresh process and fresh connections** unless the run tests resumption.
4. Stop; collect logs, `tc -s` deltas and optional pcap or qlog.
5. Validate the run:
   - all bytes delivered;
   - no flow-control blocking;
   - ECN validation state as expected for the scenario;
   - no harness errors.

   Failing runs are excluded *by predefined rules* and counted in the report.
6. Write metadata: fork and harness git hashes, kernel, CPU, full config, seed, timestamps.
7. Cool down (1 s, empty queues).

Run order: **randomized, interleaved blocks**. Each repetition runs every arm in random order, so time drift
affects all arms equally.

Example experiment file:

```yaml
experiment: w1_single_object
repetitions: 25
grid:
  rate_mbit: [20, 100]
  rtt_ms: [20, 50, 100, 200]
  queue: [fifo_0.5bdp, fifo_2bdp, codel_ecn, fq_codel_ecn, dualpi2]
arms: [A1, A2, A4, A5]
workload: {type: single_object, sizes: [100KB, 250KB, 500KB, 1MB, 2MB, 5MB, 10MB, 20MB, 50MB]}
logging: {qlog_sample: 0.1, pcap: false}
```

### 5.9 Statistics and reporting

- **Repetitions.** Choose n from the pilot (W14) so that the 95 % CI of the median is within ±3 %. Expect
  20–30 per configuration; fewer for 20 and 50 MB, which have low variance and long runtimes. For p99 tails,
  use request-level data from W4 (thousands of requests).
- **Report.** Median with a 95 % bootstrap CI, plus p95 and p99. Use CDFs for distributions. Plot FCT against
  size on a log-x axis with CI bands. Show heatmaps over RTT × C.
- **Comparisons.** Mann–Whitney U, Holm–Bonferroni corrected over each family of comparisons. Effect size as
  the ratio of medians with a bootstrap CI, e.g. "Sprint reduces median FCT by 37 % [33 %, 41 %]".
- **Honesty rules.**
  - Report where Sprint loses, especially at low BDP and in no-ECN cases.
  - Report excluded runs.
  - Never tune on the held-out scenarios.

### 5.10 Time budget (estimate; recompute after the pilot)

- W1 core: 2 rates × 4 RTTs × 5 queues × 4 arms × 25 repetitions = 4 000 size sweeps.
- One size sweep (9 objects) takes ≈ 36 s at 20 Mbit/s and ≈ 7 s at 100 Mbit/s, plus ≈ 1 s overhead per
  object.
- Total ≈ 2 000 × (45 s + 16 s) ≈ **34 h** of machine time.
- W2–W9 together are about the same order. Plan **1–2 weeks of unattended runs**. Run overnight and keep the
  daytime for analysis.

### 5.11 Figure and table plan (each maps to an RQ; draft the captions early)

| # | Figure | RQ |
|---|--------|----|
| F1 | Headroom heatmap: model slow-start/ideal over size × BDP (§1.3) | motivation |
| F2 | Anatomy of one transfer: R, cwnd, inflight, queueing delay, CE marks over time, Sprint vs HyStart++ | design |
| F3 | FCT vs size (W1) per path; lines per arm, CI bands | RQ2 |
| F4 | Headroom capture heatmap over RTT × C, per queue type | RQ2 |
| F5 | Overshoot, peak queue and loss in startup: ECN vs no ECN, per AQM | RQ3 |
| F6 | W2: short-FCT CDFs per queue discipline (A1, A1p, A3, A4, A4c) + bulk throughput | RQ1 |
| F7 | Harm (throughput and delay) of Sprint vs CUBIC on competing flows (W3, W6) | RQ4 |
| F8 | W4: slowdown per size bucket × load | RQ1, RQ2 |
| F9 | W5: loss and FCT spread vs number of synchronized flows | RQ4 |
| F10 | W7 robustness table: Δ FCT and loss per adverse condition | RQ3 |
| F11 | W9: threshold sweep, fixed vs BDP-relative | RQ5 |
| F12 | Ablation waterfall: contribution of each component (receiver, ECN, size hint, warm start, coupling) | all |
| T1–T3 | Parameters; testbed configuration; one-line answer per RQ | — |

### 5.12 Threats to validity (and mitigations)

| Threat | Mitigation |
|--------|------------|
| Emulation is not the Internet (single bottleneck, synthetic cross traffic) | Several AQMs and buffers, cross-traffic types, optional real-path runs (§5.13); discuss explicitly |
| User-space QUIC CPU and timer limits (pacing granularity, receiver timestamp jitter) | Preflight items 6–7; keep C ≤ CPU-safe rate; measure timestamp error; report pacing realism |
| Parameter overfitting | Tune on 50 ms RTT / 100 Mbit/s FIFO and FQ-CoDel only; evaluate everywhere else |
| Weak baselines | HyStart++ instead of plain slow start; BBR; A1p priority baseline; optional SEARCH and large IW |
| Implementation bugs | Unit, property and simulation tests; qvis inspection; quinn CUBIC vs Linux CUBIC sanity check |
| ECN is rare on real paths | No-ECN, bleached and all-CE are first-class scenarios (W7) and part of RQ3 |
| Unpublished ROS and Nimble Start | Self-contained description of reused parts; no claims about their original performance |
| Two-connection unfairness | Coupling (A4c); harm metrics; the A5 single-connection ablation |

### 5.13 Optional: real-network validation

Two cloud VMs on different continents (RTT about 100–200 ms), or the university network to a cloud VM.
- Run W1 for a few sizes, 50 repetitions, interleaved arms.
- Expect no ECN marking (Sander et al.), which makes this a test of the no-ECN fallback.
- Also log the outcome of QUIC ECN validation on those paths: it is itself a small, interesting data point.

---

## 6. Timeline (26 weeks; W1 = week of 5 Oct 2026 if starting now; stretch or compress to the deadline)

| Weeks | Phase | Tasks | Milestone (done means) |
|-------|-------|-------|------------------------|
| 1–2 | Setup | Linux test machine; build quinn; stock client/server through netns; qlog + qvis working; read RFC 9406, RFC 9743, RFC 9330–9332, RFC 9959, Paced Chirping, Sander et al. | **M0**: stock quinn transfer through an emulated 100 Mbit/s × 50 ms path, with qlog |
| 3–4 | Harness and baselines | file server/client (single connection); runner + YAML; tc profiles; preflight; HyStart++ in the fork | **M1**: calibrated testbed; A0/A1/A2 W1 pilot results |
| 5–6 | Dual-lane | two endpoints, session binding, steering P1/P2, threshold, PathState skeleton; start writing Background | **M2**: A3 vs A1/A1p on W1/W2 (architecture-only result) |
| 7–8 | Fork hooks | pacing hook + burst control; ECN deltas; app-signal API; packet number in `on_ack`; `sprint-sim` | **M3**: hooks merged in the fork, sim runs. **Go/no-go: stay on quinn or switch to s2n-quic** |
| 9–11 | Sprint v0 | sender-only Sprint, exits, fallback; unit, property and sim tests; write Related Work | **M4**: v0 beats A1 at high BDP in W1 pilot. **Go/no-go: revise the design if not** |
| 12–13 | Sprint v1 | transport parameter, `PROBE`/`RATE_REPORT`, receiver estimator; timestamp-precision study | **M5**: v1 working; estimation-accuracy plot |
| 14 | Pilot and pre-registration | variance study gives n; tune κ and g_hi on the training subset; freeze the analysis script | **M6**: code freeze 1, parameters frozen |
| 15–16 | Sprint v2/v3 | per-packet CE (E4); optional L4S mode (E5); warm start + coupling; write Design | **M7**: feature freeze (anything unfinished becomes future work) |
| 17–20 | Main campaign | W1–W9 runs; ablations; robustness; analysis pipeline gives figures; write Methodology | **M8**: all figures F1–F12 drafted |
| 21–24 | Writing | Results, Discussion, Introduction, Conclusion; integrate the earlier chapters | **M9**: full draft to supervisor |
| 25–26 | Buffer | revisions, gap-filling experiments, presentation | **M10**: submission |

Habits:
- Keep a dated lab notebook (`notes/YYYY-MM-DD.md`) for every experiment and decision.
- Reserve one day a week for writing from W3 onward.
- Tag the fork and the harness at every milestone.

---

## 7. Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| quinn pacer and GSO changes are harder than expected | M | H | Start with the minimal `pacing_rate()` hook; GSO off while probing; fall back to s2n-quic or quic-go (decide in W8) |
| Receiver timestamps too noisy | M | M | Kernel timestamps, GRO off, longer τ_p; worst case keep v0 (sender-only) plus ECN as the main result |
| Sprint does not beat HyStart++ by a meaningful margin | L–M | H | Headroom model says gains exist at high BDP; warm start and coupling are separate wins; a careful negative result is still a thesis |
| Emulation artifacts (netem/HTB/GSO/CPU) | M | H | Preflight checklist; offloads off; cap C; Linux TCP sanity reference |
| Experiment matrix explodes | H | M | Core grid + one-factor sweeps; simulation for exploration; pilot-based n |
| L4S tooling issues (dualpi2, ECT(1) in quinn) | M | L | L4S is optional (A4l); classic ECN covers RQ3 |
| Writing runs late | H | H | Write from W3; figure plan fixed early; 2-week buffer |
| Unclear citation rules for ROS and Nimble Start | M | M | Settle with the supervisor in W1 |

---

## 8. Thesis outline

1. **Introduction**: short flows dominate; the slow-start cost (F1); why QUIC; RQs; contributions.
2. **Background**: QUIC transport and recovery (condensed from README); CC fundamentals; slow start and its
   exits; ECN, AQM and L4S; flow control vs CC.
3. **Related work**: startup algorithms (HyStart, HyStart++, SUSS, SEARCH, Rapid Start, Paced Chirping, Jump
   Start, Quick-Start, Halfback, RC3); state reuse (Careful Resume, hStart, RFC 9040); receiver-driven designs
   (ROS, Homa, NDP, ExpressPass); ECN-based CC (DCTCP, L4S/Prague, ABC); size-aware scheduling (pFabric, PIAS);
   QUIC CC studies.
4. **Design**: dual-lane architecture, steering, threshold, PathState and coupling, Sprint (state machine,
   ECN, feedback extension), safety argument.
5. **Implementation**: quinn fork patch set, extension wire format, application, harness.
6. **Evaluation methodology**: testbed, calibration, arms, scenarios, workloads, metrics, statistics.
7. **Results**: one section per RQ, plus the ablations.
8. **Discussion**: deployability (sender-only mode, ECN availability), fairness and ethics of aggressive CC,
   limitations, threats to validity.
9. **Conclusion and future work**: multipath, HTTP/3 integration, learning-based parameter selection,
   real-world deployment.

Appendices: parameter tables, wire format, reproducibility guide, extra plots.

---

## 9. Open questions to settle with your supervisor (early)

1. ROS and Nimble Start: citation form; access to the authors' code or parameter values; should they be
   baselines?
2. Is the L4S mode (ECT(1) + DualPI2) in scope, or future work?
3. Are real-network experiments expected, or is calibrated emulation enough?
4. Deadline and expected length, to adjust §6.
5. Is the harm framework (Ware et al.) an acceptable fairness methodology, in addition to JFI?
6. Is a minimal file protocol acceptable, or is HTTP/3 (`h3` crate) expected?

---

## 10. References

**QUIC and IETF**
- RFC 9000, 9001, 9002 (QUIC). RFC 9406 HyStart++. RFC 9743 Specifying New Congestion Control Algorithms.
  RFC 9959 Careful Resume.
- RFC 3168 ECN. RFC 8311 ECN experimentation. RFC 8257 DCTCP. RFC 8511 ABE. RFC 9330 / 9331 / 9332 L4S.
- RFC 8290 FQ-CoDel. RFC 8289 CoDel. RFC 8033 PIE. RFC 7928 AQM characterization.
- RFC 9040 TCP Control Block Interdependence. RFC 3124 Congestion Manager. RFC 8699 Coupled CC for RTP.
  RFC 6928 IW10. RFC 4782 Quick-Start.
- draft-ietf-quic-ack-frequency-14. draft-ietf-quic-receive-ts-00. draft-seemann-quic-accurate-ack-ecn-01.
  draft-chung-ccwg-search-09. draft-kazuho-ccwg-rapid-start-02. draft-ietf-ccwg-bbr.
- qlog: draft-ietf-quic-qlog-main-schema, draft-ietf-quic-qlog-quic-events; qvis: https://qvis.quictools.info

**Papers**
- Sander, Kunze, Blöcher, Kosek, Wehrle. *ECN with QUIC: Challenges in the Wild*. IMC 2023.
  https://arxiv.org/abs/2309.14273
- Fischer, Hofstätter, Deutschmann, German. *Getting up to Speed with Neqo and Careful Resume*. ANRW 2025.
  https://doi.org/10.1145/3744200.3744766
- *Multi-CDN as a Collective Service: Towards Hot Start in Congestion Control at Scale* (hStart). APNet 2026.
  https://doi.org/10.1145/3820441.3820445
- Misund, Briscoe. *Paced Chirping: Rapid flow start with very low queuing delay*. INFOCOM WKSHPS 2019.
- Ware, Mukerjee, Seshan, Sherry. *Beyond Jain's Fairness Index: Setting the Bar for the Deployment of
  Congestion Control Algorithms*. HotNets 2019.
- Goyal, Agarwal, Netravali, Alizadeh, Balakrishnan. *ABC: A Simple Explicit Congestion Controller for
  Wireless Networks*. NSDI 2020.
- Mittal, Sherry, Ratnasamy, Shenker. *Recursively Cautious Congestion Control*. NSDI 2014.
- Li, Dong, Godfrey. *Halfback: Running Short Flows Quickly and Safely*. CoNEXT 2015.
- Liu, Allman, Jin, Wang. *Congestion Control Without a Startup Phase*. PFLDnet 2007.
- Rüth, Wolsing, Wehrle, Hohlfeld. *Blitz-starting QUIC Connections*. arXiv 2019.
- Keshav. *A Control-Theoretic Approach to Flow Control*. SIGCOMM 1991.
- Dovrolis, Ramanathan, Moore. *Packet-Dispersion Techniques and a Capacity-Estimation Methodology*.
  IEEE/ACM ToN 2004.
- Ribeiro et al. *pathChirp: Efficient Available Bandwidth Estimation for Network Paths*. PAM 2003.
- Montazeri et al. *Homa*. SIGCOMM 2018. Handley et al. *NDP*. SIGCOMM 2017. Cho, Jang, Han. *ExpressPass*.
  SIGCOMM 2017.
- Alizadeh et al. *pFabric*. SIGCOMM 2013. Bai et al. *PIAS*. NSDI 2015.
- Marx, Herbots, Lamotte, Quax. *Same Standards, Different Decisions*. EPIQ 2020.
- Jaeger, Zirngibl, Kempf, Ploch, Carle. *QUIC on the Highway*. IFIP Networking 2023.
- Netravali et al. *Mahimahi*. USENIX ATC 2015. Yan et al. *Pantheon*. USENIX ATC 2018.
- Already in README: SUSS (SIGCOMM 2024), HyStart (Ha & Rhee), BBR draft, Aurora, Fair Queueing, Sizing Router
  Buffers.

**Code**
- quinn: https://github.com/quinn-rs/quinn (`Controller` trait:
  https://docs.rs/quinn-proto/latest/quinn_proto/congestion/trait.Controller.html)
- s2n-quic custom CC: https://aws.github.io/s2n-quic/examples/custom-congestion-controller.html
- quiche: https://github.com/cloudflare/quiche. neqo: https://github.com/mozilla/neqo
- SUSS: https://github.com/SUSSdeveloper/SUSS. SEARCH: https://search-ss.wpi.edu/
