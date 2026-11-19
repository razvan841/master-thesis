# Has this been done? Prior-art check for plan A and plan B

Checked: October 2026. **Method**: web searches for each building block of
[THESIS_PLAN.md](THESIS_PLAN.md) (option A, two connections) and
[THESIS_PLAN_SINGLE_CONNECTION.md](THESIS_PLAN_SINGLE_CONNECTION.md) (option B, one stream-aware connection),
plus the Sprint startup algorithm shared by both.

**Limits:**
- "Not found" does not mean "does not exist". Search engines miss workshop papers, theses and industry work.
- ROS and Nimble Start are unpublished, so nothing could be checked about them.
- Re-check before submission (see the end of this file).

---

## Verdict

1. **No published work was found that combines everything you plan:**
   - congestion control that treats short and long transfers differently, based on known transfer size;
   - implemented in QUIC;
   - organized either as two connections (plan A) or as one stream-aware controller (plan B);
   - with a receiver- and ECN-assisted startup.

   As a whole, the thesis looks new.
2. **Almost every building block has prior art.** Startup by bandwidth estimation is 20+ years old, ECN-based
   startup exit exists in BBRv2, size-aware congestion control exists in datacenters, and coupling
   connections is known. Your novelty is the **combination, the QUIC realization and the evaluation**, not
   any single mechanism. Cite the closest work for each piece explicitly.
3. **The papers you must read and contrast with:**
   - **Plan B**: **CATS** (arXiv, June 2026), the closest architecture; **PCC Priority** (IEEE Networking
     Letters, 2023), the closest *congestion-control-level* stream awareness in QUIC.
   - **Plan A**: **TCP-LP / TCP Nice** (2002–2003) and the LEDBAT / LEDBAT++ deployments, i.e. a per-class
     CC on a separate connection; **MPAT** (ICNP 2004), coupled connections with priority; **rLEDBAT**
     (RFC 9840), the receiver controlling a class through its window.
   - **Both**: **L2DCT** (INFOCOM 2013), congestion control whose aggressiveness depends on how much a flow
     has sent, in datacenters.
4. **Plan A's architecture is not new.** Separate connections with per-class congestion control is
   established and deployed (Apple, Microsoft). What is new is the direction: a *more* aggressive,
   short-flow-specific startup on the express lane, while the bulk lane stays standard rather than a
   scavenger. See "Plan A, closer look".

---

## Plan A: two connections (express lane and bulk lane)

