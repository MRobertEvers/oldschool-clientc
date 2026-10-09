#!/usr/bin/env python3
"""Build every server pack: `make -C src torirsserver-packs` runs this.

    build_packs.py --sscompile BIN --cachepack BIN --content DIR --out DIR
                   [--lane-out DIR --lanes "a b"] [--servpack-out DIR]
                   [--full] [--verbose] [--explain FILE]

1. tools/ss_allocate.py: new `[name]` blocks get their ids (skipped in 0.05 s
   when nothing it reads has moved).
2. In parallel: the base script pack, the lane script pack and the server pack.
   Each is incremental (docs/serverpack.md), so a build with nothing to do costs
   a fraction of a second and there is no separate freshness check to run first.

Every tool's output is printed in full, in order, under a header naming the
pack: nothing is cut to a last line. Each tool prints one line per unit it
rebuilt and why, and writes every unit's line to `pack.log` beside its pack.
The last line is the summary with each step's wall time. `--explain FILE`
asks sscompile why that file's unit in each script pack is (or is not) stale,
and builds nothing.

Exit status: 0 when every step succeeded, else 1.
"""

import argparse
import os
import subprocess
import sys
import time


def run(name, command):
    started = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return name, command, process, started


def finish(job):
    name, command, process, started = job
    output, _ = process.communicate()
    return name, process.returncode, output.decode("utf-8", "replace"), time.monotonic() - started


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--sscompile", required=True)
    ap.add_argument("--cachepack", required=True)
    ap.add_argument("--content", required=True)
    ap.add_argument("--out", required=True, help="the base script pack's directory")
    ap.add_argument("--lane-out", help="the lane script pack's directory")
    ap.add_argument("--lanes", default="", help="the lanes compiled into --lane-out")
    ap.add_argument("--servpack-out", help="the server pack (default <content>/server/pack)")
    ap.add_argument("--full", action="store_true", help="recompile and repack every unit")
    ap.add_argument("--verbose", action="store_true", help="print every unit's line")
    ap.add_argument("--explain", help="why is this file's unit stale? builds nothing")
    args = ap.parse_args()

    scripts = os.path.join(args.content, "server", "scripts")
    lanes = args.lanes.split()
    script_packs = [("script pack (base)", args.out, [])]
    if args.lane_out and lanes:
        script_packs.append(("script pack (%s)" % " ".join(lanes), args.lane_out, lanes))

    def sscompile(out, pack_lanes, *extra):
        command = [args.sscompile, "--src", scripts, "--out", out, "--content-root", args.content]
        for lane in pack_lanes:
            command += ["--lane", lane]
        return command + list(extra)

    if args.explain:
        status = 0
        for name, out, pack_lanes in script_packs:
            print("== %s -> %s" % (name, out), flush=True)
            status |= subprocess.call(sscompile(out, pack_lanes, "--explain", args.explain))
        return 1 if status else 0

    started = time.monotonic()
    timings = []
    print("== allocate ids", flush=True)
    allocate_started = time.monotonic()
    code = subprocess.call([sys.executable,
                            os.path.join(os.path.dirname(os.path.abspath(__file__)), "ss_allocate.py"),
                            "--tree", args.content,
                            "--stamp", os.path.join(scripts, "build", ".ss_allocate.stamp")])
    timings.append(("allocate", time.monotonic() - allocate_started))
    if code != 0:
        print("== packs: FAILED at the id allocation (exit %d)" % code, flush=True)
        return 1

    extra = (["--full"] if args.full else []) + (["--verbose"] if args.verbose else [])
    jobs = []
    for name, out, pack_lanes in script_packs:
        os.makedirs(out, exist_ok=True)
        jobs.append(run("%s -> %s" % (name, out), sscompile(out, pack_lanes, *extra)))
    servpack = [args.cachepack, "pack", "--src", args.content, "--server-only"]
    if args.servpack_out:
        servpack += ["--server-out", args.servpack_out]
    if args.full:
        servpack += ["--force"]
    jobs.append(run("server pack -> %s" % (args.servpack_out or
                                           os.path.join(args.content, "server", "pack")),
                    servpack))

    failed = []
    for job in jobs:
        name, returncode, output, elapsed = finish(job)
        print("== %s" % name, flush=True)
        sys.stdout.write(output)
        if output and not output.endswith("\n"):
            sys.stdout.write("\n")
        timings.append((name.split(" -> ")[0], elapsed))
        if returncode != 0:
            failed.append(name)
            print("== %s: FAILED (exit %d)" % (name, returncode), flush=True)
    summary = ", ".join("%s %.2fs" % (label, seconds) for label, seconds in timings)
    print("== packs: %s [%s; wall %.2fs]"
          % ("FAILED: " + "; ".join(failed) if failed else "ok", summary,
             time.monotonic() - started), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
