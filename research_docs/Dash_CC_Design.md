# Dash: a QUIC-native short-flow congestion controller (design draft v3, no ECN)

"Dash" is a working name. The design is **QUIC-only by construction**. Its core depends on four things TCP
cannot offer:
1. receiver-side packet timing, carried in **new extension frames**;
2. **sender-controlled ACK frequency** (draft-ietf-quic-ack-frequency) used as a control loop;
3. extension negotiation through **transport parameters**;
4. a congestion controller (CC) that sits next to the application and knows stream sizes.

Section letters in brackets (A2, B6, …) refer to [TCP_vs_QUIC.md](TCP_vs_QUIC.md). **†** marks a reference not
yet in [Reading_List.md](Reading_List.md); verify it before citing.

- **Measurement and control are decoupled.** TCP-style CC infers the path from *when ACKs come back*, which
  is distorted by delayed ACKs, ACK aggregation, ACK thinning and reverse-path queues. Dash measures the path
  *where the packets land* (at the receiver) and uses ACKs only for reliability and the window. That is also
  why Dash can *reduce* the ACK rate without losing information.
- **Relation to Sprint** ([Double_Connection_Plan.md](Double_Connection_Plan.md) §3.6–3.7): Dash is Sprint's
  no-ECN core, specified concretely. Its frames refine plan §3.7's PROBE / RATE_REPORT (`PROBE_TRAIN` ≈
  PROBE, `TRAIN_REPORT` ≈ RATE_REPORT). The changes:
  - the report is an **ACK supplement** (TCP_vs_QUIC F1);
  - trains are **chirps**, not flat bursts;
  - the receiver can raise an **early alarm**.

  ECN plugs in later (§16).
- **Fallbacks** for clients without the extension: §10.
- **How to read.** §3 is the one-page overview; §4–§9 specify each mechanism; **§11 is a worked example**
  (100 Mbit/s × 50 ms path, 2 MB object) showing the window per round and every state change.

**Changes from v2.**
- **The IW estimate only aims the first chirp.** It no longer sets the first carrier rate; that rate is
  capped at 4× slow start's first round (§7.5). A token-bucket shaper can let a single burst through at line
  rate, so one burst is not enough evidence.
- **New report field and alarm.** The receiver reports the receive rate during queueing (Excursion Rate) and
  can raise an alarm on an open train (§6.7). This covers a chirp that starts above the available rate, for
  example after idle.
- **New CHECK phase**: one monitored RTT after every rate jump (§7.7).
- **Rounds are clocked by reports**, and the chirp-schedule formulas are spelled out (§7.2, §7.3).

---

## 1. Goals

- Minimize the FCT of 10 KB – few MB transfers, especially on high-BDP paths (plan §1.3).
- **Locate the bottleneck rate instead of doubling blindly.** In the best case Dash runs near the bottleneck
  rate from the second round after the request. Without a usable first estimate it gets there in
  O(log₄(BDP/IW)) rounds, versus slow start's O(log₂(BDP/IW)).
- Never worse than CUBIC + HyStart++ on low-BDP paths; harm ≤ that of a CUBIC flow (plan RQ4).
- Fewer ACKs than the RFC 9000 defaults (receiver CPU, asymmetric links), not more.
- Steady state stays CUBIC (RFC 9438) for coexistence.

## 2. Why this needs QUIC

| Mechanism | QUIC feature | Spec | Why TCP cannot |
|-----------|--------------|------|----------------|
| Probe trains tagged per packet | New extension frame `PROBE_TRAIN` | RFC 9000 §19.21 | No room for per-packet metadata: options ≤ 40 B, and middleboxes strip them (F1) |
| Receiver-computed train reports and alarms | New frame `TRAIN_REPORT`, an ACK supplement | RFC 9000 §19.21 | Would need a new TCP option on ACKs |
| Capability negotiation before the first data byte | Transport parameter in the ClientHello; unknown parameters ignored | RFC 9000 §7.4, §7.4.2 | SYN options can be stripped |
| ACK rate set by the sender, per phase | ACK_FREQUENCY, IMMEDIATE_ACK | draft-ietf-quic-ack-frequency-14 | The receiver alone owns TCP's ACK policy (B6) |
| Immediate ACKs at chosen packets | IMMEDIATE_ACK, or a skipped packet number | draft §5; RFC 9002 §6.2.4 | No way to request an immediate ACK |
| Exact per-packet attribution | Packet numbers never reused | RFC 9002 §4.2 | Retransmissions reuse sequence numbers (B1) |
| Exact lost ranges per round | Many irrevocable ACK ranges | RFC 9000 §19.3 | 3–4 SACK blocks, renegable (B3) |
| Clean RTT samples | ACK Delay field | RFC 9000 §13.2.5 | Timestamps include delayed-ACK time (B2) |
| Remaining bytes known to the CC | Streams and FIN in a user-space stack | RFC 9000 §2, §4.5 | The kernel CC sees a byte stream (E2) |
| Warm state carried by the client | Session ticket holding server state | RFC 9001 §4.5 | Kernel TCP cannot read TLS tickets (A5) |
| Amplification-aware first flight | Anti-amplification limit | RFC 9000 §8.1 | Constraint does not exist in TCP (A2) |

---

## 3. How it works (overview)

### 3.1 Roles: the receiver measures, the sender decides
- The **server** (data sender) owns every decision: rates, windows, train shapes, ACK settings.
- The **client** (data receiver) only timestamps tagged packets and reports facts about them: rates, delay
  changes, where queueing started, which packets were lost. It never chooses a rate.
- So the receiver stays simple and generic, and the policy can change on the server without updating
  clients.

### 3.2 Three kinds of trains

| Train | When | Shape | Tagged packets | Receiver measures | Sender uses it to |
|-------|------|-------|----------------|-------------------|-------------------|
| **IW train** | round 0 | the initial window (≤ 10 packets) as one back-to-back burst | all | receive rate (dispersion) | aim the first chirp, bound the first step |
| **Chirp** | start of every RAMP or RESUME round | 16–32 packets whose send rate rises by γ ≈ 1.149 per packet, from R to ≥ 8R | all | the turning point (the first packet that queued) and the receive rate behind it | locate the bottleneck, or grow ×4 if none was found |
| **Carrier** | rest of every RAMP round; whole CHECK phase | data paced at R | first, every 8th, last | one-way-delay trend, receive vs send rate | confirm that R builds no queue; raise an alarm if it does |

All train packets carry real stream data. Measurement costs only the ~10-byte tag.

### 3.3 One round, step by step
1. The report for the previous chirp arrives. The sender updates R, or leaves RAMP.
2. If the ACK settings change, the sender sends ACK_FREQUENCY (at most once per round).
3. It sends **chirp k**. Every packet carries PROBE_TRAIN; the last one also carries IMMEDIATE_ACK.
4. It sends **carrier k** at R until the next report arrives.
5. The receiver gets the chirp's last packet. It ACKs immediately and attaches the TRAIN_REPORT.
6. Throughout, the receiver watches the open carrier. If a queue builds, it sends an alarm report at once.

```
 sender (server)                                              receiver (client)
   | report k-1 arrives -> update R                               |
   | [ACK_FREQUENCY if settings change]                           |
   | chirp k: N pkts, rate R -> 8R, PROBE_TRAIN on each -------->| timestamps every packet
   | last chirp pkt + IMMEDIATE_ACK ---------------------------->| finds the turning point
   | carrier k: data at R, every 8th pkt tagged -------------->  | watches delay; alarm on queue
   |<--------------------------------- ACK + TRAIN_REPORT(chirp k) |
   | report k arrives -> update R, start round k+1                 |
```

### 3.4 Phases
- **IW_TRAIN**: round 0. The IW burst gives a first estimate R_iw.
- **RAMP**: chirp rounds. Each round either grows R ×4 (no turn found) or locates the bottleneck and leaves.
- **DRAIN**: only if the flight is larger than the new window; pace at 0.9·R until it fits (as in BBR).
- **CHECK**: one monitored RTT at the new rate. The receiver alarms if the rate was slightly too high.
- **CRUISE**: CUBIC (RFC 9438) with RFC 9002 pacing; ACKs thinned.
- **RESUME**: after idle, one validation round (a chirp around the old rate) instead of a blind restart.

The full state machine is in §7.1.

