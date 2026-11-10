"""Superseded by tools/agent/torirs_agent.py and the client's --agent-port (docs/AGENT_API.md); kept as it was.

Drive a torirs_win64.exe window with posted Win32 messages and capture it.

    python drive.py <pid-file> <command> [args] [-- <command> [args]]...

Commands (client coordinates):
    move X Y            hover
    click X Y           left click            rclick / mclick likewise
    dclick X Y          double click (two clicks)
    down X Y / up X Y   left button edges     (rdown/rup, mdown/mup)
    drag X0 Y0 X1 Y1 [steps] [button]
    wheel X Y NOTCHES   positive = away from the user
    key NAME            tap: a..z 0..9 f1..f12 enter esc tab space back del
                        up down left right home end pgup pgdn  (chords: ctrl+s,
                        ctrl+shift+z, alt+x)
    keydown NAME / keyup NAME
    type TEXT           WM_CHAR per character
    wait MS
    shot PATH [X Y W H] PNG of the client area (optionally a crop)
    size                print the client size
    close               WM_CLOSE

The window is found by the pid in <pid-file>. Nothing here takes the real
cursor or the foreground: every event is a PostMessage to that one window.
"""

import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_CLOSE = 0x0010
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102
WM_SYSKEYDOWN = 0x0104
WM_SYSKEYUP = 0x0105
WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_RBUTTONDOWN = 0x0204
WM_RBUTTONUP = 0x0205
WM_MBUTTONDOWN = 0x0207
WM_MBUTTONUP = 0x0208
WM_MOUSEWHEEL = 0x020A
MK = {"l": 0x0001, "r": 0x0002, "m": 0x0010}
DOWN = {"l": WM_LBUTTONDOWN, "r": WM_RBUTTONDOWN, "m": WM_MBUTTONDOWN}
UP = {"l": WM_LBUTTONUP, "r": WM_RBUTTONUP, "m": WM_MBUTTONUP}

VK = {
    "enter": 0x0D, "return": 0x0D, "esc": 0x1B, "escape": 0x1B, "tab": 0x09,
    "space": 0x20, "back": 0x08, "backspace": 0x08, "del": 0x2E, "delete": 0x2E,
    "up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27, "home": 0x24,
    "end": 0x23, "pgup": 0x21, "pgdn": 0x22, "ctrl": 0x11, "shift": 0x10,
    "alt": 0x12, "lbracket": 0xDB, "rbracket": 0xDD, "comma": 0xBC,
    "period": 0xBE, "minus": 0xBD, "equals": 0xBB, "grave": 0xC0,
    "slash": 0xBF, "semicolon": 0xBA, "quote": 0xDE, "backslash": 0xDC,
    "insert": 0x2D,
}
for i in range(1, 13):
    VK["f%d" % i] = 0x70 + i - 1
for c in "abcdefghijklmnopqrstuvwxyz":
    VK[c] = ord(c.upper())
for c in "0123456789":
    VK[c] = ord(c)

FRAME = 0.04


def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        out = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(out))
        if out.value == pid and user32.IsWindowVisible(hwnd):
            rc = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(rc))
            found.append((rc.right * rc.bottom, hwnd))
        return True

    user32.EnumWindows(each, 0)
    if not found:
        raise SystemExit("drive: no visible window for pid %d" % pid)
    found.sort(reverse=True)
    return found[0][1]


def lparam(x, y):
    return (int(y) & 0xFFFF) << 16 | (int(x) & 0xFFFF)


def post(hwnd, msg, wparam, lp):
    user32.PostMessageW(hwnd, msg, wt.WPARAM(wparam), wt.LPARAM(lp))


def client_size(hwnd):
    rc = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rc))
    return rc.right, rc.bottom


def shot(hwnd, path, crop=None):
    from PIL import Image

    w, h = client_size(hwnd)
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    ok = user32.PrintWindow(hwnd, mem, 3)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
            ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
            ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
            ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
            ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
        ]

    info = BITMAPINFOHEADER()
    info.biSize = ctypes.sizeof(info)
    info.biWidth = w
    info.biHeight = -h
    info.biPlanes = 1
    info.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(info), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    image = Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1)
    if crop:
        x, y, cw, ch = crop
        image = image.crop((x, y, x + cw, y + ch))
    image.save(path)
    print("shot %s %dx%d%s" % (path, image.width, image.height, "" if ok else " (PrintWindow failed)"))


def mods_of(name):
    parts = name.lower().split("+")
    return [VK[p] for p in parts[:-1]], VK[parts[-1]]


