# Master thesis - Razvan Stefan 5722446

A QUIC congestion control algorithm for short flows, implemented in picoquic.

## Repository layout

| Folder | Contents |
|---|---|
| `thesis/` | LaTeX chapters and bibliography |
| `research_docs/` | Research notes, design and testbed plans |
| `picoquic/` | Fork of [picoquic](https://github.com/private-octopus/picoquic) (git submodule, branch `dash`) with the new algorithm |
| `sim/` | Spec files for the picoquic simulator. qlogs are written to `sim/qlogs/` and are not tracked |

## Getting the code

```sh
git clone --recurse-submodules https://github.com/razvan841/master-thesis.git
cd master-thesis
git -C picoquic checkout dash      # branch with the algorithm work
```

If the repository was cloned without `--recurse-submodules`, run `git submodule update --init` first.

## Building picoquic (Linux or WSL)

Install the dependencies once:

```sh
sudo apt update && sudo apt install -y build-essential cmake pkg-config libssl-dev git
```

Configure and build from the repository root:

```sh
cmake -S picoquic -B picoquic/build -DCMAKE_BUILD_TYPE=Release -DPICOQUIC_FETCH_PTLS=ON
cmake --build picoquic/build -j --target pico_sim picoquicdemo picoquic_ct
```

- **Internet access:** the first configure step downloads picotls, the TLS library picoquic depends on.
- **Rebuilding:** after a code change, run only the second command.
- **Build folder:** `picoquic/build/` is ignored by picoquic's `.gitignore`. Don't use `cmake .` as picoquic's own README suggests: it builds inside the source tree and makes the submodule look modified.
- **WSL:** building on the Windows file system (`/mnt/c/...`) works but is slower. You can build into your Linux home instead (`-B ~/picoquic-build`) and adjust the paths below.

To check that the new algorithm is compiled in, run `picoquic/build/picoquicdemo -h`. The line under `-G cc_algo` should end with `wip`.

## Running the simulator

`pico_sim` runs real picoquic client and server code over a simulated link, in virtual time. Run it from the repository root:

```sh
mkdir -p sim/qlogs
picoquic/build/pico_sim -S picoquic sim/cubic_1MB.txt
picoquic/build/pico_sim -S picoquic sim/wip_1MB.txt
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
