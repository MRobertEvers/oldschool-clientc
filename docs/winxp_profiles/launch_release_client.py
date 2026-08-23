from __future__ import print_function

import os
import subprocess


ROOT = r"C:\dev\oldschool-clientc"
CLIENT = os.path.join(ROOT, "torirs.exe")
LOG = os.path.join(ROOT, "client.log")
args = [
    CLIENT,
    "--manifest",
    "build/manifests/osrs239-xp-js5.ini",
    "--user",
    "testc",
    "--pass",
    "test",
    "--soft3d",
]
log = open(LOG, "ab")
try:
    proc = subprocess.Popen(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
finally:
    log.close()
print("release client pid=%d log=%s" % (proc.pid, LOG))
