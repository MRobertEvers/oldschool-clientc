"""Peak-memory runner for the XP box (rpdxp), driven by a job file.

The Windows 11 half of this measurement is tools/mem/measure_peak.ps1, and the
two have to agree or the comparison they exist to support is worthless. They
read the SAME kernel counters: .NET's Process.PeakWorkingSet64 and
PeakPagedMemorySize64 are thin wrappers over PROCESS_MEMORY_COUNTERS, which is
what GetProcessMemoryInfo fills in below. Nothing here estimates anything.

Peak is read from a LIVE process. Once a process exits its counters are gone --
not zeroed-but-readable, gone with the handle -- so the poll loop IS the
measurement, on this platform exactly as on the other.

Python on this box is 3.2: no f-strings, no subprocess.run, no pathlib. Keep it
that way; a syntax error here surfaces as an empty /scripts/run body with no
traceback, which is a slow way to learn about an f-string.

Job file (C:\\dev\\memjob.json), written by the caller before /scripts/run:

    {"exe": "C:\\\\dev\\\\...\\\\torirs.exe",
     "args": ["--manifest", "...", "--soft3d"],
     "env": {"TORIRS_MAX_FRAMES": "900"},
     "duration_sec": 0,          # 0 = wait for exit
     "label": "xp-c-soft3d",
     "cwd": "C:\\\\dev\\\\oldschool-clientc"}
"""

import ctypes
import json
import os
import subprocess
import sys
import time
from ctypes import wintypes

JOB_PATH = r"C:\dev\memjob.json"

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010


class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("PageFaultCount", wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]


def read_counters(handle):
    """Return the counter struct, or None once the process is unreadable."""
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    # psapi.dll, not kernel32: XP predates the kernel32 forwarders.
    ok = ctypes.windll.psapi.GetProcessMemoryInfo(
        handle, ctypes.byref(counters), counters.cb)
    if not ok:
        return None
    return counters


def main():
    fh = open(JOB_PATH, "rb")
    try:
        job = json.loads(fh.read().decode("utf-8"))
    finally:
        fh.close()

    exe = job["exe"]
    args = job.get("args", [])
    label = job.get("label", "run")
    duration = float(job.get("duration_sec", 0))
    cwd = job.get("cwd") or os.path.dirname(exe)

    env = dict(os.environ)
    for key, value in job.get("env", {}).items():
        env[str(key)] = str(value)

    if not os.path.exists(exe):
        print("no such executable: " + exe)
        return 2

    print("[%s] %s %s" % (label, exe, " ".join(args)))
    if job.get("env"):
        print("[%s] env: %s" % (
            label,
            " ".join("%s=%s" % (k, v) for k, v in job["env"].items())))

    log_path = os.path.join(cwd, label + ".clientlog")
    log = open(log_path, "wb")

    started = time.time()
    proc = subprocess.Popen(
        [exe] + list(args),
        cwd=cwd,
        env=env,
        stdout=log,
        stderr=subprocess.STDOUT)

    handle = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, proc.pid)
    if not handle:
        proc.kill()
        log.close()
        print("OpenProcess failed for pid %d" % proc.pid)
        return 2

    peak_ws = 0
    peak_pagefile = 0
    max_ws = 0
    samples = 0
    stopped_by_timer = False

    try:
        while True:
            if proc.poll() is not None:
                break

            counters = read_counters(handle)
            if counters is not None and counters.WorkingSetSize > 0:
                samples += 1
                if counters.PeakWorkingSetSize > peak_ws:
                    peak_ws = counters.PeakWorkingSetSize
                if counters.PeakPagefileUsage > peak_pagefile:
                    peak_pagefile = counters.PeakPagefileUsage
                if counters.WorkingSetSize > max_ws:
                    max_ws = counters.WorkingSetSize

            if duration > 0 and (time.time() - started) >= duration:
                stopped_by_timer = True
                break

            time.sleep(0.05)

        if stopped_by_timer:
            # The peak is already banked; killing only ends the run.
            try:
                proc.kill()
            except Exception:
                pass

        proc.wait()
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)
        log.close()

    elapsed = time.time() - started
    mb = 1024.0 * 1024.0

    print("")
    print("[%s] exit=%s wall=%.1fs samples=%d" % (
        label,
        "(stopped at %.0fs)" % duration if stopped_by_timer else str(proc.returncode),
        elapsed,
        samples))
    print("[%s] peak working set   : %8.2f MB" % (label, peak_ws / mb))
    print("[%s] peak private bytes : %8.2f MB" % (label, peak_pagefile / mb))
    print("[%s] max sampled WS     : %8.2f MB" % (label, max_ws / mb))
    print("[%s] client log -> %s" % (label, log_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
