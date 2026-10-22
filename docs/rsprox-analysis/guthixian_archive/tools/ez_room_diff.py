#!/usr/bin/env python3
"""Ezekiel's room, recording against ours, section by section: every map
anim, area sound, loc change and song of the room, with its tile relative to
Ezekiel's south-west tile on the section's anchor tick (so the template and
instance frames drop out), and its tick relative to the anchor.

    python3 ez_room_diff.py <transcript> <ticklog.tsv> REC_T:OUR_T:LABEL:LEN [...] [--all] [--tiles]

Without --all only the rows one side has and the other lacks are printed
(same relative tick, kind, id, delay and tile; a map anim's tile set is
compared as a set per tick and id). The raiders' kit (thralls, specials,
glaive) is left out.
"""
import re
import sys
from collections import Counter

KIT_SOUNDS = {211, 212}
KIT_ANIM = re.compile(r"dragon_halberd|thrall|glaive|blood|sang|xbows|leagues|dragon_claw|voidwaker")
TICK = re.compile(r"^\[(\d+)\]")
BOSS = re.compile(r"^ {8}\[\w+\] npc=\(index=\d+, id=tfa_ezekiel \(16652\), coord=\((\d+), (\d+), \d+\)\)(?:, newcoord=\((\d+), (\d+))?")
ANIM = re.compile(r"\[map_anim\] id=(\w+) \((\d+)\)(?:, delay=(\d+))?(?:, height=\d+)?, coord=\((\d+), (\d+), \d+\)")
SOUND = re.compile(r"\[sound_area\] id=(\d+), loops=\d+(?:, delay=(\d+))?, range=(\d+)(?:, size=(\d+))?, coord=\((\d+), (\d+), \d+\)")
LOC = re.compile(r"\[(loc_add_change\w*|loc_del)\] (?:id=(\w+) \((\d+)\), )?coord=\((\d+), (\d+), \d+\), shape=(\d+), rotation=(\d+)")
SONG = re.compile(r"\[midi_song\w*\] id=(-?\d+)(.*)")


def recording(path, t0, t1):
    rows, boss = [], {}
    t = 0
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            t = int(m.group(1))
            continue
        if t < t0 - 5 or t > t1:
            continue
        m = BOSS.match(line)
        if m:
            boss[t] = (int(m.group(3) or m.group(1)), int(m.group(4) or m.group(2)))
            continue
        m = ANIM.search(line)
        if m and not KIT_ANIM.search(m.group(1)):
            rows.append((t, "anim", int(m.group(2)), int(m.group(3) or 0), (int(m.group(4)), int(m.group(5)))))
            continue
        m = SOUND.search(line)
        if m and int(m.group(1)) not in KIT_SOUNDS:
            rows.append((t, "sound", int(m.group(1)), int(m.group(2) or 0),
                         (int(m.group(5)), int(m.group(6))), f"r{m.group(3)} s{m.group(4) or 0}"))
            continue
        m = LOC.search(line)
        if m:
            rows.append((t, "loc", int(m.group(3) or -1), 0, (int(m.group(4)), int(m.group(5))),
                         f"shape{m.group(6)} rot{m.group(7)}"))
            continue
        m = SONG.search(line)
        if m:
            env = re.findall(r"=(\d+)", m.group(2))
            rows.append((t, "song", int(m.group(1)), 0, None, "fade " + " ".join(env)))
    return rows, boss


def xz(c):
    return (c >> 14) & 0x3FFF, c & 0x3FFF


def ours(path, t0, t1):
    rows, boss = [], {}
    slot = None
    for line in open(path, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 9 or not f[1].isdigit():
            continue
        t, kind, v = int(f[1]), f[2], f[3:]
        label = f[9] if len(f) > 9 else ""
        if kind == "npc_spawn" and v[1] == "16652":
            slot = v[0]
            boss[t] = xz(int(v[2]))
        if kind == "npc_tile" and v[0] == slot:
            boss[t] = (int(v[1]), int(v[2]))
        if t < t0 - 5 or t > t1:
            continue
        if kind == "map_spotanim":
            rows.append((t, "anim", int(v[1]), int(v[3]), xz(int(v[0]))))
        elif kind == "sound" and "area" in label:
            s = re.search(r"size=(\d+)", label)
            rows.append((t, "sound", int(v[1]), int(v[3]), xz(int(v[4])),
                         f"r{v[5]} s{s.group(1) if s else 0}"))
        elif kind == "loc_set":
            rows.append((t, "loc", int(v[1]), 0, xz(int(v[0])), f"shape{v[2]} rot{v[3]}"))
        elif kind == "music":
            rows.append((t, "song", int(v[1]), 0, None, f"fade {v[2]} {v[3]} {v[4]} {v[5]}"))
    return rows, boss


def at(boss, t):
    best = None
    for k in sorted(boss):
        if k <= t:
            best = boss[k]
    return best


def keyed(rows, anchor, origin, length, tiles):
    out = []
    for r in rows:
        rel = r[0] - anchor
        if rel < 0 or rel > length:
            continue
        # Relative to where he stands on the row's own tick (he walks and
        # jumps inside a section).
        here = at(origin, r[0])
        tile = None if r[4] is None else (r[4][0] - here[0], r[4][1] - here[1])
        # A map anim's tiles follow where he stood; compared by count unless
        # --tiles.
        if r[1] == "anim" and not tiles:
            tile = "*"
        extra = r[5] if len(r) > 5 else ""
        out.append((rel, r[1], r[2], r[3], tile, extra))
    return Counter(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    show_all = "--all" in sys.argv
    tiles = "--tiles" in sys.argv
    rec_path, our_path = args[0], args[1]
    for spec in args[2:]:
        rt, ot, label, length = spec.split(":")
        rt, ot, length = int(rt), int(ot), int(length)
        rrows, rboss = recording(rec_path, rt, rt + length)
        orows, oboss = ours(our_path, ot, ot + length)
        rk = keyed(rrows, rt, rboss, length, tiles)
        ok = keyed(orows, ot, oboss, length, tiles)
        print(f"== {label}: recording [{rt}] boss {at(rboss, rt)}, ours [{ot}] boss {at(oboss, ot)}")
        both = sorted(set(rk) | set(ok), key=lambda k: (k[0], k[1], k[2], k[3], str(k[4])))
        matched = 0
        for k in both:
            r, o = rk.get(k, 0), ok.get(k, 0)
            matched += min(r, o)
            if show_all or r != o:
                print(f"  +{k[0]:3d} {k[1]:5s} {k[2]:6d} d{k[3]:<4d} {str(k[4]):12s} {k[5]:22s} R{r} O{o}")
        print(f"  matched {matched}, recording {sum(rk.values())}, ours {sum(ok.values())}")


main()
