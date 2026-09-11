from __future__ import print_function

import os
import subprocess


root = r"C:\dev\oldschool-clientc"
sleepy = os.path.join(
    root, "toolchains", "winxp_profiles", "verysleepy_0_7_exact", "sleepy.exe"
)
proc = subprocess.Popen([sleepy, "/h"], cwd=os.path.dirname(sleepy))
print("sleepy help pid=%d" % proc.pid)
