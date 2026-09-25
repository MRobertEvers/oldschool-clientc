"""Start the Very Sleepy Soft3D capture and return immediately.

run_verysleepy_profile.py waits for the profiler, which makes the whole capture
one HTTP request to RemoteProxyDesktopXP -- and that server kills a script at
its --script-timeout (120s by default, reported by /scripts/list). A 60s
sampling run plus client boot plus symbol resolution against a 10MB PDB does
not reliably fit, and a killed script leaves an orphaned sleepy.exe holding the
trace file open.

So: launch detached, exit, and let the caller poll /fs/list for the outputs.
check_profile_outputs.py reports when they have landed.
"""

from __future__ import print_function

import os
import subprocess
import sys


ROOT = r"C:\dev\oldschool-clientc"
TOOL_ROOT = os.path.join(ROOT, "toolchains", "winxp_profiles")
SLEEPY = os.path.join(TOOL_ROOT, "verysleepy_0_7_exact", "sleepy.exe")
CLIENT = os.path.join(ROOT, "torirs-profile.exe")
MANIFEST = "build/manifests/osrs239-xp-js5.ini"
TRACE = os.path.join(ROOT, "winxp-soft3d-60s.sleepy")
PERF_CSV = os.path.join(ROOT, "winxp-soft3d-60s-perf.csv")
PERF_WINDOWS = PERF_CSV + ".windows.csv"

SECONDS = "60"

DETACHED_PROCESS = 0x00000008
CREATE_NEW_PROCESS_GROUP = 0x00000200


def require_file(path):
    if not os.path.isfile(path):
        print("missing required file: " + path)
        raise SystemExit(2)


def main():
    require_file(SLEEPY)
    require_file(CLIENT)
    require_file(os.path.splitext(CLIENT)[0] + ".pdb")
    require_file(os.path.join(ROOT, MANIFEST.replace("/", os.sep)))

    cache = os.path.join(ROOT, "cache.osrs239.sparse")
    if not os.path.isdir(cache):
        os.mkdir(cache)

    # Clear last run's outputs so polling cannot mistake them for this run's.
    for path in (TRACE, PERF_CSV, PERF_WINDOWS):
        if os.path.isfile(path):
            os.remove(path)
            print("removed stale " + os.path.basename(path))

    # Deliberately NOT TORIRS_PERF=1.
    #
    # soft3d_execute_measured brackets every render command with two
    # clock_gettime calls, and there are ~1,795 commands a frame, so the
    # instrumentation puts ~3,590 clock reads a frame into the very profile
    # that is supposed to say where the frame goes. It shows up as time in
    # the timer path and, worse, inflates every r_* caller around it.
    #
    # The counters come from run_torirs_perf_detached.py instead, which is a
    # separate 1,000-frame run. Sleepy's job here is the shipping
    # configuration and nothing else.
    env = dict(os.environ)
    env.pop("TORIRS_PERF", None)
    env.pop("TORIRS_PERF_CSV", None)
    env["TORIRS_ESC_QUIT"] = "1"

    # The XP paths contain no spaces. Very Sleepy 0.7 passes this string
    # directly to CreateProcess; nested executable quotes produce error 123.
    client_command = (
        "%s --manifest %s --user testc --pass test --soft3d"
        % (CLIENT, MANIFEST)
    )
    args = [SLEEPY, "/r", client_command, "/o", TRACE, "/t", SECONDS, "/q"]

    print("Very Sleepy command: " + " ".join(args))
    proc = subprocess.Popen(
        args,
        cwd=ROOT,
        env=env,
        creationflags=DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
    )
    print("detached sleepy.exe pid=%d, capturing %ss" % (proc.pid, SECONDS))
    print("poll with check_profile_outputs.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
