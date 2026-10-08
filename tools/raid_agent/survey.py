#!/usr/bin/env python3
"""Survey a bot-runner policy over many seeds, in parallel.

    python3 tools/raid_agent/survey.py verzik --names 12 [--prefix vz] [--jobs 6]

Each name seeds its own run (torirsserver --botrun --name <name>), so a name
that fails replays exactly: the command file and ticklog are kept under
build/raid_agent/<policy>/<name>.{cmd,tsv,log}. One line per name, then the
tally. A run is GREEN when the policy's report says the boss fell with
nobody dead.
"""
import argparse
import concurrent.futures
import os
import re
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER = os.path.join(ROOT, "src", "build_botrun_opt", "torirsserver")


def run_one(policy, name, bots, out_dir):
    saves = tempfile.mkdtemp(prefix="botsaves_")
    # the policy's LOADOUT (tools/raid_agent/loadout.py): each bot logs in on
    # its seat's save, the runner names bots <name>1..N
    loadout = os.environ.get("RAID_AGENT_LOADOUT", policy)
    if os.path.isfile(os.path.join(ROOT, "test", "raids", "fixtures", "loadouts", loadout + "_seat1.ini")):
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "raid_agent", "loadout.py"), "install",
                        loadout, saves, name], check=True)
    log_path = os.path.join(out_dir, name + ".log")
    cmd = [SERVER, "--botrun", "--bots", str(bots), "--name", name, "--ticks", "4000",
           "--ticklog", os.path.join(out_dir, name + ".tsv"),
           "--record", os.path.join(out_dir, name + ".cmd"),
           "--agent", "lua tools/raid_agent/run.lua " + policy]
    if os.environ.get("RAID_AGENT_SHARED"):
        cmd.append("--shared")
    env = dict(os.environ, TORIRSSERVER_SAVES=saves)
    if os.path.isfile(os.path.join(ROOT, "test", "raids", "fixtures", "loadouts", loadout + "_seat1.ini")):
        env["RAID_AGENT_LOADOUT"] = loadout
    with open(log_path, "w") as log:
        subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    report = ""
    with open(log_path) as log:
        for line in log:
            # the policy's report line by its shape, not its prefix: a policy
            # kept under another name (RAID_AGENT_PATH) still reports as its room
            if re.match(r"^[a-z_]+: [A-Z ]*(GONE|WIPE|TIMEOUT) at t\d+", line):
                report = line.strip()
    # deaths from the server's own record (raider rows at 0), not from what
    # any raider's client saw
    dead = set()
    with open(os.path.join(out_dir, name + ".tsv")) as tl:
        for line in tl:
            c = line.split("\t")
            if len(c) > 5 and c[2] == "raider" and c[4] == "0":
                dead.add(c[3])
    return name, report + (" | deaths %d" % len(dead) if dead else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("policy")
    ap.add_argument("--names", type=int, default=8)
    ap.add_argument("--prefix", default="sv")
    ap.add_argument("--bots", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args()
    assert os.path.exists(SERVER), "build it: make -B -C src OPT=1 PLATFORM_OBJ_BASE=build_botrun torirsserver"
    out_dir = os.path.join(ROOT, "build", "raid_agent", os.environ.get("RAID_AGENT_REPORT", args.policy))
    os.makedirs(out_dir, exist_ok=True)
    names = ["%s%02d" % (args.prefix, i) for i in range(args.names)]
    green = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, report in pool.map(lambda n: run_one(args.policy, n, args.bots, out_dir), names):
            ok = "GONE" in report and "deaths" not in report
            green += ok
            print("%-8s %s  %s" % (name, "GREEN" if ok else "FAIL ", report.split(":", 1)[-1].strip()[:150]))
            sys.stdout.flush()
    print("%s: %d of %d green" % (args.policy, green, len(names)))


if __name__ == "__main__":
    main()
