#!/usr/bin/env python3
"""Static lint for one test/quests/<quest>.lua file -- the checks that do not
need a client run because the defect is visible in the text itself.

None of this replaces run.py + gate.py (the gate is behaviour, CLAUDE.md):
a file can lint clean and still fail its quest, and a file this refuses can
still, in principle, drive a passing run. What it catches is the class of
defect that a real run would not surface cleanly, or would surface as a
confusing failure three steps downstream of where the actual mistake is:

  * a numeric interface/component id, or client op string, where a content
    symbol belongs (docs/ARCHITECT.md S2: "No numeric interface or component
    ids and no client op strings" -- never in driver C, driver Lua or a
    test). `talk_to(123)` is the example the spec names: it happens to run
    (the driver's own symbol trampolines accept whatever Lua hands them),
    and then fails three steps later against whatever npc id 123 actually
    is, reading like a broken verb instead of a typo'd test.
  * `::complete <the quest's own row>` inside `setup` -- a setup cheat that
    completes the very quest under test before `run(t)` gets to play it is
    not "state the world", it is "skip the test and still call it green".
  * a hardcoded `"PASS"` literal as `t.step`'s own verdict argument -- as
    opposed to the `cond and "PASS" or "FAIL"` idiom every real row in this
    project uses -- which is a verdict nothing computed, catching a pinned
    "PASS" masking a bug this exact syntax would exist to catch.
  * a BOOLEAN (or any other non-verdict) where `t.step`'s verdict argument
    belongs -- `t.step(name, result == "ok", detail)`, which is the natural
    thing to write because `t.check`'s second argument really is a
    condition. It used to end the entire run: api.drive.ledger reads that
    column with luaL_checkstring and this sandbox has no pcall, so Between
    a Rock threw away 93 PASS rows at its 94th on 2026-09-21. core.lua now
    names the bad argument on the row and carries on, so the shape is
    survivable -- but it is still a mistyped call, and this is where it is
    refused before a run is ever spent on it.
  * a `-- CHECK` marker: new_quest.py's own guess-markers (op numbers, chat
    rows, routes an author has not confirmed against a live run yet). A
    generated file legitimately carries these until a human clears them, so
    `--allow-check` turns this one rule off without touching the rest.
  * a duplicate literal `t.exec` step name written twice in straight-line
    source (the runtime's own `-2`/`-3` suffixing, core.lua's
    unique_step_name, exists for a name that legitimately recurs at runtime,
    e.g. inside a loop where the name itself is an expression -- this rule
    only ever sees two IDENTICAL STRING LITERALS, which a loop never writes
    twice, so it is a copy-paste catch, not a conflict with that mechanism).
  * a content symbol that names nothing in any `all.*.compack` -- a typo'd
    npc/obj/loc/varp/varbit name that would otherwise only surface as
    `not_found` deep into a real client run.
  * a malformed `-- GUIDE-GAP: <step> <reason>` marker. The marker is how a
    file declares a Quest Helper guide step the CONTENT does not implement
    (the guide is the spec -- docs/QUEST_AUTHORING.md): <step> is the
    guide's own step variable (`doAllPuzzles`, `getToads`), and <reason>
    must cite the .rs2 line that writes past the leg, as
    `<file>.rs2:<line>` naming a real line of a real script. A marker with
    no citation, or one that does not resolve, is refused here, and
    tools/quest_gate/helper_coverage.py would not honour it either -- it
    would leave the step UNMATCHED and gate.py would call the run RED. When
    the file has a QUEUE row and a guide, <step> must also be a step of that
    guide's ladder.

The test's own controls sit on the ROOT of `t` -- `t.exec`, `t.step`,
`t.check`, `t.cheat` -- so every regex below spells one prefix, not two. The
wrapper verb was once named after the Lua reserved word `do` and had to be
written as a string index at every call site; `exec` is an ordinary Name and
that shape is gone from this tree.

Symbol coverage is deliberately a named, closed list of call sites whose
first argument (or, for `player.by_symbol`, second) IS a content symbol by
that verb's own contract (docs/ARCHITECT.md's per-owner verb list) -- not a
blanket "every string literal must be a known symbol", which would flag
skill names, chat text, row labels and shot names, none of which are
content symbols at all. A quest file that never calls any of these verbs
lints clean by construction; that is correct, not a gap, because a rule
with no target to check has nothing to be wrong about.

Usage:
  tools/quest_gate/lint_quest.py <file> [<file> ...] [--allow-check]
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
CONFIGS_DIR = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "configs")

PACK_NPC = "npc"
PACK_OBJ = "obj"
PACK_LOC = "loc"
PACK_VARP = "varp"  # varp and varbit share one namespace here: a quest's
                     # `var.*` calls are already varp-or-varbit-transparent
                     # (state.lua), so a symbol lint that split them would
                     # have to duplicate that same transparency or produce
                     # false positives on every varbit-backed progress var.

# Verb -> which pack its symbol argument is drawn from. Only verbs whose own
# banner documents a content-symbol argument are listed (see the module
# docstring); npc.by_name is deliberately absent -- it matches a NAME
# substring, not a symbol (ui.lua).
CALL_PACKS = {
    "t.player.talk_to": PACK_NPC,
    "t.npc.by_symbol": PACK_NPC,
    "t.npc.nearest": PACK_NPC,
    "t.npc.await_present": PACK_NPC,
    "t.npc.await_gone": PACK_NPC,
    "t.player.click_loc": PACK_LOC,
    "t.player.click_obj": PACK_OBJ,
    "t.player.equip": PACK_OBJ,
    "t.player.drop": PACK_OBJ,
    "t.player.inv_op": PACK_OBJ,
    "t.player.use_on": PACK_OBJ,
    "t.inv.count": PACK_OBJ,
    "t.inv.has": PACK_OBJ,
    "t.inv.expect_has": PACK_OBJ,
    "t.inv.expect_absent": PACK_OBJ,
    "t.inv.await": PACK_OBJ,
    "t.var.varp": PACK_VARP,
    "t.var.varbit": PACK_VARP,
    "t.var.server": PACK_VARP,
    "t.var.await": PACK_VARP,
    "t.var.await_server": PACK_VARP,
    "t.var.expect": PACK_VARP,
}

# Matches a known call name immediately followed by either `(` (a direct
# call, `t.player.talk_to("cook")`) or `,` (the same name passed BY VALUE as
# t.exec's verb argument, `t.exec("name", t.player.talk_to, "cook")` -- in
# both shapes the very next token is the symbol argument this lints).
_CALLNAMES = sorted(CALL_PACKS, key=len, reverse=True)
SYMBOL_CALL_RE = re.compile(
    r'\b(' + "|".join(re.escape(name) for name in _CALLNAMES) + r')'
    r'\s*[,(]\s*([^,()]*?)\s*(?=[,)])'
)
BY_SYMBOL_RE = re.compile(
    r't\.player\.by_symbol\s*[,(]\s*"(npc|loc|obj)"\s*,\s*([^,()]*?)\s*(?=[,)])'
)

T_EXEC_OPEN_RE = re.compile(r'(?<![\w.])t\s*\.\s*exec\s*\(')
T_STEP_OPEN_RE = re.compile(r'(?<![\w.])t\s*\.\s*step\s*\(')
QUEST_BIND_OPEN_RE = re.compile(r't\.quest\.bind\s*([{(])')
SETUP_OPEN_RE = re.compile(r'\bsetup\s*=\s*(\{)')
CHECK_MARKER_RE = re.compile(r'--\s*CHECK\b')
COMPLETE_CHEAT_RE = re.compile(r'^::complete\s+(\S+)')
STRING_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

_OPEN_CHARS = set("([{")
_CLOSE_CHARS = set(")]}")


def _line_of(text, index):
    return text.count("\n", 0, index) + 1


def _extract_balanced(text, open_idx):
    """(inner_text, index_just_past_the_matching_close) for the bracket that
    starts at text[open_idx] -- any of `(`, `{`, `[`, matched against ITS
    OWN kind by depth alone (a Lua table argument nested inside a call, or a
    call nested inside a table, is common in this project's own quest files
    and must not desync the count)."""
    assert text[open_idx] in _OPEN_CHARS
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
        elif ch in _OPEN_CHARS:
            depth += 1
        elif ch in _CLOSE_CHARS:
            depth -= 1
        i += 1
    return text[open_idx + 1:i - 1], i


def _split_top_level(args_text):
    """Top-level comma split, blind to commas inside nested brackets or
    string literals -- `t.step("x", (a and "PASS" or "FAIL"), y)` must
    stay three arguments, not five."""
    parts = []
    depth = 0
    in_string = None
    current = []
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
        elif ch in _OPEN_CHARS:
            depth += 1
            current.append(ch)
        elif ch in _CLOSE_CHARS:
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
    return parts


def _literal_kind(arg_text):
    """("string", value) / ("number", value) / (None, None) -- the last for
    any Lua expression more complex than a bare literal (a variable, a
    concatenation, a function call): those are not this lint's business,
    because a static reader cannot know what they evaluate to."""
    arg_text = arg_text.strip()
    match = re.match(r'^"((?:[^"\\]|\\.)*)"$', arg_text)
    if match:
        return "string", match.group(1)
    match = re.match(r"^'((?:[^'\\]|\\.)*)'$", arg_text)
    if match:
        return "string", match.group(1)
    if re.match(r'^-?\d+$', arg_text):
        return "number", arg_text
    return None, None


def load_compack(name):
    """The symbol set of OSRS-Content/.../configs/all.<name>.compack --
    `id=symbol`, one per line (verified against the file on disk, not
    assumed: `29=cookquest` in all.varp.compack, `4626=cook` in
    all.npc.compack)."""
    path = os.path.join(CONFIGS_DIR, "all.%s.compack" % name)
    symbols = set()
    if not os.path.isfile(path):
        return symbols
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line:
                continue
            _, _, symbol = line.partition("=")
            if symbol:
                symbols.add(symbol)
    return symbols


def load_packs():
    return {
        PACK_NPC: load_compack("npc"),
        PACK_OBJ: load_compack("obj"),
        PACK_LOC: load_compack("loc"),
        PACK_VARP: load_compack("varp") | load_compack("varbit"),
    }


def _find_setup_cheats(text):
    """Every string literal inside the FIRST `setup = { ... }` table --
    plain (index, text) pairs, index being where the table itself opens (for
    line-numbering a finding when the literal's own position is not
    distinguishing enough)."""
    match = SETUP_OPEN_RE.search(text)
    if not match:
        return []
    open_idx = match.start(1)
    inner, _ = _extract_balanced(text, open_idx)
    return [(open_idx, lit.group(1)) for lit in STRING_LITERAL_RE.finditer(inner)]


def _find_bound_row(text):
    """The `row = "..."` a `t.quest.bind{...}` call carries, or None -- a
    file that does not call quest.bind has no "own row" to compare
    `::complete` against, so the check that needs it is simply inapplicable
    (see check_complete_own_row)."""
    match = QUEST_BIND_OPEN_RE.search(text)
    if not match:
        return None
    inner, _ = _extract_balanced(text, match.start(1))
    row_match = re.search(r'\brow\s*=\s*"([^"]+)"', inner)
    return row_match.group(1) if row_match else None


def check_numeric_ids_and_symbols(text, packs):
    findings = []
    for match in SYMBOL_CALL_RE.finditer(text):
        call_name, arg_text = match.group(1), match.group(2)
        pack_name = CALL_PACKS[call_name]
        kind, value = _literal_kind(arg_text)
        line = _line_of(text, match.start())
        if kind == "number":
            findings.append((line, "%s(%s, ...): a numeric id where a content symbol "
                                    "belongs -- name it (talk_to(123) is the spec's own "
                                    "example of this bug)" % (call_name, value)))
        elif kind == "string" and packs is not None:
            if value and value not in packs.get(pack_name, set()):
                findings.append((line, "%s(\"%s\", ...): not in all.%s.compack -- typo, "
                                        "or the wrong pack" % (call_name, value, pack_name)))
    for match in BY_SYMBOL_RE.finditer(text):
        kind_word, arg_text = match.group(1), match.group(2)
        kind, value = _literal_kind(arg_text)
        line = _line_of(text, match.start())
        if kind == "number":
            findings.append((line, "player.by_symbol(\"%s\", %s): a numeric id where a "
                                    "content symbol belongs" % (kind_word, value)))
        elif kind == "string" and packs is not None:
            if value and value not in packs.get(kind_word, set()):
                findings.append((line, "player.by_symbol(\"%s\", \"%s\"): not in all.%s"
                                        ".compack" % (kind_word, value, kind_word)))
    return findings


def check_complete_own_row(text):
    row = _find_bound_row(text)
    if not row:
        return []
    findings = []
    for open_idx, cheat in _find_setup_cheats(text):
        match = COMPLETE_CHEAT_RE.match(cheat.strip())
        if match and match.group(1) == row:
            findings.append((_line_of(text, open_idx),
                              "setup contains \"%s\", which completes this quest's own "
                              "row (%s) before run(t) plays it -- the run would prove "
                              "nothing" % (cheat, row)))
    return findings


def check_step_pass_literal(text):
    findings = []
    for match in T_STEP_OPEN_RE.finditer(text):
        open_idx = match.end() - 1
        inner, _ = _extract_balanced(text, open_idx)
        args = _split_top_level(inner)
        if len(args) >= 2:
            kind, _ = _literal_kind(args[1])
            if kind == "string" and args[1].strip().strip("'\"") == "PASS":
                findings.append((_line_of(text, match.start()),
                                  "t.step(...)'s verdict argument is a bare \"PASS\" "
                                  "literal -- nothing computed it, so nothing can fail "
                                  "it either"))
    return findings


# The three words the ledger's verdict column takes (docs/QUEST_SUITE_KIT.md,
# "Result vocabulary"). A verb's own result word -- "ok", "timeout",
# "not_found", ... -- is NOT one of them: that belongs to t.expect, which
# grades it into one of these.
VERDICT_WORDS = ("PASS", "FAIL", "BLOCKED")

# Top-level (unbracketed, unquoted) Lua tokens of one argument expression.
# `and`/`or` matter because `cond and "PASS" or "FAIL"` -- the correct idiom --
# contains a comparison too, and its VALUE is a string, not a boolean.
_COMPARISON_RE = re.compile(r'==|~=|<=|>=|<|>')
_ANDOR_RE = re.compile(r'(?<![\w.])(?:and|or)(?![\w])')
_NOT_RE = re.compile(r'^not(?![\w])')


def _blank_comments(expr):
    """`expr` with every Lua comment (line and long-bracket) blanked to
    spaces, string literals left alone. An argument written across several
    lines can carry one, and a `-- PASS when stage == 40` sitting above the
    argument is not a comparison the argument computes."""
    out = []
    in_string = None
    i = 0
    while i < len(expr):
        ch = expr[i]
        if not in_string and expr.startswith("--", i):
            if expr.startswith("--[[", i):
                end = expr.find("]]", i + 4)
                end = len(expr) if end < 0 else end + 2
            else:
                end = expr.find("\n", i)
                end = len(expr) if end < 0 else end
            out.append(" " * (end - i))
            i = end
            continue
        if in_string:
            if ch == "\\" and i + 1 < len(expr):
                out.append(ch)
                i += 1
                out.append(expr[i])
                i += 1
                continue
            if ch == in_string:
                in_string = None
        elif ch in ('"', "'"):
            in_string = ch
        out.append(ch)
        i += 1
    return "".join(out)


def _one_line(expr):
    """The expression as a message quotes it: no comments, no newlines, no
    runs of spaces, and short enough to read at the end of a finding."""
    shown = " ".join(_blank_comments(expr).split())
    return shown if len(shown) <= 60 else shown[:60] + "..."


def _strip_top_level(expr):
    """`expr` with every string literal and every bracketed group blanked to
    spaces (comments already gone), so a regex over the result only ever
    sees the expression's own top level."""
    expr = _blank_comments(expr)
    out = []
    depth = 0
    in_string = None
    i = 0
    while i < len(expr):
        ch = expr[i]
        if in_string:
            out.append(" ")
            if ch == "\\" and i + 1 < len(expr):
                i += 1
                out.append(" ")
            elif ch == in_string:
                in_string = None
        elif ch in ('"', "'"):
            in_string = ch
            out.append(" ")
        elif ch in _OPEN_CHARS:
            depth += 1
            out.append(" ")
        elif ch in _CLOSE_CHARS:
            depth -= 1
            out.append(" ")
        else:
            out.append(ch if depth == 0 else " ")
        i += 1
    return "".join(out)


