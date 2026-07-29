#!/usr/bin/env python3
"""Import equipment requirements from Kronos into a LostCity `.obj` overlay.

An OldSchool cache states a *lot* about an item — name, model, cost, weight,
wear position, the twelve combat bonuses, the attack rate — but it states the
level you need to wear the thing only sometimes, and only ever two of them.
This fills the rest in, and cross-checks the part the cache does state.

    tools/kronos_item_import.py --report
    tools/kronos_item_import.py --out OSRS-Content/osrs239-content/server/scripts/skill_combat/configs/equipment.obj

Two sources, and neither is the authority on its own:

  * `cache.osrs239`, through the unpacked `configs/all.obj`. Params 434/436 and
    435/437 are a (skill, level) requirement pair. Newer than Kronos by 55
    revisions, and structurally limited to two requirements per record.
  * Kronos' `kronos-server/data/items/item_info.json` — 11,512 hand-curated
    entries at rev 184, complete where the cache is patchy, and able to state
    seven requirements for a piece of Void knight gear.

The merge rule, and every part of it was forced by a measurement:

  * Take the **union** of the two requirement sets. Kronos wins on the *set*,
    because the cache cannot hold more than two.
  * **A same-skill conflict is not merged at all.** 18 items state a different
    level in each source and neither side is reliably right: the cache says the
    twisted bow needs 85 Ranged and Kronos says 75 (75 is correct), while the
    cache says guardian boots need 75 Defence and Kronos says 60 (75 is
    correct). A tie-break rule in either direction would bake in a dozen wrong
    numbers, so these are *withheld* from the generated file, printed under
    `--report`, and resolved by hand in `equipment_disputed.obj` — which this
    generator does not own and will not overwrite.
  * **Drop non-combat skills from the cache side.** 434/436 is a requirement to
    *use* the record, and for a wearable obj "use" can mean *make*: a fire
    battlestaff reads Crafting 62, which is what it takes to build one. 19
    records in this cache would otherwise gate a weapon behind a crafting
    level. Kronos never states a non-combat requirement, so it needs no filter.
  * **Emit only what the cache does not already say.** A config that restates
    the cache is a config that can disagree with it, so an item the cache
    already covers correctly gets no line at all. 713 items need one; 243 do
    not.

Items are matched by **display name**, not by id, and the mismatch is reported
rather than assumed away: of Kronos' 1,102 requirement-bearing items, 931 agree
with this cache on both id and name, 96 match by name with a drifted id, and 75
match on neither. Those 75 are almost all abbreviations Kronos writes and the
cache does not ("Blue d'hide vamb" for vambraces) plus duplicate-name variants.
They are printed, not silently dropped.
"""

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEFAULT_CACHE_OBJ = os.path.join(REPO, "OSRS-Content/osrs239-content/configs/all.obj")
DEFAULT_OBJ_PACK = os.path.join(REPO, "OSRS-Content/osrs239-content/pack/obj.pack")
DEFAULT_PARAM_PACK = os.path.join(REPO, "OSRS-Content/osrs239-content/pack/param.pack")
DEFAULT_KRONOS = os.path.join(
    os.path.dirname(REPO),
    "kronos-osrs-184/Kronos-master/kronos-server/data/items/item_info.json",
)

# The skill ids the wire and `server/pack/stat.pack` agree on. Index is the id.
STAT_NAMES = [
    "attack", "defence", "strength", "hitpoints", "ranged", "prayer", "magic",
    "cooking", "woodcutting", "fletching", "fishing", "firemaking", "crafting",
    "smithing", "mining", "herblore", "agility", "thieving", "slayer",
    "farming", "runecraft", "hunter", "construction",
]
STAT_ID = {name: i for i, name in enumerate(STAT_NAMES)}

# Requirements the engine stores per obj. Seven is the observed maximum (Void
# knight gear, and the max capes as Kronos records them), so eight leaves a
# little room without the array being a guess.
MAX_REQUIREMENTS = 8

# Which skills the *cache* side is allowed to contribute.
#
# The filter is on the cache only, and the asymmetry is the whole point:
# Kronos records what gates *wearing* and nothing else, so a Fishing 99 there is
# a skill cape and a Hunter 28 is a larupia hat — both real. The cache's
# 434/436 records what gates *using* the record at all, and for a wearable obj
# "using" can mean making it, which is how a fire battlestaff comes to read
# Crafting 62. There is no field distinguishing the two, so the skill is the
# only signal available, and a combat skill is never a creation requirement.
COMBAT_STATS = {
    STAT_ID[n] for n in
    ("attack", "defence", "strength", "hitpoints", "ranged", "prayer", "magic",
     "agility", "slayer")
}


