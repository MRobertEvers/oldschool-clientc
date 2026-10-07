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
  * an item cheat after setup: a `::bankgive` literal anywhere outside the
    `setup = {...}` table, and a `::give` there that carries no
    `-- lint: kit-give <reason>` marker and is not one of the gives
    baselined in mid_run_gives_baseline.tsv (check_mid_run_gives;
    `--mid-run-gives` lists them all).
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
  * a var named without its kind and id. Every varp/varbit/varc symbol is
    `varp<id>_<name>` / `varb<id>_<name>` / `varc<id>_<name>` since PR #99
    (OSRS-Content/docs/VAR_NAMES.md); a `varp = "..."` / `varbit = "..."`
    key, a `::setvar <name>` in a string literal, or a `t.var.*` symbol
    that spells the old bare name is a finding naming the prefixed one
    (check_var_names).
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
  * a `-- BRANCH-IN:` / `-- PARTNER:` / `-- NOT-A-STEP:` / `-- OBSOLETE:` /
    `-- ANY-OF:` marker (a guide step that is not a gap -- the vocabulary is
    in helper_coverage.py's "equivalent markers" banner and trap 32) that is
    malformed, bare, uncited, or whose evidence does not check out: a sibling
    that does not drive the step, a cheat missing from the partner table, a
    NOT-A-STEP on anything but a plugin sync step, an OBSOLETE with no pinned
    wiki `?oldid=`/`#Section`, an ANY-OF with no citation or no such row.

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
  tools/quest_gate/lint_quest.py --mid-run-gives <file> [<file> ...]
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
CONFIGS_DIR = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "configs")
PACK_ALLOC_DIR = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "pack")

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
# A legs file's top-level `bind = { ... }` field: the harness calls
# t.quest.bind with it before the first leg it runs (docs/quest_authoring/
# relay.md "Checkpoints"), so a leg resumed from a checkpoint is bound too.
RUN_FIELD_RE = re.compile(r'(?<![\w.])run\s*=\s*function\b')
LEG_NAME_RE = re.compile(r'^[A-Za-z0-9_-]+$')
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


def load_alloc(name):
    """The symbol set of OSRS-Content/osrs239-content/pack/<name>.alloc --
    the ids this server allocated ABOVE the cache's own (`7166=
    twocats_locator_found`), `id=symbol` lines under `//` comments. A var
    content allocates there is as real as a compack one, so the symbol
    check must not call it a typo."""
    path = os.path.join(PACK_ALLOC_DIR, "%s.alloc" % name)
    symbols = set()
    if not os.path.isfile(path):
        return symbols
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            _, _, symbol = line.partition("=")
            if symbol:
                symbols.add(symbol.strip())
    return symbols


def load_packs():
    return {
        PACK_NPC: load_compack("npc"),
        PACK_OBJ: load_compack("obj"),
        PACK_LOC: load_compack("loc"),
        PACK_VARP: load_compack("varp") | load_compack("varbit") | load_alloc("varp"),
        PACK_VAR_BARE: load_var_names(),
    }


# ------------------------------------------------ var names carry kind and id
#
# Since OSRS-Content PR #25 / parent PR #99 (merged 2026-10-01) every varp,
# varbit and varc symbol is spelled `varp<id>_<name>`, `varb<id>_<name>` or
# `varc<id>_<name>` (OSRS-Content/docs/VAR_NAMES.md is the before/after
# table). A test that still writes the old bare name is wrong in one of two
# quiet ways: `t.quest.bind{varp = "cookquest"}` and `t.var.*("cookquest")`
# read nothing by that name, and `::setvar <bare>` falls through the cheat's
# exact rung to its substring rung, which refuses an ambiguous name
# (`::setvar qp 43` -> `Which qp? varb456_tog_qp_before_return, ...`: ten
# quests failed setup that way right after the merge) and silently picks the
# one var containing a unique one. So the lint refuses the bare spelling and
# names the prefixed one.

PACK_VAR_BARE = "var_bare"  # load_packs() key: {bare name: prefixed name}
VAR_PREFIX_RE = re.compile(r'^var([pbc])(\d+)_(\w+)$')
VAR_NAME_SOURCES = (
    # (directory, file, the prefix letter every name in it carries)
    (CONFIGS_DIR, "all.varp.compack", "p"),
    (CONFIGS_DIR, "all.varbit.compack", "b"),
    (CONFIGS_DIR, "all.varc.compack", "c"),
    (PACK_ALLOC_DIR, "varp.alloc", "p"),
)
VAR_NAMES_DOC = "OSRS-Content/docs/VAR_NAMES.md"