def _boolean_valued(expr):
    """True when this argument's VALUE is certainly a boolean: the literals
    themselves, or a top-level comparison / `not` with no top-level
    `and`/`or` turning it back into a string. Anything else -- a bare
    variable, a call, the `cond and "PASS" or "FAIL"` idiom -- is not this
    rule's business, because a static reader cannot know what it is."""
    expr = expr.strip()
    if expr in ("true", "false"):
        return True
    top = _strip_top_level(expr)
    if _ANDOR_RE.search(top):
        return False
    return bool(_COMPARISON_RE.search(top)) or bool(_NOT_RE.match(top.strip()))


def check_step_verdict_type(text):
    """t.step's verdict argument must be one of the three verdict WORDS. The
    two shapes a static reader can be sure about are a boolean-valued
    expression and a string literal that is not a verdict; both used to be
    (boolean) or still are (bad word) a row the author did not mean."""
    findings = []
    for match in T_STEP_OPEN_RE.finditer(text):
        open_idx = match.end() - 1
        inner, _ = _extract_balanced(text, open_idx)
        args = _split_top_level(inner)
        if len(args) < 2:
            continue
        line = _line_of(text, match.start())
        kind, value = _literal_kind(args[1])
        if kind == "string":
            if value not in VERDICT_WORDS:
                findings.append((line,
                                  "t.step(..., \"%s\", ...): the verdict column takes "
                                  "%s -- \"%s\" is not one of them (a verb's own result "
                                  "word goes to t.expect, which grades it)"
                                  % (value, "/".join(VERDICT_WORDS), value)))
        elif _boolean_valued(args[1]):
            shown = _one_line(args[1])
            instead = ("\"PASS\"" if shown == "true" else
                       "\"FAIL\"" if shown == "false" else
                       "`%s and \"PASS\" or \"FAIL\"`" % shown)
            findings.append((line,
                              "t.step(..., %s, ...): a BOOLEAN where the verdict word "
                              "belongs -- write %s, or use t.check, whose second "
                              "argument IS the condition. This shape used to end the "
                              "whole run at that row (Between a Rock, 93 PASS rows "
                              "lost, 2026-09-21); core.lua now names it on the row "
                              "instead, which is a floor, not a licence."
                              % (shown, instead)))
    return findings


