# Real-world use cases: where each design pays off

- **Option A**: two QUIC connections per client, an express lane for small transfers and a bulk lane for
  large ones ([THESIS_PLAN.md](THESIS_PLAN.md)).
- **Option B**: one QUIC connection whose stream-aware controller gives small-file streams an aggressive
  start and keeps large-file streams on a normal schedule
  ([THESIS_PLAN_SINGLE_CONNECTION.md](THESIS_PLAN_SINGLE_CONNECTION.md)).

Both only matter where **small transfers meet a high-BDP path or a concurrent bulk transfer**; see the
headroom table in plan A §1.3.

## At a glance

| # | Use case | Best fit | The deciding property |
|---|----------|----------|------------------------|
| A1 | Photo-library restore or sync while the user browses the app | **A** | The bulk part needs OS-level background treatment (service class, Wi-Fi only, out-of-process) |
| A2 | Vehicle or IoT fleet: OTA updates plus remote commands over 5G | **A** | Cellular QoS is assigned per 5-tuple; the OTA lane is paused or resumed independently |
| A3 | Cloud file sync with separate metadata and block servers | **A** | The two classes end on different server pools anyway |
| B1 | Web pages with heavy media | **B** | HTTP/3 means one connection per origin; sizes and urgency are already known |
| B2 | Package and container registries in CI/CD | **B** | One registry origin; sizes are listed in manifests before the transfer starts |
| B3 | Video streaming: startup and seeks vs buffer filling | **B** | Aggression only at start and seek; steady segments must stay normal for ABR and fairness |

---

## When option A wins

**Rule of thumb.** The two traffic classes need different treatment *outside the sender's congestion
controller*: a network QoS class, an interface, a process lifetime, or a server pool. Each of those attaches
to a **connection** (socket, 5-tuple), never to a stream.

### A1. Photo-library restore or sync while the user browses the app

- **Use case.** A cloud photo app restores a library onto a new phone: gigabytes of full-resolution
  downloads. Meanwhile the user scrolls the app, which needs thumbnails, previews and API responses
  (10 KB–2 MB) quickly.
- **Problem today.**
  - On one connection, the sync data queues ahead of every thumbnail.
  - Opening extra ad-hoc connections gives the sync more than its share, and it still fills the access
    link's buffer, often a high-RTT cellular link.
- **Why A helps:**
  - **Background treatment per socket.** The OS can mark the bulk socket as background, for example Apple
    Network.framework's `background` service class, or the DSCP Lower-Effort marking (RFC 8622), which home
    Wi-Fi maps to its background queue (RFC 8325). None of this can be applied to one stream inside a shared
    connection.
  - **Interface policy.** The bulk lane runs on Wi-Fi or unmetered links only; the express lane may use
    cellular.
  - **Lifetime.** The bulk lane can be handed to the OS background-transfer service and survive app
    suspension, while the express lane lives and dies with the UI.
  - **CC per lane.** The express lane runs Sprint (fast start), and the bulk lane runs CUBIC or even a
    scavenger CCA.
- **Thesis proxy.** Workload W2. Add one variant: the bulk lane marked LE plus a priority qdisc at the
  bottleneck.

### A2. Vehicle or IoT fleet: OTA updates plus remote commands over 5G

- **Use case.** Cars download over-the-air firmware and map updates (hundreds of MB to GB). The backend
  meanwhile sends small commands and config (remote unlock, diagnostics requests) and fetches small
  telemetry snapshots.
- **Problem today.** The OTA download saturates the cellular link, so a remote command queued behind it
  takes seconds. Every small transfer also pays slow start on a high-RTT, variable path.
- **Why A helps:**
  - **Separate QoS flow.** 5G assigns traffic to QoS flows with packet filters on the 5-tuple or DSCP
    (3GPP TS 23.501 QoS rules). Only a separate connection can be put in a different QoS flow or slice from
    the OTA download.
  - **Independent control.** The OTA lane can be throttled or paused by vehicle state (driving vs parked and
    charging) and resumed on its own. The command lane is always on.
  - **Fault isolation.** A radio-loss burst or a persistent-congestion collapse on the bulk lane does not
    reset the command lane's window or RTT state.
- **Thesis proxy.** W2 and W7 (loss bursts, bandwidth steps) at cellular-like RTTs.

### A3. Cloud file sync with separate metadata and block servers

- **Use case.** A sync client (Dropbox or OneDrive style) lists folders, opens small documents and fetches
  previews, while it uploads or downloads large file blocks. Large storage services typically run metadata
  and API servers separately from block-storage servers.
- **Problem today.** Small edits and folder listings wait behind block transfers, and each fresh connection
  starts cold.
- **Why A helps:**
  - **Different server pools.** The classes already end on different servers, so the design's `MOVED`
    steering simply points at a block server. The bulk lane gets throughput settings (large flow-control
    windows, sparse ACKs via ACK-frequency, a bulk CCA); the express lane gets low-latency settings.
  - **Same as the testbed.** This is the thesis testbed (a file server, 100 KB–50 MB) generalized to two
    endpoints.
  - **Coupling moves to the client.** With two servers, no single sender can couple the lanes. The client
    can, by shrinking the bulk lane's flow-control credit or receiver reports (the ROS idea) while express
    transfers run. That is a natural extension chapter.
- **Thesis proxy.** W1, W2 and W4 directly.

---

## When option B wins

**Rule of thumb.** The mix of small and large transfers lives inside **one session to one origin**, a
second connection is unwelcome, and the server knows response sizes. A second connection is unwelcome
because of:
- connection-reuse rules;
- a handshake on the critical path;
- a double fair share;
- NAT, battery and per-client connection limits.

