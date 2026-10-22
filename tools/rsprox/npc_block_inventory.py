#!/usr/bin/env python3
"""Every NPC_INFO block kind each npc type gets in the recordings, against our
tick log: the inventory that catches a whole KIND an audit never looked at.

npc_timeline.py and rsprox_tells.py print a chosen set of blocks; a kind
outside it (Ezekiel's `[bas]` and `[body_customisation]`, 2026-10-22) is
never compared, and the content ships without it. This lists every kind the
transcript holds, per npc type, with the count on each side and the distinct
values of the kinds that carry one (stances, models, transformations, ops,
headicons, says), and flags each kind the recording has and ours never sent.

    python3 tools/rsprox/npc_block_inventory.py <transcript>... --ours <ticklog.tsv>... \\
        [--match '<npc name regex>'] [--values]

Our side comes from the tick log, file-only `npc_info` rows included:
  sequence npc_anim | spotanim npc_spotanim | facing npc_face | say npc_say
  transformation npc_retype | headicon_customisation npc_headicon
  body_customisation npc_body | headbar npc_headbar / npc_info timedbar
  hit hit_npc | bas, enabled_ops, transparency, level_change, exact_move:
  npc_info rows labelled bas, ops, transparency, level, exactmove
"""
import argparse
import re
from collections import Counter, defaultdict

HDR = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\)")
BLOCK = re.compile(r"^ {12}\[([a-z_]+)\](.*)")
SUB = re.compile(r"^ {16}\[([a-z_]+)\](.*)")
VALUED = {"bas", "body_customisation", "transformation", "enabled_ops", "headicon_customisation", "say",
          "spotanim"}
SPOT_ID = re.compile(r"id=(?:[a-z0-9_]+ )?\(?(-?\d+)\)?")
OURS = {
    "npc_anim": "sequence",
    "npc_spotanim": "spotanim",
    "npc_face": "facing",
    "npc_say": "say",
    "npc_retype": "transformation",
    "npc_headicon": "headicon_customisation",
    "npc_body": "body_customisation",
    "npc_headbar": "headbar",
    "hit_npc": "hit",
}
INFO = {"bas": "bas", "ops": "enabled_ops", "transparency": "transparency", "level": "level_change",
        "exactmove": "exact_move", "timedbar": "headbar"}


def recorded(paths, match):
    count = defaultdict(Counter)
    values = defaultdict(lambda: defaultdict(Counter))
    names = {}
    for path in paths:
        cur = None
        last = None
        for line in open(path, errors="replace"):
            line = line.rstrip("\n")
            m = HDR.match(line)
            if m:
                cur = int(m.group(4)) if (not match or re.search(match, m.group(3))) else None
                if cur is not None:
                    names[cur] = m.group(3)
                continue
            if line.startswith("        [") or line.startswith("    ["):
                cur = None
                continue
            if cur is None:
                continue
            m = BLOCK.match(line)
            if m:
                kind = m.group(1)
                count[cur][kind] += 1
                last = kind
                if kind == "spotanim":
                    sm = SPOT_ID.search(m.group(2))
                    values[cur][kind]["id %s %s" % (sm.group(1) if sm else "?", m.group(2).strip()[:60])] += 1
                elif kind in VALUED:
                    values[cur][kind][m.group(2).strip() or "(empty)"] += 1
                continue
            m = SUB.match(line)
            if m and last == "body_customisation":
                values[cur]["body_customisation"]["model " + m.group(2).strip()] += 1
    return count, values, names


def ours(paths):
    count = defaultdict(Counter)
    values = defaultdict(lambda: defaultdict(Counter))
    for path in paths:
        for line in open(path, errors="replace"):
            f = line.rstrip("\n").split("\t")
            if len(f) < 10 or not f[1].lstrip("-").isdigit():
                continue
            kind = f[2]
            if kind == "npc_info":
                k = INFO.get(f[9].split(" ")[0])
                typ = int(f[4])
                if k:
                    count[typ][k] += 1
                    if k == "bas":
                        values[typ][k]["ready %s walk %s" % (f[6], f[7])] += 1
                    elif k == "enabled_ops":
                        # The log keeps the hidden mask; the wire sends the
                        # enabled one (`opflags=`).
                        values[typ][k]["opflags=0b{:05b}".format(~int(f[6]) & 31)] += 1
                continue
            # hit_npc rows name a slot, not a type: hits are left to the
            # damage audit (rsprox_player_hits.py), not counted here.
            if kind not in OURS or kind == "hit_npc":
                continue
            typ = int(f[4])
            k = OURS[kind]
            count[typ][k] += 1
            if k == "body_customisation":
                values[typ][k]["model %s" % f[5]] += 1
            elif k == "transformation":
                values[typ][k][f[5]] += 1
            elif k == "say":
                values[typ][k][f[9]] += 1
            elif k == "spotanim":
                values[typ][k]["id %s" % f[5]] += 1
    return count, values


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--ours", nargs="+", required=True)
    ap.add_argument("--match", default=None)
    ap.add_argument("--values", action="store_true")
    a = ap.parse_args()
    rc, rv, names = recorded(a.transcripts, a.match)
    oc, ov = ours(a.ours)
    gaps = 0
    for typ in sorted(rc, key=lambda t: names.get(t, "")):
        print("== %s (%d)" % (names.get(typ, typ), typ))
        for kind in sorted(rc[typ]):
            r, o = rc[typ][kind], oc[typ][kind]
            flag = "MISSING" if o == 0 and kind != "hit" else ""
            gaps += bool(flag)
            print("   %-24s recorded %6d   ours %6d  %s" % (kind, r, o, flag))
            if a.values and kind in VALUED:
                for v, n in rv[typ][kind].most_common(8):
                    print("        R %4d  %s" % (n, v[:110]))
                for v, n in ov[typ][kind].most_common(8):
                    print("        O %4d  %s" % (n, v[:110]))
    print("kinds recorded and never sent: %d" % gaps)


if __name__ == "__main__":
    main()
