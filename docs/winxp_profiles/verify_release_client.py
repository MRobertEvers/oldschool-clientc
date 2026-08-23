from __future__ import print_function

import hashlib
import os
import subprocess


path = r"C:\dev\oldschool-clientc\torirs.exe"
digest = hashlib.sha256()
with open(path, "rb") as stream:
    while True:
        chunk = stream.read(1024 * 1024)
        if not chunk:
            break
        digest.update(chunk)
print("path=%s" % path)
print("size=%d" % os.path.getsize(path))
print("sha256=%s" % digest.hexdigest().upper())
subprocess.call(["tasklist", "/FI", "IMAGENAME eq torirs.exe"])
