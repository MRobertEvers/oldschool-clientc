#!/usr/bin/env python3
"""Single-way combat in RSProx transcripts: every Attack click (opnpc2 / opnpct)
by the recorder, the ticks since the LAST npc swing at the recorder (the
reference's %aggressive_npc is the last swinger only), whether that npc died
before the click, whether the zone was multi (varbit multiway_indicator), and
whether a refusal message followed before the next click (within 3 ticks).

Prints one TSV row per click
    transcript tick op npc_type npc_index multi ticks_since_swing refusal died
then a histogram of single-way clicks: ticks since another npc's swing ->
[refused, allowed].

A "swing" is any npc sequence other than def/block/death/walk/ready while that
npc faces the recorder, so a follower's emote can read as one (index 4523
[5204]: Zanik's pipe-squeeze). Read the rows, not only the histogram.

    python3 tools/rsprox/rsprox_singles.py ~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx4523.txt ...

docs/rsprox-analysis/combat/singles.md has the sessions and the result.
"""
import re, sys, collections
NONSWING = re.compile(r"def|block|death|walk|ready|stand|idle|_die|spawn|-1 ", re.I)
MSG = re.compile(r'message="(I\'m already under attack|Someone else is fighting that|It\'s not after you)')
hist = collections.defaultdict(lambda: [0, 0])  # delta -> [refused, allowed]
rows_out = []
for path in sys.argv[1:]:
    ticks = []  # (tick, clicks[(idx,type)], msgs[], swings[(idx,type)])
    facing = {}; multi = 0; recorder = None
    cur = None; section = None; ent = None; entkey = None
    for line in open(path, errors="replace"):
        m = re.match(r"^\[(\d+)\]", line)
        if m:
            cur = {"t": int(m.group(1)), "clicks": [], "msgs": [], "swings": [], "multi": multi}
            ticks.append(cur); section = None; continue
        if cur is None: continue
        s = line.strip(); ind = len(line) - len(line.lstrip())
        if ind == 4:
            section = s.split("]")[0] + "]"; ent = entkey = None
            mm = re.match(r"\[(opnpc2\w*|opnpct\w*)\] .*?npc=\(index=(\d+), id=([^ ]+)", s)
            if mm: cur["clicks"].append((mm.group(2), mm.group(3), mm.group(1)))
            mv = re.search(r"id=multiway_indicator \(4605\), old=\d+, new=(\d+)", s)
            if mv: multi = int(mv.group(1)); cur["multi"] = multi
            mg = MSG.search(s)
            if mg: cur["msgs"].append(mg.group(1))
            continue
        if ind == 8:
            ent = s; entkey = None
            mm = re.search(r"player=\(index=\d+, name=([^,]+)", s)
            if section == "[player_info]" and recorder is None and mm: recorder = mm.group(1)
            mn = re.search(r"npc=\(index=(\d+), id=([^ ]+)", s)
            if section and section.startswith("[npc_info") and mn: entkey = (mn.group(1), mn.group(2))
            continue
        if ind >= 12 and entkey:
            if s.startswith("[facing]"):
                facing[entkey[0]] = bool(recorder and ("name=" + recorder) in s)
            elif s.startswith("[sequence]") and re.search(r"death|_die", s):
                cur.setdefault("deaths", []).append(entkey[0])
            elif s.startswith("[sequence]") and facing.get(entkey[0]) and not NONSWING.search(s):
                cur["swings"].append(entkey)
    by_t = {x["t"]: x for x in ticks}
    order = [x["t"] for x in ticks]
    for i, x in enumerate(ticks):
        for (idx, typ, op) in x["clicks"]:
            # LostCity: %aggressive_npc = the LAST swinger only; a dead one frees
            last = None; dead = set(); last_died = False
            for y in reversed(ticks[: i + 1]):
                if x["t"] - y["t"] > 30: break
                dead.update(y.get("deaths", []))
                live = y["swings"]; diedflag = any(k[0] in dead for k in live)
                if live:
                    if all(k == (idx, typ) for k in live): last = None
                    else: last = y; last_died = diedflag
                    break
            # refusal before next click, within 3 ticks
            refused = None
            for y in ticks[i: i + 4]:
                if y is not x and y["clicks"]: break
                if y["msgs"]: refused = y["msgs"][0]; break
            delta = x["t"] - last["t"] if last else None
            rows_out.append((path.split("/")[-1], x["t"], op, typ, idx, x["multi"], delta, refused, "DIED" if last_died else ""))
            if x["multi"] == 0 and delta is not None and not last_died:
                hist[delta][0 if refused == "I'm already under attack" else 1] += 1
for r in rows_out:
    if r[6] is not None or r[7]: print(*r, sep="\t")
print("\nsingle-way, attacker alive: ticks since another npc's swing -> [refused, allowed]")
for d in sorted(hist): print(d, hist[d])
