#!/usr/bin/env python3
"""
port_weapon_fx — which weapons still swing with the wrong animation, and
which source states the right one.

    tools/port_weapon_fx.py --report        the review artifact, TSV on stdout
    tools/port_weapon_fx.py --summary       the counts this port is measured by
    tools/port_weapon_fx.py --check         the build bar (exit 1 on a broken row)
    tools/port_weapon_fx.py --sources NAME  the three-source card for one weapon

The problem this exists to size.

`[proc,combat_attack_anim]` reads `oc_param($weapon, <style>attack_anim)`, and
those params carry a `default=` — `human_sword_slash` for slash, `human_unarmedpunch`
for stab and crush. So a weapon with no overlay does not fail: it swings a bronze
longsword. Every post-2004 weapon in this cache is in that state, because the 210
overlays in `skill_combat/configs/bas/` were ported from LostCity and LostCity
stops at September 2004. An abyssal whip plays `human_sword_slash`; so does a
scythe of vitur, a twisted bow and a ghrazi rapier. Nothing reports it, which is
why the number below had never been counted.

Three sources, and they answer different questions. Do not substitute one for
another — §4.1 rule 4 is that ids never cross trees, and two of these three are
id-shaped.

  1. **Cache** (`OSRS-Content/osrs239-content/`) — the authority for *what
     exists here*: which objs are weapons, what `category=` they carry, and
     which seq **names** are spellable. 14,413 seq records, all named. If a name
     does not resolve here, the row cannot be written, and that is the whole of
     the id-safety story: this tool emits names, never numbers.
  2. **Wiki** (`oldschool.runescape.wiki`) — the authority for *functionality*:
     attack speed, which styles a weapon offers, whether it has a special attack
     and what that special does. It is NOT an authority for animation or sound
     ids; it does not state them. Every row carries its article URL so a claim
     about behaviour can be checked against the game rather than against a
     server someone else wrote.
  3. **RuneLite** (`~/Documents/git_repos/runelite`) — the authority for
     *id ↔ gameval name*, via `runelite-api/.../gameval/AnimationID.java`. It is
     read from a current live cache, so it is what turns another server's
     integer into a name this tree can resolve. `rsmod`'s `.data/symbols/seq.sym`
     is the same mapping at higher coverage (12,205 rows) and is used first;
     RuneLite is the independent second opinion on every id the port cites.

The data source for the mapping itself is **rsmod** (`~/Documents/git_repos/rsmod`),
`api/cache-enricher/.../obj/objs.toml` — 4,686 obj rows, 969 of them carrying
`anim_stance1..4` / `sound_stance1..4` / `block_anim` / `equipment_sound`, keyed
by the *same gameval obj names this tree uses*. rsmod is the closest structural
match of any reference: it states weapon FX as **obj params**, which is exactly
where this tree already reads them from.

Why stance and not damage type.

The overlays here are keyed by damage type (`stabattack_anim` / `slashattack_anim`
/ `crushattack_anim`), which is LostCity's shape. rsmod and the live game key by
**stance** — the 1..4 slot of the attack-style button, which this server already
tracks as `%com_mode`. The two agree wherever the style's damage type decides the
swing, which is most of the time, and disagree wherever it does not (unarmed
punch vs kick is the case already special-cased in `combat_stats.rs2`). The
report emits both keyings so the port can land the stance params without
rewriting the damage-type ones.
"""

import argparse
import collections
import os
import re
import signal
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
GITREPOS = os.path.expanduser("~/Documents/git_repos")
RSMOD = os.path.join(GITREPOS, "rsmod")
RUNELITE = os.path.join(GITREPOS, "runelite")

OBJS_TOML = os.path.join(
    RSMOD,
    "api/cache-enricher/src/main/resources/org/rsmod/api/cache/enricher/obj/objs.toml",
)
SEQ_SYM = os.path.join(RSMOD, ".data/symbols/seq.sym")
RL_ANIM = os.path.join(
    RUNELITE, "runelite-api/src/main/java/net/runelite/api/gameval/AnimationID.java"
)

BAS_DIR = os.path.join(TREE, "server/scripts/skill_combat/configs/bas")

WIKI = "https://oldschool.runescape.wiki/w/"

