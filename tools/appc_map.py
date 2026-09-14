#!/usr/bin/env python3
"""
Measure src/app.c, and gate what may reach into `struct App`.

app.c is the client's orchestrator translation unit. It is large by nature --
App_Init, the frame, the tick and the minimenu verb table genuinely live there
-- but a great deal of what is in it is not orchestration at all: it is a field
family plus the dozen functions that own it, which could be its own module with
its own test.

This script exists so that "app.c got smaller" is a number in a pull request
rather than an impression, and so that a module extracted OUT of app.c cannot
quietly reach back IN.

Two things it does:

  summary   Parse app.c into function bodies and report, per candidate slice,
            how many lines it is, how many distinct `app->` fields it touches,
            and how many sibling slices it calls. A slice that touches no App
            field is already free to leave; a slice whose fields are its own
            prefixed family and nobody else's is one struct away from leaving.

  fields    Parse `struct App` and report, per FIELD FAMILY, how many fields
            and declaration lines it is and which units read it. A family only
            one unit and the constructor touch is a subsystem that happens to
            be declared inside App; one that half the file reads is App's own.
            This is the second pass's measurement: `slices` groups functions,
            `fields` groups state, and what is left in app.c after the rules
            have gone is state.

  check     Fail if a translation unit that is NOT app.c reaches into App.
            A `.u.c` unity fragment is exempt: it is textually part of the
            translation unit that includes it, which is exactly what those are
            for. Anything else naming `struct App` or including "app.h" is a
            module that was extracted in name only.

            The reach-ins that already existed when this gate was written are
            listed in tools/appc_map_baseline.txt. They are not forgiven, they
            are recorded: the gate fails on a file that is NOT in that list,
            and it ALSO fails on a list entry that has since become clean, so
            the baseline can only ever shrink.

            The same file records a ceiling on how many top-level fields
            `struct App` may have, and `check` fails when the struct grows past
            it. The reach-in list cannot see a module that forward-declares
            `struct App` and keeps a pointer; the field count can see the state
            that module would have taken with it.

Usage:
    python3 tools/appc_map.py summary [--source src/app.c] [--json]
    python3 tools/appc_map.py check   [--root src] [--baseline FILE]
    python3 tools/appc_map.py slices  [--source src/app.c] [--top N]
    python3 tools/appc_map.py fields  [--header src/app.h] [--family NAME]

`check` is the gate; it exits 1 on a violation. `summary` always exits 0 --
it reports, it does not judge.
"""

import argparse
import json
import os
import re
import sys

# Handles that every slice legitimately reaches for. A module lifted out of
# app.c takes these as parameters; they are not evidence of coupling to App,
# so the summary reports them separately from the fields a slice owns.
SHARED_HANDLES = {
    "world",
    "scene",
    "tree",
    "host",
    "provider",
    "runner",
    "exec_runner",
    "net",
    "bridge",
    "need_redraw",
}

# Names that look like a call but are not one, or are libc.
NOT_A_CALL = {
    "if", "for", "while", "switch", "return", "sizeof", "assert", "defined",
    "memset", "memcpy", "memmove", "memcmp", "strcmp", "strncmp", "strlen",
    "strchr", "strrchr", "strstr", "strncpy", "strcpy", "strdup", "snprintf",
    "printf", "fprintf", "sprintf", "sscanf", "vsnprintf", "fflush", "puts",
    "fputs", "fopen", "fclose", "fread", "fwrite", "fseek", "ftell",
    "malloc", "calloc", "realloc", "free", "qsort", "getenv", "exit", "abort",
    "abs", "labs", "fabs", "sqrt", "sqrtf", "floor", "ceil", "sin", "cos",
    "atan2", "hypot", "lroundf", "atoi", "strtol", "strtoul", "strtod",
    "va_start", "va_end", "time", "clock", "usleep", "tolower", "toupper",
    "isdigit", "isspace", "isalpha",
}

CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*\(")
FIELD_RE = re.compile(r"\bapp->([A-Za-z_][A-Za-z_0-9]*)")


def parse_functions(path):
    """Every function defined at column 0 in `path`, with its body.

    The file's house style puts the return type on its own line and the name
    at column 0, so a definition is recognisable without a C parser: a bare
    identifier followed by `(` at column 0, whose prototype closes and is
    followed by a `{` at column 0.
    """
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")

    total = len(lines)
    functions = []
    for index, line in enumerate(lines):
        match = re.match(r"^([A-Za-z_][A-Za-z_0-9]*)\(", line)
        if not match or index == 0:
            continue
        previous = lines[index - 1]
        if previous.rstrip().endswith(";") or previous.startswith("#") or not previous.strip():
            continue

        # Walk to the prototype's closing paren, then require a `{` at column 0.
        depth = 0
        close = None
        for scan in range(index, min(index + 40, total)):
            depth += lines[scan].count("(") - lines[scan].count(")")
            if depth == 0 and ")" in lines[scan]:
                close = scan
                break
        if close is None:
            continue
        brace = close + 1
        while brace < total and not lines[brace].strip():
            brace += 1
        if brace >= total or not lines[brace].startswith("{"):
            continue

        end = index
        while end < total and not lines[end].startswith("}"):
            end += 1
        functions.append(
            {
                "name": match.group(1),
                "start": index + 1,
                "end": end + 1,
                "static": previous.startswith("static"),
                "body": "\n".join(lines[index : end + 1]),
            }
        )
    return functions, total


def annotate(functions):
    """Fill in each function's local calls and the App fields it touches."""
    names = {function["name"] for function in functions}
    for function in functions:
        body = function["body"]
        calls = set(CALL_RE.findall(body)) - {function["name"]} - NOT_A_CALL
        function["local_calls"] = sorted(call for call in calls if call in names)
        fields = sorted(set(FIELD_RE.findall(body)))
        function["fields"] = fields
        function["owned_fields"] = [f for f in fields if f not in SHARED_HANDLES]
        function["lines"] = function["end"] - function["start"] + 1
        del function["body"]
    return functions


def slice_key(name):
    """The field family a function belongs to, from its name.

    A crude grouping on purpose: the point is to find families, and a family
    in this file is spelled in the prefix of every name that touches it
    (`app_worldmap_*`, `app_inv_drag_*`, `app_locedit_*`). Anything that does
    not fit lands in its own bucket, which is the honest answer.
    """
    stripped = re.sub(r"^(App_|app_|Task_App|CreateTask_)", "", name)
    stripped = re.sub(r"^([a-z])", lambda m: m.group(1), stripped)
    parts = re.split(r"(?<=[a-z0-9])(?=[A-Z])|_", stripped)
    parts = [part.lower() for part in parts if part]
    if not parts:
        return "other"
    if len(parts) >= 2 and parts[0] in ("world", "plugin", "if", "cs2", "cs1", "ui", "inv"):
        return "_".join(parts[:2])
    return parts[0]


def summarise(path):
    functions, total_lines = parse_functions(path)
    annotate(functions)

    by_name = {function["name"]: function for function in functions}
    slices = {}
    for function in functions:
        key = slice_key(function["name"])
        entry = slices.setdefault(
            key,
            {"slice": key, "functions": 0, "lines": 0, "fields": set(), "owned": set(),
             "calls_out": set(), "public": 0},
        )
        entry["functions"] += 1
        entry["lines"] += function["lines"]
        entry["fields"].update(function["fields"])
        entry["owned"].update(function["owned_fields"])
        if not function["static"]:
            entry["public"] += 1
        for call in function["local_calls"]:
            other = slice_key(by_name[call]["name"])
            if other != key:
                entry["calls_out"].add(other)

    rows = []
    for entry in slices.values():
        rows.append(
            {
                "slice": entry["slice"],
                "functions": entry["functions"],
                "lines": entry["lines"],
                "fields": len(entry["fields"]),
                "owned_fields": len(entry["owned"]),
                "calls_out": len(entry["calls_out"]),
                "public": entry["public"],
            }
        )
    rows.sort(key=lambda row: -row["lines"])

    free = [f for f in functions if not f["fields"]]
    return {
        "source": path,
        "total_lines": total_lines,
        "functions": len(functions),
        "function_lines": sum(f["lines"] for f in functions),
        "public_functions": sum(1 for f in functions if not f["static"]),
        "app_field_touches": sum(len(f["fields"]) for f in functions),
        "app_free_functions": len(free),
        "app_free_lines": sum(f["lines"] for f in free),
        "slices": rows,
    }


def command_summary(args):
    report = summarise(args.source)
    if args.json:
        print(json.dumps(report, indent=1))
        return 0
    print(f"{report['source']}: {report['total_lines']} lines")
    print(f"  functions          {report['functions']} ({report['public_functions']} public)")
    print(f"  lines in functions {report['function_lines']}")
    print(f"  App-free functions {report['app_free_functions']} ({report['app_free_lines']} lines)")
    print(f"  candidate slices   {len(report['slices'])}")
    return 0


def command_slices(args):
    report = summarise(args.source)
    rows = report["slices"][: args.top]
    print(f"{'slice':<26s}{'lines':>7s}{'fns':>5s}{'fields':>8s}{'own':>5s}{'out':>5s}{'pub':>5s}")
    for row in rows:
        print(
            f"{row['slice']:<26s}{row['lines']:>7d}{row['functions']:>5d}"
            f"{row['fields']:>8d}{row['owned_fields']:>5d}{row['calls_out']:>5d}{row['public']:>5d}"
        )
    return 0


# ---------------------------------------------------------------------------
# struct App, as state
# ---------------------------------------------------------------------------

# The handles every slice legitimately reaches for. A module lifted out takes
# these as parameters, so they are not evidence that a field family is App's.
# Wider than SHARED_HANDLES above on purpose: that set is about what a FUNCTION
# borrows, this one about what a SUBSYSTEM borrows, and a subsystem is entitled
# to the registries as well as to the bare pointers.
SHARED_STATE = SHARED_HANDLES | {
    "active_world",
    "app_state",
    "audio",
    "cfg",
    "esync",
    "features",
    "host",
    "interact",
    "invs",
    "painter_buffer",
    "plugins",
    "provider",
    "screen",
    "slots",
    "ui_host",
    "varps",
    "wevs",
    "worldviews",
}

# Construction touches everything by definition, so a field it shares with one
# other unit is still that unit's own.
CONSTRUCTION_UNITS = {"app_construct.u.c", "app_boot.u.c"}

FIELD_DECL_RE = re.compile(
    r"^\s{4}(?:struct |enum |union |const |unsigned |signed |volatile )*"
    r"[A-Za-z_][A-Za-z_0-9 \*]*?[\s\*]([A-Za-z_][A-Za-z_0-9]*)(?:\[[^\]]*\])*\s*;"
)
FIELD_USE_RE = re.compile(r"\bapp(?:->|\.)([A-Za-z_][A-Za-z_0-9]*)")

# Families whose first word is too generic to name a subsystem on its own.
TWO_WORD_FAMILIES = {"world", "plugin", "if", "cs2", "cs1", "ui", "inv"}


def parse_app_fields(header):
    """Top-level fields of `struct App`, with the lines each declaration costs.

    The comment above a field counts toward it: that is what actually leaves
    app.h when the field does, and on this struct the comments are most of it.
    A nested struct or union counts as one field of however many lines it
    spans, because that is how it moves.
    """
    with open(header, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")

    start = next((i for i, line in enumerate(lines) if line == "struct App"), None)
    if start is None:
        return [], 0
    end = next(i for i in range(start, len(lines)) if lines[i] == "};")
    body = lines[start + 2 : end]

    fields = []
    comment_lines = 0
    in_comment = False
    index = 0
    while index < len(body):
        line = body[index]
        stripped = line.strip()
        if in_comment:
            comment_lines += 1
            if "*/" in stripped:
                in_comment = False
            index += 1
            continue
        if stripped.startswith("/*"):
            comment_lines = 1
            if "*/" not in stripped:
                in_comment = True
            index += 1
            continue
        if not stripped:
            comment_lines = 0
            index += 1
            continue
        if (stripped.startswith("struct") or stripped.startswith("union")) and stripped.endswith("{"):
            scan = index
            depth = 0
            while True:
                depth += body[scan].count("{") - body[scan].count("}")
                if depth == 0 and "}" in body[scan]:
                    break
                scan += 1
            named = re.search(r"}\s*([A-Za-z_][A-Za-z_0-9]*)", body[scan])
            fields.append(
                {
                    "name": named.group(1) if named else "(anonymous)",
                    "lines": scan - index + 1 + comment_lines,
                    "declared_at": start + 3 + index,
                }
            )
            comment_lines = 0
            index = scan + 1
            continue
        match = FIELD_DECL_RE.match(line)
        if match:
            fields.append(
                {
                    "name": match.group(1),
                    "lines": 1 + comment_lines,
                    "declared_at": start + 3 + index,
                }
            )
        comment_lines = 0
        index += 1
    return fields, end - start


def field_family(name):
    """The subsystem a field name claims to belong to.

    Same crude prefix rule as slice_key, and for the same reason: a family in
    this struct is spelled in the prefix of every field that belongs to it.
    """
    parts = name.split("_")
    if len(parts) >= 2 and parts[0] in TWO_WORD_FAMILIES:
        return parts[0] + "_" + parts[1]
    return parts[0]


def translation_units(root):
    """Every unit of app.c's translation unit, plus the external unity files.

    The plugin bridge and panel are included BY app.c, so their reaches are
    app.c's reaches; they are listed separately because they are where the
    largest single block of App is actually read.
    """
    units = [os.path.join(root, "app.c")]
    units += sorted(
        os.path.join(root, name)
        for name in os.listdir(root)
        if name.startswith("app_") and name.endswith(".u.c")
    )
    for extra in ("plugin/torirs_plugin_bridge.u.c", "plugin/torirs_plugin_panel.u.c"):
        path = os.path.join(root, extra)
        if os.path.exists(path):
            units.append(path)
    return [unit for unit in units if os.path.exists(unit)]


def field_report(root, header):
    """Who reads what: fields by family, and each unit's own/shared/foreign."""
    fields, declaration_lines = parse_app_fields(header)
    declared = {field["name"] for field in fields}

    readers = {}       # field -> {unit -> {function}}
    unit_fields = {}   # unit -> {field}
    for unit in translation_units(root):
        functions, _ = parse_functions(unit)
        base = os.path.basename(unit)
        touched = set()
        for function in functions:
            for name in set(FIELD_USE_RE.findall(function["body"])):
                touched.add(name)
                readers.setdefault(name, {}).setdefault(base, set()).add(function["name"])
        unit_fields[base] = touched

    def owning_units(name):
        """Units that read `name`, construction excluded -- it reads everything."""
        return {u for u in readers.get(name, {}) if u not in CONSTRUCTION_UNITS}

    families = {}
    for field in fields:
        family = field_family(field["name"])
        entry = families.setdefault(
            family, {"family": family, "fields": [], "lines": 0, "units": set()}
        )
        entry["fields"].append(field["name"])
        entry["lines"] += field["lines"]
        entry["units"] |= owning_units(field["name"])

    rows = []
    for entry in families.values():
        units = entry["units"]
        shared = all(name in SHARED_STATE for name in entry["fields"])
        rows.append(
            {
                "family": entry["family"],
                "fields": len(entry["fields"]),
                "lines": entry["lines"],
                "units": sorted(units),
                "sole_owner": sorted(units)[0] if len(units) == 1 else None,
                "shared": shared,
                "names": sorted(entry["fields"]),
            }
        )
    rows.sort(key=lambda row: -row["lines"])

    units_report = []
    for unit, touched in unit_fields.items():
        own = sorted(
            n for n in touched
            if n in declared and n not in SHARED_STATE and owning_units(n) <= {unit}
        )
        shared = sorted(n for n in touched if n in SHARED_STATE)
        foreign = sorted(
            n for n in touched
            if n in declared and n not in SHARED_STATE and not owning_units(n) <= {unit}
        )
        units_report.append({"unit": unit, "own": own, "shared": shared, "foreign": foreign})
    units_report.sort(key=lambda row: -len(row["own"]))

    return {
        "header": header,
        "field_count": len(fields),
        "declaration_lines": declaration_lines,
        "families": rows,
        "units": units_report,
        "readers": readers,
    }


def command_fields(args):
    report = field_report(args.root, args.header)
    if args.json:
        printable = dict(report)
        printable["readers"] = {
            name: {unit: sorted(functions) for unit, functions in units.items()}
            for name, units in report["readers"].items()
        }
        print(json.dumps(printable, indent=1))
        return 0

    if args.family:
        for row in report["families"]:
            if row["family"] != args.family:
                continue
            print(f"{row['family']}: {row['fields']} fields, {row['lines']} declaration lines")
            for name in row["names"]:
                where = report["readers"].get(name, {})
                summary = ", ".join(
                    f"{unit.replace('.u.c', '').replace('app_', '')}:{len(functions)}"
                    for unit, functions in sorted(where.items(), key=lambda kv: -len(kv[1]))
                )
                print(f"  {name:<40s}{summary}")
            return 0
        print(f"no field family named {args.family!r}", file=sys.stderr)
        return 1

    if args.units:
        for row in report["units"]:
            unit = row["unit"].replace(".u.c", "").replace("app_", "")
            print(
                f"{unit:<22s}own {len(row['own']):>3d}  shared {len(row['shared']):>3d}"
                f"  foreign {len(row['foreign']):>3d}"
            )
            if row["own"]:
                print(f"    own:     {' '.join(row['own'])}")
            if row["foreign"]:
                print(f"    foreign: {' '.join(row['foreign'])}")
        return 0

    print(
        f"{report['header']}: struct App has {report['field_count']} top-level fields "
        f"in {report['declaration_lines']} lines"
    )
    print(f"{'family':<22s}{'fields':>7s}{'decl':>6s}  read from")
    for row in report["families"][: args.top]:
        if row["shared"]:
            where = "(shared handle)"
        elif row["sole_owner"]:
            where = row["sole_owner"].replace(".u.c", "").replace("app_", "") + "  <- subsystem"
        else:
            where = ", ".join(
                unit.replace(".u.c", "").replace("app_", "") for unit in row["units"]
            ) or "(unread)"
        print(f"{row['family']:<22s}{row['fields']:>7d}{row['lines']:>6d}  {where[:74]}")
    return 0


FIELD_CEILING_RE = re.compile(r"^struct-App-fields:\s*(\d+)\s*$")


def read_baseline(path):
    """The recorded reach-ins and the field ceiling.

    Missing file means an empty baseline, which is the strict reading: every
    reach-in is then new, and there is no ceiling to hold the struct under.
    Comments and blank lines are ignored. A `struct-App-fields: N` line is the
    ceiling rather than a path.
    """
    recorded = set()
    ceiling = None
    if not os.path.exists(path):
        return recorded, ceiling
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            match = FIELD_CEILING_RE.match(line)
            if match:
                ceiling = int(match.group(1))
                continue
            recorded.add(os.path.normpath(line))
    return recorded, ceiling


def find_reach_ins(root):
    """Translation units outside app.c that name App, path -> reason."""
    found = {}
    for directory, _subdirectories, filenames in os.walk(root):
        if any(part.startswith("build") or part == "3rd" for part in directory.split(os.sep)):
            continue
        for filename in filenames:
            if not filename.endswith(".c") or filename.endswith(".u.c"):
                continue
            path = os.path.join(directory, filename)
            if os.path.normpath(path) == os.path.normpath(os.path.join(root, "app.c")):
                continue
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            reasons = []
            if re.search(r'^\s*#\s*include\s+"(?:\.\./)*app\.h"', text, re.M):
                reasons.append('includes "app.h"')
            if re.search(r"\bstruct\s+App\b", text):
                reasons.append("names `struct App`")
            if reasons:
                found[os.path.normpath(path)] = ", ".join(reasons)
    return found


def command_check(args):
    """A module outside app.c must not reach into App, and App must not grow.

    Exempt from the first: app.c itself (it owns the struct) and `.u.c` unity
    fragments (they are textually part of whichever translation unit includes
    them).

    The second gate is the field count, and it exists because the first cannot
    see the shape an extraction leaves behind. A module that forward-declares
    `struct App` and keeps a pointer passes the reach-in list -- it includes no
    header and names no type in a way this grep finds -- while the state it
    should have taken sits in app.h exactly where it was. The count sees that,
    and it only ever goes down.
    """
    found = find_reach_ins(args.root)
    recorded, ceiling = read_baseline(args.baseline)

    added = sorted(path for path in found if path not in recorded)
    cleared = sorted(path for path in recorded if path not in found)

    fields, _ = parse_app_fields(os.path.join(args.root, "app.h"))
    grown = ceiling is not None and len(fields) > ceiling
    shrunk = ceiling is not None and len(fields) < ceiling

    if not added and not cleared and not grown and not shrunk:
        print(
            f"appc_map check: {len(found)} recorded reach-ins, no new ones; "
            f"struct App at its recorded {len(fields)} fields "
            f"({args.root}, baseline {args.baseline})"
        )
        return 0

    if added:
        print("appc_map check FAILED -- these reach into App and are not in the baseline:", file=sys.stderr)
        for path in added:
            print(f"  {path}: {found[path]}", file=sys.stderr)
        print(
            "\nA module lifted out of app.c takes what it needs as parameters. If it still\n"
            "needs App, it was renamed, not extracted. A `.u.c` unity fragment is exempt.",
            file=sys.stderr,
        )
    if cleared:
        print(
            "\nappc_map check FAILED -- these baseline entries no longer reach into App.\n"
            "Delete them from the baseline so it keeps meaning what it says:",
            file=sys.stderr,
        )
        for path in cleared:
            print(f"  {path}", file=sys.stderr)
    if grown:
        print(
            f"\nappc_map check FAILED -- struct App has {len(fields)} top-level fields, "
            f"and the baseline records {ceiling}.\n"
            "State added to App is state a subsystem will have to be prised back out of\n"
            "it later. Put it in the subsystem that owns it, or raise the ceiling in the\n"
            "baseline and say in the commit message why this field belongs to nothing.",
            file=sys.stderr,
        )
    if shrunk:
        print(
            f"\nappc_map check FAILED -- struct App is down to {len(fields)} fields and the\n"
            f"baseline still records {ceiling}. Lower it to {len(fields)}, so the number\n"
            "keeps meaning what it says and the next field cannot creep back in for free.",
            file=sys.stderr,
        )
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", required=True)

    summary = subparsers.add_parser("summary", help="one-line size report for app.c")
    summary.add_argument("--source", default="src/app.c")
    summary.add_argument("--json", action="store_true")
    summary.set_defaults(func=command_summary)

    slices = subparsers.add_parser("slices", help="per-slice coupling table")
    slices.add_argument("--source", default="src/app.c")
    slices.add_argument("--top", type=int, default=40)
    slices.set_defaults(func=command_slices)

    field_parser = subparsers.add_parser("fields", help="struct App's field families, and who reads them")
    field_parser.add_argument("--root", default="src")
    field_parser.add_argument("--header", default="src/app.h")
    field_parser.add_argument("--top", type=int, default=40)
    field_parser.add_argument("--family", help="list one family's fields and their readers")
    field_parser.add_argument("--units", action="store_true", help="per unit: own / shared / foreign")
    field_parser.add_argument("--json", action="store_true")
    field_parser.set_defaults(func=command_fields)

    check = subparsers.add_parser("check", help="gate: nothing new outside app.c reaches into App")
    check.add_argument("--root", default="src")
    check.add_argument("--baseline", default="tools/appc_map_baseline.txt")
    check.set_defaults(func=command_check)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
