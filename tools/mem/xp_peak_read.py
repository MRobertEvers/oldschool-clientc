"""Read the peak memory counters of an already-running process on the XP box.

The companion to xp_launch_detached.py. Reads the same kernel counters as
tools/mem/measure_peak.ps1 and tools/mem/xp_measure_peak.py --
PROCESS_MEMORY_COUNTERS via GetProcessMemoryInfo -- so all three report the
same quantity and the numbers can sit in one table.

Attaching late is sound for the PEAK fields only: PeakWorkingSetSize and
PeakPagefileUsage are high-water marks the kernel has maintained since the
process started, so they already include the boot and the load that happened
before this script existed. WorkingSetSize is a spot reading and is printed as
such -- do not confuse the two.

Job file (C:\\dev\\memjob.json) supplies `label`; the pid comes from
C:\\dev\\<label>.pid, written by the launcher. Set "kill": true in the job to
end the process after reading.
"""

import ctypes
import json
import os
import sys
from ctypes import wintypes

JOB_PATH = r"C:\dev\memjob.json"

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
PROCESS_TERMINATE = 0x0001


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


def main():
    fh = open(JOB_PATH, "rb")
    try:
        job = json.loads(fh.read().decode("utf-8"))
    finally:
        fh.close()

    label = job.get("label", "run")
    pid_path = os.path.join(r"C:\dev", label + ".pid")
    if not os.path.exists(pid_path):
        print("no pid file: " + pid_path)
        return 2

    ph = open(pid_path, "rb")
    try:
        pid = int(ph.read().decode("ascii").strip())
    finally:
        ph.close()

    access = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE
    handle = ctypes.windll.kernel32.OpenProcess(access, False, pid)
    if not handle:
        print("[%s] pid %d is gone (peak counters die with the handle)" % (label, pid))
        return 2

    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    ok = ctypes.windll.psapi.GetProcessMemoryInfo(
        handle, ctypes.byref(counters), counters.cb)
    if not ok:
        ctypes.windll.kernel32.CloseHandle(handle)
        print("[%s] GetProcessMemoryInfo failed for pid %d" % (label, pid))
        return 2

    mb = 1024.0 * 1024.0
    print("[%s] pid %d" % (label, pid))
    print("[%s] peak working set   : %8.2f MB" % (label, counters.PeakWorkingSetSize / mb))
    print("[%s] peak private bytes : %8.2f MB" % (label, counters.PeakPagefileUsage / mb))
    print("[%s] working set now    : %8.2f MB" % (label, counters.WorkingSetSize / mb))
    print("[%s] private bytes now  : %8.2f MB" % (label, counters.PagefileUsage / mb))

    if job.get("kill"):
        ctypes.windll.kernel32.TerminateProcess(handle, 0)
        print("[%s] terminated" % label)

    ctypes.windll.kernel32.CloseHandle(handle)
    return 0


if __name__ == "__main__":
    sys.exit(main())
