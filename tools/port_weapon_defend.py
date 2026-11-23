#!/usr/bin/env python3
"""port_weapon_defend.py -- the player's block animation for every weapon and shield, from RSProx.

Written 2026-11-23. `[proc,combat_defend_anim]` (skill_combat/combat.rs2) plays the shield's
`defend_anim` when it is not `human_unarmedblock`, else the weapon's. The generated obj layer
states `defend_anim,human_unarmedblock` for every one of the 32,865 objs, and only two sources
replaced it: LostCity's 210 weapons (attack_anims.obj) and the rsmod port (attack_anims_modern.obj,
which has no block for many weapons rsmod does not state one for). So every other weapon blocked
bare-handed: the dragon scimitar (RSProx 4628 [3082]: human_sword_def), the thunder khopesh,
every axe and pickaxe, the blowpipes, the sceptres; and every shield without a stated block let
the weapon's show instead (the elemental shield, the dragonfire shield, the spectral, the
non-dragon parrying daggers).

What it does:

1. MINE every --bas transcript in the index cache (home-bas, home-all-bas): for each splat on a
   player, the righthand/lefthand they wore (their appearance block) and the [sequence] in their
   player block on that tick.
2. ATTRIBUTE each (weapon, offhand, block seq): a seq of the offhand kind (one that our data or
   the recordings give to a wearpos-5 item: human_shield_defence, warguild_parry_defend, ...)
   is the offhand's; any other block-like seq is the weapon's.
3. RECORDED rows: an obj whose attributed seqs agree on one block (>= 90% of its hits).
4. CATEGORY rows: an obj with no block stated anywhere but the generated layer, whose cache
   `category` (same wearpos) agrees on one block (>= 90% of its members that have one, from
   rank 3 and our hand/generator overlays, at least 2 members).
5. WRITE skill_combat/configs/bas/defend_anims_recorded.obj (recorded rows, then category rows,
   each with its evidence). A recorded row that contradicts another overlay is reported: that
   overlay's line has to go (--conflicts).

    python3 tools/port_weapon_defend.py --summary
    python3 tools/port_weapon_defend.py --conflicts
    python3 tools/port_weapon_defend.py --write
"""
import argparse
import collections
import glob
import json
import os
import re
import subprocess
import sys
import concurrent.futures as cf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs-content")
SCRIPTS = os.path.join(CONTENT, "server", "scripts")
OUT = os.path.join(SCRIPTS, "skill_combat", "configs", "bas", "defend_anims_recorded.obj")
CACHE = os.path.expanduser("~/.rsprox-index-cache")
UNARMED = "human_unarmedblock"
BLOCKISH = re.compile(r"block|_def|defend|defence|parry|guard")
# Handled by content, not a param (gear/obligator.rs2: the wearer's gender).
SCRIPTED = {"obligator"}

PLAYER = re.compile(r"^        \[(?:idle|walk|run|teleport|add|crawl|telejump)\] player=\(index=(\d+)")
WEAR = re.compile(r"^\s+- wearpos=(righthand|lefthand), id=(\S+) \(")
SEQ = re.compile(r"^            \[sequence\] id=([a-z0-9_\-]+) \((-?\d+)\)")
HIT = re.compile(r"^            \[hit\] id=(\d+), value=(\d+)")
TICK = re.compile(r"^\[(\d+)\]\s*$")


def mine_one(path):
    """(rh, lh) -> Counter(seq) for one transcript."""
    counts = collections.defaultdict(collections.Counter)
    gear = {}
    cur = None
    in_app = False
    hits = 0
    seq = None

    def flush():
        if cur is not None and hits:
            g = gear.get(cur, {})
            counts[(g.get("righthand", "-"), g.get("lefthand", "-"))][seq or "(none)"] += 1

    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if TICK.match(line) or (line.startswith("    [") and not line.startswith("        ")):
                flush()
                cur, hits, seq, in_app = None, 0, None, False
                continue
            m = PLAYER.match(line)
            if m:
                flush()
                cur, hits, seq, in_app = int(m.group(1)), 0, None, False
                continue
            if cur is None:
                continue
            if line.startswith("        [") and not line.startswith("         "):
                flush()
                cur, hits, seq = None, 0, None
                continue
            if "[appearance]" in line:
                in_app = True
                gear[cur] = {}
                continue
            if in_app:
                w = WEAR.match(line)
                if w:
                    gear[cur][w.group(1)] = w.group(2)
                    continue
                if line.startswith("            [") and not line.startswith("                "):
                    in_app = False
            s = SEQ.match(line)
            if s:
                seq = s.group(1)
                continue
            if HIT.match(line):
                hits += 1
        flush()
    return os.path.basename(path), counts


