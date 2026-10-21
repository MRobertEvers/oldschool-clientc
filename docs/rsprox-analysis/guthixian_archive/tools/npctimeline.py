"""Per-tick npc timeline: python3 npctimeline.py TXT T0 T1 REGEX(npc name)."""
import re, sys
path, t0, t1, rx = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), re.compile(sys.argv[4])
tick = -1; cur = None; out = []
ent = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \(\d+\), coord=\((\d+), (\d+), (\d+)\)\)(.*)$")
zone = re.compile(r"^\s{4}\[(loc_add_change_v2|loc_del|map_anim|loc_anim|sound_area|map_projanim\w*|message_game)\] (.*)$")
for line in open(path, encoding="utf-8", errors="replace"):
    m = re.match(r"^\[(\d+)\]$", line.strip())
    if m: tick = int(m.group(1)); cur = None; continue
    if tick < t0 or tick > t1: continue
    if re.match(r"^ {8}\[\w+\] player=", line):
        cur = None
        continue
    m = ent.match(line)
    if m:
        kind, idx, name, x, z, lvl, rest = m.groups()
        cur = name if rx.search(name) else None
        if cur and kind != "idle":
            out.append(f"{tick}\t{name}#{idx}\t{kind} ({x},{z}){rest[:80]}")
        continue
    if line.startswith("        [") or line.startswith("    [") and not line.startswith("     "):
        if not line.startswith("            "): cur = None if not line.startswith("        ") else cur
    if cur and line.startswith("            ["):
        s = line.strip()
        if s.startswith("[facing] player") or s.startswith("[headbar]") and "removed" not in s: 
            if "[headbar]" in s: out.append(f"{tick}\t{cur}\t{s[:120]}")
            continue
        out.append(f"{tick}\t{cur}\t{s[:140]}")
    z = zone.match(line)
    if z and (rx.search(z.group(2)) or z.group(1) in ("map_anim","message_game")):
        out.append(f"{tick}\tZONE\t{z.group(1)} {z.group(2)[:140]}")
print("\n".join(out))
