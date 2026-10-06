#!/usr/bin/env python3
"""Grade a quest test against its Quest Helper guide -- the guide is the spec.

    python3 tools/quest_gate/helper_coverage.py <test_id> [--json] [--lua <copy.lua>] [--ledger <ledger.tsv>]
    python3 tools/quest_gate/helper_coverage.py --all-green [--tier N] [--json]

WHY. gate.py checks a ledger's shape and every author/reviewer card checks a
test against its own .rs2. Neither compares the test to the guide, so a port
that narrates a leg in a mes() (Mourning's End II's Temple of Light), or a
test that ::goto's past a gated door or ::gives an item the guide has you
gather, passed review (docs/QUEST_HELPER_COVERAGE_2026-09-23.md). This tool
is the machine version of that audit.

WHAT IT READS.
  * the guide: test/quests/QUEUE.tsv's helper_dir/helper_file, under
    $QUEST_HELPER_ROOT (default: the `quest-helper` checkout beside this
    repo). The step ladder is the guide's own getPanels() list (sub-steps
    folded into their parent); a guide with no panels uses the leaves of its
    steps.put ladder (ConditionalStep.addStep resolved). Every step keeps its
    text, its npc/object/item ids (the gameval names, which ARE the content
    symbols lowercased), its WorldPoint and its steps.put stage.
  * the test: test/quests/<id>.lua -- its code strings, cheats (every "::"
    string in code), goto_tile targets, t.blocked()/content_bug text and
    `-- GUIDE-GAP: <step> <reason>` markers.
  * the ledger: build/quest_gate/<id>/ledger.tsv, else the published one
    under OSRS-Content/.../selftest/quests/<quest_dir>/play/ledger.tsv.
  * the content: the quest's .rs2 under server/scripts/quests/<quest_dir>/,
    every [op*,<symbol>] trigger in the tree, and each debugproc a test calls.

THE LADDER. The guide's getPanels() entries in order (a sibling helper's
`x.panelDetails()` spliced in), sub-steps folded into their parent -- and,
since seam30, every addSubSteps child with a target of its own (at any
depth) ALSO graded as its own step just before that parent: it is a state
QH shows in the parent's place, so one not driven is ALTERNATIVE unless the
run was in that state and teleported out of it (teleported_across: CHEAT;
Nature Spirit's leaveDrezel barrier). A custom
step class beside the guide is graded by its parts: `x.getPanelSteps()` /
`x.getDisplaySteps()` splice its own list (Recruitment Drive's Miss Cheevers
gather chain), a ConditionalStep subclass stands for its addStep children, an
NpcStep/ObjectStep subclass takes its super(...) target; lists grown with
`.add/.addAll` and `panel.addSteps(...)` count, puzzle-wrapper aliases
(`pw = step.puzzleWrapStep()`) resolve, and a leaf is graded once. A panel
entry that is a ConditionalStep stands for its leaves, each its own step,
except leaves that differ from a sibling in one name word (compareAnna /
compareDavid: one step, done any way) and *Fallback leaves (optional).
Panels built twice in an if/else (Heroes' Quest's two gang routes) are
alternatives: the branch the test drove most is the spec. Since seam36 every
leaf the steps.put ConditionalStep tree can show that no panel lists (not a
folded sub-step, not a custom class whose own panels were spliced) is a
BRANCH-ONLY step, placed before the state it leads to and graded like a
promoted sub-step (Grader._grade_branch): ALTERNATIVE/TRAVEL when not driven,
CHEAT when the ledger shows a goto from the zone its addStep condition names
to a zone a later sibling is shown in without pressing its gated loc
(zone_crossing; Monkey Madness's enterGate, the Bamboo Gate).

CLASSES (first rule that fires wins, in this order).
  CHEAT        a quest debugproc named after the step (::mortton_repairtemple
               for repairTemple) -- before anything else. DRIVEN instead when
               docs/QUEST_SERVER_CHEATS.md section A sanctions it as a GRIND
               fast-forward (parsed from the doc's "GRIND fast-forward"
               bullet) AND a t.check/t.expect/t.msg.expect/t.var/t.inv/
               t.quest.stage read follows within 12 lines.
  CHEAT        a PASS row the driver's reach retry got by standing on the
               loc's own square with ::goto ("stood on with ::goto" in the
               detail) whose loc is a target of the step or whose row is
               named after it. Since seam10 the driver stands on a square
               only for a call carrying `stand_on_square = true`; that
               opt-in with a `-- GUIDE-GAP:` marker within 8 lines above it
               citing the .rs2 line or map square (m<x>_<z>.jm2) that says
               no route ends elsewhere grades the step CONTENT_GAP (a
               declared guide gap, which gate.py accepts); a bare opt-in, or
               a stand-on with no opt-in in the Lua at all, is CHEAT. A
               stand-on no step claims, and an opt-in with no marker beside
               it, are gate findings of their own. A loc crossed more than
               once (Mountain Daughter's lake rocks) claims each later
               crossing by that call's own cited marker naming the guide
               step -- panel or ConditionalStep-only -- on the same loc
               (Grader.claim_marked_crossings).
  CONTENT_GAP  the content's own soft-skip comment names the step as
               collapsed (mend1_sheep.rs2's `getToads`), even when the test
               drove the stand-in.
  DRIVEN       a PASS row named after the step (never a goto-/quest.* row;
               its puzzle-wrapper alias counts), or an action (talk_to/
               click_loc/use_on/...) or action row naming one of its npc/loc/
               obj symbols -- rev 239's name, a multinpc parent, or its display
               name; a "use X on Y" step needs a use_on of X (its guide
               alternates, dose/noted variants: obj_family) on Y whose row
               PASSed and shows an effect (an item lost, a var, a page, a
               server line, a landing: _use_effect; a bare call with no row
               of its own drives nothing, b63-seam1) -- nothing else drives
               it (use_item_driven; b57), a pick-up step
               may be done by another route (buy-rum), and a door clicked once
               is one step, not two. Since seam26 a mention counts only when
               (i) its line/row is not ANOTHER guide step's own row (the first
               row named after that step and its non-pressing same-visit rows:
               The Feud's `pickpocketVillager1` press does not drive
               `talkToAVillager`), and (ii) it presses the step's own op when
               the step's leading verb is one of the target's menu ops
               (all.npc/all.loc opN=; op3 Pickpocket is not "Talk to"). A named
               row pressing the wrong op does not count either. An Open/Close/
               Lock/Unlock ConditionalStep leaf on a loc already in that state
               is met by the earlier press (state_already_set).
  EQUIVALENT   a VERIFIED `-- BRANCH-IN:` / `-- PARTNER:` / `-- NOT-A-STEP:` /
               `-- OBSOLETE:` / `-- ANY-OF:` marker names the step: not a gap
               (the sibling drives it, a partner cheat does it, it is a plugin
               sync, the live game removed it, it was done another way). See
               the "equivalent markers" banner below; neutral, like DRIVEN.
  CONTENT_GAP  a t.blocked()/content_bug line or a `-- GUIDE-GAP:` marker
               names the step (or a ConditionalStep above it); the marker
               counts only when its reason cites a real `<file>.rs2:<line>`.
  CHEAT        a ::give / ::bankgive / debugproc inv_add of an item the step
               OBTAINS (::bankgive is graded exactly as ::give: same item,
               same verdict -- GIVE_CHEATS)
               (pick/buy/kill/mine... -- a bring-along only when the step's
               whole job is picking it up), or a ::setvar / ::complete /
               debugproc write of a quest var the step names.
  BRING_ALONG  the step only obtains (or makes a precursor of) an item the
               guide lists as a bring-along (getItemRequirements, not
               canBeObtainedDuringQuest) and the test gives it.
  CONTENT_GAP  the quest's .rs2 writes a stage in a narrating branch (mes()
               lines, inventory, gates -- no interaction of its own; a
               dialogue-only branch counts for a "use X on npc" step in that
               npc's script) whose text names the step; a soft-skip comment
               names it; a two-player step; a talk whose [opnpc] never reads
               the quest varp (Reldo for the Black Arm Gang); or none of its
               npc/loc symbols has a trigger that serves this quest (another
               quest's loc counts only when that quest is a prerequisite).
  CHEAT        (overrides every class but CONTENT_GAP/EQUIVALENT, for each
               leaf a ladder step stands for, and adds a row for a
               ConditionalStep leaf no ladder step lists) a goto on one level
               of one map frame that left the zone where a ConditionalStep
               shows an obstacle step (door, rockslide, spear trap, log,
               pitfall, tripwire...) for another state of that route, with
               no press of that obstacle's copy between (route_crossing;
               Regicide's pit-to-lever hop past climbOverRockslide4); or a
               goto from one island of a route's zones to another, landing
               where the route's entry ConditionalStep leads, without
               pressing that entry's obstacles (route_entries; Regicide's
               voyage-cave-to-pit re-entry charged to crossTheBridge); or a
               goto from outside a building into the room the door a
               ConditionalStep's DEFAULT step names opens on, without
               pressing it (door_entries; Black Knights' Fortress's
               Falador-to-entrance hop past enterFortress); or a goto out of
               a room an obstacle step's door opens on to a nearby tile in no
               zone, after which the step the guide shows only inside that
               room is driven (room_exits; Heroes' Quest's hop out of the
               secret room to melee Grip, charged to killGrip); or a goto
               that lands in a room the MAP walls in (maps/*.jl2 walls and
               blocked tiles, MapWalls; a door loc in its perimeter) from a
               departure outside it, with no press of that door in the 500
               ticks before -- whether or not the guide names the door,
               charged to the step the goto serves (enclosure_entries; Tale
               of the Righteous's returnToPhileasTent past Phileas's house
               door); or a
               PASS row named after an obstacle step whose newest server
               line is that attempt's failure (stale_success: passTrap5-tile
               on "...and fail, activating the trap!"); or (b63-seam1) a goto
               whose ends no walk joins with every door shut while every
               walk opens the same gate at margins 30/80/160, that gate not
               pressed in the 500 ticks before (gate_crossings: Dwarf
               Cannon's railing yard, the Taverley members' gate); or a goto
               that lands ON a blocked tile, a step of its own "(goto onto
               <loc>)" (solid_landings: goto-gotoCave onto mcannoncave).
  CHEAT        a goto_tile (or ::goto) that lands within reach of a door/
               gate/wall/fence/stair/puzzle loc the step names, when that loc
               has a trigger and no action clicked it; or a journey the
               content implements (Port Sarim's seaman) replaced by a goto.
               A goto over a leg of a stage the port narrates is that
               stage's CONTENT_GAP instead.
  TRAVEL       a ladder/stair/trapdoor climb (or "Travel to ...") with no
               trigger that writes a quest var, merged into the next step.
  ALTERNATIVE  the other branch of an if/else panel, or a fallback.
  UNMATCHED    nothing above -- reported, never guessed. In a stage most of
               whose steps are narrated, it joins them as CONTENT_GAP.

VERDICT. FULL (every step DRIVEN/EQUIVALENT/TRAVEL/BRING_ALONG/ALTERNATIVE), CONTENT_GAP (the only
gaps are CONTENT_GAP), TEST_GAP (the only gaps are CHEAT/UNMATCHED), MIXED
(both). gate.py grades a green run with `gate_findings()`: FULL, or every gap
a CONTENT_GAP that the file itself declares (t.blocked/content_bug/GUIDE-GAP).

Calibrated against tools/quest_gate/helper_coverage.tsv (the human audit, the
reference): `--calibrate` prints the agreement -- 35/39 on 2026-09-23. It
reads the OSRS-Content WORKING TREE, so a content port in flight moves its
answers (Throne of Miscellania's 75% support became real content after the
audit, and the tool now calls that leg the test's).
"""

# Same sys.path scrub as gate.py: this directory holds queue.py, which
# shadows the stdlib module for anything that imports concurrent.futures.
import os as _os
import sys as _sys
_HERE = _os.path.dirname(_os.path.abspath(__file__))
_sys.path[:] = [_p for _p in _sys.path if _os.path.abspath(_p or ".") != _HERE]
_sys.path.append(_HERE)

import argparse
import copy
import csv
import json
import os
import re
import sys
import heapq
from collections import deque

HERE =os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))

import ledger  # noqa: E402

QUEUE_PATH = os.path.join(REPO_ROOT, "test", "quests", "QUEUE.tsv")
AUDIT_PATH = os.path.join(HERE, "helper_coverage.tsv")
CONTENT_ROOT = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts")
QUEST_HELPER_ROOT = os.environ.get(
    "QUEST_HELPER_ROOT",
    os.path.join(os.path.dirname(REPO_ROOT), "quest-helper"))
# A worktree sits beside the main checkout, so the sibling may be one level up.
if not os.path.isdir(QUEST_HELPER_ROOT):
    QUEST_HELPER_ROOT = os.path.join(os.path.dirname(REPO_ROOT), "..", "quest-helper")
GUIDES_DIR = os.path.join(QUEST_HELPER_ROOT, "src", "main", "java", "com", "questhelper",
                          "helpers", "quests")

GAP_CLASSES_TEST = ("CHEAT", "UNMATCHED")
NEUTRAL_CLASSES = ("DRIVEN", "EQUIVALENT", "TRAVEL", "BRING_ALONG", "ALTERNATIVE")

# The cheats that hand the player an item by NAME, graded alike (_cheat_effects,
# bring_along): `::give <obj> <n>` into the pack and `::bankgive <obj> <n>`
# into the bank (cheat_bank.rs2, setup-only). A banked item is one t.bank.
# withdraw away from the pack, so a setup ::bankgive of an item the guide has
# the player OBTAIN is the same CHEAT as a ::give of it. ::bankgive's body is
# `inv_add(bank, $stored, ...)` -- a local, not a symbol -- so the debugproc
# reader below would see no item and drop it as a reset adapter; it must be
# named here, before that branch. helper_coverage_bankgive_test.py.
GIVE_CHEATS = ("give", "bankgive")

# A guide step that USES an item on its target ("Use the serum 208 on
# Razmire") is driven only by a use_on of that item -- or one of its family:
# the guide's alternates and ItemCollections, dose/charge variants, the noted
# form (Grader.use_item_wanted / use_item_driven). Before, any action naming
# the target drove it, and Shades of Mort'ton's serum 207 cures drove both
# serum 208 steps (sampler b57 (a)). QUEST_GATE_USE_ITEM_RULE=0 switches the
# rule off, for measuring it (helper_coverage_use_item_test.py).
USE_ITEM_RULE = os.environ.get("QUEST_GATE_USE_ITEM_RULE", "1") != "0"
USE_CLAUSE = re.compile(r"\buse\s+(.+?)\s+(?:on|onto|with|in|into)\s+(.+?)(?=[,.;!?()]|\bthen\b|\band\b|\bto\b|$)",
                        re.I | re.S)
LOST_RE = re.compile(r"\blost ([^\]]*)")

# ------------------------------------------------------------------ words

STOPWORDS = set("""
a an the and or of to in on at by for from with into onto over under up down out off
you your yours he she it its they them their there here this that these those then than
is are be been being was were will would can could should may might must do does did
not no nor so if as but all any each every some more most much many few one two three
four five six seven eight nine ten first second third next last again back also only
talk speak ask tell give get go goes going went come return use using used make made
take bring find search open close enter leave climb walk head run pick grab have has
about after before near next north south east west north-west north-east south-west
south-east northwest northeast southwest southeast inside outside around through side
room floor level top bottom ground way area part place spot point time times
now still just once while when where which who whom what why how
quest step steps guide player players need needs needed want wants
him her his hers himself herself yourself myself something anything
new old other another same own right left
""".split())


def words(text):
    """Significant lowercase words of `text` (possessives stripped)."""
    out = []
    for word in re.findall(r"[A-Za-z][A-Za-z']+", text or ""):
        word = word.lower().replace("'s", "").replace("'", "")
        if len(word) < 3 or word in STOPWORDS:
            continue
        out.append(stem(word))
    return out


def stem(word):
    """A crude stem, enough that raking/rake, delivery/deliver, pillars/pillar meet."""
    for suffix in ("ing", "ies", "ed", "es", "s", "y", "e", "er"):
        if word.endswith(suffix) and len(word) - len(suffix) >= 3 and not word.endswith("ss"):
            word = word[:-len(suffix)]
            break
    if word.endswith("er") and len(word) > 5:
        word = word[:-2]
    return word


def camel_words(name):
    """`talkToUnferth` -> ['talk', 'to', 'unferth'] filtered like words()."""
    spaced = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", name or "").replace("_", " ")
    spaced = re.sub(r"(\d+)", r" \1 ", spaced)
    return words(spaced)


def norm(name):
    return re.sub(r"[^a-z0-9]", "", (name or "").lower())


# ------------------------------------------------------------------ java

def strip_java_comments(text):
    """`text` with // and /* */ comments blanked (newlines kept, strings kept)."""
    out = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char == '"' or char == "'":
            quote = char
            out.append(char)
            index += 1
            while index < length:
                current = text[index]
                out.append(current)
                index += 1
                if current == "\\" and index < length:
                    out.append(text[index])
                    index += 1
                elif current == quote or current == "\n":
                    break
            continue
        if text.startswith("//", index):
            while index < length and text[index] != "\n":
                index += 1
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            out.append("".join(c if c == "\n" else " " for c in text[index:end]))
            index = end
            continue
        out.append(char)
        index += 1
    return "".join(out)


def matching_close(text, open_index):
    """Index of the bracket closing the one at `open_index` (strings skipped)."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    stack = [pairs[text[open_index]]]
    index = open_index + 1
    length = len(text)
    while index < length:
        char = text[index]
        if char == '"':
            index += 1
            while index < length and text[index] != '"':
                index += 2 if text[index] == "\\" else 1
        elif char in pairs:
            stack.append(pairs[char])
        elif stack and char == stack[-1]:
            stack.pop()
            if not stack:
                return index
        index += 1
    return length - 1


def split_top(argtext):
    """Split a call's argument text at top-level commas."""
    args = []
    depth = 0
    current = []
    index = 0
    while index < len(argtext):
        char = argtext[index]
        if char == '"':
            end = index + 1
            while end < len(argtext) and argtext[end] != '"':
                end += 2 if argtext[end] == "\\" else 1
            current.append(argtext[index:end + 1])
            index = end + 1
            continue
        if char in "([{<" and not (char == "<" and depth == 0 and index and argtext[index - 1] == " "):
            depth += 1
        elif char in ")]}>" and depth > 0:
            depth -= 1
        if char == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(char)
        index += 1
    if "".join(current).strip():
        args.append("".join(current).strip())
    return args


def string_text(arg):
    """The concatenated string literal(s) of an argument, '' if none."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', arg or "")
    return "".join(p.replace('\\"', '"').replace("\\n", " ") for p in parts)


ID_RE = re.compile(r"\b(NpcID|ObjectID|ItemID|NullObjectID)\.([A-Z0-9_]+)")
KIND_OF = {"NpcID": "npc", "ObjectID": "loc", "NullObjectID": "loc", "ItemID": "obj"}
STEP_CTOR_RE = re.compile(r"\b(\w+)\s*=\s*new\s+(\w+)\s*(?:<[^>]*>)?\s*\(")
POINT_RE = re.compile(r"new\s+WorldPoint\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)")


_ITEM_COLLECTIONS = None


def item_collection(name):
    """The obj symbols of Quest Helper's `ItemCollections.<name>` (AXES,
    COINS, ...), as the guide's ItemID constants lowercased; [] when the
    collection or the file is not there."""
    global _ITEM_COLLECTIONS
    if _ITEM_COLLECTIONS is None:
        _ITEM_COLLECTIONS = {}
        path = os.path.join(QUEST_HELPER_ROOT, "src", "main", "java", "com", "questhelper", "collections",
                            "ItemCollections.java")
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                code = strip_java_comments(handle.read())
            for match in re.finditer(r"^\s*([A-Z][A-Z0-9_]*)\s*\(", code, re.M):
                open_index = match.end() - 1
                body = code[open_index + 1:matching_close(code, open_index)]
                _ITEM_COLLECTIONS.setdefault(match.group(1), [
                    const.lower().lstrip("_") for cls, const in ID_RE.findall(body) if cls == "ItemID"])
    return list(_ITEM_COLLECTIONS.get(name, ()))


class Step:
    def __init__(self, name, kind, line):
        self.name = name
        self.kind = kind
        self.line = line
        self.text = ""
        self.targets = []       # (kind, symbol)
        self.req_vars = []      # ItemRequirement variable names among the args
        self.point = None
        self.children = []      # ConditionalStep: sub-step names
        self.substeps = []      # addSubSteps: folded into this one
        self.promoted_targets = set()  # targets folded in from a promoted sub-step
        self.stage = None
        self.dialog = []
        self.panel = ""
        self.alt_group = None
        self.req_raw = []       # the requirement idents as written (`serum208Highlighted`), before item_base
        self.icons = []         # obj symbols of `step.addIcon(ItemID.X)`

    def is_composite(self):
        return "Conditional" in self.kind or self.kind in ("ReorderableConditionalStep",)


class Guide:
    def __init__(self, path, nested=False):
        self.path = path
        self._nested = nested
        self.spliced_stage = {}  # sub-guide step -> the owner step it came from
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            self.raw = handle.read()
        self.code = strip_java_comments(self.raw)
        self.steps = {}
        self.items = {}          # var -> {"name":, "ids": [...], "obtainable": bool}
        self.aliases = {}        # var -> var it was derived from
        self.id_arrays = {}      # var -> [(kind, symbol)]
        self.item_requirements = []
        self.item_recommended = []
        self.stage_puts = []     # (value, step var)
        self.panels = []         # (title, [step vars])
        self._parse()
        # each step's targets as the guide wrote them, before ladder() folds
        # its sub-steps' in (enterFortress stands for the castle stairs too)
        self.own_targets = {name: list(step.targets) for name, step in self.steps.items()}

    def line_of(self, index):
        return self.code.count("\n", 0, index) + 1

    def _ids_in(self, text):
        found = []
        for cls, const in ID_RE.findall(text):
            # Java names cannot start with a digit: _0_41_53_SINISTERFISHSPOT.
            found.append((KIND_OF[cls], const.lower().lstrip("_")))
        for ident in re.findall(r"\b([a-z]\w*)\b", text):
            if ident in self.id_arrays:
                found.extend(self.id_arrays[ident])
        return found

    def _parse(self):
        code = self.code
        # int[] / List<Integer> id arrays: `var shopkeepers = new int[]{NpcID.A, NpcID.B};`
        for match in re.finditer(r"\b(\w+)\s*=\s*(?:new\s+int\s*\[\s*\]\s*\{|List\.of\(|Arrays\.asList\()", code):
            start = code.find("{" if "{" in match.group(0) else "(", match.start() + len(match.group(1)))
            end = matching_close(code, start)
            body = code[start + 1:end]
            ids = [(KIND_OF[c], k.lower().lstrip("_")) for c, k in ID_RE.findall(body)]
            if ids and not re.search(r"new\s+\w+Step", body):
                self.id_arrays[match.group(1)] = ids
        # Item requirements.
        for match in STEP_CTOR_RE.finditer(code):
            var, ctor = match.group(1), match.group(2)
            if ctor not in ("ItemRequirement", "ItemRequirements", "FollowerItemRequirement",
                            "KeyringRequirement", "TeleportItemRequirement"):
                continue
            open_index = match.end() - 1
            args = split_top(code[open_index + 1:matching_close(code, open_index)])
            self.items[var] = {
                "name": string_text(args[0]) if args else "",
                "ids": [s for k, s in self._ids_in(",".join(args[1:])) if k == "obj"],
                "obtainable": False,
            }
            # ItemRequirements(a, b) groups other requirement vars.
            if ctor == "ItemRequirements":
                for arg in args:
                    base = re.match(r"\s*(\w+)", arg)
                    if base and base.group(1) in self.items:
                        self.items[var]["ids"].extend(self.items[base.group(1)]["ids"])
        for match in re.finditer(r"\b(\w+)\s*=\s*(\w+)\s*\.\s*(?:quantity|highlighted|named|hideConditioned|showConditioned|alsoCheckBank|equipped|copy)\b", code):
            if match.group(2) in self.items and match.group(1) not in self.items:
                self.aliases[match.group(1)] = match.group(2)
        # Every id a requirement accepts, for a "use X on Y" step's item
        # (Grader.use_item_wanted): the ctor's ids, an ItemCollections list,
        # `addAlternates(...)`. Kept apart from `ids`, which the other rules
        # read as the ctor's own ids.
        for var, item in self.items.items():
            item["alts"] = list(item["ids"])
        for match in STEP_CTOR_RE.finditer(code):
            var = match.group(1)
            if match.group(2) in ("ItemRequirement", "FollowerItemRequirement", "KeyringRequirement",
                                  "TeleportItemRequirement") and var in self.items:
                open_index = match.end() - 1
                body = code[open_index + 1:matching_close(code, open_index)]
                for name in re.findall(r"\bItemCollections\s*\.\s*(\w+)", body):
                    self.items[var]["alts"].extend(item_collection(name))
        for match in re.finditer(r"\b(\w+)\s*\.\s*addAlternates\s*\(", code):
            base = self.item_base(match.group(1))
            if base in self.items:
                open_index = match.end() - 1
                body = code[open_index + 1:matching_close(code, open_index)]
                self.items[base]["alts"].extend(s for k, s in self._ids_in(body) if k == "obj")
                for name in re.findall(r"\bItemCollections\s*\.\s*(\w+)", body):
                    self.items[base]["alts"].extend(item_collection(name))
        for match in STEP_CTOR_RE.finditer(code):
            if match.group(2) == "ItemRequirements" and match.group(1) in self.items:
                open_index = match.end() - 1
                for arg in split_top(code[open_index + 1:matching_close(code, open_index)]):
                    base = re.match(r"\s*(\w+)", arg)
                    if base and base.group(1) in self.items and base.group(1) != match.group(1):
                        self.items[match.group(1)]["alts"].extend(self.items[base.group(1)]["alts"])
        # The requirement the step highlights in the inventory is the one it uses.
        self.highlighted = set()
        for match in re.finditer(r"\b(\w+)\s*\.\s*setHighlightInInventory\s*\(\s*true\s*\)", code):
            self.highlighted.add(match.group(1))
        for match in re.finditer(r"\b(\w+)\s*=\s*\w+\s*\.\s*highlighted\s*\(", code):
            self.highlighted.add(match.group(1))
        for match in re.finditer(r"\b(\w+)\s*\.\s*canBeObtainedDuringQuest\s*\(", code):
            base = self.item_base(match.group(1))
            if base in self.items:
                self.items[base]["obtainable"] = True
        self.item_requirements = self._method_idents("getItemRequirements")
        self.item_recommended = self._method_idents("getItemRecommended")

        # String constants a step's text may be passed as (`getSupport`).
        self.string_vars = {}
        for match in re.finditer(r"\b(\w+)\s*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*\+?\s*)+);", code):
            self.string_vars[match.group(1)] = string_text(match.group(2))
        self.prerequisites = [q.lower().replace("_", "") for q in
                              re.findall(r"QuestRequirement\(\s*QuestHelperQuest\.(\w+)", code)]
        # Steps.
        for match in STEP_CTOR_RE.finditer(code):
            var, ctor = match.group(1), match.group(2)
            if not (ctor.endswith("Step") or ctor.endswith("Steps")):
                continue
            open_index = match.end() - 1
            close = matching_close(code, open_index)
            argtext = code[open_index + 1:close]
            args = split_top(argtext)
            step = Step(var, ctor, self.line_of(match.start()))
            if step.is_composite():
                if len(args) > 1:
                    child = re.match(r"\s*(\w+)", args[1])
                    if child:
                        step.children.append(child.group(1))
                step.text = next((string_text(a) for a in args[1:] if string_text(a)), "")
            elif ctor == "PuzzleWrapperStep":
                inner = args[1] if len(args) > 1 else ""
                inner_ctor = re.match(r"\s*new\s+(\w+)\s*\(", inner)
                if inner_ctor:
                    step.kind = inner_ctor.group(1)
                    inner_args = split_top(inner[inner.find("(") + 1:inner.rfind(")")])
                    self._fill_leaf(step, inner_args)
                    if len(args) > 2 and string_text(args[2]):
                        step.text = step.text or string_text(args[2])
                else:
                    ident = re.match(r"\s*(\w+)", inner)
                    step.kind = "wrap:" + (ident.group(1) if ident else "")
                    step.text = string_text(",".join(args[2:]))
            else:
                self._fill_leaf(step, args)
            # `x = new ObjectStep(...).puzzleWrapStep("text")`
            tail = code[close + 1:close + 200]
            wrap = re.match(r"\s*\.\s*puzzleWrapStep\s*\(\s*(true|false)?\s*,?\s*(\"(?:[^\"\\]|\\.)*\")?", tail)
            if wrap and wrap.group(2) and not step.text:
                step.text = string_text(wrap.group(2))
            if var not in self.steps or not self.steps[var].text:
                self.steps[var] = step
        # Resolve wrap:<var> steps to the wrapped step's targets.
        for step in self.steps.values():
            if step.kind.startswith("wrap:"):
                inner = self.steps.get(step.kind[5:])
                if inner:
                    step.targets = list(inner.targets)
                    step.point = inner.point
                    step.text = step.text or inner.text
                    step.kind = inner.kind
        # composite -> [(child, condition text)] in addStep order: QH shows
        # the FIRST child whose condition holds, so a child listed earlier is
        # a state further along than one listed after it (seam36).
        self.branch_conds = {}
        for match in re.finditer(r"\b(\w+)\s*\.\s*addStep\s*\(", code):
            open_index = match.end() - 1
            args = split_top(code[open_index + 1:matching_close(code, open_index)])
            if match.group(1) in self.steps and args:
                child = re.match(r"\s*(\w+)", args[-1])
                if child:
                    self.steps[match.group(1)].children.append(child.group(1))
                    self.branch_conds.setdefault(match.group(1), []).append(
                        (child.group(1), ",".join(args[:-1]).strip()))
        self._parse_zones()
        for match in re.finditer(r"\b(\w+)\s*\.\s*addSubSteps\s*\(", code):
            open_index = match.end() - 1
            args = split_top(code[open_index + 1:matching_close(code, open_index)])
            if match.group(1) in self.steps:
                for arg in args:
                    for ident in re.findall(r"\b([a-z]\w*)\b", arg):
                        if ident in self.steps:
                            self.steps[match.group(1)].substeps.append(ident)
        for match in re.finditer(r"\b(\w+)\s*\.\s*(addAlternateNpcs|addAlternateObjects)\s*\(", code):
            open_index = match.end() - 1
            body = code[open_index + 1:matching_close(code, open_index)]
            if match.group(1) in self.steps:
                self.steps[match.group(1)].targets.extend(self._ids_in(body))
        for match in re.finditer(r"\b(\w+)\s*\.\s*addIcon\s*\(", code):
            open_index = match.end() - 1
            body = code[open_index + 1:matching_close(code, open_index)]
            if match.group(1) in self.steps:
                self.steps[match.group(1)].icons.extend(s for k, s in self._ids_in(body) if k == "obj")
        for match in re.finditer(r"\b(\w+)\s*\.\s*addDialogSteps?\s*\(", code):
            open_index = match.end() - 1
            body = code[open_index + 1:matching_close(code, open_index)]
            if match.group(1) in self.steps:
                self.steps[match.group(1)].dialog.extend(
                    string_text(a) for a in split_top(body) if string_text(a))
        for match in re.finditer(r"\b(\w+)\s*\.\s*setText\s*\(", code):
            open_index = match.end() - 1
            body = code[open_index + 1:matching_close(code, open_index)]
            step = self.steps.get(match.group(1))
            if step and not step.text and string_text(body):
                step.text = string_text(body)
        for match in re.finditer(r"\bsteps\s*\.\s*put\s*\(\s*(-?\d+)\s*,\s*(\w+)\s*\)", code):
            self.stage_puts.append((int(match.group(1)), match.group(2)))
        # `pwGetMagnet = getMagnet.puzzleWrapStep(true)`: the panel lists the
        # wrapper, the step is the wrapped one (resolve()).
        self.step_alias = {}
        for match in re.finditer(r"\b(\w+)\s*=\s*(\w+)\s*\.\s*puzzleWrapStep\w*\s*\(", code):
            if match.group(1) not in self.steps and match.group(2) != "this":
                self.step_alias[match.group(1)] = match.group(2)
        # A custom step CLASS's own `addStep(cond, x)` / `this.addStep(...)`
        # children (MissCheeversStep extends ConditionalStep): read when this
        # file is parsed as a sibling (Guide.sibling).
        self.class_children = []
        for match in re.finditer(r"(?<![\w.])(?:this\s*\.\s*)?addStep\s*\(", code):
            open_index = match.end() - 1
            args = split_top(code[open_index + 1:matching_close(code, open_index)])
            child = re.match(r"\s*(\w+)", args[-1]) if args else None
            if child and self.resolve(child.group(1)) not in self.class_children:
                self.class_children.append(self.resolve(child.group(1)))
        self.extends = ""
        extends = re.search(r"\bclass\s+\w+\s+extends\s+(\w+)", code)
        if extends:
            self.extends = extends.group(1)
        self._siblings = {}
        self._parse_panels()

    # -- zones (seam36): which side of a gate a ConditionalStep branch is shown on

    @staticmethod
    def _zone_box(body):
        """`new Zone(<body>)` as (min_x, max_x, min_y, max_y, min_plane,
        max_plane), or None for a form this does not read (zone.dz(1))."""
        points = [tuple(int(v) for v in p) for p in POINT_RE.findall(body)]
        if len(points) >= 2:
            (x1, y1, p1), (x2, y2, p2) = points[0], points[1]
            return (min(x1, x2), max(x1, x2), min(y1, y2), max(y1, y2), min(p1, p2), max(p1, p2))
        if len(points) == 1:
            x, y, p = points[0]
            return (x, x, y, y, p, p)
        region = re.match(r"\s*(\d+)\s*(?:,\s*(\d+)\s*)?$", body)
        if region:
            # Zone(int regionID[, int plane]): REGION_SIZE is 64, planes 0..2
            rid = int(region.group(1))
            x, y = ((rid >> 8) & 0xFF) << 6, (rid & 0xFF) << 6
            plane = (int(region.group(2)),) * 2 if region.group(2) else (0, 2)
            return (x, x + 64, y, y + 64) + plane
        return None

    def _zone_req_boxes(self, body):
        """The boxes a `new ZoneRequirement(<body>)` is satisfied in, or None
        when it is a NOT-in-zone check (ZoneRequirement(false, zone) /
        ZoneRequirement("text", true, zone)) or names no zone."""
        args = split_top(body)
        if not args:
            return None
        if string_text(args[0]) and len(args) >= 3 and args[1].strip() == "true":
            return None
        if args[0].strip() == "false":
            return None
        boxes = []
        for arg in args:
            arg = arg.strip()
            if arg in self.zones:
                boxes.extend(self.zones[arg])
                continue
            inline = re.match(r"new\s+Zone\s*\(", arg)
            if inline:
                box = self._zone_box(arg[inline.end():matching_close(arg, inline.end() - 1)])
                if box:
                    boxes.append(box)
                continue
            point = POINT_RE.match(arg)
            if point:
                x, y, p = (int(v) for v in point.groups())
                boxes.append((x, x, y, y, p, p))
        return boxes or None

    def _parse_zones(self):
        code = self.code
        self.zones = {}      # Zone var -> [box]
        for match in re.finditer(r"\b(\w+)\s*=\s*new\s+Zone\s*\(", code):
            open_index = match.end() - 1
            box = self._zone_box(code[open_index + 1:matching_close(code, open_index)])
            if box:
                self.zones[match.group(1)] = [box]
        self.zone_reqs = {}  # positive ZoneRequirement var -> [box]
        for match in re.finditer(r"\b(\w+)\s*=\s*new\s+ZoneRequirement\s*\(", code):
            open_index = match.end() - 1
            boxes = self._zone_req_boxes(code[open_index + 1:matching_close(code, open_index)])
            if boxes:
                self.zone_reqs[match.group(1)] = boxes

    def condition_zones(self, text):
        """{name: [box]} of the positive zone requirements a ConditionalStep
        condition holds the player in: a ZoneRequirement var or an inline
        `new ZoneRequirement(...)`; anything under not(...)/nor/nand is
        skipped (the player is OUTSIDE it)."""
        if not text or re.search(r"LogicType\s*\.\s*N(?:OR|AND)\b", text):
            return {}
        while True:
            negated = re.search(r"\bnot\s*\(|\bnor\s*\(|\bnand\s*\(", text)
            if not negated:
                break
            open_index = negated.end() - 1
            text = text[:negated.start()] + text[matching_close(text, open_index) + 1:]
        out = {}
        for inline in re.finditer(r"new\s+ZoneRequirement\s*\(", text):
            open_index = inline.end() - 1
            body = text[open_index + 1:matching_close(text, open_index)]
            boxes = self._zone_req_boxes(body)
            if boxes:
                out["ZoneRequirement(%s)" % " ".join(body.split())[:40]] = boxes
        for ident in re.findall(r"\b([a-z]\w*)\b", text):
            if ident in self.zone_reqs:
                out[ident] = self.zone_reqs[ident]
        return out

    def progression(self, composite):
        """A ConditionalStep's children in the order a player meets them: the
        default (constructor) child, then the addStep children last-listed
        first (QH shows the first child whose condition holds)."""
        step = self.steps.get(composite)
        if step is None or not step.children:
            return []
        default = step.children[0]
        added = [child for child, _ in self.branch_conds.get(composite, [])]
        return [default] + list(reversed(added))

    def branch_label(self, leaf):
        """`bringMonkey[onApeAtollSouth]`: the ConditionalStep a branch-only
        step is shown from and the condition it is shown on."""
        found = self.branch_of.get(leaf) if hasattr(self, "branch_of") else None
        if not found:
            return ""
        composite, condition, _ = found
        idents = [i for i in re.findall(r"\b([a-z]\w*)\b", condition or "")
                  if i not in ("new", "true", "false", "and", "or", "not", "this")]
        return "%s[%s]" % (composite, "+".join(idents)[:60] if condition is not None else "default")

    def _branch_index(self):
        """leaf -> (composite, condition text or None for the default child,
        index in the composite's addStep list or None): the FIRST ConditionalStep
        whose child (or a composite child's leaf) the leaf is, walked from the
        steps.put ladder in stage order."""
        out = {}
        seen = set()

        def walk(composite):
            if composite in seen or composite not in self.steps or not self.steps[composite].is_composite():
                return
            seen.add(composite)
            step = self.steps[composite]
            entries = [(step.children[0], None, None)] if step.children else []
            entries += [(child, cond, index) for index, (child, cond) in
                        enumerate(self.branch_conds.get(composite, []))]
            for child, cond, index in entries:
                child = self.resolve(child)
                if child not in self.steps:
                    continue
                if self.steps[child].is_composite():
                    walk(child)
                else:
                    out.setdefault(child, (composite, cond, index))
        for _, var in sorted(self.stage_puts, key=lambda p: p[0]):
            walk(self.resolve(var))
        return out

    def resolve(self, ident):
        """A puzzle-wrapper alias's wrapped step (pwGetMagnet -> getMagnet)."""
        seen = set()
        while ident in getattr(self, "step_alias", {}) and ident not in seen:
            seen.add(ident)
            ident = self.step_alias[ident]
        return ident

    def sibling(self, owner):
        """The Guide parsed from the sibling .java of step `owner`'s custom
        class (MissCheeversStep.java beside RecruitmentDrive.java), or None."""
        step = self.steps.get(owner)
        if step is None or self._nested:
            return None
        kind = step.kind
        kind = getattr(step, "class_kind", kind)
        if kind in self._siblings:
            return self._siblings[kind]
        path = os.path.join(os.path.dirname(self.path), kind + ".java")
        sub = Guide(path, nested=True) if os.path.isfile(path) and path != self.path else None
        if sub is not None:
            # A sibling step named like one of ours (SlugSteps' own
            # talkToPete) stays the sibling's: our step keeps the name, and no
            # sibling step folds or nests ours by it.
            collided = {name for name, sub_step in sub.steps.items()
                        if name in self.steps and self.steps[name] is not sub_step}
            for name, sub_step in sub.steps.items():
                if name in collided:
                    continue
                sub_step.substeps = [n for n in sub_step.substeps if n not in collided]
                sub_step.children = [n for n in sub_step.children if n not in collided]
                self.steps[name] = sub_step
            self.items.update({k: v for k, v in sub.items.items() if k not in self.items})
            self.aliases.update({k: v for k, v in sub.aliases.items() if k not in self.aliases})
            for alias, target in sub.step_alias.items():
                self.step_alias.setdefault(alias, target)
            # A custom ConditionalStep class is a composite of its own
            # addStep children: grade them, not the class (grade()).
            if "Conditional" in sub.extends and sub.class_children:
                step.children = [c for c in sub.class_children if c in self.steps]
                step.class_kind = kind
                step.kind = "ConditionalStep:" + kind
            elif sub.extends in ("NpcStep", "ObjectStep", "ItemStep") and not step.targets:
                # `class MsHynnAnswerDialogQuizStep extends NpcStep` names its
                # npc in its own `super(questHelper, NpcID.X, "text")`.
                call = re.search(r"\bsuper\s*\(", sub.code)
                if call:
                    open_index = call.end() - 1
                    args = split_top(sub.code[open_index + 1:matching_close(sub.code, open_index)])
                    step.class_kind = kind
                    step.kind = sub.extends
                    sub._fill_leaf(step, args)
        self._siblings[kind] = sub
        return sub

    def spliced(self, owner):
        """`owner.getPanelSteps()` / `owner.getDisplaySteps()`: the step names
        the custom class lists for the sidebar, each graded as its own guide
        step (Recruitment Drive's Miss Cheevers gather chain, Rum Deal's
        sluglings, Biohazard's three chemical hand-overs)."""
        sub = self.sibling(owner)
        if sub is None:
            return []
        for method in ("getPanelSteps", "getDisplaySteps"):
            match = re.search(r"\b%s\s*\(\s*\)\s*\{" % method, sub.code)
            if not match:
                continue
            start = match.end() - 1
            body = sub.code[start:matching_close(sub.code, start)]
            return [self.resolve(i) for i in re.findall(r"\b([a-z]\w*)\b", body)
                    if self.resolve(i) in self.steps]
        return []

    def step_idents(self, text, list_vars=None):
        """The step names an argument/body names, in order: a
        `x.getPanelSteps()` / `x.getDisplaySteps()` spliced, a list variable
        expanded, a puzzle-wrapper alias resolved."""
        out = []
        for match in re.finditer(r"\b([a-z]\w*)\b(\s*\.\s*get(?:Panel|Display)Steps\s*\(\s*\))?", text):
            ident = match.group(1)
            if match.group(2):
                out.extend(i for i in self.spliced(ident) if i not in out)
                continue
            if ident in ("getPanelSteps", "getDisplaySteps"):
                continue
            ident = self.resolve(ident)
            if ident in self.steps:
                self.sibling(ident)  # a custom class: its targets / children
                if ident not in out:
                    out.append(ident)
            elif list_vars and ident in list_vars:
                out.extend(i for i in list_vars[ident] if i not in out)
        return out

    def item_base(self, var):
        seen = set()
        while var in self.aliases and var not in seen:
            seen.add(var)
            var = self.aliases[var]
        return var

    def _method_idents(self, method):
        match = re.search(r"\b%s\s*\(\s*\)\s*\{" % method, self.code)
        if not match:
            return []
        start = match.end() - 1
        body = self.code[start:matching_close(self.code, start)]
        return [self.item_base(i) for i in re.findall(r"\b([a-z]\w*)\b", body)
                if self.item_base(i) in self.items]

    def _fill_leaf(self, step, args):
        joined = ",".join(args[1:])
        texts = [string_text(a) for a in args[1:] if string_text(a)]
        if not texts:
            texts = [self.string_vars[a.strip()] for a in args[1:] if a.strip() in getattr(self, "string_vars", {})]
        step.text = texts[0] if texts else ""
        point = POINT_RE.search(joined)
        if point:
            step.point = tuple(int(v) for v in point.groups())
        if step.kind in ("NpcStep", "ObjectStep", "NpcEmoteStep", "EmoteStep") and len(args) > 1:
            step.targets.extend(self._ids_in(args[1]))
        for arg in args[1:]:
            if string_text(arg):
                continue
            for ident in re.findall(r"\b([a-z]\w*)\b", arg):
                base = self.item_base(ident)
                if base in self.items:
                    step.req_vars.append(base)
                    step.req_raw.append(ident)
        if step.kind in ("ItemStep", "DigStep"):
            for base in step.req_vars:
                step.targets.extend(("obj", s) for s in self.items[base]["ids"])

    def _parse_panels(self):
        code = self.code
        list_vars = {}
        for match in re.finditer(r"\b(\w+)\s*=\s*(?:new\s+ArrayList<>\s*\()?\s*(?:Arrays\.asList|List\.of|QuestUtil\.toArrayList|Collections\.singletonList)\s*\(", code):
            open_index = match.end() - 1
            body = code[open_index + 1:matching_close(code, open_index)]
            idents = self.step_idents(body)
            if idents:
                list_vars[match.group(1)] = idents
        # `testingSteps.addAll(giveChemicals.getDisplaySteps())`,
        # `mapSteps.addAll(Arrays.asList(useKeyOnChest, ...))`: a list built
        # up after its declaration.
        for match in re.finditer(r"\b(\w+)\s*\.\s*(?:addAll|add)\s*\(", code):
            if match.group(1) not in list_vars:
                continue
            open_index = match.end() - 1
            body = code[open_index + 1:matching_close(code, open_index)]
            if "PanelDetails" in body:
                continue
            for ident in self.step_idents(body, list_vars):
                if ident not in list_vars[match.group(1)]:
                    list_vars[match.group(1)].append(ident)
        for match in re.finditer(r"\bnew\s+PanelDetails\s*\(|\b(\w+)\s*\.\s*panelDetails\s*\(\s*\)", code):
            if match.group(1):
                # `sections.addAll(smuggleRum.panelDetails())`: a custom step
                # class in a sibling file carries its own panels.
                owner = self.steps.get(match.group(1))
                sibling = os.path.join(os.path.dirname(self.path), (owner.kind if owner else "") + ".java")
                if owner and os.path.isfile(sibling) and not getattr(self, "_nested", False):
                    Guide._nesting = True
                    sub = Guide(sibling, nested=True)
                    for name, step in sub.steps.items():
                        self.steps.setdefault(name, step)
                    self.items.update({k: v for k, v in sub.items.items() if k not in self.items})
                    self.aliases.update({k: v for k, v in sub.aliases.items() if k not in self.aliases})
                    for title, idents, _ in sub.panels:
                        self.panels.append((title, idents, None))
                        for ident in idents:
                            self.spliced_stage[ident] = match.group(1)
                continue
            open_index = match.end() - 1
            args = split_top(code[open_index + 1:matching_close(code, open_index)])
            if len(args) < 2:
                continue
            title = string_text(args[0])
            idents = self.step_idents(args[1], list_vars)
            # `thirdPanel = new PanelDetails(...)` twice, in an if/else on
            # the player's gang: the branches are alternatives.
            assigned = re.search(r"\b(\w+)\s*=\s*$", code[max(0, match.start() - 80):match.start()])
            # `missCheeversSection.addSteps(missCheeversStep.getPanelSteps())`
            # after `var missCheeversSection = new PanelDetails("...", pw)`.
            if assigned:
                for add in re.finditer(r"\b%s\s*\.\s*addSteps?\s*\(" % re.escape(assigned.group(1)), code):
                    open_add = add.end() - 1
                    for ident in self.step_idents(code[open_add + 1:matching_close(code, open_add)], list_vars):
                        if ident not in idents:
                            idents.append(ident)
            # A custom class step listed beside its own spliced sub-steps is
            # their wrapper, not a step of its own.
            owners = {owner for owner in idents if self.spliced(owner)}
            idents = [i for i in idents if i not in owners]
            self.panels.append((title, idents, assigned.group(1) if assigned else None))

    def leaves(self, name, seen=None):
        seen = seen if seen is not None else set()
        if name in seen or name not in self.steps:
            return []
        seen.add(name)
        step = self.steps[name]
        if not step.is_composite():
            return [name]
        out = []
        for child in step.children:
            for leaf in self.leaves(child, seen):
                if leaf not in out:
                    out.append(leaf)
        return out

    def ladder(self):
        """The guide's step list: [Step], sub-steps folded into their parent."""
        stage_of = {}
        for value, var in sorted(self.stage_puts, key=lambda p: p[0]):
            for leaf in self.leaves(var):
                stage_of.setdefault(leaf, value)
        for sub, owner in self.spliced_stage.items():
            if owner in stage_of:
                stage_of.setdefault(sub, stage_of[owner])
        folded = set()
        for step in self.steps.values():
            for sub in step.substeps:
                folded.add(sub)
        order = []
        if any(idents for _, idents, _ in self.panels):
            panel_vars = [var for _, _, var in self.panels if var]
            for index, (title, idents, var) in enumerate(self.panels):
                for ident in idents:
                    # A ConditionalStep in a panel is ONE guide step (Murder
                    # Mystery's comparePrints: whichever suspect it is).
                    if ident not in order and ident not in folded:
                        order.append(ident)
                        self.steps[ident].panel = title
                        if var and panel_vars.count(var) > 1:
                            self.steps[ident].alt_group = (var, index)
        else:
            for value, var in sorted(self.stage_puts, key=lambda p: p[0]):
                for leaf in self.leaves(var):
                    if leaf not in order and leaf not in folded:
                        order.append(leaf)
        # Every step the steps.put ConditionalStep tree can show is a guide
        # step, not only the ones getPanels() lists (seam36): Monkey Madness's
        # enterGate -- the Bamboo Gate into Marim -- lives only in
        # bringMonkey.addStep(onApeAtollSouth, enterGate), and a run that
        # ::goto'd from the Ape Atoll dock to Garkor read FULL. Such a
        # branch-only step is placed beside the sibling it leads to and
        # graded like a promoted sub-step (Grader._grade_branch): a state the
        # run may never be in, but never one it teleports out of.
        self.branch_of = self._branch_index()
        self.branch_only = {}
        covered = set(order) | folded
        for name in order:
            covered.update(self.leaves(name))
        # A custom step class whose own panels or display steps the ladder
        # already spliced in (Pirate's Treasure's smuggleRum, Biohazard's
        # giveChemicals) is graded by those parts, not again as itself.
        covered.update(self.spliced_stage.values())
        branch = []
        for _, var in sorted(self.stage_puts, key=lambda p: p[0]):
            for leaf in self.leaves(self.resolve(var)):
                if leaf not in covered and leaf not in branch and not self.spliced(leaf):
                    branch.append(leaf)
        if any(idents for _, idents, _ in self.panels):
            for leaf in branch:
                self.branch_only[leaf] = self.branch_of.get(leaf)
                order.insert(self._branch_anchor(leaf, order, stage_of), leaf)
        # A sub-step with a target of its own is a guide step of its own,
        # graded before the step it was folded into, at ANY depth
        # (x.addSubSteps(y); y.addSubSteps(z)). Folding its targets into the
        # parent let one click stand for the whole composite: Nature
        # Spirit's enterSwamp passed on the Mort Myre gate while its
        # leaveDrezel sub-step, the holy barrier under Paterdomus, was
        # teleported past (seam30). A target-less sub-step (plain text) is
        # still folded; it has nothing of its own to drive. How a promoted
        # sub-step is graded: Grader._grade_promoted.
        promoted = {}  # promoted sub-step -> the ladder step it came from
        self.promoted = promoted
        expanded_order = []
        for name in order:
            chain = []
            self._promote_substeps(name, name, order, promoted, chain, set())
            for sub in chain:
                if sub not in expanded_order:
                    expanded_order.append(sub)
            if name not in expanded_order:
                expanded_order.append(name)
        steps = []
        last_stage = None
        for name in expanded_order:
            step = self.steps[name]
            stage = stage_of.get(name)
            if stage is None and step.is_composite():
                stage = min((stage_of[l] for l in self.leaves(name) if l in stage_of), default=None)
            if stage is None and name in promoted:
                stage = self._stage_via_owner(name, promoted, stage_of)
            for sub in step.substeps:
                if sub in self.steps:
                    # The parent keeps standing for its sub-steps' targets
                    # too (its own grade is unchanged); a promoted sub-step
                    # is ALSO graded on its own below.
                    if sub in promoted:
                        step.promoted_targets.update(
                            t for t in self.steps[sub].targets if t not in step.targets)
                    step.targets.extend(t for t in self.steps[sub].targets if t not in step.targets)
                    if stage is None:
                        stage = stage_of.get(sub)
            step.stage = stage if stage is not None else last_stage
            last_stage = step.stage
            steps.append(step)
        return steps

    def _branch_anchor(self, leaf, order, stage_of):
        """Where a branch-only step goes in `order`: just before the next
        state of its ConditionalStep a player meets that is already placed
        (enterGate before talkToGarkorWithMonkey; goUpF0ToF1, goUpF1ToF2,
        goUpF2ToF3 chained before flyGandius), else just after the previous
        placed one, else after the last step of its stage or an earlier one."""
        found = self.branch_of.get(leaf)
        if found:
            progression = self.progression(found[0])
            here = next((i for i, child in enumerate(progression)
                         if leaf == self.resolve(child) or leaf in self.leaves(self.resolve(child))), None)
            if here is not None:
                for child in progression[here + 1:]:
                    child = self.resolve(child)
                    hits = [order.index(n) for n in [child] + self.leaves(child) if n in order]
                    if hits:
                        return min(hits)
                for child in reversed(progression[:here]):
                    child = self.resolve(child)
                    hits = [order.index(n) for n in [child] + self.leaves(child) if n in order]
                    if hits:
                        return max(hits) + 1
        stage = stage_of.get(leaf)
        place = 0
        for index, name in enumerate(order):
            other = stage_of.get(name)
            if other is None and self.steps[name].is_composite():
                other = min((stage_of[l] for l in self.leaves(name) if l in stage_of), default=None)
            if stage is None or (other is not None and other <= stage):
                place = index + 1
        return place

    def _promote_substeps(self, owner, name, order, promoted, chain, seen):
        """Collect, depth first, every sub-step under `name` that has a
        target of its own and is not already a ladder entry; deeper ones
        first (a sub-step's own sub-steps lead to it)."""
        if name in seen or name not in self.steps:
            return
        seen.add(name)
        for sub in self.steps[name].substeps:
            if sub not in self.steps or sub in seen:
                continue
            self._promote_substeps(owner, sub, order, promoted, chain, seen)
            sub_step = self.steps[sub]
            if sub in order or sub_step.is_composite() or not sub_step.targets:
                continue
            if sub not in promoted:
                promoted[sub] = name
                chain.append(sub)

    def _stage_via_owner(self, name, promoted, stage_of):
        seen = set()
        while name in promoted and name not in seen:
            seen.add(name)
            name = promoted[name]
            if name in stage_of:
                return stage_of[name]
        return None


# ------------------------------------------------------------------ content

_CONTENT_INDEX = None
DISPLAY = {}      # (kind, symbol) -> display name, lowercased
BY_DISPLAY = {}   # (kind, display name) -> {symbol}
PARENTS = {}      # multinpc/multiloc child -> {parent}
CHILDREN = {}     # multinpc/multiloc parent -> [child]
OPS = {}          # (kind, symbol) -> {op number: op name, lowercased}
OBJ_LINKS = {}    # obj symbol <-> its certlink / placeholderlink / countobj forms (obj_family)
SCRIPTS = {}      # (trigger kind, subject) -> (relpath, line): labels, procs, triggers
SYMBOLS = set()   # (kind, symbol) of every loc/npc/obj config block in this cache (resolves)
LOC_CLIP = {}     # loc symbol -> (blockwalk, width, length, active): all.loc's collision fields (MapWalls)
_BODIES = {}


def script_body(rel, line):
    """The lines of the script whose header is at rel:line."""
    key = (rel, line)
    if key not in _BODIES:
        with open(os.path.join(CONTENT_ROOT, rel), "r", encoding="utf-8", errors="replace") as handle:
            lines = handle.read().split("\n")
        body = []
        for text in lines[line:]:
            if text.startswith("["):
                break
            body.append(text)
        _BODIES[key] = "\n".join(body)
    return _BODIES[key]


def reaches_var(rel, line, varname, depth=4):
    """Does the script at rel:line, or a @label/~proc it jumps to (to
    `depth`), read or write %varname?"""
    content_index()
    seen = set()
    frontier = [(rel, line)]
    for _ in range(depth + 1):
        following = []
        for where in frontier:
            if where in seen:
                continue
            seen.add(where)
            body = script_body(*where)
            if re.search(r"%%%s\b" % re.escape(varname), body):
                return True
            for label in re.findall(r"@(\w+)", body):
                if ("label", label) in SCRIPTS:
                    following.append(SCRIPTS[("label", label)])
            for proc in re.findall(r"~(\w+)", body):
                if ("proc", proc) in SCRIPTS:
                    following.append(SCRIPTS[("proc", proc)])
        frontier = following
    return False


_CONSTANTS = None


def content_constant(name):
    """The value of `^name` from the content's *.constant files, or None."""
    global _CONSTANTS
    if _CONSTANTS is None:
        _CONSTANTS = {}
        for directory, dirnames, filenames in os.walk(CONTENT_ROOT):
            dirnames[:] = [d for d in dirnames if d not in ("selftest",)]
            for filename in filenames:
                if filename.endswith(".constant"):
                    with open(os.path.join(directory, filename), "r", encoding="utf-8", errors="replace") as handle:
                        for line in handle:
                            match = re.match(r"^\^(\w+)\s*=\s*(\S+)", line)
                            if match:
                                _CONSTANTS.setdefault(match.group(1), match.group(2))
    return _CONSTANTS.get(name)


def coord_tile(text):
    """`0_48_55_34_38` (level_mx_mz_lx_lz) -> (3106, 3558, 0); None when it is not one."""
    match = re.match(r"^([0-3])_(\d+)_(\d+)_(\d+)_(\d+)$", (text or "").strip())
    if not match:
        return None
    level, mx, mz, lx, lz = (int(v) for v in match.groups())
    return (mx * 64 + lx, mz * 64 + lz, level)


TELEPORT_CALL = re.compile(r"\bp_tele(?:port|jump)\s*\(([^()]*)\)")


def script_teleports(rel, line, depth=2):
    """(moves, landings) for the script at rel:line followed through the
    @labels and ~procs it calls, `depth` levels down: moves when one of them
    calls p_teleport / p_telejump; landings the tiles those calls name
    ({tile} from a coord literal or a ^constant; None in the set for an
    argument this reader cannot resolve: a coord computed at run time).
    `[debugproc,entertheabyss]` is only `@eta_debug_reset;`, whose label
    calls p_teleport(^eta_wildy_coord) (3106,3558,0)."""
    content_index()
    seen = set()
    frontier = [(rel, line)]
    moves = False
    landings = set()
    for _ in range(depth + 1):
        following = []
        for where in frontier:
            if where in seen:
                continue
            seen.add(where)
            body = script_body(*where)
            for argument in TELEPORT_CALL.findall(body):
                moves = True
                argument = argument.strip()
                if argument.startswith("^"):
                    argument = content_constant(argument[1:]) or ""
                landings.add(coord_tile(argument))
            for label in re.findall(r"@(\w+)", body):
                if ("label", label) in SCRIPTS:
                    following.append(SCRIPTS[("label", label)])
            for proc in re.findall(r"~(\w+)", body):
                if ("proc", proc) in SCRIPTS:
                    following.append(SCRIPTS[("proc", proc)])
        frontier = following
    return moves, landings


FIXTURES_ROOT = os.path.join(REPO_ROOT, "test", "quests", "fixtures")


def fixture_tile(name):
    """(x, z, level) a fixture save (test/quests/fixtures/<name>) stands the
    player on: its [player] x / z / level; None when it names none."""
    path = os.path.join(FIXTURES_ROOT, name)
    if not name or not os.path.isfile(path):
        return None
    found = {}
    section = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = re.match(r"^\s*\[(\w+)\]", line)
            if match:
                section = match.group(1)
                continue
            match = re.match(r"^\s*(x|z|level)\s*=\s*(\d+)", line)
            if match and section == "player":
                found[match.group(1)] = int(match.group(2))
    if "x" not in found or "z" not in found:
        return None
    return (found["x"], found["z"], found.get("level", 0))


def family(symbol):
    """`symbol` and every multinpc/multiloc parent that can show it."""
    content_index()
    out = {symbol}
    frontier = [symbol]
    while frontier:
        for parent in PARENTS.get(frontier.pop(), ()):
            if parent not in out:
                out.add(parent)
                frontier.append(parent)
    return out


def ops_of(kind, symbol):
    """{op number: op name} the client's menu shows for `symbol` (all.npc /
    all.loc `opN=`); a multinpc/multiloc parent with none of its own shows
    its children's."""
    content_index()
    ops = dict(OPS.get((kind, symbol), {}))
    for child in CHILDREN.get(symbol, ()):
        for number, name in OPS.get((kind, child), {}).items():
            ops.setdefault(number, name)
    return ops


# A guide step's leading verb -> the op word it presses ("Talk to Ali" is a
# Talk-to; "Kill the troll" an Attack).
STEP_VERB_ALIAS = {"speak": "talk", "ask": "talk", "tell": "talk", "chat": "talk",
                   "kill": "attack", "fight": "attack", "defeat": "attack", "slay": "attack"}


# Loc ops whose effect persists, so a guide step re-asking for the state
# (a ConditionalStep leaf) is met by the earlier press (state_already_set).
STATE_OPS = ("open", "close", "unlock", "lock")


# A guide step that GOES somewhere through a loc ("Enter the H.A.M. lair",
# "Climb down the trapdoor", "Cross the bridge") -- its leading verb (seam35).
TRAVEL_STEP_VERBS = ("enter", "climb", "cross", "go", "exit", "leave", "descend", "ascend",
                     "squeeze", "crawl", "jump", "walk", "pass", "swing", "board")
# Loc ops that move the player across the loc (op_word of the menu text).
# Anything else on a loc that also has one of these -- Pick-Lock, Unlock,
# Light, Search, Knock -- is the GATING op: it readies the crossing and does
# not make it (seam35 two_op_loc_credits_the_wrong_op: Lost Tribe's
# ham_multi_trapdoor is Open/Pick-Lock while closed, Climb-down/Close while
# open, and a Pick-Lock press credited "enterHamLair" while the test then
# ::goto'd into the lair).
TRAVEL_OPS = ("climb", "enter", "cross", "open", "go", "walk", "squeeze", "crawl", "jump",
              "exit", "leave", "pass", "board", "descend", "ascend", "swing", "push", "use",
              "slide", "ride", "dive", "travel")
# `teleport: 3166,3253,0 -> 3152,9644,0` (pointer.lua's jump readout) or any
# `x,z,level -> x,z,level` / `tile x,z -> x,z` the driver writes after a press.
MOVED_RE = re.compile(r"(\d+),\s*(\d+)(?:,\s*(\d+))?\s*->\s*(\d+),\s*(\d+)(?:,\s*(\d+))?")


def family_op_words(kind, symbol):
    """Every op word the client can show for `symbol` in ANY of its states:
    its own ops and every multiloc/multinpc child's, all numbers kept
    (ops_of folds children by op number, so a closed trapdoor's op1 Open
    hides the open one's op1 Climb-down)."""
    content_index()
    found = set()
    frontier = [symbol]
    seen = set()
    while frontier:
        current = frontier.pop()
        if current in seen:
            continue
        seen.add(current)
        found.update(op_word(name) for name in OPS.get((kind, current), {}).values())
        frontier.extend(CHILDREN.get(current, ()))
    found.discard("")
    return found


def shown_by(kind, guide_symbol, test_string):
    """Is `test_string` one of the multiloc/multinpc states the guide's
    `guide_symbol` shows (the test pressed `osf_trapdoor_closed`, the guide
    names its parent `ham_multi_trapdoor`)? same_thing only walks UP from the
    guide's symbol; a press on a child state is a press on the guide's loc,
    and is held to the step's op like one (seam35)."""
    if not test_string or not re.match(r"^[a-z0-9_]+$", test_string) or test_string == guide_symbol:
        return False
    return guide_symbol in family(test_string)


def loc_states(symbol):
    """Every multiloc child `symbol` can show, at any depth (not itself)."""
    content_index()
    out = []
    frontier = list(CHILDREN.get(symbol, ()))
    while frontier:
        child = frontier.pop(0)
        if child in out or child == symbol or child in ("-1", ""):
            continue
        out.append(child)
        frontier.extend(CHILDREN.get(child, ()))
    return out


def detail_moved(detail):
    """The `a -> b` tile pair a row's detail reports, when the two differ by a
    level or by more than one tile (a press that put the player across the
    loc, not the approach walk to it); None otherwise."""
    for match in MOVED_RE.finditer(detail or ""):
        x1, z1, l1, x2, z2, l2 = match.groups()
        if l1 is not None and l2 is not None and l1 != l2:
            return match.group(0)
        if max(abs(int(x1) - int(x2)), abs(int(z1) - int(z2))) > 1:
            return match.group(0)
    return None


def op_word(op_name):
    """`Talk-to` -> 'talk', `Pick-up` -> 'pick', `Climb up` -> 'climb'."""
    return re.split(r"[-\s]", (op_name or "").strip().lower())[0]


# Two npc/loc symbols this cache has both of are one thing only through
# family(), and the display-name rule is the objs' alone (distinct_things).
# False is the reading before seam matthew-mbp-m4-b66-seam1 (any npc of the
# guide npc's display name, any spelling prefix), kept for the fixtures.
DISTINCT_SYMBOLS = True


def resolves(kind, symbol):
    """Is `symbol` a `kind` config block in this cache (all.loc/all.npc/all.obj)?"""
    content_index()
    return (kind, symbol) in SYMBOLS


def distinct_things(kind, guide_symbol, test_string):
    """Two npc or loc symbols that BOTH exist in this cache, differ, and are
    not in each other's family() (multinpc/multiloc parents) are two
    different things, whatever their display names or spellings share:
    questscorpiona/b/c are all 'Kharid scorpion' and are three scorpions in
    three places (seam matthew-mbp-m4-b66-seam1 helper_coverage_credits_a_
    step_to_a_same_named_npc_or_another_copy_of_its_loc: catchOutpostScorpion,
    whose guide npc is questscorpionb, read DRIVEN off the use on
    questscorpiona). The name rules below are for a guide symbol this cache
    does not have (a later cache's gameval). Objs keep their display rule:
    a guide item's rev 239 variants share it (use_item_matches)."""
    if not DISTINCT_SYMBOLS or kind not in ("npc", "loc") or test_string == guide_symbol:
        return False
    if not (resolves(kind, guide_symbol) and resolves(kind, test_string)):
        return False
    return test_string not in family(guide_symbol) and guide_symbol not in family(test_string)


def same_thing(kind, guide_symbol, test_string, loose=True):
    """Does a test's string name the guide's symbol? The guide's gameval names
    are a later cache's, so rev 239 may call the same npc by a shorter name
    (holgartlandnotravel/holgartland, kennith_platform/kennith) or by its
    multinpc parent. Two npc/loc symbols this cache has both of are the same
    thing only through family() (distinct_things); the display-name rule is
    for objs only (a guide item's rev 239 variants)."""
    if not test_string or not re.match(r"^[a-z0-9_]+$", test_string):
        return False
    if distinct_things(kind, guide_symbol, test_string):
        return False
    for name in family(guide_symbol):
        if name == test_string:
            return True
        short, long_ = sorted((name, test_string), key=len)
        if len(short) >= 4 and long_.startswith(short):
            rest = long_[len(short):]
            # kennith / kennith_platform: a whole-word prefix. holgartland /
            # holgartlandnotravel: most of the name. Never mourners /
            # mournerstewfence.
            if loose and (rest.startswith("_") or len(short) / len(long_) > 0.55):
                return True
    # The display-name rule is the objs' alone now: an npc's guide symbol
    # this cache has is judged by distinct_things above, and one it does not
    # have has no display name here to compare (questscorpiona/b/c are all
    # 'kharid scorpion'; seam matthew-mbp-m4-b66-seam1).
    if kind == "obj" or (kind == "npc" and not DISTINCT_SYMBOLS):
        display = DISPLAY.get((kind, guide_symbol))
        same_name = BY_DISPLAY.get((kind, display), ())
        if display and len(display) >= 4 and test_string in same_name and len(same_name) <= 4:
            return True
    return False


DOSE_SUFFIX = re.compile(r"\s*\(\s*\d+\s*\)\s*$")


def obj_family(symbol):
    """The obj symbols that are `symbol` for a "use X on Y" step: itself, its
    noted form / placeholder / stack images (certlink, placeholderlink,
    countobj), and its dose or charge variants -- the objs whose display name
    is the same once a trailing "(N)" is cut, where at least one of the two
    carries the "(N)" (`Olive oil(4)` / `Olive oil(3)`, `Serum 207 (1)` ..
    `(4)`). Two objs that merely share a plain display name ("Key") are not
    one family; same_thing's display rule (at most four objs of that name)
    still applies on top. NOT the content's next_obj_stage chain: it walks
    into other objects (oliveoil4 -> sacred_oil4, mort_serum1 -> vial_empty)."""
    content_index()
    out = {symbol}
    frontier = [symbol]
    while frontier:
        for linked in OBJ_LINKS.get(frontier.pop(), ()):
            if linked not in out:
                out.add(linked)
                frontier.append(linked)
    for member in list(out):
        display = DISPLAY.get(("obj", member))
        if not display:
            continue
        base = DOSE_SUFFIX.sub("", display)
        if len(base) < 3:
            continue
        dosed = ["%s(%d)" % (base, n) for n in range(0, 11)] + ["%s (%d)" % (base, n) for n in range(0, 11)]
        # a dosed member reaches the undosed name too (an uncharged
        # `Amulet of glory` beside `Amulet of glory(4)`); two plain names never
        for candidate in dosed + ([base] if display != base else []):
            out.update(BY_DISPLAY.get(("obj", candidate), ()))
    for member in list(out):
        out.update(OBJ_LINKS.get(member, ()))
    return out


def content_index():
    """{symbol: [(relpath, line, trigger)]}, {symbol: category}, {debugproc: (relpath, line, body)}."""
    global _CONTENT_INDEX
    if _CONTENT_INDEX is not None:
        return _CONTENT_INDEX
    triggers = {}
    categories = {}
    debugprocs = {}
    header = re.compile(r"^\[(\w+),([\w:]+)\]")
    for directory, dirnames, filenames in os.walk(CONTENT_ROOT):
        dirnames[:] = [d for d in dirnames if d not in ("selftest",)]
        for filename in filenames:
            path = os.path.join(directory, filename)
            rel = os.path.relpath(path, CONTENT_ROOT)
            if filename.endswith(".rs2"):
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    lines = handle.read().split("\n")
                current = None
                for number, line in enumerate(lines, 1):
                    match = header.match(line)
                    if match:
                        kind, subject = match.group(1), match.group(2)
                        SCRIPTS.setdefault((kind, subject), (rel, number))
                        current = None
                        if kind == "debugproc":
                            current = subject
                            debugprocs[subject] = [rel, number, []]
                        elif re.match(r"^(op|ap)(loc|npc|obj|held)", kind) or kind.startswith("ai_"):
                            triggers.setdefault(subject, []).append((rel, number, kind))
                        continue
                    if current:
                        debugprocs[current][2].append(line)
            elif filename.endswith((".loc", ".npc", ".obj")):
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    name = None
                    for line in handle:
                        match = re.match(r"^\[(\w+)\]", line)
                        if match:
                            name = match.group(1)
                            continue
                        cat = re.match(r"^category=(\w+)", line)
                        if name and cat:
                            categories[name] = cat.group(1)
    # The cache's own configs carry most categories, as numbers.
    pack_root = os.path.join(os.path.dirname(os.path.dirname(CONTENT_ROOT)))
    category_names = {}
    category_pack = os.path.join(pack_root, "pack", "category.pack")
    if os.path.isfile(category_pack):
        with open(category_pack, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = re.match(r"^(\d+)=(\w+)", line)
                if match:
                    category_names[match.group(1)] = match.group(2)
    for kind in ("loc", "npc", "obj"):
        path = os.path.join(pack_root, "configs", "all." + kind)
        if not os.path.isfile(path):
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for match in re.finditer(r"^\[(\w+)\]\n((?:[^\[\n].*\n|\n(?!\[))*)", text, re.M):
            symbol, body = match.group(1), match.group(2)
            SYMBOLS.add((kind, symbol))
            cat = re.search(r"^category=(\w+)", body, re.M)
            if cat and symbol not in categories:
                categories[symbol] = category_names.get(cat.group(1), cat.group(1))
            display = re.search(r"^name=(.*)$", body, re.M)
            if display:
                DISPLAY[(kind, symbol)] = display.group(1).strip().lower()
                BY_DISPLAY.setdefault((kind, display.group(1).strip().lower()), set()).add(symbol)
            for child in re.findall(r"^multi(?:npc|loc)\d+=(\w+)", body, re.M):
                if child != symbol:
                    PARENTS.setdefault(child, set()).add(symbol)
                    CHILDREN.setdefault(symbol, []).append(child)
            for number, op in re.findall(r"^op([1-5])=(.+)$", body, re.M):
                OPS.setdefault((kind, symbol), {})[int(number)] = op.strip().lower()
            if kind == "obj":
                # The noted form, the bank placeholder and the stack images
                # of one object are that object (obj_family).
                for linked in re.findall(r"^(?:certlink|placeholderlink|countobj\d+)=(\w+)", body, re.M):
                    OBJ_LINKS.setdefault(symbol, set()).add(linked)
                    OBJ_LINKS.setdefault(linked, set()).add(symbol)
            if kind == "loc":
                clip = [re.search(r"^%s=(\d+)" % field, body, re.M)
                        for field in ("blockwalk", "width", "length", "active")]
                LOC_CLIP[symbol] = (int(clip[0].group(1)) if clip[0] else 2,
                                    int(clip[1].group(1)) if clip[1] else 1,
                                    int(clip[2].group(1)) if clip[2] else 1,
                                    int(clip[3].group(1)) == 1 if clip[3] else
                                    re.search(r"^op[1-5]=", body, re.M) is not None)
    _CONTENT_INDEX = (triggers, categories, debugprocs)
    return _CONTENT_INDEX


MAPS_ROOT = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "maps")
# A wall loc on a room's perimeter that is the way through it: its display
# name, or an Open op (a closed door/gate shows Open).
DOOR_NAME_RE = re.compile(r"\b(door|doors|gate|gates|portcullis|doorway)\b")
CLIMB_NAME_RE = re.compile(r"\b(ladder|stairs|staircase|stairway|trapdoor|trap door|steps)\b")
# Ops that never gate a walk (the sample tools' reach.py PASS_THROUGH_OPS): an open door's leaf and a crop.
PASS_THROUGH_OPS = {"close", "pick"}
# The zone-trigger tiles (a [zone]/[mapzone] timer that hurts a player standing on them: Regicide's
# tripwires and pitfalls, the Underground Pass spear traps), shared with reach.py.
ZONE_TRIGGERS_TSV = os.path.join(REPO_ROOT, "test", "quests", "orchestrator", "matthew-mbp-m4", "reports",
                                 "sample_tools", "zone_triggers.tsv")


def zone_triggers():
    """[(timer, {loc symbols}, [(dx, dz)], {(zone x, zone z, level)}, source)] from ZONE_TRIGGERS_TSV."""
    out = []
    if not os.path.isfile(ZONE_TRIGGERS_TSV):
        return out
    with open(ZONE_TRIGGERS_TSV, "r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip() or line.startswith("#"):
                continue
            timer, locs, offsets, zones, source = line.rstrip("\n").split("\t")[:5]
            boxes = set()
            for zone in zones.split(","):
                parts = [int(v) for v in zone.split("_")]
                if len(parts) == 3:
                    level, mx, mz = parts
                    boxes.update((mx * 8 + a, mz * 8 + b, level) for a in range(8) for b in range(8))
                else:
                    level, mx, mz, lx, lz = parts
                    boxes.add(((mx * 64 + lx) >> 3, (mz * 64 + lz) >> 3, level))
            out.append((timer, set(locs.split(",")),
                        [tuple(int(v) for v in o.split(":")) for o in offsets.split(",")], boxes, source))
    return out


class MapWalls:
    """The map's walls, read the way the client builds its collision map
    (OSRS CollisionMap.addWall / addLoc), from `maps/m<x>_<z>.jl2` (`level x
    z: loc shape [rotation]`, square-local tiles) and `.jm2` (`f<flags>` per
    tile: 1 blocked, 2 on level 1 = a bridge, which moves that column's locs
    and blocks one level down). all.loc's `blockwalk` is the client's
    clipType (default 2): a wall (shapes 0-3) or an object (9-21) blocks
    unless it is 0, a ground decoration (22) only when it is 1 and the loc
    is active (`active=1`, or ops when absent); wall decorations (4-8)
    never -- the server's own stamp (torirs_server_scene.c, the switch on
    loc->shape; angle 0 west, 1 north, 2 east, 3 south). Only what the
    grader needs: wall edges and blocked tiles per level, the locs that make
    each edge, which locs are doors (DOOR_NAME_RE / an Open op) and which are
    climbs (CLIMB_NAME_RE / a Climb op) with the tiles around them. Locs
    content adds or removes at run time are not seen."""

    WEST, NORTH, EAST, SOUTH = 1, 2, 4, 8
    # A loc with an op on a tile the map leaves walkable (a trap, a stepping stone, a log: blockwalk=0, or
    # a ground decoration that does not block) and a zone-trigger tile stop the flood like a blocked
    # tile: the walk must CLICK it (reach.py's NEEDS-OP; matthew-mbp-m4-b60-seam0). False is the old
    # reading that walked over Regicide's traps, kept for the fixtures.
    OP_LOCS_BLOCK = True
    # enclosure() floods from a blocked start tile (a landing on furniture);
    # False is the seam-1 reading (no room at all), kept for the fixtures.
    START_ON_BLOCKED = True
    STEP ={1: (-1, 0, 4), 2: (0, 1, 8), 4: (1, 0, 1), 8: (0, -1, 2)}  # side -> dx, dz, opposite side

    def __init__(self, root=None):
        self.root = root or MAPS_ROOT
        self.loaded = set()
        self.present = set()
        self.walls = {}       # (x, z, level) -> side flags
        self.edge_locs = {}   # (x, z, level, side) -> [(symbol, (x, z, level) of the loc)]
        self.blocked = set()
        self.climb_at = {}    # (x, z, level) -> [(symbol, origin)]: climbs whose footprint or ring holds it
        self.climb_ring = {}  # (symbol, origin) -> [(x, z, footprint x, footprint z, side)]: the 4-way ring
        self.locs_at = {}     # (x, z, level) -> [(symbol, origin)]: every loc on the tile (an object's footprint)
        self.op_tiles = {}    # (x, z, level) -> (symbol, origin): a walkable tile an op loc or zone trigger holds
        self.blockers = {}    # (x, z, level) -> [(symbol, origin)]: the locs that block the tile (solid_landings)
        self.diagonal_door_at = {}  # (x, z, level) -> [(symbol, origin)]: a closed door set diagonally (shape 9)
        self._names = None
        self._triggers = None

    def names(self):
        if self._names is None:
            self._names = {}
            path = os.path.join(os.path.dirname(self.root), "configs", "all.loc.compack")
            if os.path.isfile(path):
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    for line in handle:
                        key, _, value = line.strip().partition("=")
                        if key.isdigit() and value:
                            self._names[int(key)] = value
        return self._names

    @staticmethod
    def is_door(symbol):
        """A CLOSED door or gate: an Open op, or a door/gate name -- not an
        open leaf (Close and no Open: it lies along the wall it swung to, and
        the doorway beside it is open, so it never closes a room)."""
        words = {op_word(name) for name in OPS.get(("loc", symbol), {}).values()}
        if "close" in words and "open" not in words:
            return False
        display = DISPLAY.get(("loc", symbol), "")
        if DOOR_NAME_RE.search(display) and not CLIMB_NAME_RE.search(display):
            return True
        return "open" in words and "trapdoor" not in symbol and "trap_door" not in symbol

    @staticmethod
    def is_climb(symbol):
        if CLIMB_NAME_RE.search(DISPLAY.get(("loc", symbol), "")):
            return True
        return any(op_word(name) == "climb" or name.startswith("climb") for name in
                   OPS.get(("loc", symbol), {}).values())

    # The ops of a BLOCKING loc (an object, shapes 9-21) that put the player
    # on its far side: the Wilderness Ditch (`ditch_wilderness_cover`, Cross),
    # the Shantay Pass (`shantay_pass_henge_doorway`, Go-through), a squeeze
    # gap, a jump. TRAVEL_OPS without the words a blocking object uses for
    # something else (a chest's Open, a lever's Push or Use) or for going
    # somewhere else (Enter, Board, Climb: a cave, a ship, a ladder).
    CROSSING_OPS = ("cross", "go", "walk", "squeeze", "crawl", "jump", "pass", "swing")

    @classmethod
    def is_crossing(cls, symbol):
        """Is `symbol` a blocking loc a walk crosses by clicking it (a
        CROSSING_OPS op, and no climb: a ladder or stair changes level and
        stays the climb rules' business)?"""
        if cls.is_climb(symbol):
            return False
        return any(op_word(name) in cls.CROSSING_OPS for name in OPS.get(("loc", symbol), {}).values())

    # door_route (doors open) opens a closed door set diagonally across a
    # wall's corner (shape 9), which blocks its whole tile: the Wizards'
    # Tower's ladder room door (fai_wiztower_poor_door at 3107,3162,0) read
    # as a wall with no door before seam matthew-mbp-m4-b65-seam1, so the
    # room was UNREACHABLE. False restores that, kept for the fixtures.
    DIAGONAL_DOORS = True

    def diagonal_doors(self, x, z, level):
        """[(symbol, origin)]: the closed diagonal doors (shape 9, is_door)
        that alone block the tile (x, z, level); [] when none does or
        another loc blocks it too."""
        if not self.DIAGONAL_DOORS:
            return []
        doors = self.diagonal_door_at.get((x, z, level), [])
        if not doors or any(loc not in doors for loc in self.blockers.get((x, z, level), ())):
            return []
        return list(doors)

    def crossing_locs(self, x, z, level):
        """[(symbol, origin)]: the crossing locs (is_crossing) that block the
        tile (x, z, level); [] when none does."""
        return [loc for loc in self.blockers.get((x, z, level), ()) if self.is_crossing(loc[0])]

    def _crossing_wall(self, cx, cz, nx, nz, level, side, opposite):
        """Is the wall on the step (cx, cz) -> (nx, nz) part of a crossing:
        made only by op-less, nameless locs (an `inviswall`) and on the edge
        of a tile a crossing loc blocks? The Shantay Pass at 3302,3116 has
        an inviswall (shape 2) on its own tile beside the Go-through."""
        if not (self.crossing_locs(cx, cz, level) or self.crossing_locs(nx, nz, level)):
            return False
        for key, flag in (((cx, cz, level), side), ((nx, nz, level), opposite)):
            if not self.walls.get(key, 0) & flag:
                continue
            for symbol, _ in self.edge_locs.get(key + (flag,), []) or [(None, None)]:
                if symbol is None or OPS.get(("loc", symbol)) or DISPLAY.get(("loc", symbol)):
                    return False
        return True

    @staticmethod
    def climb_ways(symbol):
        """(goes up, goes down) for a climb loc, from its ops (`Climb-up`,
        `Climb-down`; a bare `Climb` or none readable: both). A trapdoor
        goes down."""
        ops = [name for name in OPS.get(("loc", symbol), {}).values()]
        up = any(re.search(r"\bup\b", name) for name in ops)
        down = any(re.search(r"\bdown\b", name) for name in ops) or "trap" in symbol
        if not up and not down:
            return True, True
        return up, down

    @staticmethod
    def is_op_loc(symbol, shape, blockwalk, active):
        """Does a placement leave its tile walkable while carrying an op the walk must click?"""
        if 4 <= shape <= 8:
            return False
        ops = list(OPS.get(("loc", symbol), {}).values())   # lowercased whole op names
        if not ops or all(op in PASS_THROUGH_OPS for op in ops):
            return False
        if shape == 22:
            return not (blockwalk == 1 and active)
        return blockwalk == 0

    def _add_wall(self, x, z, level, side, symbol, at):
        dx, dz, opposite = self.STEP[side]
        for key, flag in (((x, z, level), side), ((x + dx, z + dz, level), opposite)):
            self.walls[key] = self.walls.get(key, 0) | flag
            self.edge_locs.setdefault(key + (flag,), []).append((symbol, at))

    def load(self, square_x, square_z):
        if (square_x, square_z) in self.loaded:
            return
        self.loaded.add((square_x, square_z))
        base = os.path.join(self.root, "m%d_%d" % (square_x, square_z))
        if not os.path.isfile(base + ".jl2"):
            return
        content_index()
        self.present.add((square_x, square_z))
        origin_x, origin_z = square_x * 64, square_z * 64
        bridges = set()
        tile_flags = []
        if os.path.isfile(base + ".jm2"):
            with open(base + ".jm2", "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            for match in re.finditer(r"^([0-3]) (\d+) (\d+):[^\n]*?\bf(\d+)", text, re.M):
                level, x, z, flags = (int(v) for v in match.groups())
                tile_flags.append((level, x, z, flags))
                if level == 1 and flags & 2:
                    bridges.add((x, z))
        for level, x, z, flags in tile_flags:
            if flags & 1:
                real = level - 1 if (x, z) in bridges else level
                if real >= 0:
                    self.blocked.add((origin_x + x, origin_z + z, real))
        with open(base + ".jl2", "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        names = self.names()
        for match in re.finditer(r"^([0-3]) (\d+) (\d+): (\d+) (\d+)(?: (\d+))?", text, re.M):
            level, x, z, loc, shape = (int(v) for v in match.groups()[:5])
            rotation = int(match.group(6) or 0)
            real = level - 1 if (x, z) in bridges else level
            if real < 0:
                continue
            symbol = names.get(loc, str(loc))
            blockwalk, width, length, active = LOC_CLIP.get(symbol, (2, 1, 1, False))
            wx, wz = origin_x + x, origin_z + z
            size = (length, width) if rotation in (1, 3) else (width, length)
            if 9 <= shape <= 22:
                for ox in range(size[0] if shape != 22 else 1):
                    for oz in range(size[1] if shape != 22 else 1):
                        self.locs_at.setdefault((wx + ox, wz + oz, real), []).append((symbol, (wx, wz, real)))
            else:
                self.locs_at.setdefault((wx, wz, real), []).append((symbol, (wx, wz, real)))
            if self.OP_LOCS_BLOCK:
                if self._triggers is None:
                    self._triggers = zone_triggers()
                for timer, symbols, offsets, boxes, source in self._triggers:
                    if symbol in symbols:
                        for dx, dz in offsets:
                            if ((wx + dx) >> 3, (wz + dz) >> 3, level) in boxes:
                                self.op_tiles.setdefault((wx + dx, wz + dz, real), (timer, (wx, wz, real)))
                if self.is_op_loc(symbol, shape, blockwalk, active):
                    for ox in range(size[0] if 9 <= shape <= 21 else 1):
                        for oz in range(size[1] if 9 <= shape <= 21 else 1):
                            self.op_tiles[(wx + ox, wz + oz, real)] = (symbol, (wx, wz, real))
            if self.is_climb(symbol):
                size = (length, width) if rotation in (1, 3) else (width, length)
                key = (symbol, (wx, wz, real))
                footprint = {(wx + ox, wz + oz) for ox in range(size[0]) for oz in range(size[1])}
                ring = []
                for fx, fz in sorted(footprint):
                    for side, (dx, dz, _) in self.STEP.items():
                        if (fx + dx, fz + dz) not in footprint:
                            ring.append((fx + dx, fz + dz, fx, fz, side))
                self.climb_ring[key] = ring
                for tx, tz in footprint | {(r[0], r[1]) for r in ring}:
                    self.climb_at.setdefault((tx, tz, real), []).append(key)
            if shape <= 3:
                if blockwalk == 0:
                    continue
                at = (wx, wz, real)
                if shape == 0:
                    self._add_wall(wx, wz, real, (self.WEST, self.NORTH, self.EAST, self.SOUTH)[rotation], symbol, at)
                elif shape == 2:
                    first = (self.WEST, self.NORTH, self.EAST, self.SOUTH)[rotation]
                    second = (self.NORTH, self.EAST, self.SOUTH, self.WEST)[rotation]
                    self._add_wall(wx, wz, real, first, symbol, at)
                    self._add_wall(wx, wz, real, second, symbol, at)
                # shapes 1 and 3 are corner pillars: they block only a
                # diagonal step, which a 4-way flood never takes
            elif 9 <= shape <= 21:
                if blockwalk == 0:
                    continue
                if rotation in (1, 3):
                    width, length = length, width
                for ox in range(width):
                    for oz in range(length):
                        self.blocked.add((wx + ox, wz + oz, real))
                        self.blockers.setdefault((wx + ox, wz + oz, real), []).append((symbol, (wx, wz, real)))
                if shape == 9 and self.is_door(symbol):
                    self.diagonal_door_at.setdefault((wx, wz, real), []).append((symbol, (wx, wz, real)))
            elif shape == 22 and blockwalk == 1 and active:
                self.blocked.add((wx, wz, real))
                self.blockers.setdefault((wx, wz, real), []).append((symbol, (wx, wz, real)))

    def _ready(self, x, z):
        square = (x >> 6, z >> 6)
        if square not in self.loaded:
            # a wall on a neighbour's edge row also closes this square's tile
            for sx in (square[0] - 1, square[0], square[0] + 1):
                for sz in (square[1] - 1, square[1], square[1] + 1):
                    self.load(sx, sz)
        return square in self.present

    def _flood(self, x, z, level, limit, perimeter, touched=None, through=None):
        """Tiles a 4-way walk from (x, z) reaches on `level` without crossing
        a wall or entering a blocked tile (except those in `through`); None
        past `limit` tiles. The wall locs met on the way go into
        `perimeter`, the blocked tiles it stops at into `touched`."""
        seen = {(x, z)}
        todo = [(x, z)]
        while todo:
            cx, cz = todo.pop()
            for side, (dx, dz, opposite) in self.STEP.items():
                nx, nz = cx + dx, cz + dz
                if (nx, nz) in seen:
                    continue
                if self.walls.get((cx, cz, level), 0) & side or self.walls.get((nx, nz, level), 0) & opposite:
                    for symbol, at in self.edge_locs.get((cx, cz, level, side), []):
                        perimeter[(symbol, at)] = True
                    continue
                if not self._ready(nx, nz):
                    continue
                if ((nx, nz, level) in self.blocked or (nx, nz, level) in self.op_tiles) and \
                        (through is None or (nx, nz, level) not in through):
                    if touched is not None:
                        touched[(nx, nz, level)] = True
                    continue
                seen.add((nx, nz))
                if len(seen) > limit:
                    return None
                todo.append((nx, nz))
        return seen

    def enclosure(self, x, z, level, limit):
        """The building the tile (x, z, level) is in, across its floors: a
        4-way flood from it that stops at walls and blocked tiles, and that
        follows every climb loc it reaches to the floor that climb leads to
        (Climb-up one level up, Climb-down one down, from the tiles around
        it). {floors {level: tiles}, tiles (the landing floor's), doors
        [(symbol, tile)] (closed doors met on any floor), climbs [symbol],
        to_dungeon (a climb DOWN from level 0: it leads to another map
        frame), box (min x, max x, min z, max z over every floor)} when every
        flood closes within `limit` tiles in all; None when one does not
        (open ground, a courtyard bigger than a building, an upper floor
        whose stairs come down outside) or the tile is not on a loaded map
        square.

        The tile itself may be blocked (a goto lands where it is told, on a
        table, a barrel, a stair's footprint, a rock): the flood starts from
        it anyway, since a player standing there steps off it to any side no
        wall closes (twilightspromise's goto-enterHQ onto civitas_stairs_1x3,
        atfirstlight's goto-makeEquipmentPile onto Atza's table). Before
        matthew-mbp-m4-b56-grader2 a blocked landing was read as no room at
        all."""
        if not self._ready(x, z):
            return None
        if (x, z, level) in self.blocked and not self.START_ON_BLOCKED:
            return None
        floors = {}
        perimeter = {}
        touched = {}
        climbs = set()
        to_dungeon = False
        down_from_ground = []
        total = 0
        # A blocked start: the whole footprint of the loc(s) the player stands
        # in. A goto onto the middle of a 2x3 staircase steps off at its
        # front, not into the stair's own blocked tiles (Ardougne castle's
        # stairs at 2571,3295 read as a 1-tile "room" without this).
        start = self.footprint(x, z, level)
        todo = sorted(start)
        while todo:
            sx, sz, sl = todo.pop()
            if (sx, sz) in floors.get(sl, ()):
                continue
            if not self._ready(sx, sz) or ((sx, sz, sl) in self.blocked and (sx, sz, sl) not in start):
                continue
            seen = self._flood(sx, sz, sl, limit - total, perimeter, touched, start)
            if seen is None:
                return None
            floors.setdefault(sl, set()).update(seen)
            total += len(seen)
            for tx, tz in seen:
                for key in self.climb_at.get((tx, tz, sl), ()):
                    if key in climbs:
                        continue
                    climbs.add(key)
                    up, down = self.climb_ways(key[0])
                    if down and sl == 0:
                        to_dungeon = True
                        down_from_ground.append(key)
                    for other in ([sl + 1] if up and sl < 3 else []) + ([sl - 1] if down and sl > 0 else []):
                        # the tiles beside the climb on the floor it leads to,
                        # not through a wall from it (a ring tile outside the
                        # upstairs wall is not where the climb comes out)
                        todo.extend((rx, rz, other) for rx, rz, fx, fz, side in self.climb_ring[key]
                                    if not self.walls.get((fx, fz, other), 0) & side)
        if all((tx, tz, level) in start for tx, tz in floors.get(level, ())) and (x, z, level) in self.blocked:
            # the player stands in a blocked footprint no walkable tile of this
            # floor touches (water, a barrier, the museum barge's gangway at
            # 3362,3447): no room to read, not a 1-tile one
            return None
        tiles = [t for tiles in floors.values() for t in tiles]
        # Every loc with an op the room touches -- a wall in its perimeter, a
        # blocked tile it stops at, a loc inside it: one of these may be a way
        # in or out that is not a door (a stepping stone, a squeeze gap, a
        # portal, a counter flap with an op).
        ops = set()
        found = list(perimeter) + [loc for key in touched for loc in self.locs_at.get(key, ())] + \
            [loc for sl, seen in floors.items() for tx, tz in seen for loc in self.locs_at.get((tx, tz, sl), ())]
        for symbol, at in found:
            # an op, or a script trigger (a use-on: Spirits of the Elid's
            # desert_water_cave_root has no op, only [oplocu])
            if OPS.get(("loc", symbol)) or symbol_triggers(symbol):
                ops.add((symbol, at))
        return {
            "floors": floors, "tiles": floors.get(level) or {(x, z)},
            "doors": sorted({(symbol, at) for symbol, at in perimeter if self.is_door(symbol)}),
            "ops": sorted(ops), "down_from_ground": sorted(down_from_ground),
            "climbs": sorted({key[0] for key in climbs}), "to_dungeon": to_dungeon,
            "box": (min(t[0] for t in tiles), max(t[0] for t in tiles),
                    min(t[1] for t in tiles), max(t[1] for t in tiles)),
        }

    def steps_off(self, x, z, level):
        """The 4-way neighbours a player on (x, z, level) steps to without
        crossing a wall (blocked tiles included: the caller decides)."""
        out = []
        for side, (dx, dz, opposite) in self.STEP.items():
            if self.walls.get((x, z, level), 0) & side or self.walls.get((x + dx, z + dz, level), 0) & opposite:
                continue
            out.append((x + dx, z + dz))
        return out

    def in_room(self, room, x, z, level):
        """Is a player on (x, z, level) inside `room` (an enclosure())? On a
        tile the flood reached, or on a BLOCKED tile (a loc the flood does
        not enter: a table, a barrel, a diagonal wall) that steps off into
        it: a player teleported onto the furniture is in the room, and one
        on a diagonal wall tile at its corner may step in."""
        tiles = room["floors"].get(level, ())
        if (x, z) in tiles:
            return True
        if (x, z, level) not in self.blocked:
            return False
        self._ready(x, z)
        return any((fx, fz) in tiles or any(tile in tiles for tile in self.steps_off(fx, fz, fl))
                   for fx, fz, fl in self.footprint(x, z, level))

    def _edge_doors(self, cx, cz, nx, nz, level, side, opposite):
        """What closes the 4-way step (cx, cz) -> (nx, nz) on `level`: None
        when nothing does, [] when a wall that is no door does (or a wall
        no loc this reader knows makes), else the door locs [(symbol,
        origin)] that do -- a closed door or gate (is_door) is the only wall
        a walk opens."""
        found = []
        closed = False
        for key, flag in (((cx, cz, level), side), ((nx, nz, level), opposite)):
            if not self.walls.get(key, 0) & flag:
                continue
            closed = True
            locs = self.edge_locs.get(key + (flag,), [])
            if not locs or not all(self.is_door(symbol) for symbol, _ in locs):
                return []
            found.extend(loc for loc in locs if loc not in found)
        return found if closed else None

    def door_route(self, start, end, level, margin, doors_open):
        """A 4-way walk from `start` (x, z) to `end` on `level` inside their
        box widened by `margin` tiles: walls, blocked tiles and op tiles (a
        trap, a stepping stone: OP_LOCS_BLOCK) closed, the two end tiles
        always enterable (a goto may stand on a loc) -- reach.py's flood
        (test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools/) on
        the grader's own map reader. `doors_open` False: every closed door
        and gate shut; (True, []) when a walk exists. True: doors open, the
        SHORTEST walk (reach.py's BFS, so the gate named is the one its
        NEEDS-DOOR names); (True, [(symbol, origin) of each door it
        crosses]). (False, []) when no walk reaches `end` in the box.
        `doors_open` "cross": doors open AND a tile a crossing loc blocks
        (crossing_locs: the Wilderness Ditch, the Shantay Pass) enterable,
        the loc recorded among the doors crossed, with the op-less wall on
        its own edge (_crossing_wall) part of it; the walk entering the
        FEWEST crossing tiles, then crossing the fewest doors, then the
        shortest (reach.py's NEEDS-OP rung): a shortest walk would take
        every agility shortcut on the way (Horror from the Deep's hop into
        the Barbarian agility course named two log balances and Lawgof's
        railing gate, where only the course's pipe is needed).
        Cached per (start, end, level, margin, doors_open)."""
        cache = self.__dict__.setdefault("_door_routes", {})
        key = (start, end, level, margin, doors_open)
        if key in cache:
            return cache[key]
        x0, x1 = min(start[0], end[0]) - margin, max(start[0], end[0]) + margin
        z0, z1 = min(start[1], end[1]) - margin, max(start[1], end[1]) + margin
        ends = {start, end}
        self._ready(start[0], start[1])
        prev = {start: None}
        crossed = {}
        todo = deque([start])
        found = False
        cross = doors_open == "cross"
        best = {start: (0, 0, 0)}
        heap = [((0, 0, 0), start)]
        while heap if cross else todo:
            if cross:
                cost, cell = heapq.heappop(heap)
                if best.get(cell) != cost:
                    continue
            else:
                cell = todo.popleft()
            if cell == end:
                found = True
                break
            for side, (dx, dz, opposite) in self.STEP.items():
                nx, nz = cell[0] + dx, cell[1] + dz
                if not (x0 <= nx <= x1 and z0 <= nz <= z1):
                    continue
                if not self._ready(nx, nz):
                    continue
                edge = self._edge_doors(cell[0], cell[1], nx, nz, level, side, opposite)
                if edge == []:
                    if doors_open != "cross" or \
                            not self._crossing_wall(cell[0], cell[1], nx, nz, level, side, opposite):
                        continue
                    edge = None
                if edge and not doors_open:
                    continue
                if (nx, nz) in prev and not cross:
                    continue
                over = []
                if (nx, nz) not in ends and ((nx, nz, level) in self.blocked or (nx, nz, level) in self.op_tiles):
                    over = self.crossing_locs(nx, nz, level) if cross else []
                    diagonal = self.diagonal_doors(nx, nz, level) if doors_open and not over and \
                        (nx, nz, level) not in self.op_tiles else []
                    if diagonal:
                        # a door set diagonally across a wall's corner (shape
                        # 9) blocks its tile: a walk opens it like a wall door
                        edge = (edge or []) + [loc for loc in diagonal if loc not in (edge or [])]
                    elif not over or (nx, nz, level) in self.op_tiles:
                        continue
                if cross:
                    step = (cost[0] + (1 if over else 0), cost[1] + (1 if edge else 0), cost[2] + 1)
                    if (nx, nz) in best and best[(nx, nz)] <= step:
                        continue
                    best[(nx, nz)] = step
                    heapq.heappush(heap, (step, (nx, nz)))
                else:
                    todo.append((nx, nz))
                prev[(nx, nz)] = cell
                crossed[(nx, nz)] = (edge or []) + [loc for loc in over if loc not in (edge or [])] or None
        doors = []
        if found:
            cell = end
            while cell is not None:
                for loc in crossed.get(cell) or ():
                    if loc not in doors:
                        doors.append(loc)
                cell = prev[cell]
            doors.reverse()
        cache[key] = (found, doors)
        return cache[key]

    def door_cluster(self, doors, slack=2):
        """`doors` and every closed door loc within `slack` tiles of one of
        them on its level: the other leaf of a double gate
        (mcannon_dwarf_railing_gate 2567,3456 beside
        mcannon_dwarf_railing_gate_mir 2568,3456)."""
        out = list(doors)
        for _, (x, z, level) in doors:
            self._ready(x, z)
            for dx in range(-slack, slack + 1):
                for dz in range(-slack, slack + 1):
                    for loc in self.locs_at.get((x + dx, z + dz, level), ()):
                        if loc not in out and self.is_door(loc[0]):
                            out.append(loc)
        return out

    GATE_MARGINS = (30, 80, 160)
    # only_way_gates retries a margin with the crossing locs enterable
    # (door_route "cross"); False is the reading before seam
    # matthew-mbp-m4-b64-seam1 (a blocking crossing loc is a wall), kept
    # for the fixtures.
    CROSSINGS = True

    def only_way_gates(self, start, end, level):
        """The b59 sampler ruling as a map question: the gates every walk
        from `start` to `end` on `level` must open. Closed-doors flood at
        the widest of GATE_MARGINS: a walk exists -> []. Else the fewest-door
        route at each margin; [] when the widest has none (no walk at all:
        a climb, an op loc, water) or when the margins disagree on the gate.
        A margin with no route at all (UNREACHABLE: the box is too small to
        go round) says nothing and is skipped; every other margin's route
        must cross the same gate (a door of the widest route, or a leaf
        within 2 tiles of it). Returns [(symbol, origin)], the widest
        route's doors that every route crosses.

        A margin with no route even with every door open is tried again
        with the crossing locs enterable (door_route "cross": the
        Wilderness Ditch, the Shantay Pass -- a blocking object a walk
        crosses by its own op, which the doors-open walk read as a wall,
        so no goto across one was ever charged before seam
        matthew-mbp-m4-b64-seam1). A crossing loc is common to two routes
        when both cross a loc of the same symbol (the ditch is one
        placement per column, and two routes may jump it a column apart).
        CROSSINGS False is the reading before it, kept for the fixtures."""
        widest = self.GATE_MARGINS[-1]
        walked, _ = self.door_route(start, end, level, widest, False)
        if walked:
            return []
        routes = []
        for margin in self.GATE_MARGINS:
            found, doors = self.door_route(start, end, level, margin, True)
            if not found and self.CROSSINGS:
                found, doors = self.door_route(start, end, level, margin, "cross")
            if found:
                routes.append(doors)
        if not routes or not (self.door_route(start, end, level, widest, True)[0] or
                              (self.CROSSINGS and self.door_route(start, end, level, widest, "cross")[0])):
            return []
        common = []
        for door in routes[-1]:
            near = set(self.door_cluster([door]))
            if self.is_crossing(door[0]) and not self.is_door(door[0]):
                if all(any(other[0] == door[0] for other in doors) for doors in routes):
                    common.append(door)
            elif all(near & set(doors) for doors in routes):
                common.append(door)
        return common

    # hop_route's wider bound: a hop with no route on foot at all at the
    # widest of GATE_MARGINS (not even with every door open and every
    # crossing loc enterable) is flooded again at each of these before it is
    # called UNREACHABLE -- a walk that goes round the margin-160 box (Making
    # History's Lumbridge -> Jorral hop crosses membergater 2933,3320 only at
    # margin 250; Chompy Bird's Lumbridge -> Rantz and Clock Tower's
    # Lumbridge -> Kojo only at 400). A true island (Lunar Isle, the
    # Pandemonium) or a region only a climb reaches (Morytania past the
    # Paterdomus trapdoor, Lletya) has none at 600. NO_ROUTE False is the reading before seam
    # matthew-mbp-m4-b65-seam1 (a hop with no route at 160 said nothing),
    # kept for the fixtures.
    WIDE_MARGINS = (250, 400, 600)
    NO_ROUTE = True

    def square_present(self, x, z):
        """Is the map square holding (x, z) on disk (a flood can read it)?"""
        return self._ready(x, z)

    def hop_route(self, start, end, level):
        """What a walk on foot from `start` to `end` on `level` must do, for
        gate_crossings: ("walk", [], margin) when a walk with every door shut
        joins them (at margin 160, else at a WIDE_MARGINS margin); ("gates",
        [(symbol, origin)], margin) when every walk opens or crosses those
        (only_way_gates at 30/80/160 -- margin None -- or the doors-open /
        crossing route at the first WIDE_MARGINS margin that has one);
        ("none", [], None) when the margins disagree on the gate, an end's
        map square is not on disk, an end is a solid tile (solid_landings'
        business), or NO_ROUTE is off; ("unreachable", [],
        margin) when no walk exists even at the widest WIDE_MARGINS margin:
        an island, a cave with no way in on this level, a region a climb,
        an op loc, a boat or a teleport alone reaches."""
        widest = self.GATE_MARGINS[-1]
        if self.door_route(start, end, level, widest, False)[0]:
            return ("walk", [], widest)
        gates = self.only_way_gates(start, end, level)
        if gates:
            return ("gates", gates, None)
        if self.door_route(start, end, level, widest, True)[0] or \
                (self.CROSSINGS and self.door_route(start, end, level, widest, "cross")[0]):
            return ("none", [], None)  # a route exists; the margins disagree on its gate
        if not self.NO_ROUTE or not self.square_present(start[0], start[1]) or \
                not self.square_present(end[0], end[1]):
            return ("none", [], None)
        for margin in self.WIDE_MARGINS:
            if self.door_route(start, end, level, margin, False)[0]:
                return ("walk", [], margin)
            # the FEWEST-door walk (door_route "cross" weighs crossings, then
            # doors, then length): a shortest doors-open walk cuts through
            # every house on the way (Elena's Lumbridge -> Edmond hop named
            # three Ardougne poordoors beside membergater)
            found, doors = self.door_route(start, end, level, margin, "cross" if self.CROSSINGS else True)
            if found:
                return ("gates", doors, margin) if doors else ("walk", [], margin)
        if (start[0], start[1], level) in self.blocked or (end[0], end[1], level) in self.blocked:
            # a hop off or onto a solid tile (furniture, a stair's footprint):
            # solid_landings charges the goto that put the player there, and
            # a flood from inside a footprint says nothing about the route
            return ("none", [], None)
        return ("unreachable", [], self.WIDE_MARGINS[-1] if self.WIDE_MARGINS else widest)

    def footprint(self, x, z, level):
        """{(x, z, level)}: the tile, and when it is blocked every blocked
        tile within 4 that carries a loc it carries (the rest of a table's or
        a staircase's footprint)."""
        start = {(x, z, level)}
        if (x, z, level) not in self.blocked:
            return start
        mine = set(self.locs_at.get((x, z, level), ()))
        if not mine:
            return start
        for dx in range(-4, 5):
            for dz in range(-4, 5):
                tile = (x + dx, z + dz, level)
                if tile in self.blocked and mine & set(self.locs_at.get(tile, ())):
                    start.add(tile)
        return start


_MAP_WALLS = None


def map_walls():
    global _MAP_WALLS
    if _MAP_WALLS is None:
        _MAP_WALLS = MapWalls()
    return _MAP_WALLS


def symbol_triggers(symbol):
    """Every [op*]/[ap*] trigger on `symbol` or its category."""
    triggers, categories, _ = content_index()
    found = []
    for name in family(symbol):
        found.extend(triggers.get(name, []))
        category = categories.get(name)
        if category:
            found.extend(triggers.get("_" + category, []))
    return found


def trigger_is_unconditional(rel, line):
    """True when the trigger body at `rel`:`line` reads no %variable: it does
    the same thing for every player, whichever quest's file it lives in."""
    with open(os.path.join(CONTENT_ROOT, rel), "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")
    body = []
    for text in lines[line:]:
        if text.startswith("["):
            break
        body.append(text.split("//")[0])
    text = "\n".join(body)
    return bool(text.strip()) and not re.search(r"%\w+", text)


def quest_rs2(quest_dir):
    """[(relpath, [lines])] for the quest's own scripts."""
    base = os.path.join(CONTENT_ROOT, "quests", quest_dir)
    out = []
    for directory, _, filenames in os.walk(base):
        for filename in sorted(filenames):
            if filename.endswith((".rs2", ".constant")):
                path = os.path.join(directory, filename)
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    out.append((os.path.relpath(path, CONTENT_ROOT), handle.read().split("\n")))
    return out


NARRATION_SAFE = re.compile(
    r"^\s*(mes\(|~chat|~mesbox|~objbox|~doubleobjbox|inv_add|inv_del|if\s*\(|else|\}|\{|return|//|[|&]|"
    r"\$\w+\s*=|def_\w+|~p_|p_delay|anim\(|sound_synth|spotanim|queue\(|%\w+\s*=|$)")


def narrating_writes(quest_dir):
    """Stage writes in a branch that only narrates: [(rel, line, text of its mes() lines)].

    A branch is the lines from the nearest enclosing `if (...) {`/`case`/
    trigger header to the `%var = ^stage` write. It narrates when it has at
    least one mes() and every line is dialogue, narration, inventory or a
    gate -- no loc/npc interaction of its own."""
    found = []
    for rel, lines in quest_rs2(quest_dir):
        if not rel.endswith(".rs2"):
            continue
        for number, line in enumerate(lines, 1):
            if not re.match(r"^\s*%\w+\s*=\s*\^\w+\s*;", line):
                continue
            start = number - 1
            depth = 0
            while start > 0:
                previous = lines[start - 1]
                depth += previous.count("}") - previous.count("{")
                start -= 1
                if depth < 0 or re.match(r"^\[", previous) or re.match(r"^\s*case\b", previous):
                    break
            block = lines[start:number - 1]
            header_line = next((lines[i] for i in range(number - 1, -1, -1) if lines[i].startswith("[")), "")
            if re.match(r"^\[debugproc,", header_line) or re.search(r"test|debug", os.path.basename(rel)):
                continue
            said = [b for b in block if re.match(r"^\s*(mes\(|~chat|~mesbox)", b) and not re.search(r"PASS|FAIL", b)]
            if not said:
                continue
            if all(NARRATION_SAFE.match(b) for b in block[1:]):
                has_mes = any(re.match(r"^\s*mes\(", b) for b in said)
                said.sort(key=lambda b: 0 if re.match(r"^\s*mes\(", b) else 1)  # quote the narration
                found.append((rel, number, " ".join(said), header_line + "\n" + "\n".join(block), has_mes))
    return found


# Only the phrases the content queue uses to declare a skipped leg; "deferred"
# and bare "soft" also describe animations, progress packets and soft clay.
PARAGRAPHS = {}   # (rel, line) of a soft marker -> its whole comment paragraph
SOFT_MARK = re.compile(r"soft-skip|soft skip|soft stand-in|stand-in for|narrated rather than|"
                       r"collapsed (to|into)|collapses", re.IGNORECASE)


def soft_markers(quest_dir):
    """Comments/mes() in the quest's own scripts that say a leg is soft-skipped."""
    found = []
    for rel, lines in quest_rs2(quest_dir):
        if re.search(r"test|debug", os.path.basename(rel)):
            continue
        for number, line in enumerate(lines, 1):
            if SOFT_MARK.search(line) and ("//" in line or "mes(" in line):
                text = line.strip()
                if line.lstrip().startswith("//"):
                    # the marker line and the comment lines either side of it
                    start, end = number - 1, number - 1
                    while start > number - 3 and start > 0 and lines[start - 1].lstrip().startswith("//"):
                        start -= 1
                    while end < number + 1 and end + 1 < len(lines) and lines[end + 1].lstrip().startswith("//"):
                        end += 1
                    following = next((l for l in lines[end + 1:] if l.strip() and not l.lstrip().startswith("//")), "")
                    if following.startswith("[debugproc,"):
                        continue  # a test hook's own comment, not the quest
                    text = " ".join(l.strip().lstrip("/").strip() for l in lines[start:end + 1])
                    # the whole paragraph, for the guide step names it cites
                    first, last = number - 1, number - 1
                    while first > 0 and lines[first - 1].lstrip().startswith("//"):
                        first -= 1
                    while last + 1 < len(lines) and lines[last + 1].lstrip().startswith("//"):
                        last += 1
                    PARAGRAPHS[(rel, number)] = " ".join(lines[first:last + 1])
                found.append((rel, number, text))
    return found


# ------------------------------------------------------------------ test

def strip_lua_comments(source):
    from gate import strip_lua_comments as strip  # the one comment stripper
    return strip(source)


ACTION_VERB = re.compile(
    r"\b(talk_to|click_loc|use_on|use_item_on_item|click_obj|attack|inv_op|equip|shop\.buy|"
    r"by_symbol|drive\.op|await_dead\w*|pickup|take_obj|press|choose|chat\.play|emote|"
    r"click_npc|npc_op|loc_op|dig|drop)\b")


class Test:
    def __init__(self, test_id, path, text=None):
        """`text` grades a copy (lint_quest's text, `--lua <file>`) in place of
        the file at `path`."""
        self.test_id = test_id
        self.path = path
        if text is None:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        self.raw = text
        self.code = strip_lua_comments(self.raw)
        self.raw_lines = self.raw.split("\n")
        self.code_lines = self.code.split("\n")
        self.strings = {}      # string literal -> first line
        self.cheats = []       # (line, text)
        self.gotos = []        # (line, x, y, z)
        self.blocked_text = []  # (line, text)
        self.guide_gaps = []   # (line, step, reason)
        # BRANCH-IN / PARTNER / NOT-A-STEP / OBSOLETE / ANY-OF markers:
        # [{line, kind, step, ..., error}] (parse_equivalent_marker).
        self.equivalents = []
        for number, line in enumerate(self.code_lines, 1):
            for literal in re.findall(r'"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.)*)\'', line):
                text = literal[0] or literal[1]
                if text.startswith("::"):
                    self.cheats.append((number, text))
                else:
                    self.strings.setdefault(text, number)
            for match in re.finditer(r"goto_tile\s*[,(]\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)", line):
                self.gotos.append((number,) + tuple(int(v) for v in match.groups()))
            if "t.blocked(" in line:
                chunk = " ".join(self.code_lines[number - 1:number + 4])
                self.blocked_text.append((number, chunk))
        # `stand_on_square = true` -- the one opt-in that lets the driver's
        # reach retry ::goto onto a loc's own square (pointer.lua SEAM
        # reach_stand_on_opt_in). Each carries the GUIDE-GAP marker within
        # STAND_ON_MARKER_SPAN lines above it that declares it, or None.
        self.stand_on_optins = []  # (line, marker (line, step, reason, cite) or None)
        for number, line in enumerate(self.code_lines, 1):
            if STAND_ON_OPTIN.search(line):
                self.stand_on_optins.append((number, None))
        for number, line in enumerate(self.raw_lines, 1):
            if "content_bug" in line:
                self.blocked_text.append((number, line))
            match = GUIDE_GAP_RE.match(line)
            if match:
                self.guide_gaps.append((number, match.group(1), match.group(2).strip()))
            parsed = parse_equivalent_marker(line)
            if parsed:
                parsed["line"] = number
                self.equivalents.append(parsed)
        # The strings an ACTION names: a literal on a line that calls an
        # action verb, or a local assigned a literal and later passed to one.
        # An inv.count("egg") read is not a pickup.
        self.action_strings = {}
        action_lines = [n for n, line in enumerate(self.code_lines, 1) if ACTION_VERB.search(line)]
        local_literal = {}
        for number, line in enumerate(self.code_lines, 1):
            # `local cauldron = t.player.by_symbol("loc", "mournercauldron_op")`
            # and `local rock = "tinrock2"` both bind a name to its symbol.
            for names, rhs in re.findall(r"^\s*(?:local\s+)?(\w+(?:\s*,\s*\w+)*)\s*=\s*([^=].*)$", line):
                for var in re.split(r"\s*,\s*", names):
                    for text in re.findall(r"\"([^\"]*)\"", rhs):
                        local_literal.setdefault(var, []).append((number, text))
            for var, body in re.findall(r"\blocal\s+(\w+)\s*=\s*\{([^}]*)\}", line):
                for text in re.findall(r"\"([^\"]*)\"", body):
                    local_literal.setdefault(var, []).append((number, text))
            # A table field anywhere on the line binds too, not only one that
            # starts its own line: `{ npc = "a", loc = "b" }` resolves
            # SUS[n].npc as well as the one-field-per-line form does.
            for var, text in re.findall(r"[{,;]\s*(\w+)\s*=\s*\"([^\"]*)\"", line):
                if (number, text) not in local_literal.get(var, []):
                    local_literal.setdefault(var, []).append((number, text))
        self.action_lines = []  # (line, {strings the action names})
        for number in action_lines:
            line = self.code_lines[number - 1]
            named = set(re.findall(r"\"([^\"]*)\"", line))
            for var, literals in local_literal.items():
                if re.search(r"\b%s\b" % re.escape(var), line):
                    named.update(text for _, text in literals)
            for text in named:
                self.action_strings.setdefault(text, number)
            self.action_lines.append((number, named))
        # Which ledger row each line writes: `t.exec("name", ...)` /
        # `t.check("name", ...)` / `t.expect("name", ...)` spans every line of
        # its call, so a target named on the call's second line still knows
        # the row it drives (row_name_at).
        self.row_spans = []  # (first line, last line, row name)
        for match in re.finditer(r"\bt\.(?:exec|check|expect)\s*\(\s*\"([^\"]*)\"", self.code):
            open_index = self.code.index("(", match.start())
            close = matching_close(self.code, open_index)
            first = self.code.count("\n", 0, match.start()) + 1
            last = min(self.code.count("\n", 0, close) + 1, first + 30)
            self.row_spans.append((first, last, match.group(1)))
        self.local_literal = local_literal
        self.loop_literal = self._loop_literals()
        self.use_calls = self._use_calls()
        for index, (number, _) in enumerate(self.stand_on_optins):
            for above in range(number, max(0, number - STAND_ON_MARKER_SPAN - 1), -1):
                match = GUIDE_GAP_RE.match(self.raw_lines[above - 1])
                if match:
                    cite = rs2_citation(match.group(2))
                    if cite:
                        self.stand_on_optins[index] = (number, (above, match.group(1),
                                                                match.group(2).strip(), cite))
                        break
        # The fixture save the run starts from, and the cheats of the
        # `setup = { ... }` table (run before run()'s first row).
        match = re.search(r"\bfixture\s*=\s*\"([^\"]+)\"", self.code)
        self.fixture = match.group(1) if match else None
        self.setup_cheats = []  # (line, text)
        match = re.search(r"\bsetup\s*=\s*\{", self.code)
        if match:
            close = matching_close(self.code, match.end() - 1)
            first = self.code.count("\n", 0, match.start()) + 1
            last = self.code.count("\n", 0, close) + 1
            self.setup_cheats = [(number, text) for number, text in self.cheats if first <= number <= last]
        # "::goto x y z" cheats are gotos too.
        for number, text in self.cheats:
            match = re.match(r"::(?:goto|tele)\s+(\d+)[\s,]+(\d+)[\s,]+(\d+)", text)
            if match:
                self.gotos.append((number,) + tuple(int(v) for v in match.groups()))


    def _bound(self, argtext):
        """The strings an argument names: its literals, and every literal a
        local or table field it mentions was bound to."""
        found = set(re.findall(r"\"([^\"]*)\"", argtext))
        for ident in re.findall(r"\b([A-Za-z_]\w*)\b", re.sub(r"\"[^\"]*\"", "", argtext)):
            found.update(text for _, text in self.local_literal.get(ident, ()))
            found.update(self.loop_literal.get(ident, ()))
        return found

    def _loop_literals(self):
        """{loop variable: {literal}} for `for _, piece in ipairs({ "a", "b" })`
        and `for _, piece in ipairs(PIECES)` over a local table of literals:
        what a use_on(piece, furnace) in the loop body can use (Legends'
        crystal pieces on the furnace). Only _use_calls reads it."""
        out = {}
        for names, table in re.findall(r"\bfor\s+([\w\s,]+?)\s+in\s+i?pairs\s*\(\s*\{([^}]*)\}", self.code):
            for var in re.split(r"\s*,\s*", names.strip()):
                out.setdefault(var, set()).update(re.findall(r"\"([^\"]*)\"", table))
        for names, table in re.findall(r"\bfor\s+([\w\s,]+?)\s+in\s+i?pairs\s*\(\s*(\w+)\s*\)", self.code):
            for var in re.split(r"\s*,\s*", names.strip()):
                out.setdefault(var, set()).update(text for _, text in self.local_literal.get(table, ()))
        return out

    def _use_calls(self):
        """[{line, items, target, row}] for every `use_on(item, target)` call
        -- `t.player.use_on("x", target)`, a local alias `use_on(...)`, and
        `t.exec("row", t.player.use_on, "x", target)`: `items` the strings
        the item argument can be (a literal, or what its local was bound
        to), `target` the ones the target argument can be, `row` the ledger
        row the call writes (row_name_at), or None."""
        calls = []
        code = self.code
        for match in re.finditer(r"\buse_on\b\s*([(,])", code):
            start = code.rfind("\n", 0, match.start()) + 1
            if code[start:match.start()].count('"') % 2:
                continue  # inside a string: a message naming use_on
            if match.group(1) == "(":
                open_index = match.end() - 1
                args = split_top(code[open_index + 1:matching_close(code, open_index)])
            else:
                # the call's arguments follow it inside the enclosing t.exec(...)
                depth = 0
                index = match.start() - 1
                while index >= 0:
                    if code[index] in ")]}":
                        depth += 1
                    elif code[index] in "([{":
                        if depth == 0:
                            break
                        depth -= 1
                    index -= 1
                if index < 0 or code[index] != "(":
                    continue
                args = split_top(code[match.end():matching_close(code, index)])
            if len(args) < 2:
                continue
            line = code.count("\n", 0, match.start()) + 1
            row = self.row_name_at(line)
            # `t.exec("usePotionOnOgre" .. i, ...)`: the literal is only the
            # rows' prefix (usePotionOnOgre1..6)
            prefix = row is not None and re.search(
                r"\bt\.(?:exec|check|expect)\s*\(\s*\"%s\"\s*\.\." % re.escape(row), code) is not None
            calls.append({"line": line, "items": sorted(self._bound(args[0])),
                          "target": self._bound(args[1]), "row": row, "row_prefix": prefix,
                          "item_arg": args[0].strip()})
        return calls

    def row_name_at(self, number):
        """The literal name of the ledger row line `number` writes, or None
        (a helper's `t.exec(name, ...)` with a variable name)."""
        for first, last, name in self.row_spans:
            if first <= number <= last:
                return name
        return None

    def pressed_op(self, number, kind):
        """What the action on line `number` presses on a `kind` target: an op
        number (talk_to/click_npc/click_loc's argument after the target,
        default 1), 'attack' or 'use'; None when the line does not say."""
        first, last = number, number
        for span_first, span_last, _ in self.row_spans:
            if span_first <= number <= span_last:
                first, last = span_first, span_last
                break
        text = " ".join(self.code_lines[first - 1:last])
        verbs = {"npc": r"talk_to|click_npc|npc_op", "loc": r"click_loc|loc_op"}.get(kind)
        if verbs:
            match = re.search(r"\b(?:%s)\s*[,(]\s*[^,()]+?\s*(?:,\s*(\d+)\s*)?[,)]" % verbs, text)
            if match:
                return int(match.group(1)) if match.group(1) else 1
        if kind == "npc" and re.search(r"\b(attack|await_dead\w*)\b", text):
            return "attack"
        if re.search(r"\b(use_on|use_item_on_item)\b", text):
            return "use"
        return None


GUIDE_GAP_RE = re.compile(r"^\s*--\s*GUIDE-GAP:\s*(\S+)\s+(.*)$")
STAND_ON_OPTIN = re.compile(r"\bstand_on_square\s*=\s*true\b")
# How far above a `stand_on_square = true` call its GUIDE-GAP marker may sit:
# the marker, a comment paragraph under it, and a multi-line t.exec.
STAND_ON_MARKER_SPAN = 8


MAPS_ROOT = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "maps")


def rs2_citation(reason):
    """The first `<file>.rs2:<line>` in `reason` that names a real line of a
    real script under server/scripts (by path, or by unique basename), as the
    text cited it; else the first map fact -- `m<x>_<z>.jm2`/`.jl2` (a map
    square's collision or loc file, optionally `:<line>`) that exists under
    OSRS-Content/osrs239-content/maps/ -- which is how a "no route ends on
    that square" marker cites its evidence; None when there is none. A
    GUIDE-GAP marker is evidence only when its citation resolves --
    `foo.rs2:12` for a file that does not exist is not evidence of anything."""
    found = _rs2_line_citation(reason)
    if found:
        return found
    for match in re.finditer(r"\b(m\d+_\d+\.j[ml]2)(?::(\d+))?", reason or ""):
        path = os.path.join(MAPS_ROOT, match.group(1))
        if not os.path.isfile(path):
            continue
        if match.group(2):
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                if not 1 <= int(match.group(2)) <= handle.read().count("\n") + 1:
                    continue
        return match.group(0)
    return None


def _rs2_line_citation(reason):
    for match in re.finditer(r"([\w/.-]+\.rs2):(\d+)", reason or ""):
        cited, line = match.group(1), int(match.group(2))
        candidates = []
        direct = os.path.join(CONTENT_ROOT, cited)
        if os.path.isfile(direct):
            candidates.append(direct)
        else:
            base = os.path.basename(cited)
            for directory, dirnames, filenames in os.walk(CONTENT_ROOT):
                dirnames[:] = [d for d in dirnames if d != "selftest"]
                if base in filenames and os.path.join(directory, base).endswith(cited):
                    candidates.append(os.path.join(directory, base))
        for path in candidates:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                if 1 <= line <= handle.read().count("\n") + 1:
                    return match.group(0)
    return None


CHEATS_DOC = os.path.join(REPO_ROOT, "docs", "QUEST_SERVER_CHEATS.md")
_SANCTIONED = None


def sanctioned_grind_cheats():
    """{debugproc: doc line} -- the GRIND fast-forwards docs/QUEST_SERVER_CHEATS.md
    section A sanctions (the bullet that says "GRIND fast-forward"): every
    `[debugproc,<name>]` and `::<name>` in that bullet. Read from the doc, so
    a future sanctioned cheat needs only its line there."""
    global _SANCTIONED
    if _SANCTIONED is not None:
        return _SANCTIONED
    _SANCTIONED = {}
    if not os.path.isfile(CHEATS_DOC):
        return _SANCTIONED
    with open(CHEATS_DOC, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")
    in_a = False
    bullets = []  # [(first line, text)]
    for number, line in enumerate(lines, 1):
        if line.startswith("## "):
            in_a = line.startswith("## A.")
            continue
        if not in_a:
            continue
        if line.startswith("- "):
            bullets.append([number, line])
        elif bullets and line.startswith("  "):
            bullets[-1][1] += " " + line.strip()
        elif not line.strip() and bullets:
            bullets.append([number, ""])
    for number, text in bullets:
        if not re.search(r"grind\s+fast-forward", text, re.I):
            continue
        for name in re.findall(r"\[debugproc,(\w+)\]|::(\w+)", text):
            _SANCTIONED.setdefault(name[0] or name[1], number)
    return _SANCTIONED


# ------------------------------------------------------------------ equivalent markers
#
# A GUIDE-GAP marker declares a leg the CONTENT lacks. Some guide steps are
# not gaps at all, and the marker vocabulary below says so, each kind with
# evidence this tool checks (Grader.verify_equivalent) -- a marker that does
# not verify is ignored here, refused by lint_quest.py and a gate finding:
#
#   -- BRANCH-IN: <sibling test_id> <step> [reason]
#        a mutually exclusive guide branch (Throne of Miscellania's Astrid /
#        Brand courting) driven by a SIBLING test of the same guide: the
#        sibling is a committed, green QUEUE row with the same Quest Helper
#        file, and its own grading (with its BRANCH-IN markers switched off,
#        so two siblings cannot vouch for each other) reads <step> DRIVEN.
#   -- PARTNER: <step> ::<cheat> [reason]
#        a two-player step whose guide text names another player, performed
#        by a cheat listed in docs/QUEST_SERVER_CHEATS.md's "Two-player
#        partner affordances" table, which the test calls and (once a
#        ledger exists) a PASS row reports.
#   -- NOT-A-STEP: <step> <reason>
#        a Quest Helper plugin-state step: a QuestSyncStep, or a target-less
#        DetailedQuestStep whose text asks the player to open the journal to
#        sync the plugin's state (Clock Tower's syncStep). Nothing else.
#   -- OBSOLETE: <step> <reason citing the wiki with ?oldid=N or #Section>
#        a guide step the live game removed, or one naming an object with no
#        interaction in the real game.
#   -- ANY-OF: <step> <driven step> <reason citing .rs2:line / map / wiki>
#        a guide step one of several ways satisfy, done another way: <driven
#        step> is a PASS action row of this run (named exactly, or as
#        `<name>.<check>` / `<name>-<n>`) or a guide step graded DRIVEN.
#
# Such a step grades EQUIVALENT, a neutral class: the verdict can read FULL.

EQUIVALENT_KINDS = ("BRANCH-IN", "PARTNER", "NOT-A-STEP", "OBSOLETE", "ANY-OF")
EQUIVALENT_RE = re.compile(r"^\s*--\s*(BRANCH-IN|PARTNER|NOT-A-STEP|OBSOLETE|ANY-OF):\s*(.*?)\s*$")
# oldschool.runescape.wiki/w/<Page>?oldid=<n>[#Section] or .../w/<Page>#<Section>
WIKI_CITE_RE = re.compile(r"oldschool\.runescape\.wiki/w/[^\s?#)\]`\"']+"
                          r"(?:\?oldid=\d+(?:#[^\s)\]`\"']+)?|#[^\s)\]`\"']+)")
TWO_PLAYER_TEXT = re.compile(r"\b(?:another|other) player\b|\bpartner\b", re.I)
SYNC_TEXT = re.compile(r"\bsync\b", re.I)
SYNC_TEXT_OBJECT = re.compile(r"\bjournal\b|\bstate\b", re.I)


def parse_equivalent_marker(line):
    """{kind, step, sibling|cheat|other, reason, error} for one raw Lua line
    carrying an equivalent marker, else None. `error` names a malformed one."""
    match = EQUIVALENT_RE.match(line)
    if not match:
        return None
    kind, body = match.group(1), match.group(2)
    out = {"kind": kind, "step": None, "reason": "", "error": None, "text": body}
    if kind == "BRANCH-IN":
        found = re.match(r"^(\w+)\s+(\w+)(?:\s+(.*))?$", body)
        if found:
            out.update(sibling=found.group(1), step=found.group(2), reason=(found.group(3) or "").strip())
        else:
            out["error"] = "not `-- BRANCH-IN: <sibling test_id> <step> [reason]`"
    elif kind == "PARTNER":
        found = re.match(r"^(\w+)\s+::(\w+)(?:\s+(.*))?$", body)
        if found:
            out.update(step=found.group(1), cheat=found.group(2), reason=(found.group(3) or "").strip())
        else:
            out["error"] = "not `-- PARTNER: <step> ::<cheat> [reason]`"
    elif kind == "ANY-OF":
        found = re.match(r"^(\w+)\s+([\w.-]+)\s+(\S.*)$", body)
        if found:
            out.update(step=found.group(1), other=found.group(2), reason=found.group(3).strip())
        else:
            out["error"] = "not `-- ANY-OF: <step> <driven step> <reason citing .rs2:line or the wiki>`"
    else:  # NOT-A-STEP, OBSOLETE
        found = re.match(r"^(\w+)\s+(\S.*)$", body)
        if found:
            out.update(step=found.group(1), reason=found.group(2).strip())
        else:
            out["error"] = "not `-- %s: <step> <reason>`" % kind
    return out


def wiki_citation(reason):
    """The first pinned OSRS wiki citation in `reason` -- a page URL carrying
    `?oldid=<n>` or a `#Section` anchor -- else None. A bare page URL is not a
    citation: the page moves under it."""
    match = WIKI_CITE_RE.search(reason or "")
    return match.group(0).rstrip(".,;:") if match else None


_PARTNERS = None


def partner_cheats():
    """{cheat name: doc line} -- the two-player partner affordances
    docs/QUEST_SERVER_CHEATS.md lists in its "Two-player partner affordances"
    table (a row's first cell is the `::<cheat>`)."""
    global _PARTNERS
    if _PARTNERS is not None:
        return _PARTNERS
    _PARTNERS = {}
    if not os.path.isfile(CHEATS_DOC):
        return _PARTNERS
    with open(CHEATS_DOC, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")
    inside = False
    for number, line in enumerate(lines, 1):
        if line.startswith("#"):
            inside = bool(re.search(r"two-player partner", line, re.I))
            continue
        if not inside:
            continue
        found = re.match(r"^\|\s*`::(\w+)`\s*\|", line)
        if found:
            _PARTNERS.setdefault(found.group(1), number)
    return _PARTNERS


def git_tracked(path):
    """Is `path` committed (known to git's index) in this checkout?"""
    import subprocess
    result = subprocess.run(["git", "-C", REPO_ROOT, "ls-files", "--error-unmatch",
                             os.path.relpath(path, REPO_ROOT)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return result.returncode == 0


_SIBLING_GRADES = {}


def sibling_grade(test_id):
    """The sibling's own per-step classes, graded WITHOUT its BRANCH-IN
    markers (a sibling may vouch for a step only by driving it itself)."""
    if test_id not in _SIBLING_GRADES:
        grader = Grader(test_id, follow_branch_in=False)
        _SIBLING_GRADES[test_id] = grader.grade()
    return _SIBLING_GRADES[test_id]


# A read of a cheat's effect: a named check, an expect, a var/inv/stage read.
READBACK = re.compile(r"\bt\.(check|expect\w*|msg\.expect\w*|var\.\w+|inv\.\w+|quest\.(stage|expect\w*)|"
                      r"stat\.\w+)\s*\(|\binv\.count\s*\(|await_server")
READBACK_SPAN = 12


# The driver's reach retry, when every walkable neighbour of a loc refused the
# press, stands on the loc's OWN square with ::goto (SEAM use_on_own_square,
# script/plugins/quest_driver/pointer.lua QD.player._reach_retry) and a press
# that then lands says so: `[<sym> reached from its own square X,Z, which no
# route can end on and the harness stood on with ::goto, ...]`, or in the
# retry's own note `pressed from X,Z (the loc's own square, standoff
# suppressed, stood on with ::goto) -> ok`.
STAND_ON_BRACKET = re.compile(r"\[(\S+) reached from its own square (\d+),(\d+), which no route can end on "
                              r"and the harness stood on with ::goto")
STAND_ON_NOTE = re.compile(r"pressed from (\d+),(\d+) \(the loc's own square, standoff suppressed, "
                           r"stood on with ::goto[^)]*\) -> ok")


def stand_ons(rows):
    """[{row, step, symbol, tile}] -- every PASS row the reach retry satisfied
    from a square it ::goto'd onto. A row whose stand-on press was refused
    (a door that is SUPPOSED to refuse) is not one."""
    out = []
    for row in rows:
        if row["verdict"] != "PASS" or "stood on with ::goto" not in row["detail"]:
            continue
        match = STAND_ON_BRACKET.search(row["detail"])
        if match:
            symbol, tile = match.group(1), "%s,%s" % (match.group(2), match.group(3))
        else:
            match = STAND_ON_NOTE.search(row["detail"])
            if not match:
                continue
            tile = "%s,%s" % (match.group(1), match.group(2))
            verb = re.search(r"\b\w+\(\s*([a-z0-9_]+)", row["detail"])
            symbol = verb.group(1) if verb else ""
        out.append({"row": row["index"], "step": row["step"], "symbol": symbol.rstrip("*"), "tile": tile})
    return out


def ledger_path(test_id, quest_dir):
    candidates = [
        os.path.join(REPO_ROOT, "build", "quest_gate", test_id, "ledger.tsv"),
        os.path.join(CONTENT_ROOT, "selftest", "quests", quest_dir, "play-%s" % test_id, "ledger.tsv"),
        os.path.join(CONTENT_ROOT, "selftest", "quests", quest_dir, "play", "ledger.tsv"),
        os.path.join(CONTENT_ROOT, "selftest", "quest_tests", test_id, "ledger.tsv"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


# ------------------------------------------------------------------ queue

def queue_row(test_id):
    with open(QUEUE_PATH, "r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            if row["test_id"] == test_id:
                return row
    return None


VERIFY_ONLY = "VERIFY ONLY"


def joint_verify(row, sibling_row):
    """Is a sibling that is not green yet being verified in the SAME round as
    this test? Two siblings reopened together (misc and misc_astrid, batch
    matthew-mbp-m4-b59) each name the other in a BRANCH-IN marker, so neither
    can wait for the other's row to turn green first. The orchestrator marks
    both rows `VERIFY ONLY` for one batch (queue.py set --owner <batch>
    --failure "VERIFY ONLY ..."); such a sibling may vouch while this row is
    itself VERIFY ONLY or already green in that batch. The sibling still has
    to DRIVE the step in its own grading, and a sibling that is sent back
    loses the note, so this test goes RED with it."""
    if sibling_row.get("status") != "todo":
        return False
    if not (sibling_row.get("last_failure") or "").startswith(VERIFY_ONLY):
        return False
    owner = sibling_row.get("owner") or ""
    if not owner or (row.get("owner") or "") != owner:
        return False
    if row.get("status") == "green":
        return True
    return row.get("status") == "todo" and (row.get("last_failure") or "").startswith(VERIFY_ONLY)


def guide_path(row):
    directory = os.path.join(GUIDES_DIR, row["helper_dir"])
    if row.get("helper_file"):
        name = row["helper_file"]
        if not name.endswith(".java"):
            name += ".java"
        return os.path.join(directory, name)
    if not os.path.isdir(directory):
        return None
    candidates = []
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".java"):
            continue
        with open(os.path.join(directory, name), "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        if re.search(r"extends\s+(Basic|ComplexState)QuestHelper", text):
            candidates.append(name)
    if not candidates:
        return None
    if len(candidates) > 1:
        # shieldofarrav: two helpers in one dir -- pick the one whose name
        # shares the most with the test id / quest dir.
        key = norm(row["test_id"] + row["quest_dir"])
        candidates.sort(key=lambda n: -sum(1 for w in camel_words(n[:-5]) if w in key))
    return os.path.join(directory, candidates[0])


# ------------------------------------------------------------------ grading

GATE_WORDS = ("door", "gate", "wall", "fence", "railing", "barrier", "entrance", "tunnel",
              "pipe", "passage", "hole", "tombstone", "raft", "rockslide", "trapdoor",
              "stair", "ladder", "puzzle", "lever", "bookcase", "curtain", "cave", "rope")
TRAVEL_WORDS = ("ladder", "stair", "staircase", "steps", "trapdoor")
# A route obstacle (route_crossing): a loc a walk has to get over or through.
# GATE_WORDS plus the agility-style crossings a guide ConditionalStep shows
# one per zone (Regicide's rockslides, spear traps, rock swing, log balance,
# pitfalls, tripwire, dense forests).
ROUTE_OBSTACLE_WORDS = GATE_WORDS + ("trap", "rockslide", "swing", "log", "bridge", "ledge", "grid",
                                     "forest", "leaves", "wire", "stepping", "plank", "vine", "crevice",
                                     "obstacle", "balance", "web", "pitfall", "cross", "shortcut", "stile")
ROUTE_OBSTACLE_TEXT = re.compile(r"\b(cross|climb|swing|jump|squeeze|crawl|balance|walk past|go through|"
                                 r"step over|pass the|disable)", re.I)
# The newest server line of an attempt that says it did not get through.
ATTEMPT_FAILED = re.compile(r"\bfail(?:s|ed)?\b|activating the trap", re.I)
NEAR_TILES = 48
OBTAIN_VERB = re.compile(r"^\s*(pick|take|get|grab|collect|buy|purchase|obtain|kill|mine|steal|"
                         r"pickpocket|loot|fill|dig|gather|catch|fish|chop|milk)", re.I)
ROW_ACTION = re.compile(r"click|talk|use_on|use_item|attack|inv_op|op\(|oploc|opnpc|opobj|pick|took|"
                        r"take|killed|dead|equip|bought|buy|drive\.op|dialogue npc", re.I)


def _near(point, goto):
    if point is None:
        return False
    _, x, y, _z = goto
    # A dungeon is the surface's y + 6400; compare both frames.
    for dy in (0, 6400, -6400):
        if abs(point[0] - x) <= NEAR_TILES and abs(point[1] + dy - y) <= NEAR_TILES:
            return True
    return False


def _distance(point, goto):
    _, x, y, _z = goto
    return min(max(abs(point[0] - x), abs(point[1] + dy - y)) for dy in (0, 6400, -6400))


class Grader:
    def __init__(self, test_id, test_path=None, test_text=None, follow_branch_in=True, ledger_file=None):
        """`test_path`/`test_text` grade another copy of the test (a proof copy
        under build/, lint_quest's text) against this test_id's QUEUE row,
        guide and ledger. `ledger_file` grades another run's ledger (a
        reverted green's published one) in place of the default lookup.
        `follow_branch_in=False` ignores BRANCH-IN markers (a sibling graded
        to verify one)."""
        self.test_id = test_id
        self.follow_branch_in = follow_branch_in
        self._driven = {}
        self._related = {}
        self._reserved = None
        self.line_credits = {}  # action line -> the step a mention credited it to
        # step name -> why each mention that named its target did not count
        # (another step's row, the wrong op): the reason an UNMATCHED shows.
        self.refusals = {}
        # step name -> why no use_on of the guide's item drove a "use X on Y"
        # step (use_item_driven): the UNMATCHED reason, naming both items.
        self.item_refusals = {}
        self.row = queue_row(test_id)
        assert self.row, "test_id %r is not in %s" % (test_id, QUEUE_PATH)
        self.quest_dir = self.row["quest_dir"]
        path = guide_path(self.row)
        assert path and os.path.isfile(path), "no Quest Helper guide for %r (%s)" % (test_id, path)
        self.guide = Guide(path)
        self.test = Test(test_id, test_path or os.path.join(REPO_ROOT, "test", "quests", test_id + ".lua"),
                         text=test_text)
        self.ledger_path = ledger_file or ledger_path(test_id, self.quest_dir)
        rows, _ = ledger.read(self.ledger_path) if self.ledger_path else (None, None)
        self.rows = rows or []
        self.pass_rows = [r for r in self.rows if r["verdict"] == "PASS"]
        # A goto row is travel, whatever step it is named after
        # ("goto-talkToElena" walks TO the step; it does not do it). `tele`
        # is a teleport row (`tele-...`, `teleport...`), never a guide step
        # that merely starts with those letters: Spirits of the Elid's
        # `telegrabKey.cast` IS the step's action (seam27), and the bare
        # prefix used to drop it, so an ANY-OF naming it could not verify.
        # ...unless the row is named after a guide step that merely starts
        # with those letters and its source line is not itself a teleport:
        # Darkness of Hallowvale's `goToMines` is a talk to a Vyrewatch, and
        # the bare prefix (case-folded) dropped its rows, so the step fell
        # through to a stray narration match (seam doh_gotomines_graded_gap).
        # Fenkenstrain's `goToMonsterFloor1` row, a goto_tile, stays travel.
        guide_names = {norm(name) for name in self.guide.steps} | {norm(name) for name in self.guide.step_alias}

        def guide_step_row(name):
            if norm(re.split(r"[.\-:/ ]", name)[0]) not in guide_names:
                return False
            for first, last, row_name in self.test.row_spans:
                if row_name == name:
                    source = " ".join(self.test.code_lines[first - 1:last])
                    return not re.search(r"goto_tile|::goto|::tele|player\.teleport", source)
            return True
        self.action_rows = [r for r in self.pass_rows
                            if (not re.match(r"^(goto|walk|travel|tele(?:port|[^a-z]|$)|quest\.|setup|reset)",
                                             r["step"], re.I)
                                or guide_step_row(r["step"]))
                            and not re.search(r"goto_tile|::goto", r["detail"])]
        self.bound_varp = None
        match = re.search(r"t\.quest\.bind\s*\(\s*\{[^}]*?varp\s*=\s*\"(\w+)\"", self.test.code, re.S)
        if match:
            self.bound_varp = match.group(1)
        self.consumed = set()
        self.loc_uses = {}
        self.quest_vars = self._quest_vars()
        self.quest_words = set(words(self.quest_dir.replace("_", " ") + " " + self.test_id.replace("_", " ")))
        self.quest_words |= {"quest", "main", "stage", "state", "progress"}
        self.effects = self._cheat_effects()
        self.narration = narrating_writes(self.quest_dir)
        self.softs = soft_markers(self.quest_dir)
        self.stand_ons = stand_ons(self.rows)
        self.claimed_stand_ons = set()
        self._start_rows()

    # -- the run's start (owner ruling 2026-10-05: the run's first goto and
    # the setup placement obey the door rule like any other goto)

    # A setup cheat that moves the player is judged as a hop of its own from
    # the fixture's tile, and an unstamped first goto leaves from the
    # fixture's tile (or the last placement's landing). False is the reading
    # before seam matthew-mbp-m4-b64-seam1 (neither ever judged), kept for
    # the fixtures.
    START_JUDGED = True
    PLACEMENT = "(setup placement) "
    _start_departure = None   # ((x, z, level), label): where an unstamped first goto leaves from
    _start_labels = {}        # (row index, row step) -> how the charge names a start departure

    def setup_placements(self):
        """[(line, text, landing (x, z, level) or None)]: the setup cheats
        that move the player, in order -- `::goto x z [level]` / `::tele`
        (its numbers, else no landing), or a debugproc whose body, followed
        through its @labels and ~procs two levels down (script_teleports),
        calls p_teleport / p_telejump (the one tile those calls name, else
        no landing). Enter the Abyss's `::entertheabyss` runs
        [debugproc,entertheabyss] -> @eta_debug_reset ->
        p_teleport(^eta_wildy_coord), 3106,3558,0, across the Wilderness
        Ditch from the fixture's Lumbridge tile."""
        _, _, debugprocs = content_index()
        out = []
        for number, text in self.test.setup_cheats:
            parts = text[2:].split()
            if not parts:
                continue
            command = parts[0]
            if command in ("goto", "tele", "teleport"):
                match = re.match(r"::\w+\s+(\d+)[\s,]+(\d+)(?:[\s,]+([0-3]))?\s*$", text)
                landing = (int(match.group(1)), int(match.group(2)), int(match.group(3) or 0)) if match else None
                out.append((number, text, landing))
            elif command in debugprocs:
                rel, line, _ = debugprocs[command]
                moves, landings = script_teleports(rel, line)
                if moves:
                    out.append((number, text, next(iter(landings)) if len(landings) == 1 else None))
        return out

    def _first_reading(self):
        """Where the run first reads the player standing, before any row
        that may have moved them (_row_moves): the first goto's departure
        stamp, or the first reading row's tile. None when unknown (the first
        reading is an unstamped goto's landing, or a row before it moves)."""
        track = self.player_track()
        if not track:
            return None
        position, point, is_goto = track[0]
        if is_goto:
            stamp = self._goto_from.get(position)
            if stamp is None or any(self._row_moves(row) for row in self.rows[:position]):
                return None
            return stamp
        if any(self._row_moves(row) for row in self.rows[:position + 1]):
            return None
        return point

    def _start_rows(self):
        """Judge the run's start (START_JUDGED). When setup cheats move the
        player (setup_placements), the setup's net placement -- from the
        fixture's tile to where the LAST of them leaves the player -- becomes
        a ledger row of its own at the front of self.rows, named
        "(setup placement) <that cheat>", a goto `at <landing> from <fixture
        tile>` that every goto rule judges. The setup runs before any row
        (nothing is done between two of its cheats), so only its net hop is
        a hop: `::entertheabyss` then `::goto 3106 3510 0` stands the player
        south of the Wilderness Ditch, which the run then crosses by its op.
        The landing is the run's own first reading (_first_reading) when
        there is one, else the tile the last cheat names; unknown, nothing
        is judged. An unstamped first goto then leaves from that landing, or
        from the fixture's tile when no setup cheat moves the player
        (player_track stamps it). Druid's goto-talkToKaqemeex from the
        fixture's 3206,3233 to 2925,3486, past membergater 2935,3450, and
        Enter the Abyss's ::entertheabyss (3206,3233 -> 3106,3558, over the
        ditch) were skipped by every rule before seam
        matthew-mbp-m4-b64-seam1."""
        self._start_labels = {}
        self._start_departure = None
        if not self.START_JUDGED or not self.rows or not self.test.fixture:
            return
        start = fixture_tile(self.test.fixture)
        if start is None:
            return
        label = "the fixture's tile, %s" % self.test.fixture
        placements = self.setup_placements()
        if not placements:
            self._start_departure = (start, label)
            return
        line, text, landing = placements[-1]
        reading = self._first_reading()
        if reading is not None:
            landing = reading
        self._track = None
        if landing is None:
            return
        if landing != start:
            row = {"index": "0", "step": self.PLACEMENT + text, "verdict": "PASS", "ticks": "0", "shots": "",
                   "detail": "at %d,%d,%d from %d,%d,%d" % (landing + start), "placement": line}
            self.rows = [row] + self.rows
            self._start_labels[(row["index"], row["step"])] = label
        self._start_departure = (landing, "the landing of setup %s (line %d)" % (text, line))

    def _departure_row(self, from_row):
        """The row a stamped hop's charge says it left from: the goto row
        itself (`<step> departure`), or for a start hop the fixture's tile
        or the setup placement it left from."""
        label = self._start_labels.get((from_row["index"], from_row["step"]))
        if label:
            return dict(from_row, index="start", step=label)
        return dict(from_row, step=from_row["step"] + " departure")

    # -- the cheats

    def _quest_vars(self):
        names = set()
        if self.bound_varp:
            names.add(self.bound_varp)
        for rel, lines in quest_rs2(self.quest_dir):
            for line in lines:
                for var in re.findall(r"%(\w+)\s*=[^=]", line):
                    names.add(var)
        base = os.path.join(CONTENT_ROOT, "quests", self.quest_dir, "configs")
        if os.path.isdir(base):
            for filename in os.listdir(base):
                if filename.endswith((".varp", ".varbit")):
                    with open(os.path.join(base, filename), "r", encoding="utf-8", errors="replace") as handle:
                        names.update(re.findall(r"^\[(\w+)\]", handle.read(), re.M))
        return names

    def bring_alongs(self):
        out = set()
        for var in self.guide.item_requirements + self.guide.item_recommended:
            item = self.guide.items.get(var)
            if item and not item["obtainable"]:
                out.update(item["ids"])
        return out

    def item_words(self, symbol):
        found = set(words(symbol.replace("_", " ")))
        for item in self.guide.items.values():
            if symbol in item["ids"]:
                found.update(words(item["name"]))
        return found

    # -- seam matthew-mbp-m4-b67-seam1 (a): an item cheat is charged to a
    # step only for the step's own item, never for a word the two merely
    # share. Rum Deal's setup `::give deal_slayer_gloves 1` (a bring-along
    # the guide's getItemRequirements lists: slayerGloves, whose alternate
    # deal_slayer_gloves is) was graded CHEAT against useBucketOnTap ("Fill a
    # bucket from the output tap", deal_brewvat_tap) because both carry the
    # content's own symbol prefix "deal". False is the reading before it.
    CHEAT_BY_OWN_ITEM = True
    # A symbol prefix is the quest's own (not a word of any step) when at
    # least this many of the symbols the guide names start with it and they
    # are at least PREFIX_SHARE of them all (Rum Deal: deal_, 25 of 30).
    PREFIX_MIN = 4
    PREFIX_SHARE = 0.5

    def bring_along_alts(self):
        """Every symbol a guide bring-along (getItemRequirements /
        getItemRecommended, not obtainable during the quest) may be given as:
        its ids and its alternates. Rum Deal's slayerGloves is
        slayerguide_slayer_gloves OR deal_slayer_gloves."""
        out = set()
        for var in self.guide.item_requirements + self.guide.item_recommended:
            item = self.guide.items.get(var)
            if item and not item["obtainable"]:
                out.update(item["ids"])
                out.update(item.get("alts") or ())
        return out

    def prefix_words(self):
        """words() of the quest's dominant symbol prefix -- the first `_`
        token most of the symbols the guide names share ("deal" for Rum
        Deal's deal_pete, deal_brewvat_tap, deal_slayer_gloves ...), which
        says nothing about WHICH item or loc -- or an empty set when no token
        reaches PREFIX_MIN symbols and PREFIX_SHARE of them."""
        cached = getattr(self, "_prefix_words", None)
        if cached is not None:
            return cached
        symbols = {s for step in self.guide.steps.values() for _, s in step.targets}
        symbols |= {i for item in self.guide.items.values() for i in item["ids"]}
        counts = {}
        for symbol in symbols:
            if "_" in symbol:
                head = symbol.split("_")[0]
                counts[head] = counts.get(head, 0) + 1
        found = set()
        if counts:
            head, count = max(sorted(counts.items()), key=lambda pair: pair[1])
            if count >= self.PREFIX_MIN and count >= self.PREFIX_SHARE * len(symbols):
                found = set(words(head))
        self._prefix_words = found
        return found

    def step_input_items(self, step):
        """The items a step HOLDS to do its work and does not obtain: the
        ids and alternates of its req_vars, unless the step is an ItemStep
        (a pickup, whose requirement is the item it picks up) or the item is
        one of its own obj targets. Rum Deal's useBucketOnTap ("Fill a
        bucket", req bucket -> bucket_empty) spends the empty bucket: a
        setup `::give bucket_empty` is its input, not its result."""
        if step.kind == "ItemStep":
            return set()
        own = [s for k, s in step.targets if k == "obj"]
        out = set()
        for var in step.req_vars:
            item = self.guide.items.get(var)
            if not item:
                continue
            for symbol in list(item["ids"]) + list(item.get("alts") or ()):
                if not any(same_thing("obj", target, symbol) for target in own):
                    out.add(symbol)
        return out

    def is_own_item(self, step, symbol):
        """Is `symbol` the item the step obtains: one of its obj targets (or
        that target's family), or for an ItemStep one of its req_vars ids or
        alternates?"""
        own = [s for k, s in step.targets if k == "obj"]
        if step.kind == "ItemStep":
            for var in step.req_vars:
                item = self.guide.items.get(var)
                if item:
                    own += list(item["ids"]) + list(item.get("alts") or ())
        return any(target == symbol or same_thing("obj", target, symbol) or symbol in obj_family(target)
                   for target in own)

    def _cheat_effects(self):
        """[{line, text, kind: give|var|teleport, items, vars, words}] -- the cheats
        that do quest work. Resets, stat/level setup and bring-along gives are
        dropped here."""
        _, _, debugprocs = content_index()
        bring = self.bring_alongs()
        # a bring-along given as one of its alternates is still a bring-along
        # (Rum Deal's deal_slayer_gloves for slayerGloves; seam b67-seam1 (a))
        bring_given = (bring | self.bring_along_alts()) if self.CHEAT_BY_OWN_ITEM else bring
        effects = []
        for number, text in self.test.cheats:
            parts = text[2:].split()
            if not parts:
                continue
            command = parts[0]
            args = parts[1:]
            effect = {"line": number, "text": text, "items": [], "vars": [], "teleport": False}
            if command in GIVE_CHEATS and args:
                if args[0] in bring_given:
                    # A bring-along is fine to give -- unless the ladder has a
                    # step whose whole job is picking it up (Fishing Contest's
                    # garlic on the Seers' table).
                    effect["obtain_only"] = True
                effect["items"].append(args[0])
            elif command in ("setvar", "setvarp", "setvarbit") and args:
                if args[0] not in self.quest_vars:
                    continue  # a prerequisite quest's var
                if len(args) > 1 and args[1] in ("0",):
                    continue
                effect["vars"].append(args[0])
            elif command in ("complete", "completequest"):
                target = args[0] if args else ""
                if norm(target) not in (norm(self.quest_dir), norm(self.test_id),
                                        norm(self.quest_dir.replace("quest_", ""))):
                    continue  # a prerequisite quest
                effect["vars"].append(self.bound_varp or target)
            elif command in ("goto", "tele", "teleport"):
                continue  # graded as a goto against the steps it lands past
            elif command in sanctioned_grind_cheats() and self.readback(number):
                continue  # a sanctioned grind fast-forward, read back (named_cheat)
            elif command in debugprocs:
                rel, line, body = debugprocs[command]
                body_text = "\n".join(body)
                items = [i for i in re.findall(r"inv_add\(\s*\w+\s*,\s*(\w+)", body_text) if i not in bring]
                writes = []
                for var, value in re.findall(r"%(\w+)\s*=\s*([\^\w]+)", body_text):
                    if var not in self.quest_vars or re.search(r"not_started|^0$|^\^?false$", value):
                        continue
                    if var != self.bound_varp and value.endswith("_complete"):
                        continue  # a prerequisite quest forced complete
                    writes.append(var)
                effect["items"] = items
                effect["vars"] = writes
                effect["where"] = "%s:%d" % (rel, line)
                if not items and not writes:
                    continue  # a reset / teleport adapter
            else:
                continue
            found = set()
            for item in effect["items"]:
                found.update(self.item_words(item))
            for var in effect["vars"]:
                found.update(words(var.replace("_", " ")))
            if command in debugprocs and command not in GIVE_CHEATS:
                found.update(words(command.replace("_", " ")))
            found -= set(words(self.quest_dir.replace("_", " ") + " " + self.test_id.replace("_", " ")))
            found -= {"quest", "test", "give", "cheat"}
            effect["words"] = found
            effects.append(effect)
        return effects

    # -- per-step

    def step_words(self, step):
        found = set(words(step.text)) | set(camel_words(step.name))
        if step.kind not in ("NpcStep", "ObjectStep", "ItemStep", "DetailedQuestStep", "ConditionalStep"):
            found |= set(camel_words(re.sub(r"Step$", "", step.kind)))
        for kind, symbol in step.targets:
            found.update(words(symbol.replace("_", " ")))
        return found

    @staticmethod
    def row_names_step(row_step, step_name):
        """Is a ledger row called `row_step` named after guide step `step_name`
        (talkToUnferth, talkToUnferth-dialog, drunkenAli-beer1 for drunkenAli,
        pickpocketVillager1 for pickpocketVillager)?"""
        name = norm(step_name)
        row_name = norm(row_step)
        if not name or not row_name:
            return False
        segments = [norm(part) for part in re.split(r"[.\-:/ ]", row_step)]
        return row_name == name or row_name.startswith(name) or \
            (len(name) > 6 and any(seg.startswith(name) for seg in segments)) or \
            (len(row_name) >= 6 and name.startswith(row_name) and
             (re.search(r"\d", name[len(row_name):]) is not None or len(row_name) / len(name) >= 0.75))

    def related_steps(self, step):
        """`step`, its sub-steps and leaves, and every step it is a sub-step or
        leaf of: a row named after any of them is not ANOTHER step's row."""
        if step.name in self._related:
            return self._related[step.name]
        out = {step.name}
        out.update(step.substeps)
        out.update(self.guide.leaves(step.name))
        for other in self.guide.steps.values():
            if step.name in other.substeps or step.name in self.guide.leaves(other.name) or \
                    step.name in other.children:
                out.add(other.name)
        self._related[step.name] = out
        return out

    def reservations(self):
        """{row name: {guide step}} for every row that IS some guide step's own
        evidence: the first PASS action row named after the step, plus the
        rows of that same visit that press nothing (`talkToRolad-dialog`
        after `talkToRolad`). A later row that presses again, whatever the
        author named it (`talkToRoladWithPages`, `talkToJorral-handin`), is
        another visit and is not reserved."""
        if self._reserved is not None:
            return self._reserved
        reserved = {}
        for name in self.guide.steps:
            if len(norm(name)) < 6:
                continue
            first = next((row for row in self.action_rows if self.row_names_step(row["step"], name)), None)
            if first is None:
                continue
            reserved.setdefault(first["step"], set()).add(name)
            for row in self.action_rows:
                rest = row["step"][len(first["step"]):]
                if not (row["step"].startswith(first["step"]) and rest[:1] in ("-", ".", ":", "/", "_", " ")):
                    continue
                line = self.row_line(row["step"])
                if line is not None and (self.test.pressed_op(line, "npc") is not None or
                                         self.test.pressed_op(line, "loc") is not None):
                    continue  # `talkToJorral-handin` presses again: a visit of its own
                reserved.setdefault(row["step"], set()).add(name)
        self._reserved = reserved
        return reserved

    def claimed_by_other(self, row_step, step):
        """The OTHER guide step whose own evidence the row called `row_step`
        is, or None. A row the author named for one guide step drives that
        step; the target it presses does not also drive every other step on
        the same npc or loc (The Feud's `pickpocketVillager1` press on
        feud_villager_multi_1 is not `talkToAVillager`)."""
        if not row_step or self.row_names_step(row_step, step.name):
            return None
        related = self.related_steps(step)
        for other in sorted(self.reservations().get(row_step, ())):
            if other not in related:
                return other
        return None

    def step_verb(self, step):
        """The op word a step's own text (else its name) starts with."""
        first = re.match(r"\s*([A-Za-z]+)", step.text or "")
        if not first:
            first = re.match(r"([a-z]+)", step.name or "")
        verb = first.group(1).lower() if first else ""
        return STEP_VERB_ALIAS.get(verb, verb)

    def clause_verbs(self, step):
        """The verbs that open the LATER clauses of a step's text ("Climb up
        the walls and search the marked floor" -> {'search'}): each clause
        after the first, split at `and`/`then`/`,`/`;`/`.`, by its first
        word. The leading verb is step_verb's, not one of these."""
        clauses = re.split(r"\band\b|\bthen\b|[,;.]", step.text or "", flags=re.I)
        found = set()
        for clause in clauses[1:]:
            first = re.match(r"\s*([A-Za-z]+)", clause)
            if first:
                verb = first.group(1).lower()
                found.add(STEP_VERB_ALIAS.get(verb, verb))
        return found

    def op_conflict(self, step, line, kind, symbol, text):
        """Why the action on `line` is not this step's own op on its target, or
        None. Only a step whose leading verb IS one of the target's menu ops
        is held to it (Talk-to on a villager whose op3 is Pickpocket): a
        "Return to"/"Bring" step, or a target with no such op, is not."""
        verb = self.step_verb(step)
        if not verb:
            return None
        menu = dict(ops_of(kind, symbol))
        for number, op in ops_of(kind, text).items():
            menu.setdefault(number, op)
        if kind == "npc" and verb == "attack":
            menu.setdefault(0, "attack")
        if not any(op_word(op) == verb for op in menu.values()):
            return self.travel_op_conflict(step, line, kind, symbol, text, verb, menu)
        pressed = self.test.pressed_op(line, kind)
        if pressed is None:
            return None
        if pressed == "use" and step.req_vars:
            return None  # the item the step lists, used on its target: the content's own trigger
        pressed_name = pressed if isinstance(pressed, str) else ops_of(kind, text).get(pressed) or menu.get(pressed)
        if pressed_name is None or op_word(pressed_name) == verb:
            return None
        return "line %d presses %r on %s, not the step's %r" % (
            line, pressed_name if isinstance(pressed, str) else "op%d %s" % (pressed, pressed_name), text, verb)

    def travel_op_conflict(self, step, line, kind, symbol, text, verb, menu):
        """A step that goes THROUGH a loc (`enterHamLair`, "Climb down ...",
        "Cross ...") whose verb is not itself a menu op: the press must be one
        of the loc's travel ops (Climb-down/Enter/Cross/Open...), or the row it
        writes must show the player moved across it. A gating op alone --
        Pick-Lock, Unlock, Light -- readies the crossing and is not it (seam35
        two_op_loc_credits_the_wrong_op). Only a loc that HAS a travel op in
        some state is held to this; a `use` press or an unread op is not."""
        if kind != "loc" or verb not in TRAVEL_STEP_VERBS:
            return None
        offered = family_op_words(kind, symbol) | family_op_words(kind, text)
        if not offered & set(TRAVEL_OPS):
            return None
        pressed = self.test.pressed_op(line, kind)
        if not isinstance(pressed, int):
            return None
        pressed_name = ops_of(kind, text).get(pressed) or menu.get(pressed)
        if pressed_name is None or op_word(pressed_name) in TRAVEL_OPS:
            return None
        if op_word(pressed_name) in self.clause_verbs(step):
            # "Climb up the walls and search the marked floor" (Darkness of
            # Hallowvale's kickBoard): the leading "Climb" is the walk there,
            # the later clause names the press. A Search the guide asks for
            # is the step's own op, not a gating op (seam vm-b1-seam4).
            return None
        row_name = self.test.row_name_at(line)
        row = next((r for r in self.pass_rows if r["step"] == row_name), None) if row_name else None
        if row is not None and detail_moved(row["detail"]):
            return None
        return "line %d presses op%d %r on %s, a gating op: a %r step needs the travel op (%s) " \
            "or a row that moves the player across it" % (
                line, pressed, pressed_name, text, verb,
                "/".join(sorted(offered & set(TRAVEL_OPS))))

    def line_refused(self, step, line, kind=None, symbol=None, text=None):
        """Why action line `line` cannot drive `step` (claimed by another
        step's row, or the wrong op on the step's target), or None."""
        if self.never_ran(line):
            return "line %d sits below the block at line %d: the run never reached it" % (line, self.block_line())
        other = self.claimed_by_other(self.test.row_name_at(line), step)
        if other:
            return "line %d writes row %r, guide step %s's own row" % (
                line, self.test.row_name_at(line), other)
        if kind and text:
            return self.op_conflict(step, line, kind, symbol, text)
        return None

    def row_line(self, row_step):
        """The source line that writes the ledger row `row_step`, or None."""
        for first, _last, name in self.test.row_spans:
            if name == row_step:
                return first
        return None

    # -- a blocked run (seam matthew-mbp-m4-b67-seam1 (b)). A run that
    # stopped at t.blocked never ran the lines below that call: they drive
    # nothing and cheat nothing. Witch's House's enterGate and
    # useCheeseOnHole (rows at ball.lua:426/:468, under the block at :403)
    # were charged CHEAT by goto proximity, and Rum Deal's thirty island
    # steps read DRIVEN off action lines below its block at :217. False is
    # the reading before it.
    BELOW_BLOCK = True

    def _blocked_calls(self):
        """[(line, reason)]: every `t.blocked(...)` call in the test, its
        reason the call's string literals joined (a reason built from a
        variable alone is "")."""
        code = self.test.code
        out = []
        for match in re.finditer(r"\bt\.blocked\s*\(", code):
            line = code.count("\n", 0, match.start()) + 1
            if code[code.rfind("\n", 0, match.start()) + 1:match.start()].count('"') % 2:
                continue  # inside a string
            body = code[match.end():matching_close(code, match.end() - 1)]
            literals = re.findall(r'"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.)*)\'', body)
            out.append((line, "".join(a or b for a, b in literals)))
        return out

    def block_line(self):
        """The source line of the t.blocked call the run stopped at, or None:
        the ledger's BLOCKED row's detail starts with that call's literal
        reason (the only call, when none can be told apart by its reason).
        None too when the row written just before the block comes from a
        line BELOW it (the call sits in a helper or a loop: what ran after it
        in the file cannot be told)."""
        if getattr(self, "_block_line", False) is not False:
            return self._block_line
        self._block_line = None
        if not self.BELOW_BLOCK:
            return None
        blocked = [i for i, row in enumerate(self.rows) if row["verdict"] == "BLOCKED"]
        if not blocked:
            return None
        position = blocked[-1]
        detail = re.sub(r"\s+", " ", self.rows[position].get("detail") or "").strip()
        calls = self._blocked_calls()
        matched = sorted({line for line, reason in calls
                          if reason and detail.startswith(re.sub(r"\s+", " ", reason).strip()[:60])})
        if not matched and len(calls) == 1:
            matched = [calls[0][0]]
        if len(matched) != 1:
            return None
        line = matched[0]
        for row in reversed(self.rows[:position]):
            before = self.row_line(row["step"])
            if before is None:
                continue
            if before > line:
                return None
            break
        self._block_line = line
        return line

    def never_ran(self, line):
        """Is source line `line` below the t.blocked call the run stopped at
        (block_line), and not part of a row the ledger has (a loop body that
        ran before the block)?"""
        block = self.block_line()
        if block is None or line is None or line <= block:
            return False
        name = self.test.row_name_at(line)
        if name is not None and any(row["step"] == name or re.match(re.escape(name) + r"-\d+$", row["step"])
                                    for row in self.rows):
            return False
        return True

    def step_test_lines(self, step):
        """Every source line that is a step's own: the rows named after it
        (or a guide alias of it) and the action lines naming one of its
        targets."""
        names = [step.name] + [alias for alias, target in self.guide.step_alias.items()
                               if self.guide.resolve(alias) == step.name]
        lines = set()
        for first, _last, name in self.test.row_spans:
            if any(self.row_names_step(name, own) for own in names):
                lines.add(first)
        for line, named in self.test.action_lines:
            if any(same_thing(kind, symbol, text) for text in named for kind, symbol in step.targets):
                lines.add(line)
        return lines

    def below_block(self, step):
        """The UNMATCHED reason of a step whose own test lines (step_test_lines)
        all sit below the block the run stopped at, or None."""
        block = self.block_line()
        if block is None:
            return None
        lines = self.step_test_lines(step)
        if not lines or not all(self.never_ran(line) for line in lines):
            return None
        shown = sorted(lines)
        return "below the block at line %d: the run stopped there, and the step's own line%s %s never ran" % (
            block, "" if len(shown) == 1 else "s", ", ".join(str(n) for n in shown[:6]) +
            (" ..." if len(shown) > 6 else ""))

    @staticmethod
    def _tokens(text):
        """Lowercase words AND numbers of `text`, stopwords dropped, stemmed
        (words() drops "207", which is what tells Serum 207 from 208)."""
        out = set()
        for token in re.findall(r"[a-z0-9]+", (text or "").lower()):
            if token in STOPWORDS or (len(token) < 3 and not token.isdigit()):
                continue
            out.add(token if token.isdigit() else stem(token))
        return out

    def use_clauses(self, step):
        """[(item words, onto words)] of every "use <X> on|with|in <Y>" clause
        in the step's text, a pronoun X ("then use it on the pipe") standing
        for the clause before's Y."""
        out = []
        for match in USE_CLAUSE.finditer(step.text or ""):
            item, onto = match.group(1).strip(), match.group(2).strip()
            if re.fullmatch(r"(?:it|them|this|these|that|those)", item, re.I) and out:
                out.append((out[-1][1], onto))
                continue
            out.append((item, onto))
        return out

    def use_item_wanted(self, step):
        """For a step that USES an item on its npc/loc target (the guide's
        "Use <X> on|with|in <target>"): (the guide symbols X may be, a label);
        None for any other step, or when the guide does not say which item.

        A use step: an npc/loc target, and the text leads with "Use", or the
        step is named use<X>On<Y>. Not one whose NAME leads with another verb
        that a later clause of its text does ("Use the filled druid pouch on
        a ghast to make it attackable and kill it" is `killGhasts`: the use
        readies the step, the kill is it).

        The clause is the one whose <Y> names the target (by display name):
        "Use a tinderbox on the strange object then use it on the nearby
        pipe" is the strange object, on the pipe. A step whose clauses all
        aim elsewhere ("Use the jail key on the south door and talk to
        Velrak", an npc step) is no use step unless it is named
        use<X>On<Y>, when every clause counts. X is the
        requirement or `addIcon(ItemID.X)` item the clause's words name best
        -- by the requirement's name or the display name of any id it accepts
        -- ties kept ("Use the serum 208 on Razmire": Serum 208, not a Serum
        207 listed beside it); else the requirement the step highlights in
        the inventory, else its icon. A requirement the step only carries (a
        hammer beside the nails) is not named and not demanded. Each
        requirement brings every id it accepts: the ctor's, `addAlternates`,
        ItemCollections."""
        if not any(kind in ("npc", "loc") for kind, _ in step.targets):
            return None
        clauses = self.use_clauses(step)
        if not clauses:
            return None
        leading = self.step_verb(step) == "use"
        named_use = re.match(r"^use\w*?(?:On|With|In|Onto|Into)(?:[A-Z0-9]|$)", step.name or "") is not None
        if not (leading or named_use):
            return None
        name_verb = re.match(r"([a-z]+)", step.name or "")
        name_verb = STEP_VERB_ALIAS.get(name_verb.group(1), name_verb.group(1)) if name_verb else ""
        if name_verb not in ("use", "") and name_verb in self.clause_verbs(step):
            return None
        target_words = set()
        for kind, symbol in step.targets:
            target_words |= self._tokens(DISPLAY.get((kind, symbol), "")) | self._tokens(symbol.replace("_", " "))
        aimed = [item for item, onto in clauses if self._tokens(onto) & target_words]
        if not aimed and not named_use:
            # "Use the jail key on the south door and talk to Velrak": the use
            # is on something else; the step's target is the talk's
            return None
        wanted_tokens = set()
        for item in aimed or [item for item, _ in clauses]:
            wanted_tokens |= self._tokens(item)
        pool = []  # (label, [symbols], words)
        for var in dict.fromkeys(step.req_vars):
            item = self.guide.items.get(var) or {}
            symbols = list(dict.fromkeys(item.get("alts") or item.get("ids") or []))
            found = self._tokens(item.get("name", ""))
            for symbol in symbols:
                found |= self._tokens(DISPLAY.get(("obj", symbol), ""))
            pool.append((item.get("name") or var, symbols, found, var))
        for symbol in dict.fromkeys(step.icons):
            display = DISPLAY.get(("obj", symbol), "")
            pool.append((display or symbol, [symbol], self._tokens(display), None))
        picked = []
        if wanted_tokens:
            scored = [(len(found & wanted_tokens), entry) for entry in pool
                      for found in [entry[2]] if found & wanted_tokens and entry[1]]
            if scored:
                best = max(score for score, _ in scored)
                picked = [entry for score, entry in scored if score == best]
        if not picked:
            picked = [entry for entry in pool if entry[3] and entry[1] and any(
                raw in self.guide.highlighted or base in self.guide.highlighted
                for base, raw in zip(step.req_vars, step.req_raw) if base == entry[3])]
        if not picked:
            picked = [entry for entry in pool if entry[3] is None and entry[1]]
        symbols = []
        for _, entry_symbols, _, _ in picked:
            symbols.extend(s for s in entry_symbols if s not in symbols)
        if not symbols:
            return None
        labels = {}
        for label, _, _, _ in picked:
            labels.setdefault(label.lower(), label)
        return symbols, " / ".join(labels.values())

    def use_item_matches(self, symbols, used):
        """Is obj string `used` one of the guide `symbols`' family (obj_family:
        alternates as listed, dose/charge variants, noted forms)? A guide id
        rev 239 has no obj of is held to same_thing's loose name rule."""
        if not used or not re.match(r"^[a-z0-9_]+$", used):
            return False
        used_family = obj_family(used)
        for symbol in symbols:
            if symbol == used or symbol in used_family or used in obj_family(symbol):
                return True
            # same display name, at most four objs of it (same_thing):
            # viking_airtight_vase_with_lid_water / _frozen, "Sealed vase"
            if same_thing("obj", symbol, used, loose=("obj", symbol) not in DISPLAY):
                return True
        return False

    @staticmethod
    def lost_items(detail):
        """The objs a row's `[backpack: ... lost x 1->0, y 2->1]` says left the pack."""
        out = []
        for chunk in LOST_RE.findall(detail or ""):
            out.extend(re.findall(r"\b([a-z0-9_]+) \d+->\d+", chunk))
        return out

    # What a use's own row shows when the use did something (_use_effect):
    # an item left or entered the pack, a server chat line (the driver
    # answers `refused` on "Nothing interesting happens.", so an ok
    # `chat_message` is another line), a matched message, a var read, a
    # landing or jump, a page, an interface, a count that rose.
    USE_EFFECT = re.compile(
        r"\blost [a-z0-9_]+ \d+->\d+|\bgained [a-z0-9_]+ \d+->\d+|\bchat_message\b|\bmatched: |"
        r"\bteleport: |\blanded\b|\bemerged\b|\bmodal\b|\binterface\b|\bopen \(group \d+\)|"
        r"\bdialogue \w+ is up\b|\bpage \w+->(?:npc|player|mesbox|objbox|options|count|name|other)\b|"
        r"\bvar(?:p|b|bit)?\d+_\w+|\b\d+ -> \d+ \(>=|\bthe backpack is the evidence\b|\bcontent line '|"
        r"\b\d+ page\(s\): |\b(?:npc|player|mesbox|objbox|options) -> (?:npc|player|mesbox|objbox|options|none)\b|"
        r"\((?:server )?(?:varp|varbit)\)|\bin the (?:back)?pack\b|\babsent\b|world\.tile\(\)|\bp_teleport\b|"
        r"\(want \d{3,4},\d{3,5},[0-3]\b")
    # A read row named for what it reads: `quest.stage.tied_keli`,
    # `varbit.itdigsitebarrel` -- the test naming the use's effect.
    NEXT_PRESS = re.compile(r"\b(talk_to|click_loc|use_on|use_item_on_item|click_obj|attack|inv_op|equip|"
                            r"shop\.buy|drive\.op|press|click_npc|npc_op|loc_op|drop|goto_tile|walk_to|"
                            r"walk_route|climb|pass_door|cross_gate|cross_trap|cast)\b")
    EFFECT_ROW = re.compile(r"^(?:quest\.stage|varbit\.|varp\.|var\.|inv\.)")
    # A use step is credited only from a PASS row its call wrote that shows
    # an effect; False is the b57 reading (a bare call, or any PASS row,
    # drives it), kept for the fixtures.
    USE_NEEDS_EFFECT = True
    # A use_on credits a use step only on the step's own npc or loc; a row
    # named after the step does not stand in for a call that names another
    # npc/loc. False is the reading before seam matthew-mbp-m4-b66-seam1,
    # kept for the fixtures.
    USE_ON_OWN_TARGET = True
    NOT_AN_EFFECT = re.compile(r"Nothing interesting happens\.?", re.I)
    NO_USE_EFFECT = ("no item left the pack, no var, page, interface, server line or landing on it or on a "
                     "row named after it")

    def _answer_row(self, line):
        """The row a bare `local r, d = ...use_on(...)` on `line` writes
        through a later `t.check("name", ...)` / `t.expect("name", ...)`
        (within READBACK_SPAN lines) whose arguments read one of the
        call's answers; None when no row does (a `t.note` is no row)."""
        code = self.test.code_lines
        match = re.match(r"^\s*(?:local\s+)?([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*=", code[line - 1])
        if not match:
            return None
        answers = [name for name in re.split(r"\s*,\s*", match.group(1)) if name != "_"]
        for first, last, name in sorted(self.test.row_spans):
            if not line < first <= line + READBACK_SPAN:
                continue
            text = " ".join(code[first - 1:last])
            if not re.search(r"\bt\.(?:check|expect)\s*\(", text):
                continue
            if any(re.search(r"\b%s\b" % re.escape(answer), text) for answer in answers):
                return name
        return None

    def _use_effect(self, passed, row_name):
        """What a use's PASS rows show it did -- the call's own row, or a
        row named after it (`useX.var`, `useX-held`) within the 8 rows after
        it: "ledger row N 'x' shows '<the words>'", or None (USE_EFFECT).
        A use credited from its source line alone, or from a row that only
        says the press was sent, drove nothing (seam matthew-mbp-m4-b63-
        seam1: Haunted Mine's key on the valve timed out with the key kept
        and read DRIVEN)."""
        rows = list(passed)
        for first in passed:
            position = next((i for i, r in enumerate(self.rows) if r is first), None)
            if position is None:
                continue
            # the reads the test wrote right after the use, up to its next
            # press, walk or goto: what it says the use did
            for later in self.rows[position + 1:position + 9]:
                line = self.row_line(later["step"])
                if re.match(r"(?:goto|walk|travel)", later["step"], re.I) or (
                        line is not None and self.NEXT_PRESS.search(self.test.code_lines[line - 1])):
                    break
                if later.get("verdict") == "PASS":
                    rows.append(later)
        for row in rows:
            if row is not passed[0] and self.EFFECT_ROW.match(row["step"]):
                return "ledger row %s %r reads %r" % (row["index"], row["step"], (row.get("detail") or "")[:60])
            detail = self.NOT_AN_EFFECT.sub("", row.get("detail") or "")
            found = self.USE_EFFECT.search(detail)
            if found:
                return "ledger row %s %r shows %r" % (row["index"], row["step"], found.group(0).strip())
        return None

    def _line_use_effect(self, line):
        """(effect, why not) for the use on source `line` (a use_on, a
        use_item_on_item, a drive.op): its row (the t.exec/t.check span,
        else a check of its answer) PASSed and shows an effect."""
        call = next((c for c in self.test.use_calls if c["line"] == line), None)
        row_name = call["row"] if call else self.test.row_name_at(line)
        prefix = bool(call and call["row_prefix"])
        if not row_name:
            row_name, prefix = self._answer_row(line), False
        if not row_name:
            return None, "line %d is a bare use: no t.exec/t.check row of its own records what it did" % line
        passed = [r for r in self.pass_rows if (r["step"].startswith(row_name) if prefix else r["step"] == row_name)]
        if not passed:
            return None, "line %d writes row %r, which never PASSed" % (line, row_name)
        effect = self._use_effect(passed, row_name)
        if not effect:
            return None, "line %d's row %r PASSed and shows no effect: %s" % (
                line, passed[0]["step"], self.NO_USE_EFFECT)
        return effect, None

    def use_item_driven(self, step, wanted):
        """DRIVEN for a "use X on Y" step only on a use_on of X's family on
        the step's target: the test's item argument names it, or -- an item
        the test passes through a variable it never bound -- the ledger row
        the call writes says X left the pack. A use_on of another item on
        the target is refused, naming both (item_refusals); so is a call
        whose row never PASSed. A row named after the step with no use_on
        in it counts when its backpack diff lost X (the content took it by
        another op)."""
        symbols, label = wanted
        refused = self.refusals.setdefault(step.name, [])
        names = [step.name] + [alias for alias, target in self.guide.step_alias.items()
                               if self.guide.resolve(alias) == step.name]
        targets = ", ".join(symbol for _, symbol in step.targets[:2])
        wants = "%s (%s)" % (label, ", ".join(symbols[:4]) + (", ..." if len(symbols) > 4 else ""))
        seen = []
        candidates = []
        for call in self.test.use_calls:
            line, row_name, row_prefix = call["line"], call["row"], call["row_prefix"]
            if not row_name:
                # `local r, d = t.player.use_on(...)` then `t.check("x", r == "ok", d)`:
                # the check is the row the call wrote
                row_name, row_prefix = self._answer_row(line), False
            named_row = bool(row_name) and any(self.row_names_step(row_name, name) for name in names)
            on_target = any(same_thing(kind, symbol, text) or shown_by(kind, symbol, text)
                            for text in call["target"] for kind, symbol in step.targets)
            # A use credits the step only on the step's own npc or loc: a
            # row named after the step stands in for the target only when
            # the call names no npc/loc this cache has (an unbound variable).
            # The outpost scorpion is not caught by a cage used on the
            # Taverley one (seam matthew-mbp-m4-b66-seam1).
            elsewhere = sorted(text for text in call["target"]
                               if resolves("npc", text) or resolves("loc", text))
            if self.USE_ON_OWN_TARGET and not on_target and elsewhere and step.targets:
                if named_row:
                    seen.append("line %d uses it on %s, not the step's %s" % (line, "/".join(elsewhere), targets))
                continue
            if not (on_target or named_row):
                continue
            if not row_name and self.USE_NEEDS_EFFECT:
                # a bare call: nothing on the ledger says what it did (Haunted
                # Mine's useKeyOnValve, a use_on that timed out with the key
                # kept, read DRIVEN from its source line alone)
                refused.append("line %d is a bare use_on: no t.exec/t.check row of its own records what it did"
                               % line)
                seen.append("line %d is a bare use_on (no row of its own records an effect)" % line)
                continue
            rows = [r for r in self.rows if row_name and
                    (r["step"].startswith(row_name) if row_prefix else r["step"] == row_name)]
            if row_prefix and any(len(norm(name)) > len(norm(row_name)) and
                                          norm(name).startswith(norm(row_name)) for name in names):
                # `"usePotionOnOgre" .. i` for usePotionOnOgre3: its own row,
                # not the loop's first
                rows = [r for r in rows if norm(r["step"]) in {norm(name) for name in names}]
            passed = [r for r in rows if r["verdict"] == "PASS"]
            lost = [item for r in passed for item in self.lost_items(r["detail"])]
            where = "line %d" % line + (" (ledger row %s %r%s)" % (
                passed[0]["index"], passed[0]["step"], ", lost " + " ".join(lost) if lost else "") if passed else "")
            if row_name and not passed:
                # the call writes a named row and the ledger has no PASS of it:
                # the run never got there (Watchtower stops BLOCKED before
                # its shamans), or the use failed
                refused.append("line %d writes row %r%s, which %s" % (
                    line, row_name, "..." if row_prefix else "",
                    "never PASSed" if rows else "the ledger never reached"))
                seen.append("line %d (row %r%s) never ran" % (line, row_name, "..." if row_prefix else ""))
                continue
            used = call["items"] or lost
            hit = next((item for item in used if self.use_item_matches(symbols, item)), None)
            if hit is None:
                seen.append("%s uses %s" % (where, " / ".join(used) if used else
                                              "%s (unbound, and no backpack diff)" % call["item_arg"]))
                continue
            why = self.line_refused(step, line)
            if why:
                refused.append(why)
                continue
            effect = self._use_effect(passed, row_name) if self.USE_NEEDS_EFFECT else "(effect not read)"
            if not effect:
                refused.append("line %d's row %r PASSed and shows no effect: %s" % (
                    line, passed[0]["step"], self.NO_USE_EFFECT))
                seen.append("%s shows no effect (%s)" % (where, self.NO_USE_EFFECT))
                continue
            where = "%s, %s" % (where, effect)
            candidates.append((not named_row, line in self.line_credits, line, hit, where))
        if candidates:
            _, _, line, hit, where = min(candidates)
            self.line_credits.setdefault(line, step.name)
            return "%s uses %r on the target, the guide's %s" % (where, hit, label)
        use_lines = {call["line"] for call in self.test.use_calls}
        for row in self.action_rows:
            if not any(self.row_names_step(row["step"], name) for name in names):
                continue
            if self.USE_ON_OWN_TARGET and self.row_line(row["step"]) in use_lines:
                continue  # its use_on was judged above, on its own target
            hit = next((item for item in self.lost_items(row["detail"])
                        if self.use_item_matches(symbols, item)), None)
            if hit and not self.claimed_by_other(row["step"], step):
                return "ledger row %s %r lost %r, the guide's %s" % (row["index"], row["step"], hit, label)
        self.item_refusals[step.name] = "the guide's step uses %s on %s; %s" % (
            wants, targets, "; ".join(seen[:3]) if seen else "no use_on of it names the target")
        return None

    def driven(self, step):
        self.refusals.pop(step.name, None)
        self.item_refusals.pop(step.name, None)
        if USE_ITEM_RULE and self.pass_rows:
            wanted = self.use_item_wanted(step)
            if wanted:
                return self.use_item_driven(step, wanted)
        conflicted = []
        # The author may name the row after the panel's puzzle wrapper
        # (`pwMsHynnTerprett` for msHynnDialogQuiz).
        names = [step.name] + [alias for alias, target in self.guide.step_alias.items()
                               if self.guide.resolve(alias) == step.name]
        for row in self.action_rows:
            if any(self.row_names_step(row["step"], name) for name in names):
                if any(row["step"].startswith(bad) and row["step"][len(bad):][:1] in ("-", ".", ":", "/", "_", " ")
                       for bad in conflicted):
                    continue  # `x-dialog` of a press that was the wrong op
                line = self.row_line(row["step"])
                conflict = None
                if line is not None:
                    for kind, symbol in step.targets:
                        named = next((named for number, named in self.test.action_lines if number == line), ())
                        for text in named:
                            if same_thing(kind, symbol, text) or shown_by(kind, symbol, text):
                                conflict = conflict or self.op_conflict(step, line, kind, symbol, text)
                if conflict:
                    conflicted.append(row["step"])
                    self.refusals.setdefault(step.name, []).append(
                        "ledger row %s %r: %s" % (row["index"], row["step"], conflict))
                    continue
                return "ledger row %s %r PASS" % (row["index"], row["step"])
        if not self.pass_rows:
            return None
        use_items = [sym for var in step.req_vars for sym in self.guide.items[var]["ids"]]
        refused = self.refusals.setdefault(step.name, [])
        if re.match(r"^use", step.name) and use_items and step.targets:
            # "Use serum 208 on Razmire" is not done by the line that uses
            # serum 207 on him: when every action on the target names a
            # DIFFERENT guide item, the step is not driven.
            all_items = {sym for item in self.guide.items.values() for sym in item["ids"]}
            on_target = []
            for line, named in self.test.action_lines:
                if not re.search(r"use_on|use_item|drive\.op", self.test.code_lines[line - 1]):
                    continue  # talking to Dondakan is not using the bar on him
                if any(same_thing(k, s, t) for t in named for k, s in step.targets):
                    on_target.append((line, named))
            for line, named in on_target:
                item = next((t for t in named for s in use_items if same_thing("obj", s, t)), None)
                if item:
                    why = self.line_refused(step, line)
                    if why:
                        refused.append(why)
                        continue
                    effect, why = self._line_use_effect(line) if self.USE_NEEDS_EFFECT else ("(effect not read)", None)
                    if not effect:
                        refused.append(why)
                        continue
                    return "an action at line %d uses %r on the target (%s)" % (line, item, effect)
            if all(any(t in all_items for t in named) for _, named in on_target):
                return None  # no use at all, or only uses of other items
        for kind, symbol in step.targets:
            if kind == "loc" and any(w in symbol for w in GATE_WORDS) and \
                    self.loc_uses.get(symbol, 0) >= self.loc_evidence(symbol):
                continue  # every pass through this door already did an earlier step
            # EVERY action line naming the target, not the first: the first
            # may be another step's row (pickpocket) and a later one this
            # step's own press (talk).
            # A line no other step has been credited from first: the talk
            # to Da Vinci in Varrock, not the one in Rimmington that already
            # drove giveVinciEthenea.
            candidates = []
            for line, named in self.test.action_lines:
                for text in sorted(named):
                    if not same_thing(kind, symbol, text):
                        continue
                    why = self.line_refused(step, line, kind, symbol, text)
                    if why:
                        held = None if self.never_ran(line) else self.state_already_set(step, line, kind, symbol, text)
                        if held:
                            return held
                        refused.append(why)
                        continue
                    candidates.append((line in self.line_credits, text == symbol and 0 or 1, line, text))
            if candidates:
                _, _, line, text = min(candidates)
                self.line_credits.setdefault(line, step.name)
                return "an action at line %d names %r (%s)" % (line, text, symbol)
        text_words = set(words(step.text))
        if not step.targets:
            for var in step.req_vars:
                for symbol in self.guide.items[var]["ids"]:
                    for line, named in self.test.action_lines:
                        if (line, symbol) in self.consumed:
                            continue  # one action does one step (findBob / findBobAgain)
                        text = next((t for t in named if same_thing("obj", symbol, t)), None)
                        if text:
                            why = self.line_refused(step, line)
                            if why:
                                refused.append(why)
                                continue
                            self.consumed.add((line, symbol))
                            return "an action at line %d uses %r, the step's item" % (line, text)
            for line, named in self.test.action_lines:
                for text in sorted(named):
                    if re.match(r"^[a-z][a-z0-9_]+$", text):
                        own = set(words(text.replace("_", " ")))
                        distinct = self.weak_filter(own)
                        strong = len(distinct) >= 2 or any(len(w) >= 5 for w in distinct)
                        if own and own <= text_words and strong:
                            why = self.line_refused(step, line)
                            if why:
                                refused.append(why)
                                continue
                            return "an action at line %d names %r, which the step's text names" % (line, text)
        if re.search(r"\b(buy|purchase|pick up|pickup|take|grab)\b", step.text, re.I) or \
                OBTAIN_VERB.match((camel_words(step.name) or [""])[0]):
            # "Buy a Karamjan rum from Zembo": any buy/pickup of the rum is
            # the step done by another route.
            wanted = set(words(step.text))
            for line, named in self.test.action_lines:
                source = self.test.code_lines[line - 1]
                if not re.search(r"shop\.buy|click_obj|take_obj|pickup", source) or self.never_ran(line):
                    continue
                for text in named:
                    if re.match(r"^[a-z][a-z0-9_]+$", text) and set(words(text.replace("_", " "))) & wanted \
                            and ("obj", text) in DISPLAY:
                        return "an action at line %d obtains %r, the step's item, by another route" % (line, text)
            for row in self.action_rows:
                parts = re.split(r"[^a-z0-9]+", re.sub(r"([a-z])([A-Z])", r"\1 \2", row["step"]).lower())
                parts = [p for p in parts if p]
                if parts and OBTAIN_VERB.match(parts[0]) and set(words(" ".join(parts[1:]))) & wanted:
                    return "ledger row %s %r obtains it by another route" % (row["index"], row["step"])
                # `slugling.fish3` for "Fish 5 sluglings": the step's own
                # obtain verb and its item, in either order (a step whose
                # npc ids the guide takes from a RuneLite enum, FishingSpot).
                own_verb = OBTAIN_VERB.match((camel_words(step.name) or [""])[0])
                for index, part in enumerate(parts):
                    verb = OBTAIN_VERB.match(part)
                    item_words = words(" ".join(parts[:index] + parts[index + 1:]))
                    if index and verb and own_verb and verb.group(1).lower() == own_verb.group(1).lower() and \
                            any(a.startswith(b) or b.startswith(a) for a in item_words for b in wanted
                                if min(len(a), len(b)) >= 4) and \
                            not self.claimed_by_other(row["step"], step):
                        return "ledger row %s %r does the step's own %r on its item" % (
                            row["index"], row["step"], verb.group(1).lower())
        if step.kind in ("NpcStep", "NpcEmoteStep"):
            nouns = [w.lower() for w in re.findall(r"(?<!^)(?<![.!?] )\b([A-Z][a-z]{3,})", step.text)]
            nouns = [n for n in nouns if n not in STOPWORDS]
            # "Talk to Eluned, then Arianwyn": only the npc the step targets
            display = set()
            for kind, symbol in step.targets:
                display |= set(re.findall(r"[a-z]+", DISPLAY.get((kind, symbol), "")))
            if display:
                nouns = [n for n in nouns if n in display]
            for row in self.action_rows:
                row_words = set(re.split(r"[^a-z0-9]+", re.sub(r"([a-z])([A-Z])", r"\1 \2", row["step"]).lower()))
                row_words |= {re.sub(r"\d+$", "", w) for w in row_words}
                hit = [n for n in nouns if n in row_words]
                if hit:
                    why = self.row_refused(step, row)
                    if why:
                        refused.append(why)
                        continue
                    # A row NAMED after the npc is not a row that reached
                    # him: Death Plateau's `goToHaroldStairs1.castleDoorOut`
                    # (a pass_door) is no talk to Harold (seam
                    # npc_step_credited_by_a_row_that_only_names_the_npc).
                    reached = self.row_reaches_npc(step, row, hit[0])
                    if not reached:
                        refused.append("ledger row %s %r names %s, but its own action does not reach that "
                                       "npc" % (row["index"], row["step"], hit[0]))
                        continue
                    return "ledger row %s %r names %s and %s" % (row["index"], row["step"], hit[0], reached)
        for row in self.action_rows:
            tokens = set(re.findall(r"[a-z][a-z0-9_]+", (row["detail"] + " " + row["step"]).lower()))
            for kind, symbol in step.targets:
                if kind == "obj" and not ROW_ACTION.search(row["detail"] + " " + row["step"]):
                    continue  # an inventory read names items too
                hit = next((t for t in sorted(tokens) if same_thing(kind, symbol, t, loose=False)), None)
                if hit:
                    why = self.row_refused(step, row, kind, symbol, hit) or self.row_off_point(step, row, kind, hit)
                    if why:
                        refused.append(why)
                        continue
                    return "ledger row %s %r names %s" % (row["index"], row["step"], hit)
        return None

    # A row that only names a loc step's loc credits the step only when it
    # worked the copy at the guide's WorldPoint (row_off_point). False is the
    # reading before seam matthew-mbp-m4-b66-seam1, kept for the fixtures.
    ROW_AT_POINT = True
    # How near a row's tile must be to the guide step's WorldPoint for a row
    # that names the step's loc to be a row that worked THAT copy.
    POINT_SLACK = 2
    # How far from where the player stood a copy of the loc may be for a row
    # that reports no tile of its own to have worked it (a click_loc walks
    # to the nearest copy it can see).
    COPY_SEARCH = 15
    # A world tile in a detail: four-digit x (a screen pixel pair `382,252`
    # is not one).
    TILE_RE = re.compile(r"(?<![\d.])(\d{4}),\s*(\d{4,5})(?:,\s*([0-3]))?(?![\d.])")

    def row_off_point(self, step, row, kind, hit):
        """Why a ledger row that only NAMES the step's loc does not credit a
        guide step with a WorldPoint, or None. The row must have acted on a
        copy within POINT_SLACK tiles of it: a tile written right after the
        symbol (`ladder_from_cellar at 2575,9655,0`) when the detail gives
        one, else any tile the detail reports; a detail with no tile at all
        is judged by the copy nearest where the player last stood
        (player_track), and stays unjudged when neither is known. Cog's
        climbWhiteLadder (ladder_from_cellar 2575,9655) is not driven by
        `enterBasement-black`, which climbed ladder_cellar 2566,3242 and only
        says it landed "beside ladder_from_cellar 2566,9642" (seam
        matthew-mbp-m4-b66-seam1)."""
        if not self.ROW_AT_POINT or kind != "loc" or not step.point:
            return None
        px, pz = step.point[0], step.point[1]
        plevel = step.point[2] if len(step.point) > 2 else None
        detail = row.get("detail") or ""

        def near(x, z, level):
            if level is not None and plevel is not None and level != plevel:
                return False
            return max(abs(x - px), abs(z - pz)) <= self.POINT_SLACK

        def tiles(found):
            return [(int(x), int(z), int(level) if level not in (None, "") else None) for x, z, level in found]

        attached = tiles(m.groups() for m in re.finditer(
            r"\b%s\b[\s(:]*(?:at\s+)?(\d{4}),\s*(\d{4,5})(?:,\s*([0-3]))?" % re.escape(hit), detail))
        pool = attached or tiles(m.groups() for m in self.TILE_RE.finditer(detail))
        guide = "the guide's %d,%d,%s" % (px, pz, plevel)
        if pool:
            if any(near(*tile) for tile in pool):
                return None
            shown = ", ".join("%d,%d%s" % (x, z, "" if level is None else ",%d" % level) for x, z, level in pool[:3])
            return "ledger row %s %r names %s, but %s %s, not the copy within %d tiles of %s" % (
                row["index"], row["step"], hit, "names it at" if attached else "its detail reports",
                shown, self.POINT_SLACK, guide)
        stood = self._stood_before(row)
        if stood is None:
            return None
        copy = self._nearest_copy(hit, stood)
        if copy is None or near(*copy):
            return None
        return "ledger row %s %r names %s and reports no tile; the player stood at %d,%d,%d, whose nearest " \
            "copy is %d,%d,%d, not the one within %d tiles of %s" % (
                row["index"], row["step"], hit, stood[0], stood[1], stood[2], copy[0], copy[1], copy[2],
                self.POINT_SLACK, guide)

    def _stood_before(self, row):
        """The player's last tile player_track read at or before `row`, or None."""
        position = next((i for i, r in enumerate(self.rows) if r is row), None)
        if position is None:
            return None
        found = None
        for at, tile, _ in self.player_track():
            if at > position:
                break
            found = tile
        return found

    def _nearest_copy(self, symbol, stood):
        """The origin of the copy of loc `symbol` (or a multiloc state of it)
        nearest `stood` on its level within COPY_SEARCH tiles, or None."""
        walls = map_walls()
        names = family(symbol) | set(loc_states(symbol))
        x, z, level = stood
        walls._ready(x, z)
        best = None
        for dx in range(-self.COPY_SEARCH, self.COPY_SEARCH + 1):
            for dz in range(-self.COPY_SEARCH, self.COPY_SEARCH + 1):
                for name, origin in walls.locs_at.get((x + dx, z + dz, level), ()):
                    if name in names:
                        distance = max(abs(dx), abs(dz))
                        if best is None or distance < best[0]:
                            best = (distance, origin)
        return best[1] if best else None

    def state_already_set(self, step, line, kind, symbol, text):
        """A loc STATE step the guide shows only while the state is missing
        (The Restless Ghost's openCoffinToPutSkullIn, a ConditionalStep leaf:
        "Open the ghost's coffin" when it is shut) is satisfied by an earlier
        press of the same op on the same loc that another step owns -- the
        coffin opened for `openCoffin` is still open. Only open/close/lock/
        unlock, and only a loc: a conversation or a search does not persist."""
        if kind != "loc" or self.step_verb(step) not in STATE_OPS:
            return None
        if not any(step.name in other.children for other in self.guide.steps.values()):
            return None
        if self.test.row_name_at(line) is None or self.op_conflict(step, line, kind, symbol, text):
            return None
        pressed = self.test.pressed_op(line, kind)
        pressed_name = ops_of(kind, text).get(pressed) if isinstance(pressed, int) else None
        if op_word(pressed_name) != self.step_verb(step):
            return None
        return "state already set: line %d (row %r) pressed %r on %s, and the guide's ConditionalStep " \
            "shows this step only while it is not" % (line, self.test.row_name_at(line), pressed_name, text)

    def row_reaches_npc(self, step, row, noun):
        """How the ledger row `row` reached the npc an NpcStep targets, or
        None. The row's detail names one of the step's npc symbols (an
        accepted press reports its target), or the source line that writes
        the row presses an npc -- talk_to/click_npc/npc_op, attack, use_on,
        or an emote for an NpcEmoteStep -- and names one of the step's npc
        symbols (with no npc symbol in the guide, an npc string carrying
        `noun`). A pass_door, a walk, a varbit read, a leg checkpoint or a
        dialogue page row only names him."""
        npcs = [symbol for kind, symbol in step.targets if kind == "npc"]
        detail = row.get("detail") or ""
        for token in sorted(set(re.findall(r"[a-z][a-z0-9_]+", detail.lower()))):
            if any(same_thing("npc", symbol, token, loose=False) for symbol in npcs):
                return "its detail names %r" % token
        first = self.row_line(row["step"])
        if first is None:
            return None
        last = next((span_last for span_first, span_last, name in self.test.row_spans
                     if span_first == first and name == row["step"]), first)
        for number, named in self.test.action_lines:
            if not first <= number <= last:
                continue
            pressed = self.test.pressed_op(number, "npc")
            if pressed is None and step.kind == "NpcEmoteStep" and \
                    re.search(r"\bemote\b", self.test.code_lines[number - 1]):
                pressed = "emote"
            if pressed is None:
                continue
            for text in sorted(named):
                if npcs and any(same_thing("npc", symbol, text) or shown_by("npc", symbol, text)
                                for symbol in npcs):
                    return "line %d presses %r (%s)" % (number, text, pressed)
                if not npcs and noun in re.split(r"[^a-z0-9]+", text.lower()):
                    return "line %d presses %r (%s)" % (number, text, pressed)
        return None

    def row_refused(self, step, row, kind=None, symbol=None, text=None):
        """line_refused for a ledger row found by what it mentions: named after
        another guide step, or its source line presses the wrong op."""
        other = self.claimed_by_other(row["step"], step)
        if other:
            return "ledger row %s %r is guide step %s's own row" % (row["index"], row["step"], other)
        line = self.row_line(row["step"])
        if line is not None:
            for number, named in self.test.action_lines:
                if number != line:
                    continue
                for kind2, symbol2 in step.targets:
                    for candidate in named:
                        if same_thing(kind2, symbol2, candidate) or shown_by(kind2, symbol2, candidate):
                            conflict = self.op_conflict(step, line, kind2, symbol2, candidate)
                            if conflict:
                                return "ledger row %s %r: %s" % (row["index"], row["step"], conflict)
        return None

    def loc_evidence(self, symbol):
        """How many times the test demonstrably worked this loc: its action
        lines, or its action rows, whichever is more. A door clicked once
        cannot also be the second entry the guide lists (Mourning's End I
        walks the Mourner HQ door once, then ::goto's past it)."""
        lines = sum(1 for _, named in self.test.action_lines
                    if any(same_thing("loc", symbol, t) for t in named))
        rows = sum(1 for row in self.action_rows
                   if any(same_thing("loc", symbol, t, loose=False)
                          for t in re.findall(r"[a-z][a-z0-9_]+", (row["detail"] + " " + row["step"]).lower())))
        return max(lines, rows)

    def marker(self, step):
        name = step.name.lower()
        symbols = [s for _, s in step.targets]
        for number, marked, reason in self.test.guide_gaps:
            # A marker may name a ConditionalStep (doAllPuzzles): it covers
            # every leaf under it.
            covers = marked in self.guide.steps and step.name in self.guide.leaves(marked)
            if marked.lower() == name or marked.lower() in symbols or covers:
                cite = rs2_citation(reason)
                if cite:
                    return "GUIDE-GAP marker line %d cites %s" % (number, cite)
        # An equivalent marker that did not verify (equivalent() found no
        # evidence) still declares the step like a GUIDE-GAP when its reason
        # cites a real .rs2 line -- the grading it had before the vocabulary.
        for found in self.test.equivalents:
            if found["error"] or not found["step"] or not self._marker_names(found["step"], step):
                continue
            cite = rs2_citation(found["reason"])
            if cite:
                ok, why = self.verify_equivalent(found)
                if not ok:
                    return "GUIDE-GAP marker line %d (a %s marker that does not verify: %s) cites %s" % (
                        found["line"], found["kind"], why, cite)
        for number, text in self.test.blocked_text:
            low = text.lower()
            if re.search(r"\b%s\b" % re.escape(name), low) or any(
                    re.search(r"\b%s\b" % re.escape(s), low) for s in symbols):
                return "t.blocked/content_bug line %d names it" % number
        return None

    def _marker_names(self, marked, step):
        """Does a marker naming `marked` stand for `step` -- the step itself,
        or a ConditionalStep above it?"""
        if marked == step.name:
            return True
        return marked in self.guide.steps and step.name in self.guide.leaves(marked)

    def verify_equivalent(self, found, static=False):
        """(True, evidence) when an equivalent marker's claim checks out, else
        (False, why). `static` (lint_quest, before any run) skips the checks
        that need this run's ledger."""
        if found["error"]:
            return False, found["error"]
        kind, marked = found["kind"], found["step"]
        step = self.guide.steps.get(marked)
        if step is None:
            return False, "%s is not a step of the guide %s" % (marked, os.path.basename(self.guide.path))
        if kind == "BRANCH-IN":
            sibling = found["sibling"]
            if sibling == self.test_id:
                return False, "a test cannot be its own sibling"
            row = queue_row(sibling)
            if row is None:
                return False, "sibling %s has no QUEUE row" % sibling
            if guide_path(row) != guide_path(self.row):
                return False, "sibling %s is graded against another guide (%s)" % (
                    sibling, os.path.basename(guide_path(row) or "") or "none")
            if row.get("status") != "green" and not joint_verify(self.row, row):
                return False, "sibling %s is %s, not green" % (sibling, row.get("status"))
            path = os.path.join(REPO_ROOT, "test", "quests", sibling + ".lua")
            if not os.path.isfile(path) or not git_tracked(path):
                return False, "sibling test/quests/%s.lua is not a committed file" % sibling
            if not self.follow_branch_in:
                return False, "BRANCH-IN not followed while grading a sibling"
            for result in sibling_grade(sibling):
                if result["step"] == marked or result["reason"].startswith(marked + ":"):
                    if result["class"] == "DRIVEN":
                        return True, "sibling %s drives %s (%s)" % (sibling, marked, result["reason"])
                    return False, "sibling %s grades %s %s, not DRIVEN (%s)" % (
                        sibling, marked, result["class"], result["reason"])
            return False, "sibling %s's grading has no step %s" % (sibling, marked)
        if kind == "PARTNER":
            cheat = found["cheat"]
            partners = partner_cheats()
            if cheat not in partners:
                return False, "::%s is not in %s's two-player partner table" % (
                    cheat, os.path.relpath(CHEATS_DOC, REPO_ROOT))
            text = step.text or ""
            if not TWO_PLAYER_TEXT.search(text):
                return False, "the guide's %s text names no other player (%r)" % (marked, text[:80])
            called = [n for n, c in self.test.cheats if re.match(r"::%s\b" % re.escape(cheat), c)]
            if not called:
                return False, "the test never calls ::%s" % cheat
            if not static:
                row = next((r for r in self.pass_rows if "::" + cheat in r["detail"] or cheat in r["step"]), None)
                if row is None:
                    return False, "no PASS ledger row reports ::%s" % cheat
                return True, "::%s (%s:%d) at line %d, ledger row %s %r PASS" % (
                    cheat, os.path.basename(CHEATS_DOC), partners[cheat], called[0], row["index"], row["step"])
            return True, "::%s (%s:%d) at line %d" % (cheat, os.path.basename(CHEATS_DOC), partners[cheat], called[0])
        if kind == "NOT-A-STEP":
            if step.kind == "QuestSyncStep":
                return True, "%s is a QuestSyncStep (guide line %d)" % (marked, step.line)
            if step.kind == "DetailedQuestStep" and not step.targets and step.point is None and \
                    SYNC_TEXT.search(step.text or "") and SYNC_TEXT_OBJECT.search(step.text or ""):
                return True, "%s is a target-less DetailedQuestStep asking for a plugin sync (guide line %d: %r)" % (
                    marked, step.line, (step.text or "")[:70])
            return False, "%s is a %s with targets %s / text %r -- not a plugin-state sync step" % (
                marked, step.kind, step.targets or "none", (step.text or "")[:60])
        if kind == "OBSOLETE":
            cite = wiki_citation(found["reason"])
            if not cite:
                return False, "the reason cites no pinned wiki page (`oldschool.runescape.wiki/w/<Page>?oldid=<n>` " \
                    "or `...#<Section>`)"
            return True, "cites %s" % cite
        if kind == "ANY-OF":
            other = found["other"]
            if other == marked:
                return False, "a step cannot be satisfied by itself"
            cite = rs2_citation(found["reason"]) or wiki_citation(found["reason"])
            if not cite:
                return False, "the reason cites no .rs2 line, map square or pinned wiki page saying any way serves"
            if static:
                if other in self.guide.steps or other in self.test.strings:
                    return True, "cites %s; %s is named in the file" % (cite, other)
                return False, "%s is neither a guide step nor a row name in the file" % other
            if self._driven.get(other):
                return True, "cites %s; guide step %s is DRIVEN (%s)" % (cite, other, self._driven[other])
            pattern = re.compile(r"^%s(?:$|[.-])" % re.escape(other))
            row = next((r for r in self.action_rows if r["step"] == other), None) or \
                next((r for r in self.action_rows if pattern.match(r["step"])), None)
            if row is None:
                return False, "no PASS action row %s (nor a DRIVEN guide step of that name)" % other
            return True, "cites %s; ledger row %s %r PASS" % (cite, row["index"], row["step"])
        return False, "unknown marker kind %s" % kind

    def equivalent(self, step):
        """The verified equivalent marker that stands for this step, as a
        reason, else None."""
        for found in self.test.equivalents:
            if found["error"] or not found["step"] or not self._marker_names(found["step"], step):
                continue
            if found["kind"] == "BRANCH-IN" and not self.follow_branch_in:
                continue
            ok, why = self.verify_equivalent(found)
            if ok:
                return "%s marker line %d: %s" % (found["kind"], found["line"], why)
        return None

    def cheat(self, step, driven_symbols):
        own = self.step_words(step)
        first = (camel_words(step.name) or [""])[0]
        obtains = bool(OBTAIN_VERB.match(step.text)) or bool(OBTAIN_VERB.match(first))
        # seam b67-seam1 (a): words that name no particular thing -- the
        # quest's own words and its symbol prefix -- and the items the step
        # holds to do its work, never charge it
        common = (self.quest_words | self.prefix_words()) if self.CHEAT_BY_OWN_ITEM else set()
        inputs = self.step_input_items(step) if self.CHEAT_BY_OWN_ITEM else set()
        for effect in self.effects:
            if self.never_ran(effect["line"]):
                continue  # below the block: the run never reached it (seam b67-seam1 (b))
            hit = set()
            own_item = False
            # An item cheat stands in only for a step that OBTAINS the item;
            # a step that merely uses it is still the test's to do.
            if effect["items"] and obtains:
                items = [i for i in effect["items"] if i not in inputs]
                own_item = self.CHEAT_BY_OWN_ITEM and any(self.is_own_item(step, i) for i in items)
                item_words = set()
                for item in items:
                    item_words |= self.item_words(item)
                if effect.get("obtain_only"):
                    # a bring-along: charged only to the step that picks
                    # THAT item up (Fishing Contest's garlic), never to one
                    # whose item differs
                    if item_words and item_words <= set(words(step.text)):
                        hit |= item_words
                else:
                    hit |= own & (item_words - common)
            var_words = set()
            for var in effect["vars"]:
                var_words |= set(words(var.replace("_", " ")))
            var_words -= self.quest_words
            var_words -= common
            hit |= own & var_words
            target_items = [s for k, s in step.targets if k == "obj"]
            if set(effect["items"]) & set(target_items) or hit or own_item:
                what = effect["text"]
                if effect.get("where"):
                    what += " (debugproc %s)" % effect["where"]
                return "%s at line %d%s" % (what, effect["line"],
                                              (" -- shares %s" % ",".join(sorted(hit))) if hit else "")
        return None

    def goto_cheat(self, step, driven_symbols):
        # a journey the content implements (Port Sarim's seaman), replaced by a goto
        npcs = [s for k, s in step.targets if k == "npc"]
        if self.is_travel(step) and step.kind == "NpcStep" and self.test.gotos and \
                any(self.relevant_triggers(s) for s in npcs):
            trigger = next(t for s in npcs for t in self.relevant_triggers(s))
            return "the journey (%s, %s:%d) is replaced by goto_tile at line %d" % (
                ",".join(npcs), trigger[0], trigger[1], self.test.gotos[0][0])
        # goto past a gated loc the step names
        lowered = (step.text + " " + " ".join(s for k, s in step.targets if k == "loc")).lower()
        gated = [w for w in GATE_WORDS if w in lowered]
        if gated and step.kind == "ObjectStep" and self.test.gotos:
            # A multiloc the guide names (ham_multi_trapdoor) carries no
            # trigger of its own: its states' [oploc] are its triggers, and a
            # climb on its open state that writes a quest var is no plain
            # travel (seam35).
            locs = [s for k, s in step.targets if k == "loc"]
            locs = locs + [c for s in locs for c in loc_states(s) if c not in locs]
            if any(s in driven_symbols for s in locs):
                return None
            if locs and not any(self.relevant_triggers(s) for s in locs):
                return None  # the content has no such leg: a CONTENT_GAP, not a cheat
            if self.is_travel(step) and not self.writes_quest_var(locs):
                return None
            near = [g for g in self.test.gotos if _near(step.point, g)]
            if self.GOTO_FAR_SIDE and near:
                return self._goto_past(step, near, locs, gated)
            if near:
                # The goto that lands CLOSEST to the step's tile is the one
                # that skipped it, not the first nearby one in file order
                # (Prince Ali's goto-ned, 30 tiles off, used to be cited for
                # the jail door that goto-prince-cell walked past).
                goto = min(near, key=lambda g: (_distance(step.point, g), -g[0]))
                # A climb's far side is the other map frame (a dungeon is the
                # surface's y + 6400) or another level: the goto landing THERE
                # is the one that skipped the trapdoor, not the one that
                # walked up to it (Lost Tribe's goto-enterHamLair, seam35).
                climbs = any("climb" in family_op_words("loc", s) for s in locs)
                if climbs and step.point is not None:
                    across = [g for g in near if abs(g[2] - step.point[1]) > 3200 or
                              (len(step.point) > 2 and g[3] != step.point[2])]
                    if across:
                        goto = min(across, key=lambda g: (_distance(step.point, g), g[0]))
                return "goto_tile %d,%d,%d at line %d lands past the %s the guide names (%s)" % (
                    goto[1], goto[2], goto[3], goto[0], max(gated, key=len), ",".join(locs) or step.text[:40])
        return None

    # -- seam matthew-mbp-m4-b67-seam1 (b): a goto is charged with the gated
    # loc a step names only when it lands on the loc's far side: in the
    # loc's own map frame and level (across it, for a climb), from a
    # departure no walk joins with every door shut. Witch's House's
    # goto-getKey (2928,3455,0 -> 2899,3473,0, the potted plant outside the
    # front door: REACH with the doors closed) was charged with the
    # basement's shockgater (2902,9873,0, another frame, matched only
    # through _near's 6400 shift) and the mouse hole behind two doors
    # (2903,3466,0). False is the reading before it (proximity alone).
    GOTO_FAR_SIDE = True
    # reach.py's margins, as goto_table.py reads a hop
    GOTO_REACH_MARGINS = (30, 100, 250)

    def _goto_runs(self, goto):
        """[(departure (x, z, level) or None, landing, ledger row)] for every
        ledger goto row that landed within a tile of the goto's target."""
        _, x, z, level = goto
        out = []
        track = self.player_track()
        for i, (position, point, is_goto) in enumerate(track):
            if not is_goto or point[2] != level or max(abs(point[0] - x), abs(point[1] - z)) > 1:
                continue
            known = self._known_departure(track, i)
            out.append((known[0] if known else None, point, self.rows[position]))
        return out

    def _hop_reaches(self, start, end):
        """Does a walk join `start` and `end` (one level, one map frame) with
        every door shut, at any of GOTO_REACH_MARGINS (goto_table's REACH
        closed-doors)?"""
        if start[2] != end[2] or abs(start[1] - end[1]) > 3200 or abs(start[0] - end[0]) > 3200:
            return False
        if start[:2] == end[:2]:
            return True
        if max(abs(start[0] - end[0]), abs(start[1] - end[1])) > self.GATE_MAX_TILES:
            return False
        walls = map_walls()
        return any(walls.door_route(start[:2], end[:2], end[2], margin, False)[0]
                   for margin in self.GOTO_REACH_MARGINS)

    def _goto_past(self, step, near, locs, gated):
        """goto_cheat's charge under GOTO_FAR_SIDE: the goto in `near` (the
        file's gotos within NEAR_TILES of the step's WorldPoint) that lands
        on the far side of the step's loc, or None. A goto whose line never
        ran (below the block) is not one; one with no ledger row to read
        its departure from is judged on its landing alone."""
        point = step.point
        climbs = any("climb" in family_op_words("loc", s) for s in locs)
        level = point[2] if len(point) > 2 else None
        if climbs:
            # a climb's far side is the other map frame or level
            sided = [g for g in near if abs(g[2] - point[1]) > 3200 or (level is not None and g[3] != level)]
        else:
            sided = [g for g in near if abs(g[2] - point[1]) <= 3200 and (level is None or g[3] == level)]
        charged = []
        for goto in sided:
            if self.never_ran(goto[0]):
                continue
            runs = self._goto_runs(goto)
            if runs and all(departure is not None and self._hop_reaches(departure, landing)
                            for departure, landing, _ in runs):
                continue  # every time it ran, a walk with the doors shut got there: past no gate
            charged.append((goto, runs))
        if not charged:
            return None
        goto, runs = min(charged, key=lambda pair: (_distance(point, pair[0]), pair[0][0]))
        hop = next(((departure, landing, row) for departure, landing, row in runs
                    if departure is None or not self._hop_reaches(departure, landing)), None)
        if hop is None:
            how = "no ledger row read its departure"
        elif hop[0] is None:
            how = "ledger row %s %r, departure unknown" % (hop[2]["index"], hop[2]["step"])
        else:
            how = "ledger row %s %r from %d,%d,%d: no walk with every door shut" % (
                (hop[2]["index"], hop[2]["step"]) + tuple(hop[0]))
        return "goto_tile %d,%d,%d at line %d lands past the %s the guide names (%s) -- %s" % (
            goto[1], goto[2], goto[3], goto[0], max(gated, key=len), ",".join(locs) or step.text[:40], how)

    def readback(self, line):
        """The first code line within READBACK_SPAN lines after `line` that
        reads a cheat's effect back (t.check/t.expect/t.msg.expect/t.var/
        t.inv/t.quest.stage...), as (line, text); None when there is none. A
        named check whose ledger row is not PASS is no readback."""
        for number in range(line, min(line + READBACK_SPAN, len(self.test.code_lines)) + 1):
            source = self.test.code_lines[number - 1]
            if number == line:
                # the cheat's own line reads nothing back after the call
                source = source[source.find("::"):]
                source = source[source.find(")") + 1:] if ")" in source else ""
            match = READBACK.search(source)
            if not match:
                continue
            named = re.search(r"t\.check\s*\(\s*\"([^\"]+)\"", source)
            if named:
                rows = [r for r in self.rows if r["step"].startswith(named.group(1))]
                if rows and not all(r["verdict"] == "PASS" for r in rows):
                    continue
                return number, "t.check %r" % named.group(1)
            return number, match.group(0).rstrip("( ")
        return None

    def named_cheat(self, step):
        """A quest debugproc named after the step (::mortton_repairtemple for
        repairTemple) did it, whatever else the test touched. One that
        docs/QUEST_SERVER_CHEATS.md sanctions as a GRIND fast-forward, whose
        effect the test reads back right after it, is the step DRIVEN: the
        debugproc walks the quest's own advance body and skips only the
        waiting. Returns (class, reason) or None."""
        name = norm(step.name)
        if len(name) < 6:
            return None
        _, _, debugprocs = content_index()
        sanctioned = sanctioned_grind_cheats()
        unread = None
        for line, text in self.test.cheats:
            if self.never_ran(line):
                continue  # below the block the run stopped at (seam b67-seam1 (b))
            parts = text[2:].split()
            if parts and parts[0] in debugprocs and name in norm(parts[0]):
                rel, where, _ = debugprocs[parts[0]]
                if parts[0] in sanctioned:
                    read = self.readback(line)
                    if read:
                        return "DRIVEN", ("%s at line %d is a sanctioned grind fast-forward "
                                          "(docs/QUEST_SERVER_CHEATS.md:%d, debugproc %s:%d), read back "
                                          "at line %d (%s)" % (text, line, sanctioned[parts[0]], rel, where,
                                                              read[0], read[1]))
                    unread = unread or ("%s at line %d (debugproc %s:%d) is sanctioned "
                                        "(docs/QUEST_SERVER_CHEATS.md:%d) but nothing within %d lines "
                                        "reads its effect back" % (text, line, rel, where,
                                                                   sanctioned[parts[0]], READBACK_SPAN))
                    continue
                return "CHEAT", "%s at line %d (debugproc %s:%d) is named after the step" % (text, line, rel, where)
        return ("CHEAT", unread) if unread else None

    def stand_on(self, step):
        """The ledger row (from stand_ons) whose PASS came from a square the
        driver's reach retry ::goto'd onto, when that row did this step: its
        loc is one of the step's targets, or its row is named after the
        step."""
        name = norm(step.name)
        locs = [s for k, s in step.targets if k == "loc"]
        for found in self.stand_ons:
            row_name = norm(found["step"])
            segments = [norm(part) for part in re.split(r"[.\-:/ ]", found["step"])]
            if any(same_thing("loc", s, found["symbol"]) for s in locs) or \
                    (len(name) >= 6 and (row_name.startswith(name) or any(seg == name for seg in segments))):
                return found
        return None

    def stand_on_optin_for(self, stood):
        """The (line, marker) of the `stand_on_square = true` call that
        produced this stand-on row: the opt-in whose line, or one of the
        three above it (a multi-line t.exec), names the row's step (its -N
        repeat suffix dropped) or the loc symbol; else the only opt-in the
        file has; else None."""
        if not self.test.stand_on_optins:
            return None
        name = re.sub(r"-\d+$", "", stood["step"])
        # The call named after the row first, across every opt-in: a loc
        # crossed three times has three opt-ins, and the symbol test alone
        # would hand every repeat the first crossing's call.
        for number, marker in self.test.stand_on_optins:
            window = "\n".join(self.test.raw_lines[max(0, number - 4):number])
            if ('"%s"' % name) in window:
                return number, marker
        for number, marker in self.test.stand_on_optins:
            window = "\n".join(self.test.raw_lines[max(0, number - 4):number])
            if stood["symbol"] and stood["symbol"] in window:
                return number, marker
        if len(self.test.stand_on_optins) == 1:
            return self.test.stand_on_optins[0]
        return None

    def claim_marked_crossings(self):
        """A loc the quest's own stage gates make the player cross more than
        once (Mountain Daughter's pole-vault and plank rocks to the lake
        island, three trips) leaves a stand-on row per crossing, and
        stand_on() hands a graded step only the first. Every other crossing
        is claimed here, by its OWN call's evidence and nothing looser: the
        row's `stand_on_square = true` call (stand_on_optin_for, named after
        the row) carries a `-- GUIDE-GAP:` marker whose citation resolves,
        and that marker names a step the guide DEFINES -- a panel step or a
        ConditionalStep-only one such as plankRocksReturn -- one of whose loc
        targets is the loc the row stood on. A repeat under a bare opt-in, a
        marker naming no guide step or another loc, or no opt-in at all stays
        an unclaimed stand-on (a gate finding). Returns
        [{row, step, symbol, tile, marker_line, marker_step, cite, optin_line}]."""
        claimed = []
        for found in self.stand_ons:
            if found["row"] in self.claimed_stand_ons:
                continue
            optin = self.stand_on_optin_for(found)
            if optin is None or optin[1] is None:
                continue
            number, (marker_line, marker_step, _, cite) = optin
            guide_step = self.guide.steps.get(marker_step)
            if guide_step is None:
                continue
            if not any(kind == "loc" and same_thing("loc", symbol, found["symbol"])
                       for kind, symbol in guide_step.targets):
                continue
            self.claimed_stand_ons.add(found["row"])
            claimed.append(dict(found, marker_line=marker_line, marker_step=marker_step,
                                cite=cite, optin_line=number))
        return claimed

    def is_travel(self, step):
        lowered = (step.text + " " + " ".join(s for _, s in step.targets)).lower()
        if step.kind == "ObjectStep" and any(w in lowered for w in TRAVEL_WORDS):
            return True
        # "Travel to Miscellania": the audit merges a journey into the step
        # it leads to; a gated one is caught by the goto rule first.
        return bool(re.match(r"^(travel|goTo|sail|headTo|runTo)", step.name)) or \
            bool(re.match(r"^\s*(travel|go to|head to|run to)\b", step.text, re.I))

    def writes_quest_var(self, locs):
        """A loc whose own trigger writes one of the quest's vars -- a climb
        the quest counts, not plain travel (Elemental Workshop's spiral
        stairs set %elemental_workshop_stairs)."""
        triggers, _, _ = content_index()
        for symbol in locs:
            for name in family(symbol):
                for rel, line, _ in triggers.get(name, []):
                    with open(os.path.join(CONTENT_ROOT, rel), "r", encoding="utf-8", errors="replace") as handle:
                        lines = handle.read().split("\n")
                    for body in lines[line:]:
                        if body.startswith("["):
                            break
                        written = re.findall(r"%(\w+)\s*=[^=]", body)
                        if any(var in self.quest_vars for var in written):
                            return True
        return False

    def reads_quest_var(self, locs):
        """A loc whose own trigger READS one of the quest's vars -- a climb
        the quest gates, not plain travel: Mourning's End I's
        mourning_hideout_trap_door refuses below ^mend1_gathering
        (mend1_disguise.rs2:346-353). A generic maplink stair or ladder has
        no trigger of its own naming a quest var."""
        triggers, _, _ = content_index()
        for symbol in locs:
            for name in family(symbol):
                for rel, line, _ in triggers.get(name, []):
                    with open(os.path.join(CONTENT_ROOT, rel), "r", encoding="utf-8", errors="replace") as handle:
                        lines = handle.read().split("\n")
                    for body in lines[line:]:
                        if body.startswith("["):
                            break
                        if any(var in self.quest_vars for var in re.findall(r"%(\w+)", body)):
                            return True
        return False

    def relevant_triggers(self, symbol):
        """Triggers on `symbol` that can serve this quest: generic ones, this
        quest's own, or another quest's that reads one of this quest's vars
        (Bob the Cat's only [opnpc1] is Dragon Slayer II's)."""
        out = []
        own_dir = os.path.join("quests", self.quest_dir) + os.sep
        for trigger in symbol_triggers(symbol):
            rel = trigger[0]
            if not rel.startswith("quests" + os.sep) or rel.startswith(own_dir):
                out.append(trigger)
                continue
            if trigger[2].startswith(("oploc", "aploc")):
                # Another quest's loc serves this one when that quest is a
                # prerequisite (Waterfall's tomb for Roving Elves, Troll
                # Stronghold's cell door for Eadgar's Ruse) -- not when it is
                # unrelated (Another Slice of Ham's rope hole for Tears of
                # Guthix).
                owner = norm(rel.split(os.sep)[1].replace("quest_", ""))
                if any(owner in pre or pre in owner for pre in self.guide.prerequisites):
                    out.append(trigger)
                elif trigger_is_unconditional(rel, trigger[1]):
                    # ...or when its body reads no player variable at all: a
                    # plain door or exit that happens to live in that quest's
                    # file works for everyone (the Colosseum lobby exit in
                    # twilightspromise.rs2 for Meat and Greet, b55: graded a
                    # CONTENT_GAP, which let a goto_tile past it).
                    out.append(trigger)
                continue
            # An npc's talk in another quest's scripts is that quest's
            # dialogue, unless its own body reads this quest's varp.
            with open(os.path.join(CONTENT_ROOT, rel), "r", encoding="utf-8", errors="replace") as handle:
                lines = handle.read().split("\n")
            body = []
            for line in lines[trigger[1]:]:
                if line.startswith("["):
                    break
                body.append(line)
            if self.bound_varp and re.search(r"%%%s\b" % re.escape(self.bound_varp), "\n".join(body)):
                out.append(trigger)
        return out

    def weak_filter(self, hit):
        """Drop words that only restate the quest's own name (mourn/Mourning's
        End) or are too generic to tie a comment to a step."""
        name = norm(self.quest_dir + self.test_id)
        return {w for w in hit if w not in name and w not in ("full", "item", "help", "need", "lat")}

    def content_words(self, step):
        own = self.step_words(step)
        if not step.text.strip():
            # a text-less step (Miscellania's get75Support) is its tools
            for var in step.req_vars:
                own |= set(words(self.guide.items[var]["name"]))
        return own

    def content_gap(self, step):
        if re.search(r"\b(partner|another player|other player|second account)\b", step.text, re.I):
            return "a two-player step (%s); a single-player port has no such leg" % step.text[:60]
        npcs = [s for k, s in step.targets if k == "npc"]
        talk = step.kind == "NpcStep" and re.match(r"^\s*(talk|speak|ask|tell|return|go back|bring|show)", step.text, re.I)
        if npcs and talk and self.bound_varp and not self.is_travel(step):
            triggers = [t for s in npcs for t in self.relevant_triggers(s) if t[2].startswith("opnpc")]
            if triggers and not any(reaches_var(t[0], t[1], self.bound_varp) for t in triggers):
                return "[%s,%s] (%s:%d) never reads %%%s -- talking to them cannot advance this quest" % (
                    triggers[0][2], npcs[0], triggers[0][0], triggers[0][1], self.bound_varp)
        # The .rs2 line that writes past the leg is the best evidence: first.
        own = self.content_words(step)
        use_step = re.match(r"^(use|give|show)", step.name)
        step_items = [sym for var in step.req_vars for sym in self.guide.items[var]["ids"]]
        for rel, number, text, block, has_mes in self.narration:
            if not has_mes:
                # A dialogue-only branch narrates only a "use X on Y" step
                # it writes past without taking X (Between a Rock: "Show him
                # the gold bar" sets stage 60, the bar never leaves the pack).
                if not use_step or any(re.search(r"inv_del\([^)]*\b%s\b" % re.escape(i), block) for i in step_items):
                    continue
                # ...and only in the target's own script (dondakan.rs2).
                where = (os.path.basename(rel) + " " + block.split("\n")[0]).lower()
                if not any(w in where for _, sym in step.targets for w in sym.split("_") if len(w) >= 5):
                    continue
            hit = self.weak_filter((own & set(words(text))) - self.quest_words)
            if len(hit) >= 2:
                return "narrated at %s:%d (a %s branch writes the stage: \"%s\"; shares %s)" % (
                    rel, number, "mes()" if has_mes else "dialogue-only",
                    re.sub(r"^\s*(mes|~\w+)\((\^\w+,\s*)?\"|\"\);.*$", "", text.strip())[:70], ",".join(sorted(hit)))
        for rel, number, text in self.softs:
            hit = self.weak_filter((own & set(words(text))) - self.quest_words)
            if len(hit) >= 2:
                return "soft-skipped at %s:%d (shares %s)" % (rel, number, ",".join(sorted(hit)))
        npc_loc = [s for k, s in step.targets if k in ("npc", "loc")]
        if npc_loc and not any(self.relevant_triggers(s) for s in npc_loc):
            if not self.is_travel(step):
                elsewhere = [t for s in npc_loc for t in symbol_triggers(s)]
                return "no [op*]/[ap*] trigger on %s serves this quest%s" % (
                    ",".join(npc_loc),
                    (" (only %s:%d, another quest's)" % elsewhere[0][:2]) if elsewhere else " anywhere in the pack")
        return None

    def bring_along(self, step):
        if re.match(r"^(use|give|show)", step.name):
            return None  # a step that spends the item is not obtaining it
        bring = self.bring_alongs()
        given = set()
        for _, text in self.test.cheats:
            parts = text[2:].split()
            if len(parts) > 1 and parts[0] in GIVE_CHEATS:
                given.add(parts[1])
        items = [s for k, s in step.targets if k == "obj"]
        if items and set(items) <= bring:
            return "obtains %s, which the guide lists as a bring-along" % ",".join(items)
        own = set(words(step.text)) | set(camel_words(step.name))
        for symbol in sorted(bring & given):
            item = self.item_words(symbol)
            # A step that makes a PRECURSOR of the given item (shearing wool
            # for balls of wool) -- never one that uses the item itself.
            req_words = set()
            for var in step.req_vars:
                if symbol not in self.guide.items[var]["ids"]:
                    req_words |= set(words(self.guide.items[var]["name"]))
            # An ItemStep that fetches the empty precursor (Clock Tower's
            # getBucket, "fill it up on the well") when the test gives the
            # filled item the guide's own condition skips it for.
            precursor = (req_words & item) if not step.targets or step.kind in ("NpcStep", "ItemStep") else set()
            hit = (item if item and item <= own else set()) | precursor
            if hit:
                return "works toward bring-along %s, which the test gives (shares %s)" % (
                    symbol, ",".join(sorted(hit)))
        return None

    def collapsed_by_name(self, step):
        """The content's own soft-skip comment names this guide step
        (mend1_sheep.rs2: "quest-helper's own `getToads` step ... is
        collapsed") -- a content gap unless the test drove the step with a real
        row or declared it with a GUIDE-GAP marker (classify checks those first)."""
        if len(step.name) < 6:
            return None
        for rel, number, text in self.softs:
            paragraph = re.sub(r"\s*//\s*", " ", PARAGRAPHS.get((rel, number), text))
            for sentence in re.split(r"(?<=[.;])\s+(?=[A-Z(`])", paragraph):
                if re.search(r"\b%s\b" % re.escape(step.name), sentence) and \
                        re.search(r"collaps|narrated as|soft-skip|stand-in", sentence, re.I):
                    return "the content's soft-skip comment at %s:%d says %s is collapsed" % (
                        rel, number, step.name)
        return None

    def classify(self, step, driven_reason, driven_symbols):
        strong = self.named_cheat(step)
        if strong:
            return strong
        # The driver's reach retry ::goto'd the player onto the loc's own
        # square and the press landed from there: a route the player never
        # walked (Roving Elves' tree rope pressed from across the river). A
        # CHEAT unless the file's GUIDE-GAP marker for the step says, citing
        # the .rs2 or the map square, why no route ends anywhere else.
        stood = self.stand_on(step)
        if stood:
            self.claimed_stand_ons.add(stood["row"])
            why = "reach retry stood on %s with ::goto (ledger row %s %r, %s)" % (
                stood["tile"], stood["row"], stood["step"], stood["symbol"])
            optin = self.stand_on_optin_for(stood)
            if optin is None:
                return "CHEAT", "%s -- and no `stand_on_square = true` call in the Lua asked for it" % why
            number, marker = optin
            if marker is None:
                return "CHEAT", "%s -- bare stand_on_square opt-in at line %d: no `-- GUIDE-GAP:` marker " \
                    "citing the .rs2 line or map square within %d lines above it" % (
                        why, number, STAND_ON_MARKER_SPAN)
            return "CONTENT_GAP", "GUIDE-GAP marker line %d cites %s -- stand_on_square opt-in at line %d: %s" % (
                marker[0], marker[3], number, why)
        # A real driving row wins over anything the content says about the
        # step; then the file's own declaration (GUIDE-GAP / t.blocked), which
        # gate_findings accepts; only then the content's collapsed-by-name
        # prose, which the file can neither drive past nor declare away if it
        # came first (mourningsendparti's talkToIslwyn, pickUpRottenApple).
        if driven_reason:
            return "DRIVEN", driven_reason
        # A verified BRANCH-IN / PARTNER / NOT-A-STEP / OBSOLETE / ANY-OF
        # marker: not a gap at all.
        equivalent = self.equivalent(step)
        if equivalent:
            return "EQUIVALENT", equivalent
        declared = self.marker(step)
        if declared:
            return "CONTENT_GAP", declared
        collapsed = self.collapsed_by_name(step)
        if collapsed:
            return "CONTENT_GAP", collapsed
        # A step whose own lines all sit below the block the run stopped at
        # was never reached: not a cheat, not a narrated leg -- not done
        # (seam b67-seam1 (b)).
        below = self.below_block(step)
        if below:
            return "UNMATCHED", below
        for klass, check in (("CHEAT", lambda: self.cheat(step, driven_symbols)),
                             ("BRING_ALONG", lambda: self.bring_along(step)),
                             ("CONTENT_GAP", lambda: self.content_gap(step)),
                             # Weakest test-side evidence last: a goto past a
                             # gated loc or a journey the content implements. A
                             # leg the port collapsed (above) is the content's
                             # gap, not a cheat.
                             ("CHEAT", lambda: self.goto_cheat(step, driven_symbols))):
            reason = check()
            if reason:
                if klass == "CONTENT_GAP" and re.match(r"(narrated|soft-skipped) at", reason) and \
                        step.kind == "ObjectStep" and step.targets and \
                        all(k == "loc" and any(w in sym for w in TRAVEL_WORDS) for k, sym in step.targets) and \
                        not self.writes_quest_var([s for k, s in step.targets if k == "loc"]):
                    # A ladder climb whose words happen to meet a narrating
                    # branch (Ghosts Ahoy's goDownToMan vs "The chest is
                    # locked.") is travel, not a narrated leg.
                    return "TRAVEL", "travel, merged into the step it leads to (%s)" % reason
                return klass, reason
        if self.is_travel(step):
            return "TRAVEL", "travel, merged into the step it leads to"
        if self.item_refusals.get(step.name):
            return "UNMATCHED", "no use_on of the step's item drives it: %s" % self.item_refusals[step.name]
        refused = self.refusals.get(step.name)
        if refused:
            return "UNMATCHED", "no row of its own drives it; the lines naming its target do not count: %s" % (
                "; ".join(sorted(set(refused))[:3]))
        return "UNMATCHED", "no row, cheat or content evidence found"

    def grade(self):
        steps = self.guide.ladder()
        # every leaf a step stands for: itself, or a panel ConditionalStep's leaves
        # A panel ConditionalStep stands for its leaves, each its own guide
        # step -- except leaves that differ from a sibling in ONE name word
        # (compareAnna/compareDavid: whichever suspect it is), which are one
        # step done any way, and *Fallback leaves, which are optional.
        expanded = []
        # A leaf is graded once, where it first appears: a composite panel
        # entry (Recruitment Drive's pwSirSpishyusStep) and the same leaves
        # listed after it in the panel are one set of steps.
        graded_leaves = set()
        for step in steps:
            if step.name in graded_leaves:
                continue
            if not step.is_composite():
                graded_leaves.add(step.name)
                expanded.append([step])
                continue
            leaves = [self.guide.steps[l] for l in self.guide.leaves(step.name)
                      if l not in graded_leaves] or ([step] if step.name not in graded_leaves else [])
            if not leaves:
                continue
            graded_leaves.update(leaf.name for leaf in leaves)
            for leaf in leaves:
                leaf.stage = leaf.stage if leaf.stage is not None else step.stage
                leaf.panel = step.panel
                leaf.alt_group = step.alt_group
            groups = []
            for leaf in leaves:
                words_of = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", leaf.name).lower().split()
                home = None
                for group in groups:
                    other = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", group[0].name).lower().split()
                    if len(other) == len(words_of) and sum(a != b for a, b in zip(other, words_of)) == 1:
                        home = group
                        break
                if home is not None:
                    home.append(leaf)
                else:
                    groups.append([leaf])
            expanded.extend(groups)
        steps = [group[0] for group in expanded]
        members = {group[0].name: group for group in expanded}
        self._ladder_leaves = [leaf for group in expanded for leaf in group]  # enclosure_entries charges these
        driven = {}
        driven_symbols = set()
        self.loc_uses = {}
        self.line_credits = {}
        # A branch-only step (Guide.ladder, seam36) takes credit after every
        # panel step: the click on a door a panel step names is that step's,
        # and the state QH shows beside it must not take it first.
        branch_only = getattr(self.guide, "branch_only", {})
        for step in sorted(steps, key=lambda s: s.name in branch_only):
            for leaf in members[step.name]:
                reason = self.driven(leaf)
                driven[leaf.name] = reason
                if reason:
                    # a target folded in from a promoted sub-step is that
                    # sub-step's to drive (it is graded on its own): the
                    # parent's click does not make it driven
                    own_targets = [t for t in leaf.targets if t not in leaf.promoted_targets]
                    driven_symbols.update(s for _, s in own_targets)
                    for kind, symbol in own_targets:
                        if kind == "loc":
                            self.loc_uses[symbol] = self.loc_uses.get(symbol, 0) + 1
        self._driven = driven
        results = []
        for step in steps:
            graded = [(leaf,) + self.classify(leaf, driven[leaf.name], driven_symbols)
                      for leaf in members[step.name]]
            if step.name.endswith("Fallback") and graded[0][1] != "DRIVEN":
                graded = [(step, "ALTERNATIVE", "a fallback the guide offers when the main way fails")]
            if step.name in getattr(self.guide, "promoted", {}):
                graded = [self._grade_promoted(g, driven_symbols) for g in graded]
            elif branch_only.get(step.name):
                graded = [self._grade_branch(g) for g in graded]
            if len(graded) == 1:
                klass, reason = graded[0][1], graded[0][2]
            else:
                # One guide step, several ways to do it: any leaf done is done.
                order = ("DRIVEN", "EQUIVALENT", "BRING_ALONG", "CHEAT", "CONTENT_GAP", "UNMATCHED", "TRAVEL")
                best = min(graded, key=lambda g: order.index(g[1]))
                klass, reason = best[1], "%s: %s" % (best[0].name, best[2])
            # An obstacle teleported across, or a crossing row that passed on
            # an earlier try's line, is CHEAT whatever else drove the step:
            # one press of rockslide 1 does not walk rockslides 4 and 5, and
            # a pass through the same route in a later leg is a walk of its
            # own (route_crossing, stale_success). A declared gap or a
            # verified equivalent marker stays what it is.
            if klass not in ("CONTENT_GAP", "EQUIVALENT"):
                found = [(leaf.name, why) for leaf in members[step.name]
                         for why in (self.route_crossing(leaf), self.stale_success(leaf)) if why]
                if found:
                    klass = "CHEAT"
                    reason = found[0][1] if len(members[step.name]) == 1 and len(found) == 1 else \
                        "; ".join("%s: %s" % pair for pair in found)
            results.append({
                "step": step.name, "type": step.kind, "stage": step.stage,
                "text": step.text or (members[step.name][0].text if members[step.name] else ""),
                "targets": ["%s:%s" % t for t in step.targets],
                "point": list(step.point) if step.point else None,
                "class": klass, "reason": reason, "guide_line": step.line,
                "alt_group": step.alt_group,
            })
        # An obstacle the ladder never lists is still an obstacle (sampler
        # matthew-mbp-m4-b47 (a)): a ConditionalStep leaf that no ladder step
        # stands for and that the run teleported across is reported as a
        # step of its own, CHEAT; one not crossed by a goto adds nothing.
        listed = {leaf.name for group in members.values() for leaf in group}
        shown, _ = self.route_states()
        for name in sorted(shown, key=lambda n: (self.guide.steps[n].line or 0) if n in self.guide.steps else 0):
            leaf = self.guide.steps.get(name)
            if leaf is None or name in listed or leaf.is_composite():
                continue
            crossed = self.route_crossing(leaf) or self.stale_success(leaf)
            if not crossed:
                continue
            results.append({
                "step": leaf.name, "type": leaf.kind, "stage": leaf.stage, "text": leaf.text,
                "targets": ["%s:%s" % t for t in leaf.targets],
                "point": list(leaf.point) if leaf.point else None,
                "class": "CHEAT", "reason": "not in the ladder (a ConditionalStep state): %s" % crossed,
                "guide_line": leaf.line, "alt_group": None,
            })
        # A goto past a door or into a sealed pocket that no guide step can
        # be charged with (Grader._charge_name) is a step of its own.
        for name, hops in sorted(self.route_hops().items()):
            if not name.startswith((self.UNCHARGED, self.PLACEMENT)):
                continue
            reason = hops[0][1] if len(hops) == 1 else "%s (and %d more: ledger row%s %s)" % (
                hops[0][1], len(hops) - 1, "" if len(hops) == 2 else "s",
                ", ".join("%s %r" % hop[0] for hop in hops[1:]))
            results.append({
                "step": name, "type": "goto", "stage": None, "text": "", "targets": [], "point": None,
                "class": "CHEAT", "reason": "no guide step to charge: %s" % reason,
                "guide_line": None, "alt_group": None,
            })
        self._stage_narration(results)
        self._alternatives(results)
        return results

    def _grade_promoted(self, graded, driven_symbols):
        """A promoted sub-step (Guide.ladder) is one of the states QH shows
        in its parent's place -- re-enter the base, climb back down, the
        other side of the same gate -- so a test that never reached that
        state has nothing to drive there. What it can NOT do is skip one:
        a promoted sub-step the test teleported past (a goto_tile landing
        beyond its door/gate/barrier/stair, a journey replaced by a goto),
        stood on with ::goto, or whose own item it ::gave, stays CHEAT.
        Anything else it did not drive is ALTERNATIVE (a ladder or stair,
        TRAVEL), with the class it would have had kept in the reason.

        A sub-step graded TRAVEL on its own words (a trapdoor, ladder or
        stair: "travel, merged into the step it leads to") is NOT let
        through on that class: the words say a plain climb may be skipped,
        not a climb the quest gates. It goes to teleported_across, which
        lets a plain maplink climb through and catches one whose trigger
        writes or reads a quest var (seam31: Mourning's End I's
        enterMournerBasementAfterPoison trapdoor was caught only because
        its symbol spells trap_door, which TRAVEL_WORDS misses; spelled
        trapdoor, the same goto past it read TRAVEL)."""
        leaf, klass, reason = graded
        if klass in NEUTRAL_CLASSES and klass != "TRAVEL":
            return graded
        owner = self.guide.promoted.get(leaf.name, "?")
        # Kept as CHEAT: a stand-on with ::goto, an item cheat naming the
        # sub-step's own obj. Re-judged: a word-overlap item/var cheat (it
        # is evidence for a step that OBTAINS the item, not for a sub-step's
        # door or ladder), and goto_cheat's two landing rules -- "a goto
        # lands within reach of the loc" and "the file has any goto at all"
        # (the journey rule) -- which prove a skip for a step every run
        # must pass, not for a state the run may never have been in. For a
        # sub-step the proof is teleported_across: the run WAS on that side.
        weak = " -- shares " in reason or reason.startswith(("goto_tile ", "the journey "))
        if klass == "CHEAT" and not weak:
            return graded
        crossed = self.teleported_across(leaf, owner)
        if crossed:
            return (leaf, "CHEAT", crossed)
        if self.is_travel(leaf):
            return (leaf, "TRAVEL", "sub-step of %s, travel not skipped by a teleport (was %s: %s)" % (
                owner, klass, reason))
        return (leaf, "ALTERNATIVE", "sub-step of %s, a state its parent is shown in place of; not driven, "
                "not skipped by a teleport or cheat (was %s: %s)" % (owner, klass, reason))

    def _grade_branch(self, graded):
        """A branch-only step (Guide.ladder, seam36): a step the guide's
        ConditionalStep tree shows in one state -- on the Ape Atoll dock side
        of the Bamboo Gate, on the first floor of the Grand Tree -- that its
        getPanels() list never names. Like a promoted sub-step: a run that was
        never in that state has nothing to drive there, so one not driven is
        ALTERNATIVE (TRAVEL for a plain climb); but a run that WAS in it and
        left it by goto_tile for a state the same ConditionalStep shows later,
        never pressing the step's loc between, teleported past the step:
        CHEAT (zone_crossing). A strong CHEAT (a debugproc named after the
        step, a stand-on, its own item ::given) stays CHEAT."""
        leaf, klass, reason = graded
        if klass in NEUTRAL_CLASSES and klass != "TRAVEL":
            return graded
        weak = " -- shares " in reason or reason.startswith(("goto_tile ", "the journey "))
        if klass == "CHEAT" and not weak:
            return graded
        crossed = self.zone_crossing(leaf)
        if crossed:
            return (leaf, "CHEAT", crossed)
        where = self.guide.branch_label(leaf.name)
        if self.is_travel(leaf):
            return (leaf, "TRAVEL", "branch-only step (%s), travel not skipped by a teleport (was %s: %s)" % (
                where, klass, reason))
        return (leaf, "ALTERNATIVE", "branch-only step (%s): a state the guide shows outside its panels; not "
                "driven, not skipped by a teleport or cheat (was %s: %s)" % (where, klass, reason))

    # player_track reads a row as a goto by what it DID (a setup placement,
    # or _real_goto: its source line calls goto_tile/::goto, or its detail is
    # goto_tile's own `at x,z,l[ from a,b,c]`), never by its name; a
    # crossing row named goTo* contributes its END tile (TRACK_END_READS).
    # False is the reading before seam matthew-mbp-m4-b65-seam1 (a row named
    # goto*/goTo* read as a goto, its loc tile as the landing), kept for the
    # fixtures.
    GOTO_BY_ACTION = True
    # Where a crossing, climb, cast or walk row says the player ended up
    # (GOTO_BY_ACTION): `landed x,z,l` (climb, cross_gate, a press),
    # `-> at x,z,l` (pass_door's far side, walk_route's end), a cast's `at
    # a,b,c -> x,z,l`, a walk's `reached x,z,l` / `; at x,z,l`, a position
    # check's `player at x,z,l`.
    TRACK_END_READS = (
        re.compile(r"\blanded (?:on level \d at )?(\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"-> at (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"\bat \d{3,4},\d{3,5},[0-3] -> (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"\breached (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"; at (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"^player at (\d{3,4}),(\d{3,5}),([0-3])\b"),
    )

    def player_track(self):
        """[(ledger row position, (x, z, level), is_goto)] -- where the
        player stood, read from the run's own rows: a goto row's landing
        (`at x,z,l`), a relay leg's `tile=`, `checkpoint N written at`,
        `teleport: a -> b`, `never arrived, still at`, `standing at x,z level
        l` / `after the shot at x,z level l`, `from a,b to x,z`, `ok now at
        x,z`, `player x,z` and a bare `x,z` (no level: the last reading's),
        and a detail that opens with `at x,z,l` (a leg's end check).
        `pressed the copy at` is a loc's tile and is never read, nor is a
        chat block (` :: <server lines>`): its `Teleported to` / `checkpoint
        written at` lines may be stale."""
        if getattr(self, "_track", None) is not None:
            return self._track
        goto_rows = {self.test.row_name_at(number) for number, _, _, _ in self.test.gotos} - {None}
        tile = r"(\d{3,4}),(\d{3,5}),([0-3])\b"
        reads = [re.compile(p + tile) for p in (r"checkpoint \d+ written at ", r"\btile=",
                                                r"\bteleport: [\d,]+ -> ", r"still at ", r"^at ")]
        # A position check's own reading, `standing at 2466,9699 level 0`
        # (the tile read back after a crossing): without it the start of the
        # next goto is the tile BEFORE the crossing, and a goto that skips
        # the obstacle after it looks like one that left from its near side
        # (travel_hops_that_skip_a_guide_obstacle).
        reads.append(re.compile(r"\bat (\d{3,4}),(\d{3,5}) level ([0-3])\b"))
        if self.GOTO_BY_ACTION:
            reads.extend(self.TRACK_END_READS)
        # Readings with no level: `from 2209,3201 to 2209,3205` (a crossing
        # check), `attempt 1: ok now at 2480,9712` (a climb).
        flat = [re.compile(r"\bfrom \d{3,4},\d{3,5} to (\d{3,4}),(\d{3,5})\b"),
                re.compile(r"\bnow at (\d{3,4}),(\d{3,5})\b"),
                re.compile(r"^player (\d{3,4}),(\d{3,5})\b"),
                # a bare reading: `3209,9585` (The Lost Tribe's trap.fell,
                # where the floor trap dropped the player after the goto)
                re.compile(r"^(\d{3,4}),(\d{3,5})(?=(?:,[0-3])?$)")]
        track = []
        self._goto_from = {}
        for position, row in enumerate(self.rows):
            detail = row.get("detail") or ""
            # the author's reading, not the server lines quoted after ` :: `
            own = re.sub(r" :: .*?(?= ## | -- |$)", "", detail)
            if self.GOTO_BY_ACTION:
                # by what the row did, never by its name: Fishing Contest's
                # cross_gate row `goToHemenster.gateIn` read as a goto
                # landing on the gate's own tile, so its own press of the
                # gate was not "before" it (fix_b65 fishingcompo run 1)
                is_goto = bool(row.get("placement")) or self._real_goto(position)
            else:
                is_goto = row["step"] in goto_rows or \
                    re.search(r"(?:^|[.\-_])goto", row["step"], re.I) is not None or bool(row.get("placement"))
            if is_goto:
                landing = re.search(r"(?<!copy )\bat " + tile, own)
                if landing and row["verdict"] == "PASS":
                    track.append((position, tuple(int(v) for v in landing.groups()), True))
                    # `at <landing> from <departure>`: the tile goto_tile read
                    # before it fired (hop_start)
                    departed = re.match(r"at " + tile + r" from " + tile, own)
                    if departed:
                        self._goto_from[position] = tuple(int(v) for v in departed.groups()[3:])
                    continue
                if landing:
                    continue
                # a press the static goto map took for a goto row (a
                # helper's retry `goto_tile` inside a crossing): read it as
                # any other row
            best, point = None, None
            for pattern in reads:
                for match in pattern.finditer(own):
                    if best is None or match.start() > best.start():
                        best, point = match, tuple(int(v) for v in match.groups()[-3:])
            level = track[-1][1][2] if track else 0
            for pattern in flat:
                for match in pattern.finditer(own):
                    if best is None or match.start() > best.start():
                        best, point = match, (int(match.group(1)), int(match.group(2)), level)
            if best:
                track.append((position, point, False))
        # The run's first goto with no departure stamp leaves from where the
        # run started (_start_rows): the fixture's tile, or the last setup
        # placement's landing -- when no row before it may have moved the
        # player. Before seam matthew-mbp-m4-b64-seam1 all five goto rules
        # skipped it.
        if self._start_departure is not None and track and track[0][2] and track[0][0] not in self._goto_from:
            position = track[0][0]
            if not self.rows[position].get("placement") and \
                    not any(self._row_moves(row) for row in self.rows[:position]):
                self._goto_from[position] = self._start_departure[0]
                row = self.rows[position]
                self._start_labels[(row["index"], row["step"])] = self._start_departure[1]
        self._track = track
        return track

    def hop_start(self, track, i):
        """(start tile, start row position, rows between, stamped) for the
        goto at track[i]. A goto row whose detail names its departure tile
        (`at <landing> from <departure>`) starts THERE, with nothing between
        to judge; else the start is the reading before it, and the rows
        between are the ones a caller must clear (a press or walk among them
        leaves the start unknown)."""
        position = track[i][0]
        stamped = self._goto_from.get(position)
        if stamped is not None:
            return stamped, position, [], True
        before_position = track[i - 1][0]
        return track[i - 1][1], before_position, self.rows[before_position + 1:position], False

    def zone_crossing(self, leaf):
        """A branch-only ObjectStep on a gated loc (door, gate, barrier...;
        a ladder/stair only when the quest owns the climb) whose
        ConditionalStep shows it while the player is in zone A, where a goto
        row took the player from a tile in A to a tile in a zone a sibling
        listed BEFORE it (a later state) is shown in, with no row between
        pressing the loc: Monkey Madness's leg 8 ::goto from the Ape Atoll
        dock (2802,2707: onApeAtollSouth, where QH shows enterGate) to Garkor
        (2807,2760: onApeAtollNorth) past mm_bamboo_largedoor_left. Read
        from the ledger, so it is the run's own positions, not the Lua's."""
        found = self.guide.branch_of.get(leaf.name)
        locs = [s for k, s in leaf.targets if k == "loc"]
        if not found or found[1] is None or leaf.kind != "ObjectStep" or not locs or not self.rows:
            return None
        gated = [w for w in GATE_WORDS if w in " ".join(locs).lower()] or \
            [w for w in GATE_WORDS if w in leaf.text.lower()]
        if not gated:
            return None
        if self.is_travel(leaf) and not self.writes_quest_var(locs) and not self.reads_quest_var(locs):
            return None
        if not any(symbol_triggers(s) for s in locs):
            return None
        composite, condition, index = found
        here = self.guide.condition_zones(condition)
        if not here:
            return None
        later = {}
        for _, other in self.guide.branch_conds.get(composite, [])[:index]:
            for name, boxes in self.guide.condition_zones(other).items():
                if name not in here:
                    later.setdefault(name, boxes)
        if not later:
            return None

        def zone_of(zones, point):
            x, z, level = point
            return next((name for name, boxes in zones.items()
                         if any(b[0] <= x <= b[1] and b[2] <= z <= b[3] and b[4] <= level <= b[5]
                                for b in boxes)), None)

        names = set()
        for symbol in locs:
            names |= family(symbol)
        pressed = re.compile(r"\b(%s)\b" % "|".join(re.escape(n) for n in sorted(names)))
        track = self.player_track()
        for i in range(1, len(track)):
            position, point, is_goto = track[i]
            before_position, before = track[i - 1][0], track[i - 1][1]
            if not is_goto:
                continue
            start, end = zone_of(here, before), zone_of(later, point)
            if not start or not end or zone_of(here, point):
                continue
            between = self.rows[before_position + 1:position]
            if any(pressed.search(row.get("detail") or "") for row in between):
                continue
            lines = [self.row_line(row["step"]) for row in between]
            if any(line is not None and pressed.search(self.test.code_lines[line - 1]) for line in lines):
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            return ("ledger row %s %r lands at %d,%d,%d (%s) from %d,%d,%d (%s, row %s %r) without pressing "
                    "the %s %s names (%s): the guide shows it in %s.addStep(%s, %s), guide line %d" % (
                        row["index"], row["step"], point[0], point[1], point[2], end,
                        before[0], before[1], before[2], start, from_row["index"], from_row["step"],
                        max(gated, key=len), leaf.name, ",".join(locs), composite,
                        " ".join(condition.split())[:60], leaf.name, leaf.line))
        return None

    # -- route obstacles (travel_hops_that_skip_a_guide_obstacle)

    def route_states(self):
        """(leaf -> [(composite, condition)], composite -> {parent}) over EVERY
        ConditionalStep in the guide, not only the first one a leaf is found
        in (branch_of): Regicide's climbThroughForest is shown by three
        composites, and the pass's rockslides by crossTheBridge and
        theUndergroundPass, each reached from two section composites."""
        if getattr(self, "_route_states", None) is not None:
            return self._route_states
        shown = {}
        parents = {}
        resolve = self.guide.resolve
        for composite, entries in self.guide.branch_conds.items():
            composite = resolve(composite)
            for child, condition in entries:
                child = resolve(child)
                shown.setdefault(child, []).append((composite, condition))
                parents.setdefault(child, set()).add(composite)
            step = self.guide.steps.get(composite)
            if step is not None and step.children:
                # the constructor's default child: shown when no condition holds
                parents.setdefault(resolve(step.children[0]), set()).add(composite)
        self._route_states = (shown, parents)
        return self._route_states

    def route_ancestors(self, composite):
        _, parents = self.route_states()
        out, todo = [], [composite]
        while todo:
            for parent in sorted(parents.get(todo.pop(), ())):
                if parent not in out and parent != composite:
                    out.append(parent)
                    todo.append(parent)
        return out

    def route_obstacle(self, leaf):
        """(locs, word) when `leaf` is an ObjectStep on a loc a walk must get
        over or through (a door, gate, rockslide, spear trap, log, pitfall,
        tripwire...), else None. A plain ladder or stair (no quest var read
        or written by its trigger) is travel, as everywhere else; a loc with
        no trigger in this pack is nothing to cross."""
        if leaf.kind != "ObjectStep":
            return None
        locs = [s for k, s in leaf.targets if k == "loc"]
        if not locs:
            return None
        joined = " ".join(locs).lower()
        word = next((w for w in ROUTE_OBSTACLE_WORDS if w in joined), None)
        if word is None:
            text = ROUTE_OBSTACLE_TEXT.search(leaf.text or "")
            if text is None:
                return None
            word = text.group(1).lower()
        if self.is_travel(leaf) and not self.writes_quest_var(locs) and not self.reads_quest_var(locs):
            return None
        if not any(symbol_triggers(s) for s in locs):
            return None
        return locs, word

    def _pressed_between(self, between, locs, point):
        """Did a row in `between` press one of `locs` -- the copy the guide
        step names, when the row says which copy (`pressed the copy at
        x,z,l` within 4 tiles of the step's own WorldPoint)? A press of the
        same symbol elsewhere (rockslide 1 of five) is not this crossing."""
        names = set()
        for symbol in locs:
            names |= family(symbol)
        pressed = re.compile(r"\b(%s)\b" % "|".join(re.escape(n) for n in sorted(names)))
        quoted = re.compile(r"[\"'](%s)[\"']" % "|".join(re.escape(n) for n in sorted(names)))
        for row in between:
            detail = row.get("detail") or ""
            if pressed.search(detail):
                copies = [tuple(int(v) for v in m) for m in
                          re.findall(r"pressed the copy at (\d+),(\d+),(\d)", detail)]
                if point is None or not copies or any(
                        max(abs(c[0] - point[0]), abs(c[1] - point[1])) <= 4 for c in copies):
                    return True
            line = self.row_line(row["step"])
            if line is not None and quoted.search(self.test.code_lines[line - 1]):
                return True
        return False

    def route_crossing(self, leaf):
        """A goto that leaves the zone where the guide shows an obstacle step
        and lands in another state of the same route, with no row between
        pressing that obstacle: the test teleported across it. Every
        ConditionalStep that shows the leaf is read, and the states it may
        land in are that composite's other zones plus every enclosing
        composite's (the pass section a sub-route is shown inside), so a hop
        from the pit landing (2466,9699: afterThePit, where Regicide shows
        climbOverRockslide4) to the grid lever (2466,9673: afterTheGrid) is
        CHEAT although a row pressed rockslide 1 elsewhere and the ladder
        graded the one-word siblings as one step. Read from the run's own
        ledger positions (player_track), like zone_crossing; see
        route_hops for which hop is charged to which step."""
        hops = self.route_hops().get(leaf.name)
        if not hops:
            return None
        if len(hops) == 1:
            return hops[0][1]
        return "%s (and %d more: ledger row%s %s)" % (
            hops[0][1], len(hops) - 1, "" if len(hops) == 2 else "s", ", ".join("%s %r" % h[0] for h in hops[1:]))

    ROW_MOVES = re.compile(r"click_loc|pressed the copy|walk_to|walk_near|map_flag|chat_message|teleport: |"
                           r"\bemerged\b|\blanded\b|\bmoved\b")
    LINE_MOVES = re.compile(r"t\.player\.(click_loc|press|walk_to|walk_near|use_on|cast)\b|t\.drive\.op\b|"
                            r"t\.sail\.")

    def _row_moves(self, row):
        """Could this row have taken the player across something (a loc
        press, an npc op that may ferry or lead, a walk, a use on a loc, a
        cast)? Its detail says so (`map_flag`, `chat_message`: how a press
        settled; `teleport: a -> b`), or the source line that writes it calls
        one of those verbs. Picking an item up, a talk or a fight walks only
        where a walk could, and a walk does not climb a rockslide."""
        if re.match(r"walk", row["step"], re.I) or self.ROW_MOVES.search(row.get("detail") or ""):
            return True
        line = self.row_line(row["step"])
        return line is not None and self.LINE_MOVES.search(self.test.code_lines[line - 1]) is not None

    def route_hops(self):
        """{leaf name: [((row index, row step), reason)]}: every goto hop that
        left an obstacle step's zone for another state of its route
        (route_crossing), in ledger order.

        Not judged: a hop with a press of the step's own copy between the
        last reading and the goto (the crossing was made), with any other
        press or walk between (where it left from is unknown), or one back
        into the zone the run walked into A from (Regicide leg 6's two-tile
        step back east to passTrap5's stand tile).

        One zone is often the state of several composites heading different
        ways (Regicide's inWestForestPath shows goUpToLeafTowardsLog on the
        way north to Iorwerth and climbThroughForest on the way south to
        Tyras): a hop is charged to the steps whose obstacle it heads toward
        ((P - S) . (E - S) > 0 for the step's WorldPoint P, start S, landing
        E; a step with no WorldPoint counts as ahead), and to every
        candidate only when none of them is ahead of it. Only a hop on one
        level of one map frame is judged here."""
        if getattr(self, "_route_hops", None) is not None:
            return self._route_hops
        self._route_hops = {}
        if not self.rows:
            return self._route_hops
        shown, _ = self.route_states()

        def zone_of(zones, point):
            x, z, level = point
            return next((name for name, boxes in zones.items()
                         if any(b[0] <= x <= b[1] and b[2] <= z <= b[3] and b[4] <= level <= b[5]
                                for b in boxes)), None)

        # (leaf, composite, condition, here zones, other zones, locs, word)
        candidates = []
        for name in sorted(shown):
            leaf = self.guide.steps.get(name)
            if leaf is None or leaf.is_composite():
                continue
            obstacle = self.route_obstacle(leaf)
            if obstacle is None:
                continue
            for composite, condition in shown[name]:
                here = self.guide.condition_zones(condition) if condition else {}
                if not here:
                    continue
                other = {}
                for owner in [composite] + self.route_ancestors(composite):
                    for _, state in self.guide.branch_conds.get(owner, []):
                        for zone, boxes in self.guide.condition_zones(state).items():
                            if zone not in here:
                                other.setdefault(zone, boxes)
                if other:
                    candidates.append((leaf, composite, condition, here, other) + obstacle)
        # no obstacle state of its own: the entry/door/room rules below may
        # still charge a hop (Black Knights' Fortress's only door is a default)
        track = self.player_track() if candidates else []
        for i in range(1, len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            # A hop to another level or map frame (down a ladder, out of a
            # dungeon to the surface) is a climb: goto_cheat and
            # teleported_across judge those against the climb the guide
            # names. Witch's House's goto from the hall to the basement does
            # not cross the shed door shown in the same hall zone.
            if before[2] != point[2] or abs(before[1] - point[1]) > 3200:
                continue
            # Something between moved the player with no reading after it (a
            # press, a walk): where the goto left from is not known.
            if any(self._row_moves(row) for row in between):
                continue
            hits = []
            for leaf, composite, condition, here, other, locs, word in candidates:
                start = zone_of(here, before)
                if not start or zone_of(here, point):
                    continue
                end = zone_of(other, point)
                if not end:
                    continue
                if self._pressed_between(between, locs, leaf.point):
                    continue
                came_from = next((track[j][1] for j in range(i - 2, -1, -1)
                                  if not zone_of(here, track[j][1])), None)
                if came_from is not None and zone_of(other, came_from) == end:
                    continue
                if leaf.point is None:
                    ahead = True
                else:
                    ahead = (leaf.point[0] - before[0]) * (point[0] - before[0]) + \
                        (leaf.point[1] - before[1]) * (point[1] - before[1]) > 0
                hits.append((ahead, leaf, composite, condition, start, end, locs, word))
            if any(hit[0] for hit in hits):
                hits = [hit for hit in hits if hit[0]]
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            charged = set()
            for _, leaf, composite, condition, start, end, locs, word in hits:
                if leaf.name in charged:
                    continue  # one hop, one charge per step (two composites show it)
                charged.add(leaf.name)
                self._route_hops.setdefault(leaf.name, []).append(((row["index"], row["step"]),
                    "ledger row %s %r lands at %d,%d,%d (%s) from %d,%d,%d (%s, row %s %r) without pressing "
                    "the %s %s names (%s): the guide shows it in %s.addStep(%s, %s), guide line %d" % (
                        row["index"], row["step"], point[0], point[1], point[2], end,
                        before[0], before[1], before[2], start, from_row["index"], from_row["step"],
                        word, leaf.name, ",".join(locs), composite, " ".join(condition.split())[:60],
                        leaf.name, leaf.line)))
        for found in (self.route_entries(), self.door_entries(), self.room_exits(), self.frame_entries(),
                      self.enclosure_entries(), self.enclosure_exits(), self.sealed_entries(),
                      self.sealed_exits(), self.gate_crossings(), self.solid_landings()):
            for name, items in found.items():
                have = {key for key, _ in self._route_hops.get(name, [])}
                merged = self._route_hops.setdefault(name, [])
                merged.extend(item for item in items if item[0] not in have)
                merged.sort(key=lambda item: int(item[0][0]) if str(item[0][0]).isdigit() else 0)
        return self._route_hops

    # -- entering a route from where it does not lead (gap (a) of b49-seam2)

    def route_subtree(self, composite):
        """Every step name under `composite` (itself included), through the
        constructor default and every addStep child."""
        resolve = self.guide.resolve
        out, todo = [], [resolve(composite)]
        while todo:
            name = todo.pop()
            if name in out:
                continue
            out.append(name)
            step = self.guide.steps.get(name)
            if step is not None and step.is_composite():
                todo.extend(resolve(child) for child in step.children)
                todo.extend(resolve(child) for child, _ in self.guide.branch_conds.get(name, []))
        return out

    def route_zone_boxes(self, names):
        """[(zone, box)] over every addStep condition of the composites in
        `names`."""
        out = []
        for name in names:
            for _, condition in self.guide.branch_conds.get(name, []):
                for zone, boxes in self.guide.condition_zones(condition).items():
                    out.extend((zone, box) for box in boxes)
        return out

    ROUTE_ZONE_SLACK = 2

    @classmethod
    def _boxes_touch(cls, a, b):
        """Two zone boxes on a shared level that overlap or sit within
        ROUTE_ZONE_SLACK tiles of each other (Quest Helper leaves one-tile
        seams between the zones of a route: Regicide's westOfBridge ends at
        x 2443 and northEastOfBridge starts at 2445)."""
        if a[5] < b[4] or b[5] < a[4]:
            return False
        return max(0, b[0] - a[1], a[0] - b[1]) <= cls.ROUTE_ZONE_SLACK and \
            max(0, b[2] - a[3], a[2] - b[3]) <= cls.ROUTE_ZONE_SLACK

    @staticmethod
    def _in_box(box, point):
        return box[0] <= point[0] <= box[1] and box[2] <= point[1] <= box[3] and box[4] <= point[2] <= box[5]

    def route_gates(self):
        """[(composite X, entry D, gate leaves, state zones, region boxes,
        component of each box)] for every ConditionalStep X whose
        constructor default D is itself a ConditionalStep route with route
        obstacles in zones of its own, and whose zones touch X's states:
        the guide's way into X's states from anywhere else is D (Regicide's
        travelThroughPassSection3 = new ConditionalStep(this,
        crossTheBridge) -- the rockslides and the bridge are how the route
        reaches isInUndergroundSection2).

        A default that is a single step is not read as the way in: Quest
        Helper uses the default for the route's LAST step as often as for
        its first (pathToIorwerth's talkToIorwerth, theUndergroundPass's
        climbDownWell, goToTyrasCampEntrance's enterTyrasCamp)."""
        if getattr(self, "_route_gates", None) is not None:
            return self._route_gates
        self._route_gates = []
        resolve = self.guide.resolve
        for composite in sorted(self.guide.branch_conds):
            step = self.guide.steps.get(resolve(composite))
            if step is None or not step.children:
                continue
            entry = resolve(step.children[0])
            entry_step = self.guide.steps.get(entry)
            if entry_step is None or not entry_step.is_composite():
                continue
            entry_names = self.route_subtree(entry)
            gate_leaves = []
            for name in entry_names:
                for child, condition in self.guide.branch_conds.get(name, []):
                    leaf = self.guide.steps.get(resolve(child))
                    if leaf is None or leaf.is_composite() or leaf in [g for g, _ in gate_leaves]:
                        continue
                    if not self.guide.condition_zones(condition):
                        continue
                    obstacle = self.route_obstacle(leaf)
                    if obstacle is not None:
                        gate_leaves.append((leaf, obstacle))
            if not gate_leaves:
                continue
            states = []
            for child, condition in self.guide.branch_conds.get(composite, []):
                if resolve(child) == entry:
                    continue
                zones = self.guide.condition_zones(condition)
                if zones:
                    states.append((resolve(child), condition, zones))
            if not states:
                continue
            names = [n for n in self.route_subtree(composite) if n not in entry_names] + entry_names
            region = self.route_zone_boxes(names)
            parent = list(range(len(region)))

            def find(i):
                while parent[i] != i:
                    parent[i] = parent[parent[i]]
                    i = parent[i]
                return i

            for i in range(len(region)):
                for j in range(i + 1, len(region)):
                    if self._boxes_touch(region[i][1], region[j][1]):
                        parent[find(i)] = find(j)
            component = [find(i) for i in range(len(region))]
            entry_boxes = self.route_zone_boxes(entry_names)
            entry_components = {component[i] for i, (zone, box) in enumerate(region) if (zone, box) in entry_boxes}
            self._route_gates.append((composite, entry, gate_leaves, states, region, component, entry_components,
                                      entry_boxes))
        return self._route_gates

    def route_entries(self):
        """{leaf name: [((row index, row step), reason)]}: a goto on one
        level of one map frame that lands in a state of a route X from a
        tile of X's own zones that does not touch the landing's (the two
        are islands of the route), when the guide's way into the landing's
        island is X's entry route D: the gate leaves of D (its obstacle
        steps that touch the landing state's zone, else all of them) are
        charged. Regicide d3505f4ee row 75: goto-crossThePit from the voyage
        cave (2314,9624: isInWellEntrance) to 2461,9699
        (isInUndergroundSection2), re-entering the pass by teleport --
        travelThroughPassSection3's way into section 2 is crossTheBridge.

        Not judged: a start in no zone of X (open ground may walk in some
        other way than the guide's), a start on D's own island (route_hops
        reads those), a hop with a press, walk, use or cast between, and a
        hop with a press of a gate leaf's copy between."""
        if getattr(self, "_route_entries", None) is not None:
            return self._route_entries
        self._route_entries = {}
        if not self.rows:
            return self._route_entries
        gates = self.route_gates()
        if not gates:
            return self._route_entries
        track = self.player_track()
        for i in range(1, len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            if before[2] != point[2] or abs(before[1] - point[1]) > 3200:
                continue
            if any(self._row_moves(row) for row in between):
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            for composite, entry, gate_leaves, states, region, component, entry_components, entry_boxes in gates:
                landing = next(((child, condition, zones) for child, condition, zones in states
                                if any(self._in_box(b, point) for boxes in zones.values() for b in boxes)), None)
                if landing is None:
                    continue
                if any(self._in_box(box, point) for _, box in entry_boxes):
                    continue  # landed on the entry route itself: route_hops' business
                end_components = {component[k] for k, (_, box) in enumerate(region) if self._in_box(box, point)}
                start_components = {component[k] for k, (_, box) in enumerate(region) if self._in_box(box, before)}
                if not start_components or start_components & end_components:
                    continue
                if start_components & entry_components:
                    continue
                if not end_components & entry_components:
                    continue  # D does not lead to where the goto landed
                start_zone = next(zone for zone, box in region if self._in_box(box, before))
                end_zone = next(zone for zone, boxes in landing[2].items() if any(self._in_box(b, point) for b in boxes))
                touching = [(leaf, obstacle) for leaf, obstacle in gate_leaves
                            if any(self._boxes_touch(gb, lb)
                                   for _, condition in self.route_states()[0].get(leaf.name, [])
                                   for gbs in self.guide.condition_zones(condition).values() for gb in gbs
                                   for lbs in landing[2].values() for lb in lbs)]
                charged = touching or gate_leaves
                if any(self._pressed_between(between, obstacle[0], leaf.point) for leaf, obstacle in charged):
                    continue
                for leaf, (locs, word) in charged:
                    items = self._route_entries.setdefault(leaf.name, [])
                    if any(key == (row["index"], row["step"]) for key, _ in items):
                        continue
                    items.append(((row["index"], row["step"]),
                        "ledger row %s %r lands at %d,%d,%d (%s) from %d,%d,%d (%s, row %s %r), which no zone of "
                        "%s joins to it, without pressing the %s %s names (%s): the guide's way into %s is %s "
                        "(%s's default, guide line %d)" % (
                            row["index"], row["step"], point[0], point[1], point[2], end_zone,
                            before[0], before[1], before[2], start_zone, from_row["index"], from_row["step"],
                            composite, word, leaf.name, ",".join(locs), end_zone, entry, composite,
                            self.guide.steps[self.guide.resolve(composite)].line or 0)))
        return self._route_entries

    # -- a guarded interior: entered through its door, left only through it
    #    (seam goto_into_a_guarded_interior_from_outside_any_zone, b51)

    ROOM_EXIT_TILES = 24

    def _region_of(self, composite):
        """[(zone, box)] over every addStep condition of `composite` and the
        ConditionalSteps under it."""
        return self.route_zone_boxes(self.route_subtree(composite))

    def _own_obstacle(self, step):
        """route_obstacle on the step's OWN targets: ladder() folds a
        parent's sub-steps' targets into it (enterFortress gains the White
        Knights' Castle stairs), which would make a door read as a stair and
        the answer depend on whether the ladder was built yet."""
        own = self.guide.own_targets.get(step.name)
        if own is None or own == step.targets:
            return self.route_obstacle(step)
        proxy = copy.copy(step)
        proxy.targets = list(own)
        return self.route_obstacle(proxy)

    def door_routes(self):
        """[(composite X, door D, locs, word, rooms [(zone, box)], footprint
        rectangle)] for every ConditionalStep X whose constructor default D is
        ONE ObjectStep on a route obstacle (a door, gate, barrier...; a plain
        ladder or stair is travel) whose WorldPoint touches X's own state
        zones on D's level: the guide shows D whenever the player is in none
        of X's zones, so D is the way into the room it opens on (Black
        Knights' Fortress's infiltrateTheFortress = new ConditionalStep(this,
        enterFortress), bkfortressdoor1 at 3016,3514,0 opening on
        mainEntrance3). The rooms are the zone boxes D's point touches
        (within ROUTE_ZONE_SLACK); a zone the guide shows D itself in is the
        door's outside and is left out. The footprint is the x,z hull of the
        island of X's zones those rooms belong to (zones within
        ROUTE_ZONE_SLACK tiles are one island) plus every zone on another
        level whose x,z footprint touches it (the floors above and below).
        Only the rooms the door opens on are judged: a building may have
        other doors the guide does not name (Ernest the Chicken's pickupSpade
        leaves Draynor Manor's east room "through the door in the same
        room"), so a landing deeper inside is not a hop past D.

        route_gates reads a default that is a single step as no way in at all
        (Quest Helper puts a route's LAST step there as often as its first:
        talkToIorwerth, climbDownWell, enterTyrasCamp). Here it is read only
        when it is an obstacle on the edge of X's zones: a last step does not
        open on the zones the route walks through."""
        if getattr(self, "_door_routes", None) is not None:
            return self._door_routes
        self._door_routes = []
        resolve = self.guide.resolve
        for composite in sorted(self.guide.branch_conds):
            step = self.guide.steps.get(resolve(composite))
            if step is None or not step.children:
                continue
            door = self.guide.steps.get(resolve(step.children[0]))
            if door is None or door.is_composite() or door.point is None:
                continue
            obstacle = self._own_obstacle(door)
            if obstacle is None:
                continue
            # a zone the guide shows D in is the door's OUTSIDE (Eadgar's
            # Ruse: returnParrotToEadgar.addStep(inTrollheimArea,
            # enterEadgarCaveWithTrainedParrot) -- the mountain before the cave)
            outside = set()
            for name in self.route_subtree(composite):
                for child, condition in self.guide.branch_conds.get(name, []):
                    if resolve(child) == door.name:
                        outside.update(self.guide.condition_zones(condition))
            region = [(zone, box) for zone, box in self._region_of(composite) if zone not in outside]
            if not region:
                continue
            x, z, level = door.point
            spot = (x, x, z, z, level, level)
            parent = list(range(len(region)))

            def find(i):
                while parent[i] != i:
                    parent[i] = parent[parent[i]]
                    i = parent[i]
                return i

            for i in range(len(region)):
                for j in range(i + 1, len(region)):
                    if self._boxes_touch(region[i][1], region[j][1]):
                        parent[find(i)] = find(j)
            rooms = [region[i] for i, (_, box) in enumerate(region) if self._boxes_touch(spot, box)]
            seeds = {find(i) for i, (_, box) in enumerate(region) if self._boxes_touch(spot, box)}
            if not seeds:
                continue
            island = [i for i in range(len(region)) if find(i) in seeds]

            def flat_touch(a, b):
                return max(0, b[0] - a[1], a[0] - b[1]) <= self.ROUTE_ZONE_SLACK and \
                    max(0, b[2] - a[3], a[2] - b[3]) <= self.ROUTE_ZONE_SLACK

            interior = [region[i] for i in range(len(region))
                        if i in island or any(flat_touch(region[i][1], region[k][1]) for k in island)]
            hull = (min(b[0] for _, b in interior), max(b[1] for _, b in interior),
                    min(b[2] for _, b in interior), max(b[3] for _, b in interior))
            self._door_routes.append((composite, door, obstacle[0], obstacle[1], rooms, hull))
        return self._door_routes

    def door_entries(self):
        """{door step name: [((row index, row step), reason)]}: a goto that
        lands in a room of a door route (door_routes) from a tile outside its
        footprint on the same map frame (any level: the start is not in the
        building at all), with nothing between to make the start unknown and
        no press of the door's copy between. A start in a dungeon frame is
        not judged: a ladder out of it may come up inside (In Search of the
        Myreque's hideout opens on the entrance island). A door several
        ConditionalSteps share as their default (Mourning's End Part I's
        mournerstewdoor, four of them) is charged to each. Black Knights'
        Fortress 4b849c46b row 16: goto-fortress-entrance from the White
        Knights' Castle (2960,3335,2) to 3016,3516,0 (inMainEntrance), past
        bkfortressdoor1 and the disguise check its [oploc1] makes.

        Not judged: a start inside the footprint (a hop between the
        interior's floors skips a ladder, not the front door: the climb rules
        judge those), a hop with a press, walk, use or cast between, and a
        hop with a press of the door between."""
        if getattr(self, "_door_entries", None) is not None:
            return self._door_entries
        self._door_entries = {}
        routes = self.door_routes()
        if not routes or not self.rows:
            return self._door_entries
        track = self.player_track()
        slack = self.ROUTE_ZONE_SLACK
        for i in range(1, len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            if any(self._row_moves(row) for row in between):
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            if abs(before[1] - point[1]) > 3200 or abs(before[0] - point[0]) > 3200:
                continue  # from a dungeon: its ladder may come up inside (the Myreque hideout)
            for composite, door, locs, word, rooms, hull in routes:
                end_zone = next((zone for zone, box in rooms if self._in_box(box, point)), None)
                if end_zone is None:
                    continue
                if hull[0] - slack <= before[0] <= hull[1] + slack and hull[2] - slack <= before[1] <= hull[3] + slack:
                    continue  # started in the building: not the front door's hop
                if self._pressed_between(between, locs, door.point):
                    continue
                items = self._door_entries.setdefault(door.name, [])
                if any(key == (row["index"], row["step"]) for key, _ in items):
                    continue
                start_zone = next((zone for zone, box in self._region_of(composite) if self._in_box(box, before)),
                                  "no zone of it")
                items.append(((row["index"], row["step"]),
                    "ledger row %s %r lands at %d,%d,%d (%s) from %d,%d,%d (%s, row %s %r), outside the building "
                    "%s's zones make, without pressing the %s %s names (%s): the guide's way in is %s (%s's "
                    "default, guide line %d), whose %s at %d,%d,%d opens on %s" % (
                        row["index"], row["step"], point[0], point[1], point[2], end_zone,
                        before[0], before[1], before[2], start_zone, from_row["index"], from_row["step"],
                        composite, word, door.name, ",".join(locs), door.name, composite,
                        self.guide.steps[self.guide.resolve(composite)].line or 0, word,
                        door.point[0], door.point[1], door.point[2], end_zone)))
        return self._door_entries

    FRAME_FOLD_SLACK = 16

    def frame_entries(self):
        """{entrance step name: [((row index, row step), reason)]}: a goto
        from one map frame into a state zone of a ConditionalStep X that lies
        in ANOTHER frame (a cave, a dungeon: 6400 tiles away, nothing walks
        there), when X's constructor default D is ONE ObjectStep on a route
        obstacle standing in the frame the goto left. The guide shows D
        whenever the player is in none of X's zones, so D is its way down, on
        every visit and not only the first. The Eyes of Glouphrie (b55 round
        3, sampler revert 65805d323): `new ConditionalStep(this, enterCave)`
        with `addStep(inCave, ...)` at almost every stage; the test clicked
        eyeglo_brimstails_cave_entrance twice, then came back three times by
        goto (rows 76, 223, 260: from the evergreen, from Narnode, from the
        Grand Tree) and read FULL, because enterCave already had a PASS row.

        door_routes cannot see this: its door must touch the zones it opens
        on, and a cave mouth stands on the surface above them. Judged only
        when the zone IS the place under the entrance (its box folded by
        whole 6400-tile frames holds D's tile within FRAME_FOLD_SLACK): a
        dungeon the map puts somewhere else than under its mouth is not read.

        Not judged: a hop that stays in one frame (walkable: the other rules'
        business), a start inside one of X's zones, a landing in a zone the
        guide shows D itself in, a hop with a press, walk, use or cast
        between, a hop with a press of D's loc between, and a hop with no
        departure stamp and rows between it and the last known tile."""
        if getattr(self, "_frame_entries", None) is not None:
            return self._frame_entries
        self._frame_entries = {}
        if not self.rows:
            return self._frame_entries
        resolve = self.guide.resolve
        routes = []
        for composite in sorted(self.guide.branch_conds):
            step = self.guide.steps.get(resolve(composite))
            if step is None or not step.children:
                continue
            door = self.guide.steps.get(resolve(step.children[0]))
            if door is None or door.is_composite() or door.point is None or door.kind != "ObjectStep":
                continue
            obstacle = self._own_obstacle(door)
            if obstacle is None:
                continue
            outside = set()
            for child, condition in self.guide.branch_conds.get(composite, []):
                if resolve(child) == door.name:
                    outside.update(self.guide.condition_zones(condition))
            # ...and the zone is the place UNDER (or over) the entrance: its
            # box, folded by whole 6400-tile frames, holds D's tile within
            # FRAME_FOLD_SLACK. A zone elsewhere in another frame is some
            # other place the route visits, not what D opens on (Eadgar's
            # Ruse: useParrotOnRack's default is the Troll Stronghold
            # entrance, and inEadgarsCave is 50 tiles from under it).
            far = []
            for zone, box in self._region_of(composite):
                if zone in outside:
                    continue
                frames = round(((box[2] + box[3]) / 2.0 - door.point[1]) / 6400.0)
                if frames == 0:
                    continue
                slack = self.FRAME_FOLD_SLACK
                if box[0] - slack <= door.point[0] <= box[1] + slack and \
                        box[2] - frames * 6400 - slack <= door.point[1] <= box[3] - frames * 6400 + slack:
                    far.append((zone, box))
            if far:
                routes.append((composite, door, obstacle[0], obstacle[1], far))
        if not routes:
            return self._frame_entries
        track = self.player_track()
        for i in range(1, len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            if abs(before[1] - point[1]) <= 3200 and abs(before[0] - point[0]) <= 3200:
                continue  # one frame: a walk could make it
            if any(self._row_moves(row) for row in between):
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            for composite, door, locs, word, far in routes:
                end_zone = next((zone for zone, box in far if self._in_box(box, point)), None)
                if end_zone is None:
                    continue
                if any(self._in_box(box, before) for _, box in self._region_of(composite)):
                    continue  # started in one of X's own zones
                if abs(before[1] - door.point[1]) > 3200 or abs(before[0] - door.point[0]) > 3200:
                    continue  # did not leave the entrance's frame
                if self._pressed_between(between, locs, door.point):
                    continue
                items = self._frame_entries.setdefault(door.name, [])
                if any(key == (row["index"], row["step"]) for key, _ in items):
                    continue
                items.append(((row["index"], row["step"]),
                    "ledger row %s %r lands at %d,%d,%d (%s) from %d,%d,%d (row %s %r), another map frame, "
                    "without pressing the %s %s names (%s): the guide's way into %s is %s (%s's default, guide "
                    "line %d), on every visit" % (
                        row["index"], row["step"], point[0], point[1], point[2], end_zone,
                        before[0], before[1], before[2], from_row["index"], from_row["step"],
                        word, door.name, ",".join(locs), end_zone, door.name, composite,
                        self.guide.steps[self.guide.resolve(composite)].line or 0)))
        return self._frame_entries

    ENCLOSURE_MAX_TILES = 400
    ENCLOSURE_BOX_SLACK = 2

    def _hop_step(self, position, until, room):
        """The ladder leaf a goto at rows[position] serves: the one its own row
        is named after (the longest such name: goto-talkToPhileasAgain is
        talkToPhileasAgain's, not talkToPhileas's), else the first PASS row
        before `until` named after a leaf whose WorldPoint is in `room`'s box
        on its level. None when neither says."""
        leaves = getattr(self, "_ladder_leaves", None) or []
        row = self.rows[position]
        named = [leaf for leaf in leaves if self.row_names_step(row["step"], leaf.name)]
        if named:
            return max(named, key=lambda leaf: len(leaf.name))
        slack = self.ENCLOSURE_BOX_SLACK
        box = room["box"]
        for later in self.rows[position + 1:until]:
            if later.get("verdict") != "PASS":
                continue
            named = [leaf for leaf in leaves if self.row_names_step(later["step"], leaf.name)]
            if not named:
                continue
            leaf = max(named, key=lambda leaf: len(leaf.name))
            point = leaf.point
            if point is not None and box[0] - slack <= point[0] <= box[1] + slack and \
                    box[2] - slack <= point[1] <= box[3] + slack and point[2] == room["level"]:
                return leaf
            return None
        return None

    DOOR_OPEN_TICKS = 500  # doors.rs2/doubledoors.rs2 loc_add(..., 500): an opened door shuts after 500 ticks

    def _door_opened_before(self, position, doors):
        """Did a PASS row before rows[position] press one of `doors` (the copy
        at its tile, _pressed_between) at most DOOR_OPEN_TICKS ticks earlier
        (the ledger's per-row `ticks`, summed)? The door may still stand open:
        a goto through an open doorway is a walk."""
        elapsed = 0
        for row in reversed(self.rows[:position]):
            if elapsed > self.DOOR_OPEN_TICKS:
                return False
            if row.get("verdict") == "PASS" and any(
                    self._pressed_between([row], [symbol], at) for symbol, at in doors):
                return True
            try:
                elapsed += int(row.get("ticks") or 0)
            except ValueError:
                pass
        return False

    def enclosure_entries(self):
        """{ladder step name: [((row index, row step), reason)]}: a goto that
        lands in a ROOM -- a tile the map's walls close in (MapWalls: a 4-way
        flood from the landing stops within ENCLOSURE_MAX_TILES) with a door
        loc in its perimeter -- from a departure outside it, with no PASS row
        pressing that door in the DOOR_OPEN_TICKS before. It went through the wall
        or past the closed door. Charged to the step the goto serves
        (_hop_step). door_entries needs the guide to name the door (an
        ObjectStep a ConditionalStep defaults to); a house the guide only
        walks into has no such step. Tale of the Righteous (b56 sampler,
        8f3c5b544 reverted): returnToPhileasTent (stage 15 -> 16, "return to
        Phileas", an arrival) was row 93's goto_tile 1542,3570,0 from Shiro's
        room 1486,3634,1, and goto-talkToPhileasAgain landed there from the
        prison 1551,10211,0 -- both inside Phileas's house, past
        wallkit_shayzien_door01_l_reverse at 1540,3570 (maps/m24_55.jl2), and
        the grade read FULL 30/30.

        The flood follows the room's own climbs to the floors they lead to
        (MapWalls.enclosure): Sanfew's upstairs room is closed only if the
        stairs come down into a closed ground floor; a plain stair from an
        open doorway is travel. Outside means: a tile no floor's flood
        reached, and on another level of the same map frame also outside
        the building's box (+ENCLOSURE_BOX_SLACK: another floor of the same
        building not reached through the room's climbs is the climb rules'
        business); or another map frame. Not judged: a landing on open
        ground or in an area bigger than a building, a building with a climb
        DOWN from level 0 (a dungeon below may come out anywhere), a room
        with no door (sealed_entries' business), a hop with a press, walk, use or
        cast between it and an unstamped start, and a hop within
        DOOR_OPEN_TICKS of a press of the door (it may still stand open).
        Locs content adds or removes at run time are not seen."""
        if getattr(self, "_enclosure_entries", None) is not None:
            return self._enclosure_entries
        self._enclosure_entries = {}
        if not self.rows:
            return self._enclosure_entries
        track = self.player_track()
        walls = None
        slack = self.ENCLOSURE_BOX_SLACK
        for i in range(len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            if i == 0 and position not in self._goto_from:
                continue  # the run's first reading, and no departure stamp _start_rows could give it
            before, before_position, between, stamped = self.hop_start(track, i)
            if any(self._row_moves(row) for row in between):
                continue
            if walls is None:
                walls = map_walls()
            room, via = self._landing_room(walls, track, i)
            if room is None or not room["doors"]:
                continue
            room["level"] = point[2]
            box = room["box"]
            if room["to_dungeon"]:
                continue  # a climb down from it leads to another frame, which may come out anywhere
            other_frame = abs(before[1] - point[1]) > 3200 or abs(before[0] - point[0]) > 3200
            if other_frame:
                where = "another map frame"
            elif walls.in_room(room, before[0], before[1], before[2]):
                continue  # a hop inside the building (any floor its climbs reach)
            elif before[2] == point[2]:
                where = "outside it on the same level"
            else:
                if box[0] - slack <= before[0] <= box[1] + slack and box[2] - slack <= before[1] <= box[3] + slack:
                    continue  # another floor of the same building, not reached through this room's climbs
                where = "level %d, outside the building's footprint" % before[2]
            if self._door_opened_before(position, room["doors"]):
                continue
            until = next((track[j][0] for j in range(i + 1, len(track)) if track[j][2]), len(self.rows))
            doors = ", ".join("%s at %d,%d,%d" % ((symbol,) + at) for symbol, at in room["doors"][:3])
            name = self._charge_name(position, until, room, "past " + room["doors"][0][0])
            if name is None:
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            items = self._enclosure_entries.setdefault(name, [])
            if any(key == (row["index"], row["step"]) for key, _ in items):
                continue
            items.append(((row["index"], row["step"]),
                "ledger row %s %r lands at %d,%d,%d%s in a room the map walls in (%d tiles%s, x %d-%d z %d-%d, "
                "door %s: maps/m%d_%d.jl2) from %d,%d,%d (%s, row %s %r), with no press of that door in the "
                "%d ticks before: it went past the closed door (enclosure_entries)" % (
                    row["index"], row["step"], point[0], point[1], point[2], self._via_text(via),
                    len(room["tiles"]),
                    "" if len(room["floors"]) == 1 else " on this floor, levels %s by %s" % (
                        ",".join(str(l) for l in sorted(room["floors"])), ",".join(room["climbs"][:2])),
                    box[0], box[1], box[2], box[3], doors, point[0] >> 6, point[1] >> 6,
                    before[0], before[1], before[2], where, from_row["index"], from_row["step"],
                    self.DOOR_OPEN_TICKS)))
        return self._enclosure_entries

    # A goto no ladder step can be charged with is reported as a step of its
    # own (grade() adds it, CHEAT), named "(goto past <loc>)".
    UNCHARGED = "(goto "
    # enclosure_entries charges a goto it cannot tie to a guide step to the
    # next guide step the run does, else to an UNCHARGED step of its own;
    # False is the seam-1 reading (not charged at all), kept for the fixtures.
    ENCLOSURE_CHARGE_ANY = True
    ENCLOSURE_NEXT_READING_TILES = 4

    def _charge_name(self, position, until, room, what):
        """The step name a goto at rows[position] is charged to: _hop_step's
        leaf, else (ENCLOSURE_CHARGE_ANY) the first PASS row before `until`
        named after a ladder leaf, else "(goto <what>)". Wanted!'s `mage`
        and `pos2.goto` rows name no guide step, and the steps after them
        are the test's own (`mage.talk`); twilightspromise's
        `goto-enterHQ` names none either (there is no enterHQ step) and its
        next guide row is goUpHQ, whose stairs are outside the side room it
        landed in. Before matthew-mbp-m4-b56-grader2 all three went
        uncharged.

        A setup placement's row (_start_rows) is always a step of its own,
        "(setup placement) <cheat> lands at x,z,l <what>": the setup put the
        player there, no guide step did."""
        placed = self.rows[position]
        if placed.get("placement"):
            landing = re.match(r"at (\S+)", placed["detail"])
            return "%s lands at %s %s" % (placed["step"], landing.group(1) if landing else "?", what)
        leaves = getattr(self, "_ladder_leaves", None) or []
        if room is not None:
            leaf = self._hop_step(position, until, room)
        else:
            leaf = self._row_leaf(self.rows[position]["step"], leaves)
        if leaf is not None:
            return leaf.name
        if not self.ENCLOSURE_CHARGE_ANY:
            return None
        for row in [self.rows[position]] + [r for r in self.rows[position + 1:until] if r.get("verdict") == "PASS"]:
            named = self._row_leaf(row["step"], leaves)
            if named is not None:
                return named.name
        return self.UNCHARGED + what + ")"

    def _row_leaf(self, row_step, leaves):
        """The leaf a row is named after: the exact name, else the longest
        name the row's name starts with, else the longest row_names_step
        match (a row `goUpHQ` is goUpHQ's, not goUpHQ2's)."""
        named = [leaf for leaf in leaves if self.row_names_step(row_step, leaf.name)]
        if not named:
            return None
        row_name = norm(row_step)
        exact = [leaf for leaf in named if norm(leaf.name) == row_name]
        if exact:
            return exact[0]
        prefix = [leaf for leaf in named if row_name.startswith(norm(leaf.name))]
        return max(prefix or named, key=lambda leaf: len(leaf.name))

    ROW_CROSSES = re.compile(r"click_loc|pressed the copy|teleport: |\bemerged\b|\blanded\b")
    LINE_CROSSES = re.compile(r"t\.player\.(click_loc|press|use_on|cast)\b|t\.drive\.op\b|t\.sail\.")

    def _row_crosses(self, row):
        """_row_moves without the plain walks: a press, a use on a loc, a
        cast, an op, a teleport -- what can take the player through a wall
        or a closed door. A walk (`walk_to`, a talk's `map_flag`) never
        opens a door, so where it ends is reachable from where it began."""
        if self.ROW_CROSSES.search(row.get("detail") or ""):
            return True
        line = self.row_line(row["step"])
        return line is not None and self.LINE_CROSSES.search(self.test.code_lines[line - 1]) is not None

    def _reading_after(self, track, i):
        """Where the player stood next after the goto at track[i], before any
        row that could take them through a wall or door (_row_crosses; a
        walk or a talk's walk is fine): the next reading, or the next goto's
        departure stamp. None when unknown."""
        position = track[i][0]
        for j in range(i + 1, len(track)):
            later, point, is_goto = track[j]
            if is_goto:
                if any(self._row_crosses(row) for row in self.rows[position + 1:later]):
                    return None
                return self._goto_from.get(later)
            if any(self._row_crosses(row) for row in self.rows[position + 1:later + 1]):
                return None
            return point
        return None

    @staticmethod
    def _via_text(via):
        return "" if via is None else " (a blocked tile on the room's edge; the player stood at %d,%d,%d next)" % via

    def _landing_room(self, walls, track, i):
        """(room, via): the enclosure the goto at track[i] lands in. A landing
        on a blocked tile whose own flood is open (a diagonal wall at a
        building's corner: it steps off inside and outside) is read by where
        the player stood NEXT (_reading_after, within
        ENCLOSURE_NEXT_READING_TILES on the same level, nothing moving the
        player between): that tile's room, when the landing steps off into
        it. Atza's house (atfirstlight goto-talkToAtza, 1696,3061 on
        wallkit_wooden01_default01 shape 9; the next goto left from
        1696,3064, inside). `via` is that next tile, else None."""
        point = track[i][1]
        room = walls.enclosure(point[0], point[1], point[2], self.ENCLOSURE_MAX_TILES)
        if room is not None or not walls.START_ON_BLOCKED or point not in walls.blocked:
            return room, None
        after = self._reading_after(track, i)
        if after is None or after[2] != point[2] or after in walls.blocked or \
                max(abs(after[0] - point[0]), abs(after[1] - point[1])) > self.ENCLOSURE_NEXT_READING_TILES:
            return None, None
        room = walls.enclosure(after[0], after[1], after[2], self.ENCLOSURE_MAX_TILES)
        if room is None or not walls.in_room(room, point[0], point[1], point[2]):
            return None, None
        return room, after

    # A goto out of a room further than this may stand for a teleport out
    # (room_exits' ROOM_EXIT_TILES, the same policy).
    ENCLOSURE_EXIT_TILES = 24

    def enclosure_exits(self):
        """{step name: [((row index, row step), reason)]}: the mirror of
        enclosure_entries -- a goto that LEAVES a room the map walls in (the
        departure's flood closes within ENCLOSURE_MAX_TILES, a closed door
        in its perimeter) for a tile outside it on the same level, at most
        ENCLOSURE_EXIT_TILES away, with no PASS row pressing that door in
        the DOOR_OPEN_TICKS before. A walk out of that room opens the door.
        At First Light (b56 sampler): goto-returnToFoxAfterTrim left Atza's
        house (1698,3063; fortis_door_l at 1698,3064) and the test never
        clicked a fortis door. Charged like an entry (_charge_name).

        Not judged: a hop further than ENCLOSURE_EXIT_TILES or to another
        level or map frame (a teleport out of a room is a spell away, and a
        climb is the climb rules' business: Dream Mentor's
        goto-returnToOneiromancer, 76 tiles out of the brazier hall, is left
        to that policy), a room with no door (sealed_exits' business, with
        no distance cap), a room with a climb down from level 0, a hop with
        an unknown start or a press, walk, use or cast between, and a hop
        within DOOR_OPEN_TICKS of a press of the door."""
        if getattr(self, "_enclosure_exits", None) is not None:
            return self._enclosure_exits
        self._enclosure_exits = {}
        if not self.rows:
            return self._enclosure_exits
        track = self.player_track()
        walls = map_walls()
        for i in range(len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            if i == 0 and position not in self._goto_from:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            if any(self._row_moves(row) for row in between):
                continue
            if before[2] != point[2] or \
                    max(abs(before[1] - point[1]), abs(before[0] - point[0])) > self.ENCLOSURE_EXIT_TILES:
                continue  # another level, or far enough that a teleport out is the player's way
            room = walls.enclosure(before[0], before[1], before[2], self.ENCLOSURE_MAX_TILES)
            if room is None or not room["doors"] or room["to_dungeon"]:
                continue
            if walls.in_room(room, point[0], point[1], point[2]):
                continue
            if self._door_opened_before(position, room["doors"]):
                continue
            until = next((track[j][0] for j in range(i + 1, len(track)) if track[j][2]), len(self.rows))
            name = self._charge_name(position, until, None, "out past " + room["doors"][0][0])
            if name is None:
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            items = self._enclosure_exits.setdefault(name, [])
            if any(key == (row["index"], row["step"]) for key, _ in items):
                continue
            box = room["box"]
            doors = ", ".join("%s at %d,%d,%d" % ((symbol,) + at) for symbol, at in room["doors"][:3])
            items.append(((row["index"], row["step"]),
                "ledger row %s %r leaves a room the map walls in (%d tiles, x %d-%d z %d-%d, door %s: "
                "maps/m%d_%d.jl2) from %d,%d,%d (row %s %r) for %d,%d,%d outside it, with no press of that "
                "door in the %d ticks before: it went out past the closed door (enclosure_exits)" % (
                    row["index"], row["step"], len(room["tiles"]), box[0], box[1], box[2], box[3], doors,
                    before[0] >> 6, before[1] >> 6, before[0], before[1], before[2], from_row["index"],
                    from_row["step"], point[0], point[1], point[2], self.DOOR_OPEN_TICKS)))
        return self._enclosure_exits

    def sealed_entries(self):
        """{step name: [((row index, row step), reason)]}: a goto that lands
        in a POCKET the map closes on every side -- the landing's flood
        closes within ENCLOSURE_MAX_TILES, with no door and no climb -- from
        a tile outside it on the same level of the same map frame. Nothing
        a player does reaches it on foot. At First Light (b56 sampler):
        goto-talkToVerity and goto-talkToVerityEnd land at 1559,9467, behind
        the bar counter (14 tiles, the flap hg_table_tavern02_door01 at
        1556,9463 has no op), from 1557,9451 in the same cave.

        A loc with an op or a script trigger in the pocket or on its edge
        (MapWalls.enclosure's `ops`: a stepping stone, a squeeze gap, a
        portal, a root a rope is used on) may be the way in: the hop is
        charged only when none was pressed in the DOOR_OPEN_TICKS before
        (Wanted!'s pos4.goto past the swamp cave's stepping stone, whose
        pocket's only op loc is tog_cave_down), and not at all when the run
        uses one before the next goto (the goto stood the player AT it, on
        the wrong side of the map's blocked tiles: Spirits of the Elid's
        desert_water_cave_root, Twilight's Promise's
        colosseum_entrance_outside).

        Not judged: a pocket with a door (enclosure_entries) or a climb (it
        may be the way in), a hop from another level or map frame (a
        ladder, a cave, a teleport), and the usual unknown start."""
        if getattr(self, "_sealed_entries", None) is not None:
            return self._sealed_entries
        self._sealed_entries = {}
        if not self.rows:
            return self._sealed_entries
        track = self.player_track()
        walls = map_walls()
        for i in range(len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            if i == 0 and position not in self._goto_from:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            if any(self._row_moves(row) for row in between):
                continue
            if before[2] != point[2] or abs(before[1] - point[1]) > 3200 or abs(before[0] - point[0]) > 3200:
                continue
            room, via = self._landing_room(walls, track, i)
            if room is None or room["doors"] or room["climbs"] or room["to_dungeon"]:
                continue
            if walls.in_room(room, before[0], before[1], before[2]):
                continue
            if room["ops"] and self._door_opened_before(position, room["ops"]):
                continue  # one of its op locs was pressed: that may have been the way in
            room["level"] = point[2]
            until = next((track[j][0] for j in range(i + 1, len(track)) if track[j][2]), len(self.rows))
            if room["ops"] and any(row.get("verdict") == "PASS" and self._pressed_between([row], [symbol], at)
                                   for row in self.rows[position + 1:until] for symbol, at in room["ops"]):
                # the goto stood the player AT the pocket's op loc and the run
                # then used it (Twilight's Promise goto-enterColosseum onto the
                # facade beside colosseum_entrance_outside, then Enter): the
                # loc was not skipped
                continue
            name = self._charge_name(position, until, room, "into a sealed pocket")
            if name is None:
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            items = self._sealed_entries.setdefault(name, [])
            if any(key == (row["index"], row["step"]) for key, _ in items):
                continue
            box = room["box"]
            ops = ", ".join("%s at %d,%d,%d" % ((symbol,) + at) for symbol, at in room["ops"][:3])
            items.append(((row["index"], row["step"]),
                "ledger row %s %r lands at %d,%d,%d%s in a pocket the map closes on every side (%d tiles, "
                "x %d-%d z %d-%d, no door, no climb, %s: maps/m%d_%d.jl2) from %d,%d,%d "
                "(row %s %r) on the same level: no walk reaches it (sealed_entries)" % (
                    row["index"], row["step"], point[0], point[1], point[2], self._via_text(via),
                    len(room["tiles"]), box[0], box[1], box[2], box[3],
                    "no loc with an op" if not ops else "its only op locs %s not pressed in the %d ticks before" % (
                        ops, self.DOOR_OPEN_TICKS),
                    point[0] >> 6, point[1] >> 6, before[0], before[1], before[2], from_row["index"],
                    from_row["step"])))
        return self._sealed_entries

    def sealed_exits(self):
        """{step name: [((row index, row step), reason)]}: the mirror of
        sealed_entries -- a goto that LEAVES a pocket the map closes on every
        side (the departure's flood closes within ENCLOSURE_MAX_TILES, with
        no door, no climb and no climb down from level 0) for a tile outside
        it on the same level of the same map frame. Nothing a player does
        walks out of it. Another Slice of H.A.M. (b62 round-2 fixer): the
        station doorway lands on the train platform 2488,5536 (296 tiles,
        x 2479-2488 z 5517-5554; its only op loc is the way back,
        slice_underground_wall_exit_goblin at 2489,5536), and
        PROBE.goto-talkToTegdak left it for Tegdak at 2512,5562 in the dig
        tunnel; enclosure_exits skipped the room (no door) and the hop
        (over ENCLOSURE_EXIT_TILES), and the grade read FULL.

        No distance cap: unlike a walled room, a pocket has no door to walk
        out through, so the hop skipped whatever the way out is. A loc with
        an op or a script trigger in the pocket or on its edge (the room's
        `ops`) may be that way: the hop is not charged when a PASS row
        pressed one in the DOOR_OPEN_TICKS before.

        Not judged: a pocket with a door (enclosure_exits) or a climb (it may
        be the way out), a hop to another level or map frame (a ladder, a
        cave, a teleport), and the usual unknown start or press, walk, use
        or cast between."""
        if getattr(self, "_sealed_exits", None) is not None:
            return self._sealed_exits
        self._sealed_exits = {}
        if not self.rows:
            return self._sealed_exits
        track = self.player_track()
        walls = map_walls()
        for i in range(len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            if i == 0 and position not in self._goto_from:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            if any(self._row_moves(row) for row in between):
                continue
            if before[2] != point[2] or abs(before[1] - point[1]) > 3200 or abs(before[0] - point[0]) > 3200:
                continue  # another level or map frame: a climb, a cave, a teleport
            room = walls.enclosure(before[0], before[1], before[2], self.ENCLOSURE_MAX_TILES)
            if room is None or room["doors"] or room["climbs"] or room["to_dungeon"]:
                continue
            if walls.in_room(room, point[0], point[1], point[2]):
                continue
            if room["ops"] and (self._pocket_left_by_press(position, room, before[2])
                                if self.POCKET_PRESS_SINCE_ARRIVAL else
                                self._door_opened_before(position, room["ops"])):
                continue  # a press of one of its op locs since the player arrived may have been the way out
            until = next((track[j][0] for j in range(i + 1, len(track)) if track[j][2]), len(self.rows))
            name = self._charge_name(position, until, None, "out of a sealed pocket")
            if name is None:
                continue
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            items = self._sealed_exits.setdefault(name, [])
            if any(key == (row["index"], row["step"]) for key, _ in items):
                continue
            box = room["box"]
            ops = ", ".join("%s at %d,%d,%d" % ((symbol,) + at) for symbol, at in room["ops"][:3])
            items.append(((row["index"], row["step"]),
                "ledger row %s %r leaves a pocket the map closes on every side (%d tiles, x %d-%d z %d-%d, "
                "no door, no climb, %s: maps/m%d_%d.jl2) from %d,%d,%d (row %s %r) for %d,%d,%d outside it "
                "on the same level: no walk leaves it (sealed_exits)" % (
                    row["index"], row["step"], len(room["tiles"]), box[0], box[1], box[2], box[3],
                    "no loc with an op" if not ops else "its only op locs %s not pressed since the player last "
                    "arrived in it, within the %d ticks before (a press that brought the player in, or left "
                    "them inside, is not a way out)" % (ops, self.DOOR_OPEN_TICKS),
                    before[0] >> 6, before[1] >> 6, before[0], before[1], before[2], from_row["index"],
                    from_row["step"], point[0], point[1], point[2])))
        return self._sealed_exits

    # sealed_exits credits a pocket op-loc press only since the player last
    # arrived (_pocket_left_by_press); False is the b62 reading (any press
    # in the DOOR_OPEN_TICKS before), kept for the fixtures.
    POCKET_PRESS_SINCE_ARRIVAL = True
    # gate_crossings and solid_landings judge at all; False is the reading
    # before seam matthew-mbp-m4-b63-seam1, kept for the fixtures.
    GATE_CROSSINGS = True
    SOLID_LANDINGS = True

    # Where a row says the player stood, in the order its detail says it
    # (_pocket_left_by_press). Only the unambiguous readings: a goto's or a
    # leg check's leading `at x,z,l`, a crossing's `landed x,z,l` and `-> at
    # x,z,l`, the jump readout `teleport: a -> x,z,l`, a walk's `reached
    # x,z,l`, `still at`, `now at x,z`, `standing at x,z level l` and `from
    # a,b to x,z`. Never `pressed the copy at` or `<loc> at x,z,l` (a loc's
    # tile) or `want x,z,l`.
    ROW_READINGS = (
        re.compile(r"^at (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"\blanded (?:on level \d at )?(\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"-> at (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"\bteleport: [\d,]+ -> (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"\breached (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"\bstill at (\d{3,4}),(\d{3,5}),([0-3])\b"),
        re.compile(r"\bstanding at (\d{3,4}),(\d{3,5}) level ([0-3])\b"),
        re.compile(r"\bnow at (\d{3,4}),(\d{3,5})()\b"),
        re.compile(r"\bfrom \d{3,4},\d{3,5} to (\d{3,4}),(\d{3,5})()\b"),
    )
    # A press whose own row says it opened something: a door leaf, a
    # passage, a lock.
    POCKET_OPENED = re.compile(r"\bopen leaf\b|\bopened\b|\bnow open\b|\bunlock(?:ed|s)\b", re.I)

    def _pocket_left_by_press(self, position, room, level):
        """Did a PASS row in the DOOR_OPEN_TICKS before rows[position] press
        one of `room`'s op locs (a pocket's way in or out) AFTER the player
        last arrived in the pocket, without its own row (or a later one)
        showing the player still inside it -- unless the row says it opened
        something? The rows are read in order and each detail in its own
        order (ROW_READINGS): a press makes a candidate; a reading OUTSIDE
        the pocket after it keeps it (the press took the player out; a
        later reading inside is a new arrival and drops it); a reading
        INSIDE drops a candidate that opened nothing (the press brought the
        player in, or left them where they stood). A press with no reading
        after it is given the benefit (Another Slice of H.A.M.'s
        slice_underground_wall_exit_goblin pressed on the platform:
        fixture asoh_sealed_op_pressed).

        Before seam matthew-mbp-m4-b63-seam1 any press of a pocket op loc in
        the 500 ticks before exempted the hop, so a test that swung INTO
        Grew's island (Watchtower's use of the rope on
        tree_ropeswing4_norope, `landed 2505,3087,0`) and then goto'd off it
        (goto-leaveGrewIsland2) was never charged."""
        window = []
        elapsed = 0
        for row in reversed(self.rows[:position]):
            if elapsed > self.DOOR_OPEN_TICKS:
                break
            window.append(row)
            try:
                elapsed += int(row.get("ticks") or 0)
            except ValueError:
                pass
        window.reverse()
        walls = map_walls()
        names = set()
        for symbol, _ in room["ops"]:
            names |= family(symbol)
        if not names:
            return False
        mention = re.compile(r"\b(%s)\b" % "|".join(re.escape(n) for n in sorted(names)))
        candidate = None  # (row, opened, took the player out)
        for row in window:
            detail = re.sub(r" :: .*?(?= ## | -- |$)", "", row.get("detail") or "")
            events = []
            if row.get("verdict") == "PASS" and any(
                    self._pressed_between([row], [symbol], at) for symbol, at in room["ops"]):
                found = mention.search(detail)
                events.append((found.start() if found else 0, "press", None))
            for pattern in self.ROW_READINGS:
                for match in pattern.finditer(detail):
                    x, z = int(match.group(1)), int(match.group(2))
                    tile_level = int(match.group(3)) if match.group(3) else level
                    inside = tile_level == level and walls.in_room(room, x, z, tile_level)
                    events.append((match.start(), "inside" if inside else "outside", (x, z, tile_level)))
            for _, kind, _ in sorted(events, key=lambda e: e[0]):
                if kind == "press":
                    candidate = [row, bool(self.POCKET_OPENED.search(detail)), False]
                elif candidate is None:
                    continue
                elif kind == "outside":
                    candidate[2] = True
                elif candidate[2] or not candidate[1]:
                    candidate = None
        return candidate is not None

    # -- gates between regions, and landings on solid tiles (seam matthew-mbp-m4-b63-seam1)

    # Ledger words that say a row may have put the player on the far side of
    # a door, gate, climb or teleport (gate_crossings' relaxed departure).
    SIDE_CHANGE_DETAIL = re.compile(r"teleport|\blanded\b|\bemerged\b|\bmoved\b|\blevel\b|\bclimb|pass_door|"
                                    r"cross_gate|cross_trap|\bopen leaf\b|\bwent through\b|::goto|::tele",
                                    re.I)
    # Every t.sail verb but the ones that never move the player or the hull
    # (SAIL_NO_MOVE: a read, a notice board, a crate taken, loaded or
    # delivered at a ledger) may change sides: Prying Times' deliverCargo
    # (t.sail.cargo_deliver) kept goto-thurgo's departure unknown before seam
    # matthew-mbp-m4-b65-seam1.
    # SAIL_NO_MOVE_KEPT False is the reading before it (every t.sail verb
    # may change sides), kept for the fixtures.
    SAIL_NO_MOVE = ("state", "tasks", "task_board", "task_accept", "cargo_take", "cargo_load", "cargo_deliver")
    SAIL_NO_MOVE_KEPT = True
    SIDE_CHANGE_LINE = re.compile(r"t\.player\.(cast|teleport|teleport_cast|climb|pass_door|cross_gate|cross_trap|"
                                  r"walk_route)\b|t\.drive\.op\b|t\.sail\.(?!(?:%s)\b)|t\.cheat\b|::goto|::tele"
                                  % "|".join(SAIL_NO_MOVE))
    SIDE_CHANGE_LINE_ANY_SAIL = re.compile(r"t\.sail\.")
    # A loc whose op takes a player across something (TRAVEL_OPS) is no
    # loc a press of keeps the player on one side.
    GATE_MAX_TILES = 1200  # a hop longer than this is not flooded (cost); the longest tier 1 overland hop is ~870

    def _side_kept(self, row):
        """Could this row NOT have taken the player through a closed door or
        gate, up or down a level, or by a teleport? A read, a talk, a chat
        page, a walk (a walk does not open a door) or a press of a loc that
        is no door, climb or travel loc (an inspection, a search, a
        railing). Unknown press targets are read from the detail."""
        detail = row.get("detail") or ""
        if self.SIDE_CHANGE_DETAIL.search(detail):
            return False
        line = self.row_line(row["step"])
        source = self.test.code_lines[line - 1] if line is not None else ""
        if self.SIDE_CHANGE_LINE.search(source) or \
                (not self.SAIL_NO_MOVE_KEPT and self.SIDE_CHANGE_LINE_ANY_SAIL.search(source)):
            return False
        named = set(re.findall(r"[a-z][a-z0-9_]+", detail)) | set(re.findall(r"\"([a-z][a-z0-9_]+)\"", source))
        named |= self._line_names(line)
        for symbol in named:
            if ("loc", symbol) not in OPS and ("loc", symbol) not in DISPLAY:
                continue
            if MapWalls.is_door(symbol) or MapWalls.is_climb(symbol):
                return False
            if any(op_word(name) in TRAVEL_OPS for name in OPS.get(("loc", symbol), {}).values()):
                return False
        return True

    def _line_names(self, line):
        """Every string the action on source `line` names, its locals'
        literals included (`local alidoor_target = t.player.by_symbol("loc",
        "alidoor")` then `use_on("princeskey", alidoor_target)`)."""
        if line is None:
            return set()
        return set(next((named for number, named in self.test.action_lines if number == line), ()))

    def _gate_pressed_before(self, position, doors):
        """_door_opened_before, or a PASS row in the DOOR_OPEN_TICKS before
        rows[position] whose own source line acts on one of `doors` through
        a local (Prince Ali Rescue's `prince.unlock`, the key used on
        alidoor_target): the gate may stand open."""
        if self._door_opened_before(position, doors):
            return True
        names = set()
        for symbol, _ in doors:
            names |= family(symbol)
        elapsed = 0
        for row in reversed(self.rows[:position]):
            if elapsed > self.DOOR_OPEN_TICKS:
                return False
            if row.get("verdict") == "PASS" and self._line_names(self.row_line(row["step"])) & names:
                return True
            try:
                elapsed += int(row.get("ticks") or 0)
            except ValueError:
                pass
        return False

    # goto_tile's own detail: `at x,z,l[ from a,b,c]`, or its ::goto retry's
    # `at x,z,l[ from a,b,c] on attempt N of M via ::goto -- ...`
    # (Watchtower's goto-usePotionOnOgre2).
    GOTO_DETAIL = re.compile(r"^at \d{3,4},\d{3,5},[0-3](?: from \d{3,4},\d{3,5},[0-3])?"
                             r"(?:\s*$| on attempt \d+ of \d+ via ::goto\b)")

    def _real_goto(self, position):
        """Is rows[position] a goto_tile/::goto row (its source line calls
        one, or its detail is goto_tile's bare `at x,z,l[ from a,b,c]`), not
        a climb or crossing row the track reads as a goto because its name
        starts with `goTo` (Quest Helper's goToFirstFloor: `climb ... at
        <the stair's tile>`)?"""
        row = self.rows[position]
        if self.GOTO_DETAIL.match((row.get("detail") or "").strip()):
            return True
        line = self.row_line(row["step"])
        return line is not None and re.search(r"goto_tile|::goto", self.test.code_lines[line - 1]) is not None

    def _known_departure(self, track, i):
        """(tile, row position, stamped) the goto at track[i] left from, for
        gate_crossings: its own departure stamp (`at <landing> from
        <departure>`), else the reading before it when every row between
        kept the player on one side of every door (_side_kept: a talk, a
        page, an inspection between two gotos does not cross the yard
        fence). None when not known. The run's first goto with no stamp
        leaves from the fixture's tile or the last setup placement's landing
        (player_track stamps it, _start_rows); one it could not stamp stays
        unjudged."""
        position = track[i][0]
        stamped = self._goto_from.get(position)
        if stamped is not None:
            return stamped, position, True
        if i == 0:
            return None
        if track[i - 1][2] and not self._real_goto(track[i - 1][0]):
            return None  # the reading before is a climb's or crossing's loc tile, not where the player stood
        before, before_position, between, _ = self.hop_start(track, i)
        if not all(self._side_kept(row) for row in between):
            return None
        # A press's own step off its target tile, `walk_near: stepped off the
        # target tile 2559,3458 (2559,3458 -> 2559,3459)`, is where the
        # player stood after it: off a goto landing on a diagonal railing
        # (which steps off to both sides of the fence), the side they
        # pressed from.
        start_position = before_position
        for offset, row in enumerate(between):
            for match in self.STEPPED_OFF.finditer(row.get("detail") or ""):
                before = (int(match.group(1)), int(match.group(2)), before[2])
                before_position = start_position + 1 + offset
        return before, before_position, False

    STEPPED_OFF = re.compile(r"stepped off the target tile \d+,\d+ \(\d+,\d+ -> (\d{3,4}),(\d{3,5})\)")

    # gate_crossings charges a hop no walk makes at all (MapWalls.hop_route
    # "unreachable": an island, a region only a boat, climb, op loc or
    # teleport reaches) unless a travel row lies between the departure and
    # the goto. False is the reading before seam matthew-mbp-m4-b65-seam1
    # (only an only-way gate was charged; a hop with no route charged
    # nothing), kept for the fixtures.
    NO_ROUTE_CHARGED = True
    # A row that may have carried the player where no walk goes: a cast or
    # teleport, a climb, a t.sail verb that moves the hull or puts the player
    # ashore, a ride a page names (a ferry, a boat, a cart, a carpet). Never
    # the word "sailing" (a Sailing xp read) or a stale "You walk down the
    # gangplank." quoted in a cargo_deliver row (Prying Times row 36).
    TRAVEL_LINE = re.compile(r"t\.player\.(cast|teleport|teleport_cast|climb)\b|"
                             r"t\.sail\.(board|helm|sails|sail_to|await_arrival|disembark)\b|::goto|::tele")
    TRAVEL_DETAIL = re.compile(r"\bteleport: [\d,]+ -> |\bcast \w*teleport|\bashore at\b|\blanded\b|"
                               r"\b(?:ferry|boat|ship|cart|carpet|charter|canoe|glider|balloon)\b", re.I)

    TRAVEL_STEP = re.compile(r"travel|ferry|boat|ship|sail|charter|carpet|glider|balloon|canoe|landed|arrived",
                             re.I)
    ANY_TILE = re.compile(r"\b(\d{4}),(\d{4,5}),[0-3]\b")
    TRAVEL_FAR_TILES = 30

    def _no_route_judged(self, walls):
        """Is the no-route reading on (NO_ROUTE_CHARGED, MapWalls.NO_ROUTE)?
        Every older switch restores the reading before its own seam, and
        this rule came after all of them, so turning any of them off turns it
        off too (the `*_off` fixtures of seams b63/b64 keep their meaning:
        Tail of Two Cats' goto-sphinx with CROSSINGS off reads DRIVEN, the
        hole, not a no-route charge)."""
        return bool(self.NO_ROUTE_CHARGED and walls.NO_ROUTE and walls.CROSSINGS and self.START_JUDGED and
                    self.POCKET_PRESS_SINCE_ARRIVAL and self.GOTO_BY_ACTION)

    def _travel_between(self, before_position, position, start):
        """Is a row strictly between rows[before_position] (where the hop
        left from, the tile `start`) and rows[position] (the goto) a travel
        row: TRAVEL_LINE on its source, TRAVEL_DETAIL in its own detail,
        TRAVEL_STEP in its name (`travelToNeitiznot`, `returnToRellekka.
        landed`), or a tile in its own detail more than TRAVEL_FAR_TILES
        from `start` (the player was somewhere else: The Fremennik Isles'
        ferry talk, whose landing check says `tile 2311,3782,0 want ...`)?
        A stamped hop has none between: its departure is the goto's own
        reading."""
        for row in self.rows[before_position + 1:position]:
            detail = re.sub(r" :: .*?(?= ## | -- |$)", "", row.get("detail") or "")
            if self.TRAVEL_DETAIL.search(detail) or self.TRAVEL_STEP.search(row["step"]):
                return True
            line = self.row_line(row["step"])
            if line is not None and self.TRAVEL_LINE.search(self.test.code_lines[line - 1]):
                return True
            for match in self.ANY_TILE.finditer(detail):
                x, z = int(match.group(1)), int(match.group(2))
                if max(abs(x - start[0]), abs(z - start[1])) > self.TRAVEL_FAR_TILES:
                    return True
        return False

    def gate_crossings(self):
        """{step name: [((row index, row step), reason)]}: a goto between two
        tiles of one level and map frame that no walk joins with every door
        shut, where every walk on foot goes through the SAME gate (MapWalls.
        only_way_gates: the closed-doors flood fails at margin 160 and the
        open-doors route crosses that gate at every margin of 30, 80, 160
        that has a route at all), with no PASS row pressing that gate (or
        the other leaf beside it) in the DOOR_OPEN_TICKS before. The b59
        sampler ruling (sampler-findings.md "Sample matthew-mbp-m4-b59"
        (a)): a gate that is the only way on foot between two regions is
        clicked on every crossing, however large the regions -- and a
        fenced yard is the same thing (Dwarf Cannon's railing yard closes at
        1,325 tiles, past enclosure_entries' 400). Charged like an entry
        (_charge_name). A hop enclosure_entries or enclosure_exits already
        charged is not charged twice.

        The departure: the goto's stamp, else the reading before it when
        the rows between kept the player on one side (_known_departure).
        Not judged: a hop to another level or map frame, a hop longer than
        GATE_MAX_TILES, a first goto with no stamp whose start is unknown (a
        setup placement with no landing, a row before it that moves the
        player). The run's start is judged since seam
        matthew-mbp-m4-b64-seam1: an unstamped first goto from the
        fixture's tile, and each setup placement as a row of its own
        (_start_rows). A blocking crossing loc (the Wilderness Ditch, the
        Shantay Pass) counts as a gate (MapWalls.only_way_gates). Locs
        content adds or removes at run time are not seen; a closed door the
        content keeps open is."""
        if getattr(self, "_gate_crossings", None) is not None:
            return self._gate_crossings
        self._gate_crossings = {}
        if not self.rows or not self.GATE_CROSSINGS:
            return self._gate_crossings
        track = self.player_track()
        walls = map_walls()
        charged = {key for found in (self.enclosure_entries(), self.enclosure_exits())
                   for items in found.values() for key, _ in items}
        for i in range(len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            if not self._real_goto(position):
                continue
            known = self._known_departure(track, i)
            if known is None:
                continue
            before, before_position, stamped = known
            if before[2] != point[2] or abs(before[1] - point[1]) > 3200 or abs(before[0] - point[0]) > 3200:
                continue  # another level or map frame: a climb, a cave, a teleport
            if before[:2] == point[:2] or \
                    max(abs(before[0] - point[0]), abs(before[1] - point[1])) > self.GATE_MAX_TILES:
                continue
            row = self.rows[position]
            if (row["index"], row["step"]) in charged:
                continue
            if self._no_route_judged(walls):
                kind, gates, margin = walls.hop_route(before[:2], point[:2], point[2])
            else:
                kind, gates, margin = "gates", walls.only_way_gates(before[:2], point[:2], point[2]), None
            if kind == "unreachable":
                if self._travel_between(before_position, position, before):
                    continue
                if any(walls.enclosure(x, z, level, self.ENCLOSURE_MAX_TILES) is not None
                       for x, z, level in (before, point)):
                    continue  # a room or pocket: enclosure_entries/exits and sealed_entries/exits judge it
                until = next((track[j][0] for j in range(i + 1, len(track)) if track[j][2]), len(self.rows))
                name = self._charge_name(position, until, None, "with no on-foot route")
                if name is None:
                    continue
                from_row = self.rows[before_position]
                if stamped:
                    from_row = self._departure_row(from_row)
                items = self._gate_crossings.setdefault(name, [])
                if any(key == (row["index"], row["step"]) for key, _ in items):
                    continue
                items.append(((row["index"], row["step"]),
                    "ledger row %s %r goes from %d,%d,%d (row %s %r) to %d,%d,%d: no on-foot route "
                    "(UNREACHABLE at margin %d): with every door open and every crossing loc enterable no walk "
                    "joins them inside their box widened by %s tiles (maps/m%d_%d.jl2 -> m%d_%d.jl2), and no "
                    "travel row (a teleport or cast, a sail that moves the hull or disembarks, a ferry, cart "
                    "or carpet, a climb) between: the goto stood in for the boat, teleport, climb or op loc "
                    "that gets there (gate_crossings)" % (
                        row["index"], row["step"], before[0], before[1], before[2], from_row["index"],
                        from_row["step"], point[0], point[1], point[2], margin,
                        "/".join(str(m) for m in walls.GATE_MARGINS + walls.WIDE_MARGINS),
                        before[0] >> 6, before[1] >> 6, point[0] >> 6, point[1] >> 6)))
                continue
            if kind != "gates" or not gates:
                continue
            if self._gate_pressed_before(position, walls.door_cluster(gates)):
                continue
            until = next((track[j][0] for j in range(i + 1, len(track)) if track[j][2]), len(self.rows))
            name = self._charge_name(position, until, None, "past " + gates[0][0])
            if name is None:
                continue
            from_row = self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            items = self._gate_crossings.setdefault(name, [])
            if any(key == (row["index"], row["step"]) for key, _ in items):
                continue
            named = ", ".join("%s at %d,%d,%d" % ((symbol,) + at) for symbol, at in gates[:6])
            # a blocking crossing loc (the Wilderness Ditch, the Shantay Pass) is crossed by its own
            # op, not opened: reach.py's NEEDS-OP
            crossing = [symbol for symbol, _ in gates if walls.is_crossing(symbol) and not walls.is_door(symbol)]
            items.append(((row["index"], row["step"]),
                "ledger row %s %r goes from %d,%d,%d (row %s %r) to %d,%d,%d: with every door shut no walk "
                "joins them (margin %d), and %s %s %s (%s at margin%s %s; maps/m%d_%d"
                ".jl2), with no press of it in the %d ticks before: it skipped an only-way gate "
                "(gate_crossings)" % (
                    row["index"], row["step"], before[0], before[1], before[2], from_row["index"],
                    from_row["step"], point[0], point[1], point[2], walls.GATE_MARGINS[-1],
                    "every walk on foot" if margin is None else "the fewest-door walk on foot",
                    "opens or crosses" if crossing and len(crossing) < len(gates) else
                    "crosses (by its own op)" if crossing else "opens", named,
                    "NEEDS-OP" if crossing else "NEEDS-DOOR", "s" if margin is None else "",
                    "/".join(str(m) for m in walls.GATE_MARGINS) if margin is None else
                    "%d only: no route at all at %s" % (
                        margin, "/".join(str(m) for m in walls.GATE_MARGINS +
                                         tuple(w for w in walls.WIDE_MARGINS if w < margin))),
                    gates[0][1][0] >> 6, gates[0][1][1] >> 6, self.DOOR_OPEN_TICKS)))
        return self._gate_crossings

    def solid_landings(self):
        """{step name: [((row index, row step), reason)]}: a goto that lands
        ON a solid tile -- one the map blocks (MapWalls.blocked: an
        object's footprint, a blocking ground decoration, a blocked floor
        flag) -- where no walk can end. Dwarf Cannon's goto-gotoCave onto
        the cave entrance mcannoncave (2622,3392) and goto-searchCrates onto
        mcannoncrateboy (2571,9850); The Dig Site's exam landings on a rock
        and a fence. Judged for every goto row with a landing, its first
        included (a landing needs no departure). Charged to a step of its
        own, "(goto onto <loc>)" (grade() adds it, CHEAT), naming the loc
        that blocks the tile."""
        if getattr(self, "_solid_landings", None) is not None:
            return self._solid_landings
        self._solid_landings = {}
        if not self.rows or not self.SOLID_LANDINGS:
            return self._solid_landings
        track = self.player_track()
        walls = map_walls()
        for i in range(len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            if not self._real_goto(position):
                continue  # a climb or crossing row named goTo...: its `at` is the loc's tile
            if not walls._ready(point[0], point[1]) or point not in walls.blocked:
                continue
            locs = walls.blockers.get(point, [])
            what = ", ".join("%s at %d,%d,%d" % ((symbol,) + at) for symbol, at in locs[:2]) or \
                "a tile the map's floor flags block"
            # Always a step of its own: the fault is the goto row's, not the
            # guide step's work (a landing on the staircase the next row
            # climbs did not skip the climb), and a guide step charged for it
            # would hide the rule its own row broke or kept.
            name = self.UNCHARGED + "onto %s)" % (locs[0][0] if locs else "a solid tile")
            row = self.rows[position]
            if row.get("placement"):
                name = "%s lands at %d,%d,%d onto %s" % (row["step"], point[0], point[1], point[2],
                                                         locs[0][0] if locs else "a solid tile")
            items = self._solid_landings.setdefault(name, [])
            if any(key == (row["index"], row["step"]) for key, _ in items):
                continue
            items.append(((row["index"], row["step"]),
                "ledger row %s %r lands at %d,%d,%d on a solid tile (%s: maps/m%d_%d.jl2): no walk ends "
                "there, so the goto put the player where no player stands (solid_landings)" % (
                    row["index"], row["step"], point[0], point[1], point[2], what,
                    point[0] >> 6, point[1] >> 6)))
        return self._solid_landings

    def guarded_rooms(self):
        """[(composite X, room step S, room zones Z, door step O, O's zones A,
        O's locs, word)]: a state zone Z of X that holds (within one tile) the
        WorldPoint of an obstacle step O which X shows in ANOTHER zone A --
        the room O's door opens on, and the guide's only way between A and Z.
        S is the step X shows while the player is in Z. Heroes' Quest's
        getThievesArmband: useKeyOnDoor (pete_sidedoor at 2781,3197) is shown
        inGarden and opens on secretRoom, where the guide shows killGrip
        ("kill him with magic/ranged" from inside it, HeroesQuest.java:411)."""
        if getattr(self, "_guarded_rooms", None) is not None:
            return self._guarded_rooms
        self._guarded_rooms = []
        resolve = self.guide.resolve
        for composite, entries in sorted(self.guide.branch_conds.items()):
            states = []
            for child, condition in entries:
                leaf = self.guide.steps.get(resolve(child))
                zones = self.guide.condition_zones(condition)
                if leaf is not None and not leaf.is_composite() and zones:
                    states.append((leaf, zones))
            for door, door_zones in states:
                if door.point is None:
                    continue
                obstacle = self._own_obstacle(door)
                if obstacle is None:
                    continue
                x, z, level = door.point
                door_boxes = [b for boxes in door_zones.values() for b in boxes]
                for room, room_zones in states:
                    if room is door:
                        continue
                    room_boxes = [b for boxes in room_zones.values() for b in boxes]
                    # the door's tile is in the room or on its edge
                    if not any(b[4] <= level <= b[5] and b[0] - 1 <= x <= b[1] + 1 and b[2] - 1 <= z <= b[3] + 1
                               for b in room_boxes):
                        continue
                    if any(self._in_box(b, (x, z, level)) for b in door_boxes):
                        continue  # the door stands in its own zone: not a room behind it
                    if any(max(0, a[0] - b[1], b[0] - a[1]) == 0 and max(0, a[2] - b[3], b[2] - a[3]) == 0 and
                           not (a[5] < b[4] or b[5] < a[4]) for a in room_boxes for b in door_boxes):
                        continue  # the room overlaps the door's side
                    self._guarded_rooms.append((composite, room, room_zones, door, door_zones) + obstacle)
        return self._guarded_rooms

    def room_exits(self):
        """{room step name: [((row index, row step), reason)]}: a goto that
        leaves a guarded room Z (guarded_rooms) for a tile within
        ROOM_EXIT_TILES on the same level -- outside Z, outside the door's
        side A and outside every zone of the route -- with no press of the
        door between, after which a row drives the step the guide shows only
        INSIDE Z. The test left the room through a wall and did the room's
        step from somewhere the guide never shows it. Heroes' Quest
        9bdd40c4d row 30: goto-grip from 2781,3197 (secretRoom) to 2774,3192,
        then attackGrip/killGrip melee Grip in his own room.

        Not judged: a hop further than ROOM_EXIT_TILES or to another level or
        frame (a player may teleport out of a room), a hop back to the door's
        side or into another zone of the route (route_hops' business), a hop
        with an unknown start, and a room whose step is not driven after the
        hop. A room the guide does not enter through a named door (no
        obstacle step points into it) is not known to be sealed: judging it
        needs the map's walls (coverage-and-gate.md, "A goto out of a sealed
        room")."""
        if getattr(self, "_room_exits", None) is not None:
            return self._room_exits
        self._room_exits = {}
        rooms = self.guarded_rooms()
        if not rooms or not self.rows:
            return self._room_exits
        track = self.player_track()
        for i in range(1, len(track)):
            position, point, is_goto = track[i]
            if not is_goto:
                continue
            before, before_position, between, stamped = self.hop_start(track, i)
            if before[2] != point[2] or max(abs(before[0] - point[0]), abs(before[1] - point[1])) > \
                    self.ROOM_EXIT_TILES:
                continue
            if any(self._row_moves(row) for row in between):
                continue
            later = next((track[j][0] for j in range(i + 1, len(track)) if track[j][2]), len(self.rows))
            after = [r for r in self.rows[position + 1:later] if r.get("verdict") == "PASS"]
            row, from_row = self.rows[position], self.rows[before_position]
            if stamped:
                from_row = self._departure_row(from_row)
            for composite, room, room_zones, door, door_zones, locs, word in rooms:
                inside = next((zone for zone, boxes in room_zones.items()
                               if any(self._in_box(b, before) for b in boxes)), None)
                if inside is None or any(self._in_box(b, point) for boxes in room_zones.values() for b in boxes):
                    continue
                if any(self._in_box(box, point) for _, box in self._region_of(composite)):
                    continue
                if self._pressed_between(between, locs, door.point):
                    continue
                symbols = sorted({s for _, s in room.targets})
                quoted = re.compile(r"[\"'](%s)[\"']" % "|".join(re.escape(s) for s in symbols)) if symbols else None
                drove = None
                for candidate in after:
                    if self.row_names_step(candidate["step"], room.name):
                        drove = candidate
                        break
                    line = self.row_line(candidate["step"])
                    if quoted is not None and line is not None and quoted.search(self.test.code_lines[line - 1]):
                        drove = candidate
                        break
                if drove is None:
                    continue
                items = self._room_exits.setdefault(room.name, [])
                if any(key == (row["index"], row["step"]) for key, _ in items):
                    continue
                condition = next((c for child, c in self.guide.branch_conds.get(composite, [])
                                  if self.guide.resolve(child) == room.name), "")
                items.append(((row["index"], row["step"]),
                    "ledger row %s %r leaves %s (from %d,%d,%d, row %s %r) for %d,%d,%d, in no zone of %s, "
                    "without pressing the %s %s names (%s) that is the room's only way out, and row %s %r "
                    "drives %s there: the guide shows it only inside the room (%s.addStep(%s, %s), guide "
                    "line %d)" % (
                        row["index"], row["step"], inside, before[0], before[1], before[2],
                        from_row["index"], from_row["step"], point[0], point[1], point[2], composite,
                        word, door.name, ",".join(locs), drove["index"], drove["step"], room.name, composite,
                        " ".join(condition.split())[:60], room.name, room.line or 0)))
        return self._room_exits

    def stale_success(self, leaf):
        """A PASS row named after an obstacle step whose own reading says the
        attempt FAILED: the newest server line of its last chat block
        (`standing at ... :: <newest> | <older>`; a multi-attempt detail's
        last ` :: ` block is its last attempt) is `...and fail, activating
        the trap!`. The row passed on a success line left over from an
        earlier try (Regicide's passTrap5-tile on trap 4's "...and
        succeed"); a crossing's PASS must come from THIS attempt's line."""
        if self.route_obstacle(leaf) is None:
            return None
        for row in self.pass_rows:
            if not self.row_names_step(row["step"], leaf.name):
                continue
            blocks = (row.get("detail") or "").split(" :: ")
            if len(blocks) < 2:
                continue
            newest = re.split(r" \| | ## | -- ", blocks[-1])[0].strip()
            if ATTEMPT_FAILED.search(newest):
                return ("ledger row %s %r PASSed, but the newest server line of its attempt is %r: a success "
                        "line from an earlier try is not this crossing's" % (
                            row["index"], row["step"], newest[:80]))
        return None

    def teleported_across(self, leaf, owner):
        """A promoted sub-step's gated loc (a barrier, door, gate...) that the
        test stood beside and then left by goto_tile for its parent's tile:
        the test was in the sub-step's state and teleported out of it
        (Nature Spirit: goto-talkToDrezel 3439,9895 under Paterdomus, then
        goto-enterSwamp 3444,3460 at the Mort Myre gate, never crossing the
        holy barrier leaveDrezel names). Exact frames here -- _near folds a
        dungeon onto the surface above it, which is the very crossing this
        looks for. A trapdoor, ladder or stair sub-step is judged the same
        way when the quest owns the climb (its trigger writes or reads a
        quest var); a plain maplink climb stays travel (seam31)."""
        parent = self.guide.steps.get(owner)
        locs = [s for k, s in leaf.targets if k == "loc"]
        if leaf.kind != "ObjectStep" or not locs or leaf.point is None or parent is None or \
                parent.point is None or not self.test.gotos:
            return None
        # the loc's own word first ("wall" for the barrier), then the text's
        gated = [w for w in GATE_WORDS if w in " ".join(locs).lower()] or \
            [w for w in GATE_WORDS if w in leaf.text.lower()]
        if not gated:
            return None
        # A trapdoor, ladder or stair is travel (QUEST_AUTHORING section 2:
        # a goto past a plain maplink climb is allowed) UNLESS the quest
        # owns the crossing: its trigger writes a quest var (Elemental
        # Workshop's stairs) or reads one to gate it (Mourning's End I's
        # basement trap door). seam30's patch let every travel sub-step
        # through here, so a teleport past a quest-gated trapdoor spelled
        # "trapdoor" read TRAVEL; seam31 closes that. Plain stairs stay
        # travel -- dropping this test outright turned eight committed
        # greens red on generic spiral stairs and ladders (arthur,
        # biohazard, blackarmgang, haunted, misc, misc_astrid, romeojuliet,
        # shadowstorm), each a goto the section 2 rule allows.
        if self.is_travel(leaf) and not self.writes_quest_var(locs) and not self.reads_quest_var(locs):
            return None
        if not any(symbol_triggers(s) for s in locs):
            return None  # nothing to cross in this pack

        def exact(point, goto):
            return max(abs(point[0] - goto[1]), abs(point[1] - goto[2]))

        def exact_near(point, goto):
            return exact(point, goto) <= NEAR_TILES

        # `before` stood on the sub-step's side (nearer it than the parent:
        # a run already AT the parent was never in that state, Between a
        # Rock's goto-Dondakan beside the cave wall), `after` landed on the
        # parent's side (nearer the parent than the sub-step).
        # And `after` must be the goto that leads to the PARENT: the lines
        # up to the next goto name the parent step or one of its own
        # targets (Biohazard's goto-releasePigeons beside the Mourner HQ
        # fence is stages before searchSarahsCupboard, whose sub-step that
        # fence is).
        gotos = sorted(self.test.gotos)
        # ...by any name of its family: the guide names the multinpc child
        # (mag_emelio_1op), the test clicks the shell that spawns (mag_emelio).
        own = sorted({name for _, sym in parent.targets if (_, sym) not in parent.promoted_targets
                      for name in family(sym)})
        lines = self.test.code_lines

        def leads_to_parent(index):
            start = gotos[index][0]
            end = gotos[index + 1][0] if index + 1 < len(gotos) else len(lines) + 1
            segment = "\n".join(lines[start - 1:end - 1])
            return re.search(r"\b%s\b" % re.escape(parent.name), segment) is not None or \
                any(re.search(r"\b%s\b" % re.escape(sym), segment) for sym in own)

        # A click on the sub-step's own loc between the two gotos is the
        # crossing done for real under another step's name (Shadow of the
        # Storm's enterRuinAfterBook clicks golem_insidestairs_top, the loc
        # enterRuinNoDark/ForRitual/ForDave name, just before goto-portal2):
        # the goto after it is travel on the far side, not a teleport past.
        loc_names = set()
        for symbol in locs:
            loc_names |= family(symbol)
        clicked = re.compile(r"[\"'](%s)[\"']" % "|".join(re.escape(n) for n in sorted(loc_names)))

        def crossed_between(index):
            start, end = gotos[index - 1][0], gotos[index][0]
            return any(clicked.search(line) for line in lines[start:end - 1])

        # A climb (ladder, stair, trapdoor inside one map frame) changes the
        # LEVEL, not the tile: Watchtower's towerladder/watchladderup stand
        # a few tiles under the wizard, so the distance rule below never
        # sees a goto past them. For a travel sub-step the sides are floors:
        # `before` on the ladder's floor beside it, `after` on the parent's
        # (another) floor beside the parent.
        climb = self.is_travel(leaf) and len(leaf.point) > 2 and len(parent.point) > 2 and \
            leaf.point[2] != parent.point[2]

        def climbed_by_goto(index):
            # ...and nothing between the two gotos clicked a loc: a floor is
            # also reached another way (Watchtower's first visit climbs the
            # qip_watchtower_trellis_base wall between goto-goUpTrellis and
            # goto-talkToWizard, which is not a teleport past towerladder).
            # `after` may be one floor of several (Watchtower: towerladder
            # 0 -> 1, then watchladderup 1 -> 2): it left the ladder's floor
            # toward the parent's, and the gotos from it on, with nothing
            # else between them, end at the parent.
            before, after = gotos[index - 1], gotos[index]
            low, high = sorted((leaf.point[2], parent.point[2]))
            if not (climb and exact_near(leaf.point, before) and before[3] == leaf.point[2] and
                    exact_near(parent.point, after) and after[3] != leaf.point[2] and
                    low <= after[3] <= high):
                return False
            if any("click_loc" in line for line in lines[before[0]:after[0] - 1]):
                return False
            last = index
            while last + 1 < len(gotos) and gotos[last + 1][3] != leaf.point[2] and \
                    not any("t.player." in line and "goto_tile" not in line
                            for line in lines[gotos[last][0]:gotos[last + 1][0] - 1]):
                last += 1
            return gotos[last][3] == parent.point[2] and leads_to_parent(last)

        for index in range(1, len(gotos)):
            before, after = gotos[index - 1], gotos[index]
            moved = (exact_near(leaf.point, before) and not exact_near(leaf.point, after) and
                     exact_near(parent.point, after) and
                     exact(leaf.point, before) < exact(parent.point, before) and
                     exact(parent.point, after) < exact(leaf.point, after))
            if ((moved and leads_to_parent(index)) or climbed_by_goto(index)) and \
                    not crossed_between(index):
                return ("goto_tile %d,%d,%d at line %d leaves the %s side (goto at line %d, %d,%d) for %s's "
                        "tile without crossing the %s the guide's sub-step names (%s)" % (
                            after[1], after[2], after[3], after[0], leaf.name, before[0], before[1], before[2],
                            owner, gated[0], ",".join(locs)))
        # The run's own departure tile, when the goto row stamped one
        # (`at <landing> from <departure>`). The goto before it can be far
        # from the sub-step: Meat and Greet (b55) lands goto-colosseum
        # outside, enters by the entrance, fights, then goto-emelio.end
        # leaves 1819,9485 in the lobby for Emelio, past the
        # colosseum_exit_lobby its sub-step leaveColosseumToReturnToEmelio
        # names. The stamp is where the player stood: on the sub-step's side.
        self.player_track()
        departures = {}
        for position, start in self._goto_from.items():
            line = self.row_line(self.rows[position]["step"])
            if line is None:
                continue
            span_last = next((last for first, last, _ in self.test.row_spans if first == line), line)
            for goto in gotos:
                if line <= goto[0] <= span_last:
                    departures[goto[0]] = start
        for index, after in enumerate(gotos):
            start = departures.get(after[0])
            if start is None or len(start) < 3:
                continue
            before = (after[0], start[0], start[1], start[2])
            moved = (exact_near(leaf.point, before) and not exact_near(leaf.point, after) and
                     exact_near(parent.point, after) and
                     exact(leaf.point, before) < exact(parent.point, before) and
                     exact(parent.point, after) < exact(leaf.point, after))
            if not moved or not leads_to_parent(index):
                continue
            first_line = gotos[index - 1][0] if index else 0
            if any(clicked.search(line) for line in lines[first_line:after[0] - 1]):
                continue
            return ("goto_tile %d,%d,%d at line %d leaves the %s side (departure %d,%d,%d stamped by its own "
                    "row) for %s's tile without crossing the %s the guide's sub-step names (%s)" % (
                        after[1], after[2], after[3], after[0], leaf.name, start[0], start[1], start[2],
                        owner, gated[0], ",".join(locs)))
        return None

    def _stage_narration(self, results):
        """A stage the port narrates is narrated for every step in it: once
        one step of stage S is a CONTENT_GAP because a mes() branch or a
        soft-skip writes past it, a goto over another step of S skips a leg
        the port does not have (Mourning's End II's cave door), not a cheat.
        An UNMATCHED step joins them only when most of its stage is gone
        (the Temple of Light's wall support among 90 narrated puzzle steps)."""
        narrated = {}
        for result in results:
            if result["class"] == "CONTENT_GAP" and result["stage"] is not None and \
                    re.match(r"(narrated|soft-skipped) at", result["reason"]):
                narrated.setdefault(result["stage"], re.sub(r"; shares [^)]*\)$", ")", result["reason"]))
        for result in results:
            where = narrated.get(result["stage"])
            stage_rows = [r for r in results if r["stage"] == result["stage"]]
            mostly_gone = sum(r["class"] == "CONTENT_GAP" for r in stage_rows) * 2 > len(stage_rows)
            if where and ((result["class"] == "CHEAT" and result["reason"].startswith(("goto_tile", "the journey")))
                          or (result["class"] == "UNMATCHED" and mostly_gone)):
                result["reason"] = "stage %s is %s (was: %s)" % (result["stage"], where, result["reason"])
                result["class"] = "CONTENT_GAP"

    def _alternatives(self, results):
        """Panels built in an if/else (Heroes' Quest: the Black Arm OR the
        Phoenix route) are alternatives: the branch the test drove most is
        the spec, the other is ALTERNATIVE."""
        groups = {}
        for result in results:
            if result["alt_group"]:
                var, index = result["alt_group"]
                groups.setdefault(var, {}).setdefault(index, []).append(result)
        for var, branches in groups.items():
            ranked = sorted(branches.items(), key=lambda b: -sum(r["class"] == "DRIVEN" for r in b[1]))
            taken = ranked[0][0]
            for index, rows in branches.items():
                if index == taken:
                    continue
                for result in rows:
                    result["class"] = "ALTERNATIVE"
                    result["reason"] = "the other branch of the guide's %s panel; the test took panel %d" % (var, taken)

    def equivalent_report(self):
        out = []
        for found in self.test.equivalents:
            if found["kind"] == "BRANCH-IN" and not self.follow_branch_in:
                continue
            ok, why = self.verify_equivalent(found)
            out.append({"line": found["line"], "kind": found["kind"], "step": found["step"],
                        "verified": ok, "evidence": why})
        return out

    def report(self):
        results = self.grade()
        marked_crossings = self.claim_marked_crossings()
        counts = {}
        for result in results:
            counts[result["class"]] = counts.get(result["class"], 0) + 1
        test_side = any(r["class"] in GAP_CLASSES_TEST for r in results)
        content_side = any(r["class"] == "CONTENT_GAP" for r in results)
        if test_side and content_side:
            verdict = "MIXED"
        elif test_side:
            verdict = "TEST_GAP"
        elif content_side:
            verdict = "CONTENT_GAP"
        else:
            verdict = "FULL"
        first_test = next((r for r in results if r["class"] in GAP_CLASSES_TEST), None)
        first_content = next((r for r in results if r["class"] == "CONTENT_GAP"), None)
        # Where the content drops the legs: one line per .rs2 site, most steps first.
        sites = {}
        for result in results:
            if result["class"] != "CONTENT_GAP":
                continue
            where = re.search(r"([\w/.-]+\.(?:rs2|constant):\d+)", result["reason"])
            key = where.group(1) if where else result["reason"].split(" -- ")[0][:80]
            site = sites.setdefault(key, {"site": key, "steps": [], "evidence": result["reason"]})
            site["steps"].append(result["step"])
        content_sites = sorted(sites.values(), key=lambda site: -len(site["steps"]))
        return {
            "test_id": self.test_id, "quest_dir": self.quest_dir,
            "guide": os.path.relpath(self.guide.path, QUEST_HELPER_ROOT),
            "ledger": os.path.relpath(self.ledger_path, REPO_ROOT) if self.ledger_path else None,
            "verdict": verdict, "counts": counts, "steps": results,
            "first_test_gap": first_test, "first_content_gap": first_content,
            "content_sites": content_sites,
            "guide_gap_markers": [{"line": l, "step": s, "reason": r} for l, s, r in self.test.guide_gaps],
            # Every BRANCH-IN / PARTNER / NOT-A-STEP / OBSOLETE / ANY-OF marker
            # and whether its evidence checked out: one that does not is a
            # false claim in the file, and gate_findings reports it.
            "equivalent_markers": self.equivalent_report(),
            # A reach-retry ::goto stand-on no guide step claimed is still a
            # leg nobody walked: gate_findings reports it.
            "unclaimed_stand_ons": [f for f in self.stand_ons if f["row"] not in self.claimed_stand_ons],
            # The other crossings of a loc crossed more than once, each
            # claimed by its own call's cited GUIDE-GAP marker naming the
            # guide step on that loc (claim_marked_crossings).
            "marked_crossings": marked_crossings,
            # A `stand_on_square = true` with no GUIDE-GAP marker beside it is
            # a finding whether or not this run's retry ever used it.
            "bare_stand_on_optins": [number for number, marker in self.test.stand_on_optins if marker is None],
        }


def grade(test_id, test_path=None, ledger_file=None):
    return Grader(test_id, test_path=test_path, ledger_file=ledger_file).report()


def has_guide(test_id):
    """Can this test be graded at all? False for a file with no QUEUE row or
    no Quest Helper guide (hans, the _conformance/_cheats harnesses)."""
    row = queue_row(test_id)
    if row is None or not row.get("helper_dir"):
        return False
    path = guide_path(row)
    return bool(path) and os.path.isfile(path)


def gate_findings(test_id):
    """RED findings for gate.py: a green run must read FULL, or have only
    CONTENT_GAP steps that the file itself declares (t.blocked/content_bug/
    GUIDE-GAP with .rs2 evidence). A BRANCH-IN / PARTNER / NOT-A-STEP /
    OBSOLETE / ANY-OF marker whose evidence does not check out is a finding
    of its own. [] means the coverage check passes."""
    report = grade(test_id)
    findings = []
    for found in report["equivalent_markers"]:
        if not found["verified"]:
            findings.append("line %d: `-- %s: %s` does not verify: %s" % (
                found["line"], found["kind"], found["step"], found["evidence"]))
    for result in report["steps"]:
        if result["class"] in GAP_CLASSES_TEST:
            findings.append("guide step %s (%s) is %s: %s" % (
                result["step"], result["text"][:60], result["class"], result["reason"]))
        elif result["class"] == "CONTENT_GAP" and not result["reason"].startswith(
                ("GUIDE-GAP marker", "t.blocked/content_bug")):
            findings.append("guide step %s (%s) is an undeclared CONTENT_GAP: %s -- add "
                            "`-- GUIDE-GAP: %s <reason citing the .rs2 line>`" % (
                                result["step"], result["text"][:60], result["reason"], result["step"]))
    for found in report["unclaimed_stand_ons"]:
        findings.append("ledger row %s %r: reach retry stood on %s with ::goto (%s) and no guide step "
                        "claims it -- walk a route to the loc or declare why none exists" % (
                            found["row"], found["step"], found["tile"], found["symbol"]))
    for number in report["bare_stand_on_optins"]:
        findings.append("line %d: `stand_on_square = true` with no `-- GUIDE-GAP: <step> <file.rs2:line or "
                        "m<x>_<z>.jm2>` marker within %d lines above it -- the opt-in to a ::goto onto a "
                        "loc's own square must say why no route ends anywhere that serves the loc" % (
                            number, STAND_ON_MARKER_SPAN))
    if findings:
        findings.insert(0, "helper_coverage verdict %s (python3 tools/quest_gate/helper_coverage.py %s)"
                        % (report["verdict"], test_id))
    return findings


# ------------------------------------------------------------------ output

def print_report(report):
    print("%s  guide=%s  ledger=%s" % (report["test_id"], report["guide"], report["ledger"]))
    width = max([len(r["step"]) for r in report["steps"]] + [4])
    print("  %-5s %-*s %-12s %s" % ("stage", width, "step", "class", "reason"))
    for result in report["steps"]:
        print("  %-5s %-*s %-12s %s" % (
            "" if result["stage"] is None else result["stage"], width, result["step"],
            result["class"], result["reason"]))
    counts = " ".join("%s=%d" % kv for kv in sorted(report["counts"].items()))
    print("VERDICT %s  (%d steps: %s)" % (report["verdict"], len(report["steps"]), counts))
    for key in ("first_test_gap", "first_content_gap"):
        if report[key]:
            print("  %s: %s -- %s" % (key, report[key]["step"], report[key]["reason"]))
    for found in report["equivalent_markers"]:
        # A verified marker is printed too: on a step a real row also drives
        # (Spirits of the Elid's telegrabKey, whose `telegrabKey.cast` row
        # grades it DRIVEN before the ANY-OF is consulted) nothing else in
        # the report would say the marker was checked at all.
        print("  %s %s marker at line %d (%s): %s" % (
            "verified" if found["verified"] else "UNVERIFIED",
            found["kind"], found["line"], found["step"], found["evidence"]))
    for number in report["bare_stand_on_optins"]:
        print("  bare stand_on_square opt-in at line %d (no GUIDE-GAP marker within %d lines above)" % (
            number, STAND_ON_MARKER_SPAN))
    for found in report["unclaimed_stand_ons"]:
        print("  unclaimed stand-on: ledger row %s %r stood on %s with ::goto (%s)" % (
            found["row"], found["step"], found["tile"], found["symbol"]))
    for found in report.get("marked_crossings", []):
        print("  declared crossing: ledger row %s %r stood on %s with ::goto (%s) -- GUIDE-GAP marker line %d "
              "(%s, cites %s) above stand_on_square opt-in at line %d" % (
                  found["row"], found["step"], found["tile"], found["symbol"], found["marker_line"],
                  found["marker_step"], found["cite"], found["optin_line"]))
    for site in report["content_sites"]:
        print("  content gap at %s (%d step%s, first %s): %s" % (
            site["site"], len(site["steps"]), "" if len(site["steps"]) == 1 else "s",
            site["steps"][0], site["evidence"]))


def audit_rows():
    out = {}
    with open(AUDIT_PATH, "r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            out[row["test_id"]] = row["verdict"].replace(" ", "_")
    return out


def green_ids(tier=None):
    out = []
    with open(QUEUE_PATH, "r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            if row["status"] == "green" and (tier is None or row["tier"] == str(tier)):
                out.append(row["test_id"])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("test_ids", nargs="*")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    parser.add_argument("--calibrate", action="store_true",
                        help="grade every row of helper_coverage.tsv and print the agreement")
    parser.add_argument("--all-green", action="store_true", help="grade every green queue row")
    parser.add_argument("--tier", type=int, default=None)
    parser.add_argument("--lua", default=None,
                        help="grade this copy of the ONE named test's Lua (a proof copy under build/) "
                             "against its QUEUE row, guide and ledger")
    parser.add_argument("--ledger", default=None,
                        help="grade the ONE named test against this ledger.tsv (another run's, e.g. a "
                             "reverted green's published ledger) instead of build/quest_gate/<id>/")
    args = parser.parse_args()

    if args.calibrate:
        audit = audit_rows()
        agree = 0
        lines = []
        for test_id, expected in audit.items():
            report = grade(test_id)
            same = report["verdict"] == expected
            agree += same
            lines.append("%-22s audit=%-12s tool=%-12s %s" % (
                test_id, expected, report["verdict"], "ok" if same else "DISAGREE"))
        print("\n".join(lines))
        print("agreement %d/%d" % (agree, len(audit)))
        return 0

    ids = list(args.test_ids)
    if args.all_green:
        ids.extend(green_ids(args.tier))
    if not ids:
        parser.error("name a test_id (or --all-green / --calibrate)")
    if args.lua and len(ids) != 1:
        parser.error("--lua grades one named test_id")
    if args.ledger and len(ids) != 1:
        parser.error("--ledger grades one named test_id")
    reports = [grade(test_id, test_path=args.lua, ledger_file=args.ledger) for test_id in ids]
    if args.json:
        print(json.dumps(reports if len(reports) > 1 else reports[0], indent=1))
    else:
        for report in reports:
            print_report(report)
            print("")
    return 0


if __name__ == "__main__":
    sys.exit(main())
