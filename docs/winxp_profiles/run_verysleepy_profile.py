from __future__ import print_function

import os
import subprocess
import sys
import time


ROOT = r"C:\dev\oldschool-clientc"
TOOL_ROOT = os.path.join(ROOT, "toolchains", "winxp_profiles")
SLEEPY = os.path.join(TOOL_ROOT, "verysleepy_0_7_exact", "sleepy.exe")
CLIENT = os.path.join(ROOT, "torirs-profile.exe")
MANIFEST = "build/manifests/osrs239-xp-js5.ini"
TRACE = os.path.join(ROOT, "winxp-soft3d-60s.sleepy")
PERF_CSV = os.path.join(ROOT, "winxp-soft3d-60s-perf.csv")
PERF_WINDOWS = PERF_CSV + ".windows.csv"


def require_file(path):
    if not os.path.isfile(path):
        print("missing required file: " + path)
        raise SystemExit(2)


def main():
    require_file(SLEEPY)
    require_file(CLIENT)
    require_file(os.path.splitext(CLIENT)[0] + ".pdb")
    require_file(os.path.join(ROOT, MANIFEST.replace("/", os.sep)))

    cache = os.path.join(ROOT, "cache.osrs239.sparse")
    if not os.path.isdir(cache):
        os.mkdir(cache)

    env = dict(os.environ)
    env["TORIRS_PERF"] = "1"
    env["TORIRS_PERF_CSV"] = PERF_CSV
    env["TORIRS_PERF_WINDOW"] = "100"
    env["TORIRS_ESC_QUIT"] = "1"

    # The XP paths contain no spaces. Very Sleepy 0.7 passes this string
    # directly to CreateProcess; nested executable quotes produce error 123.
    client_command = (
        "%s --manifest %s --user testc --pass test --soft3d"
        % (CLIENT, MANIFEST)
    )
    args = [
        SLEEPY,
        "/r",
        client_command,
        "/o",
        TRACE,
        "/t",
        "60",
        "/q",
    ]

    print("Very Sleepy command: " + " ".join(args))
    print("Starting 60 second Soft3D capture...")
    sys.stdout.flush()
    started = time.time()
    proc = subprocess.Popen(args, cwd=ROOT, env=env)
    rc = proc.wait()
    elapsed = time.time() - started
    print("Very Sleepy exit=%d elapsed=%.2fs" % (rc, elapsed))

    for path in (TRACE, PERF_CSV, PERF_WINDOWS):
        if os.path.isfile(path):
            print("output %s: %d bytes" % (path, os.path.getsize(path)))
        else:
            print("missing output: " + path)

    if rc != 0 or not os.path.isfile(TRACE):
        return rc or 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
