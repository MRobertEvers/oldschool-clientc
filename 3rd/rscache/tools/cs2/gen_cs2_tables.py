#!/usr/bin/env python3
"""Generate src/cs2/cs2_command.gen.h from the vendored RuneStar sources.

Two things come out of `vendor/`:

  Opcodes.kt  -- id <-> name. The decompiler prints the lowercased name for any
                 command it has no special syntax for, so this table is the
                 source spelling of most of the language.
  Command.kt  -- the per-opcode *signature*: how many values it pops, how many
                 it pushes, and the prototype (type + identifier hint) of each.
                 This is what drives both the type solver and the compiler's
                 argument checking.

Both are transcribed, never inferred. Where this repo knows something the
vendored table does not -- opcodes RuneStar left unnamed, or signatures it never
recorded -- the addition goes in local_commands.py and is layered on top here,
so regenerating keeps it rather than silently dropping it.

    python3 3rd/rscache/tools/cs2/gen_cs2_tables.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
VENDOR = HERE / "vendor"
OUT = HERE.parent.parent / "src" / "cs2" / "cs2_command.gen.h"

# The opcode id/name table is shared with the client's CS2 VM, which maintains
# its own vendored copy plus local additions (tools/cs2_gen_opcodes). That copy
# is a strict superset of upstream's -- it names opcodes RuneStar never did --
# so it is read directly rather than duplicated here, keeping one source of
# truth for "what is opcode 105". The local fallback keeps this library
# generating on its own if it is ever extracted from this repo.
REPO_OPCODES = HERE.parents[3] / "tools" / "cs2_gen_opcodes" / "vendor" / "Opcodes.kt"
LOCAL_OPCODES = VENDOR / "Opcodes.kt"

sys.path.insert(0, str(HERE))
from local_commands import LOCAL_BASIC, LOCAL_NAMES  # noqa: E402

# Prototype aliases Command.kt introduces at the top of the file so an enum
# constant and a prototype of the same name can coexist.
PROTO_ALIASES = {
    "_COORD": "COORD",
    "_CLIENTTYPE": "CLIENTTYPE",
    "_ENUM": "ENUM",
    "_MES": "MES",
    "_STAT": "STAT",
}

# Command handler kinds. These mirror the classes in Command.kt; the C side
# switches on them to pick a translate() body.
KINDS = [
    "UNKNOWN",
    "SWITCH",
    "BRANCH",
    "PROC",
    "RETURN",
    "ENUM",
    "JOIN_STRING",
    "DEFINE_ARRAY",
    "PUSH_ARRAY_INT",
    "POP_ARRAY_INT",
    "BRANCH_COMPARE",
    "DISCARD",
    "ASSIGN",
    "BASIC",
    "CLIENTSCRIPT",
    "PARAM",
]


def parse_opcodes(path: Path) -> tuple[dict[str, int], dict[int, str]]:
    """Return (name -> id, id -> lowercase name).

    Opcodes.kt has one genuine collision (`_202` and `_203` are both 202). The
    reference builds its id->name map by iterating the fields in declaration
    order and letting later entries overwrite, so the last name wins; matched
    here rather than "fixed", because the printed opcode name is observable
    output and a divergence would show up as a diff against the gold corpus.
    """
    text = path.read_text(encoding="utf-8")
    by_name: dict[str, int] = {}
    by_id: dict[int, str] = {}
    for name, value in re.findall(r"const val ([A-Za-z0-9_]+) = (-?\d+)", text):
        opcode = int(value)
        by_name[name] = opcode
        by_id[opcode] = name.lower()
    return by_name, by_id


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def split_top_level(s: str) -> list[str]:
    """Split a comma list, ignoring commas inside nested parentheses."""
    out: list[str] = []
    depth = 0
    current = ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        out.append(current.strip())
    return out


def parse_proto_list(s: str) -> list[str]:
    """`listOf(X, Y)` -> ['X', 'Y'], resolving the file-local aliases."""
    s = s.strip()
    m = re.fullmatch(r"listOf\((.*)\)", s, flags=re.S)
    if not m:
        raise ValueError(f"not a listOf: {s!r}")
    inner = m.group(1).strip()
    if not inner:
        return []
    return [PROTO_ALIASES.get(p, p) for p in split_top_level(inner)]


def extract_enum_body(text: str, header_pattern: str) -> str:
    """Body of an `enum class X ... { ... ; }` declaration, up to the `;`."""
    m = re.search(header_pattern, text)
    if not m:
        raise ValueError(f"enum not found: {header_pattern}")
    start = text.index("{", m.end() - 1)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
    body = text[start + 1 : i]
    # The constant list ends at the first top-level `;`.
    depth = 0
    for i, ch in enumerate(body):
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        elif ch == ";" and depth == 0:
            return body[:i]
    return body


def parse_basic(text: str) -> dict[str, tuple[list[str], list[str], bool]]:
    body = extract_enum_body(text, r"enum class Basic\(")
    out: dict[str, tuple[list[str], list[str], bool]] = {}
    for entry in split_top_level(body):
        entry = entry.strip()
        if not entry:
            continue
        m = re.fullmatch(r"([A-Za-z0-9_]+)\((.*)\)", entry, flags=re.S)
        if not m:
            raise ValueError(f"unparsed Basic entry: {entry!r}")
        name = m.group(1)
        parts = split_top_level(m.group(2))
        args = parse_proto_list(parts[0])
        defs = parse_proto_list(parts[1])
        dot = len(parts) > 2 and parts[2].strip() == "true"
        out[name] = (args, defs, dot)
    return out


def parse_plain_enum(text: str, header_pattern: str) -> list[str]:
    body = extract_enum_body(text, header_pattern)
    names = []
    for entry in split_top_level(body):
        entry = entry.strip()
        if not entry:
            continue
        names.append(re.match(r"([A-Za-z0-9_]+)", entry).group(1))
    return names


def parse_valued_enum(text: str, header_pattern: str) -> dict[str, str]:
    body = extract_enum_body(text, header_pattern)
    out: dict[str, str] = {}
    for entry in split_top_level(body):
        entry = entry.strip()
        if not entry:
            continue
        m = re.fullmatch(r"([A-Za-z0-9_]+)\((.*)\)", entry, flags=re.S)
        if not m:
            raise ValueError(f"unparsed entry: {entry!r}")
        out[m.group(1)] = m.group(2).strip()
    return out


def proto_c(name: str) -> str:
    return f"RSCACHE_CS2_PROTO_{name}"


def c_string(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    opcodes_text = REPO_OPCODES if REPO_OPCODES.is_file() else LOCAL_OPCODES
    if not opcodes_text.is_file():
        print(f"no opcode table at {REPO_OPCODES} or {LOCAL_OPCODES}", file=sys.stderr)
        return 1
    print(f"opcodes:  {opcodes_text}")
    print(f"commands: {VENDOR / 'Command.kt'}")
    command_text = strip_comments((VENDOR / "Command.kt").read_text(encoding="utf-8"))

    by_name, by_id = parse_opcodes(opcodes_text)
    for opcode, name in LOCAL_NAMES.items():
        by_id[opcode] = name.lower()
        by_name[name] = opcode

    basic = parse_basic(command_text)
    basic.update(LOCAL_BASIC)
    branch_compare = parse_plain_enum(command_text, r"enum class BranchCompare :")
    discard = parse_valued_enum(command_text, r"enum class Discard\(")
    assign = parse_plain_enum(command_text, r"enum class Assign :")
    clientscript = parse_plain_enum(command_text, r"enum class ClientScript :")
    param = parse_valued_enum(command_text, r"enum class Param\(")

    # opcode id -> (kind, args, defs, dot, extra)
    commands: dict[int, tuple[str, list[str], list[str], bool, str]] = {}

    def put(name: str, kind: str, args=(), defs=(), dot=False, extra="0"):
        if name not in by_name:
            raise ValueError(f"{name} has no opcode id")
        commands[by_name[name]] = (kind, list(args), list(defs), dot, extra)

    put("SWITCH", "SWITCH")
    put("BRANCH", "BRANCH")
    put("GOSUB_WITH_PARAMS", "PROC")
    put("RETURN", "RETURN")
    put("ENUM", "ENUM")
    put("JOIN_STRING", "JOIN_STRING")
    put("DEFINE_ARRAY", "DEFINE_ARRAY")
    put("PUSH_ARRAY_INT", "PUSH_ARRAY_INT")
    put("POP_ARRAY_INT", "POP_ARRAY_INT")

    for name in branch_compare:
        put(name, "BRANCH_COMPARE")
    for name, stack in discard.items():
        st = "RSCACHE_CS2_STACK_STRING" if "STRING" in stack else "RSCACHE_CS2_STACK_INT"
        put(name, "DISCARD", extra=st)
    for name in assign:
        put(name, "ASSIGN")
    for name in clientscript:
        put(name, "CLIENTSCRIPT")
    for name, prototype in param.items():
        put(name, "PARAM", extra=proto_c(PROTO_ALIASES.get(prototype, prototype)))
    for name, (args, defs, dot) in basic.items():
        put(name, "BASIC", args, defs, dot)

    max_id = max(max(by_id), max(commands))

    # Argument and definition prototype lists are pooled into one flat array and
    # referenced by (offset, count), so the per-opcode row stays fixed width.
    pool: list[str] = []
    pool_index: dict[tuple[str, ...], int] = {}

    def intern(protos: list[str]) -> int:
        if not protos:
            return 0
        key = tuple(protos)
        if key in pool_index:
            return pool_index[key]
        offset = len(pool)
        pool.extend(protos)
        pool_index[key] = offset
        return offset

    rows = []
    for opcode in range(max_id + 1):
        name = by_id.get(opcode)
        kind, args, defs, dot, extra = commands.get(opcode, ("UNKNOWN", [], [], False, "0"))
        if name is None and kind == "UNKNOWN":
            rows.append(None)
            continue
        rows.append(
            (
                name,
                kind,
                intern(args),
                len(args),
                intern(defs),
                len(defs),
                dot,
                extra,
            )
        )

    lines: list[str] = []
    lines.append("/*")
    lines.append(" * Generated by 3rd/rscache/tools/cs2/gen_cs2_tables.py -- do not edit.")
    lines.append(" *")
    lines.append(" * Source: tools/cs2/vendor/{Opcodes,Command}.kt (see vendor/VERSION),")
    lines.append(" * layered with tools/cs2/local_commands.py.")
    lines.append(" */")
    lines.append("#ifndef RSCACHE_CS2_COMMAND_GEN_H")
    lines.append("#define RSCACHE_CS2_COMMAND_GEN_H")
    lines.append("")
    lines.append(f"#define RSCACHE_CS2_OPCODE_TABLE_SIZE {max_id + 1}")
    lines.append("")
    lines.append("static const enum RSCache_CS2_ProtoId cs2_proto_pool[] = {")
    if pool:
        for i in range(0, len(pool), 6):
            lines.append("    " + " ".join(proto_c(p) + "," for p in pool[i : i + 6]))
    else:
        lines.append("    RSCACHE_CS2_PROTO_NONE,")
    lines.append("};")
    lines.append("")
    lines.append("static const struct RSCache_CS2_CommandInfo cs2_command_table[] = {")
    for opcode, row in enumerate(rows):
        if row is None:
            continue
        name, kind, arg_off, arg_count, def_off, def_count, dot, extra = row
        name_c = c_string(name) if name else "NULL"
        lines.append(
            f"    [{opcode}] = {{ {name_c}, RSCACHE_CS2_CMD_{kind}, "
            f"{arg_off}, {arg_count}, {def_off}, {def_count}, "
            f"{'true' if dot else 'false'}, {extra} }},"
        )
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="utf-8")

    named = sum(1 for r in rows if r and r[0])
    signed = sum(1 for r in rows if r and r[1] != "UNKNOWN")
    print(f"generated {OUT}")
    print(f"  {named} named opcodes, {signed} with signatures, pool {len(pool)} entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
