from __future__ import print_function

import os
import subprocess


ROOT = r"C:\dev\oldschool-clientc"
CLIENT = os.path.join(ROOT, "torirs-profile.exe")
MANIFEST = "build/manifests/osrs239-xp-js5.ini"
CSV = os.path.join(ROOT, "winxp-soft3d-torirs-perf.csv")
LOG = os.path.join(ROOT, "winxp-soft3d-torirs-perf.log")


def main():
    env = dict(os.environ)
    env["TORIRS_PERF"] = "1"
    env["TORIRS_PERF_CSV"] = CSV
    env["TORIRS_PERF_WINDOW"] = "100"
    env["TORIRS_MAX_FRAMES"] = "1000"
    env["TORIRS_ESC_QUIT"] = "1"

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
        rc = subprocess.call(args, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    finally:
        log.close()
    print("TORIRS_PERF client exit=%d" % rc)
    for path in (CSV, CSV + ".windows.csv", LOG):
        if os.path.isfile(path):
            print("output %s: %d bytes" % (path, os.path.getsize(path)))
        else:
            print("missing output: " + path)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
