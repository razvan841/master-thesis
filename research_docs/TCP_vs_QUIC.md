# TCP vs QUIC: spec differences a congestion controller can use

Companion to [README.md](README.md) (QUIC background, and the RFC 9002 §4 summary table in §Differences from
TCP), [Double_Connection_Plan.md](Double_Connection_Plan.md) (plan A and Sprint), [Uses_Cases.md](Uses_Cases.md)
and [Reading_List.md](Reading_List.md) §3 (signals the CC can measure).

**Purpose.** List the differences between the TCP and QUIC specifications that a congestion controller tuned for
**short-flow completion time** can exploit or has to respect. Each one links to the exact sections on both sides
and suggests how Sprint, plan A (dual lane) or plan B (stream-aware single connection) can use it.

**Checked** against the RFC and draft texts on 28 Oct 2026. Draft status is in §I. Two drafts changed since the
reading list was written: ack-frequency expired, and multipath reached the RFC Editor queue.

**Entry format.**
- **TCP**: what TCP does, with links.
- **QUIC**: what QUIC does, with links.
- **Use it**: ideas for a short-flow CCA. These are hypotheses; each needs an experiment.
- **Watch out**: only where there is a trap.

Links go to section anchors on rfc-editor.org (RFCs) and on the datatracker (drafts, pinned to the version that
was checked). MUST / SHOULD / MAY are quoted from the specs.

---

## At a glance

Priority: ★★★ changes Sprint's design or the evaluation · ★★ worth an experiment or an implementation detail ·
★ background, one sentence in the thesis.

| # | Difference | What it gives a short flow | Where it plugs in | Pri |
|---|------------|----------------------------|-------------------|-----|
| A1 | 1-RTT / 0-RTT handshake vs 2 RTT for TCP + TLS 1.3 | fewer RTTs before the request; clean early RTT samples | W8, pacing train #0 | ★★ |
| A2 | Anti-amplification limit (QUIC only) | server's first flight capped at 3× the bytes received | Sprint IW_PROBE, W8 | ★★★ |
| A3 | IW10 is Standards Track in QUIC, Experimental in TCP; min cwnd 2 vs 1 | nothing to exploit; pin it | testbed | ★ |
| A4 | Handshake loss is not special | a lossy handshake does not shrink the IW | Sprint loss test | ★★ |
| A5 | State carriers across connections (ticket, token, cached RTT) | warm start without a host-local cache | PathState, warm start | ★★★ |
| B1 | Packet numbers are never reused | unambiguous RTT and delivery samples; exact H1/H2 attribution | Sprint v0 | ★★★ |
| B2 | Explicit ACK delay, max_ack_delay contract | cleaner queueing-delay signal; the client controls the server's PTO | delay backstop, tail | ★★ |
| B3 | Many irrevocable ACK ranges; exact bytes in flight | exact lost-range counting | Nimble/BBR loss test | ★★ |
| B4 | Spurious loss is trivially detectable | cheap undo of wrong cuts | Sprint undo | ★★★ |
| B5 | ECN: counts, immediate CE ACK, ECT allowed on every packet | earliest and most precise exit signal | E1–E6 | ★★★ |
| B6 | Sender can steer the ACK rate (extension, or a skipped packet number) | dense ACKs in startup, immediate tail ACK | Sprint, bulk lane | ★★★ |
| B7 | No one-way delay in QUIC v1 (TCP has timestamps) | QUIC is behind here; fill the gap | RATE_REPORT | ★★ |
| B8 | Loss of ACK-only packets is detectable | reverse-path congestion signal | — | ★ |
| C1 | PTO replaces RTO + TLP; no 1 s floor; no cwnd collapse | tail loss costs about one PTO | tail handling | ★★★ |
| C2 | Loss epoch ends at the first ACK of a post-loss packet | ≤ 1 cut per lossy round; clean round boundaries | Sprint rounds | ★★ |
| C3 | Persistent congestion instead of RTO collapse | safety clause for free | safety argument | ★★ |
| C4 | Reordering thresholds may be adaptive or start smaller | faster loss detection at the tail | tail handling | ★★ |
| D1 | Pacing is a SHOULD; bursts limited to the IW | the pacer may wrap the CC | `pacing_rate()` hook | ★★ |
| D2 | No restart-after-idle rule | warm cwnd survives think time | W3/W4 confound, warm start | ★★★ |
| D3 | cwnd must not grow while under-utilized | the last round of a short flow is app-limited | Sprint plateau logic | ★★ |
| D4 | Credit-based flow control; credit cannot shrink | no rwnd ramp; limits ROS-via-MAX_DATA | §3.7 variant, testbed | ★★ |
| D5 | QUIC re-sends frames, not bytes; copies are allowed | proactive tail redundancy; express-first retransmission | tail handling, plan B | ★★★ |
| E1 | Streams without cross-stream head-of-line blocking | express data unaffected by bulk losses | plan B | ★★ |
| E2 | The CC lives next to the application | flow size, FIN and blocked state are known | S_rem, size hints | ★★ |
| E3 | Connection IDs and migration (CC reset per path) | cold start after a network change | testbed hygiene | ★ |
| E4 | Multipath QUIC: per-path CC, several paths on one 4-tuple | dual lane inside one connection | plan A alternative | ★★ |
| F1 | Transport parameters + extension frames vs 40 bytes of TCP options | deployable PROBE / RATE_REPORT | plan §3.7 | ★★★ |
| F2 | ACKs are encrypted; no ACK thinning, no splitting PEPs | ACK clock not distorted by middleboxes | ROS motivation | ★★ |
| F3 | Intentional packet number gaps | optimistic-ACK defence; forced immediate ACK | safety, tail | ★★ |
| F4 | 1200-byte minimum datagram, DPLPMTUD | IW in bytes depends on the MTU | pin MTU | ★ |

---

## A. Connection start (where a short flow spends most of its life)

### A1. Handshake round trips

**TCP**
- Three-way handshake ([RFC 9293 §3.5](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.5)), then
  TLS 1.3 adds another RTT, so the request leaves the client after 2 RTT.
