#!/usr/bin/env python3
"""Match every Lua `api.drive.<name>(...)` call site against its C
registration, and lint the one unit confusion that made this project look
dead for two sessions.

Two independent checks, both static (no build, no run):

1. ABI CHECK. Parse the `{"name", lua_fn}` registration arrays in
   src/plugin/torirs_plugin_drive*.c (six files, one per owner group -- see
   docs/ARCHITECT.md section 3, "The Lua surface"). For each `lua_fn`, work
   out from its own source:
     - the ARITY it reads: the highest Lua stack index touched by a
       `PluginDrive_Arg*`/`luaL_check*` call (REQUIRED -- missing raises) and
       by a `PluginDrive_ArgOpt*`/`luaL_opt*` call (OPTIONAL -- missing
       defaults). The required index is the floor a caller must clear.
     - the RETURN COUNT: every `return` in the function body, classified as
       `PluginDrive_PushResult(...)` (always 2), a bare integer literal, or
       (excluded, not a value return) a `luaL_error`/`luaL_argerror` raise or
       a `lua_yield` suspension. One consistent value across every real
       return is "known"; anything else is "unknown" and does not fail the
       gate (branches a static reader cannot rule out).
   Then parse every `api_drive.<name>(...)` call site in
   script/plugins/quest_driver/*.lua: its argument count (balanced-paren,
   top-level commas) and, from the text immediately before the call on the
   same line, how many values the caller destructures -- an assignment's LHS
   count, 0 for a bare statement, or "unknown" for a tail `return` (the
   caller's own caller decides) or an embedded expression.

   Reported, every one with file:line on both sides:
     - ARITY MISMATCH: a call site passes fewer arguments than the C side's
       required floor -- this raises `luaL_error`/aborts at that exact call
       the moment the world exercises it, which is exactly the class of bug
       a driver with no live-fire test would never catch (QUEST_DRIVER_PLAN's
       whole reason to exist).
     - RETURN-COUNT MISMATCH: a call site destructures more values than the
       C side is known to push -- the extra locals silently read `nil`.
     - REGISTERED NOWHERE: a call site names a verb no array registers.
     - NOTHING CALLS: a registered verb with no call site in the driver's own
       Lua (dead code, or a verb only a quest test itself calls -- listed for
       a human to judge, not failed).
   Unknown arities/returns/destructures are listed separately and never fail
   the gate; that is what "genuinely cannot know, statically" means.

2. UNIT LINT. Over the same six files plus src/app/app_plugin_drive_events.c
   (core-events' side of the ring): flag any statement that mixes an
   identifier containing "tick" with a `world->cycle` read, without
   `APP_SERVER_TICK_LOGIC_CYCLES` appearing in the same statement. That is
   the exact confusion (client logic cycles, 20 ms, vs server ticks, 600 ms)
   that made every verb in this project look dead for two sessions -- see
   docs/QUEST_DRIVER_REMAINING.md, "THE BUG THAT WAS JUST FIXED". Comments
   are stripped first (with their characters blanked, not removed, so line
   numbers stay right) so the bug's own explanatory comments -- which use
   both words on purpose -- cannot self-trigger the lint. The bug is fixed on
   this tree, so this must print zero hits; if it ever does not, that is
   either a second real instance or a bug in this lint, and this script says
   which it believes by construction (a hit is always a real statement, never
   a comment).

Exit status: non-zero on any ABI mismatch (arity, return-count, registered
nowhere) or any unit-lint hit. "Nothing calls" a registration and every
"unknown" are printed but do not fail the gate.

Usage: tools/quest_gate/check_drive_abi.py [--verbose]
"""

import argparse
import glob
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PLUGIN_DIR = os.path.join(REPO_ROOT, "src", "plugin")
APP_DIR = os.path.join(REPO_ROOT, "src", "app")
DRIVER_LUA_DIR = os.path.join(REPO_ROOT, "script", "plugins", "quest_driver")

C_REG_GLOB = os.path.join(PLUGIN_DIR, "torirs_plugin_drive*.c")
DRIVE_EVENTS_C = os.path.join(APP_DIR, "app_plugin_drive_events.c")

