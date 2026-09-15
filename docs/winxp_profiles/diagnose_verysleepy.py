from __future__ import print_function

import os
import subprocess
import time


root = r"C:\dev\oldschool-clientc"
sleepy = os.path.join(
    root, "toolchains", "winxp_profiles", "verysleepy_0_7_exact", "sleepy.exe"
)
client = os.path.join(root, "torirs-profile.exe")
command = (
    "%s --manifest build/manifests/osrs239-xp-js5.ini "
    "--user testc --pass test --soft3d" % client
)
args = [
    sleepy,
    "/r",
    command,
    "/o",
    os.path.join(root, "winxp-sleepy-diagnostic.sleepy"),
    "/t",
    "10",
]
print("diagnostic command: " + " ".join(args))
log_path = os.path.join(root, "verysleepy-exact-diagnostic.log")
log = open(log_path, "wb")
try:
    proc = subprocess.Popen(args, cwd=root, stdout=log, stderr=subprocess.STDOUT)
finally:
    log.close()
print("sleepy pid=%d diagnostic_log=%s" % (proc.pid, log_path))
