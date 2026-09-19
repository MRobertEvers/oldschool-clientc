#!/usr/bin/env python3
"""Generate a quest test skeleton from a Quest Helper guide.

Owner: 3b (docs/QUEST_SUITE_KIT.md phase 3). Companion to `tools/
questhelper_extract.py` (imported, not copied -- this file reuses its
gameval resolution, WorldPoint conversion and quest-dbrow guess) and to
`tools/quest_gate/quest_inventory.tsv` (179 rows; the varp/const symbol
names, the reset debugproc, the boss-fight/leftover/cutscene signals a
generated file's tier and BLOCKED stub come from).

What this does NOT do: run the file it writes. `run.py`/`gate.py` are the
gate; this is the scaffold. A generated file legitimately carries `-- CHECK`
markers (guessed op numbers, a guessed step order out of a ConditionalStep's
branches, an unresolved prerequisite) until a human or a later phase
confirms them against a live run -- `lint_quest.py --allow-check` is the bar
this file's own OUTPUT has to clear, not a live client.

Usage
-----
    python3 tools/quest_gate/new_quest.py <helper-dir> [--out test/quests]
    python3 tools/quest_gate/new_quest.py --all [--out build/generated_quests]

`<helper-dir>` is a directory name under quest-helper's `helpers/quests/`
(e.g. `cooksassistant`) or a path to it. `--all` walks every helper dir that
maps to a content quest (tools/quest_gate/quest_inventory.tsv) and writes
one file per match into `build/generated_quests/` (NOT `test/quests/` --
these are unreviewed until a human moves one), printing a summary table.

Design decisions a reader of the output should know about (see also the
worker report this pass produced, docs/QUEST_DRIVER_REMAINING.md and this
module's own inline comments):

  * The "one linear route" a ConditionalStep chain is walked to is: the
    chain's own DEFAULT step first, then every `.addStep(requirement, step)`
    target in the file's own written order, recursing into a target that is
    itself a ConditionalStep, skipping a target already visited (the same
    finish/default step is often both the last `addStep` guard AND the
    chain's own default -- see CooksAssistant.java's `doQuest`). This is a
    heuristic, not a solve of the requirement graph: `addStep` order across
    STAGES in Quest Helper source generally runs most-complete-state-first
    (so the chain falls through to less-complete guards as it reads down,
    ending at the truly-nothing-done default) for a top-level chain, and
    default-first-then-addStep-in-file-order for a SUB-chain reached only
    once a top-level guard has already been satisfied (CooksAssistant's own
    `getFlour`) -- the two are not the same shape, and this walk cannot tell
    which one it is looking at, so it always guesses default-first and
    leaves every `-- CHECK` marker in place for a human to reorder. A step
    reached through `.addSubSteps(...)` is a same-goal ALTERNATE (a
    location-variant of one logical step, e.g. "climb up" vs "already
    upstairs") and is emitted as a comment on its primary step, never as a
    separate route entry.
  * A step whose Java class this file does not know how to turn into a
    driver verb (WidgetStep -- its own ids are raw ints, and the driver's
    own no-numeric-id rule leaves no honest way to name one from Quest
    Helper source alone; DigStep -- there is no `t.player.dig`; any step
    class not in STEP_TYPES) is written as a comment, not a guessed verb
    call, and counted as "unresolved" in the --all table.
  * `quest.bind`'s `row` comes from `questhelper_extract.guess_quest_dbrow`
    (imported). `display`/`points` come from content where this file can
    read it (the quest's own `*.constant` file's `_questpoints` constant,
    the Quest Helper's own `getQuestPointReward()`) and from
    `quest_inventory.tsv`'s `human_name` column -- NOT from the cache's
    `quest` dbtable, which is packed cache data (`quest.dbtable` is a
    *schema*, not row data) this tool has no reader for; every `display`
    this file emits is content-sourced and should be read as a guess a
    human confirms against the in-game quest list, same as every other
    `-- CHECK`.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(REPO / "tools"))
import questhelper_extract as qhe  # noqa: E402  (reused, not copied)

DEFAULT_QH = qhe.DEFAULT_QH
DEFAULT_CONTENT = qhe.DEFAULT_CONTENT
INVENTORY_TSV = REPO / "tools" / "quest_gate" / "quest_inventory.tsv"
QUESTS_DIR = REPO / "test" / "quests"
GEN_DIR = REPO / "build" / "generated_quests"
QUEUE_TSV = REPO / "test" / "quests" / "QUEUE.tsv"
QUEUE_COLUMNS = ["quest_dir", "test_id", "helper_dir", "helper_file", "tier", "status", "owner", "last_failure"]

# The one hand-written test whose file stem does not match the mechanical
# "quest_dir minus quest_/miniquest_" rule -- cooks_assistant.lua predates
# this tool and QUEUE.tsv keeps its id rather than renaming the file.
# hans.lua has no row here at all: Hans is not a quest_inventory.tsv quest.
TEST_ID_OVERRIDES = {"quest_cook": "cooks_assistant"}


def compute_test_id(quest_dir: str) -> str:
    """quest_dir -> the QUEUE.tsv test_id column: the override table first,
    otherwise quest_dir with its quest_/miniquest_ prefix stripped."""
    if quest_dir in TEST_ID_OVERRIDES:
        return TEST_ID_OVERRIDES[quest_dir]
    for prefix in ("quest_", "miniquest_"):
        if quest_dir.startswith(prefix):
            return quest_dir[len(prefix):]
    return quest_dir

STEP_TYPES = (
    "NpcStep", "ObjectStep", "ItemStep", "WidgetStep",
    "PuzzleWrapperStep", "DigStep", "EmoteStep",
)
# Which STEP_TYPES this tool can turn into a real driver verb call. The
# other three (WidgetStep, DigStep, and anything outside STEP_TYPES
# entirely) are written as a comment -- see the module banner.
DRIVEN_STEP_TYPES = ("NpcStep", "ObjectStep", "ItemStep")

GAMEVAL_RE = qhe.GAMEVAL_RE
WORLDPOINT_RE = qhe.WORLDPOINT_RE

_OPEN = set("([{")
_CLOSE = set(")]}")


# --------------------------------------------------------------- utilities

def norm(s: str) -> str:
    s = s.lower().replace("&", "and")
    return re.sub(r"[^a-z0-9]", "", s)


def extract_balanced(text: str, open_idx: int) -> tuple[str, int]:
    """(inner_text, index_just_past_the_matching_close) -- same shape as
    lint_quest.py's own helper (3a's file); reimplemented here rather than
    imported so this tool has no load-bearing coupling to a sibling worker's
    in-flight file."""
    assert text[open_idx] in _OPEN
    depth = 1
    i = open_idx + 1
    in_string = None
    while i < len(text) and depth > 0:
        ch = text[i]
        if in_string:
            if ch == "\\":
                i += 1
            elif ch == in_string:
                in_string = None
        elif ch in ('"', "'"):
            in_string = ch
        elif ch in _OPEN:
            depth += 1
        elif ch in _CLOSE:
            depth -= 1
        i += 1
    return text[open_idx + 1:i - 1], i


def split_top_level(args_text: str) -> list[str]:
    parts, depth, in_string, current = [], 0, None, []
    i = 0
    while i < len(args_text):
        ch = args_text[i]
        if in_string:
            current.append(ch)
            if ch == "\\" and i + 1 < len(args_text):
                i += 1
                current.append(args_text[i])
            elif ch == in_string:
                in_string = None
        elif ch in ('"', "'"):
            in_string = ch
            current.append(ch)
        elif ch in _OPEN:
            depth += 1
            current.append(ch)
        elif ch in _CLOSE:
            depth -= 1
            current.append(ch)
        elif ch == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
        i += 1
    tail = "".join(current).strip()
    if tail or parts:
        parts.append(tail)
    return [p for p in parts if p != ""]


KIND_PACK_FILE = {"NpcID": "npc", "ObjectID": "loc", "ItemID": "obj"}
_PACK_CACHE: dict[str, set[str]] = {}
_PACK_PLUS_CACHE: dict[str, dict[str, str]] = {}


def _load_pack(kind: str) -> tuple[set[str], dict[str, str]]:
    """(exact_names, underscore_to_plus_index) for a gameval KIND's
    all.<pack>.compack -- cached across every helper --all processes in one
    run, since the content tree does not change mid-run. `underscore_to_
    plus_index` maps `name.replace("+", "_")` back to the pack's own literal
    spelling, for symbols the cache names with a `+` a Java identifier
    cannot carry at all (`royal_crate_planks+pulleys` -> gameval
    `ROYAL_CRATE_PLANKS_PULLEYS`, all.loc.compack alone carries 68 of
    these)."""
    pack_name = KIND_PACK_FILE.get(kind)
    if pack_name is None:
        return set(), {}
    if pack_name not in _PACK_CACHE:
        names = qhe.load_compack_names(
            REPO / "OSRS-Content" / "osrs239-content" / "configs" / f"all.{pack_name}.compack"
        )
        _PACK_CACHE[pack_name] = names
        _PACK_PLUS_CACHE[pack_name] = {
            n.replace("+", "_"): n for n in names if "+" in n
        }
    return _PACK_CACHE[pack_name], _PACK_PLUS_CACHE[pack_name]


def resolve_symbol(kind: str, name: str) -> str:
    """A gameval NAME (already lowercased by questhelper_extract's own
    normalize_gameval_name) against its compack, correcting two gaps that
    function leaves: a gameval whose cache name starts with a digit gets a
    Java-legal leading `_` in EVERY gameval kind (`NpcID.
    _0_41_53_SINISTERFISHSPOT` -> cache name `0_41_53_sinisterfishspot`),
    but `normalize_gameval_name` only strips that underscore for ItemID; and
    a cache name that carries a literal `+` (`royal_crate_planks+pulleys`)
    is spelled with an underscore in its own gameval identifier because Java
    has no other choice. Measured against this tool's own --all output,
    2026-09-19: three generated files named an npc with its leading
    underscore still attached, and royaltrouble named a loc
    'royal_crate_planks_pulleys' with no '+' -- neither is in its own
    compack and both would have read as a lint typo/not_found forever.
    Falls back to the given name unchanged (and lets the caller/lint catch
    it) when no spelling resolves."""
    pack, plus_index = _load_pack(kind)
    if not pack or name in pack:
        return name
    if name.startswith("_") and name[1:] in pack:
        return name[1:]
    if name in plus_index:
        return plus_index[name]
    return name


def lua_string(text: str) -> str:
    text = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + text + '"'


# ---------------------------------------------------- inventory + matching

def load_inventory() -> list[dict]:
    with open(INVENTORY_TSV, "r", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    for row in rows:
        row["tier"] = str(compute_tier(row))
    return rows


def compute_tier(row: dict) -> int:
    """The plan's tier rule (docs/QUEST_SUITE_KIT.md phase 3's QUEUE.tsv
    bullet): 1 no fight/no cutscene/no leftover; 2 fight only; 3 cutscene
    signal; 4 any leftover_count>0; 5 unknown varp. The five clauses are not
    mutually exclusive (a quest can have both a boss fight AND a leftover
    item), so they are applied worst-first: an untestable varp beats a
    missing item beats "needs a real client to sit through a cutscene" beats
    "just needs ::skipboss" beats a clean tier-1 quest -- each clause is
    strictly harder to automate than the one after it, so the worst one
    present is what should gate a Haiku agent's attempt."""
    varp = (row.get("varp") or "").strip()
    if not varp or varp == "?" or varp.startswith("?"):
        return 5
    try:
        leftover = int(row.get("leftover_count") or 0)
    except ValueError:
        leftover = 0
    if leftover > 0:
        return 4
    if row.get("cutscene_or_instance") == "yes":
        return 3
    if row.get("boss_fight") == "yes":
        return 2
    return 1


