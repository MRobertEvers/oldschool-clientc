from __future__ import print_function

import subprocess


for image in ("torirs-profile.exe", "sleepy.exe"):
    code = subprocess.call(["taskkill", "/F", "/IM", image])
    if code not in (0, 128):
        raise SystemExit(code)
    print("%s stopped (taskkill exit %d)" % (image, code))
