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

Those four facts settle auto-assist only.  Whether a familiar is ARMED — able
to swing at a target it already holds, which this port's familiar-panel Attack
badge can hand it without any of `handleTickActions`' conditions holding — is a
separate and wider question, and since 2026-08-14 it is derived separately:
`isCombatFamiliar()` plus a style plus an admitted attack animation, with the
burden-beast and peaceful exclusions applying to auto-assist alone.

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

# How fast a familiar swings, from the game's own data rather than from a
# server's config — because neither server states it for a familiar.
#
# `npc_configs.json` carries an `attack_speed` for 1260 of its 8033 records and
# for only 13 of the 78 familiars, all but two of those repeating the five-tick
# default; 2009scape's familiars therefore swing at `NPC.configure`'s fallback,
# not at a stated rate.  Nocturne (an RS3 server) does not state one either:
# `NPC.getAttackSpeed` reads `NPCDefinitions.clientScriptData[14]` — an npc
# param in the cache — and falls back to 4 when the record has none.
#
# That param is the answer, and a pre-EoC cache has it.  `familiar_attack_
# speed_preeoc.csv` is param 14 for all 78 familiar records, read out of
# cache.rs727_preeoc with:
#
#   tools/dump_stats/dump_stats --rev rs727 cache.rs727_preeoc \
#       --npc-only --npc-csv <out>.csv     # `params` column, key 14
#
# Every one of the 72 familiar records that states it says **8**, four ticks
# slower than the five this table used to hand every familiar.  That the whole
# roster agrees is what makes the six silent records safe to give the same
# number (see PREEOC_FALLBACK_SPEED below).
#
# Param 14 is attack speed and not something that merely looks like one: it is
# what nocturne reads for exactly that purpose, and across the whole rev-727
# npc table its values land where the game's known cadences are — Commander
# Zilyana 2, guards and skeletons 4, dwarves 5, Graardor and the giants 6,
# scarab swarms 1.
PREEOC_SPEEDS = os.path.join(DOCS, "familiar_attack_speed_preeoc.csv")

# The six familiar records with no param 14 are the five foragers (Beaver,
# Macaw, Magpie, Ibis, Fruit bat) and the Vampyre bat.  Only the bat is armed,
# so only the bat's rate is a live question, and 8 is what every other familiar
# in the same cache answers.  Stated as its own constant rather than folded into
# DEFAULT_ATTACK_SPEED so the two defaults cannot be confused: five is
# 2009scape's engine fallback, eight is the roster's own measured rate.
PREEOC_FALLBACK_SPEED = 8

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


def pack_path(*parts):
    return os.path.join(CONTENT, "ported", "scape2009_summoning", *parts)


def read_pack_index(name):
    """`<id>=<path>` ledger -> {id: record name}, ignoring the path prefix."""
    out = {}
    for line in open(pack_path("pack", name), encoding="latin-1"):
        line = line.strip()
        if "=" not in line or line.startswith("#"):
            continue
        key, value = line.split("=", 1)
        if key.isdigit():
            out[int(key)] = value.rsplit("/", 1)[-1]
    return out


def seq_frame_archives(names):
    """The frame archives the given sequence records draw their frames from.

    A `.seq` record lists `frame=<packed>,<delay>` where the high half of the
    packed id is the destination frame-archive id.  Reading it here is what
    makes the admission closure derived rather than declared: a sequence cannot
    be admitted without the archive that holds its frames, and nothing else in
    the tree states that link.
    """
    text = open(pack_path("configs", "summoning_roster_530.seq"),
                encoding="latin-1").read()
    blocks = {m.group(1): m.group(2) for m in
              re.finditer(r"^\[(\w+)\]\n(.*?)(?=^\[|\Z)", text, re.M | re.S)}
    archives = set()
    for name in names:
        body = blocks.get(name)
        if body is None:
            continue
        for frame in re.findall(r"frame=(\d+)", body):
            archives.add(int(frame) >> 16)
    return archives


