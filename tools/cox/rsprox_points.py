#!/usr/bin/env python3
"""Chambers of Xeric points as Jagex's server sent them, from RSProx
transcripts: every change of the raider's score (varp raids_playerscore 4609)
and the party's (varbit raids_client_partyscore 5431), set against what
happened that tick -- the raider's hitsplats on npcs, messages, loc changes.

Sections (--section, default all):
  changes  every score change with its tick's events (--limit N per file)
  damage   points against the raider's own damage, by npc: the ratio, and how
           often a change lands on the hit's tick
  other    changes on ticks with no hit of the raider's: their sizes and the
           events beside them (the non-combat channels)
  attack   the combat channel: each of the raider's hits matched to its
           attack (the projectile launched at that npc which lands then), the
           score change on the ATTACK tick against 5 x its damage, by npc
  channels the non-combat channels: each raid potion dose with its timer
           varbit before the sip, the keystone, a lit brazier, a landed chop
           on the meat tree, and the score change on the tick and the next

    python3 tools/cox/rsprox_points.py ~/.rsprox/binary/archive/{3285,3282,843,3389,820}.txt
"""
import argparse
import re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]")
TOP = re.compile(r"^ {4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^ {8}\[(\w+)\] ?(.*)$")
SUB12 = re.compile(r"^ {12}\[(\w+)\] ?(.*)$")
NPC = re.compile(r"npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\)")
TIMER = re.compile(r"id=(raids_overload_timer|raids_prayerenhance_timer) \(\d+\), old=(-?\d+), new=(-?\d+)")
SCORE = re.compile(r"id=raids_playerscore \(4609\), old=(-?\d+), new=(-?\d+)")
PARTY = re.compile(r"id=raids_client_partyscore \(5431\), old=(-?\d+), new=(-?\d+)")


def num(text, key, default=None):
    m = re.search(r"\b" + key + r"=(-?\d+)", text)
    return int(m.group(1)) if m else default


class Rec:
    def __init__(self, path):
        self.path = path.split("/")[-1]
        self.score = {}                     # tick -> (old, new), the last change of the tick
        self.party = {}
        self.hits = defaultdict(list)       # tick -> [(npc name, hitsplat id, value, npc index)]
        self.proj = defaultdict(list)       # tick -> [(npc index, endtime)] projectiles at an npc
        self.events = defaultdict(list)     # tick -> [text]
        self.timers = {}                    # tick -> {timer: value before the tick's changes}
        timer_now = {"raids_overload_timer": 0, "raids_prayerenhance_timer": 0}
        tick = 0
        who = None
        pl = False
        last_proj = None
        for line in open(path, errors="replace"):
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                who = None
                continue
            m = TOP.match(line)
            if m:
                kind, text = m.group(1), m.group(2)
                who = None
                tm = TIMER.search(text)
                if tm:
                    self.timers.setdefault(tick, dict(timer_now))
                    timer_now[tm.group(1)] = int(tm.group(3))
                if kind.startswith("message") and "You drink some of" in text:
                    self.timers.setdefault(tick, dict(timer_now))
                last_proj = num(text, "endtime") if kind.startswith("map_projanim") else None
                pl = False
                s = SCORE.search(text)
                if s:
                    old = self.score.get(tick, (int(s.group(1)),))[0]
                    self.score[tick] = (old, int(s.group(2)))
                    continue
                p = PARTY.search(text)
                if p:
                    old = self.party.get(tick, (int(p.group(1)),))[0]
                    self.party[tick] = (old, int(p.group(2)))
                    continue
                if kind.startswith("message") or kind.startswith("loc_add") or kind.startswith("loc_del") \
                        or kind.startswith("obj_") or kind == "update_inv_partial":
                    self.events[tick].append("%s %s" % (kind, text[:90]))
                continue
            m = SUB8.match(line)
            if m:
                n = NPC.search(m.group(2))
                if m.group(1) == "target" and n and last_proj is not None:
                    self.proj[tick].append((int(n.group(1)), last_proj))
                who = (n.group(2), int(n.group(1))) if n and m.group(1) in ("add", "idle", "walk", "run", "teleport", "crawl") else None
                pl = (not n) and "player=(" in m.group(2) and m.group(1) in ("add", "idle", "walk", "run", "teleport")
                continue
            m = SUB12.match(line)
            if m and pl and m.group(1) == "sequence":
                self.events[tick].append("raider seq %s" % m.group(2).split(" (")[0].replace("id=", ""))
            if m and who and m.group(1) == "hit":
                self.hits[tick].append((who[0], num(m.group(2), "id"), num(m.group(2), "value"), who[1]))


# The raider's own hitsplats (their damage, their max hit, a block); another
# raider's are the "other" ids and never score for them.
OWN = {12, 16, 43}