def load_var_names():
    """{bare name: prefixed name} for every var the content declares, read
    from the compacks and the varp allocation (`29=varp29_cookquest` ->
    `cookquest: varp29_cookquest`). Asserts each line's prefix carries its own
    kind and id -- a table that disagrees with itself is not one to lint
    against -- and that no bare name maps to two vars (measured 2026-10-01:
    27,704 bare names, none shared)."""
    table = {}
    for directory, filename, letter in VAR_NAME_SOURCES:
        path = os.path.join(directory, filename)
        if not os.path.isfile(path):
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("//"):
                    continue
                var_id, _, symbol = line.partition("=")
                symbol = symbol.split("//")[0].strip()
                match = VAR_PREFIX_RE.match(symbol)
                assert match, "%s: %r does not carry var<kind><id>_" % (filename, line)
                assert match.group(1) == letter, "%s: %r has the wrong kind" % (filename, line)
                assert match.group(2) == var_id.strip(), "%s: %r has the wrong id" % (filename, line)
                bare = match.group(3)
                assert table.get(bare, symbol) == symbol, \
                    "bare var name %r is both %s and %s" % (bare, table[bare], symbol)
                table[bare] = symbol
    return table


def prefixed_var_name(name, var_names):
    """`name` if it already carries its kind and id and names a declared var;
    the prefixed spelling of a bare `name`; or None when neither holds."""
    assert var_names is not None
    match = VAR_PREFIX_RE.match(name)
    if match:
        return name if var_names.get(match.group(3)) == name else None
    return var_names.get(name)


_VAR_KEY_RE = re.compile(r'(?<![\w.])(varp|varbit)\s*=\s*"([^"]*)"')
_SETVAR_IN_LITERAL_RE = re.compile(r'::setvar\s+([^\s"\']+)')
_ANY_STRING_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.)*)\'')


def _var_name_finding(where, name, var_names):
    """The finding for one var name in a bind key or a `::setvar`, or None."""
    if VAR_PREFIX_RE.match(name):
        if prefixed_var_name(name, var_names):
            return None
        bare = VAR_PREFIX_RE.match(name).group(3)
        right = var_names.get(bare)
        return ("%s: %s is not a declared var%s (%s)"
                % (where, name, "; its id is %s" % right if right else "", VAR_NAMES_DOC))
    right = var_names.get(name)
    if right:
        return ("%s: the bare var name %s -- var names carry their kind and id; write %s "
                "(%s)" % (where, name, right, VAR_NAMES_DOC))
    return ("%s: %s is not a var name, bare or prefixed -- var names carry their kind and id "
            "(var<p|b|c><id>_<name>, %s)" % (where, name, VAR_NAMES_DOC))


def check_var_names(code, var_names, test_id=None):
    """A `varp = "..."` / `varbit = "..."` key (t.quest.bind's own, a legs
    file's top-level bind, or any table that names a var that way) and a
    `::setvar <name>` inside a string literal must spell the var with its
    kind and id. Reads COMMENT-BLANKED code: a comment that quotes the old
    spelling is history, not a call. A `"::setvar " .. name` built at run time
    has no name in the literal and is not this rule's business.

    `_`-prefixed harness files (`_cheats.lua`, `_conformance.lua`) are not
    quests -- `make test-quests` lints `test/quests/[!_]*.lua` -- and they
    spell bare names ON PURPOSE: they prove the cheat's substring rung and
    its refusal of an unknown name."""
    findings = []
    if var_names is None or (test_id or "").startswith("_"):
        return findings
    # A key is code, never the inside of a string: a detail written as
    # `"... varbit = " .. tostring(x)` is a message, and its closing quote
    # is not the start of a var name (forgettabletale.lua, 2026-10-01).
    in_string = bytearray(len(code))
    for literal in _ANY_STRING_LITERAL_RE.finditer(code):
        in_string[literal.start():literal.end()] = b"\x01" * (literal.end() - literal.start())
    for match in _VAR_KEY_RE.finditer(code):
        if in_string[match.start()]:
            continue
        message = _var_name_finding('%s = "%s"' % (match.group(1), match.group(2)),
                                    match.group(2), var_names)
        if message:
            findings.append((_line_of(code, match.start()), message))
    for literal in _ANY_STRING_LITERAL_RE.finditer(code):
        body = literal.group(1) if literal.group(1) is not None else literal.group(2)
        for setvar in _SETVAR_IN_LITERAL_RE.finditer(body):
            name = setvar.group(1)
            message = _var_name_finding("\"::setvar %s\"" % name, name, var_names)
            if message:
                findings.append((_line_of(code, literal.start()), message))
    return findings


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
    """The `row = "..."` a `t.quest.bind{...}` call (or a legs file's
    top-level `bind = {...}` field) carries, or None -- a file that does not
    bind has no "own row" to compare `::complete` against, so the check that
    needs it is simply inapplicable (see check_complete_own_row)."""
    match = QUEST_BIND_OPEN_RE.search(text)
    if match:
        inner, _ = _extract_balanced(text, match.start(1))
    else:
        layout = legs_layout(text)
        if not layout or not layout["bind"]:
            return None
        inner = layout["bind"]
    row_match = re.search(r'\brow\s*=\s*"([^"]+)"', inner)
    return row_match.group(1) if row_match else None


