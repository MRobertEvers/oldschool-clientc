#!/usr/bin/env python3
"""
gen_slayer_membership — grow `slayer_task_member.dbrow` from 22 Turael-only
rows toward the cache's full 129-task slayer_task table.

    tools/gen_slayer_membership.py --report        # candidates, no write
    tools/gen_slayer_membership.py --write          # emit the .dbrow

Why this cannot be a straight cache read: `slayer_task_member.dbrow`'s own
header states it plainly — "The cache does not expose a server-readable
npc→task map." What the cache *does* expose is a `category=` field per npc
record (`configs/all.npc`), and the existing 22 rows already establish the
working convention: one `slayer_task_member` row per (task_id, npc_category)
pair, hand-picked by matching task name to category membership.

This script automates that matching by name, the same judgment call a human
authoring the next 20 rows would make, at 100x the volume:

  1. Group every categorised, attackable npc (`op*=Attack` present) by its
     cache category id.
  2. For each category, collect the *distinct* gameval-name stems its npcs
     share (stripping the trailing `_<n>`, `_baby`, `_dad`, region suffixes
     that already showed up in the hand-authored rows — `slayer_dustdevil`,
     `slayer_kursk_1`, `strongholdcave` — so `slayer_dustdevil` and
     `slayer_dustdevil_2` collapse to one stem `dustdevil`).
  3. Match each `slayer_task` name (singularised, punctuation stripped) against
     those stems. A category matches a task if any stem contains the task
     keyword or vice versa.
  4. A task with exactly one matching category is a confident row: emit it.
     A task with zero or multiple candidate categories is a **gap**, reported
     and left for a human — silently guessing wrong here is worse than an
     empty row, per docs/name-binding-silently-kills-category.md.

The id join follows docs/NPC_WIKI_STATS_PLAN.md §2's rule in spirit even
though this pass never touches the wiki: match by the cache's own numeric
category id, never by a re-derived display name.
"""
import argparse
import collections
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
NPC_CACHE = os.path.join(CONTENT, "configs", "all.npc")
DBROW = os.path.join(CONTENT, "configs", "all.dbrow")
MEMBER_DBROW = os.path.join(
    CONTENT, "server", "scripts", "skill_slayer", "configs", "slayer_task_member.dbrow"
)

# Task-name -> stem overrides where the mechanical stem match is wrong or
# ambiguous (a bare substring match would collide two unrelated categories).
# Left empty on purpose for a name that has no safe automatic match — those
# stay a reported gap rather than a guess.
TASK_STEM_OVERRIDE = {
    "cave bugs": "cavebug",
    "tztok-jad": None,       # unique boss npc, not a category — handled by npc id, not here
    "tzkal-zuk": None,       # same
    "boss": None,            # generic dispatch category 980, not a name match
    "spiritual creatures": "spiritual",
    "magic axes": "magicaxe",
    "sea snakes": "seasnake",
    "harpie bug swarms": "harpiebugswarm",
    "fossil island wyverns": "wyvern",
    "skeletal wyverns": "skeletalwyvern",
    "custodian stalkers": "custodianstalker",
    "brutal black dragons": "brutblackdragon",
    "mutated zygomites": "zygomite",
    "sulphur lizards": "sulphurlizard",
    "lesser nagua": "nagua",
    "warped creatures": "warped",
    "cave kraken": "cavekraken",
}

STRIP_SUFFIXES = [
    "_strongholdcave", "_dungeon", "_baby", "_dad2", "_dad3", "_dad", "_mum",
    "_child", "_1", "_2", "_3", "_4", "_5", "_6",
]

# Categories measured (see --report output) to be shared/reused buckets, not
# one-family-per-category: Nightmare Zone difficulty tiers (724/725), the
# superior-monster dispatch category (980 — a *different* mechanism, handled
# entirely by slayer_superior.rs2, never a slayer_task_member row), combat
# test dummies (981, 1287), Champions' Guild one-off duel npcs (635), Arceuus
# Necromancy reanimated monsters (935 — the caster's spell instance, not the
# base creature's own task line, and this era's Slayer has no Necromancy tie-
# in), the Prifddinas Elite Diary crystal reskins (1391), and the sailing
# placeholder/test category (2353). Any category on this list produced a
# false-positive match against multiple unrelated tasks during --report and
# is excluded outright rather than trusted to disambiguate correctly.
CATEGORY_BLOCKLIST = {206, 421, 422, 635, 724, 725, 935, 980, 981, 1287, 1391, 2353}

