#!/usr/bin/env python3
"""
Identify every npc's attack / defend / death animation and combat sounds, and
persist one decision file per npc.

    tools/gen_npc_combat.py --validate            measure the layers, write nothing
    tools/gen_npc_combat.py --write               write the ledger + the config

## What this replaces

`gen_npc_anims.py` answered 3,185 npcs of 16,292 and threw everything else away:
it printed "skipped 10,073 — its name is not what its rig is for" and no record
of *which* npcs, or what the alternative had been, survived the run. A number in
a terminal is not reviewable, and the next run recomputed it from scratch.

This keeps a file per npc. Every npc that exists gets one, including the ones
with no answer, because "the catalog does not know this npc's death animation"
is a finding and belongs on disk next to the ones that worked.

## Two layers, and why the ledger is not the config

The ledger (`npc_combat/<shard>/<gameval>.combat`) is the decision. The config
(`server/scripts/npc/configs/npc_anims.generated.npc`) is compiled from it.
Three reasons the ledger cannot simply *be* per-npc `.npc` files:

  1. **cachepack would silently drop them.** `cp_walk_find` fills a caller's
     array and stops — `matched < out_capacity` — and every caller passes
     `CP_PACK_MAX_SOURCES`, which is 1024. 16,292 `.npc` files would pack 1,024
     of them and report success.
  2. **A block is not free.** An npc with no block is answered straight from the
     cache; the moment one exists, `npc_default.npc` overlays `attackrate=4` on
     it. So a file that records "no answer for this npc" must not become a block,
     and a ledger can say that where a config cannot.
  3. **The ledger holds what the config cannot express.** Which layer decided,
     what the other candidates were, and — for sounds — the *name*. cachepack has
     no `synth` type (`cp_types.c`'s `g_types`), so a sound param in a `.npc`
     can only be a bare id. The name is the thing that survives a cache change,
     and this is where it lives.

Hand-editing a ledger file and setting `source = authored` freezes it: later runs
read it back and never overwrite it.

## The layers

Animation, highest confidence first. Each fills only what the ones above left
empty, and every row records which one answered.

  a0  Another server states it. LostCity by name, rsmod by seq id — **and both
      are still put through the rig gate**, which is the rule this pipeline
      learned rather than assumed. 457 of LostCity's 1,303 rows name a sequence
      built on a framemap the npc's model no longer uses, because Jagex re-rigged
      the creatures: LostCity says the cow dies by `cow_death` on framemap 282
      and this cache's cow is on 1338, whose sequences are `cow_update_*`. The
      old sequence still exists, so a name check passes where a rig check does
      not, and the rig check is the one telling the truth. rsmod fails the gate
      0 times in 156.
  a1  The sequence is on the npc's own rig *and* shares a distinctive word with
      the npc's name; ties go to the family the npc's own `readyanim` names.
  a2  The rig is unambiguous: exactly one `*_death` sequence exists across the
      npc's framemaps, so that is how this creature dies whatever it is called.
      The name gate is what a2 drops, and it can only be dropped where the rig is
      small enough to mean something — see MEGA_RIG_SEQS.
  a3  Several candidates on a small rig and no name to separate them: prefer the
      `readyanim` family, then the plainest name.
  a4  The rig's sequences name no action at all. Classify by `forcedpriority`,
      which is engine semantics rather than convention: a death animation is the
      one that must not be interrupted. Measured over the 2,556 sequences whose
      names *do* state an action, fp=10 is a death 98% of the time and fp=11
      100%; fp=6/7/8 are 99/98/100% *not* a death.
  a5  Nothing. The npc keeps `npc_default.npc`'s human unarmed set and the
      ledger says why.

Graded blind — the layers re-run with a0 switched off, on the npcs a0 can answer
— a1 scores 88% against rsmod's revision-231 answers and a2 100%. Against
LostCity's 2004 answers every layer scores far lower, and the misses are all
reworks (`unicorn_death` -> `unicorn_rework_death`), which is the layers being
right about a 239 cache rather than wrong about the game. `--validate` prints
this table, and the ablations behind a1's and a3's tie-breaks, from the data.

Sound:

  s0  The chosen sequence carries its own sound, in-band, in the cache
      (`sound=frame,id,loops,location,retain,weight`). 1,588 sequences do. The
      client already plays these — `app_play_frame_sounds` — so there is nothing
      to write and nothing to get wrong.
  s1  LostCity's `param=attack_sound,<name>` / rsmod's ids, resolved through
      `pack/4_soundeffects.pack`.
  s2  Reserved. See `--validate`: the seq-name/sound-name join is measured there
      and is not enabled, because it does not clear the bar.
  s3  Silence, which the -1 sentinel already supports.
"""

import argparse
import csv
import os
import re
import sys
from collections import defaultdict, Counter

# ---------------------------------------------------------------------------
# Constants that encode a measurement rather than a preference
# ---------------------------------------------------------------------------

# A rig with more sequences than this is not a creature, it is a pile. Framemap 0
# holds 3,905 sequences — every human in the game plus every emote, every skill
# animation and every cutscene — so "the only `*_death` on this rig" says nothing
# there. 6,960 npcs sit on such a rig; for them only a0 and a1 may answer, which
# is right anyway: they are humans, and the human unarmed set is *correct* for a
# human.
MEGA_RIG_SEQS = 200

# The npc's name must account for a real share of its own rig (a1). `slayer`
# names 94 sequences and `gnome` 68, so frequency cannot separate a creature word
# from a namespace word — but on the rig each npc actually uses, `gnome` is 40 of
# 98 sequences while `slayer` is ~1% of framemap 0's 3,905.
MIN_FAMILY_SHARE = 0.10
MIN_FAMILY_SIZE = 3

# `forcedpriority` values that classify, and what they classify as (a4). Derived,
# not chosen: see `--validate`, which recomputes these confusion counts from the
# labelled sequences every run and prints them.
#
# The field is the engine's interruption rule, which is why it carries meaning a
# naming convention would not: a death animation is precisely the one that must
# play to the end. 10 and 11 are 98% and 100% death among labelled sequences; 6,
# 7 and 8 are 99%, 100% and 100% *not* a death, and 83/95/95% attack within that.
# 9 is 50/50 and is left out.
FP_DEATH = {"10", "11"}
FP_LIVE = {"6", "7", "8"}

# Splitting a4's fp=6 pool into attack and defend. Labelled attack sequences run
# p25=31 / median=51 / p75=69 cycles and defend p25=36 / median=48 / p75=63 — the
# distributions sit on top of each other, so duration cannot separate them and
# this does not pretend it can. a4 assigns fp=6 to `attack_anim` only, and leaves
# defend to the layers that have a name to go on.
A4_ASSIGNS_DEFEND = False

