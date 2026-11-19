# Related work: chapter outline

Skeleton for the thesis's related-work chapter. It goes from TCP fundamentals, through improvements to TCP
congestion control, to QUIC and what has been done for it, and ends with where this thesis fits.
Bullets only for now; each one becomes one or more paragraphs.

Sources are the notes already in this repo: [README.md](README.md), [Reading_List.md](Reading_List.md),
[Related_Papers.md](Related_Papers.md), [TCP_vs_QUIC.md](TCP_vs_QUIC.md),
[Double_Connection_Plan.md](Double_Connection_Plan.md). **†** marks a classic not yet in those files; add it
to the reading list before citing it.

---

## How to write it (general rules)

- **Length:** about 10–15 pages. Rough split: §1 TCP 15 %, §2 improvements 35 %, §3 QUIC 35 %,
  §4 positioning 15 %.
- **Within a section, organize by problem, not by date.** For example "how to exit slow start" rather than
  "papers from 2008, then 2011". Chronology only inside a sub-topic.
- **End every subsection with one or two sentences on what it leaves open for this thesis.** The chapter
  should build up to the gap; it is not a catalogue.
- **Contrast, don't just summarize.** For each close competitor, state what it does, what it assumes
  (history, router support, flow size, datacenter), and how Sprint differs.
- **Be fair about prior art** (Related_Papers.md verdict). Startup by bandwidth probing is 20+ years old;
  ECN-based startup exit exists in BBR; size-aware CC exists in datacenters. The novelty is the combination,
  the QUIC realization and the evaluation.
- **Keep the background chapter separate.** Mechanisms (how slow start works, how QUIC ACKs work) go in
  Background. Related work is about *other people's solutions*. Cross-reference instead of repeating.
- **Finish with a comparison table** (§4.2), which reviewers read first.

---

## 1. Foundations: TCP congestion control

### 1.1 Why congestion control exists
- Congestion collapse (1986) and Jacobson's response: slow start, congestion avoidance, RTO estimation (†
  Jacobson, *Congestion Avoidance and Control*, SIGCOMM 1988)
- AIMD and why it converges to fairness and efficiency († Chiu & Jain 1989)
- The end-to-end principle: the network only drops (or marks); the endpoints infer
- Point to make: everything after this is a trade-off between probing speed, queueing and fairness