def check_marker(text):
    findings = []
    for match in CHECK_MARKER_RE.finditer(text):
        findings.append((_line_of(text, match.start()),
                          "-- CHECK marker left in place (a generated guess not yet "
                          "confirmed against a live run)"))
    return findings


def check_max_frames(text):
    """A quest's own `max_frames = <n>,` budget (run.py applies it to
    TORIRS_MAX_FRAMES) must be one field, positive, and at most the ceiling
    quest_list.MAX_FRAMES_CEILING -- run.py asserts on the same bound, and
    the lint says so first, at authoring time."""
    import quest_list  # the runner's own constants, so the two cannot disagree
    findings = []
    matches = list(quest_list.MAX_FRAMES_RE.finditer(text))
    if len(matches) > 1:
        findings.append((_line_of(text, matches[1].start()),
                          "more than one max_frames field -- declare the budget once"))
    for match in matches:
        frames = int(match.group(1))
        if frames <= 0 or frames > quest_list.MAX_FRAMES_CEILING:
            findings.append((_line_of(text, match.start()),
                              "max_frames = %d is outside 1..%d (the ceiling is 4x the "
                              "default %d; a guide that needs more needs a harness hook, "
                              "not a bigger clock)"
                              % (frames, quest_list.MAX_FRAMES_CEILING,
                                 quest_list.DEFAULT_MAX_FRAMES)))
    return findings


