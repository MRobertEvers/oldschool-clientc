"""In-world-only view of an hprof report.

hprof samples the whole JVM lifetime, so a 40 s "in-world" profile still carries
the ~25 s of title screen and cache boot that preceded it. Those show up as
renderFlames/updateFlames, titleScreenLoop, JagFile.read and BZip2 -- 8-20% of
work in these runs -- and they are not part of a steady-state frame. Any trace
whose stack passes through the title screen, the login handshake or the cache
build is excluded here, leaving the frames the in-world client actually runs.

Excluding by STACK, not by leaf: BZip2.decompress under gameLoop is a real
in-world cache read, while the same leaf under maininit is boot.
"""
import re
import sys

IDLE_LEAF = "sun.awt.windows.WToolkit.eventLoop"

# Any of these anywhere on the stack means the sample is not steady-state play.
BOOT_MARKERS = (
    "jagex2.client.Client.titleScreenLoop",
    "jagex2.client.Client.titleScreenDraw",
    "jagex2.client.Client.renderFlames",
    "jagex2.client.Client.updateFlames",
    "jagex2.client.Client.drawFlames",
    "jagex2.client.Client.generateFlameCoolingMap",
    "jagex2.client.Client.maininit",
    "jagex2.client.Client.login",
    "jagex2.client.ClientBuild",
    "java.lang.ClassLoader",
)


def parse(path):
    traces, samples, total = {}, {}, 0
    cur, mode = None, None
    for line in open(path, "r", errors="replace"):
        s = line.rstrip("\n")
        m = re.match(r"^TRACE (\d+):", s)
        if m:
            cur = int(m.group(1))
            traces[cur] = []
            mode = "trace"
            continue
        if s.startswith("CPU SAMPLES BEGIN"):
            total = int(re.search(r"total = (\d+)", s).group(1))
            mode = "samples"
            continue
        if s.startswith("CPU SAMPLES END"):
            mode = None
            continue
        if mode == "trace" and (s.startswith("\t") or s.startswith("    ")):
            fn = s.strip().split("(")[0]
            if fn:
                traces[cur].append(fn)
            continue
        if mode == "samples":
            p = s.split()
            if len(p) >= 6 and p[0].isdigit():
                samples[int(p[4])] = samples.get(int(p[4]), 0) + int(p[3])
    return total, traces, samples


BUCKETS = [
    ("present / blit", ("sun.java2d.", "sun.awt.image.", "java.awt.image.")),
    ("2D raster (Pix2D/Pix8/PixFont)", ("jagex2.graphics.",)),
    ("3D raster (Pix3D/Model)", ("jagex2.dash3d.Pix3D", "jagex2.dash3d.Model")),
    ("scene / world / entities", ("jagex2.dash3d.",)),
    ("network", ("java.net.", "jagex2.io.ClientStream")),
    ("cache decompress / io", ("jagex2.io.", "java.io.", "java.util.zip.")),
    ("client logic", ("jagex2.",)),
    ("awt / cursor", ("sun.awt.", "java.awt.")),
]


def bucket_of(fn):
    for name, pres in BUCKETS:
        for p in pres:
            if fn.startswith(p):
                return name
    return "other"


def main(path, label):
    total, traces, samples = parse(path)
    idle = boot = work = 0
    self_by, incl_by, bucket = {}, {}, {}
    for tid, c in samples.items():
        fr = traces.get(tid) or []
        if not fr:
            continue
        if fr[0] == IDLE_LEAF:
            idle += c
            continue
        if any(any(f.startswith(b) for b in BOOT_MARKERS) for f in fr):
            boot += c
            continue
        work += c
        self_by[fr[0]] = self_by.get(fr[0], 0) + c
        bucket[bucket_of(fr[0])] = bucket.get(bucket_of(fr[0]), 0) + c
        for f in dict.fromkeys(fr):
            incl_by[f] = incl_by.get(f, 0) + c

    print("## %s" % label)
    print("")
    print("| partition | samples | share of total |")
    print("|---|---|---|")
    print("| idle AWT pump | %d | %.1f %% |" % (idle, 100.0 * idle / total))
    print("| title screen / boot / login | %d | %.1f %% |" % (boot, 100.0 * boot / total))
    print("| **in-world work** | **%d** | **%.1f %%** |" % (work, 100.0 * work / total))
    print("")
    print("Percentages below are of the %d in-world work samples." % work)
    print("")
    print("### In-world work by subsystem (self)")
    print("")
    print("| subsystem | share | samples |")
    print("|---|---|---|")
    for n, c in sorted(bucket.items(), key=lambda kv: -kv[1]):
        print("| %s | %.2f %% | %d |" % (n, 100.0 * c / work, c))
    print("")
    print("### In-world inclusive (method anywhere on the stack)")
    print("")
    print("| rank | inclusive | self | method |")
    print("|---|---|---|---|")
    for i, (fn, c) in enumerate(sorted(incl_by.items(), key=lambda kv: -kv[1])[:28], 1):
        print("| %d | %.2f %% | %.2f %% | `%s` |"
              % (i, 100.0 * c / work, 100.0 * self_by.get(fn, 0) / work, fn))
    print("")


main(sys.argv[1], sys.argv[2])
