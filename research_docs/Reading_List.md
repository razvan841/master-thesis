# Reading list, Internet traffic facts, and QUIC's measurement advantages

Companion to [THESIS_PLAN.md](THESIS_PLAN.md) (option A, two connections),
[THESIS_PLAN_SINGLE_CONNECTION.md](THESIS_PLAN_SINGLE_CONNECTION.md) (option B, one stream-aware connection)
and [USE_CASES.md](USE_CASES.md). Papers already summarized in [README.md](README.md) are not repeated
(SUSS, HyStart, BBR, Aurora, Fair Queueing, Sizing Router Buffers).

**Read first: ★.** The "Why" column says which part of the plans each paper informs.

---

## 1. Work close to the implementation plans

### 1.1 Faster startup for short flows (the Sprint algorithm in both plans)

| | Paper | Link | Why |
|---|-------|------|-----|
| ★ | Dukkipati et al., *An Argument for Increasing TCP's Initial Congestion Window*, CCR 2010 | [ACM](https://dl.acm.org/doi/10.1145/1823844.1823848) · [Google](https://research.google/pubs/an-argument-for-increasing-tcps-initial-congestion-window/) | Origin of IW = 10. Quantifies how much short web transfers lose to slow start; the classic motivation for your thesis. |
| ★ | Flach et al., *Reducing Web Latency: the Virtue of Gentle Aggression*, SIGCOMM 2013 | [PDF](https://conferences.sigcomm.org/sigcomm/2013/papers/sigcomm/p159.pdf) | Google data: lossy short transfers take about 5× longer. The title is your thesis's attitude in four words: "gentle aggression". |
| ★ | Mittal et al., *Recursively Cautious Congestion Control* (RC3), NSDI 2014 | [PDF](https://www.usenix.org/system/files/conference/nsdi14/nsdi14-paper-mittal.pdf) | Fills the pipe in the first RTT using low-priority packets: over 40 % lower FCT, and over 70 % for 100 KB–3 MB flows (exactly your size range). |
| ★ | Li, Dong, Godfrey, *Halfback: Running Short Flows Quickly and Safely*, CoNEXT 2015 | [ACM](https://doi.org/10.1145/2716281.2836107) | Sender-only, aggressive and paced short-flow start with a safety argument. A good template for your safety section. |
| | Liu, Allman, Jin, Wang, *Congestion Control Without a Startup Phase* (Jump Start), PFLDnet 2007 | [PDF](https://www.icir.org/mallman/pubs/LAJW07/LAJW07.pdf) | The extreme point: no slow start at all. Useful as a "too aggressive" reference. |
| | Anderson et al., *PCP: Efficient Endpoint Congestion Control*, NSDI 2006 | [PDF](https://www.usenix.org/legacy/events/nsdi06/tech/full_papers/anderson/anderson.pdf) | Probes available bandwidth with short explicit probes before committing to a rate. Closest older relative of the Nimble/ROS probing in Sprint. |
| ★ | Misund & Briscoe, *Paced Chirping: Rapid flow start with very low queuing delay*, INFOCOM WKSHPS 2019 | [ResearchGate](https://www.researchgate.net/publication/335994999_Paced_Chirping_Rapid_flow_start_with_very_low_queuing_delay) · [netdev paper](https://bobbriscoe.net/projects/latency/paced_chirping_netdev0x13.pdf) | Probe trains (chirps) read through queueing delay or L4S marks. The closest prior work to "probe trains + ECN". |
| | Kachooei et al., *SEARCH: Robust TCP Slow Start Performance over Satellite Networks*, LCN 2023 | [PDF](https://web.cs.wpi.edu/~claypool/papers/search-lcn-23/paper.pdf) · [project](https://search-ss.wpi.edu/) | Slow-start exit from delivered bytes. A candidate extra baseline. |
| | *Improving QUIC Slow Start Behavior in Wireless Networks with SEARCH*, LANMAN 2024 | [PDF](https://web.cs.wpi.edu/~claypool/papers/quic-search-lanman-24/paper.pdf) | The same algorithm inside QUIC, with a QUIC-specific evaluation. |
| | Rüth et al., *Blitz-starting QUIC Connections*, arXiv 2019 | [arXiv](https://arxiv.org/abs/1905.03144) | Large initial windows in QUIC and their effect on short transfers and on others. |
| ★ | Fischer et al., *Getting up to Speed with Neqo and Careful Resume*, ANRW 2025 | [ACM](https://dl.acm.org/doi/10.1145/3744200.3744766) | Careful Resume (RFC 9959) in a Rust QUIC stack: several RTTs saved per transfer, little harm. The warm-start baseline. |
| | *Careful Resume: Design and Analysis with Picoquic* (satellite), ASMS 2025 | [PDF](https://www.cs7.tf.fau.de/files/2025/02/ASMS2025-Careful-Resume-Submitted.pdf) | Same idea in picoquic, the stack recommended for option B. |
| | *Multi-CDN as a Collective Service: Towards Hot Start in Congestion Control at Scale* (hStart), APNet 2026 | [ACM](https://doi.org/10.1145/3820441.3820445) | History-based startup across CDN nodes, deployed in QUIC. |

### 1.2 One controller for several flows, or coupled controllers (plan A coupling, plan B's single controller)

| | Paper | Link | Why |
|---|-------|------|-----|
| ★ | Balakrishnan, Rahul, Seshan, *An Integrated Congestion Management Architecture for Internet Hosts* (Congestion Manager), SIGCOMM 1999 | [MIT](http://nms.lcs.mit.edu/papers/cm-sigcomm99.html) | One congestion controller shared by all flows to a host, plus a scheduler. Option B is essentially a class-aware Congestion Manager inside QUIC. |
| ★ | Islam et al., *ctrlTCP: Reducing latency through coupled, heterogeneous multi-flow TCP congestion control*, INFOCOM WKSHPS 2018 | [IEEE](https://ieeexplore.ieee.org/document/8406887) · [ResearchGate](https://www.researchgate.net/publication/326705191_ctrlTCP_Reducing_latency_through_coupled_heterogeneous_multi-flow_TCP_congestion_control) | Couples several TCP connections: short flows reuse the existing large cwnd and skip slow start, giving lower delay and loss. Option A's coupling, done for TCP. |
| | Welzl et al., *TCP-CCC: single-path TCP congestion control coupling* (Internet-Draft) | [IETF](https://datatracker.ietf.org/doc/draft-welzl-tcp-ccc/) | The protocol side of ctrlTCP. |
| | Narayan et al., *Restructuring Endpoint Congestion Control* (CCP), SIGCOMM 2018 | [PDF](https://people.csail.mit.edu/alizadeh/papers/ccp-sigcomm18.pdf) | Moves CC out of the datapath behind a common API, including a QUIC datapath. Relevant if you want one Sprint code base usable from several stacks. |
| | Meng et al., *PCC Proteus: Scavenger Transport and Beyond*, SIGCOMM 2020 | [PDF](https://pbg.cs.illinois.edu/papers/meng20pcc-proteus.pdf) | One CC framework with "primary" and "scavenger" modes: different aggressiveness per traffic class, which is your express/bulk split. |

### 1.3 Size-aware scheduling and stream prioritization (option B)

| | Paper | Link | Why |
|---|-------|------|-----|
| ★ | Harchol-Balter et al., *Size-Based Scheduling to Improve Web Performance*, ACM TOCS 2003 | [PDF](https://www.cs.utoronto.ca/~bianca/papers/submtocs-3.pdf) | SRPT at web servers: much lower mean response time, while large requests barely suffer. Theory for option B's "bulk keeps a normal schedule". |
| ★ | Bai et al., *Information-Agnostic Flow Scheduling for Commodity Data Centers* (PIAS), NSDI 2015 | [PDF](https://www.cse.ust.hk/~kaichen/papers/pias-nsdi15.pdf) | Demotes flows as they send more bytes; your fallback when the file size is unknown. |
| | Montazeri et al., *Homa: A Receiver-Driven Low-Latency Transport Protocol Using Network Priorities*, SIGCOMM 2018 | [arXiv](https://arxiv.org/abs/1803.09615) | Receiver-driven, SRPT-like. Shares ROS's "the receiver knows best" idea, in the datacenter. |
| | Hu et al., *Aeolus: A Building Block for Proactive Transport in Datacenters*, SIGCOMM 2020 | [PDF](https://www.cse.ust.hk/~kaichen/papers/aeolus-sigcomm20.pdf) | How to use the first RTT aggressively without hurting scheduled traffic. |
| ★ | Marx et al., *Of the Utmost Importance: Resource Prioritization in HTTP/3 over QUIC*, WEBIST 2019 | [PDF](https://www.scitepress.org/PublishedPapers/2019/81917/pdf/index.html) | Compares 11 HTTP/3 stream schedulers. Your scheduling-only baselines. |
| | Herbots, Marx et al., *HTTP/3's Extensible Prioritization Scheme in the Wild*, ANRW 2024 | [PDF](https://jherbots.info/public_media/research/anrw2024_h3-eps-in-the-wild_authorversion.pdf) | How RFC 9218 priorities are actually used. Relevant to deploying option B via HTTP/3. |
| | *Improving Web Content Delivery with HTTP/3 and Non-Incremental EPS*, arXiv 2024 | [arXiv](https://arxiv.org/abs/2404.13460) | Scheduling choices inside one HTTP/3 connection and their effect on page load. |

### 1.4 ECN and L4S (the ECN part of Sprint)

| | Paper | Link | Why |
|---|-------|------|-----|
| ★ | Sander et al., *ECN with QUIC: Challenges in the Wild*, IMC 2023 | [arXiv](https://arxiv.org/abs/2309.14273) | Fewer than 2 % of QUIC hosts pass ECN validation, so the no-ECN fallback is the common case. |
| | Goyal et al., *ABC: A Simple Explicit Congestion Controller for Wireless Networks*, NSDI 2020 | [arXiv](https://arxiv.org/abs/1905.03429) | Reuses the ECN bits as accelerate/brake signals; the most creative use of the "ECN bit". |
| | *To Adopt or Not to Adopt L4S-Compatible Congestion Control? Understanding Performance in a Partial L4S Deployment*, arXiv 2024 | [arXiv](https://arxiv.org/abs/2411.10952) | What happens when only some flows and bottlenecks speak L4S. Relevant to the optional ECT(1) express lane. |
| | Briscoe et al., *TCP Prague Fall-back on Detection of a Classic ECN AQM*, arXiv 2019 | [arXiv](https://arxiv.org/abs/1911.00710) | How an L4S sender detects classic-ECN bottlenecks and falls back. Needed if Sprint uses ECT(1). |

### 1.5 Evaluating congestion control and QUIC stacks (your methodology)

| | Paper | Link | Why |
|---|-------|------|-----|
| ★ | Ware et al., *Beyond Jain's Fairness Index: Setting the Bar for the Deployment of CCAs*, HotNets 2019 | [PDF](https://www.cs.swarthmore.edu/~rware/assets/pdf/ware-hotnets19.pdf) | The "harm" metric used in both plans. |
| ★ | Mishra & Leong, *Containing the Cambrian Explosion in QUIC Congestion Control*, IMC 2023 | [PhD thesis, ch. 5](https://www.comp.nus.edu.sg/~bleong/publications/phd-ayush.pdf) | Many QUIC CUBIC/BBR implementations do not match the kernel versions. Check your baselines. |
| | Mishra et al., *Understanding Speciation in QUIC Congestion Control*, IMC 2022 | [ACM](https://dl.acm.org/doi/10.1145/3517745.3561459) | Earlier part of the same story. |
| | Mishra et al., *Keeping an Eye on Congestion Control in the Wild with Nebby*, SIGCOMM 2024 | [ACM](https://dl.acm.org/doi/10.1145/3651890.3672223) | Identifies the CCAs deployed by servers, including QUIC ones, i.e. who your algorithm competes against. |
| ★ | Kempf et al., *QUIC Steps: Evaluating Pacing Strategies in QUIC Implementations*, CoNEXT 2025 | [arXiv](https://arxiv.org/abs/2505.09222) | Pacing quality per stack; probe trains depend on it. |
| ★ | Kakhki et al., *Taking a Long Look at QUIC*, IMC 2017 | [PDF](https://mislove.org/publications/QUIC-IMC.pdf) | A rigorous method for testing QUIC performance (calibration, state machines). Also see the *Replication* paper at IMC 2024: [ACM](https://dl.acm.org/doi/10.1145/3646547.3688453). |
| | Zhang et al., *QUIC is not Quick Enough over Fast Internet*, WWW 2024 | [arXiv](https://arxiv.org/abs/2310.09423) | CPU and receive-path limits of user-space QUIC at high rates. A threat to validity for 1 Gbit/s tests. |
| | Rüth, Kunze, Hohlfeld, *An Empirical View on Content Provider Fairness*, TMA 2019 | [arXiv](https://arxiv.org/abs/1905.07152) | Real CDNs tune their stacks (e.g. larger IWs) and are unfair to each other; FQ-CoDel helps. |
| | Rüth et al., *Large-Scale Scanning of TCP's Initial Window*, IMC 2017 | [PDF](https://conferences.sigcomm.org/imc/2017/papers/imc17-final43.pdf) | Which initial windows servers really use. A realistic large-IW baseline. |
| | *Demystifying TCP Initial Window Configurations of Content Distribution Networks*, arXiv 2019 | [arXiv](https://arxiv.org/abs/1902.08937) | Same question for CDNs specifically. |

---

## 2. Internet traffic: what the numbers say

### 2.1 Flow sizes: most flows are tiny, most bytes are in a few large flows

| Paper | Link | Key numbers |
|-------|------|-------------|
| ★ Bauer et al., *On the Evolution of Internet Flow Characteristics*, ANRW 2021 | [PDF](https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/bauer_anrw21.pdf) | CAIDA backbone traces from 2008–2019, 2.5 billion TCP flows. **About 85 % of TCP flows carry between 100 B and 10 kB.** The 99th-percentile flow size is roughly 250 kB to several hundred kB. Bytes are persistently dominated by heavy hitters. TCP carries over 90 % of bytes in the Chicago traces and about 80 % in the 2018–19 New York traces. |
| ★ Jurkiewicz, Rzym, Boryło, *Flow length and size distributions in campus Internet traffic*, arXiv 2018/2020 | [arXiv](https://arxiv.org/abs/1809.03486) | 4 billion flows (275 TB) at a university uplink. **Fitted, reusable flow-size models plus open-source tooling**, directly usable as the size distribution in workload W4/S4. |
| Brownlee & claffy, *Understanding Internet Traffic Streams: Dragonflies and Tortoises*, IEEE Comm. Mag. 2002 | [CAIDA](https://catalog.caida.org/paper/2002_dragonflies) | About 45 % of streams last under 2 s, while long-lived streams (over 15 min) carry 50–60 % of bytes. The classic "mice vs elephants" picture. |
| Zhang, Breslau, Paxson, Shenker, *On the Characteristics and Origins of Internet Flow Rates*, SIGCOMM 2002 | [PDF](http://conferences.sigcomm.org/sigcomm/2002/papers/dsr.pdf) | What limits flow rates in practice: congestion and receive windows. Background for "flow control must not limit your experiments". |
| Qian et al., *TCP Revisited: A Fresh Look at TCP in the Wild*, IMC 2009 | [ACM](https://dl.acm.org/doi/10.1145/1644893.1644903) | Up to 15.8 % of flows used initial windows above the standard limit, an early sign that servers tune startup aggressively. |
| Trevisan et al., *Five Years at the Edge: Watching Internet from the ISP Network*, CoNEXT 2018 | [Project](https://smartdata.polito.it/five-years-at-the-edge/) | Five years of residential ISP traffic. Per-user downloads more than doubled, driven by video. Open dataset. |

### 2.2 Web object sizes (the sizes your file server should serve)

- **HTTP Archive Web Almanac 2025, Page Weight**
  ([link](https://almanac.httparchive.org/en/2025/page-weight)).
  - Median page: **2.9 MB desktop, 2.6 MB mobile**; 90th percentile: **9.2 MB and 8.3 MB**.
  - Median page has **77 requests on desktop and 72 on mobile**.
  - Median individual resource: image 8 KB, JavaScript about 4.7 KB, font 23 KB, video 247–384 KB.
  - So a page is many tiny objects plus a few large ones: exactly the express/bulk mix.

### 2.3 QUIC vs TCP adoption

| Source | Link | Key numbers |
|--------|------|-------------|
| ★ Langley et al., *The QUIC Transport Protocol: Design and Internet-Scale Deployment*, SIGCOMM 2017 | [PDF](https://dl.acm.org/doi/pdf/10.1145/3098822.3098842) | Google QUIC was an estimated **about 7 % of all Internet traffic** in 2017. The design sections explain the measurement advantages in §3 below. |
| ★ Rüth et al., *A First Look at QUIC in the Wild*, PAM 2018 | [arXiv](https://arxiv.org/abs/1801.05168) | QUIC traffic share by vantage point in 2017: **6.7 %** at a university uplink (MAWI), **7.8 %** at a European tier-1 ISP, **9.1 %** on mobile, **2.6 %** at a large IXP. At the ISP, 98 % of QUIC was Google's. |
| Zirngibl et al., *It's over 9000: Analyzing Early QUIC Deployments with the Standardization on the Horizon*, IMC 2021 | [ACM](https://doi.org/10.1145/3487552.3487826) | More than 2.3 M IPv4 and 300 k IPv6 addresses speak QUIC, hosting more than 30 M domains. |
| Trevisan et al., *Measuring HTTP/3: Adoption and Performance*, MedComNet 2021 | [arXiv](https://arxiv.org/abs/2102.12358) | HTTP/3 helps mainly at **high latency or poor bandwidth**, the same regime where your startup gains are largest. |
| ★ Cloudflare Radar, *2025 Year in Review* | [blog](https://blog.cloudflare.com/radar-2025-year-in-review/) · [report](https://radar.cloudflare.com/year-in-review/2025) | Share of requests in 2025: **HTTP/2 50 %, HTTP/1.x 29 %, HTTP/3 21 %**, almost unchanged from 2024. 15 countries send over a third of requests via HTTP/3. |
| Mücke et al., *Waiting for QUIC: Passive Measurements to Understand QUIC Deployments*, CoNEXT 2025 (PACMNET) | [ACM](https://doi.org/10.1145/3768988) | Hypergiants (Google, Meta, Cloudflare, Akamai, …) configure QUIC very differently: initial RTO from 0.1 s to 1 s, mean retransmissions from 1.3 to 7.5. "QUIC" in the wild is several distinct stacks. |
| ★ Mishra et al., *The Great Internet TCP Congestion Control Census*, SIGMETRICS 2020 | [ACM](https://dl.acm.org/doi/10.1145/3393691.3394221) | CUBIC runs on about 36 % of the top 20 k websites; BBR on about 22 % by site count but **over 40 % of traffic volume**. These are the competitors your algorithm must coexist with. |

### 2.4 What these numbers mean for the thesis

1. **Most flows are short, most bytes are long.** That is the express/bulk split in both plans, and it is
   why SRPT-like treatment helps short flows while costing large ones little.
2. **Web pages are dozens of objects of a few KB plus a few MB-scale objects.** Your 100 KB–50 MB range
   covers the "medium" objects where slow start costs the most RTTs; consider adding a few smaller sizes
   (10–50 KB) to show where gains vanish.
3. **QUIC carries roughly a fifth of web requests** (Cloudflare 2025) and a large share of hypergiant traffic.
   A CC change in a handful of QUIC stacks therefore reaches a big part of the Internet, which is a strong
   motivation sentence for the introduction.
4. **Competitors are CUBIC and BBR**, so both must be in your baselines and harm experiments.
5. **ECN is rarely usable on today's paths** (IMC 2023), so evaluate the no-ECN fallback as a main result.

---

## 3. QUIC vs TCP: what the congestion controller can measure during a flow

QUIC was designed with loss recovery and congestion control in mind (RFC 9002; Langley et al. 2017), and it
runs in the same user-space process as the application. Both facts give a QUIC congestion controller more
and cleaner signals than kernel TCP has. The table lists them and how Sprint (either plan) can use each one.

| Signal | QUIC | TCP | How the CC can use it |
|--------|------|-----|-----------------------|
| **RTT samples** | Every packet number is used once; retransmitted data goes in a new packet. Every ACK gives an unambiguous RTT sample, including during loss recovery (RFC 9002 §5). | Retransmissions reuse sequence numbers, so samples are ambiguous (Karn's rule) unless the timestamps option is used. | Clean per-ACK RTT and queueing-delay estimates during startup, exactly when the delay backstop and plateau detection need them. |
| **Receiver's own delay** | ACK frames carry an explicit **ACK Delay** field (RFC 9000 §19.3), and RFC 9002 subtracts it from RTT samples. | Delayed-ACK time is invisible and inflates RTT. | Separates network delay from receiver delay, giving a sharper min-RTT and queue-buildup signal for the exit conditions. |
| **ECN feedback** | Exact cumulative **ECT(0), ECT(1) and CE counts** in every ACK, per packet-number space, with built-in path validation (RFC 9000 §13.4). | Classic ECN gives one ECE flag per RTT (RFC 3168); counts need the separate AccECN extension. | The CE fraction per round enables the proportional (DCTCP-style) reaction and the CE-based exits (E1–E3 in the plans). |
| **Which packets were marked** | With the accurate-ACK-ECN draft, the ECN codepoint is reported per ACK range ([draft](https://www.ietf.org/archive/id/draft-seemann-quic-accurate-ack-ecn-01.html)). | Not available. | Attribute CE marks to the slow half or the fast half of a probe round, and to express or bulk packets (plan B E5). |
| **Delivered data** | Effectively unlimited ACK ranges, and ACKs are **irrevocable** (RFC 9000 §13). | 3–4 SACK blocks; SACK may be reneged. | Exact delivered-bytes and delivery-rate samples per packet (BBR-style), the core of the Nimble-style rate estimate. |
| **Spurious-loss detection** | An ACK for a packet already declared lost is unambiguous. | Needs heuristics (F-RTO, DSACK, Eifel). | Measure path reordering, undo wrong cuts, and adapt the loss threshold (important for option B's dual-codepoint idea). |
| **ACK rate under sender control** | The ACK-frequency extension lets the sender ask for more or fewer ACKs, or an immediate ACK ([draft-ietf-quic-ack-frequency](https://datatracker.ietf.org/doc/html/draft-ietf-quic-ack-frequency)). | Fixed by the receiver's delayed-ACK policy. | Dense feedback during startup probing, sparse ACKs (less CPU) for bulk. |
| **Receive timestamps** | The receive-timestamps extension reports per-packet arrival times in ACKs ([draft-ietf-quic-receive-ts](https://datatracker.ietf.org/doc/draft-ietf-quic-receive-ts/)); picoquic has a timestamp extension. | The timestamps option gives one receiver clock value per ACK, typically at ms granularity. | Receiver-side packet dispersion and one-way-delay trends: the ROS measurement without a new frame. |
| **ACKs are encrypted** | Middleboxes cannot read, thin or rewrite ACKs selectively. | ACK filtering and thinning by middleboxes (e.g. on cable and cellular links) distorts the ACK clock. | ACK-based estimates are not corrupted by middleboxes. Link-layer aggregation still happens, which is why ROS-style receiver measurement remains useful. |
| **Pure probes** | A PING + PADDING packet is ack-eliciting and counts as in flight without carrying application data (RFC 9002 §2). | Every probe must carry data (or abuse keep-alives). | Probe for bandwidth even when the application has little to send, e.g. to keep the rate estimate fresh for a warm start. |
| **Application knowledge** | The CC lives in the same process as the application. It knows stream sizes, whether the sender is app-limited, and when flow control blocks (`DATA_BLOCKED`, `STREAM_DATA_BLOCKED`). | Kernel TCP sees a byte stream: no message sizes, coarse app-limited detection. | Flow-size hints and stream classes (both plans), and rejecting app-limited or flow-control-limited samples from the rate estimate. |
| **New signals can be added** | New frames and transport parameters are negotiated end-to-end, and encryption keeps middleboxes from stripping them. | New TCP options are often stripped or blocked by middleboxes. | Option A's `PROBE` / `RATE_REPORT` frames are deployable in principle. |
| **Path changes are explicit** | Connection migration with path validation (RFC 9000 §9) tells the endpoint the path changed, and RFC 9000 requires a CC reset. | NAT rebinding or a handover usually just breaks the connection, or silently changes the path. | The CC never mixes samples from two paths. |
| **State across connections** | Session tickets, `NEW_TOKEN` and 0-RTT give a natural carrier for cached path state (Careful Resume, RFC 9959). | No protocol carrier for path state: TCP Fast Open cookies only, plus host-local caching (RFC 9040). | Warm start of the express part from a previous connection. |
| **Instrumentation** | Standardized structured event logs (qlog) and visualizers (qvis). | Packet captures plus inference (tcptrace) or kernel tracing. | Exact CC state over time for debugging and for the thesis figures. |

**Caveats to state alongside these advantages:**
- Richer ACKs cost CPU in user space (Zhang et al., WWW 2024).
- The timestamp and accurate-ECN extensions are drafts and are not widely deployed.
- ECN counts are only useful where paths actually mark (Sander et al., IMC 2023).
- The ACK Delay field is self-reported by the receiver, so RFC 9002 bounds how much it may reduce an RTT
  sample, as a defence against a lying peer.

**Specs to keep open while reading:** [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000) (transport),
[RFC 9002](https://www.rfc-editor.org/rfc/rfc9002) (loss detection and CC),
[RFC 9959](https://datatracker.ietf.org/doc/rfc9959/) (Careful Resume).

---

## 4. Congestion control that relies on QUIC-only features

Searched: October 2026 (web search, about 20 queries). **Question**: which papers build congestion control
on something only QUIC has, such as extension frames, 0-RTT and resumption state, connection migration,
per-path packet number spaces, the ACK-frequency extension, or a CC that sits next to the application? The
entry letters refer to [TCP_vs_QUIC.md](TCP_vs_QUIC.md).

**Verdict.**
- **Few papers do this.** Most "congestion control for QUIC" papers port a TCP algorithm (CUBIC, BBR,
  Westwood+, learning-based) into a QUIC stack and use nothing QUIC-specific; those are listed in §4.3.
- **None found for short-flow startup.** No paper uses QUIC's ECN counts, ACK Delay field, ACK-frequency
  extension or stream knowledge to speed up the startup of short flows.
- **The closest work** is Wira, which uses first-frame size plus client-carried history, and BDP_FRAME /
  Careful Resume, which use resumption state.
- **For the thesis**, this supports the novelty claim in [Related_Papers.md](Related_Papers.md). Re-check
  before submission.
- **Limits**: venue abstracts were checked, but full texts were not read unless noted. "Not found" does not
  mean "does not exist".

### 4.1 Papers whose CC uses a QUIC-only mechanism

| | Paper | Link | QUIC-only feature used | Why it matters here |
|---|-------|------|------------------------|---------------------|
| ★ | Wu et al., *Reducing First-Frame Delay of Live Streaming by Simultaneously Initializing Window and Rate* (Wira), ICDCS 2024 | [DOI](https://doi.org/10.1109/ICDCS60910.2024.00030) | Built on QUIC, CC co-located with the application (E2). "Frame Perception" parses the first video frame and sizes the initial cwnd to it; a "Transport Cookie" lets the client carry historical network metrics that set the initial rate, with no server-side state (A5). How the cookie travels on the wire is not stated in the abstract. | **Closest prior work to Sprint's size hint and warm start.** Initial window from known object size, plus client-carried path history. Reported FFCT −10.6 % mean and −16.7 % p90 in a commercial deployment. Must cite and contrast: Wira sets the *initial* window and rate once, while Sprint probes over rounds. |
| ★ | Kuhn, Simo, Pradas, Stephan, *Evaluating BDP_FRAME extension for QUIC*, arXiv tech report 2021 | [arXiv](https://arxiv.org/abs/2112.05450) | A new QUIC extension frame carries saved cwnd/RTT to the client, which returns it on 0-RTT resumption (A1, A5, F1). picoquic, emulated and live SATCOM. | The direct ancestor of RFC 9959 in QUIC. On SATCOM, 0-RTT alone cut transfer time by 14 % and 0-RTT + BDP_FRAME by 58 %. An example of using QUIC's extension mechanism for CC state (the draft itself has expired, see TCP_vs_QUIC §I). |
| ★ | Michel, De Coninck, Bonaventure, *QUIC-FEC: Bringing the benefits of Forward Erasure Correction to QUIC*, IFIP Networking 2019 | [arXiv](https://arxiv.org/abs/1904.11326) | Extension FEC frames (XOR, Reed-Solomon, RLC). The CC tells packets *received* apart from packets *recovered by FEC*, so FEC does not mask congestion signals; this works cleanly because ACKs are per packet number (B1, D5). | **Short-flow FCT under loss.** FEC hurts long or low-loss transfers, but for small files on lossy, long-delay paths it "drastically" cuts completion time by avoiding timeouts. Evidence for the tail-redundancy idea (TCP_vs_QUIC D5, §H item 1) and for how CC should treat redundant packets. |
| | De Coninck, Bonaventure, *Multipath QUIC: Design and Evaluation*, CoNEXT 2017 | [PDF](https://inl.info.ucl.ac.be/system/files/conext17-deconinck.pdf) · [project](https://multipath-quic.org/conext17.html) | Per-path packet number spaces and unambiguous per-path ACKs; OLIA coupled CC; built on quic-go (E4). | Coupled CC across paths of one QUIC connection is the template for "dual lane inside one connection" and for lane coupling (plan §3.8). In lossy scenarios MPQUIC beat MPTCP. Background for draft-ietf-quic-multipath (now in the RFC Editor queue). |
| | Custura, Jones, Secchi, Fairhurst, *Reducing the acknowledgement frequency in IETF QUIC*, Int. J. Satellite Comm. and Networking 41(4), 2023 | [PDF](https://abdn.pure.elsevier.com/files/220217779/Castura_etal_IJSCN_Reducing_the_Acknowledgement_VOR.pdf) · DOI 10.1002/sat.1466 | ACK-frequency extension (B6): an ACK policy that adapts the ACK delay to the path RTT. Tested on three QUIC stacks over cellular, terrestrial and satellite paths. | Not a CCA, but the main evidence that QUIC performance holds with fewer ACKs. Use it to justify the "dense ACKs in startup, sparse ACKs in bulk" policy (TCP_vs_QUIC §H item 2) and the W7 ACK-thinning sweep. |
| | Engelbart, Ott, *Congestion Control for Real-time Media over QUIC*, EPIQ (CoNEXT workshop) 2021 | [DOI](https://doi.org/10.1145/3488660.3493801) · [code](https://github.com/mengelbart/rtp-over-quic-mininet) | Media CC (SCReAM) running inside QUIC, with variants that take feedback from QUIC's own connection statistics (ACKs) instead of RTCP, and variants with QUIC's NewReno on or off. Details are from the repository; the paper text was not accessible. | Shows the "CC next to the application, fed by QUIC ACKs" pattern (E2), and the problem of **nesting** an application rate controller inside QUIC's CC, which matters if Sprint's rate logic sits on top of quinn's controller. Real-time media, so otherwise out of scope. |
| | Kim, Koh, *Adaptive Control of Congestion Window in QUIC*, ICTC 2023 (3 pages) | [DOI](https://doi.org/10.1109/ICTC58733.2023.10393389) | Connection migration (E3): adapts cwnd after a client IP change instead of the reset that RFC 9000 §9.4 requires. | Low priority. Cite only if you discuss Sprint on mobile express lanes (Uses_Cases A1/A2). Short paper, little detail. |
| | Zheng et al., *XLINK: QoE-driven multi-path QUIC transport in large-scale video services*, SIGCOMM 2021 | [PDF](https://conferences.sigcomm.org/sigcomm/2021/files/papers/3452296.3472893.pdf) | The client attaches player QoE (buffered frames, frame rate) to its acknowledgements. The server uses it to control re-injection on multipath QUIC (E4, F1). | Scheduling and redundancy rather than CC, but it is the deployed precedent for **receiver → sender application feedback in QUIC**, the same pattern as Sprint's RATE_REPORT. Results: p99 chunk request completion time −19 to −50 % vs single-path QUIC, with 2.1 % redundant traffic. |

Already in this repo, and also QUIC-specific (not repeated): **PCC Priority** (2023, stream priorities in the
CC utility) and **CATS** (2026, per-class budget inside one connection) in [Related_Papers.md](Related_Papers.md);
**Careful Resume in neqo and picoquic** (§1.1, session resumption state); **hStart** (§1.1).

### 4.2 Frameworks and drafts (not CC algorithms, but enablers)

| Work | Link | Relevance |
|------|------|-----------|
| De Coninck et al., *Pluginizing QUIC* (PQUIC), SIGCOMM 2019 | [PDF](https://inl.info.ucl.ac.be/system/files/pquic.pdf) | Endpoints exchange verified protocol plugins per connection (examples in the paper: monitoring, multipath, FEC). A way to ship receiver-side logic (Sprint v1) without a client update; a CC plugin was not confirmed in the abstract. |
| Huitema, *QUIC Timestamps For Measuring One-Way Delays*, draft-huitema-quic-ts-08 (expired 2022) | [draft](https://datatracker.ietf.org/doc/html/draft-huitema-quic-ts-08) | TIMESTAMP frame for one-way delay; motivated by LEDBAT and delay-based CC suffering from reverse-path queues and delayed ACKs. The precursor to draft-ietf-quic-receive-ts (TCP_vs_QUIC B7). No evaluation paper found. |
| *Congestion Data Frame*, draft-yuan-quic-congestion-data (2026) | [datatracker](https://datatracker.ietf.org/doc/draft-yuan-quic-congestion-data/) | Exchanging CC state in a QUIC frame (found in search, not read). Check it against BDP_FRAME and RFC 9959 before writing the warm-start section. |
| Zhang et al., *PEMI: Transparent Performance Enhancements for QUIC*, arXiv 2026 | [arXiv](https://arxiv.org/abs/2602.12732) | An in-network helper for encrypted QUIC (up to 2.5× goodput on lossy links in Mininet). The counterpoint to "QUIC rules out PEPs" (TCP_vs_QUIC F2, Uses_Cases "Where both help most"). |

### 4.3 Checked and excluded: implemented in QUIC, but nothing QUIC-specific

Useful as baselines or related work, not as "QUIC-only" examples:
- Mascolo et al., *QUIC Delay Control* (arXiv 2025, [link](https://arxiv.org/abs/2507.00896)): one-way delay
  variation plus Westwood+ in mvfst; no QUIC-specific signal described.
- Sidhu, Bentaleb, *AERO: towards a pluggable, implementation-agnostic learning-based CC for QUIC*
  ([DOI](https://doi.org/10.1145/3827616)): portability across QUIC stacks, video QoE.
- *Congestion Control System Optimization with Large Language Models* (arXiv 2025,
  [link](https://arxiv.org/abs/2508.16074)): LLM-tuned BBR in a production QUIC stack.
- SEARCH in QUIC (LANMAN 2024) and Blitz-starting QUIC (arXiv 2019): already in §1.1.