### 3.5 Safety invariants (the rules everything else follows)
1. **A carrier runs only at a rate a chirp has shown not to queue.** The exception is round 1, where R_1 ≤
   4× slow start's round-1 rate and ≤ 0.4× the IW estimate.
2. **The IW estimate only aims.** A single burst can be misleading (shapers), so it never sets a carrier rate
   alone.
3. **Without a located bottleneck, R grows at most ×4 per round.** A missed turning point implies at least
   5.3× headroom (§9.1), so ×4 cannot overshoot.
4. **A located bottleneck sets R directly**, to the highest chirp rate that did not queue. CHECK then catches
   small errors (§9.2).
5. **The receiver alarms** as soon as a carrier builds a queue. The sender drops R to the measured receive
   rate.
6. **ACKs are for reliability; measurement rides on trains.** ACKs can be thinned to ~4–8 per RTT, plus
   immediate ACKs at train ends and response tails.

### 3.6 Notation

| Symbol | Meaning | Value in §11 |
|--------|---------|--------------|
| S | packet size (max_datagram_size) | 1200 B (9600 bit) |
| R | carrier rate: the rate currently believed safe | 15.36 Mbit/s in round 1 |
| T_R = S/R | time between packets at rate R | 625 µs at 15.36 Mbit/s |
| γ | rate step between consecutive chirp packets | 8^(1/15) ≈ 1.1487 |
| r_i = R·γ^i | instantaneous send rate of chirp packet i | r_14 = 107.0 Mbit/s |
| g_i = S/r_i | send gap before chirp packet i | g_14 = 89.7 µs |
| N | packets per chirp | 16; up to 32 when aimed |
| D | chirp duration = T_R·(1 − γ^−(N−1))/(γ − 1); ≈ 5.9·T_R for N = 16 | 3.8 ms |
| A | available bandwidth at the bottleneck (unknown to Dash) | 100 Mbit/s |
| q_i | relative one-way delay of train packet i | 0 … 89 µs |
| d_i | q_i − q_(i−1) | 6–37 µs where queued |
| ε | receiver noise floor | 5 µs |

---

## 4. Negotiation and modes

**Transport parameter `dash_feedback`** (experimental codepoint, e.g. `0x2a3b4c00`, picked at random from the
large varint space, RFC 9000 §22.1):

```
dash_feedback {
  version (i),             # 1
  ts_resolution_us (i),    # receiver timestamp precision: ~1 us with kernel timestamps, more in user space
}
```

```
Client                                                          Server
Initial: ClientHello, transport parameters
   {..., max_ack_delay, min_ack_delay (ack-frequency ext.),
    dash_feedback{version 1, ts_resolution_us 1}}  ---------->
                                                  server knows the mode before its first data packet
                                <----------  Initial/Handshake: ServerHello ... (server's parameters)
```

- The **client** (data receiver) sends it. It arrives in the ClientHello, so negotiation costs no RTT.
- A stock client doesn't send it, and a stock QUIC peer ignores it (RFC 9000 §7.4.2), so interop is safe.
- **0-RTT**: the parameter MUST NOT be remembered (RFC 9000 §7.4.1 requires the definition to state this).
  PROBE_TRAIN goes only in 1-RTT packets, which is what the server sends anyway.

