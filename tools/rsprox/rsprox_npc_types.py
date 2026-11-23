#!/usr/bin/env python3
"""rsprox_npc_types.py <transcript>... : every npc TYPE the server added in a recording, and
whether our content does anything with it.

Written after Desert Treasure I (2026-11-23). The rebuild worked section by section (one window
of ticks per boss, scene and area), and the Stranger -- npc 685 `fourdiamonds_assasin`, the
assassin a carried diamond calls -- was added, said his line and fought the recorder in a stretch
no section covered (index 4628 [2962..3170], shopping at the Grand Exchange). Nothing listed the
npc types the sessions held, so nobody asked what he was.

For each type (pooled over every transcript given), one row:

    count of adds, sessions it appears in, the first add (session [tick] tile), the first
    [say] on an add, and our reference:

    SCRIPTED    named in a .rs2 / non-generated .npc / .constant / .enum / .dbrow under --scripts
    SPAWNED     only in a .spawn (placed by the world, no behaviour of ours)
    GENERATED   only in a generated block (_explicit/, *.generated.*): stats nobody chose
    NONE        nothing of ours names it

A quest-named type (`--quest REGEX`, e.g. 'fourdiamonds|deserttreasure|^fd_') that is not
SCRIPTED is flagged `<-- quest`. Every row needs a verdict in the port's doc: this content,
generic world npc (seen, unrelated), another player's (a follower, a pet), or the recorder's.

    python3 tools/rsprox/rsprox_npc_types.py ~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx46{21..29}.txt \\
        --quest 'fourdiamonds|deserttreasure|^fd_|diamond'

Transcript formats: `id=SYM (N)` (current) and `NAME (id=N)` (older; the cache symbol is looked up
from configs/all.npc by id).
"""
import argparse
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs-content")

TICK = re.compile(r"^\[(\d+)\]\s*$")
ADD_NEW = re.compile(r"\[add\] npc=\(index=\d+, id=([a-z0-9_]+) \((\d+)\)(?:, multinpc=[^,]+)?, coord=\((\d+), (\d+), (\d+)\)\)")
ADD_OLD = re.compile(r"\[add\] npc=\(index=\d+, (.+?) \(id=(\d+)\), coord=\((\d+), (\d+), (\d+)\)\)")
SAY = re.compile(r'^\s+\[say\] text="(.*)"')
SCRIPT_EXT = (".rs2", ".npc", ".constant", ".enum", ".dbrow", ".spawn")


def cache_symbols(content):
    """id -> symbol from configs/all.npc (for the older transcript format)."""
    out = {}
    path = os.path.join(content, "configs", "all.npc")
    if not os.path.exists(path):
        return out
    # all.npc is in id order with no id field: the n-th header is npc n (685 fourdiamonds_assasin,
    # 1634 banker1_east, 1968 fourdiamonds_troll_child checked 2026-11-23)
    with open(path, encoding="utf-8", errors="replace") as f:
        n = 0
        for line in f:
            m = re.match(r"^\[([a-z0-9_]+)\]", line)
            if m:
                out[n] = m.group(1)
                n += 1
    return out


def references(scripts, symbols):
    """symbol -> set of kinds ('scripted', 'spawned', 'generated')."""
    found = collections.defaultdict(set)
    pats = {s: re.compile(r"(?<![a-z0-9_])" + re.escape(s) + r"(?![a-z0-9_])") for s in symbols}
    for dirpath, dirnames, filenames in os.walk(scripts):
        dirnames[:] = [d for d in dirnames if not d.startswith("build")]
        for name in filenames:
            if not name.endswith(SCRIPT_EXT):
                continue
            path = os.path.join(dirpath, name)
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if ".generated." in name or "_explicit" in path:
                kind = "generated"
            elif name.endswith(".spawn"):
                kind = "spawned"
            else:
                kind = "scripted"
            for s, p in pats.items():
                if s in text and p.search(text):
                    found[s].add(kind)
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--content", default=DEFAULT_CONTENT)
    ap.add_argument("--scripts", default=None, help="default <content>/server/scripts")
    ap.add_argument("--quest", default=None, help="regex on the symbol: quest-named types to flag")
    ap.add_argument("--unscripted", action="store_true", help="print only types not SCRIPTED")
    args = ap.parse_args()
    scripts = args.scripts or os.path.join(args.content, "server", "scripts")
    by_id = None

    types = {}
    for path in args.transcripts:
        sess = re.sub(r"\.txt$", "", os.path.basename(path)).replace("idx", "")
        tick = 0
        pending = None
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = TICK.match(line)
                if m:
                    tick = int(m.group(1))
                    pending = None
                    continue
                m = ADD_NEW.search(line)
                if m:
                    sym, nid = m.group(1), int(m.group(2))
                else:
                    m = ADD_OLD.search(line)
                    if m:
                        if by_id is None:
                            by_id = cache_symbols(args.content)
                        nid = int(m.group(2))
                        sym = by_id.get(nid, m.group(1))
                if m:
                    t = types.setdefault(sym, {"id": nid, "count": 0, "sessions": set(), "first": None, "say": None})
                    t["count"] += 1
                    t["sessions"].add(sess)
                    if t["first"] is None:
                        t["first"] = "%s [%d] %s,%s,%s" % (sess, tick, m.group(3), m.group(4), m.group(5))
                    pending = sym
                    continue
                if pending:
                    s = SAY.match(line)
                    if s:
                        if types[pending]["say"] is None:
                            types[pending]["say"] = s.group(1)
                        pending = None
                    elif "[add]" in line or not line.startswith("            "):
                        pending = None

    refs = references(scripts, list(types))
    quest = re.compile(args.quest) if args.quest else None
    rows = []
    for sym, t in types.items():
        kinds = refs.get(sym, set())
        if "scripted" in kinds:
            ref = "SCRIPTED"
        elif "spawned" in kinds:
            ref = "SPAWNED"
        elif "generated" in kinds:
            ref = "GENERATED"
        else:
            ref = "NONE"
        if args.unscripted and ref == "SCRIPTED":
            continue
        flag = "  <-- quest" if quest and quest.search(sym) and ref != "SCRIPTED" else ""
        rows.append((ref != "SCRIPTED", sym, t, ref, flag))
    rows.sort(key=lambda r: (not r[4], r[3], r[1]))
    print("%-10s %6s %5s  %-42s %-28s %s" % ("ours", "id", "adds", "type", "first add", "say on add"))
    for _, sym, t, ref, flag in rows:
        say = ('"%s"' % t["say"]) if t["say"] else ""
        print("%-10s %6d %5d  %-42s %-28s %s%s" % (ref, t["id"], t["count"], sym, t["first"], say, flag))
    flagged = sum(1 for r in rows if r[4])
    print("\n%d type(s); %d not SCRIPTED; %d quest-named and not SCRIPTED" % (
        len(types), sum(1 for r in rows if r[3] != "SCRIPTED"), flagged))
    return 0


if __name__ == "__main__":
    sys.exit(main())