def top_level_fields(code):
    """{field name: index of its value} for the fields of the quest file's
    returned table -- `return { id = ..., setup = {...}, legs = {...} }` --
    read from COMMENT-BLANKED code (`_blank_comments`). A field counts only
    at bracket depth 0 of that table and right after its `{` or a `,`, so a
    `local legs = {...}` or a `bind = {...}` statement inside a function body
    (death.lua keeps a local named `legs`) is never mistaken for one."""
    fields = {}
    returns = list(re.finditer(r'(?m)^return\s*(\{)', code))
    if not returns:
        return fields
    open_idx = returns[-1].start(1)
    _, close_end = _extract_balanced(code, open_idx)
    depth = 0
    in_string = None
    previous = "{"
    i = open_idx + 1
    field_re = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)\s*')
    while i < close_end - 1:
        ch = code[i]
        if in_string:
            if ch == "\\":
                i += 2
                continue
            if ch == in_string:
                in_string = None
                previous = ch
            i += 1
            continue
        if ch.isspace():
            i += 1
            continue
        if depth == 0 and previous in "{," and (ch.isalpha() or ch == "_"):
            match = field_re.match(code, i)
            if match and match.group(1) not in fields:
                fields[match.group(1)] = match.end()
        if ch in ('"', "'"):
            in_string = ch
        elif ch in _OPEN_CHARS:
            depth += 1
        elif ch in _CLOSE_CHARS:
            depth -= 1
        previous = ch
        i += 1
    return fields


def legs_layout(text):
    """The `legs = { {name=, run=function(t) ... end}, ... }` field of a
    relay-authored quest file's returned table, or None when it has none.

    Returns a dict: `legs` -- one {name, start, end, text, has_run} per
    entry, in order (start/end index the ORIGINAL text; `text` is the entry's
    own source, comments included, so a comment edit inside a leg is an edit
    to that leg); `setup` / `bind` -- the source text of the table's
    `setup = {...}` / `bind = {...}` fields, or "" when absent; `run_field`
    -- whether the table ALSO has a `run` field. run.py hashes these for a
    checkpoint's manifest (docs/quest_authoring/relay.md "Checkpoints"); this
    module's own rules below read the same layout, so the linter and the
    harness can never disagree about where a leg is.

    Comment-blind (`_blank_comments` preserves length, so an index into the
    blanked text is an index into the original)."""
    code = _blank_comments(text)
    fields = top_level_fields(code)
    if "legs" not in fields or code[fields["legs"]:fields["legs"] + 1] != "{":
        return None
    open_idx = fields["legs"]
    inner, close_end = _extract_balanced(code, open_idx)
    base = open_idx + 1
    legs = []
    depth = 0
    in_string = None
    i = 0
    while i < len(inner):
        ch = inner[i]
        if in_string:
            if ch == "\\":
                i += 1
            elif ch == in_string:
                in_string = None
        elif ch in ('"', "'"):
            in_string = ch
        elif ch == "{" and depth == 0:
            _, past = _extract_balanced(code, base + i)
            start = base + i
            entry_code = code[start:past]
            run_match = RUN_FIELD_RE.search(entry_code)
            head = entry_code[:run_match.start()] if run_match else entry_code
            name_match = re.search(r'(?<![\w.])name\s*=\s*"([^"]*)"', head)
            legs.append({
                "name": name_match.group(1) if name_match else None,
                "start": start,
                "end": past,
                "text": text[start:past],
                "has_run": bool(run_match),
            })
            i = past - base
            continue
        elif ch in _OPEN_CHARS:
            depth += 1
        elif ch in _CLOSE_CHARS:
            depth -= 1
        i += 1

    def table_text(field):
        at = fields.get(field)
        if at is None or code[at:at + 1] != "{":
            return ""
        _, past = _extract_balanced(code, at)
        return text[at:past]

    return {
        "legs": legs,
        "legs_span": (open_idx, close_end),
        "setup": table_text("setup"),
        "bind": table_text("bind"),
        "run_field": "run" in fields,
    }