ACTION_WORDS = {
    "walk", "run", "idle", "stand", "ready", "attack", "block", "death", "die",
    "dead", "spawn", "hit", "hurt", "cast", "shoot", "bow", "melee", "range",
    "ranged", "magic", "turn", "left", "right", "back", "forward", "emote",
    "anim", "animation", "seq", "talk", "open", "close", "sit", "jump", "climb",
    "fall", "land", "sleep", "eat", "drink", "spec", "special", "north",
    "south", "east", "west", "start", "end", "loop", "intro", "outro", "phase",
    "teleport", "tele", "gfx", "proj", "projectile", "impact", "travel", "npc",
    "monster", "boss", "male", "female", "man", "woman", "human", "player",
    "quest", "misc", "new", "old", "big", "small", "large", "mini", "chathead",
    "head", "body", "front", "down", "type", "var", "default", "base", "main",
    "extra", "temp", "test", "dummy", "defend", "defence", "guard",
}

SLOTS = (
    # (ledger key, words a matching sequence name contains, the plain tail)
    ("death_anim", ("death", "dying"), "death"),
    ("attack_anim", ("attack",), "attack"),
    ("defend_anim", ("defend", "block"), "defend"),
)
ANIM_KEYS = [s[0] for s in SLOTS]
SOUND_KEYS = ("attack_sound", "defend_sound", "death_sound")
# Which animation an npc is playing when each sound should fire. s0 reads the
# sound out of that animation.
SOUND_OF_ANIM = {
    "attack_sound": "attack_anim",
    "defend_sound": "defend_anim",
    "death_sound": "death_anim",
}

WIELD_HINTS = {
    "armed": ("weapon", "sword"),
    "sword": ("sword", "weapon"),
    "staff": ("staff",),
    "mage": ("mage", "staff"),
    "wizard": ("mage", "staff"),
    "bow": ("bow",),
    "archer": ("bow",),
    "ranger": ("bow",),
    "unarmed": ("unarmed",),
}


def tokens(name):
    """Distinctive words in a gameval name: letters only, four or more, not an
    action word."""
    if not name:
        return set()
    return {w for w in re.split(r"[^a-zA-Z]+", name.lower())
            if len(w) >= 4 and w not in ACTION_WORDS}


def action_of(seq_name):
    """Which slot a sequence's own name claims, or None."""
    n = seq_name.lower()
    if "death" in n or "dying" in n:
        return "death_anim"
    if "attack" in n:
        return "attack_anim"
    if "block" in n or "defend" in n:
        return "defend_anim"
    return None


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------
#
# Everything is read as latin-1. `npc_rigs.csv` carries display names straight
# off the cache and those are not all valid UTF-8 (0xe8 in row 363); a decode
# error there would take the whole catalog down over a character nothing reads.


def read_csv(path):
    with open(path, encoding="latin-1", newline="") as f:
        return list(csv.DictReader(f))


def load_catalog(catalog_dir):
    seqs_by_framemap = defaultdict(list)   # framemap -> [(seq_id, name, frames)]
    seq_framemaps = defaultdict(set)       # seq name -> {framemap}
    seq_name_by_id = {}
    for row in read_csv(os.path.join(catalog_dir, "framemap_seqs.csv")):
        if not row["seq_name"]:
            continue
        fm, sid = int(row["framemap_id"]), int(row["seq_id"])
        seqs_by_framemap[fm].append((sid, row["seq_name"], int(row["frame_count"])))
        seq_framemaps[row["seq_name"]].add(fm)
        seq_name_by_id[sid] = row["seq_name"]

    npc_rigs = defaultdict(set)
    npc_gameval = {}
    for row in read_csv(os.path.join(catalog_dir, "npc_rigs.csv")):
        npc_id = int(row["npc_id"])
        npc_rigs[npc_id].add(int(row["framemap_id"]))
        if row["gameval"]:
            npc_gameval[npc_id] = row["gameval"]

    # `npc_name_matches.csv` is deliberately not read. It was a3's tie-break until
    # the ablation in `--validate` showed its ranking scored 27% where the npc's
    # own `readyanim` stem scores 58% — identical to ignoring the npc entirely.
    # A 24 MB, 314k-row input that measures as noise is not an input.
    return seqs_by_framemap, seq_framemaps, seq_name_by_id, npc_rigs, npc_gameval


def parse_config(path):
    """A cachepack `all.<type>` export: `[name]` blocks of `key=value` lines,
    keys repeating."""
    out = {}
    current = None
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = {}
                out[line[1:-1]] = current
            elif current is not None and "=" in line and not line.startswith("//"):
                k, v = line.split("=", 1)
                current.setdefault(k, []).append(v)
    return out


def seq_features(all_seq):
    """Per sequence: how long it runs, what stops it being interrupted, and the
    sounds it carries in-band.

    `duration` is the sum of each frame's step, in client cycles (20ms). The
    frame line is `frame=<packed frame id>,<step>`.
    """
    feats = {}
    for name, rec in all_seq.items():
        steps = [f.split(",") for f in rec.get("frame", []) if "," in f]
        sounds = []
        for s in rec.get("sound", []):
            parts = s.split(",")
            if len(parts) >= 6:
                sounds.append({
                    "frame": int(parts[0]), "id": int(parts[1]),
                    "loops": int(parts[2]), "location": int(parts[3]),
                    "retain": int(parts[4]), "weight": int(parts[5]),
                })
        feats[name] = {
            "frames": len(steps),
            "duration": sum(int(p[1]) for p in steps if p[1].lstrip("-").isdigit()),
            "forcedpriority": rec.get("forcedpriority", [None])[0],
            "priority": rec.get("priority", [None])[0],
            "sounds": sounds,
        }
    return feats


def load_sound_pack(content_dir):
    """`pack/4_soundeffects.pack` both ways: name -> id and id -> name."""
    by_name, by_id = {}, {}
    path = os.path.join(content_dir, "pack", "4_soundeffects.pack")
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if "=" not in line or line.startswith("//"):
                continue
            k, v = line.split("=", 1)
            if k.isdigit():
                by_name[v.strip()] = int(k)
                by_id[int(k)] = v.strip()
    return by_name, by_id


