#!/usr/bin/env python3
"""Derive the familiar ordinary-combat table from the 2009scape rev-530 source.

`Familiar.handleTickActions` (Familiar.java:258-270) makes a familiar join its
owner's fight without being told to.  Which familiars that applies to, and what
they swing with when it does, is four separate facts in the source tree:

  * `isCombatFamiliar()` — `NPCDefinition.forId(id + 1).getName().equals(getName())`
    (Familiar.java:167).  A familiar is a combat familiar iff its `id + 1`
    wilderness form carries the same name.  Derived here from
    `Server/data/configs/npc_configs.json` rather than from a cache, since that
    file is the same record set the server reads.
  * `isPeacefulFamiliar()` — `pouch.getPeaceful()`, the eighth constructor
    argument of every `SummoningPouch` enum row.
  * `isBurdenBeast()` — overridden true by `BurdenBeast` and its subclasses;
    read here as "the familiar's own class extends BurdenBeast or Forager".
  * the attack style — the `WeaponInterface.STYLE_*` each familiar class passes
    to `super(...)`.  ACCURATE/AGGRESSIVE/CONTROLLED/DEFENSIVE are melee,
    RANGE_ACCURATE is ranged, CAST is magic.

The stats the swing rolls with are the source npc record's own
(`attack_level`, `strength_level`, ..., `bonuses`), not the owner's.

Outputs, all regenerated together so they cannot drift:

  docs/summoning_port/familiar_normal_combat_530.csv
      one row per admitted familiar type, the audit record.
  .../ported_scape2009_summoning/configs/summoning_normal_combat.npc
      the server-side stat/bonus overlay for the familiars that need one.
  .../ported_scape2009_summoning/scripts/summoning_combat_table.rs2
      the registry procs the tick reads.

Usage:
    tools/gen_familiar_normal_combat.py --source <2009scape checkout>
"""

import argparse
import csv
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
LANE = os.path.join(CONTENT, "server", "scripts", "ported_scape2009_summoning")
DOCS = os.path.join(REPO, "docs", "summoning_port")

# The two familiars whose roster entry name does not match their registry npc
# name.  `abyssal_lukrer` is the known misspelling `roster_assets_530.csv`
# inherits and documents; `forge_regent_beast` is the roster's own longer name.
ENTRY_ALIAS = {
    "summoning_cohort_abyssal_lurker_abyssal_lurker": "abyssal_lukrer",
    "summoning_cohort_forge_regent_forge_regent": "forge_regent_beast",
}

# One familiar's stats are read from its Wilderness combat form instead of the
# record its pouch summons.  SwampTitanNPC.getIds() is {7329, 7330}; the two
# share a bonus vector but 7329 declares attack 1 and strength 1 against 7330's
# 60, which is an anomaly in the source data rather than something the game
# shows — `Familiar.transform()` swaps to `id + 1` in the Wilderness, where a
# pre-EoC familiar does most of its fighting.  Transform is not ported, so
# reading 7329 verbatim would leave a level-85 familiar unable to hit anything.
# Named here so the audit agrees with the block that already made this choice.
SOURCE_STAT_OVERRIDE = {7329: 7330}

# Familiar types whose ordinary swing is hand-written in summoning_combat.rs2
# because the source gives it more than one style or an extra charged hit:
# Honey badger, Spirit graahk, Iron titan, Steel titan.  The shared swing skips
# them, so the table not arming one of them is not a gap.
BESPOKE_TYPES = {16, 40, 76, 78}

STYLE_MELEE = "melee"
STYLE_RANGED = "ranged"
STYLE_MAGIC = "magic"

STYLE_OF = {
    "STYLE_ACCURATE": STYLE_MELEE,
    "STYLE_AGGRESSIVE": STYLE_MELEE,
    "STYLE_CONTROLLED": STYLE_MELEE,
    "STYLE_DEFENSIVE": STYLE_MELEE,
    "STYLE_RANGE_ACCURATE": STYLE_RANGED,
    "STYLE_RANGE_AGGRESSIVE": STYLE_RANGED,
    "STYLE_LONG_RANGE": STYLE_RANGED,
    "STYLE_CAST": STYLE_MAGIC,
}

