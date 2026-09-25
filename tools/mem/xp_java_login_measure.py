"""Launch the Java client on the XP box, log it in, and report its peak memory.

The Java client has no user/pass on its command line, so a logged-in
measurement means typing at it. Doing that over rpdxp's /input endpoint works
but costs a screenshot-and-look round trip per keystroke group; doing it here,
on the box, costs nothing and is repeatable -- which matters more, because a
memory baseline nobody can re-run is an anecdote.

Foreground stealing is allowed here and not from the controlling machine: this
runs as the interactive user on the same desktop as the client, so
SetForegroundWindow succeeds where a remote caller's would be refused.

Coordinates are CLIENT-relative and converted with ClientToScreen, not baked as
screen coordinates: the applet canvas is a fixed 765x503 but the frame around
it is not, and a window that opens at a different spot would otherwise be
clicked in the wrong place with no symptom except a login that never happens.

Counters are the same PROCESS_MEMORY_COUNTERS fields the other two harnesses
read (tools/mem/measure_peak.ps1, tools/mem/xp_measure_peak.py).

Job file C:\\dev\\memjob.json adds to the usual keys:
    "username": "memxpj2", "password": "a",
    "settle_sec": 45      # how long to keep polling after login
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

CREATE_NEW_CONSOLE = 0x00000010
CREATE_NEW_PROCESS_GROUP = 0x00000200

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
KEYEVENTF_KEYUP = 0x0002
VK_RETURN = 0x0D

# Client-area coordinates on the 765x503 title screen.
EXISTING_USER_XY = (462, 286)
# The prompt's own Login button. Pressing Return after the password is the
# obvious way to submit and it does not work reliably here -- the first
# automated run left a filled-in form sitting unsubmitted, and a form that
# looks complete but never logged in reports the LOGIN SCREEN's memory as
# though it were the world's. Clicking the button is unambiguous.
LOGIN_BUTTON_XY = (302, 316)

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi


class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


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


def find_client_window(timeout_sec):
    """Wait for the client's frame. It is titled 'Jagex' in every rev here."""
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        hwnd = user32.FindWindowA(None, b"Jagex")
        if hwnd:
            return hwnd
        time.sleep(0.5)
    return 0


def click_client(hwnd, cx, cy):
    point = POINT(cx, cy)
    user32.ClientToScreen(hwnd, ctypes.byref(point))
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.4)
    user32.SetCursorPos(point.x, point.y)
    time.sleep(0.2)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.05)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(0.4)


def tap(vk):
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.03)
    user32.keybd_event(vk, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.05)


def type_text(text):
    """Letters and digits only -- which is all a LostCity dev account needs.

    VkKeyScan would be the general answer, but it also drags in shift state
    handling for punctuation nobody here types, and a wrong shift state is a
    silently different password."""
    for ch in text:
        if ch.isdigit() or ch.isupper():
            tap(ord(ch))
        elif ch.islower():
            tap(ord(ch.upper()))
        else:
            raise ValueError("unsupported character in credentials: " + ch)


def read_counters(handle):
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
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
    cwd = job.get("cwd") or os.path.dirname(exe)
    username = job["username"]
    password = job["password"]
    settle = float(job.get("settle_sec", 45))

    env = dict(os.environ)
    for key, value in job.get("env", {}).items():
        env[str(key)] = str(value)

    log_path = os.path.join(cwd, label + ".clientlog")
    log = open(log_path, "wb")

    print("[%s] %s %s" % (label, exe, " ".join(args)))
    started = time.time()
    # CREATE_NEW_CONSOLE is load-bearing when detaching: a child that shares
    # this script's console dies with it the moment rpdxp reaps the script, and
    # the symptom is not an error -- it is a client that was demonstrably
    # logged in a minute ago and is simply gone when you come back to read its
    # counters, taking the peak with it.
    proc = subprocess.Popen(
        [exe] + list(args), cwd=cwd, env=env,
        stdout=log, stderr=subprocess.STDOUT,
        creationflags=CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP)

    handle = kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, proc.pid)
    if not handle:
        proc.kill()
        log.close()
        print("[%s] OpenProcess failed" % label)
        return 2

    peak_ws = 0
    peak_pagefile = 0

    def sample():
        nonlocal peak_ws, peak_pagefile
        counters = read_counters(handle)
        if counters is not None and counters.WorkingSetSize > 0:
            if counters.PeakWorkingSetSize > peak_ws:
                peak_ws = counters.PeakWorkingSetSize
            if counters.PeakPagefileUsage > peak_pagefile:
                peak_pagefile = counters.PeakPagefileUsage

    hwnd = find_client_window(40)
    if not hwnd:
        proc.kill()
        log.close()
        kernel32.CloseHandle(handle)
        print("[%s] no 'Jagex' window appeared" % label)
        return 2
    print("[%s] window found after %.1fs" % (label, time.time() - started))

    # The title screen has to finish loading before the button is there to hit.
    # It is drawn from the jag archives, which come over the wire, so this is
    # not instant even on a LAN.
    deadline = time.time() + 20
    while time.time() < deadline:
        sample()
        time.sleep(0.1)

    click_client(hwnd, EXISTING_USER_XY[0], EXISTING_USER_XY[1])
    type_text(username)
    tap(VK_RETURN)
    type_text(password)
    time.sleep(0.5)
    click_client(hwnd, LOGIN_BUTTON_XY[0], LOGIN_BUTTON_XY[1])
    print("[%s] credentials submitted at %.1fs" % (label, time.time() - started))

    if job.get("detach"):
        # rpdxp kills a /scripts/run at 120 s, and a client that is still
        # climbing when the script dies reports a peak that is only "as high as
        # it got before the timeout" -- which is how the first run of this
        # returned 79 MB for a session that reaches 96 MB. Hand the pid off and
        # let tools/mem/xp_peak_read.py collect whenever the session has
        # actually settled; the kernel keeps the high-water mark either way.
        pid_path = os.path.join(r"C:\dev", label + ".pid")
        ph = open(pid_path, "wb")
        ph.write(str(proc.pid).encode("ascii"))
        ph.close()
        kernel32.CloseHandle(handle)
        log.close()
        print("[%s] logged in and detached, pid=%d -> %s" % (label, proc.pid, pid_path))
        print("[%s] read the peak later with xppeak.py" % label)
        return 0

    deadline = time.time() + settle
    while time.time() < deadline:
        sample()
        if proc.poll() is not None:
            break
        time.sleep(0.05)

    sample()
    counters = read_counters(handle)
    mb = 1024.0 * 1024.0

    print("")
    print("[%s] wall=%.1fs" % (label, time.time() - started))
    print("[%s] peak working set   : %8.2f MB" % (label, peak_ws / mb))
    print("[%s] peak private bytes : %8.2f MB" % (label, peak_pagefile / mb))
    if counters is not None:
        print("[%s] working set now    : %8.2f MB" % (label, counters.WorkingSetSize / mb))
    print("[%s] client log -> %s" % (label, log_path))

    if job.get("kill", True):
        try:
            proc.kill()
        except Exception:
            pass
    kernel32.CloseHandle(handle)
    log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
