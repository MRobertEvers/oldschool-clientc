"""Launch the Java client with a given ablation env and measure its CPU.

One script per arm so launch, settle and the 30 s window all live inside a single
rpdxp run, the same way carm.py does for the C client.
"""
import ctypes, json, os, subprocess, time
from ctypes import wintypes

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi
PQI, PVR = 0x0400, 0x0010
CREATE_NEW_CONSOLE = 0x00000010
STAGE = r"C:\dev\mem289"
JAVA = r"C:\Program Files\Java\jdk1.8.0_151\bin\java.exe"


class FILETIME(ctypes.Structure):
    _fields_ = [("l", wintypes.DWORD), ("h", wintypes.DWORD)]


class PMC(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("pf", wintypes.DWORD),
                ("pws", ctypes.c_size_t), ("ws", ctypes.c_size_t),
                ("qppp", ctypes.c_size_t), ("qpp", ctypes.c_size_t),
                ("qpnp", ctypes.c_size_t), ("qnp", ctypes.c_size_t),
                ("pfu", ctypes.c_size_t), ("ppfu", ctypes.c_size_t)]


def ft(x):
    return ((x.h << 32) | x.l) / 1e7


def cpu(h):
    a, b, k, u = FILETIME(), FILETIME(), FILETIME(), FILETIME()
    kernel32.GetProcessTimes(h, ctypes.byref(a), ctypes.byref(b),
                             ctypes.byref(k), ctypes.byref(u))
    return ft(k), ft(u)


arm = json.loads(open(r"C:\dev\arm.json", "rb").read().decode("utf-8"))
label = arm["label"]

for n in ("java.exe", "javaw.exe"):
    os.system('taskkill /F /IM ' + n + ' >nul 2>&1')
time.sleep(8)

env = dict(os.environ)
for k, v in arm.get("env", {}).items():
    env[str(k)] = str(v)

log = open(os.path.join(STAGE, label + ".log"), "wb")
args = [JAVA, "-Xmx64m", "-jar", arm.get("jar", "C:/dev/mem289/client_fps.jar"),
        "10", "0", "highmem", "members", "32"]
p = subprocess.Popen(args, cwd=STAGE, env=env, stdout=log,
                     stderr=subprocess.STDOUT, creationflags=CREATE_NEW_CONSOLE)


def find():
    res = []
    def cb(h, l):
        n = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, n, 256)
        if "Jagex" in n.value:
            res.append(h)
        return True
    W = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(W(cb), 0)
    return res[0] if res else None


hwnd = None
for _ in range(30):
    time.sleep(1)
    hwnd = find()
    if hwnd:
        break
if not hwnd:
    print("[%s] no window" % label)
    raise SystemExit(1)

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
typ(arm["user"])
user32.keybd_event(0x0D, 0, 0, 0); user32.keybd_event(0x0D, 0, 2, 0)
time.sleep(0.3)
typ("a")
time.sleep(0.5)
click(302, 316)

time.sleep(float(arm.get("settle_sec", 22)))

# Prove the client is IN-WORLD before measuring anything.
#
# Working set cannot tell: Java sits at ~82 MB on the title screen too, because
# the cache archives are loaded either way. An arm that failed to log in draws
# the title-screen flames at 5-10% of a core, and that reads as a spectacular
# ablation win. It is how a texture-only ablation came out CHEAPER than the
# gouraud-only one, with gouraud still switched on -- both arms were sitting on
# the login screen. The census prints tris/s; in-world it is ~250k, on the title
# screen it is 0.
log.flush()
tris = 0
try:
    txt = open(os.path.join(STAGE, label + ".log"), "rb").read().decode("utf-8", "replace")
    for line in txt.splitlines():
        if "[census] tris/s=" in line:
            tris = int(line.split("tris/s=")[1].split()[0])
except Exception:
    pass
if tris < 50000:
    print("[%s] NOT IN-WORLD (tris/s=%d) -- arm void" % (label, tris))
    os.system('taskkill /F /IM java.exe >nul 2>&1')
    raise SystemExit(2)

h = kernel32.OpenProcess(PQI | PVR, False, p.pid)
pm = PMC(); pm.cb = ctypes.sizeof(PMC)
psapi.GetProcessMemoryInfo(h, ctypes.byref(pm), pm.cb)
ws = pm.ws / (1024.0 * 1024.0)

a = cpu(h)
t0 = time.time()
time.sleep(float(arm.get("window_sec", 30)))
b = cpu(h)
t1 = time.time()
tot = (b[0] - a[0]) + (b[1] - a[1])
print("[%s] WS %.1f MB  user %.2f  kernel %.2f  PCT_ONE_CORE %.1f"
      % (label, ws, b[1] - a[1], b[0] - a[0], 100.0 * tot / (t1 - t0)))
fps = 0.0
try:
    txt = open(os.path.join(STAGE, label + ".log"), "rb").read().decode("utf-8", "replace")
    for line in txt.splitlines():
        if line.startswith("[fps] "):
            fps = float(line.split()[1])
except Exception:
    pass
print("[%s] in-world tris/s=%d  FPS=%.1f" % (label, tris, fps))
kernel32.CloseHandle(h)
os.system('taskkill /F /IM java.exe >nul 2>&1')
