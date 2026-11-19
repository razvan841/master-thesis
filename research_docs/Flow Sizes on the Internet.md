# Flow Sizes on the Internet

Nov 5, 2026 · @Razvan

Most flows are tiny, and very few flows carry most bytes. On one campus link, 92% of TCP flows were at most 16 KiB and carried 1.2% of TCP bytes.

## Main findings

- **Most flows are small.** About 85% of TCP flows on Tier-1 backbone links carry 100 B to 10 kB ([Bauer et al.](https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/bauer_anrw21.pdf)). At Cloudflare in October 2025, the median connection sent 4.8 KB on HTTP/1.x and 6 KB on HTTP/2 ([Cloudflare](https://blog.cloudflare.com/measuring-network-connections-at-scale/)).
- **Very few flows carry most bytes.** The largest 1% of TCP flows carry 88–89% of TCP bytes on those backbone links. On the campus link, flows above 1 MiB are 0.66% of TCP flows and 91% of TCP bytes ([Jurkiewicz et al.](https://arxiv.org/abs/1809.03486)).
- **The shape has barely changed.** Over 84% of flows were under 100 KB in every 2002 trace ([Zhang et al.](http://conferences.sigcomm.org/sigcomm/2002/papers/dsr.pdf)). Bauer et al. saw no significant trend in the size CDF between 2008 and 2016. The 99th percentile moved from about 250 kB to about 400 kB.
- **Many connections are smaller than the pipe.** Cloudflare's median BDP is about 77 KB. The 90th-percentile HTTP/1.x connection sends 51 KB, and the HTTP/2 one sends 146 KB. Setting these separate statistics side by side is my inference, not Cloudflare's claim.
- **The definition of a flow changes the numbers.** Single-packet flows are 48% of all campus flows but 27% of TCP flows. The studies below count different things, so their numbers are not directly comparable.
- **QUIC is the gap.** I found no published distribution of bytes per QUIC connection. That does not prove none exists.

## Wide-area studies

Six studies give usable numbers, and only Cloudflare's data is newer than 2019. Brownlee & claffy and Zhang et al. are the older classics already in your notes; Cloudflare and Lan & Heidemann are new to them.

| Study | Vantage and data | What counts as a flow | Key numbers |
| --- | --- | --- | --- |
| [Cloudflare 2025](https://blog.cloudflare.com/measuring-network-connections-at-scale/) (blog, not peer reviewed) | Global CDN, 1% sample of TCP connections, 7–15 Oct 2025 | One TCP connection closed by FIN, with at least one HTTP request; bytes sent by the server, including retransmissions | Median 4.8 KB (HTTP/1.x), 6 KB (HTTP/2). 90th percentile 51 KB, 146 KB. Mean 224 KB, 390 KB. Median 12 packets |
| [Bauer et al. 2021](https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/bauer_anrw21.pdf) | CAIDA Tier-1 backbone links: Chicago 2008–2016, New York 2018–2019; 2.6 billion TCP flows; one-hour captures | Bidirectional 5-tuple, payload bytes; starts with a TCP handshake; flows under 100 ms removed | About 85% carry 100 B–10 kB. Chicago 99th percentile about 250–400 kB; New York nearly ten times smaller. Top 1% carry 89.2% (Chicago), 88.4% (New York) of bytes |
| [Jurkiewicz et al. 2021](https://arxiv.org/abs/1809.03486) | AGH university uplink, 30 days from June 2015; 4.03 billion flows, 275 TB; no sampling; dormitories produced 69% of bytes | Unidirectional 5-tuple, 15 s inactive timeout, split records merged | Mean size 68 KB (all), 113 KB (TCP), 18 KB (UDP). TCP is 54% of flows, 89% of bytes |
| [Lan & Heidemann 2006](https://ant.isi.edu/~johnh/PAPERS/Lan06a.pdf) | Los Nettos regional network; NLANR Abilene backbone link | Unidirectional 5-tuple, 60 s timeout; elephants set by a size threshold | Elephants: 1% of flows, 71% of bytes (Los Nettos); 4% of flows, 82% of bytes (NLANR). Size scaling exponent 1.2 |
| [Brownlee & claffy 2002](https://www-old.caida.org/publications/papers/2002/Dragonflies/cnit.pdf) | UC San Diego and Auckland links, 2002 | Streams, classed by lifetime | 40–70% last under 2 s. About 1.5% last over 15 min and carry 5–50% of bytes. Web streams: flat from 1 to 40 kB, then a power-law fall |
| [Zhang et al. 2002](http://conferences.sigcomm.org/sigcomm/2002/papers/dsr.pdf) | 8 packet traces plus 76 summary flow data sets | 5-tuple, 60 s timeout; classed at 100 KB and 10 KB/s | Over 84% of flows under 100 KB in all traces. Big and fast flows: generally under 10% of flows, at least 60% of bytes |

The New York traces also show that 35% of TCP flows last under 100 ms, against 20% in Chicago. Those filtered flows carry only 0.04–0.06% of bytes, but they matter for a short-flow thesis, because Bauer's size figures exclude them.

## Distribution shape

&#91;embedded content: Jurkiewicz et al., Table 9, TCP-only flows. IW10 and the Cloudflare median BDP are added for scale and come from other sources.\]

The body looks lognormal and the tail is heavy, but no single simple distribution fits the whole range.

- **Best reusable model:** Jurkiewicz et al. fit a mixture: uniform up to 6 packets, lognormal above. No single distribution fit their data. Parameters are in their appendix and [code repository](https://github.com/piotrjurkiewicz/flow-models).
- **Tail steepness:** Crovella & Bestavros found Web transfers above about 10 KB heavy-tailed, with a tail index of about 1 to 1.2 ([paper](https://cs-web.bu.edu/fac/best/res/papers/ton97.pdf)). Lan & Heidemann report a flow-size exponent of 1.2.
- **Pareto or lognormal:** Downey argues that lognormal and similar models usually suffice, and that a sample can look long-tailed without being so ([paper](https://allendowney.com/research/longtail/downey04lognormal.pdf)). Say "heavy-tailed" in the thesis, and avoid claiming an exact Pareto law.
- **Your test range:** the 128 KiB to 32 MiB band, close to your 100 KB–50 MB range, holds 2.3% of TCP flows and 33% of TCP bytes on this link.

## Web objects and pages

A typical page is dozens of small objects plus a few large ones: 72–77 requests and 2.2–2.4 MB at the median ([Web Almanac 2025](https://almanac.httparchive.org/en/2025/page-weight)).

| Measure | Desktop median | Mobile median | Desktop 90th percentile | Mobile 90th percentile |
| --- | --- | --- | --- | --- |
| Page weight, all pages | 2,412 KB | 2,164 KB | 9,179 KB | 8,337 KB |
| Page weight, home pages | 2,862 KB | 2,559 KB |  |  |
| Requests per page | 77 | 72 | 185 | 179 |
| Single image file | 8 KB | 8 KB | 183 KB | 186 KB |
| Single JavaScript file | 4.6 KB | 4.7 KB | 83.5 KB | 80.1 KB |
| Font response | 23 KB | 23 KB | 79 KB | 80 KB |
| Video file | 247 KB | 384 KB | 3,904 KB | 4,799 KB |

- **A median image or script fits well inside IW10.** Even the 90th-percentile script is only about 80 KB.
- **Objects are not flows.** A page's requests share connections. At Cloudflare, the median connection carries 1 request, but the mean is about 8 on HTTP/2.
- **Correction for your notes:** Reading\_List.md line 97 gives 2.9 MB and 2.6 MB as the median page. Those are home pages; all pages are 2.4 MB and 2.2 MB.
- **Source quality:** the chapter is not fully consistent. It calls 23 KB a per-page font figure in its text but a per-response figure in Figure 14.32, and gives median mobile requests as both 72 and 68.

## QUIC and datacenters

QUIC has no public size distribution that I could find, while datacenters show the same skew at a different scale.

**QUIC.** None of these sources gives bytes per QUIC connection:

- [Cloudflare](https://blog.cloudflare.com/measuring-network-connections-at-scale/) measures TCP only, which carries about 70% of its HTTP requests.
- [Rüth et al.](https://arxiv.org/abs/1801.05168) report QUIC's traffic share, not its flow sizes.
- [Jurkiewicz et al.](https://arxiv.org/abs/1809.03486) report UDP flows with a mean of 18 KB, and 72% have one packet. That data is from 2015, so it says little about QUIC.
- [Zhang & Su](https://arxiv.org/abs/2310.10676) describe a way to infer HTTP sizes from encrypted QUIC packets. It is a method, and it could produce such a distribution.

**Datacenters.**

| Study | Setting | Key numbers |
| --- | --- | --- |
| [Roy et al. 2015](https://conferences.sigcomm.org/sigcomm/2015/pdf/papers/p123.pdf) | Facebook; one Hadoop node over 10 minutes | 70% of flows under 10 KB, median under 1 KB, under 5% over 1 MB. The authors warn against over-reading this one distribution |
| [Benson et al. 2010](https://www.cl.cam.ac.uk/teaching/2122/L50/papers/paper7.pdf) | 10 datacenters: university, enterprise, cloud | 80% of flows under 10 KB. Most bytes are in the largest 10% of flows |
| [Alizadeh et al. 2010](https://people.csail.mit.edu/alizadeh/papers/dctcp-sigcomm10.pdf) | Production web-search cluster | Queries 2–20 KB, short messages 100 KB–1 MB, long flows 1–100 MB |

Roy et al. add a warning: over sub-second intervals, Facebook's heavy hitters are not much larger than the median flow, and the set changes quickly. So the skew depends on the time scale.

## Limits of the evidence

The direction of these findings is solid, but the exact percentages depend on how each study was run.

- **Flow definitions differ.** Jurkiewicz et al. count each direction separately, so a request and its response are two flows. Bauer et al. count both directions together, and Cloudflare counts only bytes sent by the server.
- **Not every flow is a transfer.** Flows of one or two packets are 65% of all campus flows, which the authors attribute to port scans and DNS. Brownlee & claffy read their smallest web streams as failed TCP setups.
- **Filtering removes short flows.** Bauer et al. drop flows under 100 ms. Cloudflare drops connections that time out, reset, or are mitigated as attacks.
- **Capture length hides long flows.** Bauer et al. use one-hour captures. Lan & Heidemann note that their short NLANR trace is biased against long-lived flows.
- **Vantage points differ.** The campus link is a single network where dormitories produce 69% of bytes. New York's 99th percentile is nearly ten times below Chicago's, so a single link is not the Internet.
- **The public data is old.** The latest academic flow data here is from January 2019, and the campus data is from 2015. Both predate QUIC's current share. Check for newer studies before submission.

## Sources

I read the papers below as text or PDF, searching some of the longer ones for their measurement sections rather than reading them whole. I took the table values from the Jurkiewicz PDF, and pulled the Cloudflare and Web Almanac numbers through a summarising page fetch.

- [Bauer et al., On the Evolution of Internet Flow Characteristics, ANRW 2021](https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/bauer_anrw21.pdf)
- [Jurkiewicz et al., Flow length and size distributions in campus Internet traffic, Computer Communications 2021](https://arxiv.org/abs/1809.03486)
- [Ahmad & Wu, Measuring characteristics of TCP connections at Internet scale, Cloudflare 2025](https://blog.cloudflare.com/measuring-network-connections-at-scale/)
- [Zhang et al., On the Characteristics and Origins of Internet Flow Rates, SIGCOMM 2002](http://conferences.sigcomm.org/sigcomm/2002/papers/dsr.pdf)
- [Brownlee & claffy, Understanding Internet Traffic Streams, IEEE Communications Magazine 2002](https://www-old.caida.org/publications/papers/2002/Dragonflies/cnit.pdf)
- [Lan & Heidemann, A measurement study of correlations of Internet flow characteristics, Computer Networks 2006](https://ant.isi.edu/~johnh/PAPERS/Lan06a.pdf)
- [Crovella & Bestavros, Self-similarity in World Wide Web traffic, IEEE/ACM ToN 1997](https://cs-web.bu.edu/fac/best/res/papers/ton97.pdf)
- [Downey, Lognormal and Pareto Distributions in the Internet](https://allendowney.com/research/longtail/downey04lognormal.pdf)
- [Web Almanac 2025, Page Weight](https://almanac.httparchive.org/en/2025/page-weight)
- [Roy et al., Inside the Social Network's (Datacenter) Network, SIGCOMM 2015](https://conferences.sigcomm.org/sigcomm/2015/pdf/papers/p123.pdf)
- [Benson et al., Network Traffic Characteristics of Data Centers in the Wild, IMC 2010](https://www.cl.cam.ac.uk/teaching/2122/L50/papers/paper7.pdf)
- [Alizadeh et al., DCTCP, SIGCOMM 2010](https://people.csail.mit.edu/alizadeh/papers/dctcp-sigcomm10.pdf)
- [Rüth et al., A First Look at QUIC in the Wild, PAM 2018](https://arxiv.org/abs/1801.05168)
- [Zhang & Su, Application-layer Characterization and Traffic Analysis for Encrypted QUIC Transport Protocol, arXiv 2023](https://arxiv.org/abs/2310.10676)