def check_duplicate_exec_names(text):
    findings = []
    seen = {}
    for match in T_EXEC_OPEN_RE.finditer(text):
        open_idx = match.end() - 1
        inner, _ = _extract_balanced(text, open_idx)
        args = _split_top_level(inner)
        if not args:
            continue
        kind, value = _literal_kind(args[0])
        if kind != "string":
            continue
        line = _line_of(text, match.start())
        if value in seen:
            findings.append((line, "t.exec(\"%s\", ...) duplicates the literal name "
                                    "already used at line %d -- a straight-line copy/"
                                    "paste, not core.lua's own -2/-3 runtime suffix "
                                    "(that is for a NAME COMPUTED IN A LOOP, not two "
                                    "identical literals)" % (value, seen[value])))
        else:
            seen[value] = line
    return findings


def check_guide_gap_markers(text, test_id=None):
    """Every `-- GUIDE-GAP:` marker must name a step and cite a real .rs2
    line (see the module banner). Reads the RAW text: the marker is a
    comment."""
    import helper_coverage  # lazy: the guide/content readers live there
    findings = []
    steps = None
    if test_id and helper_coverage.has_guide(test_id):
        row = helper_coverage.queue_row(test_id)
        guide = helper_coverage.Guide(helper_coverage.guide_path(row))
        steps = set(guide.steps)
    for number, line in enumerate(text.split("\n"), 1):
        if "GUIDE-GAP" not in line or not line.lstrip().startswith("--"):
            continue
        match = helper_coverage.GUIDE_GAP_RE.match(line)
        if not match:
            findings.append((number, "GUIDE-GAP marker is not `-- GUIDE-GAP: <step> <reason "
                                     "citing file.rs2:line>`"))
            continue
        step, reason = match.group(1), match.group(2)
        if not helper_coverage.rs2_citation(reason):
            findings.append((number, "GUIDE-GAP %s: the reason cites no .rs2 line that exists "
                                     "(write the `<file>.rs2:<line>` that writes past the leg) -- "
                                     "helper_coverage.py will not honour it" % step))
        if steps is not None and step not in steps:
            findings.append((number, "GUIDE-GAP %s: not a step of the guide %s" % (
                step, os.path.basename(guide.path))))
    return findings