# category 347 shares every dragon colour (green/blue/red/black/bronze/iron/
# steel/adamant/rune/lava/frost) — measured via --report, all eleven dragon
# tasks candidate-matched to this one id. A category row here would credit
# every dragon-colour task on every dragon kill; those tasks are bound by
# npc_type instead, hand-authored in slayer_task_member_dragons.dbrow.
CATEGORY_BLOCKLIST.add(347)


def parse_npc_categories(path):
    """gameval name -> (category, has_attack_op).

    `configs/all.npc` keys each block by the cache's own gameval name
    (`[farming_tools_leprechaun]`), not a numeric id — unlike
    `all.npc.compack`, which is `id=name` pairs. No id join is needed here:
    the gameval name in the block header is already the name every other
    content file (spawns, drop tables, this task's own membership rows)
    addresses the npc by."""
    result = {}
    cur_name = None
    cur_cat = None
    cur_attack = False
    name_re = re.compile(r"^\[([^]]+)\]$")
    for line in open(path, encoding="utf8", errors="replace"):
        line = line.rstrip("\n")
        m = name_re.match(line)
        if m:
            if cur_name is not None:
                result[cur_name] = (cur_cat, cur_attack)
            cur_name = m.group(1)
            cur_cat = None
            cur_attack = False
            continue
        if line.startswith("category="):
            cur_cat = int(line.split("=", 1)[1])
        elif re.match(r"^op\d=", line) and line.split("=", 1)[1].strip().lower() == "attack":
            cur_attack = True
    if cur_name is not None:
        result[cur_name] = (cur_cat, cur_attack)
    return result


def stem_tokens(gameval_name):
    """Underscore-delimited tokens after stripping known suffixes/prefixes —
    kept as tokens, not concatenated, so matching can respect word
    boundaries: `roving_mossgiant` must not match `mossgiant` through
    `hillgiant`/`icegiant`/`firegiant`'s shared "giant" substring, and it
    won't, because `roving` is a leading token that has to match too."""
    n = gameval_name
    for suf in STRIP_SUFFIXES:
        if n.endswith(suf):
            n = n[: -len(suf)]
    n = re.sub(r"^(slayer_|superior_|league_)+", "", n)
    return [t for t in n.split("_") if t]


def leading_concats(tokens):
    """`['goblin','red','soldier']` -> `{'goblin','goblinred','goblinredsoldier'}`
    — this tree's naming convention leads with the creature name
    (`goblin_red_soldier`, `firegiant_big`), so only leading-token
    concatenations are checked, never a token from the middle or end."""
    out = set()
    acc = ""
    for t in tokens:
        acc += t
        out.add(acc)
    return out


def parse_slayer_tasks(path):
    txt = open(path, encoding="utf8", errors="replace").read()
    rows = []
    for block in txt.split("\n["):
        if "table=slayer_task\n" not in block:
            continue
        tid = re.search(r"columndef=0:id,int\nvalues=0:0:(-?\d+)", block)
        nm = re.search(r"columndef=9:name_lowercase,string\nvalues=9:0:(.*)", block)
        if not tid or not nm:
            continue
        rows.append((int(tid.group(1)), nm.group(1).strip()))
    return rows