- TCP Fast Open carries data in the SYN and saves one RTT
  ([RFC 7413 §6.2](https://www.rfc-editor.org/rfc/rfc7413.html#section-6.2)). It is Experimental, and
  middleboxes interfere with it ([RFC 7413 §7.1](https://www.rfc-editor.org/rfc/rfc7413.html#section-7.1)).

**QUIC**
- Transport and cryptographic handshake are one exchange
  ([RFC 9000 §7](https://www.rfc-editor.org/rfc/rfc9000.html#section-7), flows in
  [§7.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-7.1)). The request leaves after 1 RTT, or in
  the first flight with 0-RTT ([RFC 9001 §4.6](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.6)).
- The server may send 1-RTT data before the handshake completes ("0.5-RTT data";
  [RFC 9001 §5.7](https://www.rfc-editor.org/rfc/rfc9001.html#section-5.7)).
- Initial and Handshake packets MUST be acknowledged immediately
  ([RFC 9000 §13.2.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.2.1)), so the first RTT samples
  carry no ACK delay. A Retry or Version Negotiation packet also gives the client an RTT estimate
  ([RFC 9002 §6.3](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.3)).

**Use it**
- With a full handshake, the server holds an undelayed RTT sample when the request arrives. Sprint can pace
  train #0 (the IW) at a known rate from the first data packet instead of bursting it.
- With 0-RTT the request arrives before any sample exists. RFC 9002 lets a resumed connection start from the
  previous connection's smoothed RTT ([§6.2.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.2));
  carry it in the session ticket (A5).
- Report FCT_conn (W8) next to FCT_req. 0-RTT removes a whole RTT that no CCA can win back; at 200 ms that is
  larger than many startup gains, and readers need that scale.

**Watch out**
- The Application Data PTO MUST NOT be armed before the handshake is confirmed
  ([RFC 9002 §6.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.1)). A lost 0.5-RTT or 0-RTT
  tail packet therefore waits for the handshake.
- 0-RTT data can be replayed ([RFC 9001 §9.2](https://www.rfc-editor.org/rfc/rfc9001.html#section-9.2)):
  idempotent requests only, which the file server's requests are.

### A2. Anti-amplification limit (QUIC only)

**TCP**: no equivalent. The handshake validates the client's address before any data flows; only TFO data in
the SYN-ACK is exposed ([RFC 7413 §5.2](https://www.rfc-editor.org/rfc/rfc7413.html#section-5.2)).

**QUIC**
- Before the client's address is validated, the server MUST NOT send more than three times the bytes it
  received ([RFC 9000 §8.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-8.1)). Client Initials are
  padded to at least 1200 bytes and MAY be larger
  ([RFC 9000 §14.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-14.1)).
- The limit "does not directly affect the congestion window"
  ([RFC 9002 §7.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.2)). PTO rules while blocked:
  [RFC 9002 §6.2.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.2.1).
- A NEW_TOKEN token from an earlier connection removes the limit
  ([RFC 9000 §8.1.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-8.1.3)). Tokens can be abused to
  aim an initial window at a victim ([RFC 9000 §21.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-21.3)).

**Use it** (README §Anti-amplification limit has the arithmetic)
- While amplification-blocked, the server is not cwnd-limited, so cwnd must not grow
  ([RFC 9002 §7.8](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.8)). Tag the ACKs of that phase as
  app-limited in Sprint. Otherwise train #0 measures the limit instead of the path, R_iw comes out too low, and
  κ × R_iw starts too low.
- The client is yours: send Initials larger than 1200 bytes (allowed by §14.1) so the server's first flight can
  be a full IW. Make "client Initial size" a factor in W8 instead of a hidden setting.
- Log amplification-blocked time per transfer, so FCT gains are attributed to the right mechanism.

### A3. Initial and minimum window

**TCP**: an IW of 2–4 segments ([RFC 5681 §3.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-3.1)).
IW10 = min(10·MSS, max(2·MSS, 14600)) is Experimental
([RFC 6928 §2](https://www.rfc-editor.org/rfc/rfc6928.html#section-2)). After an RTO the window is 1 segment
([RFC 5681 §3.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-3.1)).

**QUIC**: IW10 is the Standards-Track SHOULD: min(10·max_datagram_size, max(14720, 2·max_datagram_size)) in
UDP-payload bytes, recomputed if the datagram size changes
([RFC 9002 §7.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.2)). The minimum window is 2 packets
([RFC 9002 §4.8](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.8)).

**Use it**: little to exploit. Stacks deviate (quic-go uses 32 packets, plan §4.1), and a deviation from the
SHOULD has to be justified under [RFC 8085 §3.1](https://www.rfc-editor.org/rfc/rfc8085.html#section-3.1) and
RFC 9743. Pin IW and MTU across arms (plan §4.7). The 2-packet floor is also Sprint's floor: never cut R below
2 packets per RTT.

### A4. Handshake loss is not special

**TCP**
- A lost SYN or SYN-ACK sets the IW to 1 segment
  ([RFC 5681 §3.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-3.1)). RFC 6928 recommends keeping the
  IW unless there was more than one retransmission
  ([RFC 6928 §2](https://www.rfc-editor.org/rfc/rfc6928.html#section-2)).
- The initial RTO is 1 s, and after a SYN timeout it is reset to 3 s for the data phase
  ([RFC 6298 §2](https://www.rfc-editor.org/rfc/rfc6298.html#section-2) rule 2.1,
  [§5](https://www.rfc-editor.org/rfc/rfc6298.html#section-5) rule 5.7).

**QUIC**
- Handshake losses are treated like any other loss
  ([RFC 9002 §4.9](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.9)); the IW is not reduced.
- Loss of packets the peer could not yet decrypt MAY be ignored
  ([RFC 9002 §7.4](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.4)). Initial and Handshake packets
  leave bytes in flight without a congestion signal when their keys are discarded
  ([RFC 9002 §6.4](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.4)).
- Persistent congestion cannot be declared before the first RTT sample
  ([RFC 9002 §7.6.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.6.2)).

**Use it**
- On lossy access links (W7), a handshake loss costs QUIC one PTO and nothing afterwards. Keep it that way:
  exclude Initial- and Handshake-space losses from Sprint's congestion-loss test (> 2 % and ≥ 6 ranges).
- ECN counts are kept per packet number space
  ([RFC 9000 §13.4.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.4.1)). Compute Sprint's CE
  fractions from the Application Data space only.

### A5. Carrying path state across connections

**TCP**
- Host-local cache: RTT, RTTVAR, ssthresh and cwnd may initialise a new connection (temporal sharing,
  [RFC 9040 §6.1](https://www.rfc-editor.org/rfc/rfc9040.html#section-6.1)) or be split among concurrent ones
  (ensemble sharing, [§7.1](https://www.rfc-editor.org/rfc/rfc9040.html#section-7.1)), with caveats about
  whether the path is really the same ([§8](https://www.rfc-editor.org/rfc/rfc9040.html#section-8)).
  RFC 9040 is Informational.
- The only carrier on the wire is the TFO cookie
  ([RFC 7413 §4.1](https://www.rfc-editor.org/rfc/rfc7413.html#section-4.1)).

**QUIC**
- "Resumed connections over the same network MAY use the previous connection's final smoothed RTT value"
  ([RFC 9002 §6.2.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.2)).
- Session tickets can carry server state ([RFC 9001 §4.5](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.5)).
  The server may keep an integrity-protected copy of its transport parameters in the ticket
  ([RFC 9000 §7.4.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-7.4.1)).
- NEW_TOKEN tokens are opaque to the client but not bound to the handshake. The spec suggests limiting them to
  address-validation information ([RFC 9000 §8.1.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-8.1.3),
  [§8.1.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-8.1.4)).
- Careful Resume (RFC 9959, Standards Track): reconnaissance in
  [§3.2](https://www.rfc-editor.org/rfc/rfc9959.html#section-3.2), the jump in
  [§3.3](https://www.rfc-editor.org/rfc/rfc9959.html#section-3.3), QUIC-specific limits (proactive MAX_DATA,
  amplification) in [§4.7](https://www.rfc-editor.org/rfc/rfc9959.html#section-4.7).

**Use it**
- Put Sprint's warm-start state (bw_est, min_rtt, ECN validated, timestamp) in the server's session ticket,
  encrypted with the server's key. It survives server restarts and load balancing without shared storage.
  PathState covers the current session; the ticket covers the next one.
- Stay inside RFC 9959's envelope, and cite it for κ_w: send the IW first, abort if the min RTT ≤ saved_rtt / 2
  (§3.2), jump to at most saved_cwnd / 2 and pace the jump (§3.3), retreat on loss or CE. Plan §3.6.5's
  κ_w = 0.5 matches the RFC's cap.
- Prefer the ticket to NEW_TOKEN for CC state, for the reason above.
- Remember the ECN result (E6), but ECN must still be validated on each new path
  ([RFC 9000 §13.4.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.4.2)).

---

## B. Feedback signals

### B1. Packet numbers are never reused

**TCP**: retransmissions reuse byte sequence numbers
([RFC 9293 §3.4](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.4)), so RTT samples MUST NOT be taken
from retransmitted segments (Karn; [RFC 6298 §3](https://www.rfc-editor.org/rfc/rfc6298.html#section-3)) unless
timestamps disambiguate them ([RFC 7323 §4](https://www.rfc-editor.org/rfc/rfc7323.html#section-4)).

**QUIC**: packet numbers increase monotonically, are never reused, and may have gaps
([RFC 9000 §12.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-12.3),
[RFC 9002 §3](https://www.rfc-editor.org/rfc/rfc9002.html#section-3),
[§4.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.2)). An ACK frame yields at most one RTT sample:
from its largest acknowledged packet, if that packet is newly acknowledged and at least one newly acknowledged
packet was ack-eliciting ([RFC 9002 §5.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-5.1)).

**Use it**
- Sprint v0 attributes ACKs and CE deltas to H1 or H2 by packet number range. In QUIC this is exact, because
  every packet number is sent once.
- Delivery-rate samples (min(ack_rate, send_rate), README §BBR) stay valid during loss recovery, because
  retransmitted data rides in new packets with their own send times.
- Per-packet timing inside a probe train needs your own send-time table keyed by packet number (fork item 1:
  pass the packet number to `on_ack`). The reported ACK delay applies only to the largest acknowledged packet.
- Many samples per RTT shorten smoothed_rtt's memory; RFC 9002 §5.1 calls this an open research question. Run
  Sprint's own filters (for example a per-round minimum) instead of reusing smoothed_rtt.

### B2. Explicit ACK delay and the max_ack_delay contract

**TCP**
- A receiver may delay ACKs by up to 500 ms
  ([RFC 5681 §4.2](https://www.rfc-editor.org/rfc/rfc5681.html#section-4.2),
  [RFC 9293 §3.8.6.3](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.8.6.3)).
- With timestamps, the receiver deliberately echoes the earliest unacknowledged segment so that delayed-ACK time
  is *included* in the RTT ([RFC 7323 §4.3](https://www.rfc-editor.org/rfc/rfc7323.html#section-4.3)).

**QUIC**
- Every ACK reports the receiver's own delay
  ([RFC 9000 §13.2.5](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.2.5); field in
  [§19.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3)), and max_ack_delay (default 25 ms) is
  "an explicit contract" ([RFC 9000 §13.2.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.2.1),
  [§18.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-18.2)).
- RFC 9002 subtracts the delay from RTT samples, with guard rails
  ([§4.6](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.6),
  [§5.3](https://www.rfc-editor.org/rfc/rfc9002.html#section-5.3)). min_rtt is never adjusted
  ([§5.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-5.2)); it starts from handshake ACKs, which are
  sent immediately, so it begins close to the propagation RTT.

**Use it**
- Queueing delay ≈ adjusted_rtt − min_rtt is cleaner than in TCP. That gives Sprint's no-ECN mode (RQ3) a
  sharper HyStart++-style delay backstop.
- The client sets max_ack_delay. On the express lane, advertise a few milliseconds: the server's PTO shrinks by
  the difference to the 25 ms default (C1), and ACK-delay noise drops. The cost is more ACKs from the client.

### B3. Many irrevocable ACK ranges, exact bytes in flight

**TCP**: a SACK option holds 4 blocks, or 3 with timestamps
([RFC 2018 §3](https://www.rfc-editor.org/rfc/rfc2018.html#section-3)). SACKed data may be reneged
([RFC 2018 §8](https://www.rfc-editor.org/rfc/rfc2018.html#section-8)), so the sender keeps it and estimates the
data in flight heuristically ("pipe", [RFC 6675 §4](https://www.rfc-editor.org/rfc/rfc6675.html#section-4)).

**QUIC**: an ACK frame carries as many ranges as fit in a packet
([RFC 9000 §19.3.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3.1),
[§13.2.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.2.3),
[RFC 9002 §4.5](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.5)). Acknowledgements are irrevocable
([RFC 9000 §19.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3),
[RFC 9002 §4.4](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.4)), and "in flight" is defined per packet
([RFC 9002 §2](https://www.rfc-editor.org/rfc/rfc9002.html#section-2); bytes_in_flight in
[Appendix B.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-b.2)).

**Use it**
- The Nimble/BBR congestion-loss test (loss > 2 % and ≥ 6 discontiguous lost ranges) is exact in QUIC: count
  the ranges of packet numbers declared lost in the round. In TCP, one ACK can show at most 3–4 SACK blocks.
- Exact bytes_in_flight gives an exact PipeSize and flight_size for Careful-Resume-style validation
  ([RFC 9959 §3.3](https://www.rfc-editor.org/rfc/rfc9959.html#section-3.3)) and for Sprint's inflight cap
  (cwnd_gain × R × min_rtt).

### B4. Spurious loss is trivially detectable

**TCP**: needs extra machinery: F-RTO ([RFC 5682](https://www.rfc-editor.org/rfc/rfc5682.html)), Eifel
([RFC 3522](https://www.rfc-editor.org/rfc/rfc3522.html)), DSACK
([RFC 2883](https://www.rfc-editor.org/rfc/rfc2883.html)). CUBIC specifies an undo
([RFC 9438 §4.9](https://www.rfc-editor.org/rfc/rfc9438.html#section-4.9)).

**QUIC**
- An ACK for a packet already declared lost is unambiguous
  ([RFC 9002 §4.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.2)). A PTO never declares loss
  ([RFC 9002 §6.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2)), so "in the case of QUIC, there are
  no spurious timeouts" ([RFC 9438 §4.9](https://www.rfc-editor.org/rfc/rfc9438.html#section-4.9)).
- RFC 9002 suggests raising the reordering threshold after spurious losses, and lets adaptive implementations
  "start with smaller initial reordering thresholds to minimize recovery latency"
  ([§6.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.1)).

**Use it**
- Undo every loss-triggered Sprint cut (R × 0.7): save the state before the cut, and restore it when all losses
  of that epoch turn out to be spurious. RFC 9438 §4.9.2 is the template, and quinn exposes
  `on_spurious_congestion_event`. CE-triggered cuts cannot be spurious.
- With undo in place, express flows can start with a smaller reordering threshold (C4) and adapt upward.
- Keep the spurious-loss count as a metric (plan §5.7); it shows whether faster detection paid off.

### B5. ECN feedback

**TCP**
- Classic ECN: one ECE flag, latched until CWR
  ([RFC 3168 §6.1.3](https://www.rfc-editor.org/rfc/rfc3168.html#section-6.1.3)); react at most once per window
  ([§6.1.2](https://www.rfc-editor.org/rfc/rfc3168.html#section-6.1.2)).
- ECT is forbidden on SYN and SYN-ACK ([§6.1.1](https://www.rfc-editor.org/rfc/rfc3168.html#section-6.1.1)),
  pure ACKs ([§6.1.4](https://www.rfc-editor.org/rfc/rfc3168.html#section-6.1.4)) and retransmissions
  ([§6.1.5](https://www.rfc-editor.org/rfc/rfc3168.html#section-6.1.5)).
  [RFC 8311 §4.3](https://www.rfc-editor.org/rfc/rfc8311.html#section-4.3) relaxes this for experiments only.
- Counts now exist for TCP too: AccECN ([RFC 9768](https://www.rfc-editor.org/rfc/rfc9768.html), Standards
  Track, 2026). Part of it lives in a TCP option, so it needs fallbacks for option stripping
  ([RFC 9768 §3.2.3.2](https://www.rfc-editor.org/rfc/rfc9768.html#section-3.2.3.2)).

**QUIC**
- Cumulative ECT(0), ECT(1) and CE counts per packet number space in every ACK frame of type 0x03
  ([RFC 9000 §13.4.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.4.1),
  [§19.3.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3.2)), with per-path validation
  ([§13.4.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.4.2); sample algorithm in
  [Appendix A.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-a.4)).
- CE-marked packets SHOULD be acknowledged immediately
  ([RFC 9000 §13.2.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.2.1)).
- RFC 9000 does not restrict which packets carry ECT. Handshake packets, retransmitted data and ACK-only packets
  can all be ECN-capable.
- Response: RFC 9002 treats CE like loss ([§7.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.1)) and
  allows other responses under RFC 8311 and RFC 8511
  ([§8.3](https://www.rfc-editor.org/rfc/rfc9002.html#section-8.3)). §8.3 also suggests that the sender mark the
  occasional packet CE itself, to catch a receiver that hides marks.

**Use it** (README §ECN in QUIC covers the counts and validation)
- The immediate CE ACK brings E1/E2 feedback up to one max_ack_delay sooner than a delayed ACK would. If you
  thin ACKs with the extension, its [§6.3](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency-14#section-6.3)
  still asks for an immediate ACK on every non-CE → CE transition.
- Retransmissions are ECN-capable in QUIC. Under an ECN AQM, a short flow's retransmissions are therefore marked
  instead of dropped, which should mean fewer cascaded losses and PTOs at the tail. Worth one W7 measurement.
- Use Application Data-space counts only (A4).
- Until validation has seen ACKs for ECT packets, treat CE only as a conservative exit signal, not as proof that
  the path marks; E3 (loss differentiation) needs the latter.
- E3 trusts the *absence* of CE. A receiver that suppresses CE would make Sprint ignore real congestion;
  §8.3's CE self-test catches that. Put it in the safety section (plan §3.10).
- Thesis wording: exact counts are no longer QUIC-only on paper (RFC 9768). QUIC's advantage is that the counts
  are mandatory in v1, cannot be stripped, and are validated per path.

### B6. The sender can steer the ACK rate

**TCP**
- The receiver alone decides: an ACK for at least every second full-sized segment, within 500 ms
  ([RFC 5681 §4.2](https://www.rfc-editor.org/rfc/rfc5681.html#section-4.2),
  [RFC 9293 §3.8.6.3](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.8.6.3)). The sender cannot ask for
  an immediate ACK.
- Middleboxes filter or decimate ACKs ([RFC 3449 §5.2.1](https://www.rfc-editor.org/rfc/rfc3449.html#section-5.2.1),
  [§5.2.2](https://www.rfc-editor.org/rfc/rfc3449.html#section-5.2.2)).

**QUIC, base protocol**
- The receiver advertises max_ack_delay ([RFC 9000 §18.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-18.2)).
  It acknowledges Initial and Handshake packets, reordering, gaps and CE immediately
  ([§13.2.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.2.1)), and otherwise every second packet.
  [§13.2.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.2.2) explicitly invites other strategies
  based on "knowledge of the peer's congestion controller".
- A skipped packet number elicits an immediate ACK
  ([RFC 9002 §6.2.4](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.4), via the gap rule in
  RFC 9000 §13.2.1).

**QUIC, ACK-frequency extension**
([draft-ietf-quic-ack-frequency-14](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency-14);
**expired Aug 2026**, still implemented in quinn)
- ACK_FREQUENCY sets the Ack-Eliciting Threshold, Requested Max Ack Delay and Reordering Threshold
  ([§4](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency-14#section-4)); IMMEDIATE_ACK asks
  for an ACK now ([§5](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency-14#section-5)).
- "a sender can send an IMMEDIATE_ACK frame with the last data packet before an idle period"
  ([§8.1.1](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency-14#section-8.1.1)). With
  IMMEDIATE_ACK, or with more packets in flight than the threshold, max_ack_delay may be left out of the PTO
  ([§7](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency-14#section-7)).

**Use it**
- Phase-dependent ACK policy: threshold 0 (ACK every packet) during IW_PROBE and RAMP for dense samples and a
  faster-converging rttvar (C1); a large threshold in CRUISE and on the bulk lane, to save CPU.
- Mark the last packet of every response with IMMEDIATE_ACK, or skip a packet number just before it. The server
  then learns about completion, or a tail loss, up to max_ack_delay earlier.
- ACK_FREQUENCY can also emulate ACK thinning for W7 (plan §5.5).
- The draft has expired: cite it as work in progress, and keep a path that works without it (skipped packet
  number, small client max_ack_delay).

### B7. One-way delay: TCP is ahead here

**TCP**: the Timestamps option carries the sender's clock on every segment and echoes the peer's
([RFC 7323 §3](https://www.rfc-editor.org/rfc/rfc7323.html#section-3)). The one-way delay *trend* is measurable
because the clock offset cancels out; TCP-LP and LEDBAT-style controllers rely on this.

**QUIC v1**: no timestamps, only ack_delay. The WG draft
[draft-ietf-quic-receive-ts-03](https://datatracker.ietf.org/doc/html/draft-ietf-quic-receive-ts-03) adds
per-packet receive timestamps; its motivation
([§2](https://datatracker.ietf.org/doc/html/draft-ietf-quic-receive-ts-03#section-2)) cites pathChirp-style
dispersion.

**Use it**: Sprint fills this gap with `PROBE.send_time` and `RATE_REPORT` (plan §3.7). Compare RATE_REPORT
with receive-ts (same information, but the computation sits at the receiver instead of the sender), and
consider reusing receive-ts's frame layout to stay close to a standards track. Your notes cite -00; the current
version is -03.

### B8. Loss of ACK-only packets is detectable

**TCP**: pure ACKs are not tracked, and they must be Not-ECT
([RFC 3168 §6.1.4](https://www.rfc-editor.org/rfc/rfc3168.html#section-6.1.4)).

**QUIC**: "Unlike TCP, QUIC can detect the loss of these packets and MAY use that information to adjust the
congestion controller or the rate of ACK-only packets being sent"
([RFC 9002 §7](https://www.rfc-editor.org/rfc/rfc9002.html#section-7)).

**Use it**: low priority. The client could thin its own ACKs when its uplink is congested (cellular, or the
reverse-path bottleneck in W7).

---

## C. Loss recovery and timers (the tail of a short flow)

### C1. PTO replaces RTO and TLP

**TCP**
- RTO = SRTT + max(G, 4·RTTVAR), and it SHOULD be rounded up to 1 s
  ([RFC 6298 §2](https://www.rfc-editor.org/rfc/rfc6298.html#section-2) rule 2.4); it doubles on each expiry
  ([§5](https://www.rfc-editor.org/rfc/rfc6298.html#section-5) rule 5.5). After an RTO, cwnd is 1 segment
  ([RFC 5681 §3.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-3.1)).
- A Tail Loss Probe fires after 2·SRTT ([RFC 8985 §7.2](https://www.rfc-editor.org/rfc/rfc8985.html#section-7.2)).
  In RFC 8985's own example, 10 lost segments cost RTO + 4 RTT without TLP and 6 RTT with it
  ([§9.3](https://www.rfc-editor.org/rfc/rfc8985.html#section-9.3)).

**QUIC**
- PTO = smoothed_rtt + max(4·rttvar, kGranularity) + max_ack_delay, with no floor beyond kGranularity (1 ms)
  ([RFC 9002 §6.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.1)). max_ack_delay counts as 0 in
  the Initial and Handshake spaces.
- A PTO is not a loss and does not reduce cwnd
  ([RFC 9002 §4.7](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.7),
  [§6.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2),
  [§7.5](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.5)). The sender sends 1–2 probes that SHOULD
  carry new data, or data chosen by application priority
  ([§6.2.4](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.4)), and the probes may exceed cwnd (§7.5).

**Use it**
- A fresh connection's PTO is large. The first RTT sample sets rttvar = latest_rtt / 2
  ([RFC 9002 §5.3](https://www.rfc-editor.org/rfc/rfc9002.html#section-5.3)), so right after it the PTO is about
  3·RTT + max_ack_delay, and it shrinks only as further samples arrive (rttvar decays by 3/4 per sample). Tail
  losses in the first one or two rounds are therefore the expensive ones: responses that fit in a few tens of
  kilobytes, which is where most web objects are (Reading_List §2.2). Two levers: dense ACKs (B6) make rttvar
  converge within a round, and a tail probe of your own (C4, D5) does not wait for the PTO.
- Shrink the rest of the PTO: a small client max_ack_delay (B2), or IMMEDIATE_ACK in probes so that
  max_ack_delay can be dropped from the PTO (ack-frequency §7).
- Send two probes when the remaining data fits in two packets (§6.2.4 allows it), so one lost probe does not
  double the wait.
- In plan B, probes carry unacknowledged express data before bulk data (§6.2.4 allows application priorities).
- Sprint must not cut R on a PTO; only persistent congestion resets it (C3).

### C2. Loss epoch and recovery period

**TCP**: NewReno recovery lasts until the "recover" point is acknowledged
([RFC 6582 §3.2](https://www.rfc-editor.org/rfc/rfc6582.html#section-3.2)). A lost retransmission counts as a
second congestion event ([RFC 5681 §4.3](https://www.rfc-editor.org/rfc/rfc5681.html#section-4.3)).

**QUIC**: recovery ends when any packet sent after it started is acknowledged
([RFC 9002 §4.3](https://www.rfc-editor.org/rfc/rfc9002.html#section-4.3),
[§7.3.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.3.2)), so there is at most one reduction per lossy
round trip. PRR is allowed (§7.3.2; PRR is now [RFC 9937](https://www.rfc-editor.org/rfc/rfc9937.html)), and one
packet may be sent before the reduction.

**Use it**: define a Sprint round by packet numbers: round k ends when the first packet sent in round k is
acknowledged. That is the epoch QUIC already uses for recovery, so the loss rate, lost ranges (B3) and CE
fraction in `on_loss_round` cover exactly the packets of that round.

### C3. Persistent congestion instead of RTO collapse

**TCP**: every RTO sets cwnd to 1 segment and restarts slow start
([RFC 5681 §3.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-3.1)).

**QUIC**: cwnd drops to 2 packets only when two ack-eliciting packets are lost more than
3 × (smoothed_rtt + max(4·rttvar, kGranularity) + max_ack_delay) apart, nothing sent between them was
acknowledged, and an RTT sample existed when they were sent
([RFC 9002 §7.6.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.6.1),
[§7.6.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.6.2)). min_rtt SHOULD then be reset
([§5.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-5.2)).

**Use it**: this is the "severe congestion" clause of Sprint's safety argument (plan §3.10, RFC 9743). Sprint
inherits it unchanged and resets to IW_PROBE. Right after the first RTT sample the duration is about
9·RTT + 3·max_ack_delay, so within a short flow it practically never fires. Do not present it as protection
during startup; Sprint's own exits are that protection.

### C4. Loss-detection thresholds

**TCP**: 3 duplicate ACKs ([RFC 5681 §3.2](https://www.rfc-editor.org/rfc/rfc5681.html#section-3.2)), Limited
Transmit ([RFC 3042](https://www.rfc-editor.org/rfc/rfc3042.html)), and RACK's reordering window of min_RTT/4,
adapted after spurious losses ([RFC 8985 §6.2](https://www.rfc-editor.org/rfc/rfc8985.html#section-6.2)).

**QUIC**: kPacketThreshold = 3 (SHOULD NOT be lower), kTimeThreshold = 9/8, kGranularity = 1 ms
([RFC 9002 §6.1.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.1.1),
[§6.1.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.1.2)). Thresholds MAY be adaptive, taken "from
previous connections", or start smaller ([§6.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.1), §6.1.2).
Reordering may be more common for QUIC, because network elements that put TCP packets back in order cannot do
so for QUIC (§6.1.1).

**Use it**
- At the tail of a short flow, fewer than 3 packets follow a lost one, so the packet threshold never fires.
  Detection then waits for 9/8·RTT (if a later packet gets acknowledged) or for the PTO. Two cheap fixes:
  (a) send a small ack-eliciting packet (PING, with IMMEDIATE_ACK if available) right after the last data packet,
  so the time threshold fires about 9/8·RTT after the loss; (b) duplicate the last 1–2 packets (D5). Measure
  both in W7 with random loss.
- Seed the time threshold from PathState (§6.1.2 allows thresholds from previous connections).

---

## D. Sending behaviour

### D1. Pacing

**TCP**: RFC 5681 does not require pacing. Bursts after idle are a known problem
([RFC 5681 §4.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-4.1)), and sender pacing appears only as a
mitigation ([RFC 3449 §4.6](https://www.rfc-editor.org/rfc/rfc3449.html#section-4.6)). In practice it is an
implementation choice (for example Linux `fq`).

**QUIC**: a sender SHOULD pace all in-flight packets, MUST either pace or limit bursts, and SHOULD limit bursts
to the IW unless it knows the path can absorb more. ACK-only packets SHOULD NOT be paced, and the pacer may wrap
the congestion controller ([RFC 9002 §7.7](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.7)). See also
[RFC 8085 §3.1.6](https://www.rfc-editor.org/rfc/rfc8085.html#section-3.1.6); RFC 9959 makes pacing a MUST in
the unvalidated phase ([§3.3](https://www.rfc-editor.org/rfc/rfc9959.html#section-3.3)).

**Use it**
- A rate-based Sprint is the "pacer wraps the congestion controller" design that §7.7 describes. Cite it to
  justify the fork's `pacing_rate()` hook.
- The H2 probe trains at g_hi·R are paced, so the burst limit does not constrain them. The IW burst cap is a
  principled default for `max_burst_packets()`; quinn's bursts of ≥ 10 packets (plan §4.7) are what break the
  trains.
- "A sender with knowledge that the network path to the receiver can absorb larger bursts MAY use a higher
  limit" (§7.7). Cite this for any back-to-back packet pairs used for dispersion measurements.

### D2. No restart-after-idle rule

**TCP**: after being idle for longer than the RTO, a TCP SHOULD reset cwnd to RW = min(IW, cwnd)
([RFC 5681 §4.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-4.1)). New CWV instead keeps cwnd for a
non-validated period ([RFC 7661 §4](https://www.rfc-editor.org/rfc/rfc7661.html#section-4), Experimental).

**QUIC**: RFC 9002 has no restart rule. It relies on pacing and the burst limit
([§7.7](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.7)), forbids cwnd growth while the window is
under-utilized, and allows alternatives "such as those proposed for TCP in [RFC7661]"
([§7.8](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.8)). Stale feedback after idle is addressed in
ack-frequency [§6.1](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency-14#section-6.1).

**Use it**
- Requests separated by think time (W3 uses exp(1 s); W4 is open-loop) reuse a warm cwnd in QUIC, where a
  spec-compliant TCP would restart at the IW. This is a free warm start for plan B and for plan A's express lane,
  **and a confound**. Check what quinn does after idle, log cwnd at every request start, and add a
  "slow start after idle" ablation arm. Otherwise retained cwnd can be mistaken for a Sprint gain.
- Design question for Sprint: how much to trust R after idle. A Careful-Resume-style answer: re-enter at
  κ_w × R, paced, and validate ([RFC 9959 §3.3](https://www.rfc-editor.org/rfc/rfc9959.html#section-3.3)), with
  trust decaying with idle time (RFC 7661's non-validated period is the TCP precedent).

### D3. An under-utilized cwnd must not grow

**TCP**: RFC 5681 has no application-limited rule beyond the idle restart;
[RFC 7661 §4](https://www.rfc-editor.org/rfc/rfc7661.html#section-4) (Experimental) covers rate-limited senders.

**QUIC**: cwnd SHOULD NOT grow when bytes in flight are below cwnd and sending is not pacing-limited. Pacing
delay does not count as being app-limited ([RFC 9002 §7.8](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.8)).

**Use it**: the last round of a short transfer never fills the window. Sprint's plateau and rate logic must skip
app-limited rounds, as BBR does (README §BBR). Amplification-limited time (A2) and flow-control-limited time (D4)
belong in the same bucket, and in user space the CC can tell the three causes apart.

### D4. Flow control

**TCP**: the receive window rides on every ACK
([RFC 9293 §3.8.6](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.8.6)). A receiver SHOULD NOT shrink it
(SHLD-14, same section), yet rLEDBAT ([RFC 9840](https://www.rfc-editor.org/rfc/rfc9840.html)) and ROS both steer
the sender through it.

**QUIC**
- Credit is an absolute byte offset per stream and per connection. Transport parameters set the initial values;
  MAX_DATA and MAX_STREAM_DATA raise them at any time
  ([RFC 9000 §4.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.1),
  [§18.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-18.2),
  [§19.9](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.9)). A smaller limit has no effect (§4.1).
- Credit below the BDP limits throughput ([§4.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.3)). A
  sender released from blocking can burst, so it should pace
  ([§4.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.2)).
- Senders signal blocking with DATA_BLOCKED and STREAM_DATA_BLOCKED (§4.1,
  [§19.12](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.12)).
- Careful Resume notes that QUIC receivers may need to send MAX_DATA proactively
  ([RFC 9959 §4.7](https://www.rfc-editor.org/rfc/rfc9959.html#section-4.7)).

**Use it**
- Nimble Start's "send beyond rwnd to force autotuning" is unnecessary in QUIC (plan §3.6.1); §4.1 and §18.2
  are the reason to cite.
- The MAX_DATA-only variant of ROS (plan §3.7): credit can only grow, so a receiver can cap R only by holding
  back growth, and cannot take back credit it has over-granted. That is a concrete argument for the explicit
  RATE_REPORT.
- Client-side coupling through delayed MAX_STREAM_DATA (Uses_Cases A3) has the same one-way limitation.

### D5. QUIC re-sends frames, not bytes

**TCP**: retransmits sequence ranges. Duplicate data shows up as DSACK
([RFC 2883](https://www.rfc-editor.org/rfc/rfc2883.html)) and complicates loss and undo inference.

**QUIC**
- Lost packets are not retransmitted whole; their information is re-sent in new frames in new packets, and it is
  "not forbidden to retransmit copies of frames"
  ([RFC 9000 §13.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.3)). Receivers discard duplicate
  stream data ([RFC 9000 §2.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-2.2)).
- Retransmissions SHOULD go before new data "unless priorities specified by the application indicate otherwise"
  (§13.3).

**Use it**
- Proactive tail redundancy (the Halfback / RC3 idea, Reading_List §1.1): duplicate the STREAM frames of the last
  1–2 packets of each express response when cwnd allows. Each copy has its own packet number and ACK, so nothing
  is ambiguous. The copies count as bytes in flight.
- Plan B: retransmit express data before new bulk data, as the application-priority clause allows.

---

## E. Multiplexing, application knowledge, paths

### E1. Streams without cross-stream head-of-line blocking

**TCP**: one "reliable, in-order, byte-stream service"
([RFC 9293 §2.2](https://www.rfc-editor.org/rfc/rfc9293.html#section-2.2)); a loss stalls every object
multiplexed over it.

**QUIC**: independent streams, ordered only within each stream
([RFC 9000 §2](https://www.rfc-editor.org/rfc/rfc9000.html#section-2),
[§2.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-2.2)). Priorities come from the application, and
implementations SHOULD let it express them ([RFC 9000 §2.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-2.3);
HTTP/3 uses [RFC 9218](https://www.rfc-editor.org/rfc/rfc9218.html)).

**Use it**
- Plan B: a lost bulk packet does not delay express streams. Keep express and bulk frames in separate packets, so
  every loss and every CE mark maps to one class; per-class accounting needs that.
- SRPT across streams (Reading_List §1.3) needs no wire change.

### E2. The CC lives next to the application

**TCP**: the kernel's CC sees a byte stream (RFC 9293 §2.2), with no message sizes and only a coarse view of the
application's send state.

**QUIC**: the stack runs inside the application process. A stream's end is explicit (FIN and final size,
[RFC 9000 §4.5](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.5); stream operations in
[§2.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-2.4)), and flow-control blocking is signalled
([§4.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.1)).

**Use it**: S_rem ("never probe beyond what is left", plan §3.6.1) follows from the stream's send buffer and FIN,
with no kernel API. The same holds for plan B's per-stream classes and the size-hint API (plan §4.2 item 5). In
the thesis, present this as an architectural difference (user space), not a wire-format one.

### E3. Connection IDs and migration

**TCP**: "A connection is defined by a pair of sockets"
([RFC 9293 §3.4.1](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.4.1)); an address change breaks it.

**QUIC**: connections are identified by connection IDs
([RFC 9000 §5.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-5.1)). After a confirmed migration, the CC
and RTT estimator MUST be reset, unless only the port changed, in which case the state MAY be kept
([RFC 9000 §9.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-9.4)). The PATH_CHALLENGE / PATH_RESPONSE
delay MAY seed the new path's initial RTT
([RFC 9002 §6.2.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.2)), and ECN is re-validated
([RFC 9000 §13.4.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.4.2)).

**Use it**: on a mobile express lane (Uses_Cases A1/A2), Sprint cold-starts after a migration but can seed its RTT
from path validation; NAT rebinding keeps the state. In the testbed, fix client addresses (plan §4.7).

### E4. Multipath QUIC

**TCP**: MPTCP ([RFC 8684](https://www.rfc-editor.org/rfc/rfc8684.html)) with coupled congestion control
([RFC 6356](https://www.rfc-editor.org/rfc/rfc6356.html)).

**QUIC**: [draft-ietf-quic-multipath-21](https://datatracker.ietf.org/doc/html/draft-ietf-quic-multipath-21)
(in the RFC Editor queue) keeps separate CC state per path
([§5.3](https://datatracker.ietf.org/doc/html/draft-ietf-quic-multipath-21#section-5.3)) and advises coupled CC
(LIA, OLIA) where paths may share a bottleneck. It also allows several paths on the **same 4-tuple** with
different path IDs, "managed independently", for example with different DiffServ markings and streams matched to
paths ([§5.2](https://datatracker.ietf.org/doc/html/draft-ietf-quic-multipath-21#section-5.2)).

**Use it**: a third way to build the dual lane. One connection and one handshake (which also satisfies
RFC 9114 §3.3), two path IDs with independent controllers (Sprint and CUBIC), each with its own packet number
space and its own ECN codepoint or DSCP, coupled RFC 6356-style as in plan §3.8. The costs: stack support (check
quinn), and paths on one 4-tuple share an FQ queue unless the client uses a second port. Plan A lists multipath
as out of scope; at least discuss it as future work.

---

## F. Extensibility and the network

### F1. Transport parameters and extension frames vs 40 bytes of TCP options

**TCP**: options are limited to 40 bytes by the 4-bit Data Offset
([RFC 9293 §3.1](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.1)); SACK's block limit
([RFC 2018 §3](https://www.rfc-editor.org/rfc/rfc2018.html#section-3)) follows from it. Middleboxes strip or alter
options, so both TFO and AccECN specify fallbacks
([RFC 7413 §7.1](https://www.rfc-editor.org/rfc/rfc7413.html#section-7.1),
[RFC 9768 §3.2.3.2](https://www.rfc-editor.org/rfc/rfc9768.html#section-3.2.3.2)).

**QUIC**
- Transport parameters ([RFC 9000 §7.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-7.4)); unknown ones
  MUST be ignored ([§7.4.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-7.4.2)), and a new one must say
  whether it is remembered for 0-RTT ([§7.4.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-7.4.1)).
- Extension frames MUST be congestion controlled and MUST cause an ACK to be sent; "the exception is extension
  frames that replace or supplement the ACK frame"
  ([RFC 9000 §19.21](https://www.rfc-editor.org/rfc/rfc9000.html#section-19.21)).
- All of it is encrypted and integrity-protected
  ([RFC 9001 §5](https://www.rfc-editor.org/rfc/rfc9001.html#section-5),
  [RFC 9312 §2.5](https://www.rfc-editor.org/rfc/rfc9312.html#section-2.5)).

**Use it**
- `sprint_feedback`: the client's transport parameters arrive in its ClientHello, so the server knows whether the
  client supports Sprint before its first data packet. Negotiation costs no RTT.
- Decide whether RATE_REPORT supplements the ACK frame (as receive-ts's ACK variant does). If it does, §19.21
  exempts it from congestion control and from eliciting ACKs; if not, it is congestion controlled and
  ack-eliciting. Plan §3.7 currently says "ack-eliciting but not retransmitted"; settle it explicitly.
- State the 0-RTT remembering rule in the parameter's definition (§7.4.1).

### F2. ACKs are encrypted: no ACK thinning, no splitting PEPs

**TCP**: middleboxes can identify ACKs and filter or decimate them
([RFC 3449 §5.2.1](https://www.rfc-editor.org/rfc/rfc3449.html#section-5.2.1),
[§5.2.2](https://www.rfc-editor.org/rfc/rfc3449.html#section-5.2.2)), and performance-enhancing proxies split
connections ([RFC 3135](https://www.rfc-editor.org/rfc/rfc3135.html)).

**QUIC**: ACK information sits inside the encrypted payload, and "ACK manipulation is impossible"
([RFC 9312 §3.3](https://www.rfc-editor.org/rfc/rfc9312.html#section-3.3)). Header bits are integrity-protected
([RFC 9312 §2.5](https://www.rfc-editor.org/rfc/rfc9312.html#section-2.5)).

**Use it**: ACK-clock estimates (Sprint v0) cannot be distorted by ACK filtering, only by link-layer aggregation,
which is why receiver-side measurement (v1) still helps. On satellite and in-flight Wi-Fi, PEPs used to hide slow
start; with QUIC the endpoint's CC has to do it (Uses_Cases, "Where both help most").

### F3. Intentional packet number gaps

**TCP**: the sequence space is contiguous, and no spec mechanism defends against optimistic ACKs.

**QUIC**: senders may leave packet numbers unused
([RFC 9002 §3](https://www.rfc-editor.org/rfc/rfc9002.html#section-3)), to detect optimistic ACKs
([RFC 9000 §21.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-21.4)) or to elicit an immediate ACK
([RFC 9002 §6.2.4](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.4)).

**Use it**: an aggressive startup is the CC most exposed to an optimistic-ACK receiver, which would inflate R
quickly. Skip random packet numbers during RAMP and close the connection on an ACK for a skipped one (plan §3.10).

**Watch out**: the packet-threshold check in
[RFC 9002 Appendix A.10](https://www.rfc-editor.org/rfc/rfc9002.html#section-a.10) "assumes that there were no
sender-induced gaps in the packet number space". A skipped number inside the window makes it fire one packet
early; count sent packets instead, or skip sparingly.

### F4. Datagram size

**TCP**: MSS option ([RFC 9293 §3.7.1](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.7.1)); cwnd shrinks
proportionally if the MSS has to shrink ([RFC 5681 §3.1](https://www.rfc-editor.org/rfc/rfc5681.html#section-3.1)).

**QUIC**: a path must carry 1200-byte UDP payloads
([RFC 9000 §14](https://www.rfc-editor.org/rfc/rfc9000.html#section-14)); Initials are padded
([§14.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-14.1)); larger sizes come from DPLPMTUD
([§14.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-14.3)). Loss of a PMTU probe SHOULD NOT trigger a
congestion reaction, but probes consume cwnd ([§14.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-14.4)).
The IW is recomputed when the datagram size changes
([RFC 9002 §7.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.2)).

**Use it**: pin the MTU in experiments (plan §4.7), and do not probe the PMTU during express transfers (probes eat
cwnd and do nothing for FCT). The IW is 10 packets either way, but that is 12,000 B at 1200 and 14,520 B at 1452.

---

## G. Where TCP is level or ahead (don't overclaim)

- **One-way delay.** TCP timestamps provide it; QUIC v1 does not (B7).
- **Early tail probes.** TLP fires at 2·SRTT ([RFC 8985 §7.2](https://www.rfc-editor.org/rfc/rfc8985.html#section-7.2)),
  while QUIC's first PTOs are about 3·RTT + max_ack_delay because of how rttvar is initialised (C1). On the tail of
  a very short flow, a TCP with RACK-TLP can recover before a stock QUIC stack does.
- **ECN counts.** TCP has them too now (AccECN, RFC 9768).
- **State caching.** Specified for TCP (RFC 9040), host-local.
- **Same algorithm family.** RACK-TLP corresponds to QUIC's time threshold plus PTO; IW10 in both; HyStart++
  explicitly applies to QUIC ([RFC 9406 §1](https://www.rfc-editor.org/rfc/rfc9406.html#section-1)); CUBIC covers
  both ([RFC 9438](https://www.rfc-editor.org/rfc/rfc9438.html)).
- **CPU and pacing quality.** Kernel TCP has mature pacing and offloads; QUIC pacing quality varies by stack (Kempf
  et al., CoNEXT 2025, Reading_List §1.5).
- **Choice of CC.** Both let the sender choose unilaterally; RFC 9002 §7 only requires conformance with RFC 8085
  §3.1. The difference is deployment (user space vs kernel), not the spec.

## H. Ideas ranked for the thesis

1. **Tail package for express responses** (C1, C4, B6, D5): IMMEDIATE_ACK or a skipped packet number on the last
   packet, two probes, a small client max_ack_delay, optionally a duplicate of the last packet. Cheap,
   FCT-relevant and independent of Sprint. Report it as its own ablation so it is not mixed into Sprint's startup
   gain.
2. **Dense ACKs during startup** (B6, C1): threshold 0 in IW_PROBE and RAMP, a large threshold in CRUISE and on the
   bulk lane. Better samples and faster rttvar convergence.
3. **The idle confound** (D2): log cwnd at every request start, and add a slow-start-after-idle arm to W3/W4.
4. **Amplification-aware train #0** (A2): mark amplification-limited samples as app-limited; make the client's
   Initial size a W8 factor.
5. **Rounds = recovery epochs, with exact lost ranges** (C2, B3).
6. **Undo, plus a smaller initial reordering threshold** (B4, C4).
7. **ECN details** (B5): Application Data-space counts only; immediate CE ACK kept under ACK thinning; ECT on
   retransmissions as a measurable short-flow effect; the CE self-test in the safety section.
8. **Warm start carried in the session ticket**, inside RFC 9959's envelope (A5).
9. **RATE_REPORT as an ACK supplement** (F1).
10. **Multipath as an alternative dual lane** (E4): discussion only.

## I. Spec status (checked 28 Oct 2026)

Only documents whose status matters for how you cite them.

| Document | Status | Note |
|----------|--------|------|
| [RFC 9743](https://www.rfc-editor.org/rfc/rfc9743.html) Specifying New CC Algorithms | BCP 133 (Mar 2025) | evaluation checklist for Sprint |
| [RFC 9768](https://www.rfc-editor.org/rfc/rfc9768.html) AccECN | Standards Track (2026) | TCP now has CE counts too |
| [RFC 9937](https://www.rfc-editor.org/rfc/rfc9937.html) PRR | Standards Track (Dec 2025) | obsoletes RFC 6937; RFC 9002 §7.3.2 permits PRR |
| [RFC 9959](https://www.rfc-editor.org/rfc/rfc9959.html) Careful Resume | Standards Track (May 2026) | warm-start envelope |
| RFC 6928 (IW10), RFC 7413 (TFO), RFC 7661 (New CWV) | Experimental | QUIC's IW10 is Standards Track |
| RFC 9040 (TCB interdependence) | Informational | TCP-side state sharing |
| [draft-ietf-quic-ack-frequency-14](https://datatracker.ietf.org/doc/draft-ietf-quic-ack-frequency/) | expired 9 Aug 2026, not an RFC | quinn implements it; cite as work in progress |
| [draft-ietf-quic-multipath-21](https://datatracker.ietf.org/doc/draft-ietf-quic-multipath/) | in the RFC Editor queue | will get an RFC number; re-check before submission |
| [draft-ietf-quic-receive-ts-03](https://datatracker.ietf.org/doc/draft-ietf-quic-receive-ts/) | active WG draft (Jul 2026) | your notes cite -00 |
| [draft-ietf-ccwg-bbr-06](https://datatracker.ietf.org/doc/draft-ietf-ccwg-bbr/) | active WG draft (Jul 2026) | matches README |
| draft-kuhn-quic-bdpframe-extension-05 | expired (2024), individual | Careful Resume parameters in a QUIC frame |
| draft-seemann-quic-accurate-ack-ecn-01 | expired (2024), individual | per-range ECN (Reading_List §3) |