def check_legs(text):
    """The legs table's own shape (docs/quest_authoring/relay.md). Every
    other rule in this file already reads the whole file, so it applies
    inside each leg function exactly as it does inside a `run` function; the
    only thing a legs file adds is the table, and this is its rule set:

      * `legs` and a top-level `run = function` are exclusive -- a file that
        declares both leaves the harness guessing which one is the test;
      * every entry is `{ name = "<name>", run = function(t) ... end }`, the
        name first, unique, and made of [A-Za-z0-9_-] (it becomes the ledger
        row `leg.<k>.<name>` and part of a checkpoint's manifest);
      * a file that uses `t.quest.*` declares the top-level `bind = {...}`
        field instead of calling t.quest.bind inside a leg: the harness binds
        before the first leg it runs, and a leg resumed from a checkpoint
        never ran leg 1's call;
      * `t.finish(` appears only in the LAST leg -- an earlier leg that
        finishes silently skips every leg after it (t.blocked may end any
        leg: a blocked quest stops where it stops)."""
    findings = []
    code = _blank_comments(text)
    layout = legs_layout(text)
    if layout is None:
        return findings
    legs_line = _line_of(text, layout["legs_span"][0])
    if layout["run_field"]:
        findings.append((legs_line, "the file declares both `legs` and a top-level `run = "
                                    "function`: a relay file has legs only"))
    if not layout["legs"]:
        findings.append((legs_line, "`legs = {}` has no entries"))
    seen = {}
    for index, leg in enumerate(layout["legs"], 1):
        line = _line_of(text, leg["start"])
        if leg["name"] is None:
            findings.append((line, "leg %d has no `name = \"...\"` before its run field"
                                   % index))
        elif not LEG_NAME_RE.match(leg["name"]):
            findings.append((line, "leg %d's name %r: use [A-Za-z0-9_-] only -- it is the "
                                   "ledger row leg.%d.<name>" % (index, leg["name"], index)))
        elif leg["name"] in seen:
            findings.append((line, "leg %d's name %r repeats leg %d's" % (
                index, leg["name"], seen[leg["name"]])))
        else:
            seen[leg["name"]] = index
        if not leg["has_run"]:
            findings.append((line, "leg %d has no `run = function(t) ... end`" % index))
        leg_code = code[leg["start"]:leg["end"]]
        if QUEST_BIND_OPEN_RE.search(leg_code):
            findings.append((line, "leg %d calls t.quest.bind: put the table in the file's "
                                   "top-level `bind = {...}` field -- a leg resumed from a "
                                   "checkpoint never runs another leg's bind" % index))
        if index < len(layout["legs"]) and re.search(r'(?<![\w.])t\s*\.\s*finish\s*\(',
                                                      leg_code):
            findings.append((line, "leg %d calls t.finish but is not the last leg: every "
                                   "leg after it would silently never run" % index))
    if re.search(r'(?<![\w.])t\s*\.\s*quest\s*\.', code) and not layout["bind"]:
        findings.append((legs_line, "the legs use t.quest.* but the file has no top-level "
                                    "`bind = {...}` field for the harness to bind with"))
    return findings


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
                prefixed = (packs.get(PACK_VAR_BARE, {}).get(value)
                            if pack_name == PACK_VARP else None)
                if prefixed:
                    findings.append((line, "%s(\"%s\", ...): the bare var name -- var names "
                                            "carry their kind and id; write \"%s\" (%s)"
                                            % (call_name, value, prefixed, VAR_NAMES_DOC)))
                else:
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


QUEST_CHEAT_RS2 = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content", "server", "scripts", "quests",
                               "scripts", "quest_cheat.rs2")
_CHEAT_ARM_RE = re.compile(r'\$row\s*=\s*(?:~quest_cheat_row\(\s*)?([A-Za-z_]\w*)')
_cheat_arms_cache = None


def load_cheat_arms():
    """The rows `::complete` has an arm for, read from quest_cheat.rs2
    (`if ($row = quest_x)` and `if ($row = ~quest_cheat_row(quest_x))`).
    None when the content tree is not checked out (lint still runs)."""
    global _cheat_arms_cache
    if _cheat_arms_cache is None:
        try:
            with open(QUEST_CHEAT_RS2, "r", encoding="utf-8", errors="replace") as handle:
                _cheat_arms_cache = set(_CHEAT_ARM_RE.findall(handle.read()))
        except OSError:
            _cheat_arms_cache = False
    return _cheat_arms_cache or None


