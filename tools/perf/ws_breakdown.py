"""Attribute a live client's peak working set to image / mapped / private pages.

    python tools/perf/ws_breakdown.py --soft3d
    python tools/perf/ws_breakdown.py --d3d9-zbuffer

Launches the non-embedded server-mode client exactly as tools/perf/memory_gate.sh
does, samples QueryWorkingSet + VirtualQueryEx while it runs, and prints the
breakdown taken at the sample with the largest resident total.  Every number is
resident pages, so it sums to the working set rather than to committed bytes.
"""
import ctypes as C
import ctypes.wintypes as W
import os
import subprocess
import sys
import time

k32 = C.WinDLL("kernel32", use_last_error=True)
psapi = C.WinDLL("psapi", use_last_error=True)

PAGE = 4096
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010

LPVOID = C.c_void_p
k32.OpenProcess.restype = LPVOID
k32.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
k32.CloseHandle.argtypes = [LPVOID]
psapi.EnumProcessModules.argtypes = [LPVOID, LPVOID, W.DWORD, C.POINTER(W.DWORD)]
psapi.GetModuleBaseNameW.argtypes = [LPVOID, LPVOID, C.c_wchar_p, W.DWORD]
psapi.QueryWorkingSet.argtypes = [LPVOID, LPVOID, W.DWORD]
psapi.GetProcessMemoryInfo.argtypes = [LPVOID, LPVOID, W.DWORD]


class MBI(C.Structure):
    _fields_ = [
        ("BaseAddress", C.c_void_p),
        ("AllocationBase", C.c_void_p),
        ("AllocationProtect", W.DWORD),
        ("__align", W.DWORD),
        ("RegionSize", C.c_size_t),
        ("State", W.DWORD),
        ("Protect", W.DWORD),
        ("Type", W.DWORD),
        ("__align2", W.DWORD),
    ]


class MODULEINFO(C.Structure):
    _fields_ = [("lpBaseOfDll", C.c_void_p), ("SizeOfImage", W.DWORD),
                ("EntryPoint", C.c_void_p)]


class PROCESS_MEMORY_COUNTERS(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD),
                ("PeakWorkingSetSize", C.c_size_t), ("WorkingSetSize", C.c_size_t),
                ("QuotaPeakPagedPoolUsage", C.c_size_t),
                ("QuotaPagedPoolUsage", C.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", C.c_size_t),
                ("QuotaNonPagedPoolUsage", C.c_size_t),
                ("PagefileUsage", C.c_size_t), ("PeakPagefileUsage", C.c_size_t)]


k32.VirtualQueryEx.argtypes = [LPVOID, LPVOID, C.POINTER(MBI), C.c_size_t]
k32.VirtualQueryEx.restype = C.c_size_t
psapi.GetModuleInformation.argtypes = [LPVOID, LPVOID, C.POINTER(MODULEINFO), W.DWORD]


