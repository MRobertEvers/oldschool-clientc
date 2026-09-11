"""Run one client binary until it crashes or hits a small frame cap.

    /scripts/run?name=run_crash_probe.py   (exe name read from crash_probe.txt)

The 0xC0000005 happens within ~20s of launch, a few frames after
`if-opentop: switching root 161 -> 548`, so unlike the 1,000-frame profiling
run this fits inside the 120s RPD script timeout and can be run synchronously
-- which matters, because the whole point is to see the exit code and the tail
of stderr together.

The launch is DETACHED_PROCESS on purpose. Every crash so far came from a
detached launch (2 of 2) and every console-attached launch survived (4 of 4),
which points at the client doing something with a console handle it does not
have rather than at anything frame-related. Redirecting stdout to a file is not
the same thing as having a console: the handle is valid either way, what
changes is whether a console is attached to the process at all.

The frame cap is deliberately small. A run that reaches it has survived the
window the crash lives in, and waiting out another 900 frames only costs
wall-clock.
"""

from __future__ import print_function

import os
import subprocess


ROOT = r"C:\dev\oldschool-clientc"
MANIFEST = "build/manifests/osrs239-xp-js5.ini"
NAME_FILE = os.path.join(ROOT, "crash_probe.txt")
LOG = os.path.join(ROOT, "crash_probe.log")
FRAMES = "150"
DETACHED_PROCESS = 0x00000008


def main():
    exe_name = "torirs-profile.exe"
    if os.path.isfile(NAME_FILE):
        handle = open(NAME_FILE, "rb")
        try:
            exe_name = handle.read().decode("latin-1").strip() or exe_name
        finally:
            handle.close()

    exe = os.path.join(ROOT, exe_name)
    if not os.path.isfile(exe):
        print("missing " + exe)
        return 1

    env = dict(os.environ)
    env["TORIRS_MAX_FRAMES"] = FRAMES
    env["TORIRS_ESC_QUIT"] = "1"

    log = open(LOG, "wb")
    try:
        proc = subprocess.Popen(
            [exe, "--manifest", MANIFEST, "--user", "testc", "--pass", "test", "--soft3d"],
            cwd=ROOT,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=DETACHED_PROCESS,
        )
        proc.wait()
    finally:
        log.close()
    handle = open(LOG, "rb")
    try:
        out = handle.read().decode("latin-1", "replace")
    finally:
        handle.close()

    print("exe=%s rc=%d (0x%08X)" % (exe_name, proc.returncode, proc.returncode & 0xFFFFFFFF))
    lines = out.splitlines()
    print("--- last 25 lines of %d ---" % len(lines))
    for line in lines[-25:]:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
