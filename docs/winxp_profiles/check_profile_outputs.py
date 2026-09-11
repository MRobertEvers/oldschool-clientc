"""Report whether the detached Very Sleepy capture has finished.

Polled by the host between /scripts/run calls; see run_verysleepy_detached.py
for why the capture is not run synchronously.
"""

from __future__ import print_function

import os
import subprocess


ROOT = r"C:\dev\oldschool-clientc"
TRACE = os.path.join(ROOT, "winxp-soft3d-60s.sleepy")
PERF_CSV = os.path.join(ROOT, "winxp-soft3d-60s-perf.csv")
PERF_WINDOWS = PERF_CSV + ".windows.csv"
# The 1,000-frame capture, which is a separate run from the 60s sampling one.
RUN_CSV = os.path.join(ROOT, "winxp-soft3d-torirs-perf.csv")
RUN_WINDOWS = RUN_CSV + ".windows.csv"
RUN_LOG = os.path.join(ROOT, "winxp-soft3d-torirs-perf.log")
RUN_RC = RUN_CSV + ".rc"


def running(image):
    try:
        out = subprocess.Popen(
            ["tasklist", "/FI", "IMAGENAME eq " + image],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        ).communicate()[0]
    except Exception as exc:  # tasklist is absent on XP Home
        return "unknown (%s)" % exc
    # Not "mbcs": that codec rejects an errors= argument on the Python 3.2
    # that ships in this XP image ("mbcs encoding does not support
    # errors='replace'"). latin-1 cannot fail and we only substring-match an
    # ASCII image name.
    try:
        out = out.decode("latin-1", "replace")
    except AttributeError:
        pass
    return "yes" if image.lower() in out.lower() else "no"


def main():
    print("sleepy.exe running: " + running("sleepy.exe"))
    print("torirs-profile.exe running: " + running("torirs-profile.exe"))
    for path in (TRACE, PERF_CSV, PERF_WINDOWS, RUN_CSV, RUN_WINDOWS, RUN_LOG):
        if os.path.isfile(path):
            print("output %s: %d bytes" % (os.path.basename(path), os.path.getsize(path)))
        else:
            print("pending: " + os.path.basename(path))
    # Written by run_torirs_perf_detached.py's child once the client returns,
    # so its presence separates "still running" from "exited" and its contents
    # say which -- a silent exit and a crash look identical from tasklist.
    if os.path.isfile(RUN_RC):
        handle = open(RUN_RC, "rb")
        try:
            print("run exit code: " + handle.read().decode("latin-1").strip())
        finally:
            handle.close()
    else:
        print("pending: run exit code")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