REQUIRED_ACCESSORS = (
    "PluginDrive_ArgInt", "PluginDrive_ArgString",
    "luaL_checkinteger", "luaL_checknumber", "luaL_checkstring",
    "luaL_checktype", "luaL_checkudata", "luaL_checkany",
)
OPTIONAL_ACCESSORS = (
    "PluginDrive_ArgOptInt",
    "luaL_optinteger", "luaL_optnumber", "luaL_optstring",
)
# Read a stack index but never raise on a missing arg -- contributes to the
# optional ceiling only, never to the required floor.
PASSIVE_ACCESSORS = ("lua_toboolean", "lua_type")

ALL_ACCESSORS = REQUIRED_ACCESSORS + OPTIONAL_ACCESSORS + PASSIVE_ACCESSORS

# ------------------------------------------------------------- source utils


def strip_comments_keep_lines(text):
    """Blank out comment characters (never delete them) so every later
    file:line stays correct, and so a comment's own prose ("world->cycle
    counts CLIENT logic cycles", "20 ticks expired after 20 cycles" -- the
    bug's OWN explanation, torirs_plugin_drive.c) cannot itself trip the unit
    lint below."""

    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))

    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    text = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)
    return text


def line_of(text, index):
    return text.count("\n", 0, index) + 1


def find_matching_brace(text, open_index):
    """index of the `{` at open_index -> index of its matching `}`."""
    assert text[open_index] == "{"
    depth = 0
    i = open_index
    n = len(text)
    while i < n:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        elif c in "\"'":
            i = skip_string(text, i)
            continue
        i += 1
    return n - 1


def skip_string(text, index):
    """index of an opening quote -> index just past the matching close,
    honouring backslash escapes."""
    quote = text[index]
    i = index + 1
    n = len(text)
    while i < n:
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == quote:
            return i + 1
        i += 1
    return n


def find_matching_paren(text, open_index):
    assert text[open_index] == "("
    depth = 0
    i = open_index
    n = len(text)
    while i < n:
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        elif c in "\"'":
            i = skip_string(text, i)
            continue
        i += 1
    return n - 1


def split_top_level(text):
    """Comma-split `text` (the inside of a paren pair), honouring nested
    parens/brackets/braces and string/char literals. `""` -> []."""
    if text.strip() == "":
        return []
    parts = []
    depth = 0
    start = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            i = skip_string(text, i)
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(text[start:i])
            start = i + 1
        i += 1
    parts.append(text[start:])
    return [p.strip() for p in parts]


# --------------------------------------------------------- C side: registry


class Registration:
    def __init__(self, lua_name, c_func, file_path, line):
        self.lua_name = lua_name
        self.c_func = c_func
        self.file = file_path
        self.line = line
        # Filled in once the function body is located.
        self.def_line = None
        self.required_arity = 0
        self.optional_arity = 0
        self.return_counts = set()   # known literal counts seen
        self.unknown_returns = []    # raw expressions a static read can't classify
        self.found_body = False


REG_ARRAY_RE = re.compile(
    r"static\s+struct\s+LuaFn\s+const\s+LUA_DRIVE_\w+_FNS\s*\[\]\s*=\s*\{(.*?)\n\};",
    re.S,
)
REG_ENTRY_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*([A-Za-z_]\w*)\s*\}')

FUNC_DEF_RE_TEMPLATE = (
    r"static\s+int\s+{name}\s*\(\s*struct\s+lua_State\s*\*\s*L\s*\)\s*\{{"
)

ACCESSOR_RE = re.compile(
    r"\b(" + "|".join(re.escape(f) for f in ALL_ACCESSORS) + r")\s*\(\s*L\s*,\s*(\d+)"
)

RETURN_RE = re.compile(r"\breturn\b\s*([^;]*);")


def parse_registrations():
    """file -> list[Registration], scanning every torirs_plugin_drive*.c."""
    registrations = []
    for path in sorted(glob.glob(C_REG_GLOB)):
        with open(path, "r", encoding="utf-8") as handle:
            raw = handle.read()
        stripped = strip_comments_keep_lines(raw)
        for array_match in REG_ARRAY_RE.finditer(stripped):
            body = array_match.group(1)
            body_start = array_match.start(1)
            for entry in REG_ENTRY_RE.finditer(body):
                lua_name, c_func = entry.group(1), entry.group(2)
                entry_line = line_of(stripped, body_start + entry.start())
                registrations.append(Registration(lua_name, c_func, path, entry_line))
        _locate_bodies(path, raw, stripped, [r for r in registrations if r.file == path])
    return registrations