def run(hwnd, argv):
    held = 0
    i = 0

    def take(n):
        nonlocal i
        out = argv[i:i + n]
        if len(out) < n:
            raise SystemExit("drive: %s wants %d argument(s)" % (argv[i - 1], n))
        i += n
        return out

    while i < len(argv):
        cmd = argv[i]
        i += 1
        if cmd == "--":
            continue
        if cmd == "move":
            x, y = map(int, take(2))
            post(hwnd, WM_MOUSEMOVE, held, lparam(x, y))
            time.sleep(FRAME)
        elif cmd in ("click", "rclick", "mclick", "dclick"):
            x, y = map(int, take(2))
            b = {"click": "l", "dclick": "l", "rclick": "r", "mclick": "m"}[cmd]
            for _ in range(2 if cmd == "dclick" else 1):
                post(hwnd, WM_MOUSEMOVE, held, lparam(x, y))
                time.sleep(FRAME)
                post(hwnd, DOWN[b], held | MK[b], lparam(x, y))
                time.sleep(FRAME * 2)
                post(hwnd, UP[b], held, lparam(x, y))
                time.sleep(FRAME * 2)
        elif cmd in ("down", "rdown", "mdown", "up", "rup", "mup"):
            x, y = map(int, take(2))
            b = "l" if cmd in ("down", "up") else cmd[0]
            post(hwnd, WM_MOUSEMOVE, held, lparam(x, y))
            time.sleep(FRAME)
            if cmd.endswith("down"):
                held |= MK[b]
                post(hwnd, DOWN[b], held, lparam(x, y))
            else:
                held &= ~MK[b]
                post(hwnd, UP[b], held, lparam(x, y))
            time.sleep(FRAME * 2)
        elif cmd == "drag":
            x0, y0, x1, y1 = map(int, take(4))
            steps = 12
            b = "l"
            if i < len(argv) and argv[i].isdigit():
                steps = int(argv[i])
                i += 1
            if i < len(argv) and argv[i] in ("l", "r", "m"):
                b = argv[i]
                i += 1
            post(hwnd, WM_MOUSEMOVE, held, lparam(x0, y0))
            time.sleep(FRAME)
            post(hwnd, DOWN[b], held | MK[b], lparam(x0, y0))
            time.sleep(FRAME * 2)
            for s in range(1, steps + 1):
                x = x0 + (x1 - x0) * s // steps
                y = y0 + (y1 - y0) * s // steps
                post(hwnd, WM_MOUSEMOVE, held | MK[b], lparam(x, y))
                time.sleep(FRAME)
            time.sleep(FRAME * 2)
            post(hwnd, UP[b], held, lparam(x1, y1))
            time.sleep(FRAME * 2)
        elif cmd == "wheel":
            x, y, notches = map(int, take(3))
            post(hwnd, WM_MOUSEMOVE, held, lparam(x, y))
            time.sleep(FRAME)
            step = 1 if notches > 0 else -1
            for _ in range(abs(notches)):
                post(hwnd, WM_MOUSEWHEEL, ((step * 120) & 0xFFFF) << 16, lparam(x, y))
                time.sleep(FRAME)
        elif cmd in ("key", "keydown", "keyup"):
            (name,) = take(1)
            mods, vk = mods_of(name)
            alt = 0x12 in mods
            kd = WM_SYSKEYDOWN if alt else WM_KEYDOWN
            ku = WM_SYSKEYUP if alt else WM_KEYUP
            if cmd in ("key", "keydown"):
                for m in mods:
                    post(hwnd, WM_KEYDOWN, m, 1)
                    time.sleep(FRAME)
                post(hwnd, kd, vk, 1)
                time.sleep(FRAME * 2)
            if cmd in ("key", "keyup"):
                post(hwnd, ku, vk, 0xC0000001)
                time.sleep(FRAME)
                for m in reversed(mods):
                    post(hwnd, WM_KEYUP, m, 0xC0000001)
                    time.sleep(FRAME)
        elif cmd == "type":
            (text,) = take(1)
            for ch in text:
                post(hwnd, WM_CHAR, ord(ch), 1)
                time.sleep(FRAME)
        elif cmd == "bs":
            # Clear a focused field: End, then N backspaces.
            (count,) = take(1)
            post(hwnd, WM_KEYDOWN, 0x23, 1)
            post(hwnd, WM_KEYUP, 0x23, 0xC0000001)
            time.sleep(FRAME)
            for _ in range(int(count)):
                post(hwnd, WM_KEYDOWN, 0x08, 1)
                post(hwnd, WM_KEYUP, 0x08, 0xC0000001)
                time.sleep(0.012)
            time.sleep(FRAME)
        elif cmd == "wait":
            (ms,) = take(1)
            time.sleep(int(ms) / 1000.0)
        elif cmd == "shot":
            (path,) = take(1)
            crop = None
            if i + 3 < len(argv) + 0 and all(a.lstrip("-").isdigit() for a in argv[i:i + 4]) and len(argv[i:i + 4]) == 4:
                crop = tuple(map(int, take(4)))
            time.sleep(FRAME * 3)
            shot(hwnd, path, crop)
        elif cmd == "size":
            print("client %dx%d" % client_size(hwnd))
        elif cmd == "close":
            post(hwnd, WM_CLOSE, 0, 0)
        else:
            raise SystemExit("drive: unknown command %r" % cmd)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    with open(sys.argv[1]) as f:
        pid = int(f.read().strip())
    run(find_window(pid), sys.argv[2:])


if __name__ == "__main__":
    main()
