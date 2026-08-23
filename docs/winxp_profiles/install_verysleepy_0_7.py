from __future__ import print_function

import os
import subprocess
import sys


ROOT = r"C:\dev\oldschool-clientc"
SETUP = os.path.join(
    ROOT, "toolchains", "winxp_profiles", "verysleepy_0_7_setup.exe"
)
DESTINATION = os.path.join(
    ROOT, "toolchains", "winxp_profiles", "verysleepy_0_7_exact"
)


def main():
    if not os.path.isfile(SETUP):
        print("missing installer: " + SETUP)
        return 2
    if not os.path.isdir(DESTINATION):
        os.makedirs(DESTINATION)

    args = [
        SETUP,
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        "/SP-",
        "/DIR=" + DESTINATION,
    ]
    print("installing Very Sleepy 0.7 to " + DESTINATION)
    rc = subprocess.call(args, cwd=ROOT)
    print("installer exit=%d" % rc)
    for current, _dirs, files in os.walk(DESTINATION):
        for filename in files:
            path = os.path.join(current, filename)
            print("%s\t%d" % (path, os.path.getsize(path)))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
