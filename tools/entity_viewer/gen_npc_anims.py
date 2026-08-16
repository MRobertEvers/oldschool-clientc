#!/usr/bin/env python3
"""
Give npcs their own attack / defend / death animations, from the rig catalog.

Every npc in this tree is seeded from `[default]` in
`server/scripts/general/configs/npc_default.npc`, which hands out the *human
unarmed* set — `human_unarmedpunch`, `human_unarmedblock`, `human_death`. That is
the right fallback (it is the one animation family not tied to a creature's
skeleton) and it is wrong for every non-human npc: a dragon dies by folding up
like a man.

The catalog says which sequences an npc *can* play. This picks the three the
engine asks for, and it only picks them when two independent things agree:

  1. **The rig.** The sequence must be built on one of the npc's own framemaps.
     This is the hard constraint — a sequence on another rig addresses bones the
     model does not have and would move the wrong vertices. It is also what
     makes a wrong pick impossible to be *catastrophic*: the worst case is the
     wrong animation from the right creature.

  2. **The name.** The sequence's gameval name must share a distinctive word
     with the npc's. Without this, every human npc would take an arbitrary
     animation off framemap 0's 3,905 sequences, which is exactly the
     "guess a name from the display name" mistake npc_default.npc's header
     records.

Only then is the sequence classified by its action word (`_death`, `_attack`,
`_defend`/`_block`) and written out. An npc where either test fails keeps the
default, which is the honest outcome: the catalog does not know its animations.

Usage:
  python3 gen_npc_anims.py --catalog out/osrs239_anims \\
      --content OSRS-Content/osrs239-content [--write] [--limit N]

Without `--write` it reports what it would do and changes nothing.
"""

import argparse
import csv
import os
import re
import sys
from collections import defaultdict

# Words that name an action rather than a creature. Shared with ev_catalog's
# tokeniser for the same reason: they are what two unrelated names have in
# common, so matching on them matches everything.
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


def tokens(name):
    """Distinctive words in a gameval name: letters only, four or more, not an
    action word."""
    if not name:
        return set()
    out = set()
    for word in re.split(r"[^a-zA-Z]+", name.lower()):
        if len(word) >= 4 and word not in ACTION_WORDS:
            out.add(word)
    return out


# ---- what the catalog says --------------------------------------------------


def load_catalog(catalog_dir):
    seqs_by_framemap = defaultdict(list)  # framemap -> [(seq_id, name, frames)]
    with open(os.path.join(catalog_dir, "framemap_seqs.csv"), encoding="utf-8",
              errors="replace") as f:
        for row in csv.DictReader(f):
            if row["seq_name"]:
                seqs_by_framemap[int(row["framemap_id"])].append(
                    (int(row["seq_id"]), row["seq_name"], int(row["frame_count"])))

    npc_rigs = defaultdict(list)  # npc id -> [framemap]
    npc_gameval = {}
    with open(os.path.join(catalog_dir, "npc_rigs.csv"), encoding="utf-8",
              errors="replace") as f:
        for row in csv.DictReader(f):
            npc_id = int(row["npc_id"])
            npc_rigs[npc_id].append(int(row["framemap_id"]))
            if row["gameval"]:
                npc_gameval[npc_id] = row["gameval"]

    return seqs_by_framemap, npc_rigs, npc_gameval


# ---- what the content tree already says -------------------------------------


def load_authored_blocks(content_dir, exclude_path=None):
    """Every npc that already has a `.npc` block.

    A second block for the same npc does not error — `load_npc_config` appends a
    *second def* with the same id — so a generated file that restates an
    authored npc would silently shadow a hand-checked port. These are skipped.

    `exclude_path` is this generator's own output. Without it a second run reads
    the first run's blocks as authored and writes a file missing exactly the npcs
    it had just configured — the count went 3,185 -> 2,941 before this argument
    existed, and a third run would have emptied it.
    """
    authored = set()
    root = os.path.join(content_dir, "server", "scripts")
    exclude = os.path.abspath(exclude_path) if exclude_path else None
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".npc"):
                continue
            full = os.path.join(dirpath, fn)
            if exclude and os.path.abspath(full) == exclude:
                continue
            with open(full, encoding="utf-8",
                      errors="replace") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("[") and line.endswith("]"):
                        name = line[1:-1]
                        if name != "default":
                            authored.add(name)
    return authored


