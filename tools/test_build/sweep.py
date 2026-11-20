"""check-test-builds: compile and link every test-* target, run none of them.

Most test rules compile and run in one recipe, so a test whose sources stop
compiling or linking is only noticed by whoever next runs that one test. At
OPT=1 thin LTO also drops code that names a missing symbol, so a rule can link
there and not at OPT=0. This sweep makes every test-* target of src/makefile
(and the makefiles it includes) under make itself, with test_build/shell.sh as
make's SHELL: each recipe line's build commands run, and the first command
that is not a build (the test binary, node, a python oracle, a content compile
or pack) ends the line.

    python3 -I tools/test_build/sweep.py --src src --objbase build_tb [--jobs 4]
        [--opt 0] [--only REGEX] [--keep-logs]

One objdir for the whole sweep; every target runs as its own make, --jobs at a
time. A target fails when its make fails. A failed target is retried once on
its own after the pool, so that a .d file read while another make was writing
it cannot fail a target that builds. The report lists every failure with the
end of its log, and the compiles the sweep could not reach (a compile inside a
for loop or an if, which it cannot split from the run).

Exit 0 when every target builds.
"""

import argparse
import concurrent.futures as cf
import glob
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def targets(src):
    files = [os.path.join(src, "makefile")]
    files += sorted(glob.glob(os.path.join(src, "maped", "*.mk")))
    files += sorted(glob.glob(os.path.join(src, "platform", "*.mk")))
    found = set()
    for f in files:
        text = open(f, encoding="latin-1").read()
        for m in re.finditer(r"^(test-[A-Za-z0-9_.-]+)\s*:", text, re.M):
            found.add(m.group(1))
    return sorted(found)


def make_env():
    env = dict(os.environ)
    for k in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
        env.pop(k, None)
    return env


def run_one(src, objbase, opt, logdir, target, extra_jobs):
    log = os.path.join(logdir, target + ".log")
    dec = os.path.join(logdir, target + ".decisions")
    if os.path.exists(dec):
        os.remove(dec)
    env = make_env()
    env["TORIRS_TB_LOG"] = dec
    cmd = ["make", "-k", "-j%d" % extra_jobs, "OPT=%d" % opt, "PLATFORM_OBJ_BASE=" + objbase,
           "OBJVERIFY_SKIP=1", "SHELL=" + os.path.join(HERE, "shell.sh"), target]
    t0 = time.time()
    with open(log, "w") as out:
        p = subprocess.run(cmd, cwd=src, stdout=out, stderr=subprocess.STDOUT, env=env)
    return target, p.returncode, time.time() - t0


def decisions(logdir, target):
    path = os.path.join(logdir, target + ".decisions")
    if not os.path.exists(path):
        return []
    return [l.rstrip("\n").split("\t", 1) for l in open(path) if "\t" in l]


def tail(path, n=12):
    try:
        lines = open(path, errors="replace").read().splitlines()
    except OSError:
        return []
    keep = [l for l in lines if re.search(r"error|Error|undefined|Undefined|ld:|fatal", l)]
    return (keep or lines)[-n:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="src")
    ap.add_argument("--objbase", default="build_testbuild")
    ap.add_argument("--opt", type=int, default=0)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--only", default=None)
    args = ap.parse_args()

    src = os.path.abspath(args.src)
    names = targets(src)
    if args.only:
        names = [t for t in names if re.search(args.only, t)]
    objdir_name = args.objbase if args.opt == 0 else args.objbase + "_opt"
    logdir = os.path.join(src, objdir_name, "test_build_logs")
    os.makedirs(logdir, exist_ok=True)

    t_start = time.time()
    # One make verifies the objdir by content (objverify.py) before the pool;
    # the pool skips it, so no make deletes an object another is linking.
    env = make_env()
    subprocess.run(["make", "OPT=%d" % args.opt, "PLATFORM_OBJ_BASE=" + args.objbase,
                    objdir_name], cwd=src, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    results = {}
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        futs = [ex.submit(run_one, src, args.objbase, args.opt, logdir, t, 1) for t in names]
        for f in cf.as_completed(futs):
            t, rc, dt = f.result()
            results[t] = (rc, dt)
            print("%s %s (%.1f s)" % ("ok  " if rc == 0 else "FAIL", t, dt), flush=True)

    retried_ok = []
    for t in sorted(t for t, (rc, _) in results.items() if rc != 0):
        _, rc, dt = run_one(src, args.objbase, args.opt, logdir, t, 1)
        if rc == 0:
            retried_ok.append(t)
            results[t] = (0, dt)

    failed = sorted(t for t, (rc, _) in results.items() if rc != 0)
    built = 0
    nothing = []
    compound = []
    for t in names:
        d = decisions(logdir, t)
        kinds = [k for k, _ in d]
        if "RUN" in kinds or "PREFIX" in kinds:
            built += 1
        else:
            nothing.append(t)
        for k, line in d:
            if k == "COMPOUND":
                compound.append((t, line[:160]))

    print()
    print("check-test-builds: %d targets at OPT=%d in %.0f s: %d built, %d failed, "
          "%d with nothing to build here" % (len(names), args.opt, time.time() - t_start, built,
                                            len(failed), len(nothing)))
    if retried_ok:
        print("passed only on the serial retry (a concurrent .d read): " + " ".join(retried_ok))
    if nothing:
        print("nothing to build (run-only rules, aggregates of other test targets, or a "
              "build this sweep may not start): " + " ".join(nothing))
    if compound:
        print("compiles inside shell control flow, not swept:")
        for t, line in compound:
            print("  %s: %s" % (t, line))
    for t in failed:
        print("FAILED %s (log %s)" % (t, os.path.join(logdir, t + ".log")))
        for l in tail(os.path.join(logdir, t + ".log")):
            print("    " + l[:300])
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