### B1. Web pages with heavy media

- **Use case.** A news or e-commerce page: HTML, CSS, JS bundles, fonts and thumbnails (10 KB–1 MB, needed
  to render), plus a hero video and large images (MBs), all from one origin or CDN.
- **Problem today.** The render-critical objects are requested exactly when the HTTP/3 connection is
  brand new and its window is 10 packets. They pay several RTTs of slow start, then compete with the media
  for the window.
- **Why B helps:**
  - **One connection by design.** RFC 9114 §3.3: clients SHOULD NOT open more than one HTTP/3 connection
    to the same IP address and UDP port. A second lane would also put another handshake on the critical
    path.
  - **Free classification.** The server already knows sizes (`Content-Length`) and importance (RFC 9218
    urgency). quic-go even exposes RFC 9218 in the transport.
  - **Targeted aggression.** The express budget gives render-critical objects an evidence-based fast start
    on the cold connection. The media keeps the normal (shadow CUBIC) schedule, and the page is still one
    flow to the network.
- **Expected effect.** Earlier First Contentful Paint and Largest Contentful Paint on high-RTT links, with
  the media's long-run behaviour unchanged.
- **Thesis proxy.** S2 (batch: 1 × 20 MB + 10 × 200 KB + 5 × 1 MB) on a cold connection.

### B2. Package and container registries in CI/CD

- **Use case.** `docker pull`, `npm install` and `pip install` from a remote registry.
  - An image is a manifest and a config (KBs) plus layers (KBs to hundreds of MB).
  - A lockfile is hundreds of small tarballs plus a few large ones.
  - CI runners often fetch across regions, so the RTT is high.
- **Problem today.** Each small blob pays several RTTs of slow start, or waits behind a large layer on the
  shared connection. CI time goes into round trips, not bytes.
- **Why B helps:**
  - **One origin.** There is one connection per registry, and requests are naturally multiplexed streams.
  - **Sizes known in advance.** OCI manifests list every layer's size, and lockfiles list package sizes.
    Classification is therefore exact, with no guessing or demotion.
  - **Express budget plus SRPT.** Small blobs finish in about 1–2 RTTs, large layers continue on the normal
    schedule, and total pull time approaches the bytes-over-bandwidth bound.
- **Thesis proxy.** S4 with a registry-like size mix (many small objects, a few large ones).

### B3. Video streaming: startup and seeks vs buffer filling

- **Use case.** HLS or DASH over HTTP/3 from a CDN.
  - At start or seek: the manifest, init segment and first media segment decide time-to-first-frame.
  - Afterwards: multi-MB segments fill the buffer.
  - Meanwhile: thumbnails, trick-play sprites and subtitles are fetched.
- **Problem today.** Startup objects ride a cold window, and a seek request waits behind the segment in
  flight and the connection's own queue. Viewers start abandoning after about 2 s of startup delay
  (Krishnan & Sitaraman, IMC 2012).
- **Why B helps:**
  - **Express only where it matters.** Startup and seek objects are express: a fast start, plus
    drain-on-express to clear the queue left by the segment in flight. Steady-state segments are bulk.
  - **Steady state stays normal.** Adaptive-bitrate (ABR) algorithms estimate bandwidth from segment
    downloads, and many players share bottlenecks. An "always aggressive" connection would distort both;
    stream-aware CC limits the aggression to the moments that need it.
  - **Already one connection.** Players already keep one HTTP/3 connection to the CDN.
- **Thesis proxy.** S1 (cold start) and S3 (short requests during a bulk download).

---

## Where neither design helps much

State these limits openly in the thesis:
- **Low-BDP paths** (e.g. 10 Mbit/s × 20 ms). Slow start fills the pipe in about 3 RTTs, so the headroom is
  about 1.0× (plan A §1.3).
- **Only large transfers** (backups, replication). Both designs reduce to the baseline; in B this is
  Rule 0 by construction.
- **Real-time media** (calls, cloud gaming). These are not short flows and need real-time CC, which is out
  of scope.
- **Option A on a plain FIFO bottleneck without coupling.** The second connection shares the queue, so only
  the algorithm helps, not the architecture.

## Where both help most

High-BDP paths whose operators can no longer fix startup inside the network: GEO satellite (about 600 ms
RTT), in-flight Wi-Fi, intercontinental links. These networks used TCP-splitting performance-enhancing
proxies to hide slow start. QUIC's encryption rules those proxies out, so fast startup must come from the
endpoints, which is also the motivation behind Careful Resume (RFC 9959).

## Decision rule

| If the short and long transfers… | Choose |
|---|---|
| need different network treatment (QoS class, DSCP, 5G slice), different interfaces, or different process lifetimes | **A** |
| end at different servers anyway | **A**, with client-side coupling |
| share one session to one origin (web, registries, video) | **B** |
| must look like one flow to the network (fairness, connection limits, NAT, battery) | **B** |

## Sources

- RFC 9114 §3.3 (HTTP/3 connection reuse).
- RFC 9218 (HTTP priorities).
- RFC 8622 (Lower-Effort PHB). RFC 8325 (Diffserv to IEEE 802.11 mapping).
- 3GPP TS 23.501 (5G QoS rules and packet filters).
- OCI Image Specification (descriptor `size` field).
- Krishnan & Sitaraman, *Video Stream Quality Impacts Viewer Behavior*, IMC 2012.
- RFC 9959 (Careful Resume).