def mine():
    files = sorted(set(glob.glob(os.path.join(CACHE, "home-bas", ".rsprox", "binary", "archive", "idx*.txt"))
                       + glob.glob(os.path.join(CACHE, "home-all-bas", ".rsprox", "binary", "archive", "idx*.txt"))))
    total = collections.defaultdict(collections.Counter)
    sessions = collections.defaultdict(set)
    with cf.ThreadPoolExecutor(8) as ex:
        # mine_one is pure Python; threads still overlap the file reads
        for sess, counts in ex.map(mine_one, files):
            for k, v in counts.items():
                total[k].update(v)
                sessions[k].add(sess.replace("idx", "").replace(".txt", ""))
    return total, sessions, len(files)


def load_cache_objs():
    objs = {}
    cur = None
    with open(os.path.join(CONTENT, "configs", "all.obj"), encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"^\[([^\]]+)\]", line)
            if m:
                cur = objs.setdefault(m.group(1), {"wearpos": None, "category": None, "name": None})
                continue
            if cur is None:
                continue
            if line.startswith("wearpos=") and cur["wearpos"] is None:
                v = line.strip().split("=", 1)[1]
                cur["wearpos"] = v if v != "default" else None
            elif line.startswith("category=") and cur["category"] is None:
                v = line.strip().split("=", 1)[1]
                cur["category"] = v if v != "default" else None
            elif line.startswith("name=") and cur["name"] is None:
                cur["name"] = line.strip().split("=", 1)[1]
    return objs


def load_overlays():
    """obj -> (anim, file) from every non-generated .obj, except our own output."""
    out = {}
    for dp, dn, fn in os.walk(SCRIPTS):
        dn[:] = [d for d in dn if not d.startswith("build")]
        for name in sorted(fn):
            if not name.endswith(".obj"):
                continue
            path = os.path.join(dp, name)
            if ".generated." in name or "_explicit" in path or path == OUT:
                continue
            cur = None
            for line in open(path, encoding="utf-8", errors="replace"):
                m = re.match(r"^\[([^\]]+)\]", line)
                if m:
                    cur = m.group(1)
                    continue
                m = re.match(r"^param=defend_anim,([a-z0-9_]+)", line)
                if m and cur:
                    out[cur] = (m.group(1), os.path.relpath(path, SCRIPTS))
    return out


def decide(counter, need=0.9):
    tot = sum(counter.values())
    if not tot:
        return None
    anim, n = counter.most_common(1)[0]
    if n / tot < need:
        return None
    return anim, n, tot


def build():
    total, sessions, nfiles = mine()
    objs = load_cache_objs()
    overlays = load_overlays()

    offhand = set(a for o, (a, _) in overlays.items() if objs.get(o, {}).get("wearpos") == "5" and a != UNARMED)
    offhand |= {"human_shield_defence", "warguild_parry_defend"}

    ev = collections.defaultdict(collections.Counter)      # obj -> Counter(anim)
    ev_sess = collections.defaultdict(set)
    for (rh, lh), c in total.items():
        for seq, n in c.items():
            if not BLOCKISH.search(seq):
                continue
            if seq in offhand and lh != "-":
                ev[lh][seq] += n
                ev_sess[lh] |= sessions[(rh, lh)]
            elif rh != "-":
                ev[rh][seq] += n
                ev_sess[rh] |= sessions[(rh, lh)]
            # a block with no weapon and no offhand shield is the unarmed block: nothing to write

    recorded = {}
    for o, c in ev.items():
        if o in SCRIPTED or o not in objs:
            continue
        d = decide(c)
        if d:
            recorded[o] = (d[0], d[1], d[2], sorted(ev_sess[o]))

    # category consensus over the objs whose block is known: recorded, else a hand/generator overlay
    known = {}
    for o, (a, _) in overlays.items():
        known[o] = a
    for o, r in recorded.items():
        known[o] = r[0]
    bycat = collections.defaultdict(collections.Counter)
    for o, a in known.items():
        info = objs.get(o)
        if not info or not info["category"] or info["wearpos"] not in ("3", "5"):
            continue
        bycat[(info["wearpos"], info["category"])][a] += 1
    category = {}
    for o, info in objs.items():
        if o in known or o in SCRIPTED or info["wearpos"] not in ("3", "5") or not info["category"]:
            continue
        if o.startswith(("cert_", "placeholder_")):
            continue
        c = bycat.get((info["wearpos"], info["category"]))
        if not c or sum(c.values()) < 2:
            continue
        d = decide(c)
        if d and d[0] != UNARMED:
            category[o] = (d[0], d[1], d[2], info["category"], info["wearpos"])

    conflicts = []
    for o, r in recorded.items():
        if o in overlays and overlays[o][0] != r[0]:
            conflicts.append((o, overlays[o][0], overlays[o][1], r))
    return dict(nfiles=nfiles, recorded=recorded, category=category, conflicts=conflicts,
                overlays=overlays, objs=objs)


