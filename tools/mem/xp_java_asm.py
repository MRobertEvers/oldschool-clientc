"""Dump the JIT's actual machine code for the Java client's hot raster methods.

hsdis-i386.dll is now installed beside the Client VM, so -XX:CompileCommand=print
disassembles the compiled nmethod rather than silently printing nothing. This is
the only way to settle what the sampler could not: hprof charged 65% of in-world
work to Pix2D.cls and ~1% to Pix3D, while PrintCompilation shows the Pix3D
raster methods being compiled. Assembly does not have a sampling bias.

Restricted to named methods rather than +PrintAssembly over everything: the
whole-VM dump is tens of megabytes of mostly startup code.
"""
import os, subprocess, time, ctypes
from ctypes import wintypes

user32 = ctypes.windll.user32
CREATE_NEW_CONSOLE = 0x00000010
STAGE = r"C:\dev\mem289"
JAVA = r"C:\Program Files\Java\jdk1.8.0_151\bin\java.exe"
OUT = r"C:\dev\mem289\java_asm.log"

if os.path.exists(OUT):
    os.remove(OUT)
for n in ("java.exe", "javaw.exe"):
    os.system('taskkill /F /IM ' + n + ' >nul 2>&1')
time.sleep(6)

methods = [
    "jagex2/graphics/Pix2D.cls",
    "jagex2/dash3d/Pix3D.gouraudRaster",
    "jagex2/dash3d/Pix3D.textureRaster",
    "jagex2/dash3d/Pix3D.flatRaster",
]
args = [JAVA, "-XX:+UnlockDiagnosticVMOptions"]
for m in methods:
    args.append("-XX:CompileCommand=print," + m)
args += ["-Xmx64m", "-jar", "C:/dev/mem289/client.jar", "10", "0", "highmem", "members", "32"]

out = open(OUT, "wb")
p = subprocess.Popen(args, cwd=STAGE, stdout=out, stderr=subprocess.STDOUT,
                     creationflags=CREATE_NEW_CONSOLE)
print("pid=%d" % p.pid)


def find():
    res = []
    def cb(h, l):
        n = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, n, 256)
        if "Jagex" in n.value:
            res.append(h)
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return res[0] if res else None


hwnd = None
for _ in range(30):
    time.sleep(1)
    hwnd = find()
    if hwnd:
        break
if hwnd:
    time.sleep(6)
    user32.SetForegroundWindow(hwnd)

    class PT(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

    def click(cx, cy):
        pt = PT(cx, cy)
        user32.ClientToScreen(hwnd, ctypes.byref(pt))
        user32.SetCursorPos(pt.x, pt.y)
        user32.mouse_event(0x0002, 0, 0, 0, 0)
        time.sleep(0.05)
        user32.mouse_event(0x0004, 0, 0, 0, 0)
        time.sleep(0.4)

    def typ(s):
        for ch in s:
            vk = user32.VkKeyScanW(ord(ch)) & 0xFF
            user32.keybd_event(vk, 0, 0, 0)
            time.sleep(0.03)
            user32.keybd_event(vk, 0, 2, 0)
            time.sleep(0.03)

    click(462, 286)
    typ("asmprobe2")
    user32.keybd_event(0x0D, 0, 0, 0); user32.keybd_event(0x0D, 0, 2, 0)
    time.sleep(0.3)
    typ("a")
    time.sleep(0.5)
    click(302, 316)
    print("credentials submitted")
    time.sleep(50)
else:
    print("no Jagex window")

os.system('taskkill /F /IM java.exe >nul 2>&1')
time.sleep(2)
out.close()
sz = os.path.getsize(OUT) if os.path.exists(OUT) else 0
print("asm log %d bytes" % sz)
t = open(OUT, "rb").read().decode("ascii", "replace")
print("disassembly present:", "Decoding compiled method" in t)
print("hsdis loaded ok    :", "PrintAssembly is enabled" not in t or "Could not load" not in t)
