#!/usr/bin/env python3
"""Every npc_info entry for the npcs a content area uses, per life (add -> del).

    python3 tools/rsprox/rsprox_npcinfo.py <transcript>... --match 'regex on npc name'
        [--raw-dir ~/.rsprox-index-cache/home-raw/.rsprox/binary/archive]

The npc-info half of a recording is not optional reading: rsprox_interact.py
and rsprox_tells.py drop other npcs' movement and most ext-info, so an audit
built only on them never sees an npc's movement kind, speed, facing,
transforms, enabled_ops, headicons, headbars or removal timing (see the
rsprox-content skill, "Npc info is required").

Per npc type: how lives start (spawnangle, the ext-info on the add tick), the
movement kind per tick and its step length (raw coords if --raw-dir gives the
matching raw transcript), every ext-info block kind with counts and values
(sequence, spotanim, transformation, level_change, enabled_ops, facing kinds,
headicon, hit ids, headbar ids/fills, combat level), and the death: ticks from
the death seq to the [del].
"""
import argparse, os, re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
# A multinpc's line names its base type and then the form it shows,
# `id=nasty_tree (4418), multinpc=nasty_tree_unchoppable (4416), coord=...`;
# without the optional group every multinpc was invisible to this tool (the
# Draynor Manor trees, 2026-10-24). --match is tested against the base name.
ENTRY = re.compile(r"^(\s+)\[(add|del|idle|walk|run|teleport|telejump|crawl)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\), (?:multinpc=[a-z0-9_]+ \(-?\d+\), )?coord=\((\d+), (\d+), (\d+)\)\)(.*)$")
NEW = re.compile(r"newcoord=\((\d+), (\d+), \d+\)")
OTHER_ENTRY = re.compile(r"^\s+\[(add|del|idle|walk|run|teleport|telejump|crawl)\] (npc|player)=")
EXT = re.compile(r"^(\s+)\[([a-z_]+)\](.*)$")

def norm(v):
    v = re.sub(r"player=\(index=\d+, name=[^,]*, coord=\([^)]*\)\)", "player=P", v)
    v = re.sub(r"npc=\(index=\d+, [^)]*\)\)", "npc=N", v)
    v = re.sub(r"coord=\(\d+, \d+, \d+\)", "coord=C", v)
    v = re.sub(r"value=\d+", "value=#", v)
    v = re.sub(r"fill=\d+", "fill=#", v)
    v = re.sub(r"creationcycle=\d+", "creationcycle=#", v)
    v = re.sub(r"spriteindex=\d+", "spriteindex=#", v)
    return v.strip()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--raw-dir")
    ap.add_argument("--match", required=True, help="regex on the npc name, e.g. crystal_|hunllef")
    a = ap.parse_args()
    want = re.compile(a.match)
    move = defaultdict(Counter); step = defaultdict(Counter); ext = defaultdict(Counter)
    addext = defaultdict(Counter); spawnang = defaultdict(Counter); deathdel = defaultdict(Counter)
    lives = Counter(); seqdelay = defaultdict(Counter); idletick = defaultdict(Counter)
    for path in a.paths:
        raw = None
        if a.raw_dir:
            rp = os.path.join(a.raw_dir, os.path.basename(path))
            if os.path.exists(rp): raw = open(rp, errors="replace")
        tick = 0; cur = None; curind = 0
        deathseq = {}
        for line in open(path, errors="replace"):
            rline = raw.readline() if raw else line
            m = TICK.match(line)
            if m:
                tick = int(m.group(1)); cur = None; continue
            m = ENTRY.match(line)
            if m:
                ind, kind, idx, name, nid = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
                if not want.search(name):
                    cur = None; continue
                t = name[:-3] if name.endswith("_hm") else name
                cur = (t, idx, kind); curind = len(ind)
                move[t][kind] += 1
                rm = ENTRY.match(rline) if raw else m
                nm = NEW.search(rline if raw else line)
                if nm and rm:
                    dx = abs(int(nm.group(1)) - int(rm.group(6))); dz = abs(int(nm.group(2)) - int(rm.group(7)))
                    step[t][(kind, max(dx, dz))] += 1
                if kind == "add":
                    lives[t] += 1
                    sa = re.search(r"spawnangle=([a-z-]+)", m.group(9))
                    spawnang[t][sa.group(1) if sa else "-"] += 1
                if kind == "del" and (idx, t) in deathseq:
                    deathdel[t][tick - deathseq.pop((idx, t))] += 1
                continue
            if OTHER_ENTRY.match(line) or line.startswith("    [") and not line.startswith("        "):
                cur = None; continue
            if cur:
                m = EXT.match(line)
                if m and len(m.group(1)) > curind:
                    k = m.group(2); v = norm(m.group(3))
                    if k == "sequence":
                        sq = re.search(r"id=([a-z0-9_]+|-1)", v)
                        d = re.search(r"delay=(\d+)", v)
                        seqdelay[cur[0]][(sq.group(1) if sq else v, d.group(1) if d else "0")] += 1
                        if sq and "death" in sq.group(1):
                            deathseq[(cur[1], cur[0])] = tick
                        continue
                    key = k if k in ("hit", "headbar", "objs") else "%s %s" % (k, v)
                    if k == "hit": key = "hit id=%s" % re.search(r"id=(\d+)", v).group(1)
                    if k == "headbar":
                        hb = re.search(r"id=(\d+)", v); key = "headbar id=%s%s" % (hb.group(1) if hb else "?", " removed" if "removed" in v else "")
                    ext[cur[0]][key] += 1
                    if cur[2] == "add": addext[cur[0]][key] += 1
    for t in sorted(lives.keys() | move.keys()):
        print("== %s  lives=%d" % (t, lives[t]))
        print("   spawnangle:", dict(spawnang[t]))
        print("   movement:", dict(move[t]))
        print("   steps:", dict(sorted(step[t].items())))
        print("   on add:", dict(addext[t].most_common(8)))
        print("   seqs:", dict(seqdelay[t].most_common(12)))
        print("   death seq -> del ticks:", dict(deathdel[t]))
        for k, v in ext[t].most_common(25):
            print("     %5d  %s" % (v, k[:150]))

if __name__ == "__main__":
    main()