def load_cache_attackrate(content_dir):
    """Each npc's `attackrate` as the cache states it.

    Needed because giving an npc a block is not a neutral act. An npc with no
    block is answered straight from the cache; the moment one exists,
    `npc_def_seed_from_cache` seeds it and `[default]` overlays its own
    `attackrate=4` on top. So adding an animation to `zombie_armed3` would also
    have quietly changed its attack speed from 5 to 4 — 503 npcs here, and the
    band verification caught two of them as "10 in the band but 4 from the text
    overlays" before this existed.

    Restating the cache's value in the block keeps the field where it was.
    """
    rates = {}
    current = None
    path = os.path.join(content_dir, "configs", "all.npc")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
            elif current and line.startswith("param=attackrate,"):
                # The cache export is typed — `param=attackrate,int,10`; an
                # authored block is not — `param=attackrate,10`.
                rates[current] = line.rsplit(",", 1)[1]
    return rates


def load_default_attackrate(content_dir):
    """`[default] param=attackrate` from npc_default.npc, read rather than
    assumed: it is the value a block would impose."""
    path = os.path.join(content_dir, "server", "scripts", "general", "configs",
                        "npc_default.npc")
    in_default = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                in_default = line == "[default]"
            elif in_default and line.startswith("param=attackrate,"):
                return line.rsplit(",", 1)[1]
    return None


def load_attackable(content_dir):
    """Npcs the player can attack, and their display names, from all.npc.

    `op2=Attack` is what the cache record says about being a combat target. Used
    only to report the split — a death animation is worth setting either way,
    since anything can be killed by a script.
    """
    attackable = set()
    display = {}
    current = None
    path = os.path.join(content_dir, "configs", "all.npc")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
            elif current and line.startswith("name="):
                display[current] = line[5:]
            elif current and re.fullmatch(r"op\d=Attack", line):
                attackable.add(current)
    return attackable, display


def claim_server_membership(content_dir, names):
    """Name every configured npc in `pack/npc.server`, and return how many were
    new.

    That file is the tree's statement of which npcs have a *server half*, and
    `cachepack pack` routes on it: a record gets a server band only if this file
    names it, and stating a server field without being named is a hard error —
    "an entity cannot carry a field its own side does not receive". So giving an
    npc `attack_anim` and leaving this file alone does not half-work, it fails
    the pack.

    `content.ini` declares this namespace `membership = authored`, meaning a
    person decides and nothing regenerates the file. This does not regenerate it:
    it merges, never removing a name and never rewriting the header, so what a
    person put there stays. Adding a name is the true statement that an npc the
    tree now configures has a server half.
    """
    path = os.path.join(content_dir, "pack", "npc.server")
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()

    listed = {l.strip() for l in lines
              if l.strip() and not l.strip().startswith("//")}
    new = sorted(set(names) - listed)
    if not new:
        return 0

    # Insert in sorted position rather than rebuilding the file.
    #
    # Rebuilding looked simpler and was wrong: the roster carries an eight-line
    # comment block *mid-file*, at the point where a `default` entry used to sit,
    # explaining why it was removed. Splitting on "comments first, names after"
    # swept those lines into the sorted body and scattered them. Walking the
    # original lines leaves every comment exactly where its author put it.
    out = []
    pending = list(new)
    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            while pending and pending[0] < stripped:
                out.append(pending.pop(0))
        out.append(line)
    out.extend(pending)

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    return len(new)


# ---- the pick ---------------------------------------------------------------

SLOTS = (
    # (param name, what a matching sequence name contains, preferred exact tail)
    ("death_anim", ("death", "dying"), "death"),
    ("attack_anim", ("attack",), "attack"),
    ("defend_anim", ("defend", "block"), "defend"),
)


