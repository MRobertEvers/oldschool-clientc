#!/usr/bin/env python3
"""Run `make -C src torirsserver-packs` under an exclusive lock.

Several sessions edit the CoX content in one checkout at once. tools/build_packs.py
writes the shared script and server packs without a lock of its own, so two
concurrent builds can interleave their writes. Every CoX worker builds the
shared packs through this wrapper instead (macOS has no flock(1)).

    python3 tools/cox/locked_packs.py [--retry N] [extra make args...]

--retry N: a failed build is tried again up to N times, 20 s apart, with the
lock released in between -- another worker's file caught mid-edit is the
usual cause, and it is theirs to finish.
"""
import fcntl
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOCK = os.path.join(ROOT, "build", ".torirsserver-packs.lock")

args = sys.argv[1:]
retries = 0
if len(args) >= 2 and args[0] == "--retry":
    retries = int(args[1])
    args = args[2:]

os.makedirs(os.path.dirname(LOCK), exist_ok=True)
while True:
    with open(LOCK, "w") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        result = subprocess.run(["make", "-C", os.path.join(ROOT, "src"), "torirsserver-packs"] + args)
    if result.returncode == 0 or retries <= 0:
        break
    retries -= 1
    print("locked_packs: build failed; retrying in 20 s (%d left)" % retries, flush=True)
    time.sleep(20)
sys.exit(result.returncode)
