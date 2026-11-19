# Related Work: Paper Links

Nov 5, 2026 · @Razvan

Every paper cited in the Related Work chapter has a link here, except Nimble Start and ROS, which are unpublished. Agree with your supervisor how to cite those two.

- **DOI links:** nearly all were matched to the right title through Crossref. The check did not load the publisher page, because ACM and IEEE often block automated requests.
- **arXiv links:** each ID was matched to the right title on arXiv.
- **PDF links:** each one loaded as a PDF, but the files were not opened to confirm the title.
- **Access:** DOI-only entries, especially IEEE and Elsevier ones, may need a university login.

## Congestion control in TCP

| Subsection | Paper | Open copy | Publisher or record |
| --- | --- | --- | --- |
| Origins | Jacobson 1988, Congestion Avoidance and Control | [PDF](https://cs162.org/static/readings/jacobson-congestion.pdf) | [DOI](https://doi.org/10.1145/52324.52356) |
| Loss-based | Ha et al. 2008, CUBIC | [PDF](https://www.cs.princeton.edu/courses/archive/fall14/cos561/papers/Cubic08.pdf) | [DOI](https://doi.org/10.1145/1400097.1400105) |
| Loss-based, model-based | Cardwell et al. 2016, BBR |  | [Queue DOI](https://doi.org/10.1145/3012426.3022184) · [CACM DOI](https://doi.org/10.1145/3009824) |
| Loss-based | Gettys & Nichols 2012, Bufferbloat | [PDF](https://web.mit.edu/6.033/2015/wwwdocs/papers/gettys.pdf) | [DOI](https://doi.org/10.1145/2063176.2063196) |
| Loss-based | Appenzeller et al. 2004, Sizing Router Buffers | [PDF](https://webee.technion.ac.il/~isaac/p/sigcomm04_sizing.pdf) | [DOI](https://doi.org/10.1145/1015467.1015499) |
| Loss-based | Flach et al. 2013, Reducing Web Latency | [PDF](https://conferences.sigcomm.org/sigcomm/2013/papers/sigcomm/p159.pdf) | [DOI](https://doi.org/10.1145/2486001.2486014) |
| Initial window | Dukkipati et al. 2010 |  | [DOI](https://doi.org/10.1145/1823844.1823848) |
| Initial window | Rüth et al. 2017, Large-Scale Scanning of TCP's Initial Window | [PDF](https://conferences.sigcomm.org/imc/2017/papers/imc17-final43.pdf) | [DOI](https://doi.org/10.1145/3131365.3131370) |
| Flow sizes | Brownlee & claffy 2002, Dragonflies and Tortoises | [CAIDA](https://catalog.caida.org/paper/2002_dragonflies) | [DOI](https://doi.org/10.1109/MCOM.2002.1039865) |
| Flow sizes | Bauer et al. 2021 | [PDF](https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/bauer_anrw21.pdf) | [DOI](https://doi.org/10.1145/3472305.3472321) |
| Flow sizes | Jurkiewicz et al. 2018 | [arXiv](https://arxiv.org/abs/1809.03486) |  |
| Web objects | HTTP Archive Web Almanac 2025, Page Weight | [Web](https://almanac.httparchive.org/en/2025/page-weight) |  |
| Better exit | Ha & Rhee 2011, HyStart |  | [DOI](https://doi.org/10.1016/j.comnet.2011.01.014) |
| Better exit | Kachooei et al. 2023, SEARCH | [PDF](https://web.cs.wpi.edu/~claypool/papers/search-lcn-23/paper.pdf) |  |
| Faster growth | Arghavani et al. 2024, SUSS | [PDF](https://www-cs.stanford.edu/~keithw/sigcomm2024/sigcomm24-final280-acmpaginated.pdf) | [DOI](https://doi.org/10.1145/3651890.3672234) |
| Probe, then jump | Liu et al. 2007, Jump Start | [PDF](https://mallman.net/pubs/LAJW07/LAJW07.pdf) |  |
| Probe, then jump | Anderson et al. 2006, PCP | [PDF](https://www.usenix.org/legacy/events/nsdi06/tech/full_papers/anderson/anderson.pdf) |  |
| Probe, then jump | Misund & Briscoe 2019, Paced Chirping | [Related netdev paper](https://bobbriscoe.net/projects/latency/paced_chirping_netdev0x13.pdf) | [ResearchGate](https://www.researchgate.net/publication/335994999_Paced_Chirping_Rapid_flow_start_with_very_low_queuing_delay) |
| Learning-based | Jay et al. 2019, Aurora | [PMLR](https://proceedings.mlr.press/v97/jay19a.html) |  |
| Network-assisted | Sander et al. 2023, ECN with QUIC | [arXiv](https://arxiv.org/abs/2309.14273) | [DOI](https://doi.org/10.1145/3618257.3624821) |
| Sharing CC state | Balakrishnan et al. 1999, Congestion Manager |  | [DOI](https://doi.org/10.1145/316188.316220) |
| Sharing CC state | Islam et al. 2018, ctrlTCP |  | [DOI](https://doi.org/10.1109/INFCOMW.2018.8406887) |
| Sharing CC state | Singh et al. 2004, MPAT | [PDF](https://www.ieee-icnp.org/2004/papers/3-4.pdf) |  |
| Scavengers | Kuzmanovic & Knightly, TCP-LP |  | [Journal version, 2006](https://doi.org/10.1109/TNET.2006.879702) |
| Scavengers | Venkataramani et al. 2002, TCP Nice | [USENIX](https://www.usenix.org/legacy/events/osdi02/tech/venkataramani.html) |  |
| Size-aware | L2DCT 2013 | [PDF](https://web.lums.edu.pk/~ihsan/papers/l2dct-infocom2013.pdf) |  |
| Size-aware | Alizadeh et al. 2013, pFabric | [PDF](https://people.csail.mit.edu/alizadeh/papers/pfabric-sigcomm13.pdf) | [DOI](https://doi.org/10.1145/2486001.2486031) |
| Size-aware | Bai et al. 2015, PIAS | [PDF](https://www.cse.ust.hk/~kaichen/papers/pias-nsdi15.pdf) |  |

Not linked: Nimble Start and ROS (receiver-informed probing), both unpublished.

## QUIC

| Subsection | Paper | Open copy | Publisher or record |
| --- | --- | --- | --- |
| Origin | Langley et al. 2017 |  | [DOI](https://doi.org/10.1145/3098822.3098842) |
| Deployment | Cloudflare Radar 2025 | [Blog](https://blog.cloudflare.com/radar-2025-year-in-review/) |  |
| TCP CC in QUIC | Mishra et al. 2022, Speciation |  | [DOI](https://doi.org/10.1145/3517745.3561459) |
| TCP CC in QUIC | Mishra & Leong 2023, Cambrian Explosion | [PDF](https://www.comp.nus.edu.sg/~bleong/publications/imc2023-quic.pdf) | [DOI](https://doi.org/10.1145/3618257.3624811) |
| TCP CC in QUIC | Marx et al. 2020, Same Standards, Different Decisions |  | [DOI](https://doi.org/10.1145/3405796.3405828) |
| TCP CC in QUIC | Kakhki et al. 2017, Taking a Long Look at QUIC | [PDF](https://mislove.org/publications/QUIC-IMC.pdf) | [DOI](https://doi.org/10.1145/3131365.3131368) |
| TCP CC in QUIC | Kempf et al. 2025, QUIC Steps | [arXiv](https://arxiv.org/abs/2505.09222) |  |
| TCP CC in QUIC | Mücke et al. 2025, Waiting for QUIC |  | [DOI](https://doi.org/10.1145/3768988) |
| TCP CC in QUIC | Sidhu & Bentaleb, Video Streaming Over QUIC | [arXiv](https://arxiv.org/abs/2505.21769) | [DOI](https://doi.org/10.1145/3793674) |
| Research using TCP-era signals | SEARCH in QUIC, LANMAN 2024 | [PDF](https://web.cs.wpi.edu/~claypool/papers/quic-search-lanman-24/paper.pdf) |  |
| Research using TCP-era signals | Rüth et al. 2019, Blitz-starting QUIC | [arXiv](https://arxiv.org/abs/1905.03144) |  |
| Research using TCP-era signals | Mascolo et al. 2025, QUIC-DC | [arXiv](https://arxiv.org/abs/2507.00896) |  |
| Research using TCP-era signals | Sidhu & Bentaleb, AERO |  | [DOI](https://doi.org/10.1145/3827616) |
| Research using TCP-era signals | LLM-tuned BBR 2025 | [arXiv](https://arxiv.org/abs/2508.16074) |  |
| QUIC-specific CC | Kuhn et al. 2021, BDP\_FRAME | [arXiv](https://arxiv.org/abs/2112.05450) |  |
| QUIC-specific CC | Fischer et al. 2025, Careful Resume in neqo |  | [DOI](https://doi.org/10.1145/3744200.3744766) |
| QUIC-specific CC | Wu et al. 2024, Wira |  | [DOI](https://doi.org/10.1109/ICDCS60910.2024.00030) |
| QUIC-specific CC | Michel et al. 2019, QUIC-FEC | [arXiv](https://arxiv.org/abs/1904.11326) |  |
| QUIC-specific CC | De Coninck & Bonaventure 2017, MPQUIC | [Project](https://multipath-quic.org/conext17.html) | [DOI](https://doi.org/10.1145/3143361.3143370) |
| QUIC-specific CC | Custura et al., ACK frequency |  | [DOI](https://doi.org/10.1002/sat.1466) |
| QUIC-specific CC | Kim & Koh 2023 |  | [DOI](https://doi.org/10.1109/ICTC58733.2023.10393389) |
| QUIC-specific CC | PCC Priority 2023 |  | [IEEE](https://ieeexplore.ieee.org/document/10106393/) |
| QUIC-specific CC | Rizvi 2026, CATS | [arXiv](https://arxiv.org/abs/2606.16924) |  |

## RFCs and drafts

Only RFC 9959 and RFC 9331 were requested in the link check, plus RFC 9000, 9001 and 9002 when they were read earlier. The rest use the same URL pattern.

| Topic | Documents |
| --- | --- |
| TCP | [RFC 5681](https://www.rfc-editor.org/rfc/rfc5681.html), [RFC 6582](https://www.rfc-editor.org/rfc/rfc6582.html), [RFC 2018](https://www.rfc-editor.org/rfc/rfc2018.html), [RFC 6675](https://www.rfc-editor.org/rfc/rfc6675.html), [RFC 9438](https://www.rfc-editor.org/rfc/rfc9438.html), [RFC 6928](https://www.rfc-editor.org/rfc/rfc6928.html) |
| Startup and state reuse | [RFC 9406](https://www.rfc-editor.org/rfc/rfc9406.html), [RFC 9040](https://www.rfc-editor.org/rfc/rfc9040.html), [RFC 9959](https://www.rfc-editor.org/rfc/rfc9959.html), [RFC 4782](https://www.rfc-editor.org/rfc/rfc4782.html) |
| ECN and AQM | [RFC 3168](https://www.rfc-editor.org/rfc/rfc3168.html), [RFC 8311](https://www.rfc-editor.org/rfc/rfc8311.html), [RFC 8289](https://www.rfc-editor.org/rfc/rfc8289.html), [RFC 8290](https://www.rfc-editor.org/rfc/rfc8290.html), [RFC 8257](https://www.rfc-editor.org/rfc/rfc8257.html), [RFC 9330](https://www.rfc-editor.org/rfc/rfc9330.html), [RFC 9331](https://www.rfc-editor.org/rfc/rfc9331.html), [RFC 9332](https://www.rfc-editor.org/rfc/rfc9332.html) |
| QUIC | [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html), [RFC 9001](https://www.rfc-editor.org/rfc/rfc9001.html), [RFC 9002](https://www.rfc-editor.org/rfc/rfc9002.html) |
| Drafts | [draft-ietf-ccwg-bbr](https://datatracker.ietf.org/doc/draft-ietf-ccwg-bbr/), [draft-ietf-quic-ack-frequency](https://datatracker.ietf.org/doc/draft-ietf-quic-ack-frequency/) |

## Link problems and citation updates

| Item | Problem | Used here |
| --- | --- | --- |
| MPQUIC PDF in Reading\_List.md, line 192 | Returned 404 | DOI and project page |
| TCP-LP PDF in Related\_Papers.md, line 65 | Host no longer resolves | Journal DOI |
| Congestion Manager page at MIT | Timed out; may be temporary | DOI |
| Marx et al. site | Returned 502; may be temporary | DOI |
| Custura et al. PDF | Returned 403; may only block automated requests | DOI |
| Paced Chirping | No official DOI found; ResearchGate link not tested | ResearchGate and a related netdev paper |
| PCC Priority | IEEE page blocked the check, so confirm the title when opening it | IEEE page |
| Mishra & Leong in Reading\_List.md | Linked to a PhD thesis chapter | The paper itself |
| Both Sidhu & Bentaleb papers | Now in an ACM multimedia journal (TOMM, 2026) | Cite the journal versions, not arXiv 2025 |