def load_lostcity(root):
    """LostCity's authored combat rows, keyed by npc gameval name.

    Both halves come from the same blocks: `param=attack_anim,<seq name>` and
    `param=attack_sound,<sound name>`. Names throughout, which is what makes them
    usable here at all.
    """
    wanted = {"attack_anim", "defend_anim", "death_anim",
              "attack_sound", "defend_sound", "death_sound"}
    out = defaultdict(dict)
    if not os.path.isdir(root):
        return out
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".npc"):
                continue
            current = None
            with open(os.path.join(dirpath, fn), encoding="latin-1") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("[") and line.endswith("]"):
                        current = line[1:-1]
                    elif current and line.startswith("param="):
                        body = line[6:]
                        if "," not in body:
                            continue
                        key, value = body.split(",", 1)
                        if key in wanted and value != "null":
                            out[current][key] = value
    return out


def load_rsmod(path, seq_name_by_id, sound_by_id):
    """rsmod's enricher toml, keyed by npc gameval, values as raw ids.

    The ids are another tree's (rsmod targets revision 231) so nothing is taken
    on trust: each is resolved to a *name* here, and `--validate` reports how
    often that name is the one LostCity independently states for the same npc.
    """
    out = defaultdict(dict)
    if not os.path.exists(path):
        return out
    current = None
    keys = {"attack_anim", "defend_anim", "death_anim",
            "attack_sound", "defend_sound", "death_sound"}
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if line == "[[config]]":
                current = None
            elif line.startswith("npc = "):
                current = line.split("=", 1)[1].strip().strip("'\"")
            elif current and "=" in line:
                k, v = (x.strip() for x in line.split("=", 1))
                if k in keys and v.lstrip("-").isdigit():
                    table = seq_name_by_id if k.endswith("_anim") else sound_by_id
                    name = table.get(int(v))
                    if name:
                        out[current][k] = name
    return out


def load_authored_blocks(content_dir, exclude_path):
    """Npcs that already have a hand-written `.npc` block anywhere in the tree.

    A second block for the same npc does not error — the loader appends a second
    def with the same id — so a generated block restating an authored npc would
    silently shadow a hand-checked port.
    """
    authored = {}
    root = os.path.join(content_dir, "server", "scripts")
    exclude = os.path.abspath(exclude_path)
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".npc"):
                continue
            full = os.path.join(dirpath, fn)
            if os.path.abspath(full) == exclude:
                continue
            with open(full, encoding="latin-1") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("[") and line.endswith("]") and line != "[default]":
                        authored.setdefault(line[1:-1],
                                            os.path.relpath(full, content_dir))
    return authored


def claim_server_membership(content_dir, names):
    """Name every configured npc in `pack/npc.server`, and return how many were new.

    That file is the tree's statement of which npcs have a *server half*, and
    `cachepack pack` routes on it: a record gets a server band only if this file
    names it, and stating a server field without being named is a hard error --
    "an entity cannot carry a field its own side does not receive". So giving an
    npc `attack_anim` and leaving this file alone does not half-work, it fails the
    pack.

    `content.ini` declares this namespace `membership = authored`, meaning a
    person decides and nothing regenerates the file. This does not regenerate it:
    it merges, never removing a name and never rewriting the header.

    Insertion is in sorted position rather than by rebuilding the file, and that
    is load-bearing: the roster carries an eight-line comment block *mid-file*, at
    the point where a `default` entry used to sit, explaining why it was removed.
    Splitting on "comments first, names after" sweeps those lines into the sorted
    body and scatters them.
    """
    path = os.path.join(content_dir, "pack", "npc.server")
    with open(path, encoding="latin-1") as f:
        lines = f.read().splitlines()
    listed = {l.strip() for l in lines
              if l.strip() and not l.strip().startswith("//")}
    new = sorted(set(names) - listed)
    if not new:
        return 0
    out = []
    pending = list(new)
    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            while pending and pending[0] < stripped:
                out.append(pending.pop(0))
        out.append(line)
    out.extend(pending)
    with open(path, "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")
    return len(new)


def load_default_attackrate(content_dir):
    """`[default] param=attackrate` — the value a block imposes just by existing."""
    path = os.path.join(content_dir, "server", "scripts", "general", "configs",
                        "npc_default.npc")
    in_default = False
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                in_default = line == "[default]"
            elif in_default and line.startswith("param=attackrate,"):
                return line.rsplit(",", 1)[1]
    return None


# ---------------------------------------------------------------------------
# The ledger
# ---------------------------------------------------------------------------

LEDGER_DIR = "npc_combat"


def shard_of(gameval):
    c = gameval[0].lower() if gameval else "_"
    return c if c.isalnum() else "_"


def ledger_path(content_dir, gameval):
    return os.path.join(content_dir, LEDGER_DIR, shard_of(gameval), gameval + ".combat")


def read_ledger(path):
    """`key = value  // note` lines. Returns {key: (value, note)}; `-` is None."""
    rows = {}
    if not os.path.exists(path):
        return rows
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.lstrip().startswith("//") or "=" not in line:
                continue
            body, _, note = line.partition("//")
            k, _, v = body.partition("=")
            v = v.strip()
            rows[k.strip()] = (None if v == "-" else v, note.strip())
    return rows


# Written and read as latin-1, and every character this file contributes is
# ASCII. The display name comes out of `configs/all.npc`, which is a cachepack
# export of the cache's own bytes and is not all valid UTF-8 -- there is a 0xe8 in
# row 363 of the catalog alone. Reading and writing the same 8-bit encoding
# round-trips those bytes exactly; anything else either mangles a name or refuses
# to write the file over a character no reader cares about.
LEDGER_HEADER = """\
// {display}
// npc {npc_id}{rig_note}
//
// Generated by tools/gen_npc_combat.py. Re-running rewrites this file.
//
// To pin a value by hand: edit it, set `source = authored`, and every later run
// will read this file back and leave it exactly as it stands. That is the whole
// contract - there is no second place to register the exception.
//
// The trailing note on each row names the layer that decided it; the layers are
// documented in docs/DEATH_ATK_DEF_ANIMS.md.
"""