# rsmod objs.toml key -> the param this tree would carry it in. `None` means the
# port has not decided a home yet and the report says so rather than inventing one.
FX_KEYS = {
    "anim_stance1": "attack_anim_stance1",
    "anim_stance2": "attack_anim_stance2",
    "anim_stance3": "attack_anim_stance3",
    "anim_stance4": "attack_anim_stance4",
    "sound_stance1": "attack_sound_stance1",
    "sound_stance2": "attack_sound_stance2",
    "sound_stance3": "attack_sound_stance3",
    "sound_stance4": "attack_sound_stance4",
    "block_anim": "defend_anim",
    "equipment_sound": "equipment_sound",
    "ready_anim": "ready_baseanim",
    "turn_anim": "turnonspot_baseanim",
    "walk_anim": "walk_f_baseanim",
    "walk_anim_back": "walk_b_baseanim",
    "walk_anim_left": "walk_l_baseanim",
    "walk_anim_right": "walk_r_baseanim",
    "run_anim": "running_baseanim",
}
ANIM_KEYS = [k for k in FX_KEYS if k.endswith("anim") or k.startswith("anim_")]
SOUND_KEYS = [k for k in FX_KEYS if "sound" in k]


# ---------------------------------------------------------------- cache


def load_config(path):
    """Parse a cachepack `all.<type>` export into {name: {key: [values]}}."""
    out = {}
    name = None
    cur = None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("[") and line.endswith("]"):
                if name is not None:
                    out[name] = cur
                name = line[1:-1]
                cur = collections.defaultdict(list)
            elif name is not None and "=" in line and not line.startswith("//"):
                key, value = line.split("=", 1)
                cur[key].append(value)
    if name is not None:
        out[name] = cur
    return out


def obj_params(record):
    out = {}
    for entry in record.get("param", []):
        parts = entry.split(",")
        if len(parts) >= 3:
            out[parts[0]] = parts[2]
    return out


def load_seq_names(path):
    names = set()
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if line.startswith("[") and line.rstrip().endswith("]"):
                names.add(line.strip()[1:-1])
    return names


def load_synth_names(path):
    """pack/4_soundeffects.pack is `id=synth_id` — the ids ARE the namespace."""
    ids = set()
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "=" in line:
                head = line.split("=", 1)[0].strip()
                if head.isdigit():
                    ids.add(int(head))
    return ids


def is_combat_weapon(name, record):
    """A weapon this port owns: worn in the right hand, real, and it fights.

    `wearpos=3` alone is 1,964 records and includes sailing cargo crates. The
    combat-param test is what makes the denominator honest — a weapon the cache
    gives no attack bonus and no attack rate is not something a player swings.
    """
    if record.get("wearpos") != ["3"]:
        return False
    if name.startswith(("placeholder", "cert_", "macro_")):
        return False
    if "name" not in record:
        return False
    params = obj_params(record)
    if "attackrate" in params:
        return True
    return any(
        k in params for k in ("stabattack", "slashattack", "crushattack", "rangeattack")
    )