def modules(h):
    """[(base, end, name)] sorted by base, for attributing MEM_IMAGE pages."""
    need = W.DWORD()
    arr = (C.c_void_p * 1024)()
    if not psapi.EnumProcessModules(h, arr, C.sizeof(arr), C.byref(need)):
        return []
    out = []
    for i in range(min(1024, need.value // C.sizeof(C.c_void_p))):
        info = MODULEINFO()
        name = C.create_unicode_buffer(260)
        if psapi.GetModuleInformation(h, arr[i], C.byref(info), C.sizeof(info)):
            psapi.GetModuleBaseNameW(h, arr[i], name, 260)
            base = info.lpBaseOfDll or 0
            out.append((base, base + info.SizeOfImage, name.value))
    out.sort()
    return out


def working_set(h):
    """Resident page addresses, via QueryWorkingSet."""
    n = 1 << 16
    while True:
        buf = (C.c_size_t * (n + 1))()
        if psapi.QueryWorkingSet(h, C.byref(buf), C.sizeof(buf)):
            count = buf[0]
            return [buf[1 + i] & ~0xFFF for i in range(count)]
        if C.get_last_error() != 24:  # ERROR_MORE_DATA
            return []
        n *= 4
        if n > (1 << 26):
            return []


def snapshot(h, mods):
    """{bucket: resident_bytes} for one instant."""
    regions = []  # (base, end, type, allocation_base)
    alloc_size = {}
    addr = 0
    mbi = MBI()
    while addr < (1 << 47):
        if not k32.VirtualQueryEx(h, C.c_void_p(addr), C.byref(mbi), C.sizeof(mbi)):
            break
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize
        if size == 0:
            break
        if mbi.State == 0x1000:  # MEM_COMMIT
            ab = mbi.AllocationBase or base
            regions.append((base, base + size, mbi.Type, ab))
            alloc_size[ab] = alloc_size.get(ab, 0) + size
        addr = base + size
    regions.sort()

    def region_of(p):
        lo, hi = 0, len(regions) - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            if p < regions[mid][0]:
                hi = mid - 1
            elif p >= regions[mid][1]:
                lo = mid + 1
            else:
                return regions[mid]
        return None

    def module_of(p):
        lo, hi = 0, len(mods) - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            if p < mods[mid][0]:
                hi = mid - 1
            elif p >= mods[mid][1]:
                lo = mid + 1
            else:
                return mods[mid][2]
        return "<unknown image>"

    TYPES = {0x1000000: "image", 0x40000: "mapped", 0x20000: "private"}
    buckets = {}
    priv = {}          # allocation base -> resident bytes
    for p in working_set(h):
        r = region_of(p)
        if r is None:
            buckets["<unmapped>"] = buckets.get("<unmapped>", 0) + PAGE
            continue
        kind = TYPES.get(r[2], "other")
        key = "image: " + module_of(p) if kind == "image" else kind
        buckets[key] = buckets.get(key, 0) + PAGE
        if kind == "private":
            priv[r[3]] = priv.get(r[3], 0) + PAGE
    return buckets, priv, {b: s for b, s in alloc_size.items()}


# --- per-process GPU memory (PDH "GPU Process Memory" counter set) ----------
# Local Usage      the adapter's DEDICATED video segment.  On a card with real
#                  VRAM (the Windows XP target) these bytes live on the board
#                  and are NOT charged to our working set.  On this machine --
#                  an integrated Radeon 8060S -- the "dedicated" segment is a
#                  carve-out of the same system RAM, so the driver hands it back
#                  as private committed pages inside our address space and it
#                  DOES land in the working set.
# Non Local Usage  the system-memory aperture.  Process RAM on any machine,
#                  integrated or discrete.
# Sampling both is what lets a run here predict what a dedicated-VRAM box would
# actually charge the process: working set minus Local Usage.
pdh = C.WinDLL("pdh", use_last_error=True)

PDH_FMT_LARGE = 0x00000400
PDH_MORE_DATA = 0x800007D2

pdh.PdhOpenQueryW.argtypes = [C.c_wchar_p, C.c_size_t, LPVOID]
pdh.PdhOpenQueryW.restype = C.c_uint32
pdh.PdhAddEnglishCounterW.argtypes = [LPVOID, C.c_wchar_p, C.c_size_t, LPVOID]
pdh.PdhAddEnglishCounterW.restype = C.c_uint32
pdh.PdhCollectQueryData.argtypes = [LPVOID]
pdh.PdhCollectQueryData.restype = C.c_uint32
pdh.PdhGetFormattedCounterArrayW.argtypes = [
    LPVOID, W.DWORD, C.POINTER(W.DWORD), C.POINTER(W.DWORD), LPVOID]
pdh.PdhGetFormattedCounterArrayW.restype = C.c_uint32

GPU_COUNTERS = ("Dedicated Usage", "Shared Usage", "Local Usage",
                "Non Local Usage", "Total Committed")


class PDH_FMT_COUNTERVALUE(C.Structure):
    _fields_ = [("CStatus", W.DWORD), ("largeValue", C.c_longlong)]


class PDH_FMT_COUNTERVALUE_ITEM_W(C.Structure):
    _fields_ = [("szName", C.c_wchar_p), ("FmtValue", PDH_FMT_COUNTERVALUE)]


class GpuMem:
    """Peak per-counter GPU memory for one pid, summed over its adapters."""

    def __init__(self, pid):
        assert pid > 0
        self.prefix = "pid_%d_" % pid
        self.peak = dict.fromkeys(GPU_COUNTERS, 0)
        self.counters = {}
        self.query = LPVOID()
        if pdh.PdhOpenQueryW(None, 0, C.byref(self.query)) != 0:
            self.query = None
            return
        for name in GPU_COUNTERS:
            handle = LPVOID()
            path = "\GPU Process Memory(*)\%s" % name
            if pdh.PdhAddEnglishCounterW(
                    self.query, path, 0, C.byref(handle)) == 0:
                self.counters[name] = handle
        pdh.PdhCollectQueryData(self.query)

    def available(self):
        return bool(self.counters)

    def sample(self):
        if not self.query:
            return
        if pdh.PdhCollectQueryData(self.query) != 0:
            return
        for name, handle in self.counters.items():
            size = W.DWORD(0)
            count = W.DWORD(0)
            rc = pdh.PdhGetFormattedCounterArrayW(
                handle, PDH_FMT_LARGE, C.byref(size), C.byref(count), None)
            if rc != PDH_MORE_DATA or size.value == 0:
                continue
            buf = (C.c_byte * size.value)()
            if pdh.PdhGetFormattedCounterArrayW(
                    handle, PDH_FMT_LARGE, C.byref(size), C.byref(count),
                    C.cast(buf, LPVOID)) != 0:
                continue
            items = C.cast(buf, C.POINTER(PDH_FMT_COUNTERVALUE_ITEM_W))
            total = 0
            for i in range(count.value):
                inst = items[i].szName
                if inst and inst.startswith(self.prefix):
                    total += items[i].FmtValue.largeValue
            if total > self.peak[name]:
                self.peak[name] = total


def main():
    flag = sys.argv[1] if len(sys.argv) > 1 else "--d3d9-zbuffer"
    frames = os.environ.get("MEMGATE_FRAMES", "900")
    env = dict(os.environ, TORIRS_TRANSPORT="tcp", TORIRS_MAX_FRAMES=frames)
    exe = os.path.abspath(os.environ.get("MEMGATE_EXE", "src/torirs_win64.exe"))
    args = [exe, "--manifest", "manifests/manifest_osrs239.ini",
            "--connect", "127.0.0.1", "--port", "43596",
            "--user", "testc", "--pass", "test", flag]
    log_path = os.environ.get("WSB_LOG", "build/memgate/ws_breakdown.log")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    log = open(log_path, "wb")
    proc = subprocess.Popen(args, env=env, stdout=log, stderr=subprocess.STDOUT)
    h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, proc.pid)
    assert h, "OpenProcess failed: %d" % C.get_last_error()
    gpu = GpuMem(proc.pid)

    best_total, best, best_priv, best_sizes = 0, {}, {}, {}
    pmc = PROCESS_MEMORY_COUNTERS()
    pmc.cb = C.sizeof(pmc)
    peak = 0
    while proc.poll() is None:
        if psapi.GetProcessMemoryInfo(h, C.byref(pmc), C.sizeof(pmc)):
            peak = max(peak, pmc.PeakWorkingSetSize)
            if pmc.WorkingSetSize > best_total:
                mods = modules(h)
                snap, snap_priv, snap_sizes = snapshot(h, mods)
                if sum(snap.values()) > sum(best.values()):
                    best, best_priv, best_sizes = snap, snap_priv, snap_sizes
                    best_total = pmc.WorkingSetSize
        gpu.sample()
        time.sleep(0.25)
    proc.wait()
    log.close()

    print("=== working-set breakdown (%s, %s frames) ===" % (flag, frames))
    print("peak working set (PeakWorkingSetSize): %.2f MB" % (peak / 1048576.0))
    print("snapshot resident total:               %.2f MB" % (sum(best.values()) / 1048576.0))
    print()
    for key, val in sorted(best.items(), key=lambda kv: -kv[1]):
        mb = val / 1048576.0
        if mb >= 0.5:
            print("  %-38s %8.2f MB" % (key, mb))
    small = sum(v for v in best.values() if v / 1048576.0 < 0.5)
    if small:
        print("  %-38s %8.2f MB" % ("(rest, each < 0.5 MB)", small / 1048576.0))

    print()
    print("--- private allocations, largest resident first ---")
    print("  %-18s %10s %10s" % ("allocation base", "resident", "reserved"))
    shown = 0
    for base, res in sorted(best_priv.items(), key=lambda kv: -kv[1])[:25]:
        print("  0x%-16x %7.2f MB %7.2f MB"
              % (base, res / 1048576.0, best_sizes.get(base, 0) / 1048576.0))
        shown += res
    rest = sum(best_priv.values()) - shown
    print("  %-18s %7.2f MB   (%d more regions)"
          % ("(rest)", rest / 1048576.0, max(0, len(best_priv) - 25)))

    print()
    print("--- per-process GPU memory, peak (PDH GPU Process Memory) ---")
    if not gpu.available():
        print("  counter set unavailable on this system")
        return
    for name in GPU_COUNTERS:
        print("  %-38s %8.2f MB" % (name, gpu.peak[name] / 1048576.0))
    print()
    dedicated = gpu.peak["Dedicated Usage"]
    shared = gpu.peak["Shared Usage"]
    committed = gpu.peak["Total Committed"]
    nonlocal_use = gpu.peak["Non Local Usage"]
    if committed == 0:
        print("  This lane makes no GPU allocations at all.")
        return
    if nonlocal_use == 0 and gpu.peak["Local Usage"] >= dedicated + shared:
        print("  Non Local is zero and Local == Dedicated + Shared: this adapter")
        print("  is INTEGRATED.  Both segments are the same system RAM, so every")
        print("  GPU byte is also a working-set byte here.")
        print()
    print("  On a board with dedicated VRAM (the Windows XP target) the")
    print("  'Dedicated' bytes live on the card and are not charged to the")
    print("  process; 'Shared' bytes stay in system RAM either way.")
    print("  %-40s %8.2f MB" % ("peak WS measured here",
                                peak / 1048576.0))
    print("  %-40s %8.2f MB" % ("  less dedicated segment -> discrete est.",
                                (peak - dedicated) / 1048576.0))
    print("  %-40s %8.2f MB" % ("  less all GPU-committed -> floor",
                                (peak - committed) / 1048576.0))
    print("  (the true discrete figure sits between those two: a card with")
    print("   room in VRAM promotes some of the shared allocations as well)")


main()