def read_kronos(path):
    """Kronos' item_info.json is JSON with `#` comments, and the comment on each
    opening brace is the item's name — the only place the file records one."""
    raw = open(path, encoding="utf-8", errors="replace").read()
    names = {}
    cleaned = []
    pending = None
    for line in raw.split("\n"):
        opening = re.match(r"\s*\{\s*#(.*)$", line)
        if opening:
            pending = opening.group(1).strip()
            cleaned.append("{")
            continue
        ident = re.match(r"\s*\"id\":\s*(\d+)", line)
        if ident and pending is not None:
            names[int(ident.group(1))] = pending
            pending = None
        cleaned.append(re.sub(r"#.*$", "", line))
    entries = json.loads("\n".join(cleaned))
    for entry in entries:
        entry["_name"] = names.get(entry["id"], "")
    return entries


def kronos_requirements(entry):
    out = []
    for key, value in entry.items():
        if not key.endswith("_level"):
            continue
        stat = key[: -len("_level")]
        # Kronos spells two of them differently from the wire's own list.
        stat = {"runecrafting": "runecraft"}.get(stat, stat)
        if stat in STAT_ID:
            out.append((STAT_ID[stat], int(value)))
    return sorted(out)


def read_cache(path, param_names):
    """symbol -> {'display', 'wearable', 'reqs'} out of the unpacked configs.

    `configs/all.obj` spells each param with whatever name `pack/param.pack` held
    when cachepack wrote it, so a param renamed since the last unpack still reads
    as `param_<id>` here. Both spellings are accepted rather than requiring a
    re-unpack — the alternative is a 180 MB round trip to rename four keys, and a
    tool that only works right after one is a tool that breaks silently.
    """
    def keys_for(param_id):
        return {f"param_{param_id}", param_names.get(param_id, "")} - {""}

    pairs = [
        (keys_for(434), keys_for(436)),
        (keys_for(435), keys_for(437)),
    ]

    text = open(path, encoding="utf-8", errors="replace").read()
    records = {}
    for block in re.split(r"\n(?=\[)", text):
        header = re.match(r"\[([^\]]+)\]", block)
        if not header:
            continue
        params = {
            key: value
            for key, _type, value in re.findall(
                r"^param=([^,]+),(\w+),(.*)$", block, re.M
            )
        }
        reqs = []
        for skill_keys, level_keys in pairs:
            skill = next((params[k] for k in skill_keys if k in params), None)
            level = next((params[k] for k in level_keys if k in params), None)
            if skill is not None and level is not None:
                reqs.append((int(skill), int(level)))
        name = re.search(r"^name=(.*)$", block, re.M)
        records[header.group(1)] = {
            "display": name.group(1).strip() if name else "",
            "wearable": re.search(r"^wearpos=", block, re.M) is not None,
            "reqs": sorted(reqs),
        }
    return records


def read_pack(path):
    """name -> id, plus the inverse, out of an `id=name` pack file."""
    ids = {}
    names = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.split("//")[0].strip()
        if "=" not in line:
            continue
        key, name = line.split("=", 1)
        if key.isdigit():
            ids[name] = int(key)
            names[int(key)] = name
    return ids, names