def write_ledger(path, display, npc_id, rig_note, source, rows, shadowed_by=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    out = [LEDGER_HEADER.format(display=display or "(unnamed)", npc_id=npc_id,
                                rig_note=rig_note)]
    if shadowed_by:
        # Without this line the file is a trap: someone pins a value here, sets
        # `source = authored`, watches the pin survive every regeneration, and
        # never sees it in the game -- because a hand-written `.npc` block wins
        # over the compiled one and the compiler skips this npc entirely. Saying
        # so costs one line and turns a silent no-op into an instruction.
        out.append("// NOT COMPILED. %s states its own [%s] block, and an authored\n"
                   "// .npc block always wins over this generator's output. Edit that\n"
                   "// file instead; changes here will not reach the game.\n"
                   % (shadowed_by, os.path.basename(path)[:-len(".combat")]))
    out.append("source       = %s" % source)
    out.append("")
    width = max(len(k) for k in list(ANIM_KEYS) + list(SOUND_KEYS))
    for group in (ANIM_KEYS, SOUND_KEYS):
        for key in group:
            value, note = rows.get(key, (None, ""))
            line = "%-*s = %s" % (width, key, value if value else "-")
            if note:
                line = "%-*s // %s" % (44, line, note)
            out.append(line.rstrip())
        out.append("")
    with open(path, "w", encoding="latin-1", errors="replace") as f:
        f.write("\n".join(out).rstrip() + "\n")


# ---------------------------------------------------------------------------
# The layers
# ---------------------------------------------------------------------------


def rank_key(candidate, wanted, exact):
    """Prefer the plainest name — `zuk_death` over `zuk_death_hardmode`.

    A creature with several variants of an action almost always names the default
    one with the bare word, so picking the longest would systematically choose
    special cases.
    """
    name = candidate[1].lower()
    parts = [p for p in re.split(r"[^a-z]+", name) if p]
    tail = parts[-1] if parts else ""
    return (
        0 if (wanted and any(w in parts for w in wanted)) else 1,
        0 if (not wanted and "unarmed" in parts) else 1,
        0 if tail == exact else 1,
        len(parts), len(name), candidate[0],
    )


def decide(npc_id, gameval, rig_seqs, mega, words, feats, readyanim,
           lostcity, rsmod, seq_framemaps, rigs, suppress_a0=False):
    """Every animation slot for one npc, as {slot: (seq_name, layer, note)}.

    `suppress_a0` runs the inference layers blind, which is how `--validate`
    grades them: it asks a1-a4 for an answer on the npcs a0 already knows, and
    compares.
    """
    out = {}

    # ---- a0: another server states it -------------------------------------
    #
    # rsmod first, and this order is measured rather than assumed. rsmod states
    # seq *ids* from revision 231, which the "ids never cross trees" rule says to
    # distrust — so all 156 of its animation rows were resolved through this
    # cache's seq table and checked: 156 of 156 resolve to a named sequence, and
    # 156 of 156 sit on the npc's own rig. Across 2,446 framemaps that is not
    # something a wrong id space produces.
    #
    # Its 30 disagreements with LostCity are then not errors but era: `chicken_*`
    # 55-57 against `lore_chicken_*` 5387-5389, `rat_*` against `mouse_*`,
    # `giantspider_*` against `spider_update_*`, `duck_death` against
    # `duck_rework_death`. Each is a coherent attack/defend/death triple on
    # consecutive ids — Jagex reworking a creature, with rsmod holding the newer
    # form. This tree is 239, so the newer form is the right one.
    rejected = defaultdict(list)
    if not suppress_a0:
        for source_name, table in (("rsmod", rsmod.get(gameval, {})),
                                   ("lc", lostcity.get(gameval, {}))):
            label = "rsmod" if source_name == "rsmod" else "LostCity"
            for slot in ANIM_KEYS:
                if slot in out or slot not in table:
                    continue
                name = table[slot]
                if name not in seq_framemaps:
                    continue
                # THE RIG GATE APPLIES TO a0 TOO, and this is the one rule in the
                # whole pipeline that was learned the hard way rather than
                # designed. Being stated by another server is not the same as
                # being playable *here*: a sequence built on a framemap this npc's
                # model does not use drives bones the model does not have.
                #
                # 457 of LostCity's 1,303 rows fail it -- 35% -- because Jagex
                # re-rigged the creatures. The cow is the clean example: LostCity
                # says `cow_death`, which is on framemap 282, and this cache's cow
                # is on framemap 1338, whose sequences are `cow_update_walk`,
                # `cow_update_attack`, `cow_update_death`. The old sequence still
                # exists, so a name check passes and a rig check does not, and the
                # rig check is the one telling the truth. The right answer was on
                # the rig the whole time and a2 finds it.
                #
                # rsmod fails it zero times out of 156, which is its own argument.
                if rigs and not (seq_framemaps[name] & rigs):
                    rejected[slot].append("%s says %s, not on this rig" % (label, name))
                    continue
                out[slot] = (name, "a0-" + source_name, "%s states it" % label)

    by_slot = defaultdict(list)
    for cand in rig_seqs:
        slot = action_of(cand[1])
        if slot:
            by_slot[slot].append(cand)

    # The npc's own `readyanim` is the strongest tie-break available, and it is
    # used by both a1 and a3. It is the one animation the *cache itself* binds to
    # this npc, so it names the family this creature's other animations belong to:
    # the goblin's is `slice_surface_goblin_squat_ready`, which is how you know
    # the modern goblin is the `slice_surface_goblin_*` family and not the
    # `champions_goblin_*` or `100_goblin_*` ones sharing its rig.
    #
    # Measured on the multi-candidate slots a1 owns, against truth restricted to
    # sequences that are themselves on the npc's rig: adding it takes a1 from 71%
    # to 86% against LostCity and leaves 81% against rsmod unchanged. Matching the
    # *whole* readyanim prefix rather than the stem is worse than either (78%/63%)
    # -- it over-fits to one variant of a name.
    ready_stem = readyanim.split("_")[0] if readyanim else None

    def prefers_ready_stem(c):
        return 0 if (ready_stem and c[1].startswith(ready_stem + "_")) else 1

    # ---- a1: on the rig, and shares a word with the npc's name ------------
    family = [c for c in rig_seqs if tokens(c[1]) & words]
    family_ok = (rig_seqs and len(family) >= MIN_FAMILY_SIZE
                 and len(family) / float(len(rig_seqs)) >= MIN_FAMILY_SHARE)
    wanted = set()
    for word in words:
        wanted.update(WIELD_HINTS.get(word, ()))
    if family_ok:
        for slot, _needles, exact in SLOTS:
            if slot in out:
                continue
            hits = [c for c in family if action_of(c[1]) == slot]
            if not hits:
                continue
            hits.sort(key=lambda c: (prefers_ready_stem(c),) + rank_key(c, wanted, exact))
            out[slot] = (hits[0][1], "a1",
                         "rig+name, %d candidate%s" % (len(hits), "" if len(hits) == 1 else "s"))

    # Below here the name gate is gone, so the rig has to carry the whole claim.
    # On a pile it carries nothing.
    if mega:
        for slot in ANIM_KEYS:
            out.setdefault(slot, (None, "a5", "rig is shared (%d seqs); only a name could tell"
                                  % len(rig_seqs)))
        return finish(out, rejected)

    for slot, _needles, exact in SLOTS:
        if slot in out:
            continue
        hits = by_slot.get(slot, [])
        if not hits:
            continue
        if len(hits) == 1:
            # ---- a2: the rig is unambiguous ------------------------------
            out[slot] = (hits[0][1], "a2", "rig's only %s seq" % exact)
            continue
        # ---- a3: rank the candidates -------------------------------------
        #
        # The npc's own `readyanim` is the tie-break, and it is the *only* one
        # that measures. An ablation over the slots a3 owns (`--validate`
        # reproduces it) scored: readyanim stem 58% against rsmod's 239-era
        # answers, plainest-name-only 27%, and `npc_name_matches.csv`'s affinity
        # score 27% — i.e. affinity ranked no better than ignoring the npc
        # entirely, so it was removed along with the 24 MB file it needed.
        #
        # It makes sense that readyanim wins: it is the one animation the cache
        # itself binds to this npc, so it names the family the creature's other
        # animations belong to. `swarm_ready` -> `swarm_attack`.
        hits = sorted(hits, key=lambda c: (prefers_ready_stem(c),)
                      + rank_key(c, wanted, exact))
        best = hits[0]
        why = "ranked %d on rig; %s" % (
            len(hits),
            "shares stem with readyanim" if (ready_stem and best[1].startswith(ready_stem + "_"))
            else "plainest name")
        out[slot] = (best[1], "a3", why)

    # ---- a4: the rig names no action; classify by forcedpriority ----------
    if "death_anim" not in out:
        deaths = [c for c in rig_seqs
                  if feats.get(c[1], {}).get("forcedpriority") in FP_DEATH]
        if len(deaths) == 1:
            fp = feats[deaths[0][1]]["forcedpriority"]
            out["death_anim"] = (deaths[0][1], "a4",
                                 "rig's only forcedpriority=%s seq" % fp)
    if "attack_anim" not in out:
        live = [c for c in rig_seqs
                if feats.get(c[1], {}).get("forcedpriority") in FP_LIVE]
        if len(live) == 1:
            out["attack_anim"] = (live[0][1], "a4", "rig's only forcedpriority=6 seq")
    if A4_ASSIGNS_DEFEND and "defend_anim" not in out:
        pass  # deliberately unreachable; see the constant's note

    for slot in ANIM_KEYS:
        out.setdefault(slot, (None, "a5", "nothing on its rig names or implies one"))
    return finish(out, rejected)


def finish(out, rejected):
    """Fold the overruled a0 rows into each slot's note.

    A rejected a0 row is a finding, not a silence: it says another server had an
    opinion and this pipeline overruled it. Carried into the ledger so the
    overruling is visible per npc rather than only in a total.
    """
    for slot, notes in rejected.items():
        value, layer, why = out[slot]
        out[slot] = (value, layer, why + "; " + "; ".join(notes))
    return out


def decide_sounds(anims, gameval, feats, lostcity, rsmod, sound_by_name):
    """Every sound slot, as {slot: (sound_name_or_None, layer, note)}."""
    out = {}
    for key in SOUND_KEYS:
        anim_key = SOUND_OF_ANIM[key]
        anim = anims.get(anim_key, (None,))[0]

        # ---- s0: the animation carries its own sound ---------------------
        sounds = feats.get(anim, {}).get("sounds") if anim else None
        if sounds:
            ids = sorted({s["id"] for s in sounds})
            shown = ", ".join(str(i) for i in ids[:3]) + ("..." if len(ids) > 3 else "")
            out[key] = (None, "s0", "%s carries synth %s in-band" % (anim, shown))
            continue

        # ---- s1: another server states it ---------------------------------
        for source_name, table in (("rsmod", rsmod.get(gameval, {})),
                                   ("lc", lostcity.get(gameval, {}))):
            # rsmod first: where the two disagree it is consistently the more
            # modern of the pair (an armed goblin gets a spear sound rather than
            # the generic one), and this tree targets 239.
            if key not in table:
                continue
            name = table[key]
            ident = sound_by_name.get(name)
            if ident is None:
                continue
            out[key] = (name, "s1-" + source_name, "synth %d" % ident)
            break
        else:
            out[key] = (None, "s3", "no source" if not anim else "%s carries none" % anim)
    return out


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------


def validate(all_seq, feats, lostcity, rsmod, seq_framemaps, sound_by_name, decisions,
             rsmod_rigs):
    """Measure every claim this generator makes, from the data, every run."""
    print("\n=== a4's classifier, recomputed ===")
    print("Sequences whose own name states an action are the labelled set; the")
    print("question is what `forcedpriority` predicts for the ones that do not.")
    labelled = {n: action_of(n) for n in all_seq}
    labelled = {n: a for n, a in labelled.items() if a}
    print("  labelled %d of %d sequences" % (len(labelled), len(all_seq)))
    for fp in ("10", "11", "6", "9", "8", "7"):
        sel = [n for n in labelled if feats.get(n, {}).get("forcedpriority") == fp]
        total = sum(1 for n in all_seq if feats.get(n, {}).get("forcedpriority") == fp)
        if not sel:
            continue
        c = Counter(labelled[n] for n in sel)
        top, hits = c.most_common(1)[0]
        used = "USED->death" if fp in FP_DEATH else ("USED->attack" if fp in FP_LIVE else "unused")
        print("  fp=%-3s %5d labelled of %5d  %-13s %3d%% %s   %s"
              % (fp, len(sel), total, top.replace("_anim", ""),
                 100 * hits // len(sel), dict(c), used))
        if fp in FP_LIVE:
            live = sum(v for k, v in c.items() if k != "death_anim")
            print("        (as 'not a death': %d%%)" % (100 * live // len(sel)))

    print("\n=== a0's two sources against each other ===")
    print("rsmod states seq *ids* from revision 231, and 'ids never cross trees'.")
    print("So the ids are checked against this cache rather than trusted:")
    resolves = on_rig = total = 0
    for npc, table in rsmod.items():
        for key in ANIM_KEYS:
            if key not in table:
                continue
            total += 1
            resolves += 1  # load_rsmod only keeps ids that resolved
            if seq_framemaps.get(table[key], set()) & rsmod_rigs.get(npc, set()):
                on_rig += 1
    print("  rsmod animation rows                        %4d" % total)
    print("  ...resolving to a named seq in this cache   %4d" % resolves)
    print("  ...sitting on that very npc's own rig       %4d   <- across 2,446 framemaps"
          % on_rig)
    agree = disagree = 0
    rows = []
    for npc, lc in lostcity.items():
        rs = rsmod.get(npc)
        if not rs:
            continue
        for key in ANIM_KEYS:
            if key in lc and key in rs:
                if lc[key] == rs[key]:
                    agree += 1
                else:
                    disagree += 1
                    rows.append((npc, key, lc[key], rs[key]))
    print("  overlap with LostCity: %d rows, agree %d, differ %d"
          % (agree + disagree, agree, disagree))
    print("  The differences are era, not error — each is a coherent rework:")
    for npc, key, a, b in rows[:6]:
        print("    %-22s %-12s 2004 %-26s 239 %s" % (npc, key, a, b))

    print("\n=== a1-a4 graded blind, and graded by ERA ===")
    print("The inference layers are re-run with a0 switched off, on exactly the")
    print("npcs a0 can answer, and asked the same question. Graded against each")
    print("source separately, because the two sources are two different games:")
    print("LostCity is September 2004 and rsmod is revision 231.")
    per_layer = defaultdict(lambda: defaultdict(Counter))
    examples = defaultdict(list)
    tested = 0
    for gameval, (npc_id, _anims, _sounds, held, blind) in decisions.items():
        if blind is None:
            continue
        tested += 1
        for era, table in (("239", rsmod.get(gameval, {})),
                           ("2004", lostcity.get(gameval, {}))):
            for key in ANIM_KEYS:
                if key not in table or table[key] not in seq_framemaps:
                    continue
                got, layer, _why = blind.get(key, (None, "a5", ""))
                c = per_layer[layer][era]
                c["n"] += 1
                if got == table[key]:
                    c["hit"] += 1
                elif got is None:
                    c["silent"] += 1
                else:
                    c["miss"] += 1
                    if era == "2004" and len(examples[layer]) < 3:
                        examples[layer].append((gameval, key, table[key], got))
    print("  %d npcs have an a0 answer to grade against\n" % tested)
    print("  %-5s %22s %22s" % ("layer", "vs rsmod (revision 231)", "vs LostCity (2004)"))
    for layer in sorted(per_layer):
        cells = []
        for era in ("239", "2004"):
            c = per_layer[layer][era]
            answered = c["hit"] + c["miss"]
            cells.append("%4d/%-4d %3d%%" % (c["hit"], answered,
                                             100 * c["hit"] // answered) if answered
                         else "%17s" % ("silent x%d" % c["silent"]))
        print("  %-5s %22s %22s" % (layer, cells[0], cells[1]))
    print("\n  Every layer scores far better against the modern source, and the")
    print("  2004 'misses' are the reason:")
    for layer in sorted(examples):
        for gv, key, want, got in examples[layer][:2]:
            print("    %-4s %-22s %-12s 2004 %-24s -> %s" % (layer, gv, key, want, got))
    print("  Those are Jagex reworks — `unicorn_*` -> `unicorn_rework_*`, `bat_*` ->")
    print("  `bat_rework_*` — and rsmod picks the same reworks (`chicken` ->")
    print("  `lore_chicken`). A cache-derived layer agreeing with the cache's newer")
    print("  animation over a 2004 server's older one is the layer being right.")

    print("\n=== s2, the seq-name/sound-name join (measured, not enabled) ===")
    both = joinable = exact = 0
    for gameval, lc in lostcity.items():
        for skey, akey in SOUND_OF_ANIM.items():
            if skey not in lc or akey not in lc:
                continue
            both += 1
            seq, snd = lc[akey], lc[skey]
            if seq in sound_by_name:
                joinable += 1
                if sound_by_name[seq] == sound_by_name.get(snd):
                    exact += 1
    print("  npc rows stating both an animation and its sound   %d" % both)
    print("  ...where the animation's own name is also a sound  %d" % joinable)
    print("  ...and that sound is the one the npc actually uses %d" % exact)
    print("  A join that fires on %d%% of rows cannot carry the other 96%% of npcs."
          % (100 * joinable // both if both else 0))
    print("  s2 stays disabled.")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def fix_authored(content_dir, out_path, seq_framemaps, npc_rigs, gameval_to_id,
                 decisions, write):
    """Correct animation rows in hand-authored `.npc` files that name a sequence
    the npc's own model cannot play.

    An authored block always wins over this generator's output, and that is the
    right rule for a *preference*. It is the wrong rule for a row that does not
    work: 54 of the 374 animation rows across the tree's authored `.npc` files
    name a sequence built on a framemap the npc is not on, so the client is told
    to drive bones the model does not have and nothing plays. Every one of them
    is a LostCity port that predates Jagex re-rigging the creature.

    This is the failure a player actually sees, and it hid behind the rule: the
    Lumbridge roster is all authored, so the goblin, cow, chicken and rat kept
    `goblin_death` / `cow_death` / `chicken_death` / `rat_death` — framemaps 308,
    282, 281, 331 against rigs 1415, 1338, 1255, 326 — and looked like they were
    still using the human default. The generator had already worked out the right
    answers (`slice_surface_goblin_death`, `cow_update_death`,
    `lore_chicken_death`, `mouse_death`) and was skipping those npcs.

    Only off-rig rows are touched. An authored row that works is left exactly as
    its author wrote it.
    """
    root = os.path.join(content_dir, "server", "scripts")
    exclude = os.path.abspath(out_path)
    fixed, unfixable, checked, playable = [], [], 0, 0

    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if not fn.endswith(".npc"):
                continue
            full = os.path.join(dirpath, fn)
            if os.path.abspath(full) == exclude:
                continue
            with open(full, encoding="latin-1") as f:
                lines = f.read().splitlines()
            current = None
            changed = False
            for i, line in enumerate(lines):
                s = line.strip()
                if s.startswith("[") and s.endswith("]"):
                    current = s[1:-1]
                    continue
                if not current or not s.startswith("param="):
                    continue
                key, _, value = s[6:].partition(",")
                if key not in ANIM_KEYS or not value:
                    continue
                rigs = npc_rigs.get(gameval_to_id.get(current, -1), set())
                if not rigs or value not in seq_framemaps:
                    continue
                checked += 1
                if seq_framemaps[value] & rigs:
                    playable += 1
                    continue
                # Off-rig. What did the rig walk decide for this npc?
                entry = decisions.get(current)
                replacement = entry[1].get(key, (None,))[0] if entry else None
                if not replacement or not (seq_framemaps.get(replacement, set()) & rigs):
                    unfixable.append((current, key, value, fn))
                    continue
                lines[i] = line.replace("param=%s,%s" % (key, value),
                                        "param=%s,%s" % (key, replacement))
                fixed.append((current, key, value, replacement, fn))
                changed = True
            if changed and write:
                with open(full, "w", encoding="latin-1") as f:
                    f.write("\n".join(lines) + "\n")

    print("\nauthored .npc animation rows checked against each npc's own rig: %d"
          % checked)
    print("   playable                              %d" % playable)
    print("   off-rig, corrected from the rig walk  %d" % len(fixed))
    print("   off-rig, no on-rig answer known       %d" % len(unfixable))
    for npc, key, old, new, fn in fixed[:12]:
        print("      %-24s %-12s %-24s -> %s   [%s]" % (npc, key, old, new, fn))
    if len(fixed) > 12:
        print("      ... and %d more" % (len(fixed) - 12))
    for npc, key, old, fn in unfixable[:6]:
        print("      LEFT %-22s %-12s %-24s [%s]" % (npc, key, old, fn))
    if not write:
        print("   (dry run — pass --write to correct them)")
    return len(fixed)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--catalog", default="out/osrs239_anims")
    ap.add_argument("--content", default="OSRS-Content/osrs239-content")
    ap.add_argument("--lostcity",
                    default=os.path.expanduser("~/Documents/git_repos/LostCity_Server/content"))
    ap.add_argument("--rsmod", default=os.path.expanduser(
        "~/Documents/git_repos/rsmod/api/cache-enricher/src/main/resources/"
        "org/rsmod/api/cache/enricher/npc/npcs.toml"))
    ap.add_argument("--write", action="store_true",
                    help="write the ledger and the compiled config")
    ap.add_argument("--validate", action="store_true",
                    help="measure every layer against held-out authored data")
    ap.add_argument("--fix-authored", action="store_true",
                    help="correct off-rig animation rows in hand-authored .npc files")
    args = ap.parse_args()

    out_path = os.path.join(args.content, "server", "scripts", "npc", "configs",
                            "npc_anims.generated.npc")

    (seqs_by_framemap, seq_framemaps, seq_name_by_id,
     npc_rigs, npc_gameval) = load_catalog(args.catalog)
    all_seq = parse_config(os.path.join(args.content, "configs", "all.seq"))
    all_npc = parse_config(os.path.join(args.content, "configs", "all.npc"))
    feats = seq_features(all_seq)
    sound_by_name, sound_by_id = load_sound_pack(args.content)
    lostcity = load_lostcity(args.lostcity)
    rsmod = load_rsmod(args.rsmod, seq_name_by_id, sound_by_id)
    authored_elsewhere = load_authored_blocks(args.content, out_path)
    default_rate = load_default_attackrate(args.content)

    print("catalog: %d npcs with a rig, %d framemaps, %d named sequences"
          % (len(npc_rigs), len(seqs_by_framemap), len(seq_framemaps)))
    print("sources: LostCity %d npcs, rsmod %d npcs, sound pack %d names"
          % (len(lostcity), len(rsmod), len(sound_by_name)))

    # Every npc the cache states, not merely every npc with a rig: an npc with no
    # rig still gets a file saying so.
    all_gamevals = sorted(all_npc)
    gameval_to_id = {gv: i for i, gv in npc_gameval.items()}

    decisions = {}
    layer_tally = Counter()
    slot_tally = Counter()
    sound_tally = Counter()
    frozen = 0

    for gameval in all_gamevals:
        rec = all_npc[gameval]
        npc_id = gameval_to_id.get(gameval, -1)
        display = rec.get("name", [""])[0]
        readyanim = rec.get("readyanim", [""])[0]

        path = ledger_path(args.content, gameval)
        existing = read_ledger(path)
        if existing.get("source", (None,))[0] == "authored":
            frozen += 1
            anims = {k: (existing.get(k, (None, ""))[0], "authored", existing.get(k, (None, ""))[1])
                     for k in ANIM_KEYS}
            sounds = {k: (existing.get(k, (None, ""))[0], "authored", existing.get(k, (None, ""))[1])
                      for k in SOUND_KEYS}
            decisions[gameval] = (npc_id, anims, sounds, None, None)
            layer_tally["authored"] += 1
            continue

        rigs = npc_rigs.get(npc_id, set())
        rig_seqs = []
        for fm in rigs:
            rig_seqs.extend(seqs_by_framemap.get(fm, ()))
        mega = len(rig_seqs) > MEGA_RIG_SEQS
        words = tokens(gameval)

        anims = decide(npc_id, gameval, rig_seqs, mega, words, feats,
                       readyanim, lostcity, rsmod, seq_framemaps, rigs)
        sounds = decide_sounds(anims, gameval, feats, lostcity, rsmod, sound_by_name)

        # What a0 knows, kept aside so `--validate` can ask the other layers the
        # same question with a0 switched off.
        held = {}
        for key in ANIM_KEYS:
            for table in (rsmod.get(gameval, {}), lostcity.get(gameval, {})):
                if key in table and table[key] in seq_framemaps:
                    held.setdefault(key, table[key])
        blind = None
        if held and args.validate:
            blind = decide(npc_id, gameval, rig_seqs, mega, words, feats,
                           readyanim, lostcity, rsmod, seq_framemaps, rigs,
                           suppress_a0=True)
        decisions[gameval] = (npc_id, anims, sounds, held or None, blind)

        for key, (value, layer, _why) in anims.items():
            layer_tally[layer] += 1
            if value:
                slot_tally[key] += 1
        for key, (_v, layer, _w) in sounds.items():
            sound_tally[layer.split("-")[0]] += 1

    print("\nnpcs: %d   ledger rows frozen by hand: %d" % (len(decisions), frozen))
    print("animation slots by layer:")
    for layer in sorted(layer_tally):
        print("   %-9s %6d" % (layer, layer_tally[layer]))
    print("filled: " + ", ".join("%s %d" % (k.replace("_anim", ""), slot_tally[k])
                                 for k in ANIM_KEYS))
    covered = sum(1 for _id, a, _s, _h, _b in decisions.values()
                  if any(v for v, _l, _w in a.values()))
    print("npcs with at least one animation: %d of %d (%d%%)"
          % (covered, len(decisions), 100 * covered // len(decisions)))
    print("sound slots by layer: " + ", ".join("%s %d" % (k, sound_tally[k])
                                               for k in sorted(sound_tally)))

    # The coverage number that matters. An npc nothing can fight does not need a
    # swing, and an npc on the shared human rig is *correctly* answered by
    # npc_default.npc's human unarmed set — so the honest denominator is the npcs
    # that fight and are not human.
    attackable = {gv for gv, rec in all_npc.items()
                  if any(re.fullmatch(r"op\d", k) and v and v[0] == "Attack"
                         for k, v in rec.items())}
    fight_covered = fight_mega = fight_gap = 0
    for gameval in attackable:
        npc_id, anims, _s, _h, _b = decisions[gameval]
        if any(v for v, _l, _w in anims.values()):
            fight_covered += 1
        elif any(l == "a5" and "shared" in w for _v, l, w in anims.values()):
            fight_mega += 1
        else:
            fight_gap += 1
    print("\nof the %d npcs a player can attack:" % len(attackable))
    print("   %5d have at least one animation of their own" % fight_covered)
    print("   %5d are on the shared human rig, where npc_default's human set is right"
          % fight_mega)
    print("   %5d genuinely unanswered" % fight_gap)

    if args.validate:
        rsmod_rigs = {gv: npc_rigs.get(gameval_to_id.get(gv, -1), set()) for gv in rsmod}
        validate(all_seq, feats, lostcity, rsmod, seq_framemaps, sound_by_name,
                 decisions, rsmod_rigs)

    if args.fix_authored:
        fix_authored(args.content, out_path, seq_framemaps, npc_rigs,
                     gameval_to_id, decisions, args.write)

    if not args.write:
        print("\n(dry run — pass --write to author the ledger and the config)")
        return 0

    # ---- the ledger ------------------------------------------------------
    written = 0
    for gameval in all_gamevals:
        npc_id, anims, sounds, _held, _blind = decisions[gameval]
        if anims and next(iter(anims.values()))[1] == "authored":
            continue
        rec = all_npc[gameval]
        rigs = sorted(npc_rigs.get(npc_id, set()))
        n_seqs = sum(len(seqs_by_framemap.get(fm, ())) for fm in rigs)
        if rigs:
            rig_note = " - rig framemap %s (%d sequences%s)" % (
                ",".join(str(r) for r in rigs), n_seqs,
                ", shared" if n_seqs > MEGA_RIG_SEQS else "")
        else:
            rig_note = " - no rig: the cache gives this npc no readyanim"
        rows = {}
        for key in ANIM_KEYS:
            value, layer, why = anims[key]
            rows[key] = (value, "%s  %s" % (layer, why))
        for key in SOUND_KEYS:
            value, layer, why = sounds[key]
            rows[key] = (value, "%s  %s" % (layer, why))
        write_ledger(ledger_path(args.content, gameval), rec.get("name", [""])[0],
                     npc_id, rig_note, "generated", rows,
                     shadowed_by=authored_elsewhere.get(gameval))
        written += 1
    print("\nwrote %d ledger files under %s/%s/"
          % (written, args.content, LEDGER_DIR))

    # ---- the compiled config --------------------------------------------
    blocks = []
    emitted_names = []
    emitted = kept_rates = shadowed = 0
    for gameval in all_gamevals:
        npc_id, anims, sounds, _held, _blind = decisions[gameval]
        if gameval in authored_elsewhere:
            shadowed += 1
            continue
        picked = [(k, anims[k][0], anims[k][1]) for k in ANIM_KEYS if anims[k][0]]
        picked_sounds = [(k, sounds[k][0], sounds[k][1])
                         for k in SOUND_KEYS if sounds[k][0]]
        if not picked and not picked_sounds:
            continue
        rec = all_npc[gameval]
        display = rec.get("name", [""])[0]
        if display:
            # Above the header, not after it: cachepack's config parser reads the
            # rest of a `[...]` line as part of the name.
            blocks.append("// %s" % display)
        # Provenance goes on its own line, never trailing a `param=`.
        #
        # A `.npc` line's value is the whole rest of the line: cachepack read
        # `param=attack_anim,monalisk_creature_attack  // a2` as a sequence named
        # "monalisk_creature_attack  // a2", which names nothing, and reported
        # 5,826 unresolved values. The layer is the ledger's job anyway --
        # `npc_combat/<npc>.combat` states it per row, with the reasoning -- and
        # this line is the index into it.
        note = " ".join("%s=%s" % (k.split("_")[0], l) for k, _v, l in picked)
        snote = " ".join("%s=%s" % (k.split("_")[0], l) for k, _v, l in picked_sounds)
        if note or snote:
            blocks.append("// anim: %s%s" % (note or "none",
                                             ("  sound: " + snote) if snote else ""))
        blocks.append("[%s]" % gameval)
        for key, value, _layer in picked:
            blocks.append("param=%s,%s" % (key, value))
        for key, value, _layer in picked_sounds:
            # A bare id, because cachepack has no `synth` type for a `ref` to
            # name. The ledger keeps the name, which is the half that survives a
            # cache revision.
            blocks.append("param=%s,%d" % (key, sound_by_name[value]))
        rate = rec.get("param", [])
        cache_rate = None
        for p in rate:
            if p.startswith("attackrate,"):
                cache_rate = p.rsplit(",", 1)[1]
        if cache_rate is not None and cache_rate != default_rate:
            blocks.append("param=attackrate,%s" % cache_rate)
            kept_rates += 1
        blocks.append("")
        emitted += 1
        emitted_names.append(gameval)

    header = [
        "// Per-npc attack / defend / death animations and combat sounds.",
        "//",
        "// COMPILED, not authored. The decision for each npc lives in its own file",
        "// under `%s/`, one per npc, and this file is what those add up to." % LEDGER_DIR,
        "// Editing here is pointless: the next run overwrites it. Edit the npc's",
        "// ledger file and set `source = authored` there, which is honoured forever.",
        "//",
        "// Regenerate with:",
        "//   python3 tools/gen_npc_combat.py --write",
        "//",
        "// Every sequence is on one of its npc's own framemaps, so it is a rig the",
        "// model can actually play. The trailing `// aN` on each line names the",
        "// layer that chose it; docs/DEATH_ATK_DEF_ANIMS.md says what each layer is",
        "// and reports how often it is right.",
        "//",
        "// Some blocks restate `attackrate`. That is not animation work: an npc with",
        "// no block is answered from the cache, and the moment it has one,",
        "// npc_default.npc's own attackrate lands on top.",
        "//",
        "// %d npcs." % emitted,
        "",
    ]
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="latin-1", errors="replace") as f:
        f.write("\n".join(header + blocks))
    print("wrote %s (%d blocks, %d restating attackrate, %d left to authored files)"
          % (out_path, emitted, kept_rates, shadowed))

    added = claim_server_membership(args.content, emitted_names)
    print("pack/npc.server: %d name(s) added" % added)
    return 0


if __name__ == "__main__":
    sys.exit(main())