**Modes** (chosen by the server from the client's parameters):

| Mode | Client sent | Trains and reports | ACK steering | Immediate ACK at chosen packets | Startup |
|------|-------------|--------------------|--------------|---------------------------------|---------|
| **FULL** | `dash_feedback` + `min_ack_delay` | yes | ACK_FREQUENCY | IMMEDIATE_ACK | chirps (this document) |
| **FEEDBACK_ONLY** | `dash_feedback` only | yes | no (RFC 9000 default: every 2nd packet) | skip a packet number before the packet | chirps |
| **DASH_S** | neither | no | no | skip a packet number | sender-only (§10) |

---

## 5. Frames

Both frames follow RFC 9000 §19.21. Codepoints are experimental (e.g. `0x2a3b4c10`, `0x2a3b4c11`); a 4-byte
varint type keeps collision risk low.

### 5.1 PROBE_TRAIN (sender → receiver)

```
PROBE_TRAIN {
  Type (i) = 0x2a3b4c10,
  Train ID (i),            # increases per train within the connection
  Index (i),               # position in the train: 0, 1, 2, ...
  Length (i),              # planned packet count; 0 = open-ended (carrier, CHECK)
  Send Delta (i),          # microseconds since the train's packet 0 was sent
}
```

- **One frame per packet**, describing the packet it is in. In a coalesced datagram only the 1-RTT packet
  carries it.
- **Tagging.** IW trains and chirps: every packet. Carriers and CHECK: packet 0, every 8th packet, and the
  last one.
- **Open trains.** `Length = 0` means the end is not known in advance. The sender closes the train by sending
  a packet with `Length = Index + 1`, or by opening a newer train. **Opening a train closes every older open
  train**, so at most one train is open at a time.
- **Send Delta** is the scheduled departure offset (with SO_TXTIME the kernel releases at that time).
- **Chirp packets bypass GSO batching**, so each leaves at its own scheduled time.
- **Never retransmitted.** If a tagged packet is lost, its STREAM data is retransmitted normally *without*
  the tag, and the receiver sees a hole in Index.
- Size: about 10 B (4-byte type, 1–2-byte ID / Index / Length, 2–4-byte Send Delta), ≈ 0.8 % of a
  1200-byte packet.
- Rides in packets that are ack-eliciting and congestion controlled anyway, which satisfies §19.21.

### 5.2 TRAIN_REPORT (receiver → sender, ACK supplement)

```
TRAIN_REPORT {
  Type (i) = 0x2a3b4c11,
  Train ID (i),
  Packets Received (i),    # tagged packets received
  Bytes Received (i),      # all 1-RTT payload bytes received from the first to the last tagged packet
  RX Span (i),             # us between first and last received tagged packet
  TX Span (i),             # us between their Send Deltas
  OWD Trend (i),           # signed (zigzag) us: median q of last 3 tagged packets minus median q of first 3
  Turn Index (i),          # start of the final excursion (Section 6.4); = Length if none
  Excursion Rate (i),      # kbit/s received across the final excursion; 0 if none
  Lost Indices (i),        # missing Index values
  Flags (8),               # 0: compression suspected  1: truncated  2: loss after rising delay
                           # 3: alarm (train still open)
}
```

- **ACK supplement.** It MUST travel in a packet that also carries an ACK frame. Under RFC 9000 §19.21's
  exception it is then not congestion controlled and not ack-eliciting, the same treatment ACK frames get.
  Never retransmitted; if one is lost the sender uses its timeout rule (§7.2).
- **When sent**: on closing a train, and on an alarm (§6.8). An alarm report can be followed by a final report
  for the same train.
- Size: about 20–30 B; a few per RTT at most.
- **Consistency rule.** The sender checks that every packet a report claims was received is also acknowledged
  in ACK frames. A mismatch means the feedback is ignored for the rest of the connection, and Dash falls back
  to DASH_S (§9.4).

### 5.3 Existing frames Dash uses

| Frame | Spec | Sent by | Used for |
|-------|------|---------|----------|
| ACK_FREQUENCY (0xaf) | ack-frequency draft §4 | server | per-phase ACK thresholds, delay and reordering threshold (§8) |
| IMMEDIATE_ACK (0x1f) | draft §5 | server | last IW / chirp packet, last packet of a response, probes |
| ACK | RFC 9000 §19.3 | client | reliability, RTT samples; carries TRAIN_REPORTs |

---

## 6. Receiver estimator

Proven basis:
- packet-pair and train dispersion (Keshav SIGCOMM 1991; Dovrolis et al. ToN 2004);
- chirps and excursion detection (pathChirp PAM 2003; Paced Chirping 2019);
- receiver-driven rate estimation (Mehra & Zakhor INFOCOM 2003; ROS).

### 6.1 Per-packet records
- For every received packet that carries PROBE_TRAIN, record `(train, Index, rx = receive time,
  tx = Send Delta, bytes)`. While a train is open, also count the bytes of untagged 1-RTT packets (for sparse
  carriers).
- **Receive time**: a kernel timestamp (SO_TIMESTAMPNS) where possible, with GRO off on the measurement socket,
  or per-segment timestamps (plan §4.7). The advertised `ts_resolution_us` tells the sender how far to trust
  small delay changes.
- Process packets in Index order. A packet that arrives after a higher Index (reordered) is left out of the
  delay analysis.

### 6.2 Relative one-way delay
- `q_i = (rx_i − rx_f) − (tx_i − tx_f)`, where f is the first received packet of the train.
- The clock offset and the propagation delay cancel, so **no clock synchronisation is needed**. Clock skew
  (~100 ppm) adds < 0.5 µs over a 4 ms chirp and ~5 µs over a 50 ms carrier, both negligible against the
  thresholds.
- `d_i = q_i − q_prev` for consecutive received packets.

### 6.3 Noise floor
`ε = max(EPS_MIN, 2 × ts_resolution, 3 × σ_d)`, where σ_d is the standard deviation of d over the most
recent carrier or CHECK train (sent below the bottleneck rate, so d should be pure noise) and EPS_MIN = 5 µs.

### 6.4 Turning-point detection (chirps)
**Principle (fluid model).** Chirp packet i is sent g_i after packet i−1. If r_i ≤ A, the bottleneck has
finished packet i−1 before i arrives, so i passes undelayed. If r_i > A, packet i waits behind i−1, and its
delay grows by `d_i = S/A − S/r_i`. Rates rise along the chirp, so the delay stays flat up to the first rate
above A and then **rises ever faster**. The start of that rise locates A.

**Rule.**
1. A **run** is a maximal block of consecutive received packets with `d_i > ε`.
2. The **final excursion** is the run that ends at the last received packet. It must have length ≥ L (= 3)
   and rise `q_end − q_(start−1) ≥ Q_THR = max(θ × g_start, 2 × ts_resolution)`, with θ = 0.25. The
   threshold is scaled to the gap *at* the run's start, so it works at any rate.
3. **Turn Index** = start of the final excursion. **Excursion Rate** = bytes after the start packet ÷
   `(rx_end − rx_start)`. Queued packets leave the bottleneck back-to-back, so this is the rate the bottleneck
   served the chirp at (= A on an idle path).
4. Runs that end before the chirp does (the delay returned to within ε of its start) are **transient** cross
   traffic and are ignored, as in pathChirp.
5. No final excursion: Turn Index = Length ("no turn").

**What the sender concludes:** `r_(turn−1)` (the highest rate that did not queue) is a lower bound on A within
a factor γ (≈ 15 %). §11.3 traces this packet by packet.

### 6.5 Rates, delay trend, compression
- **Receive rate** = (Bytes Received − first packet) / RX Span. **Send rate** = the same bytes / TX Span.
- **OWD Trend** = median q of the last 3 tagged packets − median q of the first 3. Positive means a queue grew
  during the train.
- **Compression suspected** (flag 0) if either holds:
  - for a non-burst train, RX Span < 0.8 × TX Span (packets got bunched on the way: Wi-Fi aggregation, GRO);
  - ≥ 3 consecutive packets arrive within `ts_resolution` of each other (a merged receive batch). This is the
    only test that works for the IW burst.

### 6.6 Loss discrimination (stands in for ECN)
- Proven basis: receiver-side loss discrimination from inter-arrival times and delay († Biaz & Vaidya, IEEE
  ASSET 1999; † TCP Veno, Fu & Liew, JSAC 2003).
- For every hole in Index: if the 3 received packets before the hole belong to a run (`d > ε`), the queue was
  growing when the packet vanished. That looks like a queue overflow, so set flag 2 (loss after rising delay).
- A hole on flat delay is more likely random (link) loss.

### 6.7 Carrier alarm (open trains)
At every tagged packet of an open carrier or CHECK train, look at the last 4 tagged packets (about 32 data
packets). **Alarm** if:
- q rose by ≥ `ALARM_THR = clamp(srtt / 16, 1 ms, 8 ms)` (srtt is the receiver's own estimate). One-way delay
  is cleaner than RTT, hence half of HyStart++'s divisor; or
- receive rate < 0.9 × send rate over that window.

On alarm, send an ACK **immediately**, with a TRAIN_REPORT summarising the train so far and flag 3 set. At most
one alarm per train per srtt.

### 6.8 Report timing

| Event at the receiver | Action |
|-----------------------|--------|
| Last packet of a train with Length > 0 (it carries IMMEDIATE_ACK) | close the train; attach its report to that immediate ACK |
| First packet of a newer Train ID | close the older train; report with the next ACK. If it was an IW train or chirp whose last packet is missing: mark it truncated and ACK immediately |
| Alarm condition (§6.7) | immediate ACK with an alarm report; the train stays open |
| No packet of an open train for 2 × its expected duration | close as truncated; report with the next ACK |

### 6.9 Receiver pseudocode

```
on_tagged_packet(f, rx_time, size):
    if f.train_id is new: close_older_trains(f.train_id)          # one open train at a time
    t = trains[f.train_id]
    t.add(f.index, rx_time, f.send_delta, size)
    if t.open_ended and alarm(t): send_ack_now(report(t, ALARM))   # Section 6.7
    if f.length > 0 and f.index == f.length - 1: close(t)          # its IMMEDIATE_ACK fires the ACK

close(t):  queue_for_next_ack(report(t))   # immediate if t is a truncated IW train or chirp

report(t):
    recs = in_index_order(t)                          # reordered packets excluded
    q = [(r.rx - recs[0].rx) - (r.tx - recs[0].tx) for r in recs]
    d = diff(q); eps = max(EPS_MIN, 2*ts_res, 3*sigma_d_last_carrier)
    exc = final_run(d > eps)                          # must reach the last received packet
    turn = exc.start if exc and exc.len >= L and rise(exc) >= max(THETA*gap(exc.start), 2*ts_res)
           else t.length
    return {packets, bytes, rx_span, tx_span, owd_trend(q), turn, excursion_rate(exc),
            lost, flags(compressed, truncated, loss_after_rise, alarm)}
```

---

## 7. Sender algorithm

### 7.1 State machine

```
 IW_TRAIN --report #0--> RAMP --turn found--> [DRAIN] --> CHECK --no alarm--> CRUISE
                         ^  |                             ^  |                  |
                         +--+ no turn: R x4               +--+ alarm: R = rx    | idle
                                                                                v
                         RAMP <------ no turn ------ RESUME ------ turn -----> CHECK
```

| From | Event | To | Action |
|------|-------|----|--------|
| start | handshake done, mode FULL or FEEDBACK_ONLY | IW_TRAIN | send the IW as train #0 |
| IW_TRAIN | report #0 | RAMP | R = R_1 and aim the first chirp (§7.5) |
| RAMP | chirp report, no turn | RAMP | R = min(4R, top rate shown) |
| RAMP | chirp report, turn at index ≥ 2 | CHECK (via DRAIN if needed) | R = KAPPA_EXIT × r_(turn−1) |
| RAMP | turn at index ≤ 1, or carrier alarm | DRAIN → CHECK | R = measured receive rate |
| RAMP | congestion loss (§7.8) | CRUISE | cwnd = 0.7 × cwnd at loss, via PRR |
| RAMP | remaining data ≤ R × min_rtt | final round | no chirp; tail package (§7.9, §7.10) |
| DRAIN | bytes in flight ≤ R × min_rtt | CHECK | |
| CHECK | CHECK report without alarm | CRUISE | stop tagging |
| CHECK | alarm | DRAIN → CHECK | R = alarm's receive rate |
| CRUISE | idle > IDLE_THR, estimate fresh | RESUME | §7.11 |
| CRUISE | idle, estimate stale | IW_TRAIN | |
| RESUME | chirp report | CHECK, or RAMP if no turn | §7.11 |
| any | persistent congestion | slow start from kMinimumWindow, then RAMP once cwnd ≥ N_CHIRP packets | RFC 9002 §7.6 |
| any | inconsistent feedback, or 2 report timeouts in a row | DASH_S | §10 |

### 7.2 Report-clocked rounds
- **Round k starts when the report for the previous chirp (or the IW train) arrives.** It consists of chirp k
  followed by carrier k, and **ends when chirp k's report arrives**.
- Length ≈ D + RTT. The chirp goes first so that its information arrives as early as possible; a round is
  never longer than the feedback takes.
- The packets of round k are the packet numbers sent between the two report arrivals (C2). Loss rate and lost
  ranges are counted over exactly these.
- **Report deadline** = chirp end + smoothed_rtt + 4 × rttvar + 2 ms. No ACK delay is expected, because the
  last chirp packet carries IMMEDIATE_ACK.
- If the deadline passes: R = min(2R, 2 × ACK-based delivery rate) (slow-start-like growth), and start the next
  round. Two timeouts in a row switch to DASH_S.

### 7.3 Chirp schedule
- **Rates**: `r_i = R × γ^i` for i = 0 … N−1, with γ = 8^(1/15) ≈ 1.1487.
- **Gaps**: packet i is sent `g_i = S / r_i` after packet i−1.
- **Duration**: `D = T_R × (1 − γ^−(N−1)) / (γ − 1)`; for N = 16 that is ≈ 5.88 × T_R.
- **Default chirp**: N = 16, top = 8R, 19.2 KB at S = 1200 B.
- **Aimed chirp** (round 1 only): `top = max(8R, CHIRP_AIM × R_iw)` with CHIRP_AIM = 1.5, and
  `N = min(N_MAX, 1 + ⌈log_γ(top / R)⌉)`, N_MAX = 32.
- **Queue footprint**: at most a few packets. In §11 the round-1 chirp builds 89 µs of queue (~1 packet) and it
  drains within microseconds of the chirp's end.

| R | T_R (S = 1200 B) | D for N = 16 |
|---|------------------|--------------|
| 4 Mbit/s | 2.4 ms | 14.1 ms |
| 15 Mbit/s | 0.64 ms | 3.8 ms |
| 60 Mbit/s | 0.16 ms | 0.94 ms |
| 250 Mbit/s | 38 µs | 0.23 ms |

- **Pacing precision**: chirp gaps at the top of the chirp shrink to ~10 µs at Gbit/s rates, so they need
  SO_TXTIME + `fq` (or similar).
- **Flat fallback**: where the pacer cannot hold the gaps, send the upper part of the chirp as a back-to-back
  burst. The receiver's dispersion then measures capacity, not available bandwidth, so use the result with
  KAPPA_FLAT = 0.5.

### 7.4 Carrier, window and pacing
- **Carrier**: stream data paced at R. Tagged at packet 0, every 8th packet and the last packet; Length = 0
  (open).
- **Window in RAMP**: `cwnd = 2 × R × min_rtt + N × S`. It is a cap only; pacing is what limits sending.
- **CHECK**: for one srtt after leaving RAMP or RESUME, or after an alarm, pace at R with carrier-style tags and
  the alarm armed. CHECK ends when the CHECK train's report arrives without an alarm.
- **CRUISE**: CUBIC (RFC 9438) with RFC 9002 pacing (N = 1.25); no tags.

### 7.5 Round 0 and round 1
- **Round 0**: send `min(IW, response)` as one back-to-back burst (bursts ≤ IW are allowed, RFC 9002 §7.7):
  train #0, all packets tagged, IMMEDIATE_ACK on the last.
- **Using report #0**: `R_iw` = its receive rate. The estimate is **usable** if ≥ 4 packets arrived and it is
  neither compressed nor truncated.
- **First rate**:
  - `R_ss1 = 2 × IW / min_rtt` is the average rate of slow start's round 1 (3.84 Mbit/s at 50 ms).
  - Usable: `R_1 = min(KAPPA × R_iw, G_MAX × R_ss1)`, and the first chirp is aimed at `1.5 × R_iw`.
  - Not usable: `R_1 = R_ss1`, with a default (un-aimed) chirp.
- **Why both limits.** A single burst measures roughly the capacity of the narrowest link. A token-bucket shaper
  can let one burst through at line rate († Flach et al., *An Internet-wide analysis of traffic policing*,
  SIGCOMM 2016). So R_iw only *aims* the chirp, and the carrier stays within 4× what slow start would do in
  round 1. If R_iw is right, the aimed chirp locates the bottleneck in round 1 (§11); if it is wrong, the chirp
  shows that, at the cost of a few queued packets.

### 7.6 Rate update

```
on_chirp_report(rep):                         # end of round k
    sched = chirp_rates[rep.train_id]         # r_i of this chirp, kept by the sender
    if alarm_or_queue(carrier_report):        # carrier k-1 report arrives with chirp k's report
        return slow_down(carrier_report.rx_rate)
    if rep.turn <= 1:                         # the path cannot take even R
        return slow_down(min(R, rep.excursion_rate))
    if rep.turn < rep.length:                 # bottleneck located
        R = KAPPA_EXIT * sched[rep.turn - 1]; best_ok = R
        return exit_ramp()
    top_ok = sched[last_received_index(rep)]  # no turn: the path took everything up to top_ok
    best_ok = top_ok
    R = min(G_MAX * R, top_ok)                # grow x4 at most (Section 9.1)
    start_round()
```

- **Turn found**: R jumps to the highest rate shown not to queue. This is the evidence-based exit: the
  bottleneck is located, so going to just below it is the whole point. KAPPA_EXIT = 1.0 by default; CHECK
  catches the ≤ 5 % overestimate that late detection can cause (§9.2).
- **No turn**: ×4, not ×8. A turning point missed in the last packets of the chirp still implies ≥ 5.3R
  headroom (§9.1).
- **Turn at index ≤ 1, or carrier alarm**: R was already too high (path changed, or a shaper let earlier
  bursts through). Use the measured receive rate and DRAIN.

### 7.7 Exit, drain, CHECK and handover to CUBIC

```
exit_ramp():                                  # also used by slow_down(rate) with R = KAPPA_EXIT * rate
    cwnd = max(IW, R * min_rtt)
    state = DRAIN if bytes_in_flight > cwnd else CHECK
    cubic.init(cwnd_prior = cwnd, W_max = cwnd, K = 0)        # RFC 9438 Section 4.10
    pace(R)                                   # DRAIN paces at 0.9 * R instead
    close_open_trains(); open_check_train()   # carrier-style tags for one srtt, alarm armed
    set_ack_frequency(CRUISE)
```

- **DRAIN** empties whatever queue the old rate built, the same rule as BBR's Drain.
- **Why CHECK**: a turning point can be detected one packet late when the first queued packet's delay increase
  hides under ε, so R can be up to ~5 % above A (§9.2). That builds a queue slowly, and CHECK's alarm catches
  it within one RTT.
- **After CHECK** CUBIC grows from W_max with K = 0. In practice that is about +0.5 packet per RTT (the
  Reno-friendly term), so CRUISE starts essentially at the located rate.

### 7.8 Loss handling
- Loss detection is unchanged RFC 9002: packet threshold 3, time threshold 9/8.
- **In RAMP**, at the end of a round, a loss counts as **congestion** if any of these holds:
  - a report flags it as "loss after rising delay" (§6.6);
  - loss > 2 % of the round and ≥ N_RANGES = 6 lost ranges (BBR's test; exact from ACK ranges, B3);
  - "random" losses alone exceed 5 % of the round (safety net).
- Congestion in RAMP → leave RAMP with `cwnd = 0.7 × cwnd_at_loss`, reduced gradually with PRR (RFC 9937),
  then CUBIC. Otherwise retransmit only and use G_MAX = 2 for the next round.
- **In CHECK and CRUISE**: CUBIC, one reduction per recovery epoch (C2). Undo the cut if every loss of the
  epoch proves spurious (RFC 9438 §4.9.2; B4).
- **PTO** is never a congestion event (RFC 9002).
- **Persistent congestion**: cwnd = kMinimumWindow and min_rtt reset (RFC 9002 §7.6, §5.2). Then slow start
  until cwnd ≥ N_CHIRP packets, so the chirps fit the window again, then RAMP.

### 7.9 Size awareness and the final round
- **Remaining bytes**: `S_rem` = stream send buffers + FIN, summed over the connection.
- **Budget**: `budget = min(S_rem, R × min_rtt)`. Never chirp beyond the remaining data.
- **Responses ≤ IW** are sent as one burst; no further rounds.
- **Final round in RAMP** (`S_rem ≤ R × min_rtt`): no chirp, because there is no next round to plan. Send the
  rest paced at `max(R, min(best_ok, G_MAX × R))`, where best_ok is the highest rate a chirp has shown not to
  queue.
- **In CRUISE** the window governs. The end of the response only triggers the tail package.

### 7.10 Tail package
For short flows the tail is where recovery costs the most. Few packets follow a loss, so the packet threshold
can't fire, and a fresh PTO is ≈ 3·RTT + max_ack_delay (TCP_vs_QUIC C1).
- **Tail zone** = the last TAIL_ZONE = 16 packets of pending data. With its first packet, send
  ACK_FREQUENCY{threshold 0, min_ack_delay, reordering 1}, so every tail packet is ACKed at once.
- **Last packet** carries IMMEDIATE_ACK. In FEEDBACK_ONLY mode, skip a packet number just before it instead,
  and count *sent* packets for the packet threshold (RFC 9002 A.10 note).
- **Tail duplicates**: send copies of the last TAIL_DUP = 2 packets' STREAM frames right after them, if cwnd
  has room. Allowed by RFC 9000 §13.3; §2.2 says receivers drop duplicates.
- **Early tail probe**: arm a timer at `1.25 × smoothed_rtt + 2 ms`. No ACK delay is expected thanks to the
  immediate ACK, so this fires well before the PTO. If it expires, resend the unacknowledged tail frames once;
  no cwnd change.
- **On PTO**: send 2 probes if the remainder fits in 2 packets, each with IMMEDIATE_ACK. max_ack_delay can then
  be left out of the PTO (ack-frequency draft §7).
- After the response, the next response starts with the RAMP or CRUISE ACK settings again.

### 7.11 Idle and resume: validate instead of guess
- **Idle** = nothing in flight and nothing sent for longer than `IDLE_THR = max(PTO, 100 ms)`.
- **Fresh estimate** (last rate R_old measured less than `max(1 s, 10 × srtt)` ago) → **RESUME**:
  - **carrier** at `0.5 × R_old` (Careful Resume's cap, RFC 9959 §3.3);
  - **chirp first**, from `0.5 × R_old` up to `2 × R_old` (11 packets with the default γ).
- **Outcomes** of the resume chirp:
  - turn at index ≥ 2 → `R = r_(turn−1)`, then CHECK;
  - no turn → `R = R_old`, continue in RAMP (the path may now offer more);
  - turn at index ≤ 1, or an alarm → `R` = the measured receive rate, then DRAIN and CHECK (safe retreat).
- **Stale estimate** → IW_TRAIN, keeping min_rtt as a hint.
- QUIC does not mandate TCP's restart-after-idle (D2); Dash replaces it with a one-round measurement. §11.8 shows
  the numbers.

### 7.12 Warm start from a session ticket
- The server stores `{R, min_rtt, timestamp}` in its session ticket (RFC 9001 §4.5), not in a NEW_TOKEN token
  (RFC 9000 §8.1.3 caution).
- **On resumption**, the IW train doubles as Careful Resume's reconnaissance (RFC 9959 §3.2): abort the warm
  start if the new min RTT ≤ saved_rtt / 2.
- **Then a RESUME round** with `R_saved` as R_old: carrier at ≤ 0.5 × R_saved, chirp up to 2 × R_saved. This
  stays inside RFC 9959's jump cap, and the chirp replaces its validating phase.
- **0-RTT**: PROBE_TRAIN only exists in 1-RTT packets, so the first round after the handshake does the
  validation.

### 7.13 Handshake details
- `min_rtt` is seeded from the handshake ACKs, which are immediate (RFC 9000 §13.2.1). So the IW train can be
  sent the moment the request arrives.
- **Amplification limit.** If the server is amplification-limited (A2), train #0 is cut short: mark it
  truncated, so R_iw is not used. Clients we control send Initials > 1200 B so that the IW fits in the first
  flight.

---

## 8. ACK frequency as a control loop

Because the receiver measures the path, ACKs no longer carry timing information. They are needed only for loss
detection, window accounting and RTT samples. So the sender asks for **few regular ACKs, plus immediate ACKs at
exactly the packets where timing matters**.

| Phase | Ack-Eliciting Threshold | Requested Max Ack Delay | Reordering Threshold | IMMEDIATE_ACK on |
|-------|-------------------------|-------------------------|----------------------|------------------|
| Handshake, IW_TRAIN | client default (1) | client's max_ack_delay | 1 (default) | last IW packet |
| RAMP, RESUME | ⌊R × srtt / (4S)⌋, i.e. ≈ 4 ACKs per RTT | max(min_ack_delay, srtt/4) | 3 (= kPacketThreshold, draft §9) | last chirp packet |
| CHECK, CRUISE | max(1, ⌊cwnd / (8S)⌋), i.e. ≈ 8 ACKs per RTT (Custura et al. 2023) | max(min_ack_delay, srtt/4) | 3 | first packet after idle |
| Tail zone (last 16 packets) | 0 (ACK every packet) | min_ack_delay | 1 | last packet; probes |

Rules:
- **When to send ACK_FREQUENCY**: only on a phase change, or when the computed threshold changes by more than
  2×. At most once per round (draft §8.1).
- **Loss detection stays timely.** A reordering threshold of 3 makes the receiver ACK as soon as a gap could
  already be declared lost (draft §6.2, §9).
- **PTO**: use the maximum of the current requested max-ACK-delay and all values still in flight (draft §7,
  MUST). The delay may be left out when IMMEDIATE_ACK is in flight.
- **Alarms bypass thresholds**: the receiver ACKs immediately when it raises one (§6.7).
- **Bursts**: pacing turns each thinned ACK's release into a paced flow (draft §8.2), so thinning causes no
  line-rate bursts.
- **Effect**: in §11, about **53 ACKs** for a 2 MB transfer instead of ≈ 834 with RFC 9000's
  every-second-packet default, with no loss of measurement quality.

---

## 9. Safety (RFC 9743 checklist)

### 9.1 Why ×4 is safe when no turn is found
- Detection needs a final run of at least L = 3 packets. In the fluid model, if the run is long enough, its rise
  always exceeds θ × g_start: for 3 queued packets the rise is ≥ 0.82 × g_start, against θ = 0.25.
- So a turn can only be missed when the excursion starts within the chirp's last L packets, counting one more
  packet whose delay increase may hide under ε.
- Then `A ≥ r_(N−L−1) = top × γ^−L = 8R × γ^−3 ≈ 5.3R`.
- Growing to `G_MAX × R = 4R` therefore stays below A. §11.7 shows a missed turn handled exactly like this.

### 9.2 How wrong can a located turn be?
- **Early** (noise looks like a rise): R ends up too low. That is safe, but costs throughput; see §15.
- **Late by one packet** (the first queued packet's increase `S/A − S/r < ε`): the overestimate is at most
  `ε / (T_A − ε)`, about 5.5 % at 100 Mbit/s with ε = 5 µs.
- The queue then grows at ≤ 5.5 % of A. CHECK's delay alarm (≥ 3 ms) fires within about one RTT, and R drops to
  the measured receive rate.

### 9.3 Shapers and the carrier alarm
- A deep token bucket can let whole chirps pass unshaped, so a no-turn round can raise the carrier above the
  shaped rate. The ×4 cap bounds the overload; the alarm bounds its duration.
- Detection takes `ALARM_THR × A / (R − A)` to build the threshold queue, plus srtt/2 for the report to arrive.
- **Example**: R = 2.5A at A = 100 Mbit/s, srtt = 50 ms → ≈ 2 ms + 25 ms, i.e. ≈ 0.5 MB of excess. A buffer
  under ~1 BDP will drop some of it. This is the design's weakest case; test it explicitly (§14).

### 9.4 Lying receivers
- Receiver-reported rates are capped at `2 × ACK-confirmed delivery rate`.
- Reports must be consistent with ACK frames (§5.2); a mismatch means DASH_S.
- Skipped packet numbers during RAMP detect optimistic ACKs (RFC 9000 §21.4).

### 9.5 Everything else
- **Bursts**: only the IW (allowed) and chirps of ≤ 32 packets, spread over their gaps.
- **Severe congestion**: persistent congestion behaves as in RFC 9002.
- **Coexistence**: CRUISE is CUBIC; aggressiveness is confined to RAMP and to validated resumption.
- **Overhead**: ≈ 1.2 KB of tags, reports and ACK_FREQUENCY per 2 MB, against ~780 fewer ACK packets (§11.6).

---

## 10. Fallback modes

- **FEEDBACK_ONLY**: everything in this document except the ACK steering. ACKs follow the RFC 9000 default
  (every 2nd packet). Immediate ACKs at chirp ends and response tails use a skipped packet number (gap rule,
  RFC 9000 §13.2.1).
- **DASH_S** (sender-only, design v1), for clients without `dash_feedback`:
  - **startup**: paced slow start (pacing gain 2), growing ×4 only when the ACK-based delivery rate tracks the
    pacing rate and the ACK-delay-corrected RTT shows no queue; evidence cap 2 × measured delivery rate;
  - **exits**: HyStart++ delay exit and BBR plateau exit (N = 2 rounds); BBR-style loss test;
  - **tail**: tail duplicates, and a skipped packet number before the last packet.

  It is also the ablation arm that shows what receiver feedback adds.

---

## 11. Worked example: the best case

### 11.1 Setup and assumptions
- **Path**: bottleneck A = C = 100 Mbit/s (12,500 B/ms), base RTT 50 ms (25 ms each way), FIFO buffer of
  1 BDP = 625 KB. No cross traffic, no loss. This is a core-grid point of plan §5.5.
- **Sizes**: packets of S = 1200 B (one packet takes 96 µs at the bottleneck), IW = 10 packets = 12 KB. KB means
  1000 B; IP/UDP headers are ignored.
- **Connection**: already established, so min_rtt = 50 ms is known from the handshake. The client advertised
  `dash_feedback` (ts_resolution 1 µs) and `min_ack_delay`, so mode FULL.
- **Ideal conditions**: µs-accurate pacing (SO_TXTIME), exact receiver timestamps (ε = EPS_MIN = 5 µs).
- **Request**: one 2 MB object; T = 0 is when the client sends the request.
- **Baseline**: plan §1.3's slow-start model (IW, ×2 per RTT, no loss). HyStart++ behaves the same on this path
  until about round 6.

### 11.2 Timeline

| T (ms) | Who | What happens |
|--------|-----|--------------|
| 0.0 | client | sends the request |
| 25.0 | server | request arrives. **Round 0, IW_TRAIN**: 10 packets back-to-back, PROBE_TRAIN{id 0, idx 0–9, len 10}; IMMEDIATE_ACK on the last |
| 50.1–51.0 | client | the bottleneck has spaced the burst 96 µs apart: RX Span 864 µs → receive rate 10,800 B / 864 µs = **100 Mbit/s** |
| 51.0 | client | immediate ACK + TRAIN_REPORT{id 0: 10 packets, 12,000 B, RX Span 864 µs} |
| 76.0 | server | **Round 1, RAMP**. R_iw = 100 Mbit/s is usable. R_1 = min(0.4 × 100, 4 × 3.84) = **15.36 Mbit/s**; the chirp is aimed at 1.5 × R_iw = 150 Mbit/s, so N = 18. Sends ACK_FREQUENCY{threshold 20, delay 12.5 ms, reordering 3}, then chirp 1 (id 1): 18 packets, 15.4 → 162 Mbit/s, 3.8 ms long; then carrier 1 (id 2) at 15.36 Mbit/s |
| 101.0–105.0 | client | chirp packets 0–13 arrive undelayed; packets 14–17 queue (+6, +24, +52, +89 µs) → **Turn Index 14** (§11.3). Immediate ACK + TRAIN_REPORT{id 1: turn 14, excursion rate 100 Mbit/s} |
| 130.0 | server | **R = r_13 = 93.1 Mbit/s**; leave RAMP. cwnd = 93.1 Mbit/s × 50 ms = **582 KB**. In flight ≈ 96 KB < cwnd, so no DRAIN → **CHECK**: pace at 93.1 Mbit/s, tag every 8th packet (id 3), ACK_FREQUENCY{threshold 60, delay 12.5 ms, reordering 3}; CUBIC W_max = 582 KB, K = 0 |
| ≈180 | server | carrier-1 report: flat delay, receive rate = send rate = 15.36 Mbit/s (consistent) |
| ≈230 | server | CHECK report: flat, no alarm → **CRUISE**, stop tagging |
| 240.7 | server | remaining data (582 KB) ≤ cwnd: the final round begins (nothing changes in CRUISE) |
| 289.0 | server | **tail zone** (last 16 packets): ACK_FREQUENCY{threshold 0, min_ack_delay, reordering 1} |
| 290.7 | server | last packet with IMMEDIATE_ACK, then copies of the last 2 packets; tail timer armed (≈ 65 ms) |
| **315.8** | client | **last byte arrives: FCT = 315.8 ms** (slow start: 449.5 ms; ideal: 210 ms) |
| 340.8 | server | immediate ACK: tail timer cancelled, response complete |

### 11.3 Round 1's chirp, packet by packet
Rates rise by γ = 1.1487 per packet from R = 15.36 Mbit/s. A packet queues only if its rate exceeds 100 Mbit/s.
Its extra delay is then `d = 96 µs − g` (the bottleneck needs 96 µs per packet; the gap g is shorter).

| i | r_i (Mbit/s) | gap g_i (µs) | sent at (µs) | queueing q_i (µs) | d_i (µs) | receiver's view |
|---|--------------|--------------|--------------|-------------------|----------|-----------------|
| 0 | 15.36 | — | 0 | 0 | — | flat |
| 1 | 17.64 | 544.1 | 544 | 0 | 0 | flat |
| 2 | 20.27 | 473.7 | 1018 | 0 | 0 | flat |
| 3 | 23.28 | 412.3 | 1430 | 0 | 0 | flat |
| 4 | 26.74 | 359.0 | 1789 | 0 | 0 | flat |
| 5 | 30.72 | 312.5 | 2102 | 0 | 0 | flat |
| 6 | 35.29 | 272.0 | 2374 | 0 | 0 | flat |
| 7 | 40.54 | 236.8 | 2610 | 0 | 0 | flat |
| 8 | 46.56 | 206.2 | 2817 | 0 | 0 | flat |
| 9 | 53.49 | 179.5 | 2996 | 0 | 0 | flat |
| 10 | 61.44 | 156.3 | 3152 | 0 | 0 | flat |
| 11 | 70.58 | 136.0 | 3288 | 0 | 0 | flat |
| 12 | 81.07 | 118.4 | 3407 | 0 | 0 | flat |
| 13 | **93.13** | 103.1 | 3510 | 0 | 0 | flat: **highest rate that did not queue** |
| 14 | 106.97 | 89.7 | 3600 | 6.3 | 6.3 | > ε (5 µs): **run starts** |
| 15 | 122.88 | 78.1 | 3678 | 24.1 | 17.9 | rising |
| 16 | 141.15 | 68.0 | 3746 | 52.1 | 28.0 | rising |
| 17 | 162.14 | 59.2 | 3805 | 88.9 | 36.8 | rising, last packet |

- The run 14–17 has **length 4 ≥ L = 3**. Its **rise of 88.9 µs ≥ Q_THR** = max(0.25 × 89.7, 2) = 22.4 µs, and it
  reaches the last packet, so it is the final excursion → **Turn Index 14**.
- **Excursion Rate** = 3 × 1200 B over 288 µs (packets 14–17 leave the bottleneck back-to-back) = **100 Mbit/s**,
  exactly A.
- The report: 18 packets, 21,600 B, RX Span 3,894 µs, TX Span 3,805 µs, OWD Trend +52 µs, Turn 14, no loss.
- The sender looks up its schedule: `r_13 = 93.1 Mbit/s`, the lower bound on A (true A = 100: 7 % below, within
  γ). The chirp's whole queue footprint was 89 µs, about one packet.

### 11.4 Window per round: Dash vs slow start

| Round | Slow start cwnd | % of BDP | Dash state | Dash rate R | Dash window | % of BDP |
|-------|-----------------|----------|------------|-------------|-------------|----------|
| 0 | 12 KB | 1.9 | IW_TRAIN | — | 12 KB (IW burst) | 1.9 |
| 1 | 24 KB | 3.8 | RAMP | 15.36 Mbit/s | 96 KB carrier + 21.6 KB chirp | 15.4 (+3.5) |
| 2 | 48 KB | 7.7 | CHECK | 93.1 Mbit/s | 582 KB | 93.1 |
| 3 | 96 KB | 15.4 | CRUISE | 93.1 Mbit/s | 582 KB | 93.1 |
| 4 | 192 KB | 30.7 | CRUISE | 93.1 Mbit/s | 582 KB | 93.1 |
| 5 | 384 KB | 61.4 | CRUISE, ends at 290.7 ms | 93.1 Mbit/s | last 124 KB | — |
| 6 | 768 KB (> BDP: bottleneck-limited) | 100 | done | | | |

```
window per round, 1 char = 2 % of BDP (625 KB)
round  slow start                                            Dash (best case)
  0    #                                         2 %         #                                          2 %  IW train
  1    ##                                        4 %         ########++                         15 % + chirp  RAMP
  2    ####                                      8 %         ###############################################  93 %  CHECK
  3    ########                                 15 %         ###############################################  93 %  CRUISE
  4    ###############                          31 %         ###############################################  93 %
  5    ###############################          61 %         (done at 290.7 ms)
  6    ##################################################  100 %
```

Cumulative bytes sent at the end of each round:

| Round | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|-------|---|---|---|---|---|---|---|
| Slow start | 12 KB | 36 KB | 84 KB | 180 KB | 372 KB | 756 KB | 2 MB at 424.5 ms |
| Dash | 12 KB | 130 KB | 712 KB | 1,294 KB | 1,876 KB | 2 MB at 290.7 ms | — |

- **Slow start** doubles blindly and needs 6 rounds to reach the bottleneck rate.
- **Dash** measures in round 0 (the burst), confirms and locates the bottleneck in round 1 (the chirp), and is
  at 93 % of capacity from round 2.

### 11.5 Completion times

| Object | Ideal (RTT + S/C) | Slow start | Dash (best case) | Dash ÷ ideal | Slow start ÷ ideal | Headroom captured |
|--------|-------------------|------------|------------------|--------------|--------------------|-------------------|
| 100 KB | 58.0 ms | 201.3 ms | 146.9 ms | 2.53 | 3.47 | 38 % |
| 1 MB | 130 ms | 369.5 ms | 229.9 ms | 1.77 | 2.84 | 58 % |
| 2 MB | 210 ms | 449.5 ms | 315.8 ms | 1.50 | 2.14 | 56 % |
| 5 MB | 450 ms | 689.5 ms | 573.5 ms | 1.27 | 1.53 | 48 % |

- The slow-start column reproduces plan §1.3's ratios (3.5×, 2.8×, 2.1×, 1.5×).
- **100 KB**: after the IW, the remaining 88 KB fits in round 1's budget (R_1 × min_rtt = 96 KB). That makes
  round 1 the final round: no chirp, paced at R_1.
- **Headroom captured** = (slow start − Dash) / (slow start − ideal).

**Where the remaining 106 ms (2 MB) goes**, which is also the design space for improvements:

| Cost | ms | Why | Lever |
|------|----|-----|-------|
| Round 0 carries only the IW | ≈ 50 | 51 ms spent on 12 KB | only history removes it: session-ticket warm start (§7.12) |
| Round 1 runs at 15 % of C | ≈ 45 | R_1 capped at 4× slow start's round-1 rate (safety invariant 1) | trusting R_iw (R_1 = 0.4 × R_iw = 40 Mbit/s) would save ≈ 13 ms, but trusts a single burst (shapers) |
| CRUISE at 93 % of C | ≈ 11 | exit at r_13, the γ-quantized lower bound | finer γ near the turn, or using the Excursion Rate (§15) |

### 11.6 ACKs and feedback overhead (2 MB)

| Phase | Data packets | ACKs with Dash | Why |
|-------|--------------|----------------|-----|
| Round 0 (IW) | 10 | 5 | client default (every 2nd), immediate on the last |
| Round 1 (chirp + carrier) | 18 + 80 | ≈ 5 | 1 immediate at chirp end + every 12.5 ms on the carrier |
| CHECK + CRUISE | ≈ 1,540 | ≈ 25 | one ACK per 61 packets (threshold 60) |
| Tail zone + 2 copies | 16 + 2 | 18 | threshold 0 |
| **Total** | ≈ 1,670 | **≈ 53** | RFC 9000 default (every 2nd packet): **≈ 834** |

Feedback bytes:
- PROBE_TRAIN tags: IW 10 + chirp 18 + carrier ≈ 11 + CHECK ≈ 62 → ≈ 100 tags × ≈ 10 B ≈ 1.0 KB.
- 4 TRAIN_REPORTs (≈ 100 B) and 3 ACK_FREQUENCY frames (≈ 36 B).

That is ≈ 1.2 KB forward, or 0.06 % of the object, against ≈ 780 fewer ACK packets on the reverse path.

### 11.7 Variant: same path, IW estimate unusable
Suppose GRO at the client merged the IW burst, so report #0 carries the compression flag. Then R_1 = R_ss1 =
3.84 Mbit/s, with default (un-aimed) chirps of 16 packets up to 8R. This shows how RAMP advances when no turn is
found:

| Round | Starts at T (ms) | R (carrier) | Chirp range, duration | Chirp outcome | Next R | Sent in round | Cumulative |
|-------|------------------|-------------|-----------------------|---------------|--------|---------------|------------|
| 0 | 25.0 | — | IW burst | compressed → no R_iw | 3.84 | 12.0 KB | 12.0 KB |
| 1 | 76.0 | 3.84 Mbit/s | 3.8 → 30.7 Mbit/s, 14.7 ms | no queue anywhere: no turn | min(4 × 3.84, 30.7) = 15.36 | 43.2 KB | 55.2 KB |
| 2 | 140.8 | 15.36 Mbit/s | 15.4 → 122.9 Mbit/s, 3.7 ms | queue starts at packet 14 (107 Mbit/s), but the run of 2 < L: **missed** → no turn | min(4 × 15.36, 122.9) = 61.44 | 115.4 KB | 170.6 KB |
| 3 | 194.6 | 61.44 Mbit/s | 61.4 → 491.5 Mbit/s, 0.9 ms | run 4–15 (12 packets, +590 µs): **turn at 4** | r_3 = 93.1 | 408.5 KB | 579.2 KB |
| 4 | 246.2 | 93.1 Mbit/s | — (CHECK → CRUISE, cwnd 582 KB) | — | — | 582 KB per RTT | done at 368.3 ms |

- **FCT(2 MB) ≈ 393 ms**: 1.87× ideal, vs slow start's 2.14× and the best case's 1.50×.
- Each no-turn round multiplies the carrier by 4, two slow-start doublings per round. Windows of 24 → 96 → 384 KB
  equal slow start's rounds 1, 3 and 5.
- Round 2 is §9.1 in action: the missed turn leaves R = 61.4 Mbit/s, safely below A = 100.

### 11.8 Second request after 0.5 s of idle
- A 1 MB request arrives on the same connection. The estimate is fresh (0.5 s < 1 s), so **RESUME** with
  R_old = 93.1 Mbit/s:
  - carrier at 46.6 Mbit/s;
  - chirp of 11 packets from 46.6 to 186 Mbit/s (1.0 ms).
- The queue starts at packet 6 (107 Mbit/s): run 6–10, +133 µs → turn at 6 → R = r_5 = **93.1 Mbit/s** → CHECK
  → CRUISE. The report arrives 51.3 ms after the request did.

| Policy for the 2nd request | FCT (1 MB) | Note |
|----------------------------|------------|------|
| Ideal | 130.0 ms | |
| Keep the old cwnd, no validation (allowed by RFC 9002) | 136.0 ms | blind: unsafe if the path changed |
| **Dash RESUME** | **161.0 ms** | one validation round at half rate |
| TCP restart-after-idle (RFC 5681 §4.1) | 369.5 ms | slow start from the IW again |

- **If the path had changed** (say A dropped to 30 Mbit/s during the idle), the resume chirp would queue from
  packet 1. Turn index 1 → R = the Excursion Rate ≈ 30 Mbit/s, or the carrier alarm fires, whichever comes first.
- Either way this happens about one RTT after the request. The ≈ 100 KB queue built at 46.6 Mbit/s is drained
  before CHECK.

---

## 12. Sender pseudocode (consolidated)

```
on_handshake_done():
    mode = FULL if peer.dash_feedback and peer.min_ack_delay
           else FEEDBACK_ONLY if peer.dash_feedback else DASH_S
    if mode == DASH_S: return run_dash_s()                      # Section 10
    state = RESUME if fresh_ticket_state() else IW_TRAIN        # Section 7.12

on_response_ready():
    if state == IW_TRAIN: return send_iw_train()                # Section 7.5
    if idle_for() > IDLE_THR: return fresh(R) ? start_resume() : restart_iw_train()

on_ack(ack, reports):
    rfc9002_loss_detection(ack); rfc9002_rtt_update(ack)
    for rep in reports:
        if not consistent_with_acks(rep): mode = DASH_S; return
        if rep.alarm or queue_seen(rep): slow_down(rep.rx_rate)            # Sections 6.7, 7.6
        elif rep.train is IW: on_iw_report(rep)
        elif rep.train is current chirp: on_chirp_report(rep)              # Section 7.6
        elif rep.train is CHECK: state = CRUISE; stop_tagging()
    if state in {CHECK, CRUISE}: cubic_on_ack(ack)

on_iw_report(rep):
    R_ss1 = 2 * IW / min_rtt
    if usable(rep): R = min(KAPPA * rep.rx_rate, G_MAX * R_ss1); aim = CHIRP_AIM * rep.rx_rate
    else:           R = R_ss1; aim = none
    state = RAMP; start_round(aim)

start_round(aim = none):
    if S_rem() <= R * min_rtt: return final_round()               # Section 7.9
    set_ack_frequency(RAMP)                                       # only if values changed
    top = max(CHIRP_TOP * R, aim or 0)
    N = min(N_MAX, 1 + ceil(log(top / R) / log(GAMMA)))
    send_chirp(base = R, N)        # tagged, scheduled gaps, last packet IMMEDIATE_ACK
    open_carrier(rate = R)         # tagged 0, every 8th, last; Length = 0
    arm_report_deadline(chirp_end + srtt + 4 * rttvar + 2 ms)     # Section 7.2

on_report_deadline():
    R = min(2 * R, 2 * ack_delivery_rate_max()); timeouts += 1
    if timeouts >= 2: mode = DASH_S
    else: start_round()

on_tail_zone():   set_ack_frequency(TAIL); immediate_ack(last); send_copies(TAIL_DUP); arm_tail_timer()
on_tail_timer():  resend unacknowledged tail frames once             # no CC change
on_pto():         probes (2 if remainder <= 2 packets) with IMMEDIATE_ACK; no CC change
on_loss_round(r): see Section 7.8
on_persistent_congestion(): cwnd = kMinimumWindow; min_rtt = latest_rtt; slow start until cwnd >= N_CHIRP
```

---

## 13. Parameters (initial values; tune on the training subset)

| Parameter | Default | Sweep | Basis |
|-----------|---------|-------|-------|
| KAPPA (first step from R_iw) | 0.4 | 0.25, 0.4, 0.6 | ROS |
| Round-1 cap | G_MAX × 2 × IW / min_rtt | ×2, ×4, ×8 | §7.5, SUSS-like |
| CHIRP_AIM | 1.5 × R_iw | 1.2–2 | new |
| γ (rate step) | 8^(1/15) ≈ 1.149 | 1.1–1.3 | pathChirp (1.2) |
| N_CHIRP / N_MAX | 16 / 32 | 8–32 | pathChirp, Paced Chirping |
| CHIRP_TOP | 8 × R | 4–16 | new; must keep top × γ^−L > G_MAX (§9.1) |
| L (min run length) | 3 | 2–5 | pathChirp |
| θ (rise threshold) | 0.25 × gap at run start | 0.1–0.5 | new |
| EPS_MIN | 5 µs | 2–50 µs | timestamp noise |
| G_MAX | 4 | 2, 4 | §9.1 bound |
| KAPPA_EXIT | 1.0 × r_(turn−1) | 0.8–1.0 | lower bound already; CHECK catches overestimates |
| ALARM_THR | clamp(srtt/16, 1 ms, 8 ms) | ½×, 2× | HyStart++-like |
| Alarm rate test | rx < 0.9 × tx over 4 tags | 0.8–0.95 | new |
| Carrier tagging | every 8th packet | 4–32 | overhead vs reaction time |
| RESUME | carrier 0.5 × R_old; chirp to 2 × R_old | 0.25–1.0 | RFC 9959 cap |
| FRESH | max(1 s, 10 × srtt) | 0.5–5 s | plan PathState |
| ACK threshold, RAMP | packets per ¼ srtt at R | ⅛–½ srtt | draft §8.1 |
| ACK threshold, CRUISE | cwnd_pkts / 8 | /4, /8, /32 | Custura et al. 2023 |
| TAIL_ZONE / TAIL_DUP | 16 / 2 packets | 8–32 / 0–4 | Halfback-lite |
| Tail timer | 1.25 × srtt + 2 ms | 1.25–2 × srtt | TLP (2 × SRTT) |

---

## 14. Evaluation sketch (reuses plan §5)

**Ablation ladder** (one component per step):

| Arm | Contents |
|-----|----------|
| Q0 | CUBIC + HyStart++ (baseline A1) |
| Q1 | Q0 + Dash's ACK-frequency policy only (does thinning hurt or help?) |
| Q2 | DASH_S (sender-only) |
| Q3 | + receiver feedback with flat trains |
| Q4 | + chirps (aimed in round 1) |
| Q5 | + receiver loss discrimination |
| Q6 | + tail package |
| Q7 | + RESUME and ticket warm start |

**Golden traces** (integration tests, plan §5.2). On the §11 path with offloads off and kernel timestamps:
- **Best case**: leave RAMP after round 1 with R ∈ [0.87, 1.0] × C; FCT(2 MB) ≈ 316 ms ± 5 %; ≈ 50–60 ACKs.
- **IW estimate disabled**: exit after round 3; FCT(2 MB) ≈ 393 ms.
- **Resume after 0.5 s**: R back to ≈ 93 Mbit/s after one round; FCT(1 MB) ≈ 161 ms.

These are model numbers. Whichever assumption breaks (pacing granularity, GRO, timer jitter) shows up as a
difference, and that is the point of the test.

**Other baselines**: BBRv3, SEARCH, Careful Resume (neqo-style), a large IW.

**Workloads**:
- W1, sizes 10 KB – 50 MB;
- W3, think time: resumption;
- W5, synchronized starts: chirp interference;
- W6: harm;
- W7:
  - random loss 0.1–2 % (loss discrimination);
  - netem slot aggregation (compression flag);
  - ACK thinning;
  - **a token-bucket shaper with a deep bucket** (§9.3, the weakest case);
  - shallow FIFO buffers.

**Extra metrics**:
- turning-point error vs the true available rate;
- rounds to leave RAMP; exit reasons;
- ACKs per RTT and receiver CPU;
- feedback bytes per transfer;
- alarms per transfer;
- share of losses classified random vs truly random (known in the testbed).

---

## 15. Risks and open questions

- **Pacing granularity.** Chirps need µs spacing at high rates (≈ 10 µs at 1 Gbit/s). quinn's ~1 ms timers and
  ≥ 10-packet bursts (plan §4.7) restrict chirps to low rates. Use SO_TXTIME + `fq`, or the flat fallback.
- **Receiver timestamp noise.** GRO and user-space timestamps raise ε. A larger ε delays turn detection by a
  packet (the §9.2 bound grows) and makes premature turns from jitter more likely. A premature turn leaves Dash
  in CRUISE at a low rate, which CUBIC reclaims only slowly.
  - Candidate mitigation: a confirmation chirp in CHECK when r_(turn−1) < 0.5 × R_iw.
- **Exit quantization.** Leaving at r_(turn−1) costs up to γ − 1 ≈ 15 % of throughput (7 % in §11) until CUBIC
  grows. Options:
  - use the Excursion Rate, clamped to [r_(turn−1), r_turn]. It is exact on idle paths but overestimates with
    FIFO cross traffic;
  - a finer γ in a refinement chirp.
- **Token-bucket shapers.** Chirps can pass a deep bucket unshaped. The alarm bounds the damage but does not
  prevent it (§9.3).
- **FQ bottlenecks.** Turning points measure the fair share, which is the right target, but it changes how
  results should be read (README §Fair Queueing).
- **Self-interference.** Several Dash flows chirping at once can see each other as cross traffic (W5).
- **Loss discrimination without ECN** fails when a shallow buffer overflows before the delay rises measurably.
  The 5 % safety net bounds this.
- **Round length.** A round is RTT + D, and at low rates D is not negligible (14.7 ms at 3.84 Mbit/s,
  §11.7). Starting chirps at 2R would halve D, but changes the §9.1 bound.
- **Draft dependence.** FULL mode needs the ACK-frequency extension, whose draft expired in Aug 2026 (quinn
  still implements it). FEEDBACK_ONLY and DASH_S cover clients without it. Two new frames need interop testing
  against stock clients.

## 16. Adding ECN later (towards Sprint)

- `TRAIN_REPORT` gains `CE Packets` and `First CE Index`. A CE mark inside a chirp is an even sharper turning
  point than the delay rise (plan §3.6.4, E4).
- A CE in a carrier or CHECK train means a proportional, DCTCP-style cut. No CE on a validated path strengthens
  the "random loss" verdict.
- The state machine is unchanged; only the report handling and the loss classifier gain inputs.