def lint_text(text, allow_check=False, packs=None, test_id=None):
    findings = []
    # Every rule below that looks for a CALL gets the file with its comments
    # blanked to spaces -- `_blank_comments` preserves length, so line numbers
    # are unchanged.  A rule reading the raw text finds its own shape written
    # ABOUT, which is not the same as written: `_conformance.lua`'s ledger-
    # verdict seam quotes `t.step(name, <expr> == "ok", detail)` in the banner
    # explaining why nobody may write it, and the boolean rule flagged the
    # sentence (2026-09-22).  `check_marker` is the one rule that must NOT be
    # given the blanked text, because `-- CHECK` IS a comment.
    code = _blank_comments(text)
    findings.extend(check_numeric_ids_and_symbols(code, packs))
    findings.extend(check_complete_own_row(code))
    findings.extend(check_step_pass_literal(code))
    findings.extend(check_step_verdict_type(code))
    findings.extend(check_duplicate_exec_names(code))
    findings.extend(check_max_frames(code))
    if not allow_check:
        findings.extend(check_marker(text))
    findings.extend(check_guide_gap_markers(text, test_id))
    findings.sort(key=lambda item: item[0])
    return findings


def lint_file(path, allow_check=False, packs=None):
    assert path
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    test_id = os.path.splitext(os.path.basename(path))[0]
    return lint_text(text, allow_check=allow_check, packs=packs, test_id=test_id)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", help="test/quests/<quest>.lua file(s), or any "
                                                   "throwaway .lua file under build/ for a "
                                                   "proof run")
    parser.add_argument("--allow-check", action="store_true",
                        help="do not flag -- CHECK markers (new_quest.py's own generated "
                             "output must pass with this flag; a hand-finished quest "
                             "should not need it)")
    arguments = parser.parse_args()

    packs = load_packs()
    total = 0
    for path in arguments.files:
        if not os.path.isfile(path):
            print("lint_quest: no such file %s" % path, file=sys.stderr)
            total += 1
            continue
        findings = lint_file(path, allow_check=arguments.allow_check, packs=packs)
        if findings:
            print("%s:" % path)
            for line, message in findings:
                print("    %d: %s" % (line, message))
        total += len(findings)

    if total:
        print("lint_quest: %d finding(s)" % total, file=sys.stderr)
        return 1
    print("lint_quest: clean (%d file(s))" % len(arguments.files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
