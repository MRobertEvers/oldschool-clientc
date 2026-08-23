from __future__ import print_function

import os


paths = [
    r"C:\dev\oldschool-clientc\toolchains",
    r"C:\dev\oldschool-clientc\toolchains\winxp_profiles",
    r"C:\dev\oldschool-clientc\toolchains\winxp_profiles\verysleepy_0_7",
]

for path in paths:
    if not os.path.isdir(path):
        os.mkdir(path)
    print("ready: " + path)
