#!/usr/bin/env python3
"""When does Jagex's server send an NPC's defend (block) animation, and when
does the hit it answers land? Read from RSProx transcripts (named revisions).

For every attack sequence the local player plays, the target is the npc named
by the projectile the player launched that tick (its [target] line), or, for
an attack with no projectile, the npc the player is facing
(face_pathingentity). On that npc it reads the sequence sent on the attack tick
and the player's first splat (16/12) after it, and prints one summary row per
(attack seq, projectile end, npc, defend seq@delay, splat offset):

    python3 tools/rsprox/rsprox_defend_timing.py ~/.rsprox/binary/archive/{3285,820,3299}.txt

  attack       the player's own sequence on the attack tick
  proj_end     the end cycle of the projectile the player launched that tick (- for none)
  defend       the npc sequence sent on the ATTACK tick (- for none) and its delay
  splat        ticks from the attack to the player's splat on that npc
  late_defend  a sequence sent on the SPLAT tick instead (- for none)
"""
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
PLAYER = re.compile(r"player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+)")
FACE = re.compile(r"\[face_pathingentity\] npc=\(index=(\d+)")
TARGET_NPC = re.compile(r"\[target\] coord=.*npc=\(index=(\d+)")
SOURCE = re.compile(r"\[source\] coord=\((\d+), (\d+)")
NPC = re.compile(r"^\s+\[(?:add|del|idle|walk|run|teleport|crawl)\] npc=\(index=(\d+), id=([a-z0-9_]+) \(\d+\)")
SEQ = re.compile(r"\[sequence\] id=([a-z0-9_]+) \(\d+\)(?:, delay=(\d+))?")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
PROJ = re.compile(r"\[map_projanim_v2\] id=([a-z0-9_]+) \(\d+\), starttime=(\d+), endtime=(\d+)")
ME_SPLATS = {"12", "16"}


def scan(path):
    me = None
    tick = 0
    section = None
    actor = None
    attacks = []                     # (tick, seq)
    projs = {}                       # tick -> (end, npc index) of my projectile
    my_xz = None
    face = None
    faced = {}                       # tick -> npc index I face
    npc_seq = defaultdict(list)      # (index) -> [(tick, seq, delay)]
    npc_hits = defaultdict(list)     # (index) -> [tick]
    npc_hitval = defaultdict(dict)   # (index) -> tick -> first value
    names = {}
    pending = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = ME.search(line)
            if m:
                me = m.group(1)
            s = line.strip()
            m = TICK.match(s)
            if m and line.startswith("["):
                tick = int(m.group(1))
                continue
            if s.startswith("[player_info]"):
                section = "player"
            elif s.startswith("[npc_info"):
                section = "npc"
            m = PROJ.search(line)
            if m:
                pending = [int(m.group(3)), None]
                continue
            if pending is not None:
                m = SOURCE.search(line)
                if m:
                    pending[1] = (int(m.group(1)), int(m.group(2)))
                    continue
                m = TARGET_NPC.search(line)
                if m and pending[1] is not None and my_xz is not None and \
                        max(abs(pending[1][0] - my_xz[0]), abs(pending[1][1] - my_xz[1])) <= 1:
                    projs.setdefault(tick, (pending[0], m.group(1)))
                pending = None
                continue
            m = NPC.match(line)
            if m and section == "npc":
                actor = ("npc", m.group(1))
                names[m.group(1)] = m.group(2)
                continue
            m = PLAYER.search(line)
            if m and section == "player" and "[source]" not in line and "[target]" not in line:
                actor = ("me" if m.group(1) == me else "other", m.group(1))
                if actor[0] == "me":
                    my_xz = (int(m.group(2)), int(m.group(3)))
                continue
            if actor is None:
                continue
            m = FACE.search(line)
            if m and actor[0] == "me":
                face = m.group(1)
                continue
            m = SEQ.search(line)
            if m:
                if actor[0] == "me":
                    faced[tick] = face
                    attacks.append((tick, m.group(1)))
                elif actor[0] == "npc":
                    npc_seq[actor[1]].append((tick, m.group(1), int(m.group(2) or 0)))
                continue
            m = HIT.search(line)
            if m and actor[0] == "npc" and m.group(1) in ME_SPLATS:
                npc_hits[actor[1]].append(tick)
                npc_hitval[actor[1]].setdefault(tick, int(m.group(2)))
    rows = Counter()
    for t, seq in attacks:
        pe, idx = projs.get(t, ("-", faced.get(t)))
        if idx is None:
            continue
        hits = [h for h in npc_hits[idx] if t < h <= t + 8]
        if not hits:
            continue
        h = hits[0]
        at = [(s2, d) for (tt, s2, d) in npc_seq[idx] if tt == t]
        late = [s2 for (tt, s2, d) in npc_seq[idx] if tt == h]
        defend = "%s@%d" % at[0] if at else "-"
        rows[(seq, pe, names.get(idx, idx), defend, h - t, late[0] if late else "-")] += 1
    if FOCUS:
        defends = defaultdict(set)
        for idx, lst in npc_seq.items():
            for (tt, s2, d) in lst:
                if re.search(r"defend|block|parry", s2):
                    defends[names.get(idx, idx)].add(s2)
        rows = Counter()
        for t, seq in attacks:
            pe, idx = projs.get(t, ("-", faced.get(t)))
            if idx is None or not defends.get(names.get(idx, idx)):
                continue
            want = defends[names[idx]]
            at = [(s2, d) for (tt, s2, d) in npc_seq[idx] if tt == t]
            if pe != "-":
                land = [h for h in npc_hits[idx] if h == t + (pe + 29) // 30 or h == t + pe // 30]
            else:
                land = [h for h in npc_hits[idx] if h == t + 1]
            val = npc_hitval[idx].get(land[0]) if land else None
            if at and at[0][0] in want:
                kind = "defend@%s" % ("proj_end" if pe != "-" and at[0][1] == pe else at[0][1])
            elif at:
                kind = "other seq (%s)" % at[0][0]
            else:
                kind = "none"
            rows[("melee" if pe == "-" else "projectile", names[idx], kind,
                  "-" if val is None else ("0" if val == 0 else ">0"),
                  "-" if not land else "+%d" % (land[0] - t))] += 1
    return rows


FOCUS = False


def main(paths):
    global FOCUS
    if paths and paths[0] == "--focus":
        FOCUS = True
        paths = paths[1:]
        total = Counter()
        for p in paths:
            total.update(scan(p))
        print("%5s  %-10s %-32s %-34s %-6s %s" % ("n", "attack", "npc", "sent on the attack tick", "splat", "lands"))
        for (k, npc, kind, val, off), n in sorted(total.items(), key=lambda kv: (kv[0][1], -kv[1])):
            print("%5d  %-10s %-32s %-34s %-6s %s" % (n, k, npc[:32], kind[:34], val, off))
        return
    total = Counter()
    for p in paths:
        total.update(scan(p))
    print("%5s  %-28s %-8s %-30s %-38s %5s  %s" % ("n", "attack", "proj_end", "npc", "defend@delay (attack tick)", "splat", "late_defend"))
    for (seq, pe, npc, defend, off, late), n in total.most_common(70):
        print("%5d  %-28s %-8s %-30s %-38s %5s  %s" % (n, seq[:28], pe, npc[:30], defend[:38], "+%d" % off, late))


if __name__ == "__main__":
    main(sys.argv[1:])
