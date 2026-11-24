#!/usr/bin/env python3
"""
Experiment runner for the thesis testbed.

Topology (inventory.yml): senders mickey and minnie, router daisy (emulated
bottleneck), receiver donald. The runner runs on donald, which is also the
Ansible control node, and reaches the other hosts over SSH on the management
network. Test traffic only uses the direct experiment links.

Usage, from the testbed/ folder on donald:

    python3 runner.py check                      can every host be controlled? same code everywhere?
    python3 runner.py plan campaigns/smoke.yml   list the runs of a campaign, do nothing
    python3 runner.py run  campaigns/smoke.yml   run the campaign (runs already done are skipped)

One run = one congestion control algorithm, one repetition:
    1. clean up leftovers of earlier runs
    2. configure the bottleneck on daisy (tc) and check the RTT with ping
    3. start the packet captures on daisy and wip_server on the sender
    4. run wip_client on donald (the workload of the campaign)
    5. stop the server and the captures, copy every file to donald
    6. check that the run is valid and write manifest.json

Results, relative to the repository root:
    results/<campaign>/campaign.yml        copy of the campaign file
    results/<campaign>/testbed.json        code versions and kernels of the hosts
    results/<campaign>/runs.csv            one line per run: algorithm, validity, median FCT
    results/<campaign>/<run_id>/manifest.json
    results/<campaign>/<run_id>/<host>/    every file the run produced on that host
"""

import argparse
import csv
import datetime
import json
import random
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("PyYAML is missing: sudo apt install python3-yaml (site.yml installs it)")

TESTBED_DIR = Path(__file__).resolve().parent
REPO_DIR = TESTBED_DIR.parent
RESULTS_DIR = REPO_DIR / "results"
REMOTE_ROOT = "/tmp/thesis-runs"
SERVER_PORT = 4433
SAFE_NAME = re.compile(r"^[A-Za-z0-9_.-]+$")
SSH_OPTS = [
    "-o", "BatchMode=yes",
    "-o", "ConnectTimeout=10",
    "-o", "ControlMaster=auto",
    "-o", "ControlPath=~/.ssh/cm-%C",
    "-o", "ControlPersist=15m",
]

CAMPAIGN_DEFAULTS = {
    "repetitions": 1,
    "seed": 1,
    "cooldown_s": 2,
    "client_timeout_s": 1800,
    "rtt_tolerance_ms": 1.0,
    "workload": {"repeat": 1, "per_cnx": 1, "gap_ms": 0, "parallel": False},
    "logging": {"sample_us": 10000, "pcap": True, "qlog": False},
    "quic": {"no_gso": True},
    "pinning": {},
}

RUNS_CSV_FIELDS = ["run_id", "algorithm", "rep", "valid", "requests_ok", "requests_expected",
                   "median_fct_ms", "bottleneck_drops", "failed_checks"]


