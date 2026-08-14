#!/usr/bin/env python3
"""
gen_npc_stats — join the spawned roster to the wiki corpus, write one ledger
per npc, and compile the ledgers into content the server reads.

    tools/wiki_npc_roster.py --write   tools/wiki_fetch.py --fetch   # once
    tools/gen_npc_stats.py --validate                   # join + checks, writes nothing
    tools/gen_npc_stats.py --write                       # ledger + compiled config

See docs/NPC_WIKI_STATS_PLAN.md §4–§6. Two layers, the same shape
`tools/gen_npc_combat.py` uses and for the reasons its header states:

  1. **The ledger** (`npc_stats/<shard>/<gameval>.stats`) is the decision, one
     file per attackable spawned npc. `source = authored` freezes a file:
     later runs read it back and leave it exactly as it stands.
  2. **The config** (`npc/configs/npc_stats.generated.npc`) is compiled from
     the ledger. An npc already carrying a hand-authored `hitpoints=` block
     elsewhere in the tree is skipped — an authored block always wins, and a
     second definition for the same npc does not error, it silently stacks.

## The join, concretely

`npc_roster.csv` names an id and a display name. The wiki corpus
(`wiki/monsters/*.wikitext`, `wiki/manifest.tsv`, `wiki/disambig.tsv`) is
searched for a version block whose own `id`/`idN` list contains that id —
never by name or by level, because a name match can bind the wrong variant
and the id list is the one thing the wiki states unambiguously per creature.
See `wiki_infobox.py` for the parse and `wiki_fetch.py` for how a
disambiguation page (`Cave goblin`, `Warrior`, ...) gets a second, verified
pass over its candidate links.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wiki_fetch  # noqa: E402
import wiki_infobox as wi  # noqa: E402
import wiki_droptable as wd  # noqa: E402
from gen_npc_combat import claim_server_membership  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
ROSTER = os.path.join(CONTENT, "wiki", "npc_roster.csv")
LEDGER_DIR = os.path.join(CONTENT, "npc_stats")
NPC_CONFIGS_DIR = os.path.join(CONTENT, "server", "scripts", "npc", "configs")
# Named `combat_stats`, not `npc_stats`, so it sorts alphabetically ahead of
# `npc_anims.generated.npc` in the same directory: `walk_configs`
# (mock230_content.c) now visits `.npc` files in sorted order and
# `mock230_content_npc()` resolves an id to the *first* matching `[gameval]`
# block it finds, so of the two generated files this one has to load first —
# it is the one `load_npc_anims_extra_lines` has already made a complete
# superset of the other for every npc both cover.
CONFIG_OUT = os.path.join(NPC_CONFIGS_DIR, "combat_stats.generated.npc")
NPC_ANIMS_PATH = os.path.join(NPC_CONFIGS_DIR, "npc_anims.generated.npc")
AI_OUT = os.path.join(CONTENT, "server", "scripts", "npc", "scripts", "npc_stats_attackstyle.generated.rs2")

# Physical styles carry ONE offensive bonus in the infobox (`attbns`), not one
# per style the way a player's weapon does, so exactly one of these three
# receives it — whichever the npc's primary `attack style` names. Magic and
# Ranged have their own dedicated bonus fields (`amagic`/`mbns`, `arange`/
# `rngbns`) and are always emitted regardless of primary style, because the
# infobox states them for every monster whether or not they are ever used.
STYLE_MAP = {
    "stab": ("physical", "stabattack", 0),
    "slash": ("physical", "slashattack", 1),
    "crush": ("physical", "crushattack", 2),
    "melee": ("physical", None, 2),  # unspecified physical style; crush is the engine default too
    "magic": ("magic", None, 4),
    "ranged": ("ranged", None, 3),
    "range": ("ranged", None, 3),
}

# An aggressive npc with no `huntrange` never starts a fight (npc_combat.param:
# "0 means it never starts one") and the wiki has no field for this — it is
# server design, not a monster fact. 5 matches the hand-authored blocks this
# tree already carries (dark_wizard, chaos_druid, goblin's own `huntrange=4`
# is the outlier). Stated here once rather than silently defaulted, and noted
# in every ledger row it applies to.
DEFAULT_AGGRO_HUNTRANGE = 5

FIRST_CLASS_KEYS = ["hitpoints", "attack", "strength", "defence", "magic", "ranged", "respawnrate"]
PARAM_KEYS = [
    "attackrate", "damagetype",
    "stabattack", "slashattack", "crushattack",
    "strengthbonus", "magicattack", "rangeattack", "rangebonus",
    "stabdefence", "slashdefence", "crushdefence", "magicdefence", "rangedefence",
    "magic_maxhit", "undead", "huntrange",
    "death_drop",
]
ALL_KEYS = FIRST_CLASS_KEYS + ["huntmode"] + PARAM_KEYS


# ---------------------------------------------------------------------------
# Roster + wiki corpus loading
# ---------------------------------------------------------------------------


def load_roster() -> list[dict]:
    with open(ROSTER, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def load_manifest() -> dict[str, dict]:
    with open(wiki_fetch.MANIFEST, newline="", encoding="utf-8") as f:
        return {row["title"]: row for row in csv.DictReader(f, delimiter="\t")}


def load_disambig() -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    if not os.path.exists(wiki_fetch.DISAMBIG_TSV):
        return out
    with open(wiki_fetch.DISAMBIG_TSV, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            out.setdefault(row["roster_name"], []).append(row["candidate_title"])
    return out


class WikiCorpus:
    """Cached page -> parsed version blocks, plus the name -> title indexes
    `wiki_fetch.py` built. One instance serves the whole roster."""

    def __init__(self):
        self.manifest = load_manifest()
        self.disambig = load_disambig()
        self._parsed_cache: dict[str, list[dict]] = {}
        self._title_for_name: dict[str, str] = {}
        for row in self.manifest.values():
            self._title_for_name.setdefault(row["requested_as"], row["title"])
            self._title_for_name.setdefault(row["title"], row["title"])

    def _versions(self, title: str) -> list[dict]:
        if title not in self._parsed_cache:
            path = os.path.join(wi.MONSTERS_DIR, wiki_fetch.safe_filename(title))
            if os.path.exists(path):
                self._parsed_cache[title] = wi.parse_page(open(path, encoding="utf-8").read())
            else:
                self._parsed_cache[title] = []
        return self._parsed_cache[title]

    def resolve(self, npc_id: int, display_name: str) -> dict:
        """Find the version block whose id list contains `npc_id`. Returns a
        dict describing the outcome: `status` is one of
        "resolved" / "no_wiki_page" / "not_in_any_candidate" / "ambiguous",
        plus `version`/`title` when resolved, and `candidates` for a report."""
        title = self._title_for_name.get(display_name)
        if title is None:
            return {"status": "no_wiki_page", "candidates": []}

        direct_versions = self._versions(title)
        if direct_versions:
            hits = [(title, v) for v in direct_versions if npc_id in v["ids"]]
            if len(hits) == 1:
                t, v = hits[0]
                return {"status": "resolved", "title": t, "version": v, "candidates": [t]}
            if len(hits) > 1:
                return {"status": "ambiguous", "candidates": [title]}
            # A real monster page that simply doesn't list this id — the page
            # is stale relative to this cache, or this variant was added later.
            return {"status": "not_in_any_candidate", "candidates": [title]}

        # No Infobox Monster on the direct page: a disambiguation page. Try
        # every candidate `wiki_fetch.py` fetched for this exact name.
        candidates = self.disambig.get(display_name, [])
        hits = []
        for cand in candidates:
            for v in self._versions(cand):
                if npc_id in v["ids"]:
                    hits.append((cand, v))
        if len(hits) == 1:
            t, v = hits[0]
            return {"status": "resolved", "title": t, "version": v, "candidates": candidates}
        if len(hits) > 1:
            return {"status": "ambiguous", "candidates": [t for t, _ in hits]}
        return {"status": "not_in_any_candidate", "candidates": candidates}


# ---------------------------------------------------------------------------
# Field extraction from a resolved version block
# ---------------------------------------------------------------------------


def classify_style(styles: list[str]) -> tuple[str, str | None, int, str | None]:
    """Returns (kind, melee_bonus_param, damagetype, note)."""
    for raw in styles:
        key = raw.strip().lower()
        if key in STYLE_MAP:
            kind, param, dtype = STYLE_MAP[key]
            note = None
            if len(styles) > 1:
                note = "hybrid attacker (wiki also lists: %s); only the primary style is wired" % (
                    ", ".join(s for s in styles if s.strip().lower() != key)
                )
            return kind, param, dtype, note
    return "physical", "crushattack", 2, "no recognized attack style (%r); defaulted to crush" % styles


def extract_fields(fields: dict[str, str], cache_combat_level: int, cache_size: int) -> dict:
    out: dict = {"raw_notes": []}

    out["hitpoints"] = wi.get_int(fields, "hitpoints")
    out["attack"] = wi.get_int(fields, "att")
    out["strength"] = wi.get_int(fields, "str")
    out["defence"] = wi.get_int(fields, "def")
    out["magic"] = wi.get_int(fields, "mage")
    out["ranged"] = wi.get_int(fields, "range")
    out["respawnrate"] = wi.get_int(fields, "respawn")
    out["attackrate"] = wi.get_int(fields, "attack speed")

    styles = wi.get_list(fields, "attack style")
    kind, melee_param, dtype, style_note = classify_style(styles)
    out["style_kind"] = kind
    out["damagetype"] = dtype
    if style_note:
        out["raw_notes"].append(style_note)

    attbns = wi.get_int(fields, "attbns")
    if melee_param and attbns is not None:
        out[melee_param] = attbns
    out["strengthbonus"] = wi.get_int(fields, "strbns")
    out["magicattack"] = wi.get_int(fields, "amagic")
    out["rangeattack"] = wi.get_int(fields, "arange")
    out["rangebonus"] = wi.get_int(fields, "rngbns")
    out["stabdefence"] = wi.get_int(fields, "dstab")
    out["slashdefence"] = wi.get_int(fields, "dslash")
    out["crushdefence"] = wi.get_int(fields, "dcrush")
    out["magicdefence"] = wi.get_int(fields, "dmagic")
    # The wiki split ranged defence by ammo weight (dlight/dstandard/dheavy)
    # partway through osrs239's era; older page revisions still carry the
    # single `drange`. This engine has one rangedefence param, so standard
    # ammo — the common case — is the fallback chain's preferred rung.
    rangedef = wi.get_int(fields, "dstandard")
    if rangedef is None:
        rangedef = wi.get_int(fields, "drange")
    if rangedef is None:
        rangedef = wi.get_int(fields, "dlight")
    out["rangedefence"] = rangedef

    max_hit = wi.get_int(fields, "max hit")
    out["wiki_max_hit"] = max_hit
    if kind == "magic" and max_hit is not None:
        out["magic_maxhit"] = max_hit

    attributes = [a.lower() for a in wi.get_list(fields, "attributes")]
    if "undead" in attributes:
        out["undead"] = 1
    unmapped_attrs = [a for a in wi.get_list(fields, "attributes") if a.lower() != "undead"]
    if unmapped_attrs:
        out["raw_notes"].append("attributes with no server field yet: %s" % ", ".join(unmapped_attrs))

    aggressive_raw = wi.get_str(fields, "aggressive")
    out["aggressive"] = wi.get_bool(fields, "aggressive")
    if out["aggressive"]:
        out["huntmode"] = "aggressive"
        out["huntrange"] = DEFAULT_AGGRO_HUNTRANGE
        if aggressive_raw.strip().lower() != "yes":
            out["raw_notes"].append("wiki aggressive=%r (conditional; treated as always-aggressive)" % aggressive_raw)

    poisonous = wi.get_bool(fields, "poisonous")
    if poisonous:
        out["raw_notes"].append("wiki poisonous=yes; no poison_severity param mapping yet (see plan §5)")

    out["wiki_combat"] = wi.get_int(fields, "combat")
    out["wiki_size"] = wi.get_int(fields, "size")
    out["cache_combat_level"] = cache_combat_level
    out["cache_size"] = cache_size
    out["combat_mismatch"] = out["wiki_combat"] is not None and out["wiki_combat"] != cache_combat_level
    out["size_mismatch"] = out["wiki_size"] is not None and out["wiki_size"] != cache_size

    return out


# ---------------------------------------------------------------------------
# "Already authored elsewhere" — an authored block always wins, so a
# generated one must not be compiled beside it.
# ---------------------------------------------------------------------------

# `mock230_content_section_header` (mock230_content.c) accepts anything
# between `[` and `]` as a block name — no character class at all. A regex
# narrower than that (this one used to be `[a-z0-9_]+`) silently fails to
# recognize a real block like `[mmsnailround_red+black]` as existing, which is
# exactly how two colliding `[gameval]` blocks slipped past the "does this npc
# already have a block" check that is this file's entire reason to exist.
BLOCK_RE = re.compile(r"^\[(.+)\]$")
AI_TRIGGER_RE = re.compile(r"^\[ai_(?:opplayer2|applayer2),(.+)\]$")


def find_scripted_ai(content_dir: str, exclude_path: str) -> set[str]:
    """gamevals with their own `[ai_opplayer2,<gameval>]` or
    `[ai_applayer2,<gameval>]` script anywhere in the tree (dark_wizard.rs2,
    chaos_druid.rs2, ...) — a bespoke combat AI a generated ranged/magic
    binding must not collide with. `_` (the wildcard default) is excluded on
    purpose; it names no single npc. `exclude_path` is this generator's own
    AI_OUT from a previous run — read it back here and every npc it bound
    reads as "already scripted", so a second run would skip regenerating any
    of them and settle on 0 bindings."""
    out = set()
    root = os.path.join(content_dir, "server", "scripts")
    exclude = os.path.abspath(exclude_path)
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".rs2"):
                continue
            full = os.path.join(dirpath, fn)
            if os.path.abspath(full) == exclude:
                continue
            with open(full, encoding="latin-1") as f:
                for line in f:
                    m = AI_TRIGGER_RE.match(line.strip())
                    if m and m.group(1) != "_":
                        out.add(m.group(1))
    return out


def find_existing_blocks(content_dir: str, exclude_paths: set[str]) -> dict[str, str]:
    """gameval -> the file that already declares a `[gameval]` block for it,
    anywhere in the tree.

    Not narrowed to `hitpoints=`. `mock230_content_npc()` (mock230_content.c)
    resolves an npc's def as the *first* `[gameval]` block found across every
    `.npc` file in the tree — two files naming the same npc do not merge, the
    second is simply never reached — so a block with no `hitpoints=` still has
    to be treated as "already spoken for": 63 npcs across canafis_citizen.npc,
    barbarian.npc, undead.npc and four others state real fields (anim
    overrides, `param=undead,1`, ...) with no `hitpoints=` line, and a
    generated block landing ahead of theirs in directory order would make
    those fields silently unreachable. `npc_anims.generated.npc` is the one
    deliberate exception — see `load_npc_anims_extra_lines`, which folds its
    fields into this generator's own block instead of leaving two to race.
    """
    out = {}
    root = os.path.join(content_dir, "server", "scripts")
    exclude = {os.path.abspath(p) for p in exclude_paths}
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".npc"):
                continue
            full = os.path.join(dirpath, fn)
            if os.path.abspath(full) in exclude:
                continue
            current = None
            with open(full, encoding="latin-1") as f:
                for line in f:
                    line = line.strip()
                    m = BLOCK_RE.match(line)
                    if m:
                        current = m.group(1)
                        if current not in out:
                            out[current] = os.path.relpath(full, content_dir)
                        continue
    return out


def load_npc_anims_extra_lines(path: str) -> dict[str, list[str]]:
    """gameval -> its raw `key=value` / `param=k,v` lines in
    `npc_anims.generated.npc`, so this generator's own block can carry them
    forward instead of leaving two competing blocks for the loader's
    first-match-wins resolution to pick between (see `find_existing_blocks`).
    Read as the compiled file states them — already-resolved sequence names
    and sound ids — rather than re-deriving from `gen_npc_combat.py`'s ledger,
    so this cannot drift from what that tool actually emitted.
    """
    out: dict[str, list[str]] = {}
    if not os.path.exists(path):
        return out
    current = None
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            stripped = line.strip()
            if not stripped or stripped.startswith("//"):
                continue
            m = BLOCK_RE.match(stripped)
            if m:
                current = m.group(1)
                out[current] = []
                continue
            if current is not None:
                out[current].append(stripped)
    return out


_DEATH_DROP_CACHE: dict[tuple[str, str | None], str | None] = {}


def death_drop_for_page(title: str, dropversion: str | None) -> str | None:
    """The gameval this npc's `param=death_drop` should state, or None for
    `null` — read off the npc's own cited wiki drop table.

    This has to be asked, and asked here, because the answer defaults the wrong
    way. `general/configs/npc_default.npc`'s `[default]` block authors
    `param=death_drop,bones` and `npc_def_seed_from_cache` copies the whole
    default record — params included — into every def. So an npc that never
    states the param leaves plain bones, not as a decision but as a
    fallthrough, and nothing downstream can tell the two apart: `[ai_queue3,_]`
    and all 147 generated `wiki_*.rs2` tables both just restate the param.

    Three populations, all of which were wrong before this existed:

      * 289 roster npcs whose page states no remains at all — TzHaar (rock, not
        flesh), vyrewatch, rockslugs, killerwatts, the animated tools. They get
        `null`, and both restatement paths are `! null`-guarded, so they leave
        nothing.
      * ~146 whose page states *ashes* — every demon, Vile/Malicious/Fiendish/
        Abyssal/Eldritch/Infernal. They were leaving bones.
      * ~300 whose page states a bones *variant* — Big, Dragon, Wolf, Zogre,
        Wyvern, Hydra, the Monkey Madness set. They were leaving their variant
        (from the table) *and* plain bones (from the fallthrough).

    Delegates to `wiki_droptable.death_drop_choice` rather than re-deriving:
    that module's table generator skips exactly the line this returns, and the
    two answers have to be the same one or the npc drops its remains twice or
    not at all. The block selection is delegated for the same reason — a page
    with several `dropversion` blocks must be read the same way by both.
    """
    key = (title, dropversion)
    if key in _DEATH_DROP_CACHE:
        return _DEATH_DROP_CACHE[key]
    try:
        text = wi.load_page(title)
    except OSError:
        # No page on disk is not "no remains" — it is no evidence either way,
        # and the tree-wide default has to stand. Callers only reach here for a
        # resolved title, so this is the corpus being incomplete, not a miss.
        _DEATH_DROP_CACHE[key] = "bones"
        return "bones"
    blocks = [b for b in wd.parse_drop_blocks(text) if not b["tertiary"]]
    main_blocks = wd.select_blocks(blocks, dropversion)
    if not main_blocks:
        # Ambiguous version split: the page has several `dropversion` blocks and
        # this npc's own version named none of them. Falling through to `bones`
        # here is what the first cut did, and it is wrong in the one direction
        # that matters — it is the same silent default the whole change exists
        # to remove, and `tzhaar_hur1` is exactly this case (six blocks, no
        # match, no remains anywhere on the page, and it went back to bones).
        #
        # So ask the weaker question the evidence can still answer: does *any*
        # main block on this page state remains? If none does, no version of
        # this monster leaves any, whichever one we could not pin — that is a
        # sound conclusion, not a guess. Only when the blocks actually disagree
        # is there nothing to say, and only then does the default stand.
        candidates = {
            c["gameval"]
            for c in (wd.death_drop_choice(b["lines"]) for b in blocks)
            if c
        }
        if not candidates:
            _DEATH_DROP_CACHE[key] = None
            return None
        if len(candidates) == 1:
            result = candidates.pop()
            _DEATH_DROP_CACHE[key] = result
            return result
        _DEATH_DROP_CACHE[key] = "bones"
        return "bones"
    lines = [ln for b in main_blocks for ln in b["lines"]]
    choice = wd.death_drop_choice(lines)
    result = choice["gameval"] if choice else None
    _DEATH_DROP_CACHE[key] = result
    return result


# ---------------------------------------------------------------------------
# Ledger — read/write, `key = value // note` lines, matching gen_npc_combat.py
# ---------------------------------------------------------------------------


def shard_of(gameval: str) -> str:
    c = gameval[0].lower() if gameval else "_"
    return c if c.isalnum() else "_"


def ledger_path(gameval: str) -> str:
    return os.path.join(LEDGER_DIR, shard_of(gameval), gameval + ".stats")


def read_ledger(path: str) -> dict[str, tuple[str | None, str]]:
    rows: dict[str, tuple[str | None, str]] = {}
    if not os.path.exists(path):
        return rows
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.lstrip().startswith("//") or "=" not in line:
                continue
            body, _, note = line.partition("//")
            k, _, v = body.partition("=")
            v = v.strip()
            rows[k.strip()] = (None if v in ("", "-") else v, note.strip())
    return rows


LEDGER_HEADER = """\
// {display} (npc {npc_id}) -- combat level {combat_level}
// Source: OSRS Wiki {wiki_note}
// Generated by tools/gen_npc_stats.py. Re-running rewrites this file.
// To pin a value by hand: edit it, set `source = authored`, and later runs
// read this file back and leave it exactly as it stands.
"""


def write_ledger(gameval: str, npc_id: int, display: str, combat_level: int,
                  source: str, wiki_note: str, rows: dict, checks: list[str],
                  notes: list[str], shadowed_by: str | None = None) -> None:
    path = ledger_path(gameval)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    out = [LEDGER_HEADER.format(display=display or "(unnamed)", npc_id=npc_id,
                                combat_level=combat_level, wiki_note=wiki_note)]
    if shadowed_by:
        out.append(
            "// NOT COMPILED. %s already states `hitpoints=` for [%s], and an\n"
            "// authored .npc block always wins over this generator's output.\n"
            % (shadowed_by, gameval)
        )
    out.append("source = %s" % source)
    out.append("")
    if rows:
        width = max(len(k) for k in rows)
        for key in ALL_KEYS:
            if key in rows:
                out.append("%-*s = %s" % (width, key, rows[key]))
        out.append("")
    if checks:
        out.append("// Checks -- stated, never written to the config.")
        out.extend(checks)
        out.append("")
    if notes:
        out.append("// Notes")
        for n in notes:
            out.append("// " + n)
        out.append("")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out).rstrip() + "\n")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--validate", action="store_true", help="join + checks, write nothing")
    ap.add_argument("--write", action="store_true", help="write the ledger and the compiled config")
    args = ap.parse_args()
    if not args.validate and not args.write:
        args.validate = True

    roster = load_roster()
    corpus = WikiCorpus()
    authored = find_existing_blocks(CONTENT, {CONFIG_OUT, NPC_ANIMS_PATH})
    npc_anims_extra = load_npc_anims_extra_lines(NPC_ANIMS_PATH)
    scripted_ai = find_scripted_ai(CONTENT, AI_OUT)

    status_tally: dict[str, int] = {}
    combat_mismatches = []
    size_mismatches = []
    ranged_npcs = []
    magic_npcs = []
    already_scripted = []
    style_notes = []
    config_blocks = []
    emitted_names = []
    frozen = 0

    for row in roster:
        npc_id = int(row["id"])
        gameval = row["gameval"]
        display_name = row["display_name"]
        cache_combat_level = int(row["combat_level"])
        cache_size = int(row["size"])

        if gameval in authored:
            status_tally["authored_elsewhere"] = status_tally.get("authored_elsewhere", 0) + 1
            if args.write:
                write_ledger(gameval, npc_id, display_name, cache_combat_level,
                            "skipped", "(not consulted; authored elsewhere)", {}, [], [],
                            shadowed_by=authored[gameval])
            continue

        existing_path = ledger_path(gameval)
        existing = read_ledger(existing_path)
        if existing.get("source", (None, ""))[0] == "authored":
            frozen += 1
            status_tally["frozen"] = status_tally.get("frozen", 0) + 1
            rows = {k: v for k, (v, _n) in existing.items() if k in ALL_KEYS and v is not None}
            if rows:
                emitted_names.append(gameval)
                config_blocks.append((gameval, rows))
                if gameval in scripted_ai:
                    already_scripted.append(gameval)
                elif rows.get("damagetype") == "3":
                    ranged_npcs.append(gameval)
                elif "magic_maxhit" in rows:
                    magic_npcs.append(gameval)
            continue

        outcome = corpus.resolve(npc_id, display_name)
        status = outcome["status"]
        status_tally[status] = status_tally.get(status, 0) + 1

        if status != "resolved":
            if args.write:
                write_ledger(gameval, npc_id, display_name, cache_combat_level,
                            "generated", "(unresolved: %s)" % status, {}, [],
                            ["join status: %s" % status,
                             "candidates considered: %s" % (outcome["candidates"] or "none")])
            continue

        version = outcome["version"]
        title = outcome["title"]
        fields = extract_fields(version["fields"], cache_combat_level, cache_size)

        # Always stated, including when the answer is the same `bones` the
        # tree-wide `[default]` would have given. Restating it costs a line in a
        # generated file and buys the thing whose absence caused every bug in
        # this family: after this run, `param=death_drop` on a roster npc is a
        # *statement*, sourced from that npc's own cited page, and never a
        # fallthrough that happens to look like one.
        death_drop = death_drop_for_page(title, version["fields"].get("dropversion"))
        fields["death_drop"] = death_drop if death_drop else "null"
        if death_drop:
            fields["raw_notes"].append(
                "death_drop=%s, off the Always remains line on the cited drop table"
                % death_drop)
        else:
            fields["raw_notes"].append(
                "death_drop=null: the cited drop table states no Always bones/ashes line")

        if fields["combat_mismatch"]:
            combat_mismatches.append((gameval, npc_id, title, cache_combat_level, fields["wiki_combat"]))
        if fields["size_mismatch"]:
            size_mismatches.append((gameval, npc_id, title, cache_size, fields["wiki_size"]))
        needs_binding = fields["style_kind"] in ("ranged", "magic")
        if needs_binding and gameval in scripted_ai:
            already_scripted.append(gameval)
            fields["raw_notes"].append(
                "wiki style is %s but [%s] already has its own ai_opplayer2/ai_applayer2 "
                "script; no generic binding was written so it cannot be overridden" % (
                    fields["style_kind"], gameval))
        elif fields["style_kind"] == "ranged":
            ranged_npcs.append(gameval)
        elif fields["style_kind"] == "magic":
            magic_npcs.append(gameval)
        if fields["raw_notes"]:
            style_notes.append((gameval, fields["raw_notes"]))

        rows = {}
        for key in ALL_KEYS:
            val = fields.get(key)
            if val is not None:
                rows[key] = str(val)

        checks = [
            "cache_combat_level = %s" % cache_combat_level,
            "wiki_combat        = %s%s" % (fields["wiki_combat"], "  !! MISMATCH" if fields["combat_mismatch"] else ""),
            "cache_size         = %s" % cache_size,
            "wiki_size          = %s%s" % (fields["wiki_size"], "  !! MISMATCH" if fields["size_mismatch"] else ""),
            "wiki_max_hit       = %s" % fields["wiki_max_hit"],
        ]

        if args.write:
            write_ledger(gameval, npc_id, display_name, cache_combat_level,
                        "generated", "%r (version ids include %d)" % (title, npc_id),
                        rows, checks, fields["raw_notes"])
        if rows:
            emitted_names.append(gameval)
            config_blocks.append((gameval, rows))

    # ---- report -----------------------------------------------------------
    print("roster: %d attackable spawned npcs" % len(roster))
    print("already hand-authored elsewhere: %d" % status_tally.get("authored_elsewhere", 0))
    print("frozen (ledger source=authored): %d" % frozen)
    for status in ("resolved", "no_wiki_page", "not_in_any_candidate", "ambiguous"):
        print("  %-22s %d" % (status, status_tally.get(status, 0)))
    print("npcs with a compiled stat block: %d" % len(config_blocks))
    print()
    print("combat-level mismatches (cache vs wiki, join is by id so this flags a stale wiki version "
          "or a genuinely ambiguous id, not a bad join): %d" % len(combat_mismatches))
    for gv, npc_id, title, cache_v, wiki_v in combat_mismatches[:25]:
        print("  %-30s npc %-6d cache=%-4s wiki=%-4s (%s)" % (gv, npc_id, cache_v, wiki_v, title))
    if len(combat_mismatches) > 25:
        print("  ... and %d more" % (len(combat_mismatches) - 25))
    print()
    print("size mismatches: %d" % len(size_mismatches))
    for gv, npc_id, title, cache_v, wiki_v in size_mismatches[:15]:
        print("  %-30s npc %-6d cache=%-4s wiki=%-4s (%s)" % (gv, npc_id, cache_v, wiki_v, title))
    if len(size_mismatches) > 15:
        print("  ... and %d more" % (len(size_mismatches) - 15))
    print()
    print("ranged-style npcs given a generated ai_opplayer2 binding: %d" % len(set(ranged_npcs)))
    print("magic-style npcs given a generated ai_opplayer2 binding: %d" % len(set(magic_npcs)))
    print("ranged/magic npcs left alone -- already have a bespoke ai script: %d" % len(set(already_scripted)))
    print("hybrid/unrecognized attack style notes: %d" % len(style_notes))

    if not args.write:
        return

    # ---- compile the config -----------------------------------------------
    header = [
        "// Generated by tools/gen_npc_stats.py --write. Re-running rewrites this file.",
        "//",
        "// Source: the OSRS Wiki's Infobox Monster template, joined to this cache's",
        "// npc ids via each version block's own id/idN list -- never by name. See",
        "// docs/NPC_WIKI_STATS_PLAN.md and the per-npc ledger under npc_stats/.",
        "//",
        "// Named to sort before npc_anims.generated.npc (see CONFIG_OUT in",
        "// gen_npc_stats.py) -- mock230_content_npc() resolves an npc id to the",
        "// FIRST [gameval] block found across every .npc file, so of two files",
        "// naming the same npc, only the first-loaded one is ever read. Every",
        "// block below that npc_anims.generated.npc also covers restates that",
        "// file's attack_anim/defend_anim/death_anim/attack_sound/defend_sound/",
        "// death_sound/death_drop/attackrate lines first, so this file is a",
        "// complete replacement rather than a second, losing competitor.",
        "//",
        "// An npc that already has a hand-authored block (with or without",
        "// hitpoints=) anywhere else in the tree is not repeated here at all --",
        "// see that npc's npc_stats/ ledger file for why it was skipped.",
        "//",
        "// %d npcs." % len(config_blocks),
        "",
    ]
    body = []
    for gameval, rows in sorted(config_blocks):
        body.append("[%s]" % gameval)
        for line in npc_anims_extra.get(gameval, []):
            body.append(line)
        for key in FIRST_CLASS_KEYS:
            if key in rows:
                body.append("%s=%s" % (key, rows[key]))
        if "huntmode" in rows:
            body.append("huntmode=%s" % rows["huntmode"])
        for key in PARAM_KEYS:
            if key in rows:
                body.append("param=%s,%s" % (key, rows[key]))
        body.append("")
    os.makedirs(os.path.dirname(CONFIG_OUT), exist_ok=True)
    with open(CONFIG_OUT, "w", encoding="latin-1", errors="replace") as f:
        f.write("\n".join(header + body).rstrip() + "\n")
    print("\nwrote %s (%d blocks)" % (CONFIG_OUT, len(config_blocks)))

    # ---- compile the ranged/magic ai bindings ------------------------------
    ai_header = [
        "// Generated by tools/gen_npc_stats.py --write. Re-running rewrites this file.",
        "//",
        "// [ai_opplayer2,_] (skill_combat/combat.rs2) is the melee default every npc",
        "// falls through to unless it has its own binding. `~npc_meleeattack` reads",
        "// npc_param(damagetype) and is correct for stab/slash/crush, but it rolls",
        "// accuracy off npc_stat(attack) and max hit off npc_stat(strength) whatever",
        "// the damage type says -- so a Ranged- or Magic-style npc left on the",
        "// default would fight with the wrong stat entirely. These bindings are what",
        "// route those npcs to the roll that actually reads their ranged/magic level.",
        "//",
        "// %d ranged, %d magic." % (len(ranged_npcs), len(magic_npcs)),
        "",
    ]
    ai_body = []
    for gv in sorted(set(ranged_npcs)):
        ai_body.append("[ai_opplayer2,%s]" % gv)
        ai_body.append("~npc_rangeattack;")
        ai_body.append("")
    for gv in sorted(set(magic_npcs)):
        ai_body.append("[ai_opplayer2,%s]" % gv)
        ai_body.append("~npc_generic_magicattack;")
        ai_body.append("")
    os.makedirs(os.path.dirname(AI_OUT), exist_ok=True)
    with open(AI_OUT, "w", encoding="latin-1", errors="replace") as f:
        f.write("\n".join(ai_header + ai_body).rstrip() + "\n")
    print("wrote %s (%d ranged, %d magic bindings)" % (AI_OUT, len(set(ranged_npcs)), len(set(magic_npcs))))

    added = claim_server_membership(CONTENT, emitted_names)
    print("pack/npc.server: %d name(s) added" % added)


if __name__ == "__main__":
    main()