def _locate_bodies(path, raw, stripped, regs_in_file):
    by_func = {}
    for reg in regs_in_file:
        by_func.setdefault(reg.c_func, []).append(reg)
    for c_func, regs in by_func.items():
        pattern = re.compile(FUNC_DEF_RE_TEMPLATE.format(name=re.escape(c_func)))
        match = pattern.search(stripped)
        if not match:
            # Not every registered name is necessarily a local static thunk
            # (none observed in this tree are anything else, but a future
            # group could register a shared helper) -- leave found_body
            # False and let the caller report it under "unknown".
            continue
        def_line = line_of(stripped, match.start())
        open_brace = match.end() - 1
        close_brace = find_matching_brace(stripped, open_brace)
        body = stripped[open_brace + 1 : close_brace]
        required_arity, optional_arity = _scan_arity(body)
        return_counts, unknown_returns = _scan_returns(body)
        for reg in regs:
            reg.def_line = def_line
            reg.required_arity = required_arity
            reg.optional_arity = optional_arity
            reg.return_counts = return_counts
            reg.unknown_returns = unknown_returns
            reg.found_body = True


def _scan_arity(body):
    required = 0
    optional = 0
    for match in ACCESSOR_RE.finditer(body):
        func, index = match.group(1), int(match.group(2))
        if func in REQUIRED_ACCESSORS:
            required = max(required, index)
            optional = max(optional, index)
        elif func in OPTIONAL_ACCESSORS:
            optional = max(optional, index)
        else:  # PASSIVE_ACCESSORS
            optional = max(optional, index)
    return required, optional


def _scan_returns(body):
    known = set()
    unknown = []
    for match in RETURN_RE.finditer(body):
        expr = match.group(1).strip()
        if expr.startswith("PluginDrive_PushResult"):
            known.add(2)
        elif re.fullmatch(r"-?\d+", expr):
            known.add(int(expr))
        elif expr.startswith("luaL_error") or expr.startswith("luaL_argerror"):
            continue  # longjmps; never actually returns a value to Lua
        elif expr.startswith("lua_yield"):
            continue  # suspends; the eventual resume carries the real values
        elif expr == "":
            continue  # bare `return;` -- not expected on an int thunk
        else:
            unknown.append(expr)
    return known, unknown


# -------------------------------------------------------------- Lua side


class CallSite:
    def __init__(self, name, file_path, line, arg_count, destructure_kind, destructure_count):
        self.name = name
        self.file = file_path
        self.line = line
        self.arg_count = arg_count
        self.destructure_kind = destructure_kind  # "assign" | "bare" | "tail" | "unknown"
        self.destructure_count = destructure_count  # int or None


CALL_RE = re.compile(r"api_drive\.([A-Za-z_]\w*)\s*\(")
ASSIGN_LHS_RE = re.compile(
    r"([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*(?<![=<>~!])=(?!=)\s*$"
)
RETURN_TAIL_RE = re.compile(r"\breturn\s*$")


def parse_call_sites():
    sites = []
    for path in sorted(glob.glob(os.path.join(DRIVER_LUA_DIR, "*.lua"))):
        with open(path, "r", encoding="utf-8") as handle:
            raw = handle.read()
        stripped = _strip_lua_comments(raw)
        for match in CALL_RE.finditer(stripped):
            name = match.group(1)
            open_paren = match.end() - 1
            close_paren = find_matching_paren(stripped, open_paren)
            args_text = stripped[open_paren + 1 : close_paren]
            arg_count = len(split_top_level(args_text))
            line_start = stripped.rfind("\n", 0, match.start()) + 1
            prefix = stripped[line_start : match.start()]
            kind, count = _classify_destructure(prefix)
            line_no = line_of(stripped, match.start())
            sites.append(CallSite(name, path, line_no, arg_count, kind, count))
    return sites