def archive_framemaps(archive_names):
    """The skeleton each frame archive is rigged to.

    The link lives only inside the archive binary: its first two bytes are the
    destination framemap id, big-endian.  A frame archive whose framemap is
    absent from the cache animates nothing, so this closure has to follow it.
    """
    root = os.path.join(CONTENT, "animsets", "ported", "scape2009_summoning")
    framemaps = set()
    for name in archive_names:
        path = os.path.join(root, name + ".anim")
        if not os.path.isfile(path):
            raise SystemExit("frame archive binary is missing: %s" % path)
        with open(path, "rb") as handle:
            head = handle.read(2)
        if len(head) != 2:
            raise SystemExit("frame archive is truncated: %s" % path)
        framemaps.add((head[0] << 8) | head[1])
    return framemaps


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


def resolve_seq(row, columns, admitted, review):
    """The destination sequence name for the first column that has one.

    An admitted name always wins over the roster import's own, because the two
    are the same source sequence under two destination names and only the
    admitted one was reviewed.  A roster name is returned when it is the only
    one: those records are individually admitted through
    `roster_boundary_530.json` by `--admit`, which reads exactly the names this
    function returns.
    """
    for column in columns:
        for sid in row.get(column, "").strip().split():
            name = admitted.get(int(sid)) or review.get(int(sid))
            if name:
                return name
    return ""


def read_preeoc_speeds():
    """`source npc id -> attack speed in ticks`, from the pre-EoC cache param.

    Absent and blank rows are both dropped rather than defaulted here: the
    caller distinguishes "the cache states this familiar's rate" from "nothing
    states it", and records that distinction in the audit CSV.
    """
    speeds = {}
    with open(PREEOC_SPEEDS) as handle:
        for row in csv.DictReader(handle):
            if row["attack_speed"].strip():
                speeds[int(row["source_npc"])] = int(row["attack_speed"])
    return speeds


def build(source):
    cfgs = {int(c["id"]): c for c in json.load(open(
        os.path.join(source, "Server/data/configs/npc_configs.json")))}
    preeoc = read_preeoc_speeds()
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
        row = roster.get(entry, {})
        attack_seq = resolve_seq(row, (column, "attack_seq"),
                                 seq_admitted, seq_review)
        # Defend and death are the engine's, not the swing's: it plays
        # `block_seq` on every hit a familiar takes and `death_seq` on the
        # death step.  Every familiar has them, including the beasts of burden
        # and the peaceful ones that never swing at anything.
        defend_seq = resolve_seq(row, ("defend_seq",), seq_admitted, seq_review)
        death_seq = resolve_seq(row, ("death_seq",), seq_admitted, seq_review)

        # A familiar auto-assists only when the source's own conjunction holds.
        assists = is_combat and not peaceful and not burden

        # Being ARMED is a different question, and since 2026-08-14 it is asked
        # independently: not "does this familiar join a fight it was not told
        # to join" but "can this tree draw a swing at a target the familiar is
        # already holding".  An owner hands it one directly through the familiar
        # panel's Attack badge — `~summoning_familiar_command_attack`, which has
        # never consulted `handleTickActions`' conditions and cannot, because a
        # commanded attack is this port's own seam and has no source analogue.
        #
        # So this keys off the familiar's own combat record alone: a combat
        # variant to take stats from, a style to swing in, and an admitted
        # attack animation to draw.  A beast of burden with all three is a
        # familiar that fights when it is sent at something and otherwise
        # carries your logs, which is what a Spirit terrorbird is; before this
        # it latched the commanded target and then stood still, because
        # `~summoning_familiar_generic_combat_tick` returns on a null anim.
        #
        # `is_combat` still gates.  Dropping it would arm Magpie, a Forager
        # whose source record has no combat variant to take stats from at all
        # and whose attack animation is source seq 7810 — the Dreadfowl's.
        armed = is_combat and bool(style) and bool(attack_seq)

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
            "defend_seq": defend_seq,
            "death_seq": death_seq,
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
    "attack_seq", "defend_seq", "death_seq", "attack_sound", "defend_sound",
    "death_sound", "attack_speed", "attack_level", "strength_level",
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
    """The `.npc` body for one familiar's ordinary-combat profile.

    Two halves with different consumers.  The stats and bonuses are read by the
    content-side swing through `npc_stat`/`npc_param`.  `defend_anim` and
    `death_anim` are engine fields: `mock230_combat_hit_npc` plays `block_seq`
    on every hit a familiar takes and the death step plays `death_seq`, so they
    apply to every familiar including the beasts of burden and the peaceful ones
    that never swing at anything.  A familiar with neither is a creature that
    stands perfectly still while it is beaten to death.
    """
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
    for param, key in (("defend_anim", "defend_seq"), ("death_anim", "death_seq")):
        if row[key]:
            out.append("param=%s,%s" % (param, row[key]))
    return out


