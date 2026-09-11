"""Start the 1,000-frame TORIRS_PERF capture and return immediately.

Same reason as run_verysleepy_detached.py: RemoteProxyDesktopXP kills a script
at --script-timeout (120s), and 1,000 frames on this hardware is longer than
that -- the first attempt was terminated during the client's shutdown, after
the CSV had been written but before the runner could report it.

This is the capture the recorded baseline uses
(docs/winxp_profiles/analysis.md), so the protocol is held fixed: 1,000 frames,
100-frame windows, same manifest and account.

Two details that cost a run each:

  * CREATE_NEW_CONSOLE, not DETACHED_PROCESS. The client links
    --subsystem console:5.01. Detached it has no console at all, and it died
    silently a few frames after `if-opentop: switching root`; under Very
    Sleepy, which starts it normally, the same binary ran the full 60s. Give
    it a console and redirect the handles.

  * The work happens in a --child re-exec that WAITS, so the exit code is
    recoverable. The launcher returns as soon as the child is spawned; the
    child writes its return code to <csv>.rc when the client is done, which is
    also the marker that the run finished rather than still running.
"""

from __future__ import print_function

import os
import subprocess
import sys


ROOT = r"C:\dev\oldschool-clientc"
CLIENT = os.path.join(ROOT, "torirs-profile.exe")
MANIFEST = "build/manifests/osrs239-xp-js5.ini"
CSV = os.path.join(ROOT, "winxp-soft3d-torirs-perf.csv")
WINDOWS = CSV + ".windows.csv"
LOG = os.path.join(ROOT, "winxp-soft3d-torirs-perf.log")
RC = CSV + ".rc"

CREATE_NEW_CONSOLE = 0x00000010
CREATE_NEW_PROCESS_GROUP = 0x00000200


def client_env():
    env = dict(os.environ)
    env["TORIRS_PERF"] = "1"
    env["TORIRS_PERF_CSV"] = CSV
    env["TORIRS_PERF_WINDOW"] = "100"
    env["TORIRS_MAX_FRAMES"] = "1000"
    env["TORIRS_ESC_QUIT"] = "1"
    return env


def run_child():
    args = [
        CLIENT,
        "--manifest",
        MANIFEST,
        "--user",
        "testc",
        "--pass",
        "test",
        "--soft3d",
    ]
    log = open(LOG, "wb")
    try:
        rc = subprocess.call(
            args,
            cwd=ROOT,
            env=client_env(),
            stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=CREATE_NEW_CONSOLE,
        )
    finally:
        log.close()
    handle = open(RC, "wb")
    handle.write(("%d\n" % rc).encode("ascii"))
    handle.close()
    return rc


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--child":
        return run_child()

    for path in (CSV, WINDOWS, RC):
        if os.path.isfile(path):
            os.remove(path)
            print("removed stale " + os.path.basename(path))

    proc = subprocess.Popen(
        [sys.executable, os.path.abspath(__file__), "--child"],
        cwd=ROOT,
        env=dict(os.environ),
        creationflags=CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
    )
    print("detached runner pid=%d, 1000 frames" % proc.pid)
    print("done when %s appears" % os.path.basename(RC))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