# The npc's name must account for a real share of its own rig.
#
# This is what separates a creature word from a namespace word, and no measure of
# the word itself does it. `slayer` names 94 sequences and `gnome` 68, so
# frequency cannot tell them apart — but on the rig each npc actually uses,
# `gnome` accounts for 40 of 98 sequences while `slayer` accounts for about 1% of
# framemap 0's 3,905. The first is a creature that owns its rig; the second is a
# skill namespace sprinkled across the shared human rig.
#
# Concretely this is what stopped `slayer_nechryael_spawn` (Death spawn, human
# rig, and the cache holds no nechryael animation at all) taking
# `slayer_abyssal_whip_attack` — the player's whip swing — as its attack.
MIN_FAMILY_SHARE = 0.10
MIN_FAMILY_SIZE = 3

# Which *variant* of an action to take, when a creature has several.
#
# `skeleton_armed` can reach `skeleton_update_attack_weapon`,
# `..._attack_sword`, `..._giant_attack`, `..._champion_attack` and
# `..._gorilla_attack`. All five are on its rig and all five are skeletons, so
# neither the rig nor the name family separates them — this is the one place
# where the choice is a judgement rather than a derivation, and it is made here
# in the open.
#
# The npc's own name usually carries the hint: an `_armed` skeleton wields
# something, an `_unarmed` zombie does not. Where it carries none, the unarmed
# form is preferred for exactly the reason npc_default.npc gives for using the
# human unarmed set as the global fallback — it is the form that assumes least.
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


