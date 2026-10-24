#!/usr/bin/env python3
"""
gen_decant_potions -- the decanting tables, from the cache's own dose ladders.

    tools/gen_decant_potions.py            # (re)write both files
    tools/gen_decant_potions.py --check    # fail if either file is stale

Writes, under OSRS-Content/osrs-content/server/scripts/skill_herblore/configs/decanting/:

  decant_potion.enum   one enum per dose ladder, int -> namedobj: 1..N the
                       N-dose obj, 0 the container the last dose leaves behind
  decant_potion.obj    `param=decant_potion_enum` and `param=dose_count` on
                       every obj of every ladder

That is LostCity's shape (skill_herblore/configs/decanting/, rev 254), which
hand-wrote 23 ladders. OldSchool has ~150, so they are read off the cache:

  * a ladder is the objs whose name is `<base>(<n>)`, n in 1..4, grouped by
    base name; certs carry no name and placeholders are skipped by debugname;
  * a base name the cache uses for two ladders (Last Man Standing's `br_`
    potions, Deadman's starter combat potions, the Nightmare Zone and Chambers
    of Xeric overloads) is split by debugname shape, the debugname with its
    dose digit replaced, because those are different items that do not mix;
  * only categories the cache reserves for dose potions count. Each of these
    holds nothing but dose ladders (measured: 69 holds unfinished potions too,
    which have no `(n)` and drop out), and that is the claim this list makes.

The container: Chambers of Xeric potions empty to `raids_vial_empty` and Guthix
rest to `cup_empty` (both as their drink scripts already do,
player/scripts/consumption/cox_potion.rs2 and the tea's own); everything else to
`vial_empty`.

The 23 LostCity ladders keep LostCity's enum names (`potion_1attack`, ...); the
rest are `potion_<debugname shape>`.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREE = os.path.join(REPO, "OSRS-Content", "osrs-content")
OUT_DIR = os.path.join(TREE, "server", "scripts", "skill_herblore", "configs", "decanting")

# Cache categories whose members are dose potions, and what each one is.
CATEGORIES = {
    "69": "potion",
    "98": "guthix rest (cup)",
    "108": "serum 207",
    "109": "serum 208",
    "110": "olive oil",
    "111": "sacred oil",
    "261": "compost potion",
    "546": "Nightmare Zone potion",
    "1013": "Chambers of Xeric potion",
    "1655": "Tombs of Amascut liquid supply",
}

EMPTY_BY_CATEGORY = {
    "1013": "raids_vial_empty",
    "98": "cup_empty",
}
EMPTY_DEFAULT = "vial_empty"

# LostCity's own enum names, keyed by the ladder's 1-dose obj
# (LostCity_Server/content/scripts/skill_herblore/configs/decanting/decant_potion.enum).
LOSTCITY_NAMES = {
    "1dose1strength": "potion_1strength",
    "1dose1attack": "potion_1attack",
    "1dosestatrestore": "potion_1restore",
    "1dose1defense": "potion_1defense",
    "1doseprayerrestore": "potion_prayerrestore",
    "1dose2attack": "potion_2attack",
    "1dosefisherspotion": "potion_fisherspotion",
    "1dose2strength": "potion_2strength",
    "1dose2defense": "potion_2defense",
    "1doserangerspotion": "potion_rangerspotion",
    "1doseantipoison": "potion_antipoison",
    "1dose2antipoison": "potion_2antipoison",
    "1dosepotionofzamorak": "potion_potionofzamorak",
    "1dose1antidragon": "potion_1antidragon",
    "1dose1agility": "potion_1agility",
    "1dose2restore": "potion_2restore",
    "1dose2energy": "potion_2energy",
    "1dose1energy": "potion_1energy",
    "1dose1magic": "potion_1magic",
    "oliveoil1": "potion_oliveoil",
    "sacred_oil1": "potion_sacredoil",
    "mort_serum_perm1": "potion_mortserumperm",
    "mort_serum1": "potion_mortserum",
}


def read_objs():
    objs, cur = {}, None
    with open(os.path.join(TREE, "configs", "all.obj"), encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = re.match(r"\[(.+)\]\s*$", line)
            if m:
                cur = {}
                objs[m.group(1)] = cur
                continue
            if cur is not None and "=" in line:
                key, value = line.rstrip("\n").split("=", 1)
                cur.setdefault(key, value)
    return objs


def shape(debugname, dose):
    """The debugname with its dose digit replaced: `br_2dose2combat` -> `br_#dose2combat`."""
    return debugname.replace(str(dose), "#", 1)


def ladders(objs):
    by_name = defaultdict(lambda: defaultdict(list))
    for debugname, record in objs.items():
        if debugname.startswith("placeholder_"):
            continue
        category = record.get("category", "default")
        if category not in CATEGORIES:
            continue
        m = re.fullmatch(r"(.+?)\s?\(([1-4])\)", record.get("name", ""))
        if not m:
            continue
        by_name[m.group(1)][int(m.group(2))].append((debugname, category))

    out = []
    for base, doses in sorted(by_name.items()):
        if all(len(v) == 1 for v in doses.values()):
            groups = [{d: v[0] for d, v in doses.items()}]
        else:
            split = defaultdict(dict)
            for dose, members in doses.items():
                for debugname, category in members:
                    split[shape(debugname, dose)][dose] = (debugname, category)
            groups = [split[k] for k in sorted(split)]
        for group in groups:
            top = max(group)
            if sorted(group) != list(range(1, top + 1)):
                sys.exit(f"gen_decant_potions: {base!r} has a gap in its doses: {sorted(group)}")
            categories = {c for _, c in group.values()}
            if len(categories) != 1:
                sys.exit(f"gen_decant_potions: {base!r} spans categories {sorted(categories)}")
            category = categories.pop()
            first = group[1][0]
            name = LOSTCITY_NAMES.get(first)
            if name is None:
                # `+` is an operator to the compiler, so `antidote+` is spelled out.
                stem = shape(first, 1).replace("#dose", "").replace("#", "").replace("+", "plus")
                name = "potion_" + re.sub(r"_+", "_", stem).strip("_")
            out.append({
                "base": base,
                "enum": name,
                "category": category,
                "empty": EMPTY_BY_CATEGORY.get(category, EMPTY_DEFAULT),
                "doses": {d: group[d][0] for d in sorted(group)},
            })
    names = [l["enum"] for l in out]
    dupes = {n for n in names if names.count(n) > 1}
    if dupes:
        sys.exit(f"gen_decant_potions: enum names collide: {sorted(dupes)}")
    missing = set(LOSTCITY_NAMES.values()) - set(names)
    if missing:
        sys.exit(f"gen_decant_potions: LostCity ladders not found in the cache: {sorted(missing)}")
    return out


HEADER = """\
// Decanting {what} -- GENERATED by tools/gen_decant_potions.py, do not hand-edit.
//
// One dose ladder per enum, read off the cache: every obj named `<base>(<n>)` in
// a category the cache reserves for dose potions. The tool's docstring says how
// a ladder is found and why each category is in the list; a ladder that is
// wrong is fixed there and regenerated.
//
// Ported shape: LostCity_Server/content/scripts/skill_herblore/configs/decanting/
// (rev 254), which hand-wrote 23 of these and whose names they keep.
"""


def render_enum(all_ladders):
    lines = [HEADER.format(what="ladders"),
             "// val=0 is the container the last dose leaves behind, which LostCity",
             "// hard-coded as vial_empty: Chambers of Xeric potions sit in gourd vials",
             "// and Guthix rest in a cup. A dose past the top of a ladder is null, which is",
             "// how a 2-dose mix says it stops at two.", ""]
    for ladder in all_ladders:
        lines.append(f"// {ladder['base']} ({CATEGORIES[ladder['category']]})")
        lines.append(f"[{ladder['enum']}]")
        lines.append("inputtype=int")
        lines.append("outputtype=namedobj")
        lines.append("default=null")
        lines.append(f"val=0,{ladder['empty']}")
        for dose, debugname in ladder["doses"].items():
            lines.append(f"val={dose},{debugname}")
        lines.append("")
    return "\n".join(lines)


def render_obj(all_ladders):
    lines = [HEADER.format(what="params"), ""]
    for ladder in all_ladders:
        for dose, debugname in ladder["doses"].items():
            lines.append(f"[{debugname}]")
            lines.append(f"param=decant_potion_enum,{ladder['enum']}")
            lines.append(f"param=dose_count,{dose}")
            lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="fail if a written file is stale")
    args = parser.parse_args()

    all_ladders = ladders(read_objs())
    files = {
        "decant_potion.enum": render_enum(all_ladders),
        "decant_potion.obj": render_obj(all_ladders),
    }
    stale = []
    for name, text in files.items():
        path = os.path.join(OUT_DIR, name)
        current = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if current == text:
            continue
        if args.check:
            stale.append(name)
            continue
        os.makedirs(OUT_DIR, exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
    if stale:
        sys.exit(f"gen_decant_potions: stale, re-run tools/gen_decant_potions.py: {', '.join(stale)}")
    print(f"gen_decant_potions: {len(all_ladders)} ladders, "
          f"{sum(len(l['doses']) for l in all_ladders)} dose objs")


if __name__ == "__main__":
    main()