def load_overlays(exclude=None):
    """{obj: {param: value}} for every overlay already in the content tree.

    `exclude` drops one file from the scan, and it is not a convenience: this
    directory is where `--write` puts its output, so a second `--write` would
    otherwise see its own previous run as a pre-existing overlay, find every row
    already covered, and emit an empty file. That is not hypothetical — it is
    what happened on 2026-08-04, and the same aliasing made four `--shard i/4`
    agents silently cover 456 of 667 rows. A generated file must never be an
    input to its own generation.
    """
    out = {}
    if not os.path.isdir(BAS_DIR):
        return out
    skip = os.path.basename(exclude) if exclude else None
    for fname in sorted(os.listdir(BAS_DIR)):
        if not fname.endswith(".obj"):
            continue
        if skip and fname == skip:
            continue
        name = None
        with open(os.path.join(BAS_DIR, fname), encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if line.startswith("[") and line.endswith("]"):
                    name = line[1:-1]
                    out.setdefault(name, {})
                elif name and line.startswith("param="):
                    key, value = line[len("param=") :].split(",", 1)
                    out[name][key] = value
    return out


# ---------------------------------------------------------------- rsmod


def load_objs_toml(path):
    """objs.toml is one flat `[[config]]` array; no nesting, so this is enough."""
    out = {}
    name = None
    cur = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line == "[[config]]":
                if name:
                    out[name] = cur
                name = None
                cur = {}
            elif "=" in line and not line.startswith("#"):
                key, value = [x.strip() for x in line.split("=", 1)]
                value = value.strip("'\"")
                if key == "obj":
                    name = value
                else:
                    cur[key] = value
    if name:
        out[name] = cur
    return out


def load_seq_sym(path):
    out = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 2 and parts[0].isdigit():
                out[int(parts[0])] = parts[1]
    return out


def load_runelite_anims(path):
    """`public static final int NAME = 1658;` -> {1658: 'slayer_abyssal_whip_attack'}."""
    out = {}
    if not os.path.exists(path):
        return out
    pattern = re.compile(r"int\s+([A-Z0-9_]+)\s*=\s*(\d+)\s*;")
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = pattern.search(line)
            if m:
                out.setdefault(int(m.group(2)), m.group(1).lower())
    return out


# ---------------------------------------------------------------- model


class Sources:
    def __init__(self, exclude_overlay=None):
        self.objs = load_config(os.path.join(TREE, "configs/all.obj"))
        self.seq_names = load_seq_names(os.path.join(TREE, "configs/all.seq"))
        self.synth_ids = load_synth_names(
            os.path.join(TREE, "pack/4_soundeffects.pack")
        )
        self.overlays = load_overlays(exclude_overlay)
        self.rsmod = load_objs_toml(OBJS_TOML) if os.path.exists(OBJS_TOML) else {}
        self.seq_sym = load_seq_sym(SEQ_SYM) if os.path.exists(SEQ_SYM) else {}
        self.rl_anims = load_runelite_anims(RL_ANIM)
        self.weapons = {
            n: r for n, r in self.objs.items() if is_combat_weapon(n, r)
        }

    def seq_name_for(self, seq_id):
        """id -> a name this tree can spell, with the two sources cross-checked.

        Returns (name_or_None, provenance). `disagree` is not a soft warning: it
        means the two live caches label the same integer differently, so the id
        has moved and neither name may be the one intended. The caller must not
        write that row.
        """
        seq_id = int(seq_id)
        a = self.seq_sym.get(seq_id)
        b = self.rl_anims.get(seq_id)
        if a and b and a != b:
            return None, "disagree:%s/%s" % (a, b)
        name = a or b
        if name is None:
            return None, "unnamed"
        if name not in self.seq_names:
            return None, "absent:" + name
        return name, "rsmod+runelite" if (a and b) else ("rsmod" if a else "runelite")

    def wiki_url(self, name, record):
        title = (record.get("name") or [name.replace("_", " ")])[0]
        return WIKI + title.replace(" ", "_")

    def row(self, name):
        record = self.weapons[name]
        params = obj_params(record)
        overlay = self.overlays.get(name, {})
        rs = self.rsmod.get(name, {})
        fx = {}
        problems = []
        for key, param in FX_KEYS.items():
            if key not in rs:
                continue
            raw = rs[key]
            if "sound" in key:
                sid = int(raw)
                if sid in self.synth_ids:
                    fx[param] = ("synth_%d" % sid, "cache-index4")
                else:
                    problems.append("%s=%s not in this cache's soundeffects" % (key, raw))
            else:
                seq_name, why = self.seq_name_for(raw)
                if seq_name:
                    fx[param] = (seq_name, why)
                else:
                    problems.append("%s=%s %s" % (key, raw, why))
        has_attack = any(k.startswith("attack_anim_stance") for k in fx)
        return {
            "obj": name,
            "display": (record.get("name") or [""])[0],
            "category": (record.get("category") or ["<none>"])[0],
            "attackrate": params.get("attackrate", ""),
            "weapon_category_rsmod": rs.get("weapon_category", ""),
            "speed_rsmod": rs.get("speed", ""),
            "overlaid": bool(overlay),
            "overlay": overlay,
            "fx": fx,
            "has_source": bool(rs),
            "has_attack_source": has_attack,
            "problems": problems,
            "wiki": self.wiki_url(name, record),
        }


# ---------------------------------------------------------------- commands


def cmd_summary(src):
    rows = [src.row(n) for n in sorted(src.weapons)]
    overlaid = [r for r in rows if r["overlaid"]]
    bare = [r for r in rows if not r["overlaid"]]
    sourced = [r for r in bare if r["has_attack_source"]]
    orphan = [r for r in bare if not r["has_attack_source"]]
    # an orphan can still inherit from a sibling that shares its cache category
    sourced_cats = {r["category"] for r in sourced} | {r["category"] for r in overlaid}
    inherit = [r for r in orphan if r["category"] in sourced_cats]

    print("osrs239 obj records                     %6d" % len(src.objs))
    print("  worn in the right hand and fights     %6d" % len(rows))
    print("  already carry an FX overlay           %6d" % len(overlaid))
    print("  swing the param default today         %6d" % len(bare))
    print()
    print("rsmod objs.toml rows                    %6d" % len(src.rsmod))
    print("  bare weapons it states a swing for    %6d" % len(sourced))
    print("  bare weapons with no row              %6d" % len(orphan))
    print("    of which inherit from a sibling     %6d" % len(inherit))
    print("    of which have no source at all      %6d" % (len(orphan) - len(inherit)))
    print()
    print("cross-check set (overlaid AND in rsmod) %6d" % len(
        [r for r in overlaid if r["has_attack_source"]]
    ))
    print("seq names: osrs239 %d, rsmod seq.sym %d, runelite gameval %d" % (
        len(src.seq_names), len(src.seq_sym), len(src.rl_anims)))
    both = {v for v in src.seq_sym.values()} & src.seq_names
    print("  rsmod names that resolve here         %6d / %d" % (len(both), len(src.seq_sym)))
    problems = [r for r in rows if r["problems"]]
    print()
    print("rows with an unwritable field           %6d" % len(problems))


def cmd_report(src, only_gap):
    cols = [
        "obj", "display", "cache_category", "cache_attackrate", "overlaid",
        "rsmod_category", "rsmod_speed", "source", "fx", "problems", "wiki",
    ]
    print("\t".join(cols))
    for name in sorted(src.weapons):
        r = src.row(name)
        if only_gap and (r["overlaid"] or not r["has_attack_source"]):
            continue
        fx = ";".join("%s=%s" % (k, v[0]) for k, v in sorted(r["fx"].items()))
        print("\t".join([
            r["obj"], r["display"], r["category"], r["attackrate"],
            "yes" if r["overlaid"] else "no",
            r["weapon_category_rsmod"], r["speed_rsmod"],
            "rsmod" if r["has_attack_source"] else "-",
            fx, "|".join(r["problems"]), r["wiki"],
        ]))


def cmd_sources(src, name):
    if name not in src.weapons:
        print("not a combat weapon in this cache: %s" % name, file=sys.stderr)
        return 1
    r = src.row(name)
    record = src.weapons[name]
    print("== %s (%s)" % (r["obj"], r["display"]))
    print()
    print("1. CACHE  OSRS-Content/osrs239-content/configs/all.obj")
    print("     category      %s" % r["category"])
    print("     attackrate    %s" % (r["attackrate"] or "<unstated>"))
    print("     wearpos       3")
    print("     overlay       %s" % (r["overlay"] or "<none — swings the param default>"))
    print()
    print("2. WIKI   %s" % r["wiki"])
    print("     the authority for speed, styles and the special attack;")
    print("     it states no animation or sound ids — do not read one out of it.")
    print()
    print("3. RUNELITE  runelite-api/.../gameval/AnimationID.java")
    if not r["fx"]:
        print("     no rsmod row to cross-check")
    for param, (value, why) in sorted(r["fx"].items()):
        print("     %-26s %-34s [%s]" % (param, value, why))
    for problem in r["problems"]:
        print("     PROBLEM  %s" % problem)
    return 0


def shard_filter(names, shard):
    """`--shard i/n` — a deterministic, stable partition for parallel agents.

    Partitioned on the *sorted position*, not on a hash of the name: two agents
    given `1/8` and `2/8` must produce disjoint sets, and a third run months
    later must produce the same sets again. Sorted position does that and is
    inspectable; a hash is neither.
    """
    index, total = [int(x) for x in shard.split("/")]
    if not 1 <= index <= total:
        raise SystemExit("--shard i/n needs 1 <= i <= n")
    ordered = sorted(names)
    return [n for pos, n in enumerate(ordered) if pos % total == index - 1]


def is_sound_param(param):
    """Sound-valued params, which are blocked on a `synth` pack kind.

    `attack_sound_stance1..4` and `equipment_sound`. They are declared `type=int`
    because `fields/param.ini` has no `synth` type, but this writer emits their
    values as *names* (`synth_2498`) — and `mock230_content.c`'s param-type map
    has no `synth` kind, so `int` treats the operand as a literal and the pack
    validator rejects every one of them. Until that namespace exists, the two
    families have to be written separately. See slice 3's blocker note.
    """
    return "sound" in param


def cmd_write(src, path, shard, families="all"):
    """Emit the rank-1 `.obj` overlay for the rows rsmod states and we do not.

    Regenerated, never hand-edited: running this twice must produce an identical
    file (`exporter-owns-generated-configs`). Hand corrections belong in a
    separate file at the same rank.

    `families` splits the emission because the two halves have different
    blockers, not because it is tidier: `anims` (attack/defend/BAS) validates
    today, `sounds` cannot until a `synth` pack kind exists. Default stays `all`
    so the documented command keeps its meaning.
    """
    if families not in ("all", "anims", "sounds"):
        raise SystemExit("--families must be one of: all, anims, sounds")
    names = [n for n in sorted(src.weapons)
             if not src.row(n)["overlaid"] and src.row(n)["has_attack_source"]]
    if shard:
        names = shard_filter(names, shard)
    lines = [
        "// Weapon attack/block/sound overlays for records LostCity never had.",
        "//",
        "// GENERATED by tools/port_weapon_fx.py --write — do not hand-edit; the",
        "// next regeneration will overwrite it. Hand corrections go in a separate",
        "// file at the same rank (docs/WEAPON_FX_PORT_QUEUE.md slice 3).",
        "//",
        "// Source: rsmod api/cache-enricher/.../obj/objs.toml, translated id -> name",
        "// through rsmod .data/symbols/seq.sym and cross-checked against RuneLite",
        "// runelite-api/.../gameval/AnimationID.java. Every name below resolves in",
        "// this tree's configs/all.seq; the writer refuses any that does not.",
        "//",
        "// Sounds are `synth_<id>`, which is what pack/4_soundeffects.pack calls",
        "// them — the id IS the name for that namespace.",
        "",
    ]
    if families == "anims":
        lines.insert(len(lines) - 1, "// FAMILIES: anims only — attack/defend/BAS. The sound params are held")
        lines.insert(len(lines) - 1, "// back because they are unrepresentable today (no `synth` pack kind);")
        lines.insert(len(lines) - 1, "// regenerate them with --families sounds once that lands.")
    elif families == "sounds":
        lines.insert(len(lines) - 1, "// FAMILIES: sounds only — attack_sound_stance*/equipment_sound. Needs a")
        lines.insert(len(lines) - 1, "// `synth` pack kind in mock230_content.c or every row fails to resolve.")

    def wanted(param):
        if families == "anims":
            return not is_sound_param(param)
        if families == "sounds":
            return is_sound_param(param)
        return True

    written = 0
    for name in names:
        r = src.row(name)
        if not r["fx"]:
            continue
        emit = [(p, v) for p, (v, _why) in sorted(r["fx"].items()) if wanted(p)]
        if not emit:
            continue
        lines.append("[%s]" % name)
        for param, value in emit:
            lines.append("param=%s,%s" % (param, value))
        lines.append("")
        written += 1
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    print("wrote %d records to %s" % (written, path))
    return 0


def cmd_diff(src):
    """Slice 2: every weapon that carries BOTH a LostCity overlay and an rsmod row.

    The port's validation set. A disagreement here is a finding, not a merge —
    "the 2004 animation" may be the intended answer for this project, and this
    tool has no standing to decide that.
    """
    # The two keyings do not correspond row for row — which stance carries a
    # weapon's slash depends on the weapon (a dagger slashes on its *controlled*
    # button, a longsword on its accurate one), so asserting a specific pairing
    # would manufacture disagreements out of the keying itself. The honest test
    # is set membership: does the animation this tree names for the weapon appear
    # anywhere in the set rsmod uses for that same weapon?
    stances = ["attack_anim_stance%d" % i for i in (1, 2, 3, 4)]
    pairs = [
        ("slashattack_anim", stances),
        ("stabattack_anim", stances),
        ("crushattack_anim", stances),
        ("rangeattack_anim", stances),
        ("defend_anim", ["defend_anim"]),
    ]
    print("\t".join(["obj", "param", "tree", "rsmod_candidates", "verdict", "wiki"]))
    for name in sorted(src.weapons):
        r = src.row(name)
        if not (r["overlaid"] and r["has_attack_source"]):
            continue
        for tree_param, stance_params in pairs:
            if tree_param not in r["overlay"]:
                continue
            tree_value = r["overlay"][tree_param]
            cands = []
            for sp in stance_params:
                if sp in r["fx"]:
                    cands.append(r["fx"][sp][0])
            if not cands:
                continue
            verdict = "agree" if tree_value in cands else "REVIEW"
            print("\t".join([name, tree_param, tree_value, ",".join(dict.fromkeys(cands)),
                             verdict, r["wiki"]]))
    return 0


def cmd_specials(src, shard):
    """Slice 10/12: the special-attack worklist, keyed off the wiki's list.

    The wiki is the authority for *which weapons have a spec and what it costs*;
    this only resolves those display names onto osrs239 objs and reports which
    already carry a `specwep` overlay. It states no energy costs of its own —
    read them off the article, per row.
    """
    existing = {n for n, p in src.overlays.items() if "specwep" in p}
    special_obj = os.path.join(
        TREE, "server/scripts/skill_combat/configs/special_attack.obj")
    if os.path.exists(special_obj):
        with open(special_obj, encoding="utf-8") as fh:
            for line in fh:
                s = line.strip()
                if s.startswith("[") and s.endswith("]"):
                    existing.add(s[1:-1])
    by_display = {}
    for name, record in src.weapons.items():
        display = (record.get("name") or [""])[0].lower()
        if display:
            by_display.setdefault(display, name)
    names = sorted(src.weapons)
    if shard:
        names = shard_filter(names, shard)
    print("\t".join(["obj", "display", "already_declared", "wiki"]))
    for name in names:
        record = src.weapons[name]
        print("\t".join([
            name, (record.get("name") or [""])[0],
            "yes" if name in existing else "no",
            src.wiki_url(name, record),
        ]))
    return 0


def cmd_check(src):
    """The build bar. Loud on anything that cannot be written as a name."""
    bad = 0
    for name in sorted(src.weapons):
        r = src.row(name)
        for problem in r["problems"]:
            print("%s: %s" % (name, problem))
            bad += 1
    missing = [p for p in (OBJS_TOML, SEQ_SYM, RL_ANIM) if not os.path.exists(p)]
    for path in missing:
        print("source unavailable: %s" % path)
        bad += 1
    print("%d problem(s)" % bad)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", action="store_true", help="TSV of every combat weapon")
    ap.add_argument("--gap", action="store_true", help="with --report, only the portable gap")
    ap.add_argument("--summary", action="store_true", help="the counts")
    ap.add_argument("--check", action="store_true", help="the build bar")
    ap.add_argument("--sources", metavar="OBJ", help="the three-source card for one weapon")
    ap.add_argument("--write", metavar="PATH", help="emit the rank-1 .obj overlay for the gap")
    ap.add_argument("--diff", action="store_true", help="cross-check overlaid weapons vs rsmod")
    ap.add_argument("--specials", action="store_true", help="the special-attack worklist")
    ap.add_argument("--shard", metavar="i/n", help="take a deterministic 1-of-n partition")
    ap.add_argument("--families", metavar="WHICH", default="all",
                    choices=("all", "anims", "sounds"),
                    help="which param families --write emits: all (default), "
                         "anims (attack/defend/BAS), sounds (needs a synth pack kind)")
    args = ap.parse_args()

    # `--report | head` is the normal way to read this; a closed pipe is not an
    # error and must not print a traceback that looks like one.
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)

    # A `--write` run must not read its own target as a pre-existing overlay, or
    # the second run emits nothing and the first run's coverage is silently lost.
    src = Sources(exclude_overlay=args.write if args.write else None)
    if args.sources:
        return cmd_sources(src, args.sources)
    if args.write:
        return cmd_write(src, args.write, args.shard, args.families)
    if args.diff:
        return cmd_diff(src)
    if args.specials:
        return cmd_specials(src, args.shard)
    if args.check:
        return cmd_check(src)
    if args.report:
        cmd_report(src, args.gap)
        return 0
    cmd_summary(src)
    return 0


if __name__ == "__main__":
    sys.exit(main())