def _strip_lua_comments(text):
    """Blank `--[[ ... ]]` block comments and `--` line comments, keeping
    line numbers intact. No file in script/plugins/quest_driver uses `--` or
    `[[` inside a string literal that this would misread."""

    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))

    text = re.sub(r"--\[\[.*?\]\]", blank, text, flags=re.S)
    text = re.sub(r"--[^\n]*", lambda m: " " * len(m.group(0)), text)
    return text


def _classify_destructure(prefix):
    trimmed = prefix.strip()
    if trimmed == "":
        return "bare", 0
    if RETURN_TAIL_RE.search(trimmed):
        return "tail", None
    assign = ASSIGN_LHS_RE.search(trimmed)
    if assign:
        names = [n.strip() for n in assign.group(1).split(",")]
        return "assign", len(names)
    return "unknown", None


# -------------------------------------------------------------- unit lint


TICK_IDENT_RE = re.compile(r"\b\w*[Tt]ick\w*\b")
WORLD_CYCLE_RE = re.compile(r"world\s*->\s*cycle\b")
CONVERSION_NAME = "APP_SERVER_TICK_LOGIC_CYCLES"
STATEMENT_BOUNDARY_RE = re.compile(r"[;{}]")


def unit_lint():
    """Every `world->cycle` read in the driver's own files, with the
    smallest enclosing simple-statement window (back to the previous
    `;`/`{`/`}`, forward to the next) checked for a `tick`-named identifier
    with no `APP_SERVER_TICK_LOGIC_CYCLES` conversion in that same window."""
    hits = []
    files = sorted(glob.glob(C_REG_GLOB)) + [DRIVE_EVENTS_C]
    for path in files:
        with open(path, "r", encoding="utf-8") as handle:
            raw = handle.read()
        stripped = strip_comments_keep_lines(raw)
        for match in WORLD_CYCLE_RE.finditer(stripped):
            before = stripped.rfind(";", 0, match.start())
            before = max(before, stripped.rfind("{", 0, match.start()))
            before = max(before, stripped.rfind("}", 0, match.start()))
            window_start = before + 1 if before >= 0 else 0
            after_match = STATEMENT_BOUNDARY_RE.search(stripped, match.end())
            window_end = after_match.start() if after_match else len(stripped)
            window = stripped[window_start:window_end]
            if CONVERSION_NAME in window:
                continue
            tick_hit = TICK_IDENT_RE.search(window)
            if not tick_hit or tick_hit.group(0) == CONVERSION_NAME:
                continue
            hits.append((path, line_of(stripped, match.start()), tick_hit.group(0), window.strip()))
    return hits


# -------------------------------------------------------------------- main