# `CombatSwingHandler.getCombatDistance` with no `combatDistance` override:
# melee is adjacency, RangeSwingHandler.canSwing opens at 7 and
# MagicSwingHandler at 10.
REACH_OF = {STYLE_MELEE: 1, STYLE_RANGED: 7, STYLE_MAGIC: 10}

# `NPC.configure` leaves a record with no `attack_speed` row at five ticks.
DEFAULT_ATTACK_SPEED = 5

# npc_configs.json writes `bonuses` as a 15-value comma-separated vector, and
# the source handlers index it as the tree's Steel Titan block already
# documents: five attack, five defence, prayer at 10, melee strength at 11,
# magic damage at 13 and range strength at 14.  Slot 12 has no consumer here and
# is deliberately dropped rather than guessed at, exactly as that block does.
#
# The older special-only blocks in `summoning_special_combat.npc` predate that
# derivation and stopped at eleven values, reading slot 10 as `strengthbonus`.
# `--audit` is what found that, and the blocks now carry the full mapping.
BONUS_SLOTS = [
    (0, "stabattack"),
    (1, "slashattack"),
    (2, "crushattack"),
    (3, "magicattack"),
    (4, "rangeattack"),
    (5, "stabdefence"),
    (6, "slashdefence"),
    (7, "crushdefence"),
    (8, "magicdefence"),
    (9, "rangedefence"),
    (10, "prayerbonus"),
    (11, "strengthbonus"),
    (13, "magicdamage"),
    (14, "rangebonus"),
]


def read_registry_types():
    """type int -> destination npc name, from the live registry proc."""
    path = os.path.join(LANE, "scripts", "summoning_registry.rs2")
    body = open(path).read().split("[proc,summoning_familiar_npc]")[1].split("\n[proc,")[0]
    consts = {
        "^summoning_familiar_spirit_wolf": 1,
        "^summoning_familiar_dreadfowl": 2,
        "^summoning_familiar_spirit_terrorbird": 3,
    }
    out = {}
    for m in re.finditer(r"if \(\$type = (\^?\w+)\) return\((\w+)\);", body):
        key = m.group(1)
        out[consts[key] if key.startswith("^") else int(key)] = m.group(2)
    return out


def read_npc_source_ids():
    """destination npc name -> rev-530 source npc id, from the port ledgers."""
    out = {}
    port = os.path.join(CONTENT, "port")
    for name in sorted(os.listdir(port)):
        if not name.endswith(".map"):
            continue
        for line in open(os.path.join(port, name)):
            f = line.rstrip("\n").split("\t")
            if len(f) >= 5 and f[0] == "npc" and f[1].isdigit():
                out.setdefault(f[4], int(f[1]))
    return out


# `summoning_roster_530_*` is a preserved, unreviewed bulk import experiment.
# `docs/summoning_port/roster_boundary_530.json` holds the whole prefix out of
# the feature-on stage, so those records are not in the built cache and server
# scripts may not name them (tools/test_summoning_phase5a.py enforces both).
# A familiar whose only attack sequence is a roster one therefore cannot swing
# here yet, however complete the rest of its profile is.
REVIEW_ONLY_PREFIX = "summoning_roster_530"


def read_seq_names():
    """rev-530 source seq id -> (admitted name or None, review-only name or None).

    One source sequence can carry two destination names: the roster import
    named every familiar's attack animation, and a special-move closure
    separately admitted some of the same ids under its own name.  Where both
    exist the admitted one is usable and the roster one is not — this is the
    same substitution the Iron Titan and Spirit graahk handlers already make
    by hand ("sequence 5229 is already admitted as Rending's shared source
    sequence; it is the Graahk's normal melee animation too").
    """
    admitted = {}
    review = {}
    port = os.path.join(CONTENT, "port")
    for name in sorted(os.listdir(port)):
        if not name.endswith(".map"):
            continue
        for line in open(os.path.join(port, name)):
            f = line.rstrip("\n").split("\t")
            if len(f) >= 5 and f[0] == "seq" and f[1].isdigit():
                target = review if f[4].startswith(REVIEW_ONLY_PREFIX) else admitted
                target.setdefault(int(f[1]), f[4])
    return admitted, review