# A handful of helper-dir spellings whose normalized name does not overlap
# an inventory `dir`/`human_name` at all (checked by hand against the 181
# quest-helper dirs vs the 179-row inventory, 2026-09-19) -- everything else
# resolves through the exact-normalized-match rule below.
MATCH_OVERRIDES = {
    "blackknightfortress": "quest_blackknight",
}


# OSRS sequel suffixes normalize() never distinguishes from a plain word
# boundary: "Dragon Slayer II" and "Fairytale I" both normalize their
# numeral to bare letters, same as "Dragon Slayer 2" would to a digit.
_ROMAN_SEQUEL_SUFFIXES = {"i", "ii", "iii", "iv", "v", "vi"}


def _sequel_collision(a: str, b: str) -> bool:
    """True when the only difference between the shorter and longer
    normalized name is a trailing sequel marker -- 'dragonslayer' vs
    'dragonslayer2', or 'dragonslayer' vs 'dragonslayerii' ("Dragon Slayer
    II"'s own normalized human name, ROMAN not arabic: caught live in this
    tool's own --all output, 2026-09-19 -- 'dragonslayer' silently bound to
    quest_dragonslayer2 and clobbered dragonslayerii's own generated file on
    the output path, one quest short with no error printed). A same-length
    prefix match here is almost always two DIFFERENT quests, not a spelling
    variant, so the substring fallback below must refuse it rather than
    silently binding a Quest Helper guide for game N to game N+1's content
    row."""
    shorter, longer = (a, b) if len(a) <= len(b) else (b, a)
    if longer.startswith(shorter):
        rest = longer[len(shorter):]
    elif longer.endswith(shorter):
        rest = longer[:len(longer) - len(shorter)]
    else:
        return False
    if rest == "":
        return False
    return rest[0].isdigit() or rest in _ROMAN_SEQUEL_SUFFIXES


def match_inventory_row(helper_name: str, inventory: list[dict]) -> dict | None:
    """helper-dir name -> its quest_inventory.tsv row, or None. Exact
    normalized match against `dir` (minus the `quest_` prefix) or
    `human_name` first; the override table second; a length-guarded
    substring match last (guards against the sequel collision above).
    Deliberately conservative: a wrong match here corrupts quest.bind's
    varp/constants with ANOTHER quest's, which is worse than reporting
    "no content quest"."""
    nh = norm(helper_name)
    by_dir: dict[str, dict] = {}
    by_name: dict[str, dict] = {}
    for row in inventory:
        d = row["dir"]
        key_dir = norm(d[len("quest_"):] if d.startswith("quest_") else d)
        by_dir.setdefault(key_dir, row)
        by_name.setdefault(norm(row["human_name"]), row)

    if nh in by_dir:
        return by_dir[nh]
    if nh in by_name:
        return by_name[nh]
    if helper_name in MATCH_OVERRIDES:
        target_dir = MATCH_OVERRIDES[helper_name]
        for row in inventory:
            if row["dir"] == target_dir:
                return row
        return None

    best = None
    for key, row in list(by_dir.items()) + list(by_name.items()):
        if len(key) < 6:
            continue
        if key == nh:
            continue
        if (key in nh or nh in key) and not _sequel_collision(key, nh):
            if best is None or len(key) > len(best[0]):
                best = (key, row)
    return best[1] if best else None


def load_constants(dir_name: str) -> dict[str, int]:
    """`^name = <int>` lines from the quest's own configs/<dir>.constant --
    a coord/string-valued constant (`^cook_coord = 0_50_50_6_14`) is skipped,
    not stringified, because quest.bind's constants table is documented as
    plain integers (quest.lua's own banner) and a coord literal there would
    silently make quest.expect_stage compare a stage varp against a tile
    encoding."""
    path = REPO / "OSRS-Content" / "osrs239-content" / "server" / "scripts" \
        / "quests" / dir_name / "configs" / f"{dir_name}.constant"
    values: dict[str, int] = {}
    if not path.is_file():
        return values
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r"^\^([A-Za-z0-9_]+)\s*=\s*(-?\d+)\s*$", line.strip())
        if m:
            values[m.group(1)] = int(m.group(2))
    return values


def strip_common_prefix(names: list[str]) -> dict[str, str]:
    """{"cook_not_started": .., "cook_started": .., "cook_complete": ..} ->
    {"not_started": .., "started": .., "complete": ..} -- the same short
    spelling the hand-written cooks_assistant.lua/_conformance.lua examples
    use (QUEST_NOT_STARTED etc.), found by trimming the longest leading
    `token_` run every name shares. Falls back to the raw name for any
    entry the stripped form would collide on, or when there is nothing
    common to strip (fewer than 2 names, or no shared token)."""
    if len(names) < 2:
        return {n: n for n in names}
    split = [n.split("_") for n in names]
    common = 0
    for tokens in zip(*split):
        if len(set(tokens)) == 1:
            common += 1
        else:
            break
    if common == 0:
        return {n: n for n in names}
    stripped = {n: "_".join(s[common:]) or n for n, s in zip(names, split)}
    seen = list(stripped.values())
    if len(set(seen)) != len(seen):
        return {n: n for n in names}
    return stripped


# --------------------------------------------------------------- java read

def gather_java(helper_dir: Path) -> str:
    return qhe.gather_java(helper_dir)


def find_method_body(text: str, signature_re: str) -> str | None:
    m = re.search(signature_re, text)
    if not m:
        return None
    brace = text.find("{", m.end())
    if brace == -1:
        return None
    body, _ = extract_balanced(text, brace)
    return body


def parse_list_of_identifiers(body: str | None) -> list[str]:
    """The identifiers inside a `return List.of(a, b, c);` /
    `return Arrays.asList(a, b);` -- the only two return shapes this tool
    reads; anything else (a built-up ArrayList, a conditional return) yields
    no requirements rather than a guess, and the generated setup is simply
    shorter -- an author fills it in, same as any other gap this tool
    leaves rather than invents."""
    if not body:
        return []
    m = re.search(r"return\s+(?:List\.of|Arrays\.asList)\s*\(", body)
    if not m:
        return []
    inner, _ = extract_balanced(body, m.end() - 1)
    return [p.strip() for p in split_top_level(inner) if re.match(r"^[A-Za-z_]\w*$", p.strip())]


# ------------------------------------------------------------ step parsing

STEP_DECL_RE = re.compile(
    r"\b(\w+)\s*=\s*new\s+(" + "|".join(STEP_TYPES) + r")\s*\(",
)


def parse_step_worldpoint(args_text: str) -> tuple[int, int, int] | None:
    """The step constructor's own `new WorldPoint(x, z, level)` -- the
    FIRST one in its argument list (a step with several, e.g. an
    NpcStep's own position plus one buried inside a Requirement argument,
    uses the first: the constructor's own position argument is always
    written before any Requirement argument in every STEP_TYPES
    constructor this tool parses). None when the step carries no
    WorldPoint at all (some ObjectStep/NpcStep constructors in these
    guides omit it)."""
    m = WORLDPOINT_RE.search(args_text)
    if not m:
        return None
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)))