def relpath(path):
    return os.path.relpath(path, REPO_ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--verbose", action="store_true",
                         help="also print every known-good call site and registration")
    arguments = parser.parse_args()

    registrations = parse_registrations()
    by_name = {}
    for reg in registrations:
        by_name.setdefault(reg.lua_name, []).append(reg)

    duplicate_names = sorted(n for n, regs in by_name.items() if len(regs) > 1)
    for name in duplicate_names:
        locs = ", ".join("%s:%d" % (relpath(r.file), r.line) for r in by_name[name])
        print("DUPLICATE REGISTRATION: api_drive.%s registered more than once: %s" % (name, locs))

    call_sites = parse_call_sites()
    calls_by_name = {}
    for site in call_sites:
        calls_by_name.setdefault(site.name, []).append(site)

    registered_names = set(by_name)
    called_names = set(calls_by_name)

    arity_mismatches = []
    return_mismatches = []
    unknown_notes = []

    for name in sorted(called_names & registered_names):
        reg = by_name[name][0]
        for site in calls_by_name[name]:
            if not reg.found_body:
                unknown_notes.append(
                    "%s:%d: api_drive.%s -- C definition for %s not located statically (registered %s:%d)"
                    % (relpath(site.file), site.line, name, reg.c_func, relpath(reg.file), reg.line))
                continue
            if site.arg_count < reg.required_arity:
                arity_mismatches.append(
                    "%s:%d: api_drive.%s(...) passes %d argument(s), but %s (%s:%d) "
                    "requires at least %d"
                    % (relpath(site.file), site.line, name, site.arg_count,
                       reg.c_func, relpath(reg.file), reg.def_line, reg.required_arity))

            if len(reg.return_counts) == 1:
                known_return = next(iter(reg.return_counts))
                if site.destructure_kind == "assign" and site.destructure_count > known_return:
                    return_mismatches.append(
                        "%s:%d: api_drive.%s(...) destructures %d value(s), but %s "
                        "(%s:%d) returns %d"
                        % (relpath(site.file), site.line, name, site.destructure_count,
                           reg.c_func, relpath(reg.file), reg.def_line, known_return))
            elif len(reg.return_counts) == 0:
                unknown_notes.append(
                    "%s (%s:%d): every return is a raise/yield a static read excludes -- "
                    "return count unknown"
                    % (reg.c_func, relpath(reg.file), reg.def_line))
            else:
                unknown_notes.append(
                    "%s (%s:%d): returns %s on different paths -- return count unknown"
                    % (reg.c_func, relpath(reg.file), reg.def_line,
                       sorted(reg.return_counts)))
            if reg.unknown_returns:
                for expr in reg.unknown_returns:
                    unknown_notes.append(
                        "%s (%s:%d): `return %s;` -- return count for this path unknown"
                        % (reg.c_func, relpath(reg.file), reg.def_line, expr))

            if site.destructure_kind in ("tail", "unknown"):
                unknown_notes.append(
                    "%s:%d: api_drive.%s(...) -- %s context, destructure count unknown"
                    % (relpath(site.file), site.line, name, site.destructure_kind))

    registered_nowhere = []
    for name in sorted(called_names - registered_names):
        for site in calls_by_name[name]:
            registered_nowhere.append(
                "%s:%d: api_drive.%s(...) -- no {\"%s\", ...} entry in any "
                "LUA_DRIVE_*_FNS array" % (relpath(site.file), site.line, name, name))

    nothing_calls = []
    for name in sorted(registered_names - called_names):
        for reg in by_name[name]:
            nothing_calls.append(
                "%s:%d: {\"%s\", %s} -- no api_drive.%s(...) call site in "
                "script/plugins/quest_driver/*.lua"
                % (relpath(reg.file), reg.line, name, reg.c_func, name))

    lint_hits = unit_lint()

    print("check_drive_abi: %d registration(s) across %d file(s), %d call site(s)"
          % (len(registrations), len(set(r.file for r in registrations)), len(call_sites)))
    print("")

    def section(title, rows):
        print("%s (%d)" % (title, len(rows)))
        for row in rows:
            print("  " + row)
        print("")

    section("ARITY MISMATCHES", arity_mismatches)
    section("RETURN-COUNT MISMATCHES", return_mismatches)
    section("REGISTERED NOWHERE", registered_nowhere)
    section("NOTHING CALLS (informational, does not fail the gate)", nothing_calls)
    section("UNKNOWN (informational, does not fail the gate)", unknown_notes)

    print("UNIT LINT: tick identifier mixed with world->cycle, no %s (%d)"
          % (CONVERSION_NAME, len(lint_hits)))
    for path, lineno, ident, window in lint_hits:
        print("  %s:%d: `%s` beside `world->cycle` with no %s -- %s"
              % (relpath(path), lineno, ident, CONVERSION_NAME, window))
    print("")

    if arguments.verbose:
        print("all registrations:")
        for reg in registrations:
            print("  %s -> %s (%s:%d) required=%d optional=%d returns=%s"
                  % (reg.lua_name, reg.c_func, relpath(reg.file), reg.line,
                     reg.required_arity, reg.optional_arity, sorted(reg.return_counts)))
        print("")
        print("all call sites:")
        for site in call_sites:
            print("  %s:%d api_drive.%s args=%d destructure=%s(%s)"
                  % (relpath(site.file), site.line, site.name, site.arg_count,
                     site.destructure_kind, site.destructure_count))
        print("")

    hard_failures = len(arity_mismatches) + len(return_mismatches) + \
        len(registered_nowhere) + len(duplicate_names) + len(lint_hits)

    if hard_failures == 0:
        print("check_drive_abi: PASS (0 arity/return/registration mismatches, "
              "0 unit-lint hits)")
        return 0
    print("check_drive_abi: FAIL (%d mismatch(es)/lint hit(s) above)" % hard_failures,
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
