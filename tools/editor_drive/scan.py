"""Sweep the pointer over a game window and group the hover texts.

    python scan.py <pid-file> <sheet.png> [x0 y0 x1 y1 step] [--strip X Y W H]

Moves the mouse over a grid, captures the hover-text strip (top-left of the
viewport) at each point, groups identical strips and writes a contact sheet:
one row per distinct hover text, labelled with its index. Prints, per index,
up to three points where that text showed, so the caller can click one.
"""
import hashlib
import os
import sys
import tempfile
import time

from PIL import Image, ImageDraw

import drive


def grab(hwnd):
    path = os.path.join(tempfile.gettempdir(), "scan_%d.png" % os.getpid())
    w, h = drive.client_size(hwnd)
    hdc = drive.user32.GetDC(hwnd)
    mem = drive.gdi32.CreateCompatibleDC(hdc)
    bmp = drive.gdi32.CreateCompatibleBitmap(hdc, w, h)
    drive.gdi32.SelectObject(mem, bmp)
    drive.user32.PrintWindow(hwnd, mem, 3)
    import ctypes
    import ctypes.wintypes as wt

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
    drive.gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(info), 0)
    drive.gdi32.DeleteObject(bmp)
    drive.gdi32.DeleteDC(mem)
    drive.user32.ReleaseDC(hwnd, hdc)
    del path
    return Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1).copy()


def text_mask(strip):
    """The hover text's own pixels: white, yellow (npc), cyan (loc), orange
    (obj). Everything else (the moving world behind it) is dropped, so the
    same text hashes the same over any background."""
    out = Image.new("L", strip.size, 0)
    src = strip.load()
    dst = out.load()
    for y in range(strip.height):
        for x in range(strip.width):
            r, g, b = src[x, y]
            if (r > 235 and g > 235 and b > 235) or (r > 235 and g > 235 and b < 40) \
                    or (r < 40 and g > 235 and b > 235) or (r > 235 and 130 < g < 170 and b < 60):
                dst[x, y] = 255 if b > 200 and r > 200 else (200 if b < 40 and g > 200 else 120)
    return out


def main():
    argv = sys.argv[1:]
    strip_box = None
    pick = None
    then = None
    if "--pick" in argv:
        i = argv.index("--pick")
        pick = argv[i + 1]
        del argv[i:i + 2]
    if "--then" in argv:
        i = argv.index("--then")
        then = argv[i + 1].split()
        del argv[i:i + 2]
    if "--strip" in argv:
        i = argv.index("--strip")
        strip_box = tuple(map(int, argv[i + 1:i + 5]))
        del argv[i:i + 5]
    with open(argv[0]) as f:
        hwnd = drive.find_window(int(f.read().strip()))
    sheet_path = argv[1]
    w, h = drive.client_size(hwnd)
    x0, y0, x1, y1, step = (map(int, argv[2:7]) if len(argv) >= 7 else (140, 40, w - 240, 330, 36))
    if strip_box is None:
        strip_box = (0, 0, 420, 20)
    sx, sy, sw, sh = strip_box

    groups = {}
    order = []
    # A hover text from before a teleport stays up until the pointer has
    # moved for a few frames: warm up before the first sample counts.
    for wx, wy in ((x0, y0), (x0 + 6, y0 + 6), (x0, y0)):
        drive.post(hwnd, drive.WM_MOUSEMOVE, 0, drive.lparam(wx, wy))
        time.sleep(0.3)
    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            drive.post(hwnd, drive.WM_MOUSEMOVE, 0, drive.lparam(x, y))
            time.sleep(0.15)
            strip = grab(hwnd).crop((sx, sy, sx + sw, sy + sh))
            mask = text_mask(strip)
            if mask.getbbox() is None:
                continue
            key = hashlib.md5(mask.tobytes()).hexdigest()
            if key not in groups:
                yellow = sum(1 for v in mask.getdata() if v == 200)
                groups[key] = {"strip": strip, "points": [], "npc": yellow > 12}
                order.append(key)
            groups[key]["points"].append((x, y))
            if pick == "npc" and groups[key]["npc"]:
                # An npc wanders: take it the moment it is under the pointer.
                drive.run(hwnd, ["click", str(x), str(y)])
                print("clicked npc at %d,%d" % (x, y))
                if then:
                    drive.run(hwnd, then)
                return

    sheet = Image.new("RGB", (sw + 40, max(1, len(order)) * (sh + 2)), (30, 30, 30))
    draw = ImageDraw.Draw(sheet)
    for i, key in enumerate(order):
        sheet.paste(groups[key]["strip"], (40, i * (sh + 2)))
        draw.text((4, i * (sh + 2) + 4), str(i), fill=(255, 255, 0))
        pts = groups[key]["points"]
        mid = pts[len(pts) // 2]
        print("%d: n=%d mid=%d,%d first=%d,%d" % (i, len(pts), mid[0], mid[1], pts[0][0], pts[0][1]))
    sheet.save(sheet_path)
    print("sheet %s rows=%d" % (sheet_path, len(order)))
    if pick is not None and pick != "npc":
        pts = groups[order[int(pick)]]["points"]
        x, y = pts[len(pts) // 2]
        drive.run(hwnd, ["click", str(x), str(y)])
        print("clicked %s at %d,%d" % (pick, x, y))
        if then:
            drive.run(hwnd, then)


if __name__ == "__main__":
    main()