def check_complete_has_arm(text):
    """A setup `::complete <row>` naming a row quest_cheat.rs2 has no arm for
    does NOTHING: the server answers "::complete has no arm for that quest."
    and setup carries on with the prerequisite unset. Forgettable Tale staged
    `quest_fishingcompo` (the arm is quest_fishingcontest) and was green only
    because an engine bug wrote the varp by accident; Ghosts Ahoy and Shades
    of Mort'ton staged `quest_priestperil` (quest_priestinperil), Mourning's
    End Part II `quest_mourningsendparti` (quest_mourningsendpart1)."""
    arms = load_cheat_arms()
    if not arms:
        return []
    findings = []
    for open_idx, cheat in _find_setup_cheats(text):
        match = COMPLETE_CHEAT_RE.match(cheat.strip())
        if match and match.group(1) not in arms:
            near = sorted(a for a in arms if a[:10] == match.group(1)[:10])[:4]
            findings.append((_line_of(text, open_idx),
                              "setup contains \"%s\", but quest_cheat.rs2 has no arm for %s: the server "
                              "answers \"::complete has no arm for that quest.\" and the prerequisite stays "
                              "unset%s" % (cheat, match.group(1),
                                           (" -- did you mean %s?" % " / ".join(near)) if near else "")))
    return findings


# A `"::give ..."` / `"::bankgive ..."` string literal: the cheat the driver
# sends, whatever wraps it (t.cheat, t.exec(name, t.cheat, ...), a local
# `cheat(...)` helper, `"::give " .. symbol .. " 1"`, string.format). At most
# the cheat's two arguments: a row detail that QUOTES one (`"::give clay 6 -> "
# .. tostring(r)`) is text about the cheat, not a second cheat.
GIVE_CHEAT_RE = re.compile(r'^\s*::(give|bankgive)(?:\s+[\w%]+){0,2}\s*$')
# `-- lint: kit-give <reason>` on the give's own line, or alone on the line
# directly above it: a mid-run ::give the orchestrator has accepted.
KIT_GIVE_MARKER_RE = re.compile(r'--\s*lint:\s*kit-give\b[ \t]*(.*)$')


def _setup_span(code):
    """(start, end) of the quest file's `setup = { ... }` table in
    COMMENT-BLANKED code, or None. The returned table's own top-level field
    when there is one (top_level_fields, the reader legs_layout uses), else
    the first `setup = {` anywhere (the table _find_setup_cheats reads)."""
    at = top_level_fields(code).get("setup")
    if at is None or code[at:at + 1] != "{":
        match = SETUP_OPEN_RE.search(code)
        if not match:
            return None
        at = match.start(1)
    _, past = _extract_balanced(code, at)
    return at, past


def _kit_give_marker(raw_lines, number):
    """(marker line, reason) for the `-- lint: kit-give` marker that covers
    line `number`, or None: the marker on that line, or a comment-only line
    directly above it that is the marker."""
    match = KIT_GIVE_MARKER_RE.search(raw_lines[number - 1])
    if match:
        return number, match.group(1).strip()
    if number >= 2:
        above = raw_lines[number - 2]
        match = KIT_GIVE_MARKER_RE.search(above)
        if match and above.lstrip().startswith("--"):
            return number - 1, match.group(1).strip()
    return None


def mid_run_gives(text):
    """Every `::give` / `::bankgive` string literal OUTSIDE the file's
    `setup = { ... }` table -- in run(), a leg, a local helper: anywhere that
    runs after setup -- as [{line, cheat, kind: give|bankgive, marker: (line,
    reason) or None}]. Reads comment-blanked code for the literals (a comment
    that quotes `::give` is history, not a call) and the raw text for the
    marker (it IS a comment)."""
    code = _blank_comments(text)
    span = _setup_span(code)
    raw_lines = text.split("\n")
    found = []
    for literal in STRING_LITERAL_RE.finditer(code):
        match = GIVE_CHEAT_RE.match(literal.group(1))
        if not match:
            continue
        if span and span[0] <= literal.start() < span[1]:
            continue
        number = _line_of(code, literal.start())
        found.append({"line": number, "cheat": literal.group(1).strip(), "kind": match.group(1),
                      "marker": _kit_give_marker(raw_lines, number)})
    return found


# The mid-run ::give lines committed tests carried when this rule landed
# (measured over test/quests/[!_]*.lua at matthew-mbp-m4-b56 458d31771 and
# origin/v3 a8974744e, the same 113 literals in 23 files). Each one is the
# orchestrator's to decide -- move it into setup, drive the item, or mark it
# `-- lint: kit-give <reason>` -- and until then it is BASELINED: printed and
# counted, not refused. Keyed by (test_id, cheat literal) with a count, not a
# line number, so an edit elsewhere in the file does not unbaseline it. A row
# whose give is gone is refused as stale: delete the row (the list only shrinks).
GIVE_BASELINE_PATH = os.path.join(HERE, "mid_run_gives_baseline.tsv")
_give_baseline_cache = None


