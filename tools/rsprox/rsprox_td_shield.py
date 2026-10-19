#!/usr/bin/env python3
"""Tormented Demon shield and prayer, read against the recorder's own attacks.

rsprox_td.py measures the demon's tells on the demon's clock. This one lines
them up with the recorder's attack ticks, which is what showed that the shield
graphics and the prayer writes are the player's, not the demon's:

  shield   for each explosion (engage, bomb), the recorder's attacks from X - 3
           and the demon's slot-4 graphic on each: `shield` (shield_spot, a
           raised shield struck), NONE (the one unshielded attack), `restore`
  prayer   each icon write: on one of the recorder's attack ticks or not; and
           whether the 150 counted at the launch (this attack included) had
           been reached on the attack that writes
  first    the first write after an engage: how many attacks after the first
           damaging hit landed

    python3 tools/rsprox/rsprox_td_shield.py ~/.rsprox/binary/archive/{1756,3312,3308,1696}.txt

A boosted world's 2-tick attacker made `shield_spot` look like a two-tick loop
of the demon's (README, before 2026-10-19); a 4-tick recorder (1696) shows it
only on attack ticks.
"""
import argparse
import os
import re
from collections import Counter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content", "osrs239-content")
TD = set(range(13599, 13607))
OWNER = re.compile(r"\[(?:add|del|idle|walk|run|teleport|crawl)\] (npc|player)=\(index=(\d+), "
                   r"(?:id=(?:[a-z0-9_]+ \((\d+)\)|(\d+))|[^,]*?\(id=(\d+)\))?")
ID = re.compile(r"id=(?:([a-z0-9_]+) \((\d+)\)|(-?\d+))")
NOT_ATTACK = re.compile(r"^(human_eat|human_unarmedblock|human_shield_defence|-1|.*block.*)$")


def table(kind):
    out = {}
    with open(os.path.join(ROOT, "configs", "all.%s.compack" % kind), errors="replace") as handle:
        for line in handle:
            k, _, v = line.rstrip("\n").partition("=")
            if k.isdigit():
                out[k] = v
    return out


SPOT, SEQ = table("spotanim"), table("seq")


def name(tbl, text):
    m = ID.search(text)
    if not m:
        return text
    return m.group(1) or tbl.get(m.group(3), m.group(3))


def scan(path):
    """{tick: [event]}: `atk` for a recorder attack seq; (npc, kind, value)."""
    me = None
    ev = {}
    tick, owner, depth = 0, None, 0
    with open(path, errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            text = line.strip()
            indent = len(line) - len(line.lstrip())
            if me is None:
                m = re.search(r"local player index: (\d+)", text)
                if m:
                    me = m.group(1)
            m = re.match(r"^\[(\d+)\]$", text)
            if m:
                tick, owner = int(m.group(1)), None
                continue
            m = OWNER.match(text)
            if m:
                kind, index = m.group(1), m.group(2)
                npc_id = m.group(3) or m.group(4) or m.group(5)
                if kind == "npc":
                    owner = ("n", index) if npc_id and int(npc_id) in TD else None
                else:
                    owner = ("p", index) if index == me else None
                depth = indent
                continue
            if owner and indent <= depth:
                owner = None
            if not owner:
                continue
            out = ev.setdefault(tick, [])
            if owner[0] == "p":
                if text.startswith("[sequence]") and not NOT_ATTACK.match(name(SEQ, text)):
                    out.append(("atk",))
                continue
            n = owner[1]
            if text.startswith("[spotanim]"):
                slot = re.search(r"slot=(\d+)", text)
                out.append((n, "s%s" % (slot.group(1) if slot else "0"), name(SPOT, text)))
            elif text.startswith("[sequence]"):
                out.append((n, "seq", name(SEQ, text)))
            elif text.startswith("[hit]"):
                m = re.search(r"id=(\d+), value=(\d+)", text)
                if m.group(1) in ("12", "16"):
                    out.append((n, "hit", int(m.group(2))))
            elif text.startswith("[headicon]"):
                out.append((n, "icon", text.split("spriteindex=")[1]))
    return ev


def explosions(ev):
    for t in sorted(ev):
        for e in ev[t]:
            if len(e) == 3 and e[1] == "s4" and e[2].endswith("_explosion_fire_spot"):
                bomb = any(len(x) == 3 and x[2] == "entangle_casting" for x in ev[t])
                yield t, e[0], "bomb" if bomb else "engage"


def shield(ev, C):
    for t, n, kind in explosions(ev):
        seq = []
        for u in range(t - 3, t + 16):
            r = ev.get(u, [])
            g = [x[2] for x in r if len(x) == 3 and x[0] == n and x[1] == "s4"]
            if u == t:
                seq.append("X")
                continue
            if ("atk",) in r:
                word = "NONE"
                if g:
                    word = "restore" if g[0].endswith("_restore_spot") else ("shield" if g[0].endswith("_shield_spot") else g[0])
                seq.append("%+d:%s" % (u - t, word))
                if word == "restore":
                    break
        C["shield: %s" % kind][" ".join(seq)] += 1


def prayer(ev, C):
    for t, n, kind in explosions(ev):
        if kind != "engage":
            continue
        atks, hits, writes = [], [], {}
        for u in range(t - 1, t + 400):
            r = ev.get(u, [])
            if any(len(x) == 3 and x[0] == n and x[1] == "seq" and x[2].endswith("_death") for x in r):
                break
            if ("atk",) in r:
                atks.append(u)
            for x in r:
                if len(x) == 3 and x[0] == n and x[1] == "hit":
                    hits.append((u, x[2]))
                if len(x) == 3 and x[0] == n and x[1] == "icon":
                    writes[u] = x[2]
        for w in writes:
            C["prayer: icon write"]["on an attack tick" if w in atks else "not on an attack tick"] += 1
        # each landed hit to the latest attack at least two ticks before it
        launched = Counter()
        for h, v in hits:
            cand = [a for a in atks if a <= h - 2]
            if cand:
                launched[cand[-1]] += v
        acc, seen = 0, False
        first_nonzero = next((h for h, v in hits if v > 0), None)
        for a in atks:
            acc += launched[a]
            if a in writes:
                if not seen and first_nonzero is not None:
                    later = [b for b in atks if b >= first_nonzero]
                    if later:
                        C["first write: attacks after the first damaging hit"][atks.index(a) - atks.index(later[0])] += 1
                elif seen:
                    C["prayer: 150 counted at the launch, this attack included"][">= 150" if acc >= 150 else "< 150"] += 1
                acc, seen = 0, True


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("paths", nargs="+")
    args = a.parse_args()
    for path in args.paths:
        ev = scan(path)
        C = {}
        from collections import defaultdict
        C = defaultdict(Counter)
        shield(ev, C)
        prayer(ev, C)
        print("#", os.path.basename(path))
        for key in sorted(C):
            print("==", key)
            for k, v in C[key].most_common(10):
                print("   %5d  %s" % (v, k))


if __name__ == "__main__":
    main()