def merge(cache_reqs, kronos_reqs):
    """Union the two sets. Returns (merged, conflicts), where a conflict is a
    skill both sources state at a different level — recorded, never resolved."""
    merged = dict(kronos_reqs)
    conflicts = []
    for stat, level in cache_reqs:
        if stat in merged and merged[stat] != level:
            conflicts.append((stat, level, merged[stat]))
        else:
            merged[stat] = level
    return sorted(merged.items()), conflicts


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kronos", default=DEFAULT_KRONOS)
    ap.add_argument("--cache-obj", default=DEFAULT_CACHE_OBJ)
    ap.add_argument("--obj-pack", default=DEFAULT_OBJ_PACK)
    ap.add_argument("--param-pack", default=DEFAULT_PARAM_PACK)
    ap.add_argument("--out", help="write the .obj overlay here")
    ap.add_argument("--report", action="store_true", help="print the full audit")
    args = ap.parse_args()

    for path in (args.kronos, args.cache_obj, args.obj_pack, args.param_pack):
        if not os.path.exists(path):
            sys.exit(f"missing: {path}")

    kronos = read_kronos(args.kronos)
    _, param_names = read_pack(args.param_pack)
    cache = read_cache(args.cache_obj, param_names)
    pack, _ = read_pack(args.obj_pack)

    by_display = {}
    for symbol, rec in cache.items():
        if rec["display"]:
            by_display.setdefault(rec["display"].lower(), symbol)

    emitted = []          # (symbol, [(stat, level)], note)
    already_covered = []  # the cache says it and Kronos agrees
    conflicts = []        # same skill, different level
    unmatched = []        # Kronos names an item this cache does not
    id_drift = []         # name matches, id moved between 184 and 239
    not_wearable = []     # Kronos gates something this cache says is not worn

    for entry in kronos:
        kreqs = kronos_requirements(entry)
        if not kreqs:
            continue
        symbol = by_display.get(entry["_name"].lower())
        if not symbol:
            unmatched.append((entry["id"], entry["_name"]))
            continue
        if pack.get(symbol) != entry["id"]:
            id_drift.append((entry["_name"], entry["id"], pack.get(symbol), symbol))
        rec = cache[symbol]
        if not rec["wearable"]:
            not_wearable.append((symbol, entry["_name"]))
            continue

        cache_reqs = [(s, l) for s, l in rec["reqs"] if s in COMBAT_STATS]
        merged, clashes = merge(cache_reqs, kreqs)
        if clashes:
            # Withheld entirely rather than half-merged: an item whose Attack
            # level the two sources disagree about is an item whose whole row is
            # in doubt. equipment_disputed.obj is where it gets settled.
            for stat, cache_level, kronos_level in clashes:
                conflicts.append((symbol, STAT_NAMES[stat], cache_level, kronos_level))
            continue

        if merged == cache_reqs:
            already_covered.append(symbol)
            continue
        emitted.append((symbol, merged, entry["_name"]))

    emitted.sort()

    print(f"kronos entries with a requirement : {sum(1 for e in kronos if kronos_requirements(e))}")
    print(f"  matched to this cache by name   : {len(emitted) + len(already_covered) + len(not_wearable)}")
    print(f"  cache already states it exactly : {len(already_covered)}  (no line emitted)")
    print(f"  emitted to the overlay          : {len(emitted)}")
    print(f"  kronos gates a non-wearable obj : {len(not_wearable)}  (skipped)")
    print(f"  no name match in this cache     : {len(unmatched)}")
    print(f"  name matches, id drifted 184->239: {len(id_drift)}")
    print(f"  withheld, sources disagree      : {len(set(c[0] for c in conflicts))}"
          f"  -> equipment_disputed.obj")

    if args.report:
        print("\n--- withheld: the two sources disagree, resolve by hand ---")
        for symbol, stat, cache_level, kronos_level in sorted(conflicts):
            print(f"  {symbol:34} {stat:10} cache={cache_level:<4} kronos={kronos_level}")
        print("\n--- id drift (name matched, id differs) ---")
        for name, kid, ours, symbol in sorted(id_drift)[:40]:
            print(f"  {name:32} kronos={kid:<7} ours={ours} ({symbol})")
        if len(id_drift) > 40:
            print(f"  ... and {len(id_drift) - 40} more")
        print("\n--- kronos names this cache does not have ---")
        for kid, name in sorted(unmatched)[:40]:
            print(f"  {kid:<7} {name}")
        if len(unmatched) > 40:
            print(f"  ... and {len(unmatched) - 40} more")

    if not args.out:
        return

    lines = [
        "// Equipment requirements — the level you need to wear the thing.",
        "//",
        "// GENERATED by tools/kronos_item_import.py. Do not edit by hand: a",
        "// re-import rewrites this file, and a hand-authored line would go with it.",
        "// The generator's docstring says where every number comes from and why the",
        "// two sources are merged the way they are.",
        "//",
        "// This is an *overlay*, so it is deliberately incomplete. cache.osrs239",
        f"// already states the requirement for {len(already_covered)} items correctly, and those",
        "// get no line here — restating what the cache says is how a config comes to",
        "// disagree with it. What is left is what the cache is silent about, plus the",
        "// items whose requirement it can only half-state: it has room for two, and",
        "// Void knight gear needs seven.",
        "//",
        f"// {len(set(c[0] for c in conflicts))} more items are missing on purpose: the two sources give them",
        "// different levels, so they are settled by hand in equipment_disputed.obj",
        "// rather than by a tie-break rule that would be wrong about half of them.",
        "//",
        "// `mock230_pack` re-checks every line against the cache at validation time.",
        "",
    ]
    over_two = sum(1 for _s, reqs, _n in emitted if len(reqs) > 2)
    over_cap = [(s, r) for s, r, _n in emitted if len(r) > MAX_REQUIREMENTS]
    lines += [
        f"// {over_two} of these need more than two requirements — Void knight gear and the",
        "// max capes need seven — which is why the key is repeatable here rather than the",
        f"// cache's fixed pair. {MAX_REQUIREMENTS} is the engine's cap"
        + (f"; {len(over_cap)} records exceed it and say so." if over_cap else " and nothing reaches it."),
        "",
    ]
    for symbol, reqs, _kronos_name in emitted:
        lines.append(f"[{symbol}]")
        for stat, level in reqs[:MAX_REQUIREMENTS]:
            lines.append(f"param=levelrequire,{STAT_NAMES[stat]},{level}")
        if len(reqs) > MAX_REQUIREMENTS:
            dropped = ", ".join(f"{STAT_NAMES[s]} {l}" for s, l in reqs[MAX_REQUIREMENTS:])
            lines.append(f"// over the {MAX_REQUIREMENTS}-requirement cap, dropped: {dropped}")
        lines.append("")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, "w", encoding="utf-8").write("\n".join(lines))
    print(f"\nwrote {args.out} ({len(emitted)} records)")


if __name__ == "__main__":
    main()