def parse_step_decls(text: str) -> dict[str, dict]:
    """varname -> {kind, args, desc, gameval:(GamevalKind,name)|None,
    worldpoint:(x,z,level)|None}."""
    steps: dict[str, dict] = {}
    for m in STEP_DECL_RE.finditer(text):
        var, kind = m.group(1), m.group(2)
        open_idx = m.end() - 1
        args, _ = extract_balanced(text, open_idx)
        gameval = None
        gv = GAMEVAL_RE.search(args)
        if gv:
            raw_name = qhe.normalize_gameval_name(gv.group(1), gv.group(2))
            gameval = (gv.group(1), resolve_symbol(gv.group(1), raw_name))
        worldpoint = parse_step_worldpoint(args)
        # The description is the longest string literal in the constructor
        # call -- every STEP_TYPES constructor takes exactly one `text`
        # (the walkthrough line), and every OTHER string argument these
        # constructors take (an npc's display name override) is always
        # shorter in practice; a longest-literal heuristic is what
        # questhelper_extract.py's own report formatter uses for the same
        # "which string is the human sentence" problem.
        literals = re.findall(r'"((?:[^"\\]|\\.)*)"', args)
        desc = max(literals, key=len) if literals else ""
        steps[var] = {"kind": kind, "args": args, "desc": desc, "gameval": gameval,
                       "worldpoint": worldpoint}
    return steps


DIALOG_RE = re.compile(r"\b(\w+)\.addDialogSteps?\s*\(")
SUBSTEPS_RE = re.compile(r"\b(\w+)\.addSubSteps\s*\(")


