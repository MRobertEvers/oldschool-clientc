from __future__ import print_function

import subprocess


for image in ("sleepy.exe", "torirs-profile.exe", "torirs.exe"):
    print("--- %s ---" % image)
    subprocess.call(["tasklist", "/FI", "IMAGENAME eq " + image])