def section_changes(recs, limit):
    for rec in recs:
        print("-- %s: %d score changes" % (rec.path, len(rec.score)))
        for t in sorted(rec.score)[:limit]:
            old, new = rec.score[t]
            own = [(h[0], h[2]) for h in rec.hits.get(t, []) if h[1] in OWN]
            print("  %d %+d -> %d  hits %s  %s" % (t, new - old, new, own, " | ".join(rec.events.get(t, []))[:200]))


def section_damage(recs):
    print("== damage: score change on a tick against the raider's own damage that tick")
    ratio = defaultdict(Counter)
    for rec in recs:
        for t, hs in sorted(rec.hits.items()):
            own = [(h[0], h[2]) for h in hs if h[1] in OWN]
            dmg = sum(v for n, v in own)
            if not own:
                continue
            d = rec.score[t][1] - rec.score[t][0] if t in rec.score else 0
            names = tuple(sorted(set(n for n, v in own)))
            key = "%s" % ("/".join(names))
            if dmg == 0:
                ratio[key][("0 damage", d)] += 1
            elif d % dmg == 0:
                ratio[key][("x%d" % (d // dmg),)] += 1
            else:
                ratio[key][("not a multiple", round(d / dmg, 2))] += 1
    for k, c in sorted(ratio.items()):
        print("  %s: %s" % (k, dict(c.most_common(6))))


def section_other(recs):
    print("== other: score changes with no hit of the raider's that tick")
    sizes = Counter()
    for rec in recs:
        for t in sorted(rec.score):
            own = [h for h in rec.hits.get(t, []) if h[1] in OWN]
            if own:
                continue
            old, new = rec.score[t]
            sizes[new - old] += 1
            print("  %s %d %+d  %s" % (rec.path, t, new - old, " | ".join(rec.events.get(t, []))[:220]))
    print("  sizes:", dict(sizes.most_common(20)))


def launches_of(rec):
    """{attack tick: [(npc name, damage)]}: each own hitsplat inside the raid
    matched to the projectile at that npc that lands on its tick."""
    if not rec.score:
        return {}
    lo, hi = min(rec.score) - 10, max(rec.score)
    out = defaultdict(list)
    for L, hs in rec.hits.items():
        if not lo <= L <= hi:
            continue
        for name, i, v, idx in hs:
            if i not in OWN:
                continue
            for T in range(L, L - 8, -1):
                if [e for nidx, e in rec.proj.get(T, []) if nidx == idx and T - (-e // 30) == L]:
                    out[T].append((name, v))
                    break
    return out


def section_attack(recs):
    print("== attack: the score change on the attack tick against 5 x the damage it lands")
    tab = defaultdict(Counter)
    for rec in recs:
        for T, ls in launches_of(rec).items():
            if len(ls) != 1 or T not in rec.score:
                continue
            name, v = ls[0]
            g = rec.score[T][1] - rec.score[T][0]
            if v:
                tab[name]["5 x damage" if g == 5 * v else ("less" if g < 5 * v else "more")] += 1
    for n, c in sorted(tab.items()):
        print("  %-34s %s" % (n, dict(c.most_common())))


def section_channels(recs):
    print("== channels: (gain on the tick, gain the tick after)")
    res = defaultdict(Counter)
    for rec in recs:
        if not rec.score:
            continue
        lo, hi = min(rec.score), max(rec.score)

        def gain(u):
            return rec.score[u][1] - rec.score[u][0] if u in rec.score else 0
        for t in range(lo, hi + 1):
            for e in rec.events.get(t, []):
                m = re.search(r"You drink some of your strong (overload|prayer enhance) potion", e)
                if m:
                    tv = rec.timers.get(t, {})
                    key = "raids_overload_timer" if m.group(1) == "overload" else "raids_prayerenhance_timer"
                    res["strong %s, its timer %s" % (m.group(1), tv.get(key))][(gain(t), gain(t + 1))] += 1
                if "keystone glows" in e:
                    res["keystone"][(gain(t), gain(t + 1))] += 1
                if "raids_icedemon_brazier_lit" in e:
                    res["brazier lit"][(gain(t), gain(t + 1))] += 1
                if "hack away" in e:
                    res["meat tree chop landed"][(gain(t), gain(t + 1))] += 1
    for k, c in sorted(res.items()):
        print("  %-40s %s" % (k, dict(c.most_common(8))))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=["changes", "damage", "other", "attack", "channels"])
    ap.add_argument("--limit", type=int, default=40)
    a = ap.parse_args()
    recs = [Rec(p) for p in a.transcripts]
    if a.section in (None, "changes"):
        section_changes(recs, a.limit)
    if a.section in (None, "damage"):
        section_damage(recs)
    if a.section in (None, "other"):
        section_other(recs)
    if a.section in (None, "attack"):
        section_attack(recs)
    if a.section in (None, "channels"):
        section_channels(recs)


if __name__ == "__main__":
    main()
