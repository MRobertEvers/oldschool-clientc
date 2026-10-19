#!/usr/bin/env python3
"""The Chambers of Xeric thieving room (the cavern grub chests and the corrupted
scavenger) as Jagex's server sent it, from an RSProx transcript in INSTANCE
tiles (TRANSLATE_INSTANCED_COORDS=false; docs/rsprox-analysis/cox/README.md,
"Instance tiles"). Only 3389 (solo, Challenge Mode) holds the room.

Ids: the scavenger raids_thievingchest_beast_active 7602 (asleep: 7603); the
chest raids_thievingchest_closed 29742, hatched 29745; the trough empty 29746,
full 29874; the grubs obj 20885.

Sections (--section, default all):
  searches  every chest click: the raider's arrival, his seqs, the result's
            tick (message, sounds, the chest's loc, grubs and Thieving xp
            gained), and when the chest is closed again
  trough    every trough click: the seqs, messages, sounds, the loc, the
            grubs taken; every trough loc change
  scavenger its seqs (eating), its bar, its says, its transformation and
            graphics, its steps, the sounds round it
  reward    what lands in the pack or at the raider's feet when it is full

    python3 tools/cox/rsprox_thieving.py <raw>/archive/3389.txt
"""
import argparse
import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_mystics as rm  # noqa: E402

BEAST = {7602, 7603}
rm.MYSTIC = BEAST
GRUBS = 20885
INV = re.compile(r"slot=(\d+), id=[a-z0-9_]+ \((\d+)\), count=(\d+)")
XP = re.compile(r"\[update_stat_v2\] stat=(\w+), level=\d+, invisiblelevel=\d+, experience=(\d+)")


def tops(rec, t, kind):
    return [(tx, subs) for k, tx, subs in rec.top.get(t, []) if k == kind]


class Pack:
    """The local raider's grubs, tick by tick, from the raw inventory rows
    (an update's slot lines are "- slot=..." under it)."""

    def __init__(self, rec):
        self.slots = {}
        self.at = {}
        tick = 0
        inv = False
        for line in open(rec.path_full, errors="replace"):
            m = rm.TICK.match(line)
            if m:
                tick = int(m.group(1))
                inv = False
                continue
            if line.startswith("    ["):
                inv = ("update_inv_partial" in line or "update_inv_full" in line) and "id=inv (93)" in line
                if inv and "update_inv_full" in line:
                    self.slots = {}
                continue
            if inv and line.lstrip().startswith("- slot="):
                mm = INV.search(line)
                if mm:
                    self.slots[int(mm.group(1))] = (int(mm.group(2)), int(mm.group(3)))
                self.at[tick] = sum(c for o, c in self.slots.values() if o == GRUBS)
        self.ticks = sorted(self.at)

    def grubs(self, t):
        best = 0
        for k in self.ticks:
            if k > t:
                break
            best = self.at[k]
        return best


def xp_changes(path):
    out = {}
    tick = 0
    last = None
    for line in open(path, errors="replace"):
        m = rm.TICK.match(line)
        if m:
            tick = int(m.group(1))
            continue
        x = XP.search(line)
        if x and x.group(1) == "thieving":
            v = int(x.group(2))
            if last is not None and v != last:
                out[tick] = v - last
            last = v
    return out


def window(rec):
    beasts = [i for i in rec.npc_add if rec.npc_id.get(i) in BEAST]
    lo = min(rec.npc_add[i][0] for i in beasts)
    hi = max(rec.npc_del.get(i, max(rec.npc_tile[i])) for i in beasts)
    return lo, hi, beasts


def section_searches(recs):
    print("== searches: A = the raider's arrival (his last step before the result), R = the result's tick")
    for rec in recs:
        local = rm.first_raider(rec)
        pack = Pack(rec)
        lo, hi, _ = window(rec)
        tiles = rec.pl_tile[local]
        stats = Counter()
        last_close = {}
        for t in range(lo, hi + 1):
            for tx, subs in tops(rec, t, "loc_add_change_v2"):
                if "thievingchest_closed" in tx:
                    c = rm.COORD.search(tx).group(0)
                    if c in last_close:
                        stats[("hatched -> closed after", t - last_close.pop(c))] += 1
            for tx, subs in tops(rec, t, "loc_add_change_v2"):
                if "eggs_hatched" in tx:
                    last_close[rm.COORD.search(tx).group(0)] = t
            res = [tx for tx, s in tops(rec, t, "message_game") if "grubs" in tx]
            if not res:
                continue
            seqs = [(tt - t, rm.ident(x)[0]) for tt, k, x in rec.pl[local] if k == "sequence" and t - 6 <= tt <= t]
            moves = [tt for tt in range(t - 10, t + 1) if tt in tiles and rec.tile_at(tiles, tt - 1) != tiles[tt]]
            clicks = [tt - t for tt in range(t - 15, t + 1) for tx, s in tops(rec, tt, "oploc1") if "thievingchest" in tx]
            snd = [(rm.sound_name(sid), k, d, rng) for k, sid, d, rng, lp, at in rm.sounds(rec, t)]
            locs = [rm.ident(tx)[0] for tx, s in tops(rec, t, "loc_add_change_v2") if "thieving" in tx]
            gained = pack.grubs(t) - pack.grubs(t - 1)
            print("   R=%d: %s; seqs %s; last move %s; clicks %s; sounds %s; locs %s; grubs +%d (held %d)" % (
                t, res[0].split("message=")[1][:60], seqs, (moves[-1] - t) if moves else None, clicks[-2:], snd, locs,
                gained, pack.grubs(t)))
            stats[("grubs", gained)] += 1
            if seqs:
                stats[("openchest at R", seqs[-1][0])] += 1
        for k, n in sorted(stats.items(), key=str):
            print("   %4d  %s" % (n, k))


