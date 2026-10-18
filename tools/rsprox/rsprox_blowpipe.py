"""rsprox_blowpipe.py <transcript>... : projectiles in ticks that carry the local 'dart' synth (2696)."""
import re, sys
TICK = re.compile(r"^\[(\d+)\]$")
PROJ = re.compile(r"\[map_projanim_v(\d)\] id=(?:\S+ \()?(\d+)\)?, starttime=(\d+), endtime=(\d+), angle=(\d+), progress=(\d+), startheight=(\d+), endheight=(\d+)")
SRC = re.compile(r"^\s+\[source\] coord=\((\d+), (\d+), (\d+)\)(.*)")
TGT = re.compile(r"^\s+\[target\] coord=\((\d+), (\d+), (\d+)\)(.*)")
SYN = re.compile(r"\[synth_sound\] id=(?:\S+ \()?(\d+)\)?(.*)")
SPOT = re.compile(r"\[spotanim\] (?:slot=\d+, )?id=(?:\S+ \()?(-?\d+)\)?(.*)")
print("file\ttick\tver\tproj\tstart\tend\tangle\tprogress\tsh\teh\tdist\tsrc_ent\ttgt\tdart_delay\tspots")
for path in sys.argv[1:]:
    tick = 0; projs = []; dart = None; pending = None; spots = []
    def flush():
        if dart is None: return
        for p in projs:
            d = max(abs(p["tx"] - p["sx"]), abs(p["tz"] - p["sz"]))
            print("\t".join(map(str, [path.split("/")[-1], tick, p["v"], p["id"], p["st"], p["et"], p["ang"], p["prog"], p["sh"], p["eh"], d, p["src"][:30], p["tgt"][:40], dart, ";".join(spots)])))
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        m = TICK.match(line.strip())
        if m:
            flush(); tick = int(m.group(1)); projs = []; dart = None; pending = None; spots = []; continue
        m = PROJ.search(line)
        if m:
            pending = dict(v=m.group(1), id=m.group(2), st=m.group(3), et=m.group(4), ang=m.group(5), prog=m.group(6), sh=m.group(7), eh=m.group(8)); continue
        m = SRC.match(line)
        if m and pending:
            pending["sx"], pending["sz"] = int(m.group(1)), int(m.group(2)); pending["src"] = m.group(4).strip(); continue
        m = TGT.match(line)
        if m and pending:
            pending["tx"], pending["tz"] = int(m.group(1)), int(m.group(2)); pending["tgt"] = m.group(4).strip(); projs.append(pending); pending = None; continue
        m = SYN.search(line)
        if m and m.group(1) == "2696":
            dl = re.search(r"delay=(\d+)", m.group(2)); dart = dl.group(1) if dl else "0"; continue
        m = SPOT.search(line)
        if m: spots.append(m.group(1) + m.group(2)[:30])
    flush()