def parse_dialog_steps(text: str) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    for m in DIALOG_RE.finditer(text):
        var = m.group(1)
        inner, _ = extract_balanced(text, m.end() - 1)
        lines = [lit for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', inner)]
        if lines:
            out.setdefault(var, []).extend(lines)
    return out


def parse_sub_steps(text: str) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    for m in SUBSTEPS_RE.finditer(text):
        var = m.group(1)
        inner, _ = extract_balanced(text, m.end() - 1)
        names = [p.strip() for p in split_top_level(inner) if re.match(r"^[A-Za-z_]\w*$", p.strip())]
        if names:
            out.setdefault(var, []).extend(names)
    return out


COND_DECL_RE = re.compile(r"\b(\w+)\s*=\s*new\s+(?:Reorderable)?ConditionalStep\s*\(")
ADDSTEP_RE_TMPL = r"\b{var}\.addStep\s*\("


def parse_conditional_steps(text: str) -> dict[str, dict]:
    """varname -> {"default": var|None, "adds": [(condition_text, var), ...]}.
    `adds` keeps the file's own written order -- see the module banner for
    what that order means and why it is only ever a guess."""
    conds: dict[str, dict] = {}
    for m in COND_DECL_RE.finditer(text):
        var = m.group(1)
        open_idx = m.end() - 1
        inner, _ = extract_balanced(text, open_idx)
        args = split_top_level(inner)
        # ctor: (this, [Integer id,] QuestStep step, [String text,] Requirement...)
        # -- drop `this`, drop a lone-integer id, first identifier-looking
        # remainder is the default step.
        rest = args[1:]
        if rest and re.match(r"^-?\d+$", rest[0].strip()):
            rest = rest[1:]
        default = rest[0].strip() if rest else None
        if default is not None and not re.match(r"^[A-Za-z_]\w*$", default):
            # an inline `new XStep(...)` default, or a literal -- no bare
            # var name to recurse into.
            default = None
        conds[var] = {"default": default, "adds": []}
    for var in list(conds.keys()):
        for m in re.finditer(ADDSTEP_RE_TMPL.format(var=re.escape(var)), text):
            inner, _ = extract_balanced(text, m.end() - 1)
            call_args = split_top_level(inner)
            if len(call_args) == 1:
                cond_text, target = "always", call_args[0].strip()
            elif len(call_args) >= 2:
                cond_text, target = call_args[0].strip(), call_args[1].strip()
            else:
                continue
            if re.match(r"^[A-Za-z_]\w*$", target):
                conds[var]["adds"].append((cond_text, target))
    return conds


def parse_steps_put(text: str) -> list[tuple[int, str]]:
    out = []
    for m in qhe.STEPS_PUT_RE.finditer(text):
        expr = m.group(2).strip()
        if re.match(r"^[A-Za-z_]\w*$", expr):
            out.append((int(m.group(1)), expr))
    out.sort(key=lambda t: t[0])
    return out


# ------------------------------------------------------------- the walk

def walk_route(entry: str, step_decls: dict, cond_decls: dict) -> list[tuple[str, str]]:
    """entry var -> [("step", var) | ("unresolved", var), ...] in route
    order, deduped. See the module banner for the ordering rule."""
    route: list[tuple[str, str]] = []
    visited: set[str] = set()

    def visit(var: str) -> None:
        if var in visited:
            return
        visited.add(var)
        if var in step_decls:
            route.append(("step", var))
            return
        if var in cond_decls:
            chain = cond_decls[var]
            if chain["default"]:
                visit(chain["default"])
            for _cond, target in chain["adds"]:
                visit(target)
            return
        # Neither a known step nor a known ConditionalStep -- an inline
        # anonymous step, a step type this tool does not parse, or a
        # forward reference this file's own regex missed.
        route.append(("unresolved", var))

    visit(entry)
    return route


# --------------------------------------------------------------- op guess

OP_KEYWORDS = (
    ("talk", 1), ("climb", 1), ("open", 1), ("search", 1),
    ("pick", 1), ("operate", 1), ("enter", 1),
)


def guess_op(desc: str) -> tuple[int, str]:
    lowered = desc.lower()
    for word, op in OP_KEYWORDS:
        if word in lowered:
            return op, word
    return 1, "default"


# ---------------------------------------------------------- requirements

ITEM_REQ_RE = re.compile(r"\b(\w+)\s*=\s*new\s+ItemRequirement\s*\(")
SKILL_REQ_RE = re.compile(r"\bnew\s+SkillRequirement\s*\(\s*Skill\.(\w+)\s*,\s*(-?\d+)")
QUEST_REQ_RE = re.compile(r"\bnew\s+QuestRequirement\s*\(\s*QuestHelperQuest\.(\w+)")

# An ItemRequirement the guide itself marks `.canBeObtainedDuringQuest()` is
# not a setup ::give -- Quest Helper's own contract for that flag (see
# CooksAssistant.java's egg/milk/flour) is "the player gets this while
# playing", so handing it to the player before run(t) starts erases whatever
# gather step was meant to prove it. See module banner item 2.
GATHER_MARK_RE = re.compile(r"\b(\w+)\.canBeObtainedDuringQuest\s*\(\s*\)")


def parse_gather_markers(text: str) -> set[str]:
    return set(GATHER_MARK_RE.findall(text))


def step_requirement_vars(args_text: str) -> set[str]:
    """The bare bound-identifier arguments in a step constructor's own
    argument list (`pot, grain.highlighted()` -> {"pot", "grain"}) --
    Requirement objects passed positionally after the step's description,
    which is how every STEP_TYPES constructor spells "this step needs
    these". Deliberately narrow (a whole top-level arg must be nothing but
    an identifier plus optional `.method(...)`/`.CONST` trailers) so a
    requirement's own display text -- which often repeats the item's name
    in prose, e.g. getEgg's "Grab an egg..." -- cannot masquerade as a
    reference to the `egg` variable itself."""
    ident_trailer = re.compile(
        r"^[A-Za-z_]\w*(?:\.[A-Za-z_]\w*(?:\([^()]*\))?)*$"
    )
    reqs: set[str] = set()
    for part in split_top_level(args_text):
        part = part.strip()
        if ident_trailer.match(part):
            reqs.add(part.split(".", 1)[0])
    return reqs


def gather_check_line(item_info: dict) -> str:
    sym = item_info["item"]
    return (
        f'        -- CHECK gather: {sym} is obtained during the quest (Quest Helper); '
        f'give it here with t.cheat("::give {sym}") only if the test does not drive the gathering'
    )


def comment_out(text: str) -> str:
    """Every generated line, comment-prefixed in place (a line that is
    already a bare `-- ...` comment -- the step's own description -- is
    left as is) -- used for the route tail that sits behind a fight stub
    and is unreachable until `::skipboss` lands (module banner item 3)."""
    out_lines = []
    for line in text.rstrip("\n").split("\n"):
        stripped = line.strip()
        if not stripped:
            out_lines.append(line)
            continue
        indent = line[: len(line) - len(line.lstrip())]
        if stripped.startswith("--"):
            out_lines.append(line)
        else:
            out_lines.append(indent + "-- " + stripped)
    return "\n".join(out_lines) + "\n"


def parse_item_requirements(text: str) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for m in ITEM_REQ_RE.finditer(text):
        var = m.group(1)
        args, _ = extract_balanced(text, m.end() - 1)
        parts = split_top_level(args)
        gv = GAMEVAL_RE.search(args)
        if not gv or gv.group(1) != "ItemID":
            continue
        item_name = qhe.normalize_gameval_name("ItemID", gv.group(2))
        qty = 1
        for p in parts:
            if re.match(r"^\d+$", p.strip()) and p.strip() != "1":
                qty = int(p.strip())
        qm = re.search(r"\.quantity\s*\(\s*(\d+)\s*\)", text[m.end():m.end() + 400])
        if qm:
            qty = int(qm.group(1))
        out[var] = {"item": item_name, "qty": qty}
    return out


def parse_skill_requirements(text: str) -> list[tuple[str, int]]:
    return [(sk.lower(), int(lvl)) for sk, lvl in SKILL_REQ_RE.findall(text)]


def parse_quest_requirements(text: str) -> list[str]:
    return list(dict.fromkeys(QUEST_REQ_RE.findall(text)))


def resolve_quest_prereq(token: str, inventory: list[dict]) -> str | None:
    """QuestHelperQuest.XXX -> a prerequisite quest's own `dir` (used as
    `::complete <dir-without-quest_-prefix's own row is not known here, so
    the DIR itself is what setup checks against `docs/QUEST_SERVER_CHEATS.md`
    -- the row a `::complete` cheat wants is content's own, and this tool
    only has the dir; a human confirms the exact cheat spelling, same as
    every other CHECK)>, or None."""
    row = match_inventory_row(token.replace("_", ""), inventory)
    return row["dir"] if row else None


# ------------------------------------------------------------- rewards
#
# QuestHelper's own base class (questhelpers/QuestHelper.java) declares
# getQuestPointReward()/getExperienceRewards()/getItemRewards() returning
# null by default -- a subclass overrides only the ones it has, so absence
# here means "this quest really has none of these", not "this tool missed
# it". Scoped to each method's own body (find_method_body, the same helper
# getItemRequirements() already uses above) rather than a whole-file regex,
# so an unrelated ExperienceReward/ItemReward mentioned elsewhere (a
# different override, a comment) cannot leak in.
EXP_REWARD_RE = re.compile(
    r"\bnew\s+ExperienceReward\s*\(\s*Skill\.(\w+)\s*,\s*(-?\d+)\s*\)"
)
ITEM_REWARD_RE = re.compile(
    r'\bnew\s+ItemReward\s*\(\s*"[^"]*"\s*,\s*ItemID\.([A-Z0-9_]+)\s*,\s*(-?\d+)\s*\)'
)
QP_REWARD_RE = re.compile(r"\bnew\s+QuestPointReward\s*\(\s*(-?\d+)\s*\)")


def parse_experience_rewards(text: str) -> list[tuple[str, int]]:
    """[(skill_lowercase, xp), ...] from getExperienceRewards()'s own body,
    in the order Quest Helper lists them -- t.skill.expect_gain's `name`
    argument wants exactly this lowercase spelling (QUEST_AUTHORING.md's
    verb table)."""
    body = find_method_body(text, r"getExperienceRewards\s*\(\s*\)")
    if not body:
        return []
    return [(sk.lower(), int(xp)) for sk, xp in EXP_REWARD_RE.findall(body)]


def parse_item_rewards(text: str) -> list[tuple[str, int]]:
    """[(compack_symbol, qty), ...] from getItemRewards()'s own body -- the
    ItemID gameval resolved through resolve_symbol, same as every other item
    this tool names (module banner's leading-underscore/`+` gaps apply here
    too)."""
    body = find_method_body(text, r"getItemRewards\s*\(\s*\)")
    if not body:
        return []
    out: list[tuple[str, int]] = []
    for raw_name, qty in ITEM_REWARD_RE.findall(body):
        name = qhe.normalize_gameval_name("ItemID", raw_name)
        out.append((resolve_symbol("ItemID", name), int(qty)))
    return out


def reward_local(item_sym: str) -> str:
    """The Lua local name that holds one item reward's before/after count.

    A compack symbol is not automatically a Lua Name -- the module banner's
    own gap list has symbols carrying `+` and a leading `_` -- and a local
    spelled `reward_bones+1_before` is a syntax error that takes the whole
    generated file down, not one row. Every character outside [A-Za-z0-9_]
    becomes `_`, which can only collide with another symbol that differs
    ONLY in those characters (see the module's open issues)."""
    assert item_sym
    return "reward_" + re.sub(r"\W", "_", item_sym)


def parse_quest_point_reward(text: str) -> int | None:
    """getQuestPointReward()'s own `new QuestPointReward(N)` -- the
    authoritative reward value, preferred over the `*_questpoints` constant
    guess `generate()` used alone before this pass (docs/QUEST_SUITE_KIT.md
    H2)."""
    body = find_method_body(text, r"getQuestPointReward\s*\(\s*\)")
    if not body:
        return None
    m = QP_REWARD_RE.search(body)
    return int(m.group(1)) if m else None


# -------------------------------------------------------------- emission

# `fixture = "fresh_lumbridge.ini"` (the only fixture generate() ever emits,
# QUEST_AUTHORING.md trap 7 -- an author may add a real one by hand later,
# but the scaffold itself never does) stands the player at 3206,3233,0 --
# test/quests/fixtures/fresh_lumbridge.ini's own [player] block, beside
# Hans' patrol1 waypoint. Every generated route below is walked against
# this as its starting position, so the FIRST emitted goto always leaves
# this tile (docs/QUEST_SUITE_KIT.md's WorldPoint-goto pass, 2026-09-19).
FIXTURE_START_TILE = (3206, 3233, 0)

# A goto is forced before a WorldPoint-bearing step when it is more than
# this many tiles (Chebyshev -- max(dx, dz), matching how OSRS itself
# measures "in range") from the previously tracked position, on a
# different plane, or is the first WorldPoint-bearing step the route
# reaches (that last case always forces one, regardless of distance, since
# "12 tiles from the fixture start" is not a meaningful measure of whether
# the player can actually see or reach the first step's target).
GOTO_TILE_THRESHOLD = 12


def _chebyshev(a: tuple[int, int], b: tuple[int, int]) -> int:
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


class RoutePosition:
    """Tracks the player's assumed position as the generated route is
    walked, and decides when a step needs a `player.goto_tile` ahead
    of it (`goto_tile`, never `goto` -- `goto` is a reserved word in
    this tree's Lua, so the shorter name is a syntax error at every
    generated call site).
    A step with no WorldPoint of its own (Quest Helper omitted one)
    inherits whatever position the last WorldPoint-bearing step left --
    it does not reset the tracker, and it never forces a goto on its own;
    it only ever gets a '-- CHECK position' comment, per
    docs/QUEST_SUITE_KIT.md."""

    def __init__(self) -> None:
        self.pos: tuple[int, int, int] | None = None  # None until the
        # first WorldPoint-bearing step -- see docstring.

    def lines_for(self, var: str, info: dict) -> list[str]:
        wp = info.get("worldpoint")
        if wp is None:
            return [f"        -- CHECK position: no WorldPoint in Quest Helper for '{var}' "
                     "-- confirm the player is already near this step's target"]
        x, z, level = wp
        if self.pos is None:
            reason = "first step -- leaves the fixture's start tile"
            need_goto = True
        else:
            px, pz, plevel = self.pos
            if level != plevel:
                need_goto = True
                reason = f"plane change ({plevel} -> {level})"
            else:
                dist = _chebyshev((x, z), (px, pz))
                need_goto = dist > GOTO_TILE_THRESHOLD
                reason = f"{dist} tiles from the last tracked position"
        self.pos = (x, z, level)
        if not need_goto:
            return []
        return [
            f'        t.exec({lua_string("goto-" + var)}, t.player.goto_tile, {x}, {z}, {level}) '
            f"-- {reason}"
        ]


def format_step_call(var: str, info: dict, dialog: dict, checks: list[str]) -> tuple[str, bool]:
    """One t.exec(...) line (or an unresolved comment) for a single leaf
    step. Returns (lua_lines_text, was_driven)."""
    kind = info["kind"]
    desc = info["desc"] or var
    comment = "        -- " + (desc if desc else var)
    gameval = info["gameval"]

    if kind not in DRIVEN_STEP_TYPES or gameval is None:
        checks.append(f"unresolved step kind/subject: {var} ({kind})")
        return (
            comment + "\n"
            f"        -- CHECK unresolved: {kind} '{var}' has no symbol this tool "
            "could resolve to a driver verb -- fill in by hand.\n"
        ), False

    gv_kind, symbol = gameval
    lines = [comment]
    name = f"{var}"
    if gv_kind == "NpcID":
        op, guessed_by = guess_op(desc)
        lines.append(
            f'        t.exec({lua_string(name)}, t.player.talk_to, {lua_string(symbol)}, {op}) '
            f"-- CHECK op {op} guessed from '{guessed_by}'"
        )
        checks.append(f"{var}: op guessed ({guessed_by} -> {op})")
    elif gv_kind == "ObjectID":
        op, guessed_by = guess_op(desc)
        lines.append(
            f'        t.exec({lua_string(name)}, t.player.click_loc, {lua_string(symbol)}, {op}) '
            f"-- CHECK op {op} guessed from '{guessed_by}'"
        )
        checks.append(f"{var}: op guessed ({guessed_by} -> {op})")
    elif gv_kind == "ItemID":
        lines.append(
            f'        t.exec({lua_string(name)}, t.player.click_obj, {lua_string(symbol)})'
        )
    else:
        checks.append(f"unresolved gameval kind for step: {var} ({gv_kind})")
        return comment + f"\n        -- CHECK unresolved gameval kind {gv_kind} for '{var}'\n", False

    # Each dialog line gets its OWN literal step name (`-dialog-1`, `-dialog-2`,
    # ...): a bare `name + "-dialog"` repeated across N lines is the exact
    # straight-line duplicate lint_quest.py's own duplicate-t.exec-name rule
    # exists to catch (two IDENTICAL string literals, not core.lua's runtime
    # -2/-3 suffix, which only fires for a name computed in a loop) --
    # measured against this tool's own output on cooksassistant, 2026-09-19.
    for index, line in enumerate(dialog.get(var, []), start=1):
        lines.append(
            f'        t.exec({lua_string(name + "-dialog-" + str(index))}, t.chat.play, '
            f"{{{lua_string('choose:' + line)}}}) -- CHECK dialog row text guessed from Quest Helper"
        )
        checks.append(f"{var}: dialog row guessed ({line!r})")

    return "\n".join(lines) + "\n", True


def generate(
    helper_dir: Path,
    inv_row: dict,
    qh_root: Path,
    inventory: list[dict],
    test_id: str | None = None,
    only_file: str | None = None,
) -> tuple[str, dict]:
    """`test_id`, when given, is the emitted file's own `id = ...` field
    (the QUEUE.tsv-driven flow's test_id, e.g. "cooks_assistant" for
    quest_cook) -- otherwise `id` falls back to quest_dir with its
    quest_/miniquest_ prefix stripped, the --all/--helper flow's own
    long-standing behaviour, unchanged. `only_file`, when given (QUEUE.tsv's
    helper_file column), reads exactly that one file under `helper_dir`
    instead of every `*.java` under it -- for a shared helper directory like
    recipefordisaster/ where each subquest test wants only its own file."""
    helper_name = helper_dir.name
    text = (helper_dir / only_file).read_text(errors="replace") if only_file else gather_java(helper_dir)

    step_decls = parse_step_decls(text)
    dialog = parse_dialog_steps(text)
    sub_steps = parse_sub_steps(text)
    cond_decls = parse_conditional_steps(text)
    puts = parse_steps_put(text)

    entries: list[str] = []
    for _n, var in puts:
        if var not in entries:
            entries.append(var)
    if not entries and step_decls:
        # No steps.put found at all (a helper this tool's regex missed the
        # shape of) -- fall back to every declared step in declaration
        # order rather than emitting an empty run().
        entries = list(step_decls.keys())

    checks: list[str] = []
    route: list[tuple[str, str]] = []
    for entry in entries:
        for item in walk_route(entry, step_decls, cond_decls):
            if item not in route:
                route.append(item)

    dir_name = inv_row["dir"]
    quest_id = dir_name[len("quest_"):] if dir_name.startswith("quest_") else dir_name
    emitted_id = test_id or quest_id
    display = inv_row["human_name"]
    varp = inv_row["varp"].lstrip("%")

    dbrow_names = qhe.load_compack_names(
        REPO / "OSRS-Content" / "osrs239-content" / "configs" / "all.dbrow.compack"
    )
    dbrow_names |= qhe.load_compack_names(
        REPO / "OSRS-Content" / "osrs239-content" / "pack" / "dbrow.alloc"
    )
    row_guess = qhe.guess_quest_dbrow(helper_name, text, dbrow_names)
    # questhelper_extract.guess_quest_dbrow's own fallback candidate comes
    # from a bare `Quest\.([A-Z0-9_]+)` regex over the WHOLE file, which
    # also matches inside `QuestHelperQuest.ROVING_ELVES` (the last five
    # characters of `QuestHelperQuest` spell `Quest`, so the regex fires at
    # the `.` right after it) -- a requirement naming a DIFFERENT quest, not
    # this one. Measured against this tool's own --all output, 2026-09-19:
    # mourningsendparti and pathofglouphrie both got bound to
    # "quest_rovingelves" this way (a real prerequisite of theirs, but not
    # their OWN row), and theslugmenace got "quest_wanted" the same way --
    # each one collided with lint_quest.py's own "setup completes its own
    # row" rule once the SAME guess also leaked into a resolved prereq's
    # `::complete`. A guess whose dbrow (minus quest_/miniquest_) has no
    # normalized overlap with this helper's own name is almost certainly
    # that regex picking up someone else's quest, so it is discarded here
    # rather than trusted.
    if row_guess:
        bare = row_guess
        for prefix in ("quest_", "miniquest_"):
            if bare.startswith(prefix):
                bare = bare[len(prefix):]
                break
        key = norm(bare)
        nh = norm(helper_name)
        if key != nh and key not in nh and nh not in key:
            checks.append(f"row: discarded dbrow guess {row_guess!r} -- no name overlap with "
                           f"helper '{helper_name}' (likely a same-file QuestRequirement for a "
                           "DIFFERENT quest, not this one)")
            row_guess = None

    const_values = load_constants(dir_name)
    const_not_started_sym = inv_row["const_not_started"].lstrip("^")
    const_complete_sym = inv_row["const_complete"].lstrip("^")
    key_map = strip_common_prefix(list(const_values.keys())) if const_values else {}
    constants_lua = {}
    for raw_name, value in const_values.items():
        constants_lua[key_map.get(raw_name, raw_name)] = value
    complete_value = const_values.get(const_complete_sym)
    if complete_value is None:
        checks.append(f"constants.complete: could not resolve {inv_row['const_complete']!r} "
                       f"in configs/{dir_name}.constant")
        complete_value = 0
    constants_lua["complete"] = complete_value
    not_started_value = const_values.get(const_not_started_sym)
    if not_started_value is not None:
        constants_lua["not_started"] = not_started_value

    # getQuestPointReward() is Quest Helper's own authoritative reward value
    # -- preferred over the `*_questpoints` constant guess (H2,
    # docs/QUEST_SUITE_KIT.md: "Use QuestPointReward(N) ... instead of the
    # current guess when present").
    qp_reward = parse_quest_point_reward(text)
    if qp_reward is not None:
        points = qp_reward
    else:
        points = None
        for name, value in const_values.items():
            if name.endswith("questpoints"):
                points = value
                break
        if points is None:
            points = 1
            checks.append("points: no QuestPointReward(N) or *_questpoints constant found -- defaulted to 1")

    # ::clearinv is ALWAYS the first setup line -- the fresh character
    # (fresh_lumbridge.ini) carries fourteen occupied backpack slots --
    # content's [proc,newplayer_inv] (player/newplayer.rs2:119) is what
    # puts them there, and build/quest_gate/closer_setup2 counted them --
    # and they block a
    # non-stackable requirement fitting in the backpack (H2,
    # docs/QUEST_SUITE_KIT.md: sheep's REJECTED pass had to drop tutorial
    # items one by one by hand to fit 20 balls of wool). It is on the engine
    # cheat ladder (torirs_server_world.c) and proved by a live row of
    # test/quests/_cheats.lua ("cheats.clearinv"), so a generated file can be
    # run as it stands.
    setup_cheats: list[str] = ["::clearinv"]
    if inv_row.get("has_reset") == "yes" and inv_row.get("reset_name") not in (None, "", "?"):
        setup_cheats.append(f"::{inv_row['reset_name']}")
    else:
        checks.append("setup: no reset debugproc in quest_inventory.tsv -- confirm fixture start state by hand")

    item_reqs = parse_item_requirements(text)
    gather_marked = parse_gather_markers(text)
    itemreq_body = find_method_body(text, r"getItemRequirements\s*\(\s*\)")
    start_item_vars = parse_list_of_identifiers(itemreq_body)
    # An ItemRequirement the guide marks canBeObtainedDuringQuest() is NOT
    # handed to the player in setup -- it is left as a "-- CHECK gather"
    # marker at the first step that requires it (see gather_items below,
    # placed once the route is walked). Module banner item 2.
    gather_items: dict[str, dict] = {}
    given_count = 0
    for var in start_item_vars:
        info = item_reqs.get(var)
        if info is None:
            checks.append(f"setup: getItemRequirements() names '{var}' but its ItemID could not be resolved")
        elif var in gather_marked:
            gather_items[var] = info
        else:
            setup_cheats.append(f"::give {info['item']} {info['qty']}")
            given_count += 1
    gathered_count = len(gather_items)
    placed_gather: set[str] = set()

    for skill, level in parse_skill_requirements(text):
        setup_cheats.append(f"::setlevel {skill} {level}")

    # Resolved and folded into `setup_cheats` BEFORE the setup table below is
    # emitted -- a prereq line appended after that point would land in the
    # Lua file's comments but never in the setup list itself.
    prereq_lines: list[tuple[str, str | None]] = []
    for token in parse_quest_requirements(text):
        prereq_dir = resolve_quest_prereq(token, inventory)
        # A QuestRequirement naming THIS quest is not a prerequisite -- Quest
        # Helper guides use it to branch on the player's OWN progress (a
        # later-stage check, a miniquest gate), and lint_quest.py's own rule
        # refuses a setup that completes a quest's own row before run(t)
        # plays it (measured against this tool's own --all output,
        # 2026-09-19: curseofarrav, enlightenedjourney, ragandboneman,
        # shadowsofcustodia and two RFD-adjacent quests all self-matched
        # this way). Reported as an unresolved CHECK instead of silently
        # dropped, so an author still sees the guide named a requirement
        # here.
        if prereq_dir == dir_name:
            prereq_dir = None
            checks.append(f"prereq self-reference dropped: QuestHelperQuest.{token} named this quest's own row")
        prereq_lines.append((token, prereq_dir))
        if prereq_dir:
            setup_cheats.append(f"::complete {prereq_dir}")

    boss_fight = inv_row.get("boss_fight") == "yes"
    boss_npcs = inv_row.get("boss_npcs", "")

    # Reward checks (H2, docs/QUEST_SUITE_KIT.md): sheep's REJECTED pass
    # never checked its own reward, so every generated file now snapshots
    # before the FINAL hand-in step and asserts the gain after
    # expect_complete(). Only meaningful when the route actually reaches
    # expect_complete() -- a boss_fight quest always ends at a t.blocked()
    # stub instead (see the emission loop below), so no reward_before/check
    # lines are emitted for one; the reward is real, but nothing in this
    # generated file will ever observe it landing.
    exp_rewards = parse_experience_rewards(text)
    item_rewards = parse_item_rewards(text)
    emit_rewards = bool(not boss_fight and (exp_rewards or item_rewards))
    if boss_fight and (exp_rewards or item_rewards):
        checks.append(
            "rewards: getExperienceRewards()/getItemRewards() found real rewards, but this "
            "quest ends at a t.blocked() skipboss stub -- no reward_before/reward.* rows emitted"
        )

    # ----------------------------------------------------------- emit Lua
    lines: list[str] = []
    lines.append(f"-- Generated by tools/quest_gate/new_quest.py from Quest Helper's")
    lines.append(f"-- helpers/quests/{helper_name}/ -- docs/QUEST_SUITE_KIT.md phase 3.")
    lines.append(f"-- UNREVIEWED: every '-- CHECK' marker below needs a human or a later")
    lines.append(f"-- phase to confirm against a live run before this counts as a passing")
    lines.append(f"-- quest test. Tier (quest_inventory.tsv): {inv_row.get('tier', '?')}.")
    lines.append(
        f"-- Items from getItemRequirements(): {given_count} given in setup (::give), "
        f"{gathered_count} left as '-- CHECK gather' markers (Quest Helper canBeObtainedDuringQuest())."
    )
    lines.append(
        "-- Setup always starts with ::clearinv: the fresh character carries fourteen"
    )
    lines.append(
        "-- slots of tutorial kit that block a non-stackable requirement fitting in the backpack."
    )
    lines.append(
        f"-- Rewards from Quest Helper: {len(exp_rewards)} experience, {len(item_rewards)} item, "
        f"quest points {points} ({'QuestPointReward' if qp_reward is not None else 'guessed from a constant/default'})."
        + ("" if emit_rewards or (not exp_rewards and not item_rewards) else
           " Reward checks NOT emitted -- see the CHECK near boss_fight above.")
    )
    lines.append("--")
    lines.append(
        f"-- Fixture start: fresh_lumbridge.ini stands the player at "
        f"{FIXTURE_START_TILE[0]},{FIXTURE_START_TILE[1]},{FIXTURE_START_TILE[2]} (Lumbridge, beside"
    )
    lines.append(
        "-- Hans). Every step below carries its own Quest Helper WorldPoint; the"
    )
    lines.append(
        "-- FIRST emitted t.player.goto_tile is what actually leaves that tile."
    )
    lines.append("--")
    lines.append("-- Three rules the first pilot pass broke -- read before touching this file:")
    lines.append(
        '-- (a) "blocked" means a t.blocked("...") row followed by return -- a file'
    )
    lines.append(
        "--     that runs on to expect_complete() after a failure is rejected."
    )
    lines.append(
        "-- (b) a talk_to/click answering screen_position or not_visible means you"
    )
    lines.append(
        "--     are not standing near the target -- fix the goto, not the verb."
    )
    lines.append(
        "-- (c) never add a fixture or a helper file -- this quest file is the only"
    )
    lines.append("--     file you edit.")
    lines.append("")
    lines.append("return {")
    lines.append(f"    id = {lua_string(emitted_id)},")
    lines.append('    fixture = "fresh_lumbridge.ini",')
    if setup_cheats:
        lines.append("    setup = {")
        for cheat in setup_cheats:
            suffix = (
                " -- the fixture's fourteen tutorial slots, so a requirement fits"
                if cheat == "::clearinv" else ""
            )
            lines.append(f"        {lua_string(cheat)},{suffix}")
        lines.append("    },")
    else:
        lines.append("    setup = {}, -- CHECK: nothing resolved automatically")
    lines.append("")
    lines.append("    run = function(t)")
    lines.append(f"        local bind_result, bind_detail = t.quest.bind({{")
    lines.append(f"            varp = {lua_string(varp)},")
    lines.append("            constants = {")
    for key, value in sorted(constants_lua.items()):
        lines.append(f"                {key} = {value},")
    lines.append("            },")
    if row_guess:
        lines.append(f"            row = {lua_string(row_guess)},")
    else:
        lines.append("            row = nil, -- CHECK: quest dbrow guess failed, name it by hand")
        checks.append("row: quest dbrow guess failed")
    lines.append(f"            display = {lua_string(display)}, -- CHECK: content-sourced, confirm against the live quest list")
    lines.append(f"            points = {points},")
    lines.append("        })")
    lines.append('        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)')
    lines.append("")

    if prereq_lines:
        lines.append("        -- Prerequisite quest(s) this helper names (QuestHelperQuest tokens)")
        for token, prereq_dir in prereq_lines:
            if prereq_dir:
                lines.append(f"        -- CHECK setup already has '::complete {prereq_dir}' "
                              f"(resolved from QuestHelperQuest.{token}) "
                              "-- confirm this is really the row this content pack wants")
                checks.append(f"prereq resolved but unconfirmed: {token} -> {prereq_dir}")
            else:
                lines.append(f"        -- CHECK unresolved prereq: QuestHelperQuest.{token} -- add "
                              "'::complete <its row>' to setup by hand")
                checks.append(f"prereq unresolved: {token}")
        lines.append("")

    boss_npc_set = {n.strip() for n in boss_npcs.split(";") if n.strip()}

    # Gather-item CHECKs that no walked step's own args ever reference get
    # no better anchor than "as soon as we know what this quest needs" --
    # placed right after bind(), ahead of the whole route, rather than
    # silently dropped or stranded at the end of the file.
    def _step_reqs(v: str) -> set[str]:
        return step_requirement_vars(step_decls[v]["args"]) if v in step_decls else set()

    for gvar in gather_items:
        if not any(kind == "step" and gvar in _step_reqs(var) for kind, var in route):
            lines.append(gather_check_line(gather_items[gvar]))
            lines.append(
                f"        -- CHECK: {gather_items[gvar]['item']} never appeared in a walked step's "
                "own requirement args -- placement guessed, confirm by hand"
            )
            placed_gather.add(gvar)
    if any(gvar in placed_gather for gvar in gather_items):
        lines.append("")

    stage_index = 0
    driven_count = 0
    blocked_stubs = 0
    goto_count = 0
    fight_stub_emitted = False
    route_pos = RoutePosition()

    # The last "step" route entry is the FINAL hand-in step -- reward_before
    # is snapshotted right ahead of it (H2, docs/QUEST_SUITE_KIT.md). Only
    # meaningful when the route actually reaches expect_complete()
    # (emit_rewards is already False for a boss_fight quest, which always
    # ends at a t.blocked() stub before or at this point -- see above).
    last_hand_in_idx = None
    if emit_rewards:
        for ridx in range(len(route) - 1, -1, -1):
            if route[ridx][0] == "step":
                last_hand_in_idx = ridx
                break
        if last_hand_in_idx is None:
            emit_rewards = False
            checks.append("rewards: no driven step found to anchor reward_before -- reward checks not emitted")

    # Every discoverable route step is walked and emitted REGARDLESS of
    # boss_fight -- a fight the manifest lists does not usually sit at step
    # 0, and the earlier draft of this function threw away every pre-fight
    # step (getBucket/getPot/... for a quest whose fight is its very last
    # stage) to write nothing but a single blocked() call. Whatever the
    # walk found is real, checkable content; only the TAIL -- the part that
    # needs the fight to have actually happened -- is unreachable without
    # `::skipboss`, so that is the only part this replaces, and it is
    # replaced AT THE FIGHT STEP ITSELF (the first walked step whose own
    # npc gameval is in the manifest's boss_npcs), not at the end of the
    # file: docs/QUEST_SUITE_KIT.md phase 3, module banner item 3.
    for route_idx, (kind, var) in enumerate(route):
        if fight_stub_emitted:
            # Past the fight -- still emitted, so a human/phase-4 pass sees
            # exactly what would run, just commented out until `::skipboss`
            # lands (the fight step it depends on cannot happen yet).
            if kind == "step":
                info = step_decls[var]
                pos_lines = route_pos.lines_for(var, info)
                if pos_lines:
                    goto_count += sum(1 for pl in pos_lines if "t.player.goto_tile" in pl)
                    lines.append(comment_out("\n".join(pos_lines) + "\n"))
                call_text, _driven = format_step_call(var, info, dialog, checks)
                lines.append(comment_out(call_text))
                for alt in sub_steps.get(var, []):
                    lines.append(
                        f"        -- CHECK alternative for {var}: {alt} (location/state variant, not walked)"
                    )
            else:
                lines.append(f"        -- CHECK unresolved route entry: {var}")
            continue

        if kind != "step":
            # Counted, not just printed. Review 2026-09-19: these were
            # emitted straight into the file without ever reaching `checks`,
            # so the --all table's `unresolved` column read 0 for a quest
            # carrying four of them (xmarksthespot) and an author picking
            # work off that column opened a file that was not resolved.
            checks.append(f"unresolved route entry: {var}")
            lines.append(f"        -- CHECK unresolved route entry: {var}")
            continue

        info = step_decls[var]
        gameval = info["gameval"]

        # Position BEFORE anything else this step emits -- a talk_to/click
        # against a target the player has not walked to yet is exactly the
        # screen_position/not_visible failure the pilot pass hit at its very
        # first step (docs/QUEST_SUITE_KIT.md).
        pos_lines = route_pos.lines_for(var, info)
        if pos_lines:
            goto_count += sum(1 for pl in pos_lines if "t.player.goto_tile" in pl)
            lines.extend(pos_lines)

        reqs = step_requirement_vars(info["args"])
        for gvar in gather_items:
            if gvar not in placed_gather and gvar in reqs:
                lines.append(gather_check_line(gather_items[gvar]))
                placed_gather.add(gvar)

        if emit_rewards and route_idx == last_hand_in_idx:
            lines.append("")
            lines.append(
                "        -- Reward snapshot before the FINAL hand-in step (H2, "
                "docs/QUEST_SUITE_KIT.md) -- sheep's"
            )
            lines.append(
                "        -- REJECTED pass never checked its own reward; reward.* below does."
            )
            if exp_rewards:
                # BOTH returns, never just the first: t.skill.snapshot answers
                # (result, snapshot) like every other verb in this driver
                # (script/plugins/quest_driver/state.lua), so `local s =
                # t.skill.snapshot()` binds the STRING "ok" and every
                # expect_gain against it answers `no_row: snapshot is not a
                # table`. Measured on the first generated sheep file, 2026-09-19.
                lines.append(
                    "        local reward_snapshot_result, reward_before = t.skill.snapshot()"
                )
                # The snapshot gets its own row, as cooks_assistant.lua's
                # hand-written one does: a snapshot that did not read the
                # stats makes every reward check below answer `no_row`, and
                # the row that says so should be the snapshot's, not the
                # reward's. t.step, not t.exec -- it takes no shot and
                # gate.py's one-PNG-per-t.exec-row rule does not reach it.
                lines.append(
                    '        t.step("reward.snapshot", '
                    'reward_snapshot_result == "ok" and "PASS" or "FAIL", '
                    '"skill.snapshot before the hand-in -> " '
                    ".. tostring(reward_snapshot_result))"
                )
            for item_sym, _qty in item_rewards:
                local = reward_local(item_sym)
                lines.append(
                    f"        local {local}_before_result, {local}_before = "
                    f"t.inv.count({lua_string(item_sym)})"
                )
            lines.append("")

        if boss_fight and gameval is not None and gameval[0] == "NpcID" and gameval[1] in boss_npc_set:
            desc = info["desc"] or var
            lines.append("        -- " + desc)
            lines.append(f'        t.blocked("skipboss not landed: {gameval[1]}")')
            lines.append("        return")
            lines.append("")
            lines.append(
                "        -- Steps below are UNREACHABLE until ::skipboss lands "
                "(docs/QUEST_SUITE_KIT.md phase 4) -- left commented for that phase to uncomment."
            )
            fight_stub_emitted = True
            blocked_stubs += 1
            continue

        call_text, driven = format_step_call(var, info, dialog, checks)
        lines.append(call_text)
        if driven:
            driven_count += 1
            stage_index += 1
            if stage_index % 3 == 0:
                lines.append(
                    f'        t.check("expect_stage-{stage_index}", '
                    f'select(1, t.quest.stage()) ~= nil) -- CHECK: bind a real named stage here'
                )
        for alt in sub_steps.get(var, []):
            checks.append(f"alternative for {var}: {alt} (location/state variant, not walked)")
            lines.append(f"        -- CHECK alternative for {var}: {alt} (location/state variant, not walked)")

    lines.append("")
    if boss_fight and not fight_stub_emitted:
        # The manifest says this quest has a fight, but the walked route
        # never reached a step whose own npc gameval matched
        # quest_inventory.tsv's boss_npcs -- fall back to the old
        # end-of-file stub rather than silently dropping the BLOCKED verdict.
        lines.append(f'        t.blocked("skipboss not landed: {boss_npcs or quest_id}")')
        lines.append("        return")
        checks.append("boss_fight stub: no walked step matched boss_npcs -- placed at end of file, not at the fight")
        blocked_stubs += 1
    elif not fight_stub_emitted:
        lines.append("        t.quest.expect_complete()")
        if emit_rewards:
            lines.append("")
            lines.append(
                "        -- Reward checks -- the quest's actual reward, not just its completion"
                " (H2, docs/QUEST_SUITE_KIT.md)."
            )
            for skill, xp in exp_rewards:
                lines.append(
                    f'        t.check("reward.{skill}", '
                    f"t.skill.expect_gain({lua_string(skill)}, {xp}, reward_before))"
                )
            for item_sym, qty in item_rewards:
                local = reward_local(item_sym)
                lines.append(
                    f"        local {local}_after_result, {local}_after = "
                    f"t.inv.count({lua_string(item_sym)})"
                )
                # Both reads are asserted, and the detail is built with %s
                # over tostring(): t.inv.count answers (result, count) but
                # hands back (result, THE SYMBOL) when the symbol does not
                # resolve, and there is no pcall in this sandbox -- a bare
                # `count + qty` on that string ends the whole run at this
                # line instead of failing one row.
                lines.append(
                    f'        t.check("reward.{item_sym}", '
                    f'{local}_before_result == "ok" and {local}_after_result == "ok" '
                    f"and {local}_after == {local}_before + {qty}, "
                    f'string.format("{item_sym} %s -> %s (want +{qty}), reads %s/%s", '
                    f"tostring({local}_before), tostring({local}_after), "
                    f"tostring({local}_before_result), tostring({local}_after_result)))"
                )
            lines.append("")
        lines.append("        t.finish(0)")
    lines.append("    end,")
    lines.append("}")

    source = "\n".join(lines) + "\n"
    stats = {
        "steps": driven_count,
        "checks": len(checks),
        "blocked": blocked_stubs,
        "gotos": goto_count,
        "unresolved": len([c for c in checks if c.startswith("unresolved")]),
        # Every `-- CHECK` line actually written into the file -- the size of
        # the human review this skeleton still owes, which `checks` (the
        # tool's own internal notes) understates.
        "checks_todo": sum(1 for line in lines if "-- CHECK" in line),
    }
    return source, stats


# -------------------------------------------------------------------- CLI

def resolve_helper_dir(arg: str, qh_root: Path) -> Path:
    p = Path(arg)
    if p.is_dir():
        return p.resolve()
    cand = qh_root / arg
    if cand.is_dir():
        return cand.resolve()
    raise SystemExit(f"helper dir not found: {arg}")


def run_one(helper_arg: str, out_dir: Path, qh_root: Path, inventory: list[dict]) -> dict:
    """The old bulk/--all path: one helper DIRECTORY -> one output file
    named after quest_dir (not QUEUE.tsv's test_id, though `result["test_id"]`
    now reports what that id would be, for the printed table)."""
    helper_dir = resolve_helper_dir(helper_arg, qh_root)
    inv_row = match_inventory_row(helper_dir.name, inventory)
    result = {"helper": helper_dir.name, "quest": None, "test_id": None, "path": None,
              "steps": 0, "checks": 0, "blocked": 0, "gotos": 0, "unresolved": 0,
              "checks_todo": 0, "error": None}
    if inv_row is None:
        result["error"] = "no content quest"
        return result
    result["quest"] = inv_row["dir"]
    result["test_id"] = compute_test_id(inv_row["dir"])
    try:
        lua_text, stats = generate(helper_dir, inv_row, qh_root, inventory)
    except Exception as exc:  # pragma: no cover -- --all must survive one bad helper
        result["error"] = f"{type(exc).__name__}: {exc}"
        return result
    quest_id = inv_row["dir"][len("quest_"):] if inv_row["dir"].startswith("quest_") else inv_row["dir"]
    out_path = out_dir / f"{quest_id}.lua"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path.write_text(lua_text, encoding="utf-8")
    result["path"] = str(out_path)
    result.update(stats)
    return result


# The ten Recipe for Disaster subquests: one physical Quest Helper
# directory (`helpers/quests/recipefordisaster/`) and one physical content
# directory (`quest_recipefordisaster`) hold what are, on both sides, TEN
# separate quests (dbrow-wise: subquest_rfd_intro + nine subquest_rfd_*
# rows -- quest_inventory.tsv's own note on the umbrella row). Each tuple is
# (test_id, java_file, quest_dir_suffix) -- suffix None means "the umbrella
# row itself" (quest_recipefordisaster, the intro/accept stage: RFDStart.java
# is the guide's own top-level BasicQuestHelper class, which is where the
# Lumbridge Guide -> Cook -> accept chain lives before the general splits
# into the nine kitchen subquests). helper_dir stays the shared directory
# name for all ten; helper_file (new QUEUE.tsv column) narrows generation to
# just that one file within it, since gathering the whole directory would
# mix all ten subquests' steps together (new_quest.py's --all still walks
# whole DIRECTORIES and has no seam for this list -- it is read only by
# write_queue and by the test_id-driven single-file CLI path).
RFD_SUBQUESTS = [
    ("rfd_intro", "RFDStart.java", None),
    ("rfd_amikvarze", "RFDSirAmikVarze.java", "amikvarze"),
    ("rfd_dwarf", "RFDDwarf.java", "dwarf"),
    ("rfd_evildave", "RFDEvilDave.java", "evildave"),
    ("rfd_goblins", "RFDGoblins.java", "goblins"),
    ("rfd_lumbridgeguide", "RFDLumbridgeGuide.java", "lumbridgeguide"),
    ("rfd_monkey", "RFDAwowogei.java", "monkey"),
    ("rfd_ogre", "RFDSkrachUglogwee.java", "ogre"),
    ("rfd_pirate", "RFDPiratePete.java", "pirate"),
    ("rfd_finale", "RFDFinal.java", "finale"),
]
RFD_UMBRELLA_DIR = "quest_recipefordisaster"


def write_queue(path: Path, inventory: list[dict], qh_root: Path) -> int:
    """test/quests/QUEUE.tsv: quest_dir test_id helper_dir helper_file tier
    status owner last_failure -- one row per inventory quest (179), except
    quest_recipefordisaster's single row is replaced in place by the ten
    RFD_SUBQUESTS rows above (179 - 1 + 10 = 188 rows total, same count the
    old 179 + 9-appended shape produced). `helper_dir` is this tool's own
    --all mapping (the inverse of match_inventory_row, computed the same
    way so the two never disagree), or "?" when no helper matches
    (quest_inventory.tsv's own rows for fairytalei/fairytaleii-adjacent
    gaps -- see this pass's report). `test_id` is compute_test_id()'s
    result, so this file and new_quest.py's own id-emission logic can never
    silently disagree either."""
    helpers = sorted(p.name for p in qh_root.iterdir() if p.is_dir())
    quest_to_helper: dict[str, str] = {}
    for h in helpers:
        row = match_inventory_row(h, inventory)
        if row is not None and row["dir"] not in quest_to_helper:
            quest_to_helper[row["dir"]] = h

    rows_out = []
    for row in inventory:
        if row["dir"] == RFD_UMBRELLA_DIR:
            for test_id, java_file, suffix in RFD_SUBQUESTS:
                quest_dir = RFD_UMBRELLA_DIR if suffix is None else f"{RFD_UMBRELLA_DIR}_{suffix}"
                # No per-subquest boss/leftover/cutscene signal exists in
                # quest_inventory.tsv (only the umbrella row's own
                # aggregate, which mixes all ten sub-quests together) --
                # tier 5 ("unknown") is the honest call for every one of
                # the ten, intro included, until a later pass researches
                # each individually.
                rows_out.append((quest_dir, test_id, "recipefordisaster", java_file, "5", "todo", "", ""))
            continue
        test_id = compute_test_id(row["dir"])
        rows_out.append((row["dir"], test_id, quest_to_helper.get(row["dir"], "?"), "", row["tier"], "todo", "", ""))

    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(QUEUE_COLUMNS)
        for row in rows_out:
            writer.writerow(row)
    return len(rows_out)


def load_queue(path: Path) -> list[dict]:
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def find_queue_row(rows: list[dict], test_id: str) -> dict | None:
    for row in rows:
        if row.get("test_id") == test_id:
            return row
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument(
        "test_id", nargs="?",
        help="a test_id from test/quests/QUEUE.tsv's 2nd column. Looked up there for "
             "helper_dir/helper_file/quest_dir and written to test/quests/<test_id>.lua "
             "(or --out's directory). Refuses to overwrite an existing file -- pass --force.",
    )
    ap.add_argument(
        "--all", action="store_true",
        help="the old bulk scaffold: generate every helper dir that maps to a content quest, "
             "unchanged by QUEUE.tsv/test_id",
    )
    ap.add_argument(
        "--helper",
        help="with --all, restrict the bulk walk to this one helper dir -- the old bare "
             "positional single-helper-dir form, kept under this flag",
    )
    ap.add_argument("--force", action="store_true", help="overwrite an existing output file")
    ap.add_argument("--qh-root", type=Path, default=DEFAULT_QH)
    ap.add_argument("--content", type=Path, default=DEFAULT_CONTENT)
    ap.add_argument("--queue", type=Path, default=QUEUE_TSV, help="QUEUE.tsv path for the test_id lookup")
    ap.add_argument("--out", type=Path, default=None,
                     help="output dir (default: test/quests for the test_id form, "
                          "build/generated_quests for --all)")
    ap.add_argument("--write-queue", type=Path, default=None,
                     help="write test/quests/QUEUE.tsv (179 inventory rows, quest_recipefordisaster's "
                          "replaced in place by its ten RFD_SUBQUESTS rows) to this path and exit")
    args = ap.parse_args()

    if args.helper and not args.all:
        ap.error("--helper only makes sense with --all (it restricts the bulk walk to one dir)")

    inventory = load_inventory()

    if args.write_queue:
        count = write_queue(args.write_queue, inventory, args.qh_root)
        print(f"wrote {count} rows to {args.write_queue}")
        return 0

    if args.all:
        out_dir = args.out or GEN_DIR
        if args.helper:
            helpers = [args.helper]
        else:
            helpers = sorted(p.name for p in args.qh_root.iterdir() if p.is_dir())
        results = [run_one(h, out_dir, args.qh_root, inventory) for h in helpers]

        no_content = [r for r in results if r["error"] == "no content quest"]
        errored = [r for r in results if r["error"] and r["error"] != "no content quest"]
        written = [r for r in results if r["path"]]

        total_checks = sum(r["checks"] for r in written)
        total_unresolved = sum(r["unresolved"] for r in written)
        total_todo = sum(r["checks_todo"] for r in written)
        total_blocked = sum(r["blocked"] for r in written)
        total_gotos = sum(r["gotos"] for r in written)

        header = (f"{'quest':<28} {'test_id':<20} {'steps':>5} {'checks':>6} {'blocked':>7} "
                  f"{'gotos':>5} {'unresolved':>10} {'CHECK lines':>11}")
        print(header)
        print("-" * len(header))
        for r in written:
            print(f"{r['quest']:<28} {r['test_id']:<20} {r['steps']:>5} {r['checks']:>6} {r['blocked']:>7} "
                  f"{r['gotos']:>5} {r['unresolved']:>10} {r['checks_todo']:>11}")
        print("-" * len(header))
        print(f"{'TOTAL':<28} {'':<20} {sum(r['steps'] for r in written):>5} {total_checks:>6} "
              f"{total_blocked:>7} {total_gotos:>5} {total_unresolved:>10} {total_todo:>11}")
        print()
        print(f"{len(helpers)} quest-helper dirs; {len(written)} generated into {out_dir}; "
              f"{len(no_content)} had no matching content quest; {len(errored)} errored")
        if no_content:
            print("no content quest: " + ", ".join(r["helper"] for r in no_content))
        if errored:
            print("errored: " + ", ".join(f"{r['helper']} ({r['error']})" for r in errored))
        return 0

    if not args.test_id:
        ap.error("test_id required (a QUEUE.tsv row) -- or --all, or --write-queue")

    if not args.queue.is_file():
        print(f"FAILED: no QUEUE.tsv at {args.queue}", file=sys.stderr)
        return 1
    queue_rows = load_queue(args.queue)
    row = find_queue_row(queue_rows, args.test_id)
    if row is None:
        print(f"FAILED: {args.test_id!r} is not a test_id in {args.queue}", file=sys.stderr)
        return 1

    quest_dir = row.get("quest_dir", "")
    helper_dir_name = row.get("helper_dir", "")
    helper_file = (row.get("helper_file") or "").strip()
    if not helper_dir_name or helper_dir_name == "?":
        print(f"FAILED: {args.test_id!r}'s QUEUE.tsv row has no usable helper_dir "
              f"(helper_dir={helper_dir_name!r})", file=sys.stderr)
        return 1

    inventory_by_dir = {r["dir"]: r for r in inventory}
    inv_row = inventory_by_dir.get(quest_dir)
    if inv_row is None and quest_dir.startswith(RFD_UMBRELLA_DIR):
        # An RFD subquest dir (quest_recipefordisaster_<name>) has no row of
        # its own in quest_inventory.tsv -- only the umbrella
        # quest_recipefordisaster does. Borrow its varp/const/tier data (the
        # closest real thing this tool has) rather than refusing outright.
        base = inventory_by_dir.get(RFD_UMBRELLA_DIR)
        if base is not None:
            inv_row = dict(base)
            inv_row["dir"] = quest_dir
    if inv_row is None:
        print(f"FAILED: {quest_dir!r} (from QUEUE.tsv row {args.test_id!r}) is not in "
              f"quest_inventory.tsv", file=sys.stderr)
        return 1

    out_dir = args.out or QUESTS_DIR
    out_path = out_dir / f"{args.test_id}.lua"
    if out_path.exists() and not args.force:
        print(f"FAILED: {out_path} already exists -- pass --force to overwrite", file=sys.stderr)
        return 1

    helper_dir = resolve_helper_dir(helper_dir_name, args.qh_root)
    try:
        lua_text, stats = generate(
            helper_dir, inv_row, args.qh_root, inventory,
            test_id=args.test_id, only_file=helper_file or None,
        )
    except Exception as exc:
        print(f"FAILED: {args.test_id}: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    out_path.write_text(lua_text, encoding="utf-8")
    print(f"wrote {out_path} (quest={quest_dir} steps={stats['steps']} "
          f"checks={stats['checks']} blocked={stats['blocked']} gotos={stats['gotos']} "
          f"unresolved={stats['unresolved']} check_lines={stats['checks_todo']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