def log(msg):
    print(f"[{datetime.datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


class Host:
    """A testbed machine: runs shell commands locally or over SSH."""

    def __init__(self, name, hvars, deploy_dir):
        self.name = name
        self.v = hvars
        self.addr = hvars.get("ansible_host", name)
        self.local = hvars.get("ansible_connection") == "local"
        # "~/x" becomes "$HOME/x", so that the shell on the host expands it.
        self.deploy = "$HOME" + deploy_dir[1:] if deploy_dir.startswith("~") else deploy_dir

    def _argv(self, cmd):
        return ["bash", "-c", cmd] if self.local else ["ssh"] + SSH_OPTS + [self.addr, cmd]

    def sh(self, cmd, timeout=60, check=True):
        try:
            p = subprocess.run(self._argv(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               universal_newlines=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            raise RuntimeError(f"{self.name}: timed out after {timeout} s: {cmd}")
        if check and p.returncode != 0:
            raise RuntimeError(f"{self.name}: exit code {p.returncode}: {cmd}\n{p.stderr.strip()}")
        return p

    def out(self, cmd, timeout=60):
        return self.sh(cmd, timeout=timeout).stdout

    def fetch_dir(self, remote_dir, local_dir):
        """Copy the content of remote_dir on this host into local_dir (tar over ssh)."""
        local_dir.mkdir(parents=True, exist_ok=True)
        src = subprocess.Popen(self._argv(f"tar -C {remote_dir} -cf - ."), stdout=subprocess.PIPE)
        try:
            subprocess.run(["tar", "-C", str(local_dir), "-xf", "-"], stdin=src.stdout, check=True, timeout=900)
        finally:
            src.stdout.close()
            rc = src.wait()
        if rc != 0:
            raise RuntimeError(f"{self.name}: could not copy {remote_dir}")


class Testbed:
    """The hosts of inventory.yml, by role."""

    def __init__(self, inventory_path):
        inv = yaml.safe_load(Path(inventory_path).read_text())["all"]
        self.vars = inv.get("vars", {})
        deploy = self.vars.get("deploy_dir", "~/thesis-deploy")
        groups = inv["children"]

        def group(name):
            return {h: Host(h, hv or {}, deploy) for h, hv in groups[name]["hosts"].items()}

        self.senders = group("senders")
        routers, receivers = group("router"), group("receiver")
        if len(routers) != 1 or len(receivers) != 1:
            sys.exit("inventory.yml: expected exactly one router and one receiver")
        self.router = next(iter(routers.values()))
        self.receiver = next(iter(receivers.values()))
        if not self.receiver.local:
            sys.exit("inventory.yml: the receiver must be this machine (ansible_connection: local)")
        self.hosts = dict(self.senders)
        self.hosts[self.router.name] = self.router
        self.hosts[self.receiver.name] = self.receiver

    def router_dev_to(self, peer):
        for link in self.router.v["router_links"]:
            if link["peer"] == peer:
                return link["dev"]
        raise KeyError(f"no router link to {peer} in inventory.yml")


# ---------------------------------------------------------------- campaign

def load_campaign(path):
    c = yaml.safe_load(Path(path).read_text())
    for key, default in CAMPAIGN_DEFAULTS.items():
        if isinstance(default, dict):
            c[key] = {**default, **(c.get(key) or {})}
        else:
            c.setdefault(key, default)
    for key in ("name", "sender", "network", "algorithms"):
        if key not in c:
            sys.exit(f"{path}: missing '{key}'")
    for key in ("rate_mbit", "rtt_ms", "buffer_bdp"):
        if key not in c["network"]:
            sys.exit(f"{path}: missing network.{key}")
    if "sizes" not in c["workload"]:
        sys.exit(f"{path}: missing workload.sizes")
    for name in [c["name"], c["sender"]] + list(c["algorithms"]):
        if not SAFE_NAME.match(str(name)):
            sys.exit(f"{path}: '{name}' may only contain letters, digits, '_', '.' and '-'")
    return c


def plan_runs(c):
    """Repetitions in blocks; within a block every algorithm once, in a random order,
    so that slow drifts of the testbed do not line up with one algorithm."""
    rng = random.Random(c["seed"])
    runs = []
    for rep in range(c["repetitions"]):
        algorithms = list(c["algorithms"])
        rng.shuffle(algorithms)
        for algo in algorithms:
            runs.append({"run_id": f"{len(runs):03d}_{algo}_rep{rep}", "algorithm": algo, "rep": rep})
    return runs


# ---------------------------------------------------------------- measurements

def bottleneck_commands(tb, net):
    """tc commands for daisy. Bottleneck: rate limit (HTB) and drop-tail buffer
    (bfifo) on the link to the receiver. Delay: netem on the links back to the
    senders, so it never shares a queue with the bottleneck. Every packet
    exchange crosses both directions, so the RTT grows by exactly rtt_ms."""
    rate = float(net["rate_mbit"])
    rtt = float(net["rtt_ms"])
    buffer_bytes = max(int(rate * 1e6 / 8 * rtt / 1000 * float(net["buffer_bdp"])), 3 * 1514)
    to_receiver = tb.router_dev_to(tb.receiver.name)
    to_senders = [tb.router_dev_to(s) for s in tb.senders]
    cmds = [f"sudo -n tc qdisc del dev {d} root 2>/dev/null || true" for d in [to_receiver] + to_senders]
    cmds += [
        f"sudo -n tc qdisc add dev {to_receiver} root handle 1: htb default 10",
        f"sudo -n tc class add dev {to_receiver} parent 1: classid 1:10 htb rate {rate:g}mbit ceil {rate:g}mbit burst 15k cburst 15k",
        f"sudo -n tc qdisc add dev {to_receiver} parent 1:10 handle 10: bfifo limit {buffer_bytes}",
    ]
    cmds += [f"sudo -n tc qdisc add dev {d} root netem delay {rtt:g}ms limit 100000" for d in to_senders]
    return cmds, buffer_bytes


def ping_rtt(host, ip, count=5):
    out = host.out(f"ping -c {count} -i 0.2 -q {ip}", timeout=30)
    m = re.search(r"= ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+) ms", out)
    if m is None:
        raise RuntimeError(f"{host.name}: cannot read ping output for {ip}")
    return {"min": float(m[1]), "avg": float(m[2]), "max": float(m[3]), "mdev": float(m[4])}


def udp_counters(host):
    rows = [line.split() for line in host.out("cat /proc/net/snmp").splitlines() if line.startswith("Udp:")]
    return dict(zip(rows[0][1:], (int(x) for x in rows[1][1:])))


def bfifo_counters(tc_text):
    m = re.search(r"qdisc bfifo 10:.*?Sent (\d+) bytes (\d+) pkt \(dropped (\d+)", tc_text, re.S)
    return {"bytes": int(m[1]), "packets": int(m[2]), "dropped": int(m[3])} if m else None


def wait_for(host, cmd, timeout_s, what):
    end = time.time() + timeout_s
    while time.time() < end:
        if host.sh(cmd, check=False).returncode == 0:
            return
        time.sleep(0.2)
    raise RuntimeError(f"{host.name}: {what} (waited {timeout_s} s)")


def host_versions(h):
    return {
        "thesis_commit": h.out(f"git -C {h.deploy} rev-parse HEAD").strip(),
        "picoquic_commit": h.out(f"git -C {h.deploy}/picoquic rev-parse HEAD").strip(),
        "kernel": h.out("uname -r").strip(),
        "cpus": int(h.out("nproc").strip()),
    }


def runner_version():
    p = subprocess.run(["git", "-C", str(REPO_DIR), "rev-parse", "HEAD"], stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, universal_newlines=True)
    return p.stdout.strip()


def taskset(cpu):
    return f"taskset -c {cpu} " if cpu is not None else ""


# ---------------------------------------------------------------- processes

def cleanup(tb):
    """Stop anything an interrupted run may have left behind."""
    for s in tb.senders.values():
        s.sh("pkill -x wip_server || true", check=False)
    tb.receiver.sh("pkill -x wip_client || true", check=False)
    # "[t]cpdump" does not match the command line of pkill or of the shell running it.
    tb.router.sh(f"sudo -n pkill -INT -f '[t]cpdump .*{REMOTE_ROOT}' || true", check=False)


def start_capture(router, dev, rdir, label):
    router.sh(f"nohup sudo -n tcpdump -i {dev} -n -s 96 --time-stamp-precision=nano -B 65536 -Z $(id -un) "
              f"-w {rdir}/{label}.pcap udp > {rdir}/{label}.tcpdump.log 2>&1 < /dev/null &")
    wait_for(router, f"grep -q 'listening on' {rdir}/{label}.tcpdump.log", 10, f"tcpdump on {dev} did not start")


def stop_capture(router, rdir, label):
    router.sh(f"sudo -n pkill -INT -f '[t]cpdump .*-w {rdir}/{label}.pcap' || true", check=False)
    wait_for(router, f"grep -q 'dropped by kernel' {rdir}/{label}.tcpdump.log", 15, f"tcpdump {label} did not stop")


def start_server(sender, c, run, rdir):
    log_opts = f"--cc-log {rdir}/server_cc.csv --sample-us {c['logging']['sample_us']} -F {rdir}/server_perf.csv"
    if c["logging"]["qlog"]:
        log_opts += f" -q {rdir}/qlog"
    cmd = (f"{taskset(c['pinning'].get('server_cpu'))}{sender.deploy}/build/wip_server "
           f"-G {run['algorithm']} -p {SERVER_PORT} {'-0 ' if c['quic']['no_gso'] else ''}"
           f"--label {run['run_id']} {log_opts}")
    # Only the server goes to the background, so $! is its PID (nohup and taskset exec it).
    pid = sender.out(f"mkdir -p {rdir}/qlog && cd {rdir} && "
                     f"{{ nohup {cmd} > server.out 2>&1 < /dev/null & echo $!; }}").strip()
    if not pid.isdigit():
        raise RuntimeError(f"{sender.name}: could not start wip_server: {pid}")
    wait_for(sender, f"grep -q 'Server ready.' {rdir}/server.out", 10, "wip_server did not start (see server.out)")
    return pid


def stop_server(sender, pid, rdir):
    sender.sh(f"kill -TERM {pid} 2>/dev/null || true", check=False)
    wait_for(sender, f"! kill -0 {pid} 2>/dev/null", 15, "wip_server did not stop")


def client_command(tb, c, run, sender, rdir):
    w = c["workload"]
    opts = (f"--sizes {','.join(str(s) for s in w['sizes'])} --repeat {w['repeat']} "
            f"--per-cnx {w['per_cnx']} --gap-ms {w['gap_ms']} {'--parallel ' if w['parallel'] else ''}"
            f"--req-log {rdir}/client_req.csv -F {rdir}/client_perf.csv --label {run['run_id']}")
    if c["logging"]["qlog"]:
        opts += f" -q {rdir}/qlog"
    return (f"mkdir -p {rdir}/qlog && cd {rdir} && {taskset(c['pinning'].get('client_cpu'))}"
            f"{tb.receiver.deploy}/build/wip_client {opts} {sender.v['exp_ip']} {SERVER_PORT} > client.out 2>&1")


# ---------------------------------------------------------------- one run

def run_one(tb, c, run, out_dir, versions):
    sender = tb.senders[c["sender"]]
    router, receiver = tb.router, tb.receiver
    involved = [sender, router, receiver]
    rdir = f"{REMOTE_ROOT}/{c['name']}/{run['run_id']}"
    to_receiver = tb.router_dev_to(receiver.name)
    from_sender = tb.router_dev_to(sender.name)
    router_devs = [link["dev"] for link in router.v["router_links"]]
    w = c["workload"]
    manifest = {
        "run_id": run["run_id"], "campaign": c["name"], "algorithm": run["algorithm"], "rep": run["rep"],
        "sender": sender.name, "router": router.name, "receiver": receiver.name,
        "network": c["network"], "workload": w, "logging": c["logging"], "quic": c["quic"],
        "pinning": c["pinning"], "versions": versions, "runner_commit": runner_version(),
        "start_utc": datetime.datetime.utcnow().isoformat() + "Z",
    }

    # 1. clean state
    cleanup(tb)
    for h in involved:
        h.sh(f"rm -rf {rdir} && mkdir -p {rdir}")

    # 2. bottleneck and checks before the run
    cmds, buffer_bytes = bottleneck_commands(tb, c["network"])
    router.sh(" && ".join(cmds))
    manifest["tc_commands"] = cmds
    manifest["buffer_bytes"] = buffer_bytes
    tc_before = {d: router.out(f"tc -s qdisc show dev {d}") for d in router_devs}
    manifest["ping_ms"] = ping_rtt(receiver, sender.v["exp_ip"])
    udp_before = {h.name: udp_counters(h) for h in (sender, receiver)}

    # 3. captures and server, 4. client
    captures = []
    pid = None
    try:
        if c["logging"]["pcap"]:
            for label, dev in (("bottleneck_out", to_receiver), ("sender_in", from_sender)):
                start_capture(router, dev, rdir, label)
                captures.append(label)
        pid = start_server(sender, c, run, rdir)
        t0 = time.time()
        p = receiver.sh(client_command(tb, c, run, sender, rdir), timeout=c["client_timeout_s"], check=False)
        manifest["client_exit_code"] = p.returncode
        manifest["client_duration_s"] = round(time.time() - t0, 3)
    finally:
        # 5. stop everything, even if the client failed
        if pid:
            stop_server(sender, pid, rdir)
        for label in captures:
            stop_capture(router, rdir, label)

    udp_after = {h.name: udp_counters(h) for h in (sender, receiver)}
    tc_after = {d: router.out(f"tc -s qdisc show dev {d}") for d in router_devs}
    manifest["end_utc"] = datetime.datetime.utcnow().isoformat() + "Z"

    # copy every file to donald, then remove the run folders on the hosts
    for h in involved:
        h.fetch_dir(rdir, out_dir / h.name)
        h.sh(f"rm -rf {rdir}", check=False)
    (out_dir / router.name / "tc_before.txt").write_text("".join(tc_before.values()))
    (out_dir / router.name / "tc_after.txt").write_text("".join(tc_after.values()))

    # 6. validity
    rows = []
    req_log = out_dir / receiver.name / "client_req.csv"
    if req_log.exists():
        with open(req_log, newline="") as f:
            rows = list(csv.DictReader(f))
    ok_rows = [r for r in rows if r["ok"] == "1"]
    expected = len(w["sizes"]) * int(w["repeat"])
    udp_errors = {name: {k: udp_after[name][k] - udp_before[name][k] for k in ("RcvbufErrors", "SndbufErrors", "InErrors")}
                  for name in udp_before}
    capture_drops = {}
    for label in captures:
        text = (out_dir / router.name / f"{label}.tcpdump.log").read_text()
        m = re.search(r"(\d+) packets dropped by kernel", text)
        capture_drops[label] = int(m[1]) if m else -1
    server_out = out_dir / sender.name / "server.out"
    bottleneck = bfifo_counters(tc_after[to_receiver])

    checks = {
        "client_exit_ok": manifest.get("client_exit_code") == 0,
        "all_requests_ok": len(ok_rows) == expected,
        "server_clean_exit": server_out.exists() and "Server exit, ret = 0" in server_out.read_text(),
        "rtt_as_configured": abs(manifest["ping_ms"]["avg"] - float(c["network"]["rtt_ms"])) <= float(c["rtt_tolerance_ms"]),
        "no_udp_socket_drops": all(v == 0 for d in udp_errors.values() for v in d.values()),
        "no_capture_drops": all(v == 0 for v in capture_drops.values()),
        "bottleneck_configured": bottleneck is not None,
    }
    manifest.update({
        "requests_expected": expected,
        "requests_ok": len(ok_rows),
        "median_fct_ms": round(statistics.median(int(r["fct_req_us"]) for r in ok_rows) / 1000, 3) if ok_rows else None,
        "udp_error_deltas": udp_errors,
        "capture_drops": capture_drops,
        "bottleneck_counters": bottleneck,
        "checks": checks,
        "valid": all(checks.values()),
        "status": "done",
    })
    return manifest


# ---------------------------------------------------------------- commands

def preflight(tb):
    """Can every host be controlled, and does every host run the same code?"""
    versions, problems = {}, []
    for h in tb.hosts.values():
        try:
            versions[h.name] = host_versions(h)
            for binary in ("wip_server", "wip_client"):
                if h.sh(f"test -x {h.deploy}/build/{binary}", check=False).returncode != 0:
                    problems.append(f"{h.name}: {binary} is not built (run site.yml)")
        except RuntimeError as e:
            problems.append(str(e).splitlines()[0])
    commits = {v["thesis_commit"] for v in versions.values()}
    if len(commits) > 1:
        problems.append("the hosts run different code versions: " +
                        ", ".join(f"{n}={v['thesis_commit'][:10]}" for n, v in versions.items()))
    for tool in ("tc qdisc show", "tcpdump --version"):
        if tb.router.sh(f"sudo -n {tool} > /dev/null 2>&1", check=False).returncode != 0:
            problems.append(f"{tb.router.name}: 'sudo -n {tool}' is not allowed (run site.yml)")
    for s in tb.senders.values():
        try:
            rtt = ping_rtt(tb.receiver, s.v["exp_ip"], count=3)
            log(f"{tb.receiver.name} -> {s.name} ({s.v['exp_ip']}): ping {rtt['avg']:.3f} ms")
        except RuntimeError as e:
            problems.append(f"no experiment path {tb.receiver.name} -> {s.name}: {str(e).splitlines()[0]}")
    return versions, problems


def cmd_check(args):
    tb = Testbed(args.inventory)
    versions, problems = preflight(tb)
    for name, v in versions.items():
        log(f"{name}: thesis {v['thesis_commit'][:10]}, picoquic {v['picoquic_commit'][:10]}, "
            f"kernel {v['kernel']}, {v['cpus']} cpus")
    for p in problems:
        log(f"PROBLEM: {p}")
    if not problems:
        log("All checks passed.")
    return 1 if problems else 0


def cmd_plan(args):
    c = load_campaign(args.campaign)
    runs = plan_runs(c)
    print(f"Campaign {c['name']}: {len(runs)} runs, sender {c['sender']}, network {c['network']}")
    for r in runs:
        done = (RESULTS_DIR / c["name"] / r["run_id"] / "manifest.json").exists()
        print(f"  {r['run_id']}{'   (done)' if done else ''}")
    return 0


def append_runs_csv(path, m):
    new = not path.exists()
    with open(path, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=RUNS_CSV_FIELDS)
        if new:
            writer.writeheader()
        writer.writerow({
            "run_id": m["run_id"], "algorithm": m["algorithm"], "rep": m["rep"], "valid": int(m["valid"]),
            "requests_ok": m.get("requests_ok"), "requests_expected": m.get("requests_expected"),
            "median_fct_ms": m.get("median_fct_ms"),
            "bottleneck_drops": (m.get("bottleneck_counters") or {}).get("dropped"),
            "failed_checks": " ".join(k for k, v in (m.get("checks") or {}).items() if not v) or m.get("error", ""),
        })


def cmd_run(args):
    tb = Testbed(args.inventory)
    c = load_campaign(args.campaign)
    if c["sender"] not in tb.senders:
        sys.exit(f"sender '{c['sender']}' is not in the senders group of {args.inventory}")
    camp_dir = RESULTS_DIR / c["name"]
    camp_dir.mkdir(parents=True, exist_ok=True)

    versions, problems = preflight(tb)
    if problems:
        for p in problems:
            log(f"PROBLEM: {p}")
        sys.exit("Fix the problems above first (python3 runner.py check).")
    shutil.copy(args.campaign, camp_dir / "campaign.yml")
    (camp_dir / "testbed.json").write_text(json.dumps(
        {"versions": versions, "inventory_vars": tb.vars, "runner_commit": runner_version()}, indent=2))

    runs = plan_runs(c)
    nb_done = nb_invalid = 0
    try:
        for i, run in enumerate(runs):
            out_dir = camp_dir / run["run_id"]
            manifest_path = out_dir / "manifest.json"
            if manifest_path.exists():
                old = json.loads(manifest_path.read_text())
                if old.get("valid") or not args.rerun_invalid:
                    continue
                shutil.rmtree(out_dir)
            log(f"run {i + 1}/{len(runs)}: {run['run_id']}")
            out_dir.mkdir(parents=True)
            try:
                m = run_one(tb, c, run, out_dir, versions)
            except RuntimeError as e:
                log(f"  ERROR: {e}")
                m = {**run, "campaign": c["name"], "status": "error", "valid": False, "error": str(e).splitlines()[0]}
                cleanup(tb)
            manifest_path.write_text(json.dumps(m, indent=2))
            append_runs_csv(camp_dir / "runs.csv", m)
            nb_done += 1
            nb_invalid += 0 if m["valid"] else 1
            failed = [k for k, v in (m.get("checks") or {}).items() if not v]
            log(f"  {'valid' if m['valid'] else 'INVALID'}: {m.get('requests_ok')}/{m.get('requests_expected')} requests, "
                f"median FCT {m.get('median_fct_ms')} ms" + (f", failed: {' '.join(failed)}" if failed else ""))
            time.sleep(c["cooldown_s"])
    except KeyboardInterrupt:
        log("Interrupted: stopping servers and captures.")
        cleanup(tb)
        return 130
    log(f"Campaign {c['name']}: {nb_done} runs this time, {nb_invalid} invalid. Results in {camp_dir}")
    return 1 if nb_invalid else 0


def main():
    ap = argparse.ArgumentParser(description="Thesis testbed experiment runner")
    ap.add_argument("--inventory", default=str(TESTBED_DIR / "inventory.yml"))
    sub = ap.add_subparsers(dest="command")
    sub.add_parser("check", help="check that every host can be controlled")
    p_plan = sub.add_parser("plan", help="list the runs of a campaign")
    p_plan.add_argument("campaign")
    p_run = sub.add_parser("run", help="run a campaign")
    p_run.add_argument("campaign")
    p_run.add_argument("--rerun-invalid", action="store_true", help="run again the runs marked invalid")
    args = ap.parse_args()
    if args.command == "check":
        return cmd_check(args)
    if args.command == "plan":
        return cmd_plan(args)
    if args.command == "run":
        return cmd_run(args)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