def load_give_baseline(path=None):
    """{test_id: {cheat literal: count}} from mid_run_gives_baseline.tsv
    (columns: test_id, count, cheat, lines at the measure; `#` comments)."""
    global _give_baseline_cache
    if path is None and _give_baseline_cache is not None:
        return _give_baseline_cache
    baseline = {}
    with open(path or GIVE_BASELINE_PATH, "r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.rstrip("\n").split("\t")
            assert len(fields) >= 3, "mid_run_gives_baseline.tsv: bad row %r" % line
            if fields[0] == "test_id":
                continue
            baseline.setdefault(fields[0], {})[fields[2]] = int(fields[1])
    if path is None:
        _give_baseline_cache = baseline
    return baseline


def classify_mid_run_gives(text, test_id=None, baseline=None):
    """mid_run_gives(text), each with a `state`: `bankgive` (always refused),
    `marked` (a `-- lint: kit-give` covers it), `baselined` (one of this
    test's mid_run_gives_baseline.tsv rows still has budget for that exact
    literal) or `unmarked` (refused). Returns (gives, stale) where stale is
    [(cheat, unused count)] -- baseline budget no give of this file used."""
    if baseline is None:
        baseline = load_give_baseline()
    budget = dict(baseline.get(test_id or "", {}))
    gives = mid_run_gives(text)
    for give in gives:
        if give["kind"] == "bankgive":
            give["state"] = "bankgive"
        elif give["marker"]:
            give["state"] = "marked"
        elif budget.get(give["cheat"], 0) > 0:
            budget[give["cheat"]] -= 1
            give["state"] = "baselined"
        else:
            give["state"] = "unmarked"
    stale = sorted((cheat, count) for cheat, count in budget.items() if count > 0)
    return gives, stale


def check_mid_run_gives(text, test_id=None, baseline=None):
    """No item cheat after setup (docs/QUEST_ORCHESTRATOR.md, the owner's
    standing rules; trap 16). `setup = { ... }` states the world before
    run()'s first row; a `::give` written in run() hands the player an item
    in the middle of the quest, which is how a guide step that OBTAINS it
    gets skipped without a row saying so.

      * `::bankgive` outside setup is ALWAYS refused. The driver refuses it
        at run time only once t.quest.bind has run (core.lua QD.cheat), so
        one written in run() before the bind slips through there: this is
        its static twin. Stock the bank in setup.
      * `::give` outside setup is refused unless the line carries (or the
        comment line directly above it is) `-- lint: kit-give <reason>` --
        an exception the orchestrator accepted (a leg-start kit a checkpoint
        resume must restage, brought-along food a fight used up) -- or it is
        one of the BASELINED gives committed before the rule
        (mid_run_gives_baseline.tsv). The marker needs a reason, excuses
        nothing but a `::give`, and a marker that covers no mid-run give is
        refused (a stale one would excuse the next give written beside it);
        so is a baseline row whose give is gone. main() prints the marked
        and baselined counts.

    `_`-prefixed harness files (`_cheats.lua`, `_conformance.lua`) are not
    quests: they issue these cheats mid-run ON PURPOSE (the bankgive
    refusal row is one)."""
    findings = []
    if (test_id or "").startswith("_"):
        return findings
    gives, stale = classify_mid_run_gives(text, test_id, baseline)
    used_markers = set()
    for give in gives:
        marker = give["marker"]
        if marker:
            used_markers.add(marker[0])
        if give["state"] == "bankgive":
            findings.append((give["line"],
                              "\"%s\" outside setup: ::bankgive is a SETUP cheat -- stock the bank in "
                              "`setup = {...}` (the driver refuses it after t.quest.bind; one before "
                              "the bind is a mid-run ::give with a detour through the bank, trap 16)%s"
                              % (give["cheat"], "; `-- lint: kit-give` does not excuse it"
                                 if marker else "")))
        elif give["state"] == "unmarked":
            findings.append((give["line"],
                              "\"%s\" outside setup: a mid-run ::give (owner's rule: no ::give after "
                              "setup; trap 16). Move it into `setup = {...}`, or obtain the item the "
                              "way the guide does; an exception the orchestrator accepted carries "
                              "`-- lint: kit-give <reason>` on the line" % give["cheat"]))
        elif give["state"] == "marked" and not marker[1]:
            findings.append((marker[0], "`-- lint: kit-give` with no reason: say why this ::give "
                                        "cannot be in setup (whose kit, which leg)"))
    for number, line in enumerate(text.split("\n"), 1):
        if KIT_GIVE_MARKER_RE.search(line) and number not in used_markers:
            findings.append((number, "`-- lint: kit-give` covers no mid-run ::give (it marks its own "
                                     "line, or alone the line directly below it): delete it"))
    for cheat, count in stale:
        findings.append((1, "mid_run_gives_baseline.tsv baselines %d x \"%s\" for %s that this file "
                            "no longer gives mid-run: lower or delete that row (the list only shrinks)"
                            % (count, cheat, test_id)))
    return findings


# A stat-setting cheat issued after setup. There is no baseline and no marker:
# no mid-run stat change has been accepted, stats belong in `setup = {...}`.
STAT_CHEAT_RE = re.compile(r'^\s*::(setlevel|setstat|boost|xp|setxp|addxp)(?:\s+[\w%]+){0,3}\s*$')


def mid_run_stat_cheats(text):
    """Every stat-setting cheat string literal (::setlevel, ::setstat, ::boost,
    ::xp, ::setxp, ::addxp) OUTSIDE the `setup = { ... }` table, as [{line,
    cheat}]. Same reading as mid_run_gives: comment-blanked code."""
    code = _blank_comments(text)
    span = _setup_span(code)
    found = []
    for literal in STRING_LITERAL_RE.finditer(code):
        if not STAT_CHEAT_RE.match(literal.group(1)):
            continue
        if span and span[0] <= literal.start() < span[1]:
            continue
        found.append({"line": _line_of(code, literal.start()), "cheat": literal.group(1).strip()})
    return found


def check_mid_run_stat_cheats(text, test_id=None):
    """No stat cheat after setup: a `::setlevel` in run() raises a stat in the
    middle of the quest, hiding a training step the guide has the player do.
    State the stat in `setup = {...}`. No baseline, no exemption marker.
    `_`-prefixed harness files are not quests and are skipped."""
    if (test_id or "").startswith("_"):
        return []
    return [(item["line"],
             "\"%s\" mid-run stat cheat: stats belong in `setup = {...}` (no ::setlevel / ::setstat / "
             "::boost / ::xp after setup; there is no baseline and no marker)" % item["cheat"])
            for item in mid_run_stat_cheats(text)]


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
    findings.extend(check_equivalent_markers(text, test_id))
    return findings


def check_equivalent_markers(text, test_id=None):
    """Every `-- BRANCH-IN:` / `-- PARTNER:` / `-- NOT-A-STEP:` / `-- OBSOLETE:`
    / `-- ANY-OF:` marker must be well formed and its evidence must check out
    (helper_coverage.Grader.verify_equivalent, before any run: the ledger
    checks wait for gate.py). A bare or uncited one is refused, and
    helper_coverage.py grades its step as if it were not there."""
    import helper_coverage  # lazy: the guide/content readers live there
    findings = []
    markers = []
    for number, line in enumerate(text.split("\n"), 1):
        parsed = helper_coverage.parse_equivalent_marker(line)
        if parsed:
            markers.append((number, parsed))
        elif re.match(r"^\s*--\s*(BRANCH-IN|PARTNER|NOT-A-STEP|OBSOLETE|ANY-OF)\b", line):
            findings.append((number, "marker is missing its colon: `-- <KIND>: <step> ...`"))
    if not markers:
        return findings
    if not (test_id and helper_coverage.has_guide(test_id)):
        for number, parsed in markers:
            findings.append((number, "%s marker in a file with no QUEUE row / Quest Helper guide -- "
                                     "nothing to verify it against" % parsed["kind"]))
        return findings
    grader = helper_coverage.Grader(test_id, test_text=text)
    for number, parsed in markers:
        parsed["line"] = number
        ok, why = grader.verify_equivalent(parsed, static=True)
        if not ok:
            findings.append((number, "%s %s: %s -- helper_coverage.py will not honour it" % (
                parsed["kind"], parsed["step"] or "(no step)", why)))
    return findings


T_CHECK_OPEN_RE = re.compile(r'(?<![\w.])t\s*\.\s*check\s*\(')
# Verbs that only READ: their status is "ok" whatever they read (0 of an
# item, any tile), so as a t.check condition they can never fail.
READ_VERBS = ("world.tile", "world.level", "world.camera", "inv.count", "inv.has", "inv.slot",
              "skill.read", "skill.snapshot", "var.varp", "var.varbit", "var.server")
_READ_CALL_RE = re.compile(r'^t\.(%s)\s*\(' % "|".join(re.escape(v) for v in READ_VERBS))
_BARE_VERB_RE = re.compile(r'^t(\s*\.\s*[A-Za-z_]\w*)+$')
READ_FIX = {
    "inv.count": "t.inv.expect_has(name, n) / t.inv.expect_absent(name), or compare the count it returns",
    "inv.has": "t.inv.expect_has(name) / t.inv.expect_absent(name)",
    "world.tile": "local r, h = t.world.tile(), then compare h.x, h.z and h.level",
    "world.level": "local r, level = t.world.level(), then compare the level",
    "skill.read": "local r, s = t.skill.read(name), then compare s.level (current) / s.base_level (max)",
    "var.varp": "t.var.expect(name, value)", "var.varbit": "t.var.expect(name, value)",
    "var.server": "t.var.await_server(name, value, ticks)",
}


def check_vacuous_check_condition(text):
    """t.check(name, <condition>, detail) passes on `true` or "ok". A pure
    read returns "ok" for anything it read, and a verb written without its
    call parentheses is a function value: both make a row that cannot fail
    (b53-b55: `t.check("keris.in_inv", t.inv.count("contact_keris"))` passed
    with 0 keris; `t.check("enterCave.below", t.world.tile)` printed a table).
    A status from a verb that CAN refuse (t.var.await, t.inv.expect_has,
    t.msg.expect, ...) is the designed use and is not flagged."""
    findings = []
    for match in T_CHECK_OPEN_RE.finditer(text):
        inner, _ = _extract_balanced(text, match.end() - 1)
        args = _split_top_level(inner)
        if len(args) < 2:
            continue
        condition = _one_line(args[1]).strip()
        line = _line_of(text, match.start())
        read = _READ_CALL_RE.match(condition)
        whole_call = False
        if read:
            # the condition is that one call and nothing else (no `== 2`,
            # no `and`, no select(2, ...) around it)
            _, close = _extract_balanced(condition, read.end() - 1)
            whole_call = condition[close:].strip() == ""
        if whole_call:
            verb = read.group(1)
            findings.append((line,
                              "t.check(..., t.%s(...)): a read's status is \"ok\" whatever it read, so this "
                              "row cannot fail -- %s" % (verb, READ_FIX.get(verb, "compare the value it returns")))) 
        elif _BARE_VERB_RE.match(condition) and condition not in ("true", "false"):
            findings.append((line,
                              "t.check(..., %s): a verb without its call is a function value, not a "
                              "result -- call it and compare what it returns" % condition))
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
    findings.extend(check_complete_has_arm(code))
    findings.extend(check_step_pass_literal(code))
    findings.extend(check_step_verdict_type(code))
    findings.extend(check_vacuous_check_condition(code))
    findings.extend(check_duplicate_exec_names(code))
    findings.extend(check_max_frames(code))
    findings.extend(check_var_names(code, packs.get(PACK_VAR_BARE) if packs else None, test_id))
    findings.extend(check_legs(text))
    findings.extend(check_mid_run_gives(text, test_id))
    findings.extend(check_mid_run_stat_cheats(text, test_id))
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
    parser.add_argument("--mid-run-gives", action="store_true",
                        help="only LIST every ::give / ::bankgive outside setup (file:line, kind, "
                             "state: marked / baselined / UNMARKED / bankgive, the cheat) and exit "
                             "0 -- the measure behind check_mid_run_gives; `_` harness files are "
                             "skipped as the rule skips them")
    arguments = parser.parse_args()

    def test_id_of(path):
        return os.path.splitext(os.path.basename(path))[0]

    if arguments.mid_run_gives:
        counts = {}
        for path in arguments.files:
            if test_id_of(path).startswith("_"):
                continue
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                gives, _ = classify_mid_run_gives(handle.read(), test_id_of(path))
            for give in gives:
                state = give["state"] if give["state"] != "unmarked" else "UNMARKED"
                key = "%s %s" % (give["kind"], state)
                counts[key] = counts.get(key, 0) + 1
                print("%s:%d\t%s\t%s\t%s" % (path, give["line"], give["kind"], state, give["cheat"]))
        print("lint_quest: mid-run gives: %s" % (", ".join(
            "%s %d" % (key, counts[key]) for key in sorted(counts)) or "none"))
        return 0

    packs = load_packs()
    total = 0
    tally = {"marked": [0, 0], "baselined": [0, 0]}  # state -> [gives, files]
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
        if test_id_of(path).startswith("_"):
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            gives, _ = classify_mid_run_gives(handle.read(), test_id_of(path))
        for state, label in (("marked", "marked `-- lint: kit-give`"),
                             ("baselined", "BASELINED (mid_run_gives_baseline.tsv, the orchestrator's to decide)")):
            lines = [g["line"] for g in gives if g["state"] == state]
            if lines:
                tally[state][0] += len(lines)
                tally[state][1] += 1
                print("%s: note: %d mid-run ::give %s (line%s %s)" % (
                    path, len(lines), label, "" if len(lines) == 1 else "s",
                    ", ".join(str(n) for n in lines)))
    for state in ("marked", "baselined"):
        if tally[state][0]:
            print("lint_quest: %d %s mid-run ::give(s) in %d file(s)" % (
                tally[state][0], state, tally[state][1]))

    if total:
        print("lint_quest: %d finding(s)" % total, file=sys.stderr)
        return 1
    print("lint_quest: clean (%d file(s))" % len(arguments.files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
