"""Start gdbserver on the XP box hosting the client, and return immediately.

The host then attaches with the MODERN gdb from toolchains/mingw32 -- see
lib/gdb-7.6.1-mingw32-xp.zip for why the two versions are split. gdbserver 7.6.1
is the only one of the pair that loads on XP; the winlibs one imports
GetFinalPathNameByHandleA and dies at 0xC0000139. All symbol reading happens on
the host, which is what lets a 2013 stub debug DWARF 5.

gdbserver itself is launched DETACHED_PROCESS, and that is the entire point: the
inferior inherits gdbserver's (absent) console, which is the condition the
0xC0000005 needs. Launched with a console the client has survived every run.

--once so gdbserver exits after the session instead of waiting for a second
connection, which would leave the port held for the next attempt.
"""

from __future__ import print_function

import os
import subprocess


ROOT = r"C:\dev\oldschool-clientc"
GDBSERVER = r"C:\dev\gdbtest\gdbserver761.exe"
EXE = os.path.join(ROOT, "torirs-gdb.exe")
MANIFEST = "build/manifests/osrs239-xp-js5.ini"
PORT = "2345"
LOG = os.path.join(ROOT, "gdbserver.log")

DETACHED_PROCESS = 0x00000008
CREATE_NEW_PROCESS_GROUP = 0x00000200


def main():
    subprocess.call(["taskkill", "/F", "/IM", "gdbserver761.exe"])
    subprocess.call(["taskkill", "/F", "/IM", "torirs-gdb.exe"])

    if not os.path.isfile(EXE):
        print("missing " + EXE)
        return 1

    env = dict(os.environ)
    env["TORIRS_MAX_FRAMES"] = "150"
    env["TORIRS_ESC_QUIT"] = "1"

    args = [
        GDBSERVER,
        "--once",
        "0.0.0.0:" + PORT,
        EXE,
        "--manifest",
        MANIFEST,
        "--user",
        "testc",
        "--pass",
        "test",
        "--soft3d",
    ]
    log = open(LOG, "wb")
    proc = subprocess.Popen(
        args,
        cwd=ROOT,
        env=env,
        stdout=log,
        stderr=subprocess.STDOUT,
        creationflags=DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
    )
    print("gdbserver pid=%d listening on %s" % (proc.pid, PORT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