def read_roster():
    """roster entry -> its asset row (attack sequence lives here)."""
    path = os.path.join(DOCS, "roster_assets_530.csv")
    return {r["entry"]: r for r in csv.DictReader(open(path))}


def read_pouches(source):
    """rev-530 familiar npc id -> (pouch obj id, peaceful)."""
    path = os.path.join(
        source, "Server/src/main/content/global/skill/summoning/SummoningPouch.java"
    )
    text = open(path).read()
    out = {}
    for m in re.finditer(r"^\s*([A-Z_0-9]+)\((.*?)new Item", text, re.M | re.S):
        parts = [p.strip() for p in m.group(2).split(",") if p.strip()]
        flags = [i for i, p in enumerate(parts) if p in ("true", "false")]
        if not flags:
            continue
        # (slot, [abyssal,] pouchId, level, createXp, npcId, summonXp, cost, peaceful)
        at = flags[-1]
        try:
            npc_id = int(parts[at - 3])
            pouch_id = int(parts[at - 6])
        except (ValueError, IndexError):
            continue
        out[npc_id] = (pouch_id, parts[at] == "true")
    return out


# A familiar class that names no style takes the five-argument `Familiar`
# constructor, which supplies STYLE_DEFENSIVE (Familiar.java:196-198).
DEFAULT_STYLE = "STYLE_DEFENSIVE"

# Java says `super(owner, id, ...)`; Kotlin names the superclass instead
# (`... : BurdenBeast(owner, id, ...)`).  Both spell the pouch as a literal in
# Java and as an `Items.<NAME>_<id>` constant in Kotlin.
SUPER_CALL = re.compile(
    r"(?:super|Familiar|BurdenBeast|Forager)\s*\(\s*owner[^,)]*,\s*id[^)]*\)")


def read_classes(source):
    """pouch obj id -> (attack style constant, is burden beast)."""
    root = os.path.join(source, "Server/src/main/content/global/skill/summoning/familiar")
    out = {}
    for name in sorted(os.listdir(root)):
        if not re.search(r"NPC\.(java|kt)$", name):
            continue
        text = open(os.path.join(root, name)).read()
        burden = bool(
            re.search(r"extends\s+(BurdenBeast|Forager)\b", text)
            or re.search(r":\s*(?:\w+\s*,\s*)*(BurdenBeast|Forager)\s*\(", text)
        )
        for m in SUPER_CALL.finditer(text):
            args = m.group(0)
            style = re.search(r"WeaponInterface\.(STYLE_[A-Z_]+)", args)
            # `12047` and `Items.SPIRIT_WOLF_POUCH_12047` both end in the id, but
            # the second has a word character before it, so `\b` cannot find it.
            ids = [int(x) for x in re.findall(r"(?<![0-9])(\d{4,6})(?![0-9])", args)]
            pouch_ids = [i for i in ids if 12000 <= i <= 12999]
            if not pouch_ids:
                continue
            out[pouch_ids[0]] = (style.group(1) if style else DEFAULT_STYLE, burden)
    return out


def num(cfg, key, default):
    """A source row writes an absent value as null or as an empty string."""
    value = cfg.get(key)
    return int(value) if value not in (None, "") else default