### 1.2 The standard algorithms (Reno family)
- Reno and NewReno: fast retransmit, fast recovery, the "recover" point (RFC 5681, RFC 6582)
- SACK-based recovery and its limits: 3–4 blocks, reneging (RFC 2018, RFC 6675)
- RTO computation, Karn's algorithm, the 1 s minimum (RFC 6298)
- RACK-TLP: time-based loss detection and tail loss probes (RFC 8985)
- Point to make: loss detection and tail recovery dominate the FCT of short flows (Flach et al., "Gentle
  Aggression")

### 1.3 Slow start and the initial window
- Exponential growth: log2(BDP/IW) RTTs to fill the pipe, regardless of flow size
- Initial window history: RFC 3390 (2–4 segments), then IW10 (Dukkipati et al. 2010, RFC 6928)
- Measured IWs in the wild: larger than the standard (Rüth et al. IMC 2017, the CDN IW study; Qian et al.
  IMC 2009)
- Restart after idle (RFC 5681 §4.1) and congestion window validation (RFC 7661)
- Point to make: the headroom table (plan §1.3) shows short flows live entirely in slow start

### 1.4 Short flows dominate
- Flow-size distributions: mice vs elephants (Brownlee & claffy 2002; Bauer et al. ANRW 2021; Jurkiewicz
  et al.)
- Web object sizes (HTTP Archive Web Almanac 2025)
- What limits flow rates in practice (Zhang et al. SIGCOMM 2002)
- Point to make: most flows finish before CC reaches steady state, so steady-state CC research does not help
  them

---

## 2. Improving congestion control beyond Reno

### 2.1 Loss-based controllers for high-BDP paths
- CUBIC (RFC 9438): cubic growth, RTT-fairness, the de facto default; also † BIC, † HighSpeed TCP, † H-TCP
  as predecessors
- Why loss-based CC struggles: needs very low loss rates at high BDP; fills buffers (bufferbloat)
- Buffer sizing and its interaction with CC (Appenzeller et al. SIGCOMM 2004)

### 2.2 Delay-based and hybrid controllers
- Vegas (expected vs actual rate); † FAST TCP; † Compound TCP (hybrid)
- LEDBAT (RFC 6817) and scavengers: TCP-LP, TCP Nice, LEDBAT++
- Copa († or via mvfst notes): delay target with competitive mode
- Point to make: delay signals are noisy (ACK aggregation, jitter), which is why ROS and Nimble move the
  measurement to the receiver

### 2.3 Model-based and rate-based control
- BBR v1 to v3 (draft-ietf-ccwg-bbr): bandwidth and min-RTT model, pacing, Startup / Drain / ProbeBW
- Known BBR issues: fairness with CUBIC, ECN response unspecified (README §BBR known gaps)
- PCC Allegro / Vivace: utility-based online learning († Allegro; Vivace mentioned in README §Aurora)
- Point to make: BBR's Startup still doubles per round, so the O(log BDP) ramp remains

### 2.4 Slow-start exit and acceleration (the core of the chapter)
- **Better exit:** HyStart (Ha & Rhee), HyStart++ (RFC 9406), SEARCH (LCN 2023; QUIC version LANMAN 2024),
  Limited Slow Start († RFC 3742)
- **Faster growth while safe:** SUSS (SIGCOMM 2024), Rapid Start (draft-kazuho), Paced Start (ICNP 2003)
- **Probe first, then jump:** Swift Start, PCP (NSDI 2006), Jump Start (PFLDnet 2007), Halfback
  (CoNEXT 2015), RC3 (NSDI 2014), Paced Chirping (2019)
- **Bandwidth estimation theory:** packet pair (Keshav 1991), packet dispersion (Dovrolis et al. 2004),
  pathChirp (2003)
- **Survey to frame "why none deployed":** Scharf, Computer Networks 2011 and dissertation
- Point to make: each scheme picks one signal (delay, dispersion, delivered bytes) and one assumption; none
  combines receiver-side measurement, ECN and known flow size

### 2.5 Reusing state across connections
- Temporal and ensemble sharing (RFC 2140 → RFC 9040); Congestion Manager (SIGCOMM 1999)
- Careful Resume (RFC 9959) and its evaluations (neqo ANRW 2025, picoquic ASMS 2025, BDP_FRAME 2021)
- History-based startup at scale: hStart (APNet 2026), Stateful-TCP, TCP WISE (SUSS related work)
- Point to make: needs a previous connection to the same destination; cold starts are untouched

### 2.6 Network assistance: ECN, AQM, explicit feedback
- ECN (RFC 3168), relaxed rules (RFC 8311), ABE (RFC 8511), AccECN (RFC 9768)
- AQM: CoDel, FQ-CoDel, PIE (RFC 8289, 8290, 8033); fair queueing origins (Demers et al. 1989)
- DCTCP (RFC 8257) and the proportional response; L4S architecture (RFC 9330–9332), TCP Prague
- Router-assisted startup: Quick-Start (RFC 4782), † XCP, † RCP, ABC (NSDI 2020)
- ECN in practice: rarely usable on QUIC paths (Sander et al. IMC 2023)
- Point to make: ECN gives the earliest congestion signal, but deployment is partial, so any ECN design needs
  a no-ECN fallback

### 2.7 Receiver-driven and size-aware transport
- Receiver-driven rate control: Mehra & Zakhor (INFOCOM 2003), rLEDBAT (RFC 9840), PBE-CC (SIGCOMM 2020)
- Datacenter receiver-driven: Homa, NDP, ExpressPass, Aeolus
- Size-aware scheduling and CC: SRPT at web servers (Harchol-Balter et al. 2003), pFabric, PIAS, PDQ, L2DCT
- Point to make: "CC aggressiveness depends on flow size" exists, but only in datacenters with µs RTTs and
  controlled networks

### 2.8 Learning-based congestion control (short)
- † Remy, Aurora (ICML 2019), † Orca, PCC Proteus (scavenger modes)
- Point to make: no fairness guarantees and sim-to-real gaps; out of scope, but explains why the thesis uses
  an interpretable design

### 2.9 Multi-flow coordination and fairness
- Coupling a host's connections: MPAT (ICNP 2004), MulTCP (1998), ctrlTCP / TCP-CCC (2018), RFC 8699
- Parallel connections and their fairness (IFIP Networking 2018; domain sharding)
- Multipath TCP and coupled CC (RFC 8684, RFC 6356: LIA, OLIA)
- Evaluating fairness: Jain's index vs harm (Ware et al. HotNets 2019); RFC 9743 checklist
- Point to make: plan A's coupling and RQ4's harm metric both come from here

---

## 3. QUIC and congestion control for QUIC

### 3.1 QUIC as a transport (brief; details live in Background)
- Origin and deployment: Langley et al. SIGCOMM 2017; Rüth et al. PAM 2018; Zirngibl et al. IMC 2021;
  Cloudflare Radar 2025
- Standardization: RFC 9000 / 9001 / 9002; HTTP/3 (RFC 9114), priorities (RFC 9218)
- Why it matters for CC research: user-space deployment, encryption ends middlebox interference (and PEPs)
- Point to make: QUIC is where a new CCA can actually ship

### 3.2 How QUIC changes loss recovery and congestion control
- RFC 9002 §4's list of differences (packet numbers, ACK delay, ranges, PTO, persistent congestion)
- Signals TCP lacks: exact ECN counts, explicit ACK delay, sender-steered ACK frequency (TCP_vs_QUIC B1–B6)
- Constraints TCP lacks: anti-amplification limit, 1200-byte minimum, CC reset on migration (A2, E3, F4)
- Keep this short and cite TCP_vs_QUIC material; the full comparison belongs in Background

### 3.3 Porting TCP algorithms to QUIC, and what goes wrong
- Implementation diversity: "Same Standards, Different Decisions" (EPIQ 2020); "Taking a Long Look at QUIC"
  (IMC 2017, replication IMC 2024)
- CC speciation: Mishra et al. IMC 2022, IMC 2023; Nebby (SIGCOMM 2024); CC census (SIGMETRICS 2020)
- Pacing quality per stack (Kempf et al. CoNEXT 2025); CPU limits (Zhang et al. WWW 2024; QUIC on the
  Highway 2023)
- Configuration differences between hypergiants (Mücke et al. CoNEXT 2025)
- Algorithm-only ports, no QUIC-specific features: QUIC-DC (2025), AERO, LLM-tuned BBR (Reading_List §4.3)
- Point to make: "CUBIC in QUIC" is not one algorithm, so baselines must be checked (plan's calibration
  checklist)

### 3.4 Startup and short flows in QUIC
- Larger initial windows in QUIC: Blitz-starting QUIC (2019)
- SEARCH in QUIC (LANMAN 2024); HyStart++ in quiche
- Careful Resume and BDP_FRAME (resumption state via QUIC mechanisms)
- Wira (ICDCS 2024): initial window from first-frame size plus a client-carried cookie, the closest prior
  work to Sprint's size hint
- QUIC-FEC (IFIP Networking 2019): avoiding timeouts for small files on lossy paths
- The anti-amplification limit as a hidden startup constraint (README; few papers treat it)
- Point to make: nothing found that uses QUIC's ECN counts, ACK delay or ACK frequency to speed up startup

### 3.5 CC that uses QUIC-only features (Reading_List §4)
- Extension frames: BDP_FRAME, QUIC-FEC, XLINK's QoE feedback
- ACK frequency: Custura et al. 2023; the ACK-frequency draft
- One-way delay: draft-huitema-quic-ts, draft-ietf-quic-receive-ts
- Migration-aware cwnd: Kim & Koh (ICTC 2023)
- Multipath QUIC: De Coninck & Bonaventure (CoNEXT 2017), draft-ietf-quic-multipath (per-path and coupled CC)
- Pluggable protocol logic: PQUIC (SIGCOMM 2019)
- Real-time media over QUIC: Engelbart & Ott (EPIQ 2021), RTP over QUIC
- Point to make: QUIC's extensibility is used for state transfer, FEC and feedback, but rarely to change how
  a flow starts

### 3.6 Streams, priorities and one controller for many flows
- Stream scheduling: Marx et al. WEBIST 2019; RFC 9218 in the wild (ANRW 2024); non-incremental EPS (2024)
- Deadline- and priority-aware QUIC: DTP (APNet 2019), flexible priority schedulers (2023), PStream
- Priority inside the CC: PCC Priority (2023); window reservation (2023)
- Mixed-criticality single connection: CATS (arXiv 2026), the closest architecture to plan B
- Point to make: scheduling decides *what* is sent; this thesis changes *how much* is sent at startup

### 3.7 ECN and L4S with QUIC
- ECN validation and counts (RFC 9000 §13.4); measurement: Sander et al. IMC 2023
- L4S deployment over QUIC (Apple; partial-deployment study, arXiv 2024); Prague fallback (2019)
- Point to make: QUIC's per-packet ECN feedback is ready for a proportional response, but RFC 9002 and BBR
  do not use it that way

---

## 4. Positioning this thesis

### 4.1 The gap, in three sentences
- Startup schemes either need history, router support, or stay in the doubling regime
- QUIC-specific CC work focuses on resumption, FEC and multipath, not on cold-start probing
- Size-aware aggressiveness exists only in datacenters; per-class CC on separate connections exists only as
  scavengers (bulk made *less* aggressive)

### 4.2 Comparison table (draft the columns now)
- Rows: HyStart++, SEARCH, SUSS, BBR Startup, Paced Chirping, Halfback, RC3, Jump Start, Careful Resume,
  Wira, L2DCT, CATS, PCC Priority, ctrlTCP / MPAT, Sprint (this thesis)
- Columns: signal used (delay / rate / loss / ECN); needs history?; needs router support?; uses flow size?;
  receiver involvement?; transport (TCP / QUIC / datacenter); evaluated harm or fairness?

### 4.3 Claimed contributions, phrased against prior work
- Reuse the defensible wording from Related_Papers.md "How to phrase the contribution"
- One bullet per contribution, each with the closest prior work and the difference

---

## Gaps to fill before writing
- Add the † classics to Reading_List.md (Jacobson 1988, Chiu & Jain 1989, BIC / H-TCP, FAST, Compound,
  Copa, PCC Allegro, Remy, Orca, XCP / RCP, RFC 3742)
- Read the full texts of the closest competitors: Wira, CATS, PCC Priority, SUSS, Paced Chirping, Halfback,
  L2DCT
- Settle the ROS / Nimble Start citation form with the supervisor (plan §9)
- Re-run the prior-art check in the final month (Related_Papers.md "Keep checking")