def write(res):
    rec, cat, overlays = res["recorded"], res["category"], res["overlays"]
    lines = [
        "// The player's block animation (defend_anim) for weapons and shields, from RSProx.",
        "//",
        "// GENERATED by tools/port_weapon_defend.py --write -- do not hand-edit; rerun it. The",
        "// procedure and its thresholds are in the tool's docstring. `[proc,combat_defend_anim]`",
        "// plays the shield's block when it is not human_unarmedblock, else the weapon's.",
        "//",
        "// Mined from %d --bas transcripts (~/.rsprox-index-cache home-bas + home-all-bas): every" % res["nfiles"],
        "// splat on a player, the hands they wore and the sequence on that tick.",
        "//",
        "// RECORDED: the obj's own hits agree on one block (n/total, sessions). An obj another",
        "// overlay also states is written here only when the recording contradicts it, and that",
        "// overlay's line is deleted (the tool's --conflicts list).",
        "// CATEGORY: no block of its own anywhere; its cache category agrees on one (members/known).",
        "",
    ]
    written = 0
    for o in sorted(rec):
        a, n, tot, sess = rec[o]
        if o in overlays and overlays[o][0] == a:
            continue
        s = ",".join(sess[:6]) + ("..." if len(sess) > 6 else "")
        lines += ["[%s]" % o, "// RECORDED %d/%d hits, index %s" % (n, tot, s), "param=defend_anim,%s" % a, ""]
        written += 1
    for o in sorted(cat):
        a, n, tot, c, wp = cat[o]
        lines += ["[%s]" % o, "// CATEGORY %s (wearpos %s): %d of %d with a known block" % (c, wp, n, tot),
                  "param=defend_anim,%s" % a, ""]
        written += 1
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return written


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--conflicts", action="store_true")
    ap.add_argument("--list", action="store_true", help="every row with its evidence")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    res = build()
    rec, cat = res["recorded"], res["category"]
    new_rec = [o for o in rec if not (o in res["overlays"] and res["overlays"][o][0] == rec[o][0])]
    print("transcripts %d; recorded objs %d (%d agree with an overlay, %d new or contradicting); category fills %d; conflicts %d"
          % (res["nfiles"], len(rec), len(rec) - len(new_rec), len(new_rec), len(cat), len(res["conflicts"])))
    if args.summary:
        byanim = collections.Counter(v[0] for v in cat.values())
        print("category fills by anim:", ", ".join("%s %d" % x for x in byanim.most_common()))
        bycat = collections.Counter((v[4], v[3], v[0]) for v in cat.values())
        for (wp, c, a), n in bycat.most_common(40):
            print("  wearpos %s category %-5s -> %-28s %d" % (wp, c, a, n))
    if args.conflicts:
        for o, old, path, r in res["conflicts"]:
            print("CONFLICT %-30s %s says %s; recorded %s %d/%d (%s)" % (o, path, old, r[0], r[1], r[2], ",".join(r[3][:4])))
    if args.list:
        for o in sorted(new_rec):
            a, n, tot, sess = rec[o]
            print("REC  %-34s %-28s %d/%d %s" % (o, a, n, tot, ",".join(sess[:4])))
        for o in sorted(cat):
            print("CAT  %-34s %-28s cat %s" % (o, cat[o][0], cat[o][3]))
    if args.write:
        n = write(res)
        print("wrote %d rows to %s" % (n, os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
