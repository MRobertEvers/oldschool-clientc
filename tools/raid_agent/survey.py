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
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER = os.path.join(ROOT, "src", "build_botrun_opt", "torirsserver")


def run_one(policy, name, bots, out_dir):
    saves = tempfile.mkdtemp(prefix="botsaves_")
    log_path = os.path.join(out_dir, name + ".log")
    cmd = [SERVER, "--botrun", "--bots", str(bots), "--name", name, "--ticks", "4000",
           "--ticklog", os.path.join(out_dir, name + ".tsv"),
           "--record", os.path.join(out_dir, name + ".cmd"),
           "--agent", "lua tools/raid_agent/run.lua " + policy]
    env = dict(os.environ, TORIRSSERVER_SAVES=saves)
    with open(log_path, "w") as log:
        subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    report = ""
    with open(log_path) as log:
        for line in log:
            if line.startswith(policy + ":"):
                report = line.strip()
    return name, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("policy")
    ap.add_argument("--names", type=int, default=8)
    ap.add_argument("--prefix", default="sv")
    ap.add_argument("--bots", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args()
    assert os.path.exists(SERVER), "build it: make -B -C src OPT=1 PLATFORM_OBJ_BASE=build_botrun torirsserver"
    out_dir = os.path.join(ROOT, "build", "raid_agent", args.policy)
    os.makedirs(out_dir, exist_ok=True)
    names = ["%s%02d" % (args.prefix, i) for i in range(args.names)]
    green = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, report in pool.map(lambda n: run_one(args.policy, n, args.bots, out_dir), names):
            ok = "GONE" in report and "died" not in report
            green += ok
            print("%-8s %s  %s" % (name, "GREEN" if ok else "FAIL ", report[len(args.policy) + 2:][:150]))
            sys.stdout.flush()
    print("%s: %d of %d green" % (args.policy, green, len(names)))


if __name__ == "__main__":
    main()