| Building block | Closest prior work | What they did | Difference from your plan | Overlap risk |
|---|---|---|---|---|
| Separate short and long flows | *Isolating Mice and Elephant in Data Centers* ([arXiv 2016](https://arxiv.org/abs/1605.07732)); *Freeway* ([ResearchGate](https://www.researchgate.net/publication/263581387_Freeway_Adaptively_Isolating_the_Elephant_and_Mice_Flows_on_Different_Transmission_Paths)); Guo & Matta, *The War Between Mice and Elephants*, ICNP 2001 ([IEEE](https://ieeexplore.ieee.org/document/992898/)) | Separate queues, separate paths, or in-network preference (RIO) for short flows | All are **in-network** or datacenter-only. None runs a different CC algorithm per class on two QUIC connections between the same endpoints. | Low |
| Several connections on purpose | *Domain-Sharding for Faster HTTP/2 in Lossy Cellular Networks* ([arXiv 2017](https://arxiv.org/abs/1707.05836)); browsers' 6 parallel TCP connections | More connections, so more aggregate window and less head-of-line blocking | Classes are not distinguished by size, the CC is the same, and they claim *more* share. You want different behaviour per class with *bounded* share. | Low |
| Coupling the lanes | **Congestion Manager** ([SIGCOMM 1999](http://nms.lcs.mit.edu/papers/cm-sigcomm99.html)); **ctrlTCP** ([INFOCOM WKSHPS 2018](https://ieeexplore.ieee.org/document/8406887)); TCP-CCC ([draft](https://datatracker.ietf.org/doc/draft-welzl-tcp-ccc/)); RFC 9040 | Several connections share one congestion state; short flows reuse the large cwnd and skip slow start (ctrlTCP) | They couple for **fairness and cwnd reuse** in TCP. You couple to **prioritize short transfers and bound aggression** in QUIC. The mechanism is close, so cite it as the basis. | **Medium** |
| The application declares its traffic class per connection | **RFC 9622** (TAPS API), "Capacity Profile" property ([RFC](https://www.rfc-editor.org/info/rfc9622/)) | The application marks a connection as Scavenger, Low Latency/Interactive, …; the stack picks protocols, CC and DSCP | Abstract API with no concrete short-flow CC. Your plan A is a concrete instance; cite it as the standard hook. | Low |
| Server steers responses between connections | none found | — | The `MOVED` + push-on-bulk-lane design with no extra RTT appears new (simple, though; don't oversell it). | Low |

### Plan A, closer look: "different CC per traffic class, one connection per class"

The core idea of plan A was stated over 20 years ago. TCP-LP's paper says that "end-to-end differentiation
can be achieved by having different end-host applications employ different congestion control
algorithms". Three groups of work matter.

**1. Separate connection, *less* aggressive CC for the bulk class (scavengers).** Well established, and
deployed at scale.

| Work | Link | What it does |
|------|------|--------------|
| ★ Venkataramani, Kokku, Dahlin, *TCP Nice: A Mechanism for Background Transfers*, OSDI 2002 | [USENIX](https://www.usenix.org/legacy/events/osdi02/tech/venkataramani.html) · [homepage](https://www.cs.utexas.edu/~dahlin/software/2002-nice.html) | Background flows on their own connections with a delay-based CC that provably bounds interference with foreground flows. |
| ★ Kuzmanovic & Knightly, *TCP-LP: A Distributed Algorithm for Low Priority Data Transfer*, INFOCOM 2003 (ToN 2006) | [PDF](https://infocom2003.ieee-infocom.org/papers/41_04.PDF) · [ToN](https://dl.acm.org/doi/10.1109/TNET.2006.879702) | Two-class service with no network support: the low class uses a different CC on a different connection. |
| LEDBAT (RFC 6817), used by Apple for software updates | [RFC](https://datatracker.ietf.org/doc/html/rfc6817) · [overview](https://en.wikipedia.org/wiki/LEDBAT) | A scavenger CC for bulk background downloads, deployed in production. |
| LEDBAT++ (Microsoft) | [draft](https://datatracker.ietf.org/doc/html/draft-irtf-iccrg-ledbat-plus-plus-03) | Deployed in Windows Update, Delivery Optimization, OneDrive, Store, Office, Xbox and BITS. Large-scale proof that "bulk on its own connection with its own CC" works. |
| PCC Proteus (SIGCOMM 2020) | [PDF](https://pbg.cs.illinois.edu/papers/meng20pcc-proteus.pdf) | Scavenger mode with better yielding than LEDBAT. |

**2. One host coordinating its own connections, giving some more and others less.** This is your
coupling.

| Work | Link | What it does |
|------|------|--------------|
| ★ Singh, Pradhan, Francis, *MPAT: Aggregate TCP Congestion Management as a Building Block for Internet QoS*, ICNP 2004 | [PDF](https://www.ieee-icnp.org/2004/papers/3-4.pdf) | Shares the congestion windows of a host's flows so that chosen flows get more and others less, while the aggregate is as fair as N TCP flows. **This is essentially plan A's "coupled lanes with priority to express".** |
| Crowcroft & Oechslin, *Differentiated End-to-End Internet Services Using a Weighted Proportional Fair Sharing TCP* (MulTCP), CCR 28(3), 1998 | [dblp, CCR vol. 28](https://dblp.org/db/journals/ccr/ccr28.html) | One flow behaves like N TCP flows (a weighted, more aggressive share). |
| Congestion Manager (1999); ctrlTCP / TCP-CCC (2018); RFC 9040 | see table above | Shared congestion state across one host's connections. |
| ★ **RFC 9840**, *rLEDBAT: Receiver-Driven Low Extra Delay Background Transport for TCP* | [RFC](https://www.rfc-editor.org/rfc/rfc9840.html) | The **receiver** uses its receive window to make a transfer behave as background traffic. This is exactly the "client-side coupling via flow-control credit" in [USE_CASES.md](USE_CASES.md) A3, and close to ROS's use of rwnd as a control signal. |

**3. Using several connections on purpose, and why the IETF dislikes mixing classes in one connection.**

| Work | Link | Relevance |
|------|------|-----------|
| *Is There a Case for Parallel Connections with Modern Web Protocols?*, IFIP Networking 2018 | [PDF](https://dl.ifip.org/db/conf/networking/networking2018/8B3-1570422313.pdf) | Two parallel HTTP/2 connections cut page load time by about 55 % at 2 % loss. Evidence that more than one connection can pay off, and also the fairness critique your coupling must answer. |
| *Domain-Sharding for Faster HTTP/2 in Lossy Cellular Networks*, arXiv 2017 | [arXiv](https://arxiv.org/abs/1707.05836) | Same direction, for cellular. |
| RFC 7657 (Diffserv and real-time communication) | [RFC](https://datatracker.ietf.org/doc/html/rfc7657) | Recommends **one DSCP per transport connection**: several DSCPs inside one 5-tuple cause reordering and CC interactions. A strong argument *for* plan A when the classes need different network treatment, and a caveat for plan B's E6. |
| Classic protocols: FTP (separate control and data connections), WebRTC/RTP (media vs data channels) | — | The control/data split is long-standing engineering practice, so don't present "two connections" as the novelty. |

### What is and is not new in plan A

| Claim | Status |
|-------|--------|
| "Use separate connections with different CCs for different traffic classes" | **Not new** (TCP Nice, TCP-LP, LEDBAT deployments) |
| "Coordinate a host's connections to give one class priority while the aggregate stays fair" | **Not new** (MPAT; Congestion Manager; ctrlTCP) |
| "The receiver controls one class through its receive window" | **Not new** (rLEDBAT, RFC 9840; Mehra & Zakhor 2003) |
| Express lane *more* aggressive than standard **in startup**, with a short-flow-specific CC (Sprint), while the bulk lane stays standard rather than scavenger | **Not found.** Prior work slows the bulk class down (scavengers) or reweights steady-state shares (MulTCP, MPAT); none targets startup of short transfers. |
| Size-based steering by the server between two QUIC connections, with no extra RTT on a misroute | **Not found** (modest) |
| Per-lane network treatment with QUIC (an L4S/ECT(1) express lane, DSCP per lane) evaluated for short-flow FCT | **Not found** as a study, though it is a direct application of L4S and RFC 7657 |

**Consequences for plan A:**
1. **Add a scavenger baseline.** Run the bulk lane on LEDBAT++ (or TCP-LP-like) with plain CUBIC on the
   express lane: "A3s, the established approach". Reviewers will ask "why not just make bulk a scavenger?"
   - The answer to test: a scavenger yields to *everyone*, including other users' traffic. A bulk download
     the user is actively waiting for (your 50 MB file) can then starve.
   - Plan A's coupling yields only to the client's *own* express traffic and otherwise competes normally.
2. **Cite MPAT for the coupling**, and present your coupling as MPAT-style aggregate control specialised
   for startup bursts of short transfers.
3. **Cite rLEDBAT and Mehra & Zakhor** wherever the receiver-driven (ROS-style) parts appear.
4. **Make the startup algorithm and the evaluation the headline.** The split itself is established
   practice.

**Plan A summary.** The architecture is well-trodden ground: separate connections, per-class CC,
coordinated windows. Plan A is publishable as *the first evaluation of an aggressive short-flow startup on
a dedicated, coupled QUIC lane, against the established scavenger-lane approach*. It is not publishable as
a new architecture.

---

## Plan B: one connection with a stream-aware controller

| Building block | Closest prior work | What they did | Difference from your plan | Overlap risk |
|---|---|---|---|---|
| Mixed traffic classes in one connection, one controller | ★ **CATS**: Rizvi, *Single-Connection Mixed-Criticality Transport with CATS: Bounded Guarantees, Three Structural Limits, and a QUIC Escape*, arXiv, June 2026 ([arXiv](https://arxiv.org/abs/2606.16924)) | Sender-side priority classes with a credit-based shaper in one connection. Argues that parallel connections break single-flow fairness and that QUIC's per-stream sequence spaces plus one aggregate controller give bounded per-class delay. ns-3 evaluation. | Very close in **architecture and motivation**, but it is about **scheduling** latency-critical messages against bulk. It does not give short transfers a faster startup or use size-dependent aggressiveness, ECN or receiver feedback. Your shadow-controller and excess-budget idea is absent. | **Medium–high: must cite** |
| Priority-aware congestion control between QUIC streams | ★ **PCC Priority**: *A Priority-Aware Bandwidth Allocation Framework for QUIC*, IEEE Networking Letters 2023 ([IEEE](https://ieeexplore.ieee.org/document/10106393/) · [ResearchGate](https://www.researchgate.net/publication/370194303_PCC_Priority_A_Priority-Aware_Bandwidth_Allocation_Framework_for_QUIC)) | Puts stream priorities into PCC's utility function, so streams get bandwidth proportional to priority, with an equilibrium proof | The closest **CC-level** stream awareness. But it splits a fixed total by priority; it is not about startup, short transfers or file size, and it is PCC-based (no loss/ECN startup, no "bulk stays standard" guarantee). | **Medium–high: must cite** |
| Reserving part of the window for priority data | *Performance Evaluation of a New Scheduling Model Using Congestion Window Reservation* (multipath QUIC), arXiv 2023 ([arXiv](https://arxiv.org/abs/2305.03684)) | Reserves part of the cwnd so time-critical data can go immediately | Static reservation inside the existing window. Your plan has a dynamic express budget *above* a shadow standard window, bounded by a token bucket. | Medium |
| Deadline- or priority-aware QUIC scheduling | **DTP**, Deadline-aware Transport Protocol, APNet 2019 ([PDF](https://www-users.cse.umn.edu/~fengqian/paper/dtp_apnet19.pdf) · [draft](https://www.ietf.org/archive/id/draft-shi-quic-dtp-06.html)); Marx et al., HTTP/3 prioritization, WEBIST 2019 ([PDF](https://www.scitepress.org/PublishedPapers/2019/81917/pdf/index.html)); *Flexible Priority-based Stream Schedulers in QUIC*, 2023 ([ACM](https://dl.acm.org/doi/10.1145/3616394.3618267)); SA-ECF stream-aware multipath scheduler, EPIQ 2018 ([PDF](https://people.computing.clemson.edu/~jmarty/projects/lowLatencyNetworking/papers/TransportProtocols/QUIC-MPQuic/ASTreamAwareMPATHQUIkSchedulerforhetnetspaths.pdf)) | Decide **which stream sends** (by priority, deadline, completion time); DTP also drops expired blocks | Scheduling only: the CC is unchanged. These are your scheduling-only baselines (B2/B3), and they show why the ablation ladder matters. | Medium |
| Congestion control whose aggressiveness depends on flow size | ★ **L2DCT**, *Minimizing Flow Completion Times in Data Centers*, INFOCOM 2013 ([PDF](https://web.lums.edu.pk/~ihsan/papers/l2dct-infocom2013.pdf)); PIAS ([NSDI 2015](https://www.cse.ust.hk/~kaichen/papers/pias-nsdi15.pdf)); PDQ ([arXiv](https://arxiv.org/abs/1206.2057)); pFabric; Homa ([arXiv](https://arxiv.org/abs/1803.09615)) | L2DCT changes window growth and backoff with the bytes a flow has sent (least attained service), using ECN, so short flows are more aggressive. Others use in-network priorities. | **The idea "CC aggressiveness depends on flow size" exists**, but only in datacenters (DCTCP-style ECN, µs RTTs, controlled networks). Not WAN, not QUIC, not startup, not within one connection. Claim "first for wide-area QUIC", not "first ever". | **Medium–high: must cite** |
| Different aggressiveness classes in one sender | PCC Proteus primary/scavenger modes ([SIGCOMM 2020](https://pbg.cs.illinois.edu/papers/meng20pcc-proteus.pdf)); LEDBAT (RFC 6817); MulTCP (CCR 1998) | Per-flow modes or weights | Per *flow* or connection, not per stream class inside one connection with a standard shadow. | Low–medium |
| SRPT for web responses | Harchol-Balter et al., TOCS 2003 ([PDF](https://www.cs.utoronto.ca/~bianca/papers/submtocs-3.pdf)) | SRPT across TCP connections at the server | Not within a multiplexed connection, and no CC change. No paper found doing SRPT *inside* an HTTP/2 or HTTP/3 connection by response size. | Low |

**Plan B summary.** The **scheduling** part (priorities inside one QUIC connection) is well studied. The
**CC-level** parts look new as a design:
- the class-aware budget with a shadow standard controller that guarantees bulk streams a normal schedule;
- the token-bucket-bounded excess;
- drain-on-express.

Position plan B as "CATS / PCC Priority, but for **startup aggressiveness of short transfers** with a
**standard-behaviour guarantee for long ones**".

---

## Sprint startup (both plans)

| Building block | Closest prior work | Difference / what stays new | Overlap risk |
|---|---|---|---|
| Estimate bandwidth early, then jump to a rate | **Swift Start** (BBN; packet pair + pacing; [NASA evaluation](https://ntrs.nasa.gov/api/citations/20050160231/downloads/20050160231.pdf)); **Paced Start**, Hu & Steenkiste, ICNP 2003 ([PDF](https://www.cs.cmu.edu/afs/cs/project/cmcl/archive/papers/past-icnp03.pdf)); PCP, NSDI 2006; Jump Start; Quick-Start (RFC 4782); Blitz-starting QUIC ([arXiv](https://arxiv.org/abs/1905.03144)); Paced Chirping; SUSS; SEARCH. Surveys: Scharf, *Comparison of end-to-end and network-supported fast startup schemes*, Computer Networks 2011 ([ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S1389128611000491)) and his dissertation ([PDF](https://content.ikr.uni-stuttgart.de/Content/Publications/Archive/Sf_Diss_40112.pdf)) | **This is old.** The new part can only be the specific combination: receiver-side train measurement, slow/fast half rounds, ECN exits and the κ cautious step, in QUIC, with known transfer size. Use Scharf's survey to frame "why previous fast-startup schemes didn't deploy". | **High** |
| Receiver measures bandwidth and controls the sender through the receive window | **Mehra & Zakhor, *Receiver-Driven Bandwidth Sharing for TCP*, INFOCOM 2003** ([PDF](https://infocom2003.ieee-infocom.org/papers/28_03.PDF)): sets advertised window = RTTmin × bandwidth estimate. Also PBE-CC (receiver-side PHY capacity, [SIGCOMM 2020](https://arxiv.org/abs/2002.03475)); Homa/NDP (datacenter). | Mehra & Zakhor's encoding is essentially **ROS's rwnd = bandwidth × ROS_RTT**. Mention it to the ROS authors, cite it, and present your QUIC frame (`RATE_REPORT`) plus the startup use as the difference. | **Medium–high** |
| Exit or slow startup on ECN marks | **BBRv2/v3**: Startup exits when the ECN mark rate exceeds a threshold (about 50 %) for 2 rounds, L4S-style ([BBRv2 intro, TUM](https://www.net.in.tum.de/fileadmin/TUM/NET/NET-2023-11-1/NET-2023-11-1_16.pdf)); Paced Chirping with L4S marks; DCTCP-style proportional response; L2DCT | ECN-driven startup exit **exists**. Your differentiators: CE attributed to the slow vs fast half of a probe round; proportional correction of the rate estimate; CE history used to tell random loss from congestion loss; works with classic AQMs (CoDel / FQ-CoDel), not only L4S; and QUIC's exact counts. | **Medium–high** |
| Warm start from cached or other-connection state | Careful Resume (RFC 9959; neqo and picoquic studies); hStart; Blitz-starting QUIC; BDP-frame drafts | Within-session sharing (between lanes in A, within the connection in B) is a small twist. Cite these as the baseline family. | Medium |

---

## What looks genuinely new (claim these, carefully)

1. **A standard-behaviour guarantee for long streams inside a QUIC connection that is aggressive for
   short ones.** The shadow-controller budget means that with no express data the controller is the
   baseline, and bulk never benefits from the excess. Not found anywhere.
2. **Bounded, short-lived excess per connection**: a per-stream quota plus a token bucket, with an explicit
   long-run bound on extra traffic. Not found as a mechanism for short-flow acceleration.
3. **Drain-on-express**: clearing the connection's *own* standing queue when short data arrives. Related
   ideas exist (BBR Drain; ctrlTCP's lower queues), but not this trigger and purpose.
4. **Two ECN codepoints inside one QUIC connection**, plus class-aware loss detection (plan B, optional).
   Not found.
5. **A server-steered two-lane QUIC design with coupled lanes** (plan A). Not found, though the novelty is
   modest.
6. **The specific Sprint combination in QUIC**, evaluated explicitly for the no-ECN case.
7. **Methodology**: an evaluation that separates architecture vs scheduling vs CC vs network (FQ × ECN
   factorial). Rarely done this cleanly; a solid secondary contribution.

## How to phrase the contribution

Avoid "first to accelerate short flows" or "first ECN-based startup exit": both are false. Defensible
wording, for example:

> "To our knowledge, this is the first design that gives short transfers an evidence-based aggressive
> startup *inside* a QUIC connection, or across a coupled pair of connections, while guaranteeing that long
> transfers keep a standard congestion-control schedule. It differs from priority scheduling (CATS, DTP,
> HTTP/3 priorities) by changing *how much* is sent, not only *what* is sent. It differs from
> priority-proportional CC (PCC Priority) and datacenter size-aware CC (L2DCT, PIAS) by targeting
> wide-area startup with bounded excess."

## Keep checking (novelty can disappear during the thesis)

- Set Google Scholar alerts for papers **citing**:
  - CATS (2026);
  - PCC Priority (2023);
  - ctrlTCP (2018);
  - Careful Resume (RFC 9959 / ANRW'25);
  - SUSS (SIGCOMM'24).
- Watch the IETF CCWG and QUIC WG documents (new startup or priority drafts) and the ICCRG agenda.
- Ask your supervisor and the ROS / Nimble Start authors whether they know of unpublished or in-review work
  on stream-aware or size-aware QUIC congestion control.
- Re-run this check in the last month before submission and update the related-work chapter.
