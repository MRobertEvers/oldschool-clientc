#!/usr/bin/env python3
"""Grade a quest test against its Quest Helper guide -- the guide is the spec.

    python3 tools/quest_gate/helper_coverage.py <test_id> [--json]
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
`x.panelDetails()` spliced in), sub-steps folded into their parent. A panel
entry that is a ConditionalStep stands for its leaves, each its own step,
except leaves that differ from a sibling in one name word (compareAnna /
compareDavid: one step, done any way) and *Fallback leaves (optional).
Panels built twice in an if/else (Heroes' Quest's two gang routes) are
alternatives: the branch the test drove most is the spec.

CLASSES (first rule that fires wins, in this order).
  CHEAT        a quest debugproc named after the step (::mortton_repairtemple
               for repairTemple) -- before anything else.
  CONTENT_GAP  the content's own soft-skip comment names the step as
               collapsed (mend1_sheep.rs2's `getToads`), even when the test
               drove the stand-in.
  DRIVEN       a PASS row named after the step (never a goto-/quest.* row),
               or an action (talk_to/click_loc/use_on/...) or action row
               naming one of its npc/loc/obj symbols -- rev 239's name, a
               multinpc parent, or its display name; a "use X on Y" step
               needs a use of X on Y, a pick-up step may be done by another
               route (buy-rum), and a door clicked once is one step, not two.
  CONTENT_GAP  a t.blocked()/content_bug line or a `-- GUIDE-GAP:` marker
               names the step (or a ConditionalStep above it); the marker
               counts only when its reason cites a real `<file>.rs2:<line>`.
  CHEAT        a ::give / debugproc inv_add of an item the step OBTAINS
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

VERDICT. FULL (every step DRIVEN/TRAVEL/BRING_ALONG/ALTERNATIVE), CONTENT_GAP (the only
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
import csv
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
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
NEUTRAL_CLASSES = ("DRIVEN", "TRAVEL", "BRING_ALONG", "ALTERNATIVE")

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
        self.stage = None
        self.dialog = []
        self.panel = ""
        self.alt_group = None

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
        for match in re.finditer(r"\b(\w+)\s*\.\s*addStep\s*\(", code):
            open_index = match.end() - 1
            args = split_top(code[open_index + 1:matching_close(code, open_index)])
            if match.group(1) in self.steps and args:
                child = re.match(r"\s*(\w+)", args[-1])
                if child:
                    self.steps[match.group(1)].children.append(child.group(1))
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
        self._parse_panels()

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
        if step.kind in ("ItemStep", "DigStep"):
            for base in step.req_vars:
                step.targets.extend(("obj", s) for s in self.items[base]["ids"])

    def _parse_panels(self):
        code = self.code
        list_vars = {}
        for match in re.finditer(r"\b(\w+)\s*=\s*(?:new\s+ArrayList<>\s*\()?\s*(?:Arrays\.asList|List\.of|QuestUtil\.toArrayList|Collections\.singletonList)\s*\(", code):
            open_index = match.end() - 1
            body = code[open_index + 1:matching_close(code, open_index)]
            idents = [i for i in re.findall(r"\b([a-z]\w*)\b", body) if i in self.steps]
            if idents:
                list_vars[match.group(1)] = idents
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
            idents = []
            for ident in re.findall(r"\b([a-z]\w*)\b", args[1]):
                if ident in self.steps:
                    idents.append(ident)
                elif ident in list_vars:
                    idents.extend(list_vars[ident])
            # `thirdPanel = new PanelDetails(...)` twice, in an if/else on
            # the player's gang: the branches are alternatives.
            assigned = re.search(r"\b(\w+)\s*=\s*$", code[max(0, match.start() - 80):match.start()])
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
        steps = []
        last_stage = None
        for name in order:
            step = self.steps[name]
            stage = stage_of.get(name)
            if stage is None and step.is_composite():
                stage = min((stage_of[l] for l in self.leaves(name) if l in stage_of), default=None)
            for sub in step.substeps:
                if sub in self.steps:
                    step.targets.extend(t for t in self.steps[sub].targets if t not in step.targets)
                    if stage is None:
                        stage = stage_of.get(sub)
            step.stage = stage if stage is not None else last_stage
            last_stage = step.stage
            steps.append(step)
        return steps


# ------------------------------------------------------------------ content

_CONTENT_INDEX = None
DISPLAY = {}      # (kind, symbol) -> display name, lowercased
BY_DISPLAY = {}   # (kind, display name) -> {symbol}
PARENTS = {}      # multinpc/multiloc child -> {parent}
SCRIPTS = {}      # (trigger kind, subject) -> (relpath, line): labels, procs, triggers
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


def same_thing(kind, guide_symbol, test_string, loose=True):
    """Does a test's string name the guide's symbol? The guide's gameval names
    are a later cache's, so rev 239 may call the same npc by a shorter name
    (holgartlandnotravel/holgartland, kennith_platform/kennith) or by its
    multinpc parent."""
    if not test_string or not re.match(r"^[a-z0-9_]+$", test_string):
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
    if kind in ("npc", "obj"):
        display = DISPLAY.get((kind, guide_symbol))
        same_name = BY_DISPLAY.get((kind, display), ())
        if display and len(display) >= 4 and test_string in same_name and len(same_name) <= 4:
            return True
    return False


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
    _CONTENT_INDEX = (triggers, categories, debugprocs)
    return _CONTENT_INDEX


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
    def __init__(self, test_id, path):
        self.test_id = test_id
        self.path = path
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            self.raw = handle.read()
        self.code = strip_lua_comments(self.raw)
        self.raw_lines = self.raw.split("\n")
        self.code_lines = self.code.split("\n")
        self.strings = {}      # string literal -> first line
        self.cheats = []       # (line, text)
        self.gotos = []        # (line, x, y, z)
        self.blocked_text = []  # (line, text)
        self.guide_gaps = []   # (line, step, reason)
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
        for number, line in enumerate(self.raw_lines, 1):
            if "content_bug" in line:
                self.blocked_text.append((number, line))
            match = GUIDE_GAP_RE.match(line)
            if match:
                self.guide_gaps.append((number, match.group(1), match.group(2).strip()))
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
        # "::goto x y z" cheats are gotos too.
        for number, text in self.cheats:
            match = re.match(r"::(?:goto|tele)\s+(\d+)[\s,]+(\d+)[\s,]+(\d+)", text)
            if match:
                self.gotos.append((number,) + tuple(int(v) for v in match.groups()))


GUIDE_GAP_RE = re.compile(r"^\s*--\s*GUIDE-GAP:\s*(\S+)\s+(.*)$")


def rs2_citation(reason):
    """The first `<file>.rs2:<line>` in `reason` that names a real line of a
    real script under server/scripts (by path, or by unique basename), as the
    text cited it; None when there is none. A GUIDE-GAP marker is evidence
    only when its citation resolves -- `foo.rs2:12` for a file that does not
    exist is not evidence of anything."""
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


def ledger_path(test_id, quest_dir):
    candidates = [
        os.path.join(REPO_ROOT, "build", "quest_gate", test_id, "ledger.tsv"),
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


class Grader:
    def __init__(self, test_id):
        self.test_id = test_id
        self.row = queue_row(test_id)
        assert self.row, "test_id %r is not in %s" % (test_id, QUEUE_PATH)
        self.quest_dir = self.row["quest_dir"]
        path = guide_path(self.row)
        assert path and os.path.isfile(path), "no Quest Helper guide for %r (%s)" % (test_id, path)
        self.guide = Guide(path)
        self.test = Test(test_id, os.path.join(REPO_ROOT, "test", "quests", test_id + ".lua"))
        self.ledger_path = ledger_path(test_id, self.quest_dir)
        rows, _ = ledger.read(self.ledger_path) if self.ledger_path else (None, None)
        self.rows = rows or []
        self.pass_rows = [r for r in self.rows if r["verdict"] == "PASS"]
        # A goto row is travel, whatever step it is named after
        # ("goto-talkToElena" walks TO the step; it does not do it).
        self.action_rows = [r for r in self.pass_rows
                            if not re.match(r"^(goto|walk|travel|tele|quest\.|setup|reset)", r["step"], re.I)
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

    def _cheat_effects(self):
        """[{line, text, kind: give|var|teleport, items, vars, words}] -- the cheats
        that do quest work. Resets, stat/level setup and bring-along gives are
        dropped here."""
        _, _, debugprocs = content_index()
        bring = self.bring_alongs()
        effects = []
        for number, text in self.test.cheats:
            parts = text[2:].split()
            if not parts:
                continue
            command = parts[0]
            args = parts[1:]
            effect = {"line": number, "text": text, "items": [], "vars": [], "teleport": False}
            if command == "give" and args:
                if args[0] in bring:
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
            if command in debugprocs:
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

    def driven(self, step):
        name = norm(step.name)
        for row in self.action_rows:
            row_name = norm(row["step"])
            segments = [norm(part) for part in re.split(r"[.\-:/ ]", row["step"])]
            if row_name == name or row_name.startswith(name) or \
                    (len(name) > 6 and any(seg.startswith(name) for seg in segments)) or \
                    (len(row_name) >= 6 and name.startswith(row_name) and
                     (re.search(r"\d", name[len(row_name):]) or len(row_name) / len(name) >= 0.75)):
                return "ledger row %s %r PASS" % (row["index"], row["step"])
        if not self.pass_rows:
            return None
        use_items = [sym for var in step.req_vars for sym in self.guide.items[var]["ids"]]
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
                    return "an action at line %d uses %r on the target" % (line, item)
            if all(any(t in all_items for t in named) for _, named in on_target):
                return None  # no use at all, or only uses of other items
        for kind, symbol in step.targets:
            if kind == "loc" and any(w in symbol for w in GATE_WORDS) and \
                    self.loc_uses.get(symbol, 0) >= self.loc_evidence(symbol):
                continue  # every pass through this door already did an earlier step
            for text, line in self.test.action_strings.items():
                if same_thing(kind, symbol, text):
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
                            self.consumed.add((line, symbol))
                            return "an action at line %d uses %r, the step's item" % (line, text)
            for text, line in self.test.action_strings.items():
                if re.match(r"^[a-z][a-z0-9_]+$", text):
                    own = set(words(text.replace("_", " ")))
                    distinct = self.weak_filter(own)
                    strong = len(distinct) >= 2 or any(len(w) >= 5 for w in distinct)
                    if own and own <= text_words and strong:
                        return "an action at line %d names %r, which the step's text names" % (line, text)
        if re.search(r"\b(buy|purchase|pick up|pickup|take|grab)\b", step.text, re.I) or \
                OBTAIN_VERB.match((camel_words(step.name) or [""])[0]):
            # "Buy a Karamjan rum from Zembo": any buy/pickup of the rum is
            # the step done by another route.
            wanted = set(words(step.text))
            for line, named in self.test.action_lines:
                source = self.test.code_lines[line - 1]
                if not re.search(r"shop\.buy|click_obj|take_obj|pickup", source):
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
                    return "ledger row %s %r names %s" % (row["index"], row["step"], hit[0])
        for row in self.action_rows:
            tokens = set(re.findall(r"[a-z][a-z0-9_]+", (row["detail"] + " " + row["step"]).lower()))
            for kind, symbol in step.targets:
                if kind == "obj" and not ROW_ACTION.search(row["detail"] + " " + row["step"]):
                    continue  # an inventory read names items too
                hit = next((t for t in tokens if same_thing(kind, symbol, t, loose=False)), None)
                if hit:
                    return "ledger row %s %r names %s" % (row["index"], row["step"], hit)
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
        for number, text in self.test.blocked_text:
            low = text.lower()
            if re.search(r"\b%s\b" % re.escape(name), low) or any(
                    re.search(r"\b%s\b" % re.escape(s), low) for s in symbols):
                return "t.blocked/content_bug line %d names it" % number
        return None

    def cheat(self, step, driven_symbols):
        own = self.step_words(step)
        first = (camel_words(step.name) or [""])[0]
        obtains = bool(OBTAIN_VERB.match(step.text)) or bool(OBTAIN_VERB.match(first))
        for effect in self.effects:
            hit = set()
            # An item cheat stands in only for a step that OBTAINS the item;
            # a step that merely uses it is still the test's to do.
            if effect["items"] and obtains:
                item_words = set()
                for item in effect["items"]:
                    item_words |= self.item_words(item)
                if effect.get("obtain_only"):
                    if item_words and item_words <= set(words(step.text)):
                        hit |= item_words
                else:
                    hit |= own & item_words
            var_words = set()
            for var in effect["vars"]:
                var_words |= set(words(var.replace("_", " ")))
            var_words -= self.quest_words
            hit |= own & var_words
            target_items = [s for k, s in step.targets if k == "obj"]
            if set(effect["items"]) & set(target_items) or hit:
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
            locs = [s for k, s in step.targets if k == "loc"]
            if any(s in driven_symbols for s in locs):
                return None
            if locs and not any(self.relevant_triggers(s) for s in locs):
                return None  # the content has no such leg: a CONTENT_GAP, not a cheat
            if self.is_travel(step) and not self.writes_quest_var(locs):
                return None
            near = [g for g in self.test.gotos if _near(step.point, g)]
            if near:
                goto = near[0]
                return "goto_tile %d,%d,%d at line %d lands past the %s the guide names (%s)" % (
                    goto[1], goto[2], goto[3], goto[0], gated[0], ",".join(locs) or step.text[:40])
        return None

    def named_cheat(self, step):
        """A quest debugproc named after the step (::mortton_repairtemple for
        repairTemple) did it, whatever else the test touched."""
        name = norm(step.name)
        if len(name) < 6:
            return None
        _, _, debugprocs = content_index()
        for line, text in self.test.cheats:
            parts = text[2:].split()
            if parts and parts[0] in debugprocs and name in norm(parts[0]):
                rel, where, _ = debugprocs[parts[0]]
                return "%s at line %d (debugproc %s:%d) is named after the step" % (text, line, rel, where)
        return None

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
            if len(parts) > 1 and parts[0] == "give":
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
            precursor = (req_words & item) if not step.targets or step.kind == "NpcStep" else set()
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
            return "CHEAT", strong
        # A real driving row wins over anything the content says about the
        # step; then the file's own declaration (GUIDE-GAP / t.blocked), which
        # gate_findings accepts; only then the content's collapsed-by-name
        # prose, which the file can neither drive past nor declare away if it
        # came first (mourningsendparti's talkToIslwyn, pickUpRottenApple).
        if driven_reason:
            return "DRIVEN", driven_reason
        declared = self.marker(step)
        if declared:
            return "CONTENT_GAP", declared
        collapsed = self.collapsed_by_name(step)
        if collapsed:
            return "CONTENT_GAP", collapsed
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
                return klass, reason
        if self.is_travel(step):
            return "TRAVEL", "travel, merged into the step it leads to"
        return "UNMATCHED", "no row, cheat or content evidence found"

    def grade(self):
        steps = self.guide.ladder()
        # every leaf a step stands for: itself, or a panel ConditionalStep's leaves
        # A panel ConditionalStep stands for its leaves, each its own guide
        # step -- except leaves that differ from a sibling in ONE name word
        # (compareAnna/compareDavid: whichever suspect it is), which are one
        # step done any way, and *Fallback leaves, which are optional.
        expanded = []
        for step in steps:
            if not step.is_composite():
                expanded.append([step])
                continue
            leaves = [self.guide.steps[l] for l in self.guide.leaves(step.name)] or [step]
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
        driven = {}
        driven_symbols = set()
        self.loc_uses = {}
        for step in steps:
            for leaf in members[step.name]:
                reason = self.driven(leaf)
                driven[leaf.name] = reason
                if reason:
                    driven_symbols.update(s for _, s in leaf.targets)
                    for kind, symbol in leaf.targets:
                        if kind == "loc":
                            self.loc_uses[symbol] = self.loc_uses.get(symbol, 0) + 1
        results = []
        for step in steps:
            graded = [(leaf,) + self.classify(leaf, driven[leaf.name], driven_symbols)
                      for leaf in members[step.name]]
            if step.name.endswith("Fallback") and graded[0][1] != "DRIVEN":
                graded = [(step, "ALTERNATIVE", "a fallback the guide offers when the main way fails")]
            if len(graded) == 1:
                klass, reason = graded[0][1], graded[0][2]
            else:
                # One guide step, several ways to do it: any leaf done is done.
                order = ("DRIVEN", "BRING_ALONG", "CHEAT", "CONTENT_GAP", "UNMATCHED", "TRAVEL")
                best = min(graded, key=lambda g: order.index(g[1]))
                klass, reason = best[1], "%s: %s" % (best[0].name, best[2])
            results.append({
                "step": step.name, "type": step.kind, "stage": step.stage,
                "text": step.text or (members[step.name][0].text if members[step.name] else ""),
                "targets": ["%s:%s" % t for t in step.targets],
                "point": list(step.point) if step.point else None,
                "class": klass, "reason": reason, "guide_line": step.line,
                "alt_group": step.alt_group,
            })
        self._stage_narration(results)
        self._alternatives(results)
        return results

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

    def report(self):
        results = self.grade()
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
        }


def grade(test_id):
    return Grader(test_id).report()


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
    GUIDE-GAP with .rs2 evidence). [] means the coverage check passes."""
    report = grade(test_id)
    findings = []
    for result in report["steps"]:
        if result["class"] in GAP_CLASSES_TEST:
            findings.append("guide step %s (%s) is %s: %s" % (
                result["step"], result["text"][:60], result["class"], result["reason"]))
        elif result["class"] == "CONTENT_GAP" and not result["reason"].startswith(
                ("GUIDE-GAP marker", "t.blocked/content_bug")):
            findings.append("guide step %s (%s) is an undeclared CONTENT_GAP: %s -- add "
                            "`-- GUIDE-GAP: %s <reason citing the .rs2 line>`" % (
                                result["step"], result["text"][:60], result["reason"], result["step"]))
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
    reports = [grade(test_id) for test_id in ids]
    if args.json:
        print(json.dumps(reports if len(reports) > 1 else reports[0], indent=1))
    else:
        for report in reports:
            print_report(report)
            print("")
    return 0


if __name__ == "__main__":
    sys.exit(main())