def needs_block(row):
    """Whether this familiar has anything to say in a `.npc` block.

    An armed familiar needs its stats to roll with; every familiar with a
    defend or death animation needs it whether or not it ever swings, because
    the engine plays those when it is hit and when it dies.
    """
    return bool(row["armed"] or row["defend_seq"] or row["death_seq"])


def write_npc(rows):
    owned = existing_blocks()
    out = ["""// Ordinary-combat stat overlay for the familiars that can fight.
//
// "Can fight" is wider than "auto-assists": a beast of burden that never joins
// a fight on its own still swings at a target its owner sends it at, so it
// needs the same stats.  See summoning_combat_table.rs2's header.
//
// GENERATED by tools/gen_familiar_normal_combat.py — do not hand-edit.  Every
// value is the rev-530 record's own, from 2009scape
// Server/data/configs/npc_configs.json, and every block has an audit row in
// docs/summoning_port/familiar_normal_combat_530.csv.
//
// `Familiar` rolls its swing with the familiar's own stats, never the owner's,
// so these are the numbers `~summoning_familiar_generic_combat_tick` needs.
//
// `defend_anim` and `death_anim` are engine fields and apply to every familiar,
// not only the ones that fight: `mock230_combat_hit_npc` plays the first on
// every hit taken and the death step plays the second. A beast of burden that
// never swings still flinches and still dies.
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
        if not needs_block(r) or r["npc"] in owned:
            continue
        out.append("[%s]" % r["npc"])
        out.extend(stat_lines(r))
        out.append("")
        written += 1
    path = os.path.join(LANE, "configs", GENERATED_NPC)
    open(path, "w").write("\n".join(out) + "\n")
    return path, written


def audit(rows):
    """Report every familiar whose block another file owns and is missing rows.

    A hand-authored block that predates this table can be missing a stat the
    swing rolls with, or can disagree with the source outright.  Both are
    silent at runtime — the roll simply uses a default — so they are named
    here rather than left to be discovered as "my familiar hits for 1".
    """
    owned = existing_blocks()
    root = os.path.join(LANE, "configs")
    findings = 0
    for r in rows:
        if not needs_block(r) or r["npc"] not in owned:
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


FOLD_MARK = "// generated by tools/gen_familiar_normal_combat.py --fold"


def fold(rows):
    """Append the missing generated rows to the block that owns each familiar.

    `mock230_content_npc` returns the FIRST block for an npc id, so a familiar
    another lane file already declares cannot be given its stats or animations
    from the generated file — they have to go into the block that exists. Doing
    that by hand once per new field is how the four ten-times-lifepoints typos
    and the slot-10 bonus misreading survived as long as they did.

    Only additions are folded. A line whose key the block already answers
    differently is a disagreement with the source that wants a person to decide,
    so it is left alone and `--audit` keeps reporting it.
    """
    owned = existing_blocks()
    root = os.path.join(LANE, "configs")
    pending = {}
    for r in rows:
        if not needs_block(r) or r["npc"] not in owned:
            continue
        owner = owned[r["npc"]]
        text = open(os.path.join(root, owner), encoding="latin-1").read()
        block = re.search(r"^\[%s\]\n(.*?)(?=^\[|\Z)" % re.escape(r["npc"]),
                          text, re.M | re.S)
        have = [l.strip() for l in block.group(1).splitlines() if l.strip()]
        keys = {l.split("=")[0] if "=" not in l or not l.startswith("param=")
                else l.split(",")[0] for l in have}
        additions = [l for l in stat_lines(r) if l not in have and (
            (l.split(",")[0] if l.startswith("param=") else l.split("=")[0])
            not in keys)]
        if additions:
            pending.setdefault(owner, []).append((r["npc"], additions))

    written = 0
    for owner, entries in sorted(pending.items()):
        path = os.path.join(root, owner)
        text = open(path, encoding="latin-1").read()
        for npc, additions in entries:
            block = re.search(r"^\[%s\]\n(.*?)(?=^\[|\Z)" % re.escape(npc),
                              text, re.M | re.S)
            body = block.group(1).rstrip("\n")
            replacement = "[%s]\n%s\n%s\n%s\n\n" % (
                npc, body, FOLD_MARK, "\n".join(additions))
            text = text[:block.start()] + replacement + text[block.end():]
            written += 1
        open(path, "w", encoding="latin-1").write(text)
    return written, len(pending)


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
// The five procs answer two different questions, and a familiar can answer one
// yes and the other no.
//
// `~summoning_familiar_auto_assists` is WOULD IT JOIN A FIGHT IT WAS NOT TOLD
// TO JOIN — the source's own conjunction from Familiar.handleTickActions:
// `isCombatFamiliar() && !isBurdenBeast() && !isPeacefulFamiliar()`.  Beasts of
// burden, foragers and the peaceful pouches answer false, exactly as 2009scape
// has them answer.
//
// The other four are CAN IT SWING AT A TARGET IT ALREADY HOLDS, and they are
// deliberately wider: any familiar with a combat variant record, a style and an
// admitted attack animation is armed, auto-assist notwithstanding (2026-08-14
// design call).  The familiar panel's Attack badge hands a target to any active
// familiar through `~summoning_familiar_command_attack`, and a familiar that
// accepts a commanded target must be able to act on it — a Spirit terrorbird
// sent at an NPC fights it, and goes back to carrying logs when it is not.
//
// So the two sets differ in both directions.  A familiar with no admitted
// attack sequence still auto-assists (`~summoning_familiar_attack_anim` returns
// null for it and the tick draws nothing), and a beast of burden never
// auto-assists but is armed for the moment it is sent.
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
        if r["armed"] else None,
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


BOUNDARY = os.path.join(DOCS, "roster_boundary_530.json")

# The three line-oriented ledgers a record's membership is spelled in.  The
# stage filters these line by line, so admitting a record means retaining its
# line in each one it appears in.
PACK_LEDGERS = {
    "seq": ("pack/seq.alloc", "pack/seq.client"),
    "frame_archive": ("pack/0_animations.pack",),
    "framemap": ("pack/1_skeletons.pack",),
}


def admission_closure(rows):
    """Every review-only record the familiar animations need, and why.

    Sequences come from the table itself — exactly the attack, defend and death
    animations the rows name — then the frame archives those sequences draw
    from, then the skeletons those archives are rigged to.  Nothing is admitted
    that no familiar animation reaches, and the two derivation steps are read
    out of the records rather than declared, so the set cannot quietly go stale.

    The unreviewed npc, obj, loc, spotanim and model records the same import
    holds are untouched: those are the gameplay surface the boundary exists to
    hold back.  A sequence and a skeleton animate a record that is already
    admitted on its own terms.
    """
    seqs = set()
    for row in rows:
        for key in ("attack_seq", "defend_seq", "death_seq"):
            name = row[key]
            if name.startswith(REVIEW_ONLY_PREFIX):
                seqs.add(name)

    animations = read_pack_index("0_animations.pack")
    skeletons = read_pack_index("1_skeletons.pack")
    archives = {animations[a] for a in seq_frame_archives(seqs) if a in animations}
    framemaps = {skeletons[f] for f in archive_framemaps(archives) if f in skeletons}

    references = sorted(seqs | archives | framemaps)
    review_only = {name for name in references
                   if name.startswith(REVIEW_ONLY_PREFIX)}
    return references, review_only, len(seqs), len(archives), len(framemaps)


def pack_lines_for(names):
    """Every ledger line that names one of `names`, verbatim."""
    wanted = set(names)
    lines = []
    for ledgers in PACK_LEDGERS.values():
        for ledger in ledgers:
            path = pack_path(*ledger.split("/"))
            if not os.path.isfile(path):
                continue
            for line in open(path, encoding="latin-1"):
                line = line.rstrip("\n")
                if line.rsplit("/", 1)[-1].split("=")[-1] in wanted:
                    lines.append(line)
    return sorted(set(lines))


def write_boundary(rows):
    references, review_only, n_seq, n_arch, n_map = admission_closure(rows)
    data = json.load(open(BOUNDARY))
    kept = [name for name in data.get("admitted_review_references", [])
            if name not in review_only]
    data["admitted_review_references"] = sorted(set(kept) | review_only)
    kept_lines = [line for line in data.get("admitted_review_pack_lines", [])
                  if line.rsplit("/", 1)[-1].split("=")[-1] not in review_only]
    data["admitted_review_pack_lines"] = sorted(
        set(kept_lines) | set(pack_lines_for(review_only)))
    with open(BOUNDARY, "w") as handle:
        json.dump(data, handle, indent=1)
        handle.write("\n")
    return BOUNDARY, n_seq, n_arch, n_map


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True,
                    help="root of a 2009scape checkout (rev 530)")
    ap.add_argument("--fold", action="store_true",
                    help="append the missing generated rows to the block that "
                         "owns each familiar in another lane .npc file")
    ap.add_argument("--audit", action="store_true",
                    help="report armed familiars whose block another lane .npc "
                         "file owns, and what that block is missing")
    args = ap.parse_args()
    rows = build(args.source)

    if args.fold:
        written, files = fold(rows)
        print("folded %d block%s across %d file%s" %
              (written, "" if written == 1 else "s",
               files, "" if files == 1 else "s"))
        return 0

    if args.audit:
        print("blocks owned elsewhere, with the stats they still need:")
        found = audit(rows)
        print("%d familiar%s need folding into another file" %
              (found, "" if found == 1 else "s"))
        return 0

    csv_path = write_csv(rows)
    npc_path, npc_count = write_npc(rows)
    table_path = write_table(rows)
    boundary_path, n_seq, n_arch, n_map = write_boundary(rows)

    assists = sum(r["auto_assist"] for r in rows)
    armed = sum(r["armed"] for r in rows)
    print("%d familiars, %d auto-assist, %d armed with a swing" %
          (len(rows), assists, armed))
    print("%d assist with a hand-written swing: %s" % (
        len(BESPOKE_TYPES),
        ", ".join(r["entry"] for r in rows if r["type"] in BESPOKE_TYPES)))
    silent = [r for r in rows
              if r["auto_assist"] and not r["armed"]
              and r["type"] not in BESPOKE_TYPES]
    print("%d assist but cannot swing: %s" % (
        len(silent),
        ", ".join("%s (no attack sequence in the source record)" % r["entry"]
                  for r in silent) or "none"))
    commanded = [r for r in rows if r["armed"] and not r["auto_assist"]]
    print("%d armed but never assist (fight only when sent): %s" % (
        len(commanded), ", ".join(r["entry"] for r in commanded) or "none"))
    for key, label in (("defend_seq", "defend"), ("death_seq", "death")):
        without = [r["entry"] for r in rows if not r[key]]
        print("%d of %d familiars have a %s animation%s" % (
            len(rows) - len(without), len(rows), label,
            "" if not without else "; without: " + ", ".join(without)))
    print("admitted %d review-only records: %d sequences, %d frame archives, "
          "%d skeletons" % (n_seq + n_arch + n_map, n_seq, n_arch, n_map))
    for p in (csv_path, npc_path, table_path, boundary_path):
        print("wrote", os.path.relpath(p, REPO))
    print("%d stat blocks written" % npc_count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
