from __future__ import print_function

import os
import subprocess
import time


root = r"C:\dev\oldschool-clientc"
exe = os.path.join(root, "torirs-profile.exe")
log_path = os.path.join(root, "torirs-profile-test.log")
log = open(log_path, "wb")
args = [
    exe,
    "--manifest",
    "build/manifests/osrs239-xp-js5.ini",
    "--user",
    "testc",
    "--pass",
    "test",
    "--soft3d",
]
try:
    proc = subprocess.Popen(args, cwd=root, stdout=log, stderr=subprocess.STDOUT)
finally:
    log.close()

time.sleep(5.0)
if proc.poll() is None:
    print("profile client running pid=%d" % proc.pid)
    raise SystemExit(0)
print("profile client exited code=%s; log=%s" % (proc.returncode, log_path))
raise SystemExit(proc.returncode or 1)
