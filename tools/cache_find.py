#!/usr/bin/env python3
"""Find a cache record by its DISPLAY name, not its symbol.

Written after the same mistake three times in one session.

Cache symbols routinely have nothing to do with what the game calls a thing:

    Corporeal Beast     -> corp_beast
    Skotizo             -> cata_boss
    Xamphur             -> akd_xamphur_combat
    Derwen              -> ma2_boss_guthix
    Justiciar Zachariah -> ma2_boss_saradomin
    Revenant maledictus -> wild_cave_superior
    Elite Void Knight   -> pest_voidknight_elite
    Prospector Percy    -> motherlode_percy
    Chompy bird hat     -> cbhat1 .. cbhat18

So `grep "=corporeal_beast" all.npc.compack` returns nothing and the honest
reading of that is "this cache has no Corporeal Beast" -- which is wrong, and I
recorded it as a finding twice before checking the `name=` fields instead.

Search the display name. The symbol is what you need afterwards; it is not what
you search by.

    tools/cache_find.py npc "Corporeal Beast"
    tools/cache_find.py obj "Chompy bird hat"
    tools/cache_find.py loc Sarcophagus
"""
import io
import os
import sys

CONTENT = "OSRS-Content/osrs239-content/configs"
KINDS = ("npc", "obj", "loc", "seq", "spotanim", "inv", "enum", "struct",
         "varp", "varbit", "dbtable", "param")


def search(kind, needle, exact=False):
    path = os.path.join(CONTENT, "all.%s" % kind)
    if not os.path.exists(path):
        return None
    text = io.open(path, encoding="utf-8", errors="replace").read()
    hits = []
    for block in text.split("\n\n"):
        lines = block.split("\n")
        if not lines or not lines[0].startswith("["):
            continue
        for line in lines[1:]:
            if not line.startswith("name="):
                continue
            value = line[5:]
            ok = (value == needle) if exact else (needle.lower() in value.lower())
            if ok:
                hits.append((lines[0].strip("[]"), value))
            break
    return hits


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    kind = sys.argv[1]
    needle = " ".join(sys.argv[2:])
    kinds = KINDS if kind == "all" else [kind]
    total = 0
    for k in kinds:
        hits = search(k, needle)
        if hits is None:
            if kind != "all":
                print("cache_find: no configs/all.%s in this content tree" % k,
                      file=sys.stderr)
                return 1
            continue
        for symbol, value in hits:
            print("%-9s %-40s %s" % (k, symbol, value))
        total += len(hits)
    if not total:
        print("cache_find: nothing in %s is named like %r -- and a symbol "
              "search would not have told you that reliably either"
              % (kind, needle))
    return 0


if __name__ == "__main__":
    sys.exit(main())