def build(source):
    cfgs = {int(c["id"]): c for c in json.load(open(
        os.path.join(source, "Server/data/configs/npc_configs.json")))}
    types = read_registry_types()
    src_ids = read_npc_source_ids()
    seq_admitted, seq_review = read_seq_names()
    roster = read_roster()
    pouches = read_pouches(source)
    classes = read_classes(source)

    rows = []
    for type_id in sorted(types):
        npc_name = types[type_id]
        src = src_ids.get(npc_name)
        entry = ENTRY_ALIAS.get(npc_name)
        if entry is None:
            entry = npc_name.replace("summoning_cohort_", "")
            entry = entry[: len(entry) // 2] if entry[: len(entry) // 2] + "_" + \
                entry[: len(entry) // 2] == entry else entry
        if src is None:
            # Spirit wolf and the two aliased rows resolve through the roster.
            for e, r in roster.items():
                if npc_name.endswith(e) or e == entry:
                    src = int(r["source_npc"])
                    entry = e
                    break
        if src is None:
            raise SystemExit("no source npc for %s" % npc_name)
        for e, r in roster.items():
            if int(r["source_npc"]) == src:
                entry = e
                break

        cfg = cfgs.get(src, {})
        nxt = cfgs.get(src + 1, {})
        is_combat = bool(cfg.get("name")) and cfg.get("name") == nxt.get("name")
        # Classification stays on the summoned record; only the stats move.
        cfg = cfgs.get(SOURCE_STAT_OVERRIDE.get(src, src), cfg)
        pouch_id, peaceful = pouches.get(src, (0, False))
        style_const, burden = classes.get(pouch_id, (DEFAULT_STYLE, False))
        style = STYLE_OF[style_const]

        # The swing's animation is the one for the style it swings in.  The
        # roster carries all three columns per familiar because the source
        # record does; picking `attack_seq` for a caster would play its melee
        # animation at a spell.  Fall back to the melee column only when the
        # style's own column is empty in the source record.
        column = {STYLE_RANGED: "range_seq", STYLE_MAGIC: "magic_seq"}.get(
            style, "attack_seq")
        attack_seq = ""
        withheld = ""
        row = roster.get(entry, {})
        for key in (column, "attack_seq"):
            for sid in row.get(key, "").strip().split():
                attack_seq = attack_seq or seq_admitted.get(int(sid), "")
                withheld = withheld or seq_review.get(int(sid), "")
            if attack_seq:
                break

        # A familiar auto-assists only when the source's own conjunction holds,
        # and can only *swing* here when it also has a style and an animation.
        assists = is_combat and not peaceful and not burden
        armed = assists and bool(style) and bool(attack_seq)

        bonuses = []
        if cfg.get("bonuses"):
            bonuses = [int(b) for b in cfg["bonuses"].split(",")]
            if len(bonuses) != 15:
                raise SystemExit(
                    "npc %d: expected a 15-value bonus vector, got %d"
                    % (src, len(bonuses)))

        rows.append({
            "type": type_id,
            "entry": entry,
            "npc": npc_name,
            "source_npc": src,
            "pouch": pouch_id,
            "combat_familiar": int(is_combat),
            "peaceful": int(peaceful),
            "burden_beast": int(burden),
            "auto_assist": int(assists),
            "armed": int(armed),
            "style": style,
            "style_source": style_const or "",
            "reach": REACH_OF.get(style, 0),
            "attack_seq": attack_seq,
            "attack_seq_withheld": withheld,
            "attack_speed": num(cfg, "attack_speed", DEFAULT_ATTACK_SPEED),
            "attack_level": num(cfg, "attack_level", 0),
            "strength_level": num(cfg, "strength_level", 0),
            "defence_level": num(cfg, "defence_level", 0),
            "magic_level": num(cfg, "magic_level", 0),
            "range_level": num(cfg, "range_level", 0),
            "lifepoints": num(cfg, "lifepoints", 0),
            "bonuses": " ".join(str(b) for b in bonuses),
        })
    return rows


CSV_FIELDS = [
    "type", "entry", "npc", "source_npc", "pouch", "combat_familiar", "peaceful",
    "burden_beast", "auto_assist", "armed", "style", "style_source", "reach",
    "attack_seq", "attack_seq_withheld", "attack_speed", "attack_level",
    "strength_level",
    "defence_level", "magic_level", "range_level", "lifepoints", "bonuses",
]


def write_csv(rows):
    path = os.path.join(DOCS, "familiar_normal_combat_530.csv")
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        w.writeheader()
        for r in rows:
            w.writerow(r)
    return path


GENERATED_NPC = "summoning_normal_combat.npc"


def existing_blocks():
    """npc name -> the lane `.npc` file that already declares it.

    `walk_configs` loads these files in sorted order and `mock230_content_npc`
    returns the FIRST block for an id, so a second block for one npc is not
    merged — it is silently dead.  A generated block therefore has to stay out
    of the way of any hand-authored one, and the fields it would have supplied
    belong in the block that already exists.
    """
    out = {}
    root = os.path.join(LANE, "configs")
    for name in sorted(os.listdir(root)):
        if not name.endswith(".npc") or name == GENERATED_NPC:
            continue
        for block in re.findall(r"^\[(\w+)\]", open(os.path.join(root, name)).read(), re.M):
            out.setdefault(block, name)
    return out


def stat_lines(row):
    """The `.npc` body for one familiar's ordinary-combat profile."""
    out = []
    for field, key in (("attack", "attack_level"), ("strength", "strength_level"),
                       ("defence", "defence_level"), ("magic", "magic_level"),
                       ("ranged", "range_level"), ("hitpoints", "lifepoints")):
        if row[key]:
            out.append("%s=%s" % (field, row[key]))
    if row["bonuses"]:
        values = row["bonuses"].split()
        for slot, name in BONUS_SLOTS:
            if int(values[slot]):
                out.append("param=%s,%s" % (name, values[slot]))
    return out


def write_npc(rows):
    owned = existing_blocks()
    out = ["""// Ordinary-combat stat overlay for the familiars that auto-assist.
//
// GENERATED by tools/gen_familiar_normal_combat.py — do not hand-edit.  Every
// value is the rev-530 record's own, from 2009scape
// Server/data/configs/npc_configs.json, and every block has an audit row in
// docs/summoning_port/familiar_normal_combat_530.csv.
//
// `Familiar` rolls its swing with the familiar's own stats, never the owner's,
// so these are the numbers `~summoning_familiar_generic_swing` needs.
//
// A familiar whose block already exists in another lane `.npc` file is absent
// here on purpose: `mock230_content_npc` returns the first block for an id, so
// a second one would be dead and would take these stats with it.  Run this
// generator with --audit to see which those are and whether the block that owns
// them still matches the source.
//
// A record whose source row carries no bonus vector keeps zero bonuses.  That
// is the source's answer, not a gap to fill by invention.
"""]
    written = 0
    for r in rows:
        if not r["armed"] or r["npc"] in owned:
            continue
        out.append("[%s]" % r["npc"])
        out.extend(stat_lines(r))
        out.append("")
        written += 1
    path = os.path.join(LANE, "configs", GENERATED_NPC)
    open(path, "w").write("\n".join(out) + "\n")
    return path, written


def audit(rows):
    """Report every armed familiar whose block another file owns.

    A hand-authored block that predates this table can be missing a stat the
    swing rolls with, or can disagree with the source outright.  Both are
    silent at runtime — the roll simply uses a default — so they are named
    here rather than left to be discovered as "my familiar hits for 1".
    """
    owned = existing_blocks()
    root = os.path.join(LANE, "configs")
    findings = 0
    for r in rows:
        if not r["armed"] or r["npc"] not in owned:
            continue
        owner = owned[r["npc"]]
        text = open(os.path.join(root, owner)).read()
        block = re.search(r"^\[%s\]\n(.*?)(?=^\[|\Z)" % re.escape(r["npc"]),
                          text, re.M | re.S)
        have = set(l.strip() for l in block.group(1).splitlines() if l.strip()
                   and not l.startswith("//"))
        want = stat_lines(r)
        missing = [l for l in want if l not in have]
        conflicting = []
        for line in missing:
            key = line.split("=")[0] if "=" in line and "," not in line \
                else line.rsplit(",", 1)[0]
            for existing in have:
                if existing.startswith(key + "=") or existing.startswith(key + ","):
                    conflicting.append("%s (block has %s)" % (line, existing))
                    break
        if missing:
            findings += 1
            print("  %-22s block owned by %s" % (r["entry"], owner))
            for line in missing:
                note = next((c for c in conflicting if c.startswith(line)), None)
                print("      %s" % (note or line))
    return findings


def write_table(rows):
    def switch(name, ret, value_of, default):
        lines = ["[proc,%s](int $type)(%s)" % (name, ret)]
        for r in rows:
            v = value_of(r)
            if v is None:
                continue
            lines.append("if ($type = %d) return(%s);" % (r["type"], v))
        lines.append("return(%s);" % default)
        lines.append("")
        return lines

    header = """// Familiar ordinary-combat registry.
//
// GENERATED by tools/gen_familiar_normal_combat.py — do not hand-edit.  Every
// row is derived from the rev-530 source and audited in
// docs/summoning_port/familiar_normal_combat_530.csv.
//
// `~summoning_familiar_auto_assists` is the source's own conjunction from
// Familiar.handleTickActions: `isCombatFamiliar() && !isBurdenBeast() &&
// !isPeacefulFamiliar()`.  It is the answer to "would this familiar join the
// fight at all", and it is deliberately separate from whether this tree can
// yet *draw* the swing — a familiar with no admitted attack sequence still
// answers true here, and `~summoning_familiar_attack_anim` returns null for it.
"""
    body = []
    body += switch(
        "summoning_familiar_auto_assists", "boolean",
        lambda r: "true" if r["auto_assist"] else None, "false")
    body += switch(
        "summoning_familiar_attack_style", "int",
        lambda r: {"melee": "^summoning_style_melee",
                   "ranged": "^summoning_style_ranged",
                   "magic": "^summoning_style_magic"}.get(r["style"])
        if r["auto_assist"] else None,
        "^summoning_style_none")
    body += switch(
        "summoning_familiar_attack_anim", "seq",
        lambda r: r["attack_seq"] if r["armed"] else None, "null")
    body += switch(
        "summoning_familiar_attack_speed", "int",
        lambda r: str(r["attack_speed"]) if r["armed"] else None,
        str(DEFAULT_ATTACK_SPEED))
    body += switch(
        "summoning_familiar_attack_reach", "int",
        lambda r: str(r["reach"]) if r["armed"] else None, "1")

    path = os.path.join(LANE, "scripts", "summoning_combat_table.rs2")
    open(path, "w").write(header + "\n" + "\n".join(body))
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True,
                    help="root of a 2009scape checkout (rev 530)")
    ap.add_argument("--audit", action="store_true",
                    help="report armed familiars whose block another lane .npc "
                         "file owns, and what that block is missing")
    args = ap.parse_args()
    rows = build(args.source)

    if args.audit:
        print("blocks owned elsewhere, with the stats they still need:")
        found = audit(rows)
        print("%d familiar%s need folding into another file" %
              (found, "" if found == 1 else "s"))
        return 0

    csv_path = write_csv(rows)
    npc_path, npc_count = write_npc(rows)
    table_path = write_table(rows)

    assists = sum(r["auto_assist"] for r in rows)
    armed = sum(r["armed"] for r in rows)
    print("%d familiars, %d auto-assist, %d armed with a swing" %
          (len(rows), assists, armed))
    withheld = [r for r in rows
                if r["auto_assist"] and not r["armed"]
                and r["type"] not in BESPOKE_TYPES]
    print("%d assist with a hand-written swing: %s" % (
        len(BESPOKE_TYPES),
        ", ".join(r["entry"] for r in rows if r["type"] in BESPOKE_TYPES)))
    print("%d assist but cannot swing yet:" % len(withheld))
    for r in withheld:
        why = ("no attack sequence in the source record" if not r["attack_seq_withheld"]
               else "animation held in the review-only roster import (%s)"
                    % r["attack_seq_withheld"])
        print("    %-20s %s" % (r["entry"], why))
    for p in (csv_path, npc_path, table_path):
        print("wrote", os.path.relpath(p, REPO))
    print("%d stat blocks written" % npc_count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
