"""Sample a running client's CPU consumption on the XP box.

CPU time, not CPU percent from a task manager: GetProcessTimes returns the
kernel and user time the process has actually been scheduled for, as a
monotonic total since it started. Two reads and the wall gap between them give
the rate over exactly that window, which is the only form of this measurement
that means anything when both clients are frame-capped -- at a 50 fps cap the
frame rate is pinned by the pacer, so the thing that separates a fast client
from a slow one is how much CPU it burns to hold that cap, not the cap.

Attaches to a pid rather than launching, for the same reason the memory reader
does: the Java client has to be logged in by hand or by script first, and the
window that matters is the steady-state one after the world has loaded, not the
boot.

Reports both a share of ONE core and a share of ALL cores, because those differ
here and the interesting number depends on the question. A single-threaded
renderer pinned at 100% of one core on a dual-core box reads as 50% "CPU usage"
in Task Manager while being completely saturated.

Job file C:\\dev\\memjob.json supplies `label`; pid comes from
C:\\dev\\<label>.pid. `window_sec` (default 30) sets the sampling window.
"""

import ctypes
import json
import os
import sys
import time
from ctypes import wintypes

JOB_PATH = r"C:\dev\memjob.json"

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010

kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi


class FILETIME(ctypes.Structure):
    _fields_ = [("dwLowDateTime", wintypes.DWORD),
                ("dwHighDateTime", wintypes.DWORD)]


class SYSTEM_INFO(ctypes.Structure):
    _fields_ = [
        ("wProcessorArchitecture", wintypes.WORD),
        ("wReserved", wintypes.WORD),
        ("dwPageSize", wintypes.DWORD),
        ("lpMinimumApplicationAddress", ctypes.c_void_p),
        ("lpMaximumApplicationAddress", ctypes.c_void_p),
        ("dwActiveProcessorMask", ctypes.POINTER(ctypes.c_ulong)),
        ("dwNumberOfProcessors", wintypes.DWORD),
        ("dwProcessorType", wintypes.DWORD),
        ("dwAllocationGranularity", wintypes.DWORD),
        ("wProcessorLevel", wintypes.WORD),
        ("wProcessorRevision", wintypes.WORD),
    ]


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


def filetime_to_seconds(ft):
    """FILETIME is in 100-nanosecond ticks."""
    return ((ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 1e7


def process_cpu_seconds(handle):
    creation = FILETIME()
    exited = FILETIME()
    kernel = FILETIME()
    user = FILETIME()
    ok = kernel32.GetProcessTimes(
        handle,
        ctypes.byref(creation), ctypes.byref(exited),
        ctypes.byref(kernel), ctypes.byref(user))
    if not ok:
        return None
    return filetime_to_seconds(kernel), filetime_to_seconds(user)


def main():
    fh = open(JOB_PATH, "rb")
    try:
        job = json.loads(fh.read().decode("utf-8"))
    finally:
        fh.close()

    label = job.get("label", "run")
    window = float(job.get("window_sec", 30))

    pid_path = os.path.join(r"C:\dev", label + ".pid")
    if not os.path.exists(pid_path):
        print("no pid file: " + pid_path)
        return 2
    ph = open(pid_path, "rb")
    try:
        pid = int(ph.read().decode("ascii").strip())
    finally:
        ph.close()

    info = SYSTEM_INFO()
    kernel32.GetSystemInfo(ctypes.byref(info))
    cores = int(info.dwNumberOfProcessors)

    handle = kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not handle:
        print("[%s] pid %d is gone" % (label, pid))
        return 2

    first = process_cpu_seconds(handle)
    if first is None:
        kernel32.CloseHandle(handle)
        print("[%s] GetProcessTimes failed" % label)
        return 2
    t0 = time.time()

    time.sleep(window)

    second = process_cpu_seconds(handle)
    t1 = time.time()
    if second is None:
        kernel32.CloseHandle(handle)
        print("[%s] process went away during the window" % label)
        return 2

    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
    kernel32.CloseHandle(handle)

    wall = t1 - t0
    kernel_s = second[0] - first[0]
    user_s = second[1] - first[1]
    total_s = kernel_s + user_s
    mb = 1024.0 * 1024.0

    print("[%s] pid %d, %d logical core(s)" % (label, pid, cores))
    print("[%s] window             : %8.2f s wall" % (label, wall))
    print("[%s] cpu user           : %8.2f s" % (label, user_s))
    print("[%s] cpu kernel         : %8.2f s" % (label, kernel_s))
    print("[%s] cpu total          : %8.2f s" % (label, total_s))
    print("[%s] %% of one core      : %8.1f %%" % (label, 100.0 * total_s / wall))
    print("[%s] %% of all cores     : %8.1f %%" % (label, 100.0 * total_s / (wall * cores)))
    print("[%s] working set now    : %8.2f MB" % (label, counters.WorkingSetSize / mb))
    print("[%s] peak working set   : %8.2f MB" % (label, counters.PeakWorkingSetSize / mb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
