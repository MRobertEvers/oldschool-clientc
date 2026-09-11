"""Sample-profile the Java client on the XP box and leave a readable report.

Answers "what is the Java client actually spending its 49.7% of a core on",
which is the other half of the C-vs-Java CPU comparison: knowing our own hot
path only says where OUR time goes, not which of it the reference client also
pays.

hprof rather than jstack-in-a-loop: hprof is a real sampling profiler built
into JDK 8 (cpu=samples walks the stacks off a timer thread), where jstack
attaches over the attach API and forces a safepoint per sample -- hundreds of
milliseconds each on this box, and safepoint bias on top.

The catch hprof brings is that it writes its report at JVM shutdown, so the
client has to EXIT, not be killed. TerminateProcess would leave no file at all.
GameShell.windowClosing -> shutdown() -> System.exit(0), so WM_CLOSE is a clean
exit and the report lands. That is the whole reason this script closes the
window instead of killing the process.

Job file C:\\dev\\memjob.json: exe, args, cwd, username, password, plus
    "profile_sec": 40      # how long to sample once in-world
    "hprof_file": "C:/dev/mem289/java.hprof.txt"
"""

import ctypes
import json
import os
import subprocess
import sys
import time

JOB_PATH = r"C:\dev\memjob.json"

CREATE_NEW_CONSOLE = 0x00000010
CREATE_NEW_PROCESS_GROUP = 0x00000200

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
KEYEVENTF_KEYUP = 0x0002
VK_RETURN = 0x0D
WM_CLOSE = 0x0010

EXISTING_USER_XY = (462, 286)
LOGIN_BUTTON_XY = (302, 316)

user32 = ctypes.windll.user32


class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


def find_window(timeout_sec):
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
    for ch in text:
        if ch.isdigit() or ch.isupper():
            tap(ord(ch))
        elif ch.islower():
            tap(ord(ch.upper()))
        else:
            raise ValueError("unsupported character: " + ch)


def main():
    fh = open(JOB_PATH, "rb")
    try:
        job = json.loads(fh.read().decode("utf-8"))
    finally:
        fh.close()

    exe = job["exe"]
    base_args = list(job.get("args", []))
    cwd = job.get("cwd")
    label = job.get("label", "java-profile")
    username = job["username"]
    password = job["password"]
    profile_sec = float(job.get("profile_sec", 40))
    hprof_file = job.get("hprof_file", "C:/dev/mem289/java.hprof.txt")

    if os.path.exists(hprof_file):
        os.remove(hprof_file)

    # depth=12 keeps the caller chain long enough to tell "who called the
    # blitter" apart from "the blitter"; interval=10 is 100 samples/s, which
    # over profile_sec gives thousands of samples for a 50 fps loop.
    agent = ("-agentlib:hprof=cpu=samples,interval=10,depth=12,file=%s"
             % hprof_file)
    args = [agent] + base_args

    log_path = os.path.join(cwd, label + ".clientlog")
    log = open(log_path, "wb")

    print("[%s] %s %s" % (label, exe, " ".join(args)))
    proc = subprocess.Popen(
        [exe] + args, cwd=cwd, env=os.environ,
        stdout=log, stderr=subprocess.STDOUT,
        creationflags=CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP)

    hwnd = find_window(60)
    if not hwnd:
        proc.kill()
        log.close()
        print("[%s] no window" % label)
        return 2

    # hprof slows startup, so give the title screen longer than the unprofiled
    # run needs before the button is there to click.
    time.sleep(28)
    click_client(hwnd, EXISTING_USER_XY[0], EXISTING_USER_XY[1])
    type_text(username)
    tap(VK_RETURN)
    type_text(password)
    time.sleep(0.5)
    click_client(hwnd, LOGIN_BUTTON_XY[0], LOGIN_BUTTON_XY[1])
    print("[%s] logged in, sampling for %.0fs" % (label, profile_sec))

    time.sleep(profile_sec)

    # Clean exit so hprof flushes. Killing here would leave no report.
    user32.PostMessageA(hwnd, WM_CLOSE, 0, 0)
    deadline = time.time() + 45
    while time.time() < deadline:
        if proc.poll() is not None:
            break
        time.sleep(0.5)

    if proc.poll() is None:
        print("[%s] did not exit on WM_CLOSE; killing (no hprof report)" % label)
        proc.kill()
        log.close()
        return 2

    log.close()
    if os.path.exists(hprof_file):
        print("[%s] hprof report: %s (%d bytes)"
              % (label, hprof_file, os.path.getsize(hprof_file)))
        return 0
    print("[%s] exited but no hprof report at %s" % (label, hprof_file))
    return 2


if __name__ == "__main__":
    sys.exit(main())
