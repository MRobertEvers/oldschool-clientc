#!/usr/bin/env python3
"""What each npc type's attack sends, from an RSProx transcript (named revision).

For every tick on which exactly ONE npc whose name matches --npc plays an attack
sequence (its name has "attack", or --attack lists them), everything else sent
that tick and over the next --after ticks is attributed to that attack:
projectiles (all parameters, with the Chebyshev distance from the attacker's
centre to the target tile), map graphics, area and synth sounds, the local
player's spotanims, sequences and hits. Each signature is counted per npc type
and offset, so a constant tell reads as "n of N attacks".

    python3 tools/rsprox/rsprox_attack_tells.py ~/.rsprox/binary/archive/3299.txt \
        --from 10730 --to 13632 --npc inferno_creature inferno_nibbler

Sizes come from --size name=n (default 1); the attacker's centre is its
south-west tile + (size-1)/2.
"""
import argparse
import re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
PLAYER = re.compile(r"player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+)")
NPC = re.compile(r"^\s+\[(?:add|del|idle|walk|run|teleport|crawl)\] npc=\(index=(\d+), id=([a-z0-9_]+) \(\d+\), coord=\((\d+), (\d+)")
SEQ = re.compile(r"\[sequence\] id=([a-z0-9_]+) \(\d+\)(?:, delay=(\d+))?")
SPOT = re.compile(r"\[spotanim\] (?:slot=\d+, )?id=([a-z0-9_]+) \(\d+\)(.*)$")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
PROJ = re.compile(r"\[map_projanim_v2\] id=([a-z0-9_]+) \(\d+\), (.*)$")
SRC = re.compile(r"\[source\] coord=\((\d+), (\d+)")
DST = re.compile(r"\[target\] coord=\((\d+), (\d+)(?:, \d+\))?(?:, (npc|player)=)?")
MAPANIM = re.compile(r"\[map_anim\] id=([a-z0-9_]+) \(\d+\)(.*?)coord=")
SOUND = re.compile(r"\[sound_area\] id=(\d+), (.*?)coord=")
SYNTH = re.compile(r"\[synth_sound\] id=(\d+)(.*)$")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("transcript")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--to", dest="end", type=int, default=10 ** 9)
    ap.add_argument("--npc", nargs="+", required=True)
    ap.add_argument("--attack", nargs="*", default=[])
    ap.add_argument("--after", type=int, default=4)
    ap.add_argument("--size", nargs="*", default=[])
    ap.add_argument("--sounds", default="")
    a = ap.parse_args()
    sizes = {k: int(v) for k, v in (s.split("=") for s in a.size)}
    names = {}
    if a.sounds:
        with open(a.sounds) as handle:
            for line in handle:
                k, _, v = line.strip().partition("=")
                if k.isdigit():
                    names[k] = v.split(" ")[0]
    me = None
    tick = 0
    section = None
    actor = None
    npc_at = {}
    attacks = defaultdict(list)        # tick -> [(type, (x, z))]
    events = defaultdict(list)         # tick -> [str]
    proj = None
    with open(a.transcript, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = ME.search(line)
            if m:
                me = m.group(1)
            s = line.strip()
            m = TICK.match(s)
            if m and line.startswith("["):
                tick = int(m.group(1))
                continue
            if tick < a.start or tick > a.end + a.after:
                continue
            if s.startswith("[player_info]"):
                section = "player"
            elif s.startswith("[npc_info"):
                section = "npc"
            m = PROJ.search(line)
            if m:
                proj = [m.group(1), re.sub(r"progress=0, ", "", m.group(2)), None]
                continue
            if proj is not None:
                m = SRC.search(line)
                if m:
                    proj[2] = (int(m.group(1)), int(m.group(2)))
                    continue
                m = DST.search(line)
                if m:
                    events[tick].append(("proj", proj[0], proj[1], proj[2], (int(m.group(1)), int(m.group(2))), m.group(3) or "tile"))
                proj = None
                continue
            m = NPC.match(line)
            if m and section == "npc":
                actor = ("npc", m.group(2), (int(m.group(3)), int(m.group(4))))
                continue
            m = PLAYER.search(line)
            if m and section == "player" and "[source]" not in line and "[target]" not in line:
                actor = ("me" if m.group(1) == me else "other", None, None)
                continue
            m = SEQ.search(line)
            if m and actor:
                if actor[0] == "npc" and any(k in actor[1] for k in a.npc):
                    if (a.attack and m.group(1) in a.attack) or (not a.attack and "attack" in m.group(1)):
                        attacks[tick].append((actor[1], m.group(1), actor[2]))
                    else:
                        events[tick].append(("npcseq", "%s %s%s" % (actor[1], m.group(1), ", delay=" + m.group(2) if m.group(2) else "")))
                elif actor[0] == "me":
                    events[tick].append(("myseq", m.group(1) + (", delay=" + m.group(2) if m.group(2) else "")))
                continue
            m = SPOT.search(line)
            if m and actor:
                if actor[0] == "me":
                    events[tick].append(("myspot", m.group(1) + m.group(2)))
                elif actor[0] == "npc" and any(k in actor[1] for k in a.npc):
                    events[tick].append(("npcspot", "%s %s%s" % (actor[1], m.group(1), m.group(2))))
                continue
            m = HIT.search(line)
            if m and actor and actor[0] == "me":
                events[tick].append(("myhit", "splat %s" % m.group(1)))
                continue
            m = MAPANIM.search(line)
            if m:
                events[tick].append(("mapanim", m.group(1) + m.group(2).rstrip(", ")))
                continue
            m = SOUND.search(line)
            if m:
                events[tick].append(("area", "%s %s %s" % (m.group(1), names.get(m.group(1), ""), m.group(2).rstrip(", "))))
                continue
            m = SYNTH.search(line)
            if m:
                events[tick].append(("synth", "%s %s%s" % (m.group(1), names.get(m.group(1), ""), m.group(2))))
    per = defaultdict(Counter)
    total = Counter()
    nproj = Counter()
    for t, lst in attacks.items():
        if t < a.start or t > a.end or len(lst) != 1:
            continue
        typ, seq, (x, z) = lst[0]
        key = "%s (%s)" % (typ, seq)
        total[key] += 1
        half = (sizes.get(typ, 1) - 1) / 2.0
        cx, cz = x + half, z + half
        for off in range(0, a.after + 1):
            seen = set()
            for ev in events.get(t + off, []):
                if ev[0] == "proj":
                    _, name, params, src, dst, kind = ev
                    if off == 0 and src is not None and abs(src[0] - cx) <= 3 and abs(src[1] - cz) <= 3:
                        d = int(max(abs(dst[0] - cx), abs(dst[1] - cz)))
                        sig = "proj %s -> %s: %s" % (name, kind, params)
                        per[key]["   (dist) %s d=%d" % (name, d)] += 1
                        per[key]["   (src offset) %s %+g,%+g" % (name, src[0] - x, src[1] - z)] += 1
                        nproj[key + name] += 1
                    else:
                        continue
                else:
                    sig = "%s %s" % (ev[0], ev[1])
                if sig in seen:
                    continue
                seen.add(sig)
                per[key]["+%d %s" % (off, sig)] += 1
    for key in sorted(total, key=lambda k: -total[k]):
        n = total[key]
        print("== %s: %d single attacks; projectiles %s" % (key, n, {k[len(key):]: v for k, v in nproj.items() if k.startswith(key)}))
        for sig, c in per[key].most_common():
            if c * 4 < n and c < 3:
                continue
            print("   %4d/%-4d %s" % (c, n, sig[:200]))


if __name__ == "__main__":
    main()
