#!/usr/bin/env python3
"""Every hit the recorded raider took in a Guardians room of an RSProx transcript
(tools/cox/rsprox_fetch.py + RSProx `transcribe`), with what dealt it.

    python3 tools/cox/rsprox_guardians.py ~/.rsprox/binary/archive/820.txt

Per hit: the tick, the damage, the player's tile, Protect from Melee, and the
cause read off the statues' animations: 1203 (melee) or 4278 (stomp: the rock
`motherlode_rockfall` lands on its target tile; the hit shows on the player a
tick or two later). Lines are the transcriber's text format.
"""
import re
import sys

TICK = re.compile(r"^\[(\d+)\]$")
PLAYER = re.compile(r"player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
NPC = re.compile(r"npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\)")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
SEQ = re.compile(r"\[sequence\] id=([a-z0-9_]+) \((\d+)\)")
VARBIT = re.compile(r"\[varbit\] id=prayer_protectfrommelee \(4118\), old=\d+, new=(\d+)")
PROJ = re.compile(r"\[map_projanim_v2\] id=motherlode_rockfall")
TARGET = re.compile(r"\[target\] coord=\((\d+), (\d+), (\d+)\)")
HEADER_PLAYER = re.compile(r"local player index: (\d+)")


def main(path):
    me = None
    tick = None
    section = None
    actor = None
    pos = None
    pfm = 0
    in_room = False
    events = []          # (tick, kind, detail)
    awaiting_target = False
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            m = HEADER_PLAYER.search(line)
            if m:
                me = m.group(1)
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                continue
            if "raids_stoneguardians_left (7569)" in line and "[add]" in line:
                in_room = True
            if "[del]" in line and "raids_stoneguardians_" in line:
                in_room = False
            if not in_room:
                continue
            stripped = line.strip()
            if stripped.startswith("[player_info]"):
                section = "player"
                continue
            if stripped.startswith("[npc_info"):
                section = "npc"
                continue
            m = VARBIT.search(line)
            if m:
                pfm = int(m.group(1))
            if PROJ.search(line):
                awaiting_target = True
            m = TARGET.search(line)
            if m and awaiting_target:
                events.append((tick, "rock", (int(m.group(1)), int(m.group(2)))))
                awaiting_target = False
            m = PLAYER.search(line)
            if m and section == "player":
                actor = ("player", m.group(1))
                if m.group(1) == me:
                    pos = (int(m.group(5) or m.group(2)), int(m.group(6) or m.group(3)))
                continue
            m = NPC.search(line)
            if m and section == "npc":
                actor = ("npc", m.group(2))
                continue
            m = HIT.search(line)
            if m and actor == ("player", me):
                events.append((tick, "hit", (int(m.group(1)), int(m.group(2)), pos, pfm)))
            m = SEQ.search(line)
            if m and actor and actor[0] == "npc" and actor[1].startswith("raids_stoneguardians"):
                if m.group(2) in ("1203", "4278"):
                    events.append((tick, "swing", (actor[1].replace("raids_stoneguardians_", ""), m.group(2))))
    # A hit is the STOMP's when a rock was thrown at a tile within one tile of
    # the player on the tick before it (the rock lands the next tick); every
    # other hit on the player in the room is a melee swing (the splat lands the
    # tick of the swing). Classified by the rock, not by the statue's sequence:
    # the recording shows 1203 with a rock thrown, so the sequence alone does
    # not tell the two apart.
    rocks = {}
    for t, k, d in events:
        if k == "rock":
            rocks.setdefault(t, []).append(d)
    seqs = {}
    for t, k, d in events:
        if k == "swing":
            seqs.setdefault(t, []).append(d)
    rows = {"stomp": [], "melee": []}
    print("tick  dmg  splat  tile          PfM  kind   statue seqs t-1/t")
    for t, k, d in events:
        if k != "hit":
            continue
        splat, value, tile, prayer = d
        # by timing alone: the recording's tiles are not trustworthy inside the
        # instance (npc and player coordinates sit in different frames)
        kind = "stomp" if rocks.get(t - 1) else "melee"
        rows[kind].append((value, prayer, t))
        print("%5d %4d  %4d   %-12s  %d    %-5s  %s / %s" % (t, value, splat, tile, prayer, kind,
              seqs.get(t - 1, []), seqs.get(t, [])))
    # one Guardians room per raid: split at the gaps between visits
    visits = []
    for kind in ("stomp", "melee"):
        for v, pr, t in rows[kind]:
            visits.append(t)
    visits.sort()
    starts = [visits[0]] + [b for a_, b in zip(visits, visits[1:]) if b - a_ > 500] if visits else []
    for n, start in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else 10 ** 9
        for kind in ("stomp", "melee"):
            values = sorted(v for v, _, t in rows[kind] if start <= t < end)
            landed = [v for v in values if v > 0]
            print("visit %d (from t%d) %s: %d hits, %d landed, max %s, landed %s" % (n + 1, start, kind,
                  len(values), len(landed), max(values) if values else "-", landed))


if __name__ == "__main__":
    main(sys.argv[1])
