"""Start a client on the XP box and return immediately with its pid.

Why this is separate from xp_measure_peak.py: the Java client cannot be driven
to a logged-in state by its own command line -- it has no user/pass arguments
-- so the login has to be typed at it through rpdxp's /input endpoint while it
runs. A script that both launches and polls owns the process for the whole
window and cannot be interleaved with that, and /scripts/run is capped at 120
seconds regardless.

Splitting it costs nothing in accuracy. PeakWorkingSetSize is maintained by the
kernel from process start, so a reader that attaches after the interesting part
still sees the peak of the whole run -- unlike WorkingSetSize, which is only
ever "now". That is the property this whole arrangement leans on.

CREATE_NEW_CONSOLE, not DETACHED_PROCESS: a console-less child dies silently
here (the same reason docs/winxp_profiles/run_torirs_perf_detached.py says so).

Job file is the same C:\\dev\\memjob.json the poller reads; this uses exe,
args, env, cwd, label. Writes the pid to C:\\dev\\<label>.pid.
"""

import json
import os
import subprocess
import sys

JOB_PATH = r"C:\dev\memjob.json"
CREATE_NEW_CONSOLE = 0x00000010
CREATE_NEW_PROCESS_GROUP = 0x00000200


def main():
    fh = open(JOB_PATH, "rb")
    try:
        job = json.loads(fh.read().decode("utf-8"))
    finally:
        fh.close()

    exe = job["exe"]
    args = job.get("args", [])
    label = job.get("label", "run")
    cwd = job.get("cwd") or os.path.dirname(exe)

    env = dict(os.environ)
    for key, value in job.get("env", {}).items():
        env[str(key)] = str(value)

    if not os.path.exists(exe):
        print("no such executable: " + exe)
        return 2

    log_path = os.path.join(cwd, label + ".clientlog")
    log = open(log_path, "wb")

    proc = subprocess.Popen(
        [exe] + list(args),
        cwd=cwd,
        env=env,
        stdout=log,
        stderr=subprocess.STDOUT,
        creationflags=CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP)

    pid_path = os.path.join(r"C:\dev", label + ".pid")
    ph = open(pid_path, "wb")
    ph.write(str(proc.pid).encode("ascii"))
    ph.close()

    print("[%s] launched pid=%d" % (label, proc.pid))
    print("[%s] log -> %s" % (label, log_path))
    print("[%s] pid -> %s" % (label, pid_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
