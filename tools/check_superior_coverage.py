#!/usr/bin/env python3
"""Report which superior slayer monsters the tree can never spawn.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D8.

`~slayer_superior_roll` fires on a slayer kill and asks
`~slayer_superior_for_category(npc_category)` which superior to spawn. That proc
is a hand-written mapping, so a superior the cache ships but the proc does not
name simply never appears -- silently, because the roll succeeds and then
returns null.

This lists them. It reports rather than fails: the gap is known and closing it
means finding each npc's category id, which is mechanical but not automatic.

Cache variants are excluded on purpose. `superior_gargoyle_dead` is a corpse,
`superior_nechryael_*_spawn` are its summons, `superior_cave_crawler_ice` and
`superior_kourend_*` are region reskins -- none of them is a separate superior a
roll should pick.
"""
import io
import os
import re
import sys

CACHE = "OSRS-Content/osrs239-content/configs/all.npc"
PROC = ("OSRS-Content/osrs239-content/server/scripts/skill_slayer/scripts/"
        "slayer_superior.rs2")

# Suffixes/infixes that mark a cache record as a variant of a superior rather
# than a superior in its own right.
VARIANT_MARKS = ("_nolure", "_dead", "_spawn", "_ice", "_death", "_kourend",
                 "_chilled")


def cache_superiors(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    out = []
    for block in text.split("\n\n"):
        head = block.split("\n", 1)[0].strip()
        if not head.startswith("[superior_"):
            continue
        name = head.strip("[]")
        if any(m in name for m in VARIANT_MARKS):
            continue
        out.append(name)
    return sorted(out)


def mapped(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    return sorted(set(re.findall(r"return\((superior_[a-z0-9_]+)\)", text)))


def main():
    for p in (CACHE, PROC):
        if not os.path.exists(p):
            print("check_superior_coverage: %s is missing" % p, file=sys.stderr)
            return 1
    have = set(mapped(PROC))
    all_sup = cache_superiors(CACHE)
    missing = [s for s in all_sup if s not in have]
    stray = sorted(have - set(all_sup))

    print("check_superior_coverage: %d superior(s) in the cache, %d mapped by "
          "~slayer_superior_for_category" % (len(all_sup), len(have)))
    # A superior whose BASE monster carries no `category=` in the cache can
    # never be mapped by this proc, because `npc_category` is the only key it
    # has. Settled 2026-08-20 by reading the records: drake, smoke devil, hydra,
    # both wyrms and both dark beasts are all categoryless, and custodian has no
    # non-superior record at all. Those seven are not work waiting to be done --
    # they are blocked on the cache, and saying so stops the list reading as a
    # to-do.
    UNMAPPABLE = {
        "superior_drake": "base `drake` has no category= in this cache",
        "superior_smoke_devil": "base `smoke_devil` has no category=",
        "superior_hydra": "base `hydra` has no category=",
        "superior_wyrm_dark": "base `wyrm_dark` has no category=",
        "superior_wyrm_light": "base `wyrm_light` has no category=",
        "superior_dark_beast": "both dark beast records have no category=",
        "superior_custodian": "no non-superior `custodian` record exists",
    }
    blocked = [m for m in missing if m in UNMAPPABLE]
    missing = [m for m in missing if m not in UNMAPPABLE]

    if blocked:
        print("  blocked on the cache, not mappable (%d):" % len(blocked))
        for b in blocked:
            print("    %-24s %s" % (b, UNMAPPABLE[b]))

    if missing:
        print("  never spawnable (%d):" % len(missing))
        for i in range(0, len(missing), 4):
            print("    " + "  ".join(missing[i:i + 4]))
    if stray:
        print("  mapped but not in the cache (%d): %s"
              % (len(stray), " ".join(stray)))
        return 1
    if not missing:
        print("  every superior with a mappable base monster is rolled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
