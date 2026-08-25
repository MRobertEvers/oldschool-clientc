"""Register Dr. Watson as the postmortem debugger and clear its old log.

The client exits 0xC0000005 on this box with nothing in its stdout log, and the
existing drwtsn32.log is from 2005 -- nothing is catching the fault, so there is
no faulting address to look up in the PDB. `drwtsn32 -i` writes the
AeDebug\\Debugger key that makes XP hand the crash to Dr. Watson, which then
records the faulting module, the offset, and a stack walk.

Clearing the 6.8 MB 2005 log first is not tidiness: the new entry is appended,
and finding it in that much history is worse than losing a decade-old record of
a machine that no longer exists in that state.
"""

from __future__ import print_function

import os
import subprocess


LOG_DIR = r"C:\Documents and Settings\All Users\Application Data\Microsoft\Dr Watson"
LOG = os.path.join(LOG_DIR, "drwtsn32.log")
DUMP = os.path.join(LOG_DIR, "user.dmp")


def main():
    for path in (LOG, DUMP):
        if os.path.isfile(path):
            try:
                os.remove(path)
                print("removed old " + os.path.basename(path))
            except Exception as exc:
                print("could not remove %s: %s" % (path, exc))

    rc = subprocess.call(["drwtsn32", "-i"])
    print("drwtsn32 -i rc=%d" % rc)

    query = subprocess.Popen(
        [
            "reg",
            "query",
            r"HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    ).communicate()[0]
    print(query.decode("latin-1", "replace"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