def section_trough(recs):
    print("== trough")
    for rec in recs:
        local = rm.first_raider(rec)
        pack = Pack(rec)
        lo, hi, _ = window(rec)
        for t in range(lo, hi + 1):
            for tx, s in tops(rec, t, "oploc1") + tops(rec, t, "oplocu"):
                if "foodtrough" in tx:
                    print("   %d click %s" % (t, tx[:80]))
            for tx, s in tops(rec, t, "loc_add_change_v2"):
                if "foodtrough" in tx:
                    print("   %d loc %s" % (t, tx[:90]))
            d = pack.grubs(t) - pack.grubs(t - 1)
            if d < 0:
                seqs = [rm.ident(x)[0] for tt, k, x in rec.pl[local] if k == "sequence" and tt == t]
                msgs = [x.split("message=")[1][:70] for x, s in tops(rec, t, "message_game")]
                snd = [(rm.sound_name(sid), k) for k, sid, dl, rng, lp, at in rm.sounds(rec, t)]
                print("   %d grubs %d (held %d -> %d); seqs %s; messages %s; sounds %s" % (
                    t, d, pack.grubs(t - 1), pack.grubs(t), seqs, msgs, snd))


def section_scavenger(recs):
    print("== scavenger")
    for rec in recs:
        lo, hi, beasts = window(rec)
        for b in beasts:
            print("   npc %d added %d at %s, removed %s" % (b % 1000, rec.npc_add[b][0], rec.npc_add[b][1], rec.npc_del.get(b)))
            eats = []
            for t, k, tx in rec.npc[b]:
                if k in ("sequence", "say", "transformation", "spotanim", "walk", "run"):
                    print("     %d %s %s" % (t, k, tx[:80]))
                    if k == "sequence" and "eat" in tx:
                        eats.append(t)
            bars = [(t, rm.num(tx, "fill")) for t, k, tx in rec.npc[b] if k == "headbar"]
            fills = []
            for t, f in bars:
                if not fills or fills[-1][1] != f:
                    fills.append((t, f))
            print("     bar changes: %s" % fills)
            print("     eat gaps: %s" % [b_ - a for a, b_ in zip(eats, eats[1:])])
        for t in range(lo, hi + 1):
            for k, sid, d, rng, lp, at in rm.sounds(rec, t):
                if sid in (869, 1734, 2739, 3637):
                    print("   %d sound %s %s delay %s range %s at %s" % (t, rm.sound_name(sid), k, d, rng, at))


def section_reward(recs):
    print("== reward: objs added and pack changes after the scavenger tires")
    for rec in recs:
        lo, hi, beasts = window(rec)
        tired = [t for b in beasts for t, tx in rm.npc_events(rec, b, "sequence") if "tired" in tx]
        if not tired:
            continue
        t0 = tired[0]
        for t in range(t0 - 12, t0 + 12):
            for k, tx, subs in rec.top.get(t, []):
                if k in ("obj_add_specific", "update_inv_partial", "message_game"):
                    print("   %+d %s %s %s" % (t - t0, k, tx[:100], " ".join(st for sk, st in subs)[:120]))


SECTIONS = dict(searches=section_searches, trough=section_trough, scavenger=section_scavenger, reward=section_reward)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=sorted(SECTIONS), action="append")
    a = ap.parse_args()
    recs = []
    for path in a.transcripts:
        rec = rm.Rec(path)
        rec.path_full = path
        if rec.translated:
            sys.exit("%s: translated coordinates; transcribe untranslated" % path)
        recs.append(rec)
    for n in a.section or SECTIONS:
        SECTIONS[n](recs)


if __name__ == "__main__":
    main()
