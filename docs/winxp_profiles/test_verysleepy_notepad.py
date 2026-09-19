from __future__ import print_function

import os
import subprocess


root = r"C:\dev\oldschool-clientc"
sleepy = os.path.join(
    root, "toolchains", "winxp_profiles", "verysleepy_0_7", "sleepy.exe"
)
trace = os.path.join(root, "winxp-sleepy-notepad-test.sleepy")
args = [sleepy, "/r", "notepad.exe", "/o", trace, "/t", "3", "/q", "/f"]
print("running: " + " ".join(args))
rc = subprocess.call(args, cwd=root)
print("exit=%d trace=%s size=%d" % (
    rc,
    os.path.isfile(trace),
    os.path.getsize(trace) if os.path.isfile(trace) else 0,
))
raise SystemExit(rc)