def pick(candidates, words):
    """Choose one sequence per slot from an npc's own rig.

    `candidates` are (seq_id, name, frames) already restricted to the npc's
    framemaps. Selection prefers the plainest name — `zuk_death` over
    `zuk_death_hardmode` — because a creature with several variants of an action
    almost always names the default one with the bare word, and picking the
    longest would systematically choose special cases.
    """
    family = [c for c in candidates if tokens(c[1]) & words]
    if len(family) < MIN_FAMILY_SIZE:
        return {}, []
    if len(family) / float(len(candidates)) < MIN_FAMILY_SHARE:
        return {}, []
    wanted = set()
    for word in words:
        wanted.update(WIELD_HINTS.get(word, ()))

    chosen = {}
    for param, needles, exact in SLOTS:
        hits = [c for c in family
                if any(n in c[1].lower() for n in needles)]
        if not hits:
            continue

        def rank(c):
            name = c[1].lower()
            parts = [p for p in re.split(r"[^a-z]+", name) if p]
            tail = parts[-1] if parts else ""
            return (
                # 1. the variant the npc's own name asks for
                0 if (wanted and any(w in parts for w in wanted)) else 1,
                # 2. failing a hint, the unarmed form assumes least
                0 if (not wanted and "unarmed" in parts) else 1,
                # 3. the plain form names the action last: `gnome_death`
                0 if tail == exact else 1,
                # 4. then fewest words, then shortest, then lowest id, so the
                #    same inputs always give the same answer
                len(parts),
                len(name),
                c[0],
            )

        hits.sort(key=rank)
        chosen[param] = hits[0]
    return chosen, family


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--catalog", required=True)
    ap.add_argument("--content", required=True)
    ap.add_argument("--out", default=None,
                    help="file to write (default: <content>/server/scripts/"
                         "npc/configs/npc_anims.generated.npc)")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--limit", type=int, default=0,
                    help="report only: show this many examples")
    args = ap.parse_args()

    out_path = args.out or os.path.join(
        args.content, "server", "scripts", "npc", "configs",
        "npc_anims.generated.npc")

    seqs_by_framemap, npc_rigs, npc_gameval = load_catalog(args.catalog)
    authored = load_authored_blocks(args.content, exclude_path=out_path)
    cache_rate = load_cache_attackrate(args.content)
    default_rate = load_default_attackrate(args.content)
    attackable, display = load_attackable(args.content)

    picked = {}
    skipped = defaultdict(int)

    for npc_id, rigs in sorted(npc_rigs.items()):
        gameval = npc_gameval.get(npc_id)
        if not gameval:
            skipped["no gameval name"] += 1
            continue
        if gameval in authored:
            skipped["already has an authored block"] += 1
            continue

        words = tokens(gameval)
        if not words:
            skipped["name has no distinctive word"] += 1
            continue

        candidates = []
        for fm in set(rigs):
            candidates.extend(seqs_by_framemap.get(fm, ()))
        if not candidates:
            skipped["rig has no named sequences"] += 1
            continue

        chosen, family = pick(candidates, words)
        if not family:
            skipped["its name is not what its rig is for"] += 1
            continue
        if not chosen:
            skipped["its own sequences name no action"] += 1
            continue

        picked[npc_id] = (gameval, chosen)

    total = len(npc_rigs)
    print(f"npcs with a rig: {total}")
    print(f"npcs given animations: {len(picked)}")
    for reason, n in sorted(skipped.items(), key=lambda kv: -kv[1]):
        print(f"  skipped {n:6d}  {reason}")

    per_slot = defaultdict(int)
    for _gv, chosen in picked.values():
        for k in chosen:
            per_slot[k] += 1
    print("  slots filled:", dict(per_slot))
    combat = sum(1 for i in picked if npc_gameval[i] in attackable)
    print(f"  of those, {combat} carry an Attack option")

    if args.limit:
        print("\nexamples:")
        for npc_id, (gv, chosen) in list(picked.items())[:args.limit]:
            name = display.get(gv, "")
            print(f"  npc {npc_id} [{gv}] {name}")
            for param, (seq, sname, frames) in sorted(chosen.items()):
                print(f"      {param:12s} = {sname}  (seq {seq}, {frames} frames)")

    if not args.write:
        print("\n(dry run — pass --write to author the config)")
        return 0

    added = claim_server_membership(args.content, [gv for gv, _ in picked.values()])
    print(f"  pack/npc.server: {added} name(s) added")

    lines = [
        "// Per-npc attack / defend / death animations, generated.",
        "//",
        "// Regenerate with:",
        "//   python3 tools/entity_viewer/gen_npc_anims.py \\",
        "//       --catalog out/osrs239_anims \\",
        "//       --content OSRS-Content/osrs239-content --write",
        "//",
        "// Do not hand-edit: the next run overwrites the file. An npc that needs",
        "// something other than what the rig walk found belongs in an authored",
        "// .npc alongside the other ports — this generator skips every npc that",
        "// already has a block anywhere in server/scripts, so an authored block",
        "// always wins.",
        "//",
        "// Every sequence below is on one of its npc's own framemaps, so it is a",
        "// rig the model can actually play, and shares a distinctive word with the",
        "// npc's gameval name, so it belongs to that creature rather than merely",
        "// fitting its skeleton. Both tests have to pass; npcs where either fails",
        "// keep npc_default.npc's human unarmed set, which is the honest answer",
        "// when the cache does not say.",
        "//",
        "// Some blocks also restate `attackrate`. That is not animation work:",
        "// an npc with no block is answered from the cache, and the moment it has",
        "// one, npc_default.npc's `attackrate=4` lands on top. Restating the",
        "// cache's own value keeps a field this change has no business moving.",
        "//",
        f"// {len(picked)} npcs, from {total} with a rig.",
        "",
    ]
    kept_rates = 0
    for npc_id, (gv, chosen) in sorted(picked.items(), key=lambda kv: kv[1][0]):
        name = display.get(gv, "")
        # The display name goes above the header, not after it: cachepack's
        # config parser reads the rest of a `[...]` line as part of the name and
        # reports "missing closing bracket".
        if name:
            lines.append(f"// {name}")
        lines.append(f"[{gv}]")
        for param, _needles, _exact in SLOTS:
            if param in chosen:
                lines.append(f"param={param},{chosen[param][1]}")
        rate = cache_rate.get(gv)
        if rate is not None and rate != default_rate:
            lines.append(f"param=attackrate,{rate}")
            kept_rates += 1
        lines.append("")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"\nwrote {out_path} ({len(picked)} blocks, "
          f"{kept_rates} restating the cache's attackrate)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