def normalize_task_name(name):
    n = name.lower().strip()
    n = TASK_STEM_OVERRIDE.get(n, n)
    if n is None:
        return None
    n = re.sub(r"[^a-z]", "", n)
    if n.endswith("ies"):
        n = n[:-3] + "y"
    elif n.endswith("es") and not n.endswith("ees"):
        n = n[:-2]
    elif n.endswith("s") and not n.endswith("ss"):
        n = n[:-1]
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--report", action="store_true")
    args = ap.parse_args()

    npc_info = parse_npc_categories(NPC_CACHE)
    tasks = parse_slayer_tasks(DBROW)

    cat_stems = collections.defaultdict(set)
    cat_examples = collections.defaultdict(list)
    for gv, (cat, attackable) in npc_info.items():
        if not cat or not attackable or cat in CATEGORY_BLOCKLIST:
            continue
        toks = stem_tokens(gv)
        if not toks:
            continue
        cat_stems[cat] |= leading_concats(toks)
        if len(cat_examples[cat]) < 4:
            cat_examples[cat].append(gv)

    matched = []      # (task_id, task_name, [cat, ...])
    unmatched = []
    ambiguous = []

    for tid, tname in tasks:
        if tid < 1:
            continue  # boss rows: matched by npc id, not category — Phase 5/6
        key = normalize_task_name(tname)
        if key is None:
            continue  # explicit skip (jad, zuk, boss)
        candidates = [cat for cat, stems in cat_stems.items() if key in stems]
        candidates = sorted(set(candidates))
        if len(candidates) == 1:
            matched.append((tid, tname, candidates))
        elif len(candidates) == 0:
            unmatched.append((tid, tname, key))
        else:
            ambiguous.append((tid, tname, candidates))

    # A category matched to more than one task is coarser than the task
    # split needs — measured twice already (dragons all sharing 347, giants
    # all sharing 346: hill/ice/fire/moss giants are one category in this
    # cache, confirmed by listing its 61 members). Trusting a 1:1 match here
    # would silently credit the wrong task on every kill, so any such
    # category is demoted out of `matched` before anything gets written.
    category_task_count = collections.Counter()
    for tid, tname, cats in matched:
        for c in cats:
            category_task_count[c] += 1
    coarse_categories = {c for c, n in category_task_count.items() if n > 1}
    if coarse_categories:
        kept = []
        for tid, tname, cats in matched:
            if any(c in coarse_categories for c in cats):
                unmatched.append((tid, tname, "coarse category %s shared with another task — needs npc_type" % cats))
            else:
                kept.append((tid, tname, cats))
        matched = kept

    if args.report or not args.write:
        print(f"# {len(matched)} matched, {len(unmatched)} unmatched, {len(ambiguous)} ambiguous")
        print()
        print("## matched")
        for tid, tname, cats in matched:
            for c in cats:
                print(f"  {tid:3d} {tname:24s} category {c:5d}  e.g. {', '.join(cat_examples[c])}")
        print()
        print("## unmatched (no candidate category — needs a hand row or an npc_id membership)")
        for tid, tname, key in unmatched:
            print(f"  {tid:3d} {tname}")
        print()
        print("## ambiguous (more than one candidate category — needs a human pick)")
        for tid, tname, cats in ambiguous:
            for c in cats:
                print(f"  {tid:3d} {tname:24s} category {c:5d}  e.g. {', '.join(cat_examples[c])}")

    if args.write:
        existing_ids = set()
        if os.path.exists(MEMBER_DBROW):
            existing_ids = {
                int(m) for m in re.findall(r"data=task_id,(\d+)", open(MEMBER_DBROW, encoding="utf8").read())
            }
        lines = []
        used_keys = set()
        for tid, tname, cats in matched:
            if tid in existing_ids:
                continue
            for c in cats:
                key = (tid, c)
                if key in used_keys:
                    continue
                used_keys.add(key)
                slug = re.sub(r"[^a-z0-9]+", "_", tname.lower()).strip("_")
                lines.append(f"[slayer_member_gen_{slug}_{c}]")
                lines.append("table=slayer_task_member")
                lines.append(f"data=task_id,{tid}")
                lines.append(f"data=npc_category,{c}")
                lines.append("")
        out = "\n".join(lines)
        gen_path = MEMBER_DBROW.replace(".dbrow", ".generated.dbrow")
        with open(gen_path, "w", encoding="utf8") as f:
            f.write(
                "// GENERATED by tools/gen_slayer_membership.py --write. Do not hand-edit —\n"
                "// re-run the generator after a cache bump. Rows already present in\n"
                "// slayer_task_member.dbrow (the hand-authored file) are skipped here to\n"
                "// avoid a duplicate row for the same (task_id, npc_category) pair.\n"
                "// See docs/SLAYER_CONTENT_QUEUE.md for the unmatched/ambiguous report.\n\n"
            )
            f.write(out)
        print(f"wrote {len(lines)//4} rows to {gen_path}")


if __name__ == "__main__":
    main()
