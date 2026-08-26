#!/usr/bin/env python3
"""Generate CS2 opcode tables from vendored RuneStar Opcodes.kt."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VENDOR = ROOT / "vendor" / "Opcodes.kt"
OUT_DIR = ROOT.parent.parent / "src" / "cs2vm2"
RSCACHE_OUT_DIR = ROOT.parent.parent / "src" / "osrs" / "rscache" / "dat2a"
CS2DOM_GENERATED_OUT_DIR = ROOT.parent / "cs2dom" / "src" / "generated"
CS2DOM_COMMANDS_SOURCE = ROOT.parent / "cs2dom" / "src" / "cs2_commands.js"
DISPATCH_SOURCE = OUT_DIR / "cs2vm2.c"

from local_opcodes import (
    DECODE_OPERAND_OVERRIDES,
    HANDLER_OVERRIDES,
    LOCAL_ALIASES,
    LOCAL_NAMES,
    META_OPERAND_OVERRIDES,
    SECTION_COMMENTS,
)
from opcode_docs import OPCODE_DOCS, OpcodeDoc
from opcode_groups import (
    OPCODE_GROUPS,
    OpcodeGroup,
    OpcodeSpan,
    group_for_opcode,
    span_for_opcode,
)
from opcode_semantics import (
    BarrierKind,
    Dialect,
    INTRINSICS,
    OPCODE_SEMANTICS,
    OperandKind,
    ReplayKind,
    StackEffectKind,
    TargetEffect,
    c_enum_suffix,
    validate_opcode_semantics,
)

# Opcodes executed by cs2_runtime.c without host invoke (RuneStar Command.kt core).
VM_OPCODES = {
    0,  # PUSH_CONSTANT_INT
    1, 2,  # PUSH_VAR, POP_VAR
    3,  # PUSH_CONSTANT_STRING
    6, 7, 8, 9, 10, 31, 32,  # branch
    21,  # RETURN
    25, 27,  # varbit
    33, 34, 35, 36,  # locals
    37,  # JOIN_STRING
    38, 39,  # discard
    40,  # GOSUB_WITH_PARAMS
    42, 43, 49, 50,  # varc
    44, 45, 46,  # arrays
    60,  # SWITCH
    74, 76,  # clan push (host read but can be stub)
    3325,  # MOVECOORD (packed signed-i32 arithmetic)
    4000, 4001, 4002, 4003, 4006, 4008, 4011, 4012, 4014, 4015,  # math
    4010,  # TESTBIT (int stack)
    4016, 4017, 4018, 4029,  # state-independent math
    4101, 4103, 4106, 4107, 4111, 4117, 4118, 4119, 4121,  # pure strings
    6518, 6519,  # deterministic client platform constants
    8003,  # ARRAY_LENGTH (VM-owned array handle)
    3408,  # ENUM - complex, host for now
}

INT8_OPERAND = {21, 38, 39}  # RETURN uses int8 offset; also >= 100

# Bytecode records contain a handful of core opcodes which the current
# RuneStar table does not name because CS2VM2 deliberately rejects their long
# stack.  They still need entries in the wire catalogue: a decoder must consume
# their exact operand widths before whole-script backend selection can reject
# the unsupported semantics cleanly.  Opcode 51 is dialect-dependent and 4500
# is the RS2 wire alias translated by engine/cs2_opcode_dialect.c.
WIRE_ONLY_OPCODE_NAMES: dict[int, str] = {
    51: "GET_VARC_LONG_OR_RS2_SWITCH",
    52: "POP_VARC_LONG",
    61: "PUSH_CONSTANT_LONG",
    62: "POP_LONG_DISCARD",
    66: "PUSH_LONG_LOCAL",
    67: "POP_LONG_LOCAL",
    68: "BRANCH_LONG_NOT",
    69: "BRANCH_LONG_EQUALS",
    70: "BRANCH_LONG_LESS_THAN",
    71: "BRANCH_LONG_GREATER_THAN",
    72: "BRANCH_LONG_LESS_THAN_OR_EQUALS",
    73: "BRANCH_LONG_GREATER_THAN_OR_EQUALS",
    4500: "RS2_STRUCT_PARAM",
}

RS2_WIRE_OPCODE_TRANSLATIONS: dict[int, int] = {
    51: 60,
    4500: 6516,
}


def parse_opcodes(path: Path) -> list[tuple[str, int]]:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(r"const val ([A-Z0-9_]+) = (-?\d+)")
    entries: list[tuple[str, int]] = []
    for name, val in pattern.findall(text):
        entries.append((name, int(val)))
    return entries


def merge_local(entries: list[tuple[str, int]]) -> list[tuple[str, int]]:
    """Layer LOCAL_NAMES over the vendor table, sorted by opcode id.

    An id named in LOCAL_NAMES drops its vendor entry: the vendor name is a
    placeholder (_NNNN) or an older label we have since corrected, and keeping
    both would emit two #defines for one opcode.
    """
    merged = [(name, val) for name, val in entries if val not in LOCAL_NAMES]
    merged += [(name, val) for val, name in LOCAL_NAMES.items()]
    merged.sort(key=lambda e: (e[1], e[0]))
    return merged


def validate_documented_names(entries: list[tuple[str, int]]) -> None:
    """A behavior we can document is specific enough to receive a real name."""

    anonymous_docs = [
        (name, opcode)
        for name, opcode in entries
        if re.fullmatch(r"_\d+", name) and name in OPCODE_DOCS
    ]
    if anonymous_docs:
        joined = ", ".join(f"{name} ({opcode})" for name, opcode in anonymous_docs)
        raise ValueError(f"documented opcodes still have placeholder names: {joined}")


def validate_group_coverage(entries: list[tuple[str, int]]) -> None:
    """Every real VM opcode must land in a reference dispatch group."""

    uncovered = sorted(
        {opcode for _, opcode in entries if opcode >= 0 and group_for_opcode(opcode) is None}
    )
    if uncovered:
        joined = ", ".join(str(opcode) for opcode in uncovered)
        raise ValueError(f"opcodes outside the rev-239 dispatch groups: {joined}")


def validate_dispatch_grouping(entries: list[tuple[str, int]]) -> None:
    """Keep the hand-written runtime switch in the generated group order."""

    text = DISPATCH_SOURCE.read_text(encoding="utf-8")
    try:
        start = text.index("\nCS2VM2_RunOp(")
        end = text.index("\n/* Fills *int_args", start)
    except ValueError as error:
        raise ValueError(f"cannot locate CS2VM2_RunOp in {DISPATCH_SOURCE}") from error
    run_op = text[start:end]

    opcode_by_macro = {f"CS2_OP_{name}": opcode for name, opcode in entries}
    for opcode, aliases in LOCAL_ALIASES.items():
        for alias in aliases:
            opcode_by_macro[f"CS2_OP_{alias}"] = opcode

    groups_by_slug = {group.slug: group for group in OPCODE_GROUPS}
    seen_markers: list[str] = []
    current_group: OpcodeGroup | None = None
    marker_pattern = re.compile(r"/\* === CS2 opcode group: ([a-z0-9-]+) \(")
    case_pattern = re.compile(r"\bcase\s+(CS2_OP_[A-Z0-9_]+)\s*:")

    for lineno, line in enumerate(run_op.splitlines(), 1):
        marker = marker_pattern.search(line)
        if marker:
            slug = marker.group(1)
            current_group = groups_by_slug.get(slug)
            if current_group is None:
                raise ValueError(f"unknown dispatch group marker {slug!r} at line {lineno}")
            seen_markers.append(slug)

        for macro in case_pattern.findall(line):
            if macro not in opcode_by_macro:
                raise ValueError(f"dispatch case {macro} is absent from the opcode generator")
            opcode = opcode_by_macro[macro]
            expected = group_for_opcode(opcode)
            if expected is None:
                raise ValueError(f"dispatch case {macro} ({opcode}) has no opcode group")
            if current_group != expected:
                actual = current_group.slug if current_group else "none"
                raise ValueError(
                    f"dispatch case {macro} ({opcode}) is under {actual}, "
                    f"expected {expected.slug}"
                )

    expected_markers = [group.slug for group in OPCODE_GROUPS]
    if seen_markers != expected_markers:
        raise ValueError(
            "CS2VM2_RunOp group markers differ from opcode_groups.py:\n"
            f"  dispatch: {', '.join(seen_markers)}\n"
            f"  expected: {', '.join(expected_markers)}"
        )


def format_group_banner(group: OpcodeGroup, span: OpcodeSpan) -> list[str]:
    label = group.label if len(group.spans) > 1 else span.label
    lines = [
        f"/* === CS2 opcode group: {group.slug} ({label}) ===",
        f" * {group.summary}.",
    ]
    if group.enum_name == "VM_CORE":
        lines.append(" * rev-239 execution: inline in Statics.method4464.")
    else:
        lines.append(f" * rev-239 dispatch: Statics.method6889 -> {group.deob_handler}.")
    if len(group.spans) > 1:
        lines.append(" * The listed ranges deliberately share one component handler.")
    lines.append(" */")
    return lines


def operand_kind(opcode: int) -> str:
    if opcode == 3:
        return "CS2_OPERAND_STRING"
    if opcode in INT8_OPERAND or opcode >= 100:
        return "CS2_OPERAND_INT8"
    return "CS2_OPERAND_INT32"


def meta_operand_kind(opcode: int) -> str:
    return META_OPERAND_OVERRIDES.get(opcode, operand_kind(opcode))


def decode_operand_kind(opcode: int) -> str:
    return DECODE_OPERAND_OVERRIDES.get(opcode, operand_kind(opcode))


def wire_operand_kind(opcode: int) -> str:
    """The operand bytes consumed by datatypes/clientscript.c.

    This is intentionally distinct from both metadata tables.  The native
    decoder handles LCONST first, then applies its structural signed-byte rule
    to every command opcode and RETURN/discard/null, and only then consults the
    generated table.  In particular, old decode-table overrides for opcodes
    3170..3173 and 4122 never affect bytes on the wire.
    """

    if opcode == 61:
        return "int64"
    if opcode >= 100 or opcode in {21, 38, 39, 62, 63}:
        return "int8"
    if opcode == 3:
        return "string"
    return "int32"


def parse_command_opcodes(path: Path = CS2DOM_COMMANDS_SOURCE) -> dict[int, str]:
    """Read the broader generated cache-command catalogue.

    RuneStar's named opcode file is intentionally not exhaustive. The checked-
    in command table is generated from rscache's cs2_command.gen.h and includes
    anonymous-but-shaped cache commands such as 6758/6764 which occur in real
    interface closures. They are decode-known even though no TS behavior has
    been reviewed for them.
    """

    if not path.is_file():
        return {}
    pattern = re.compile(r'^\s*\[(\d+),"([^"]+)"', re.MULTILINE)
    return {int(opcode): name.upper() for opcode, name in pattern.findall(
        path.read_text(encoding="utf-8")
    )}


def handler_kind(opcode: int) -> str:
    if opcode in HANDLER_OVERRIDES:
        return HANDLER_OVERRIDES[opcode]
    if opcode in VM_OPCODES:
        return "CS2_HANDLER_VM"
    return "CS2_HANDLER_HOST"


def format_stack_args(args: tuple[str, ...]) -> tuple[str, str | None]:
    if not args:
        return "-", None
    if len(args) == 1:
        return args[0], None
    top = args[-1]
    return ", ".join(args), f"({top} = top)"


def format_opcode_comment(name: str, doc: OpcodeDoc) -> list[str]:
    stacks = [
        ("int stack in:", format_stack_args(doc.int_in)),
        ("str stack in:", format_stack_args(doc.str_in)),
        ("int stack out:", format_stack_args(doc.int_out)),
        ("str stack out:", format_stack_args(doc.str_out)),
    ]
    label_width = max(len(label) for label, _ in stacks)
    value_width = max(len(value) for _, (value, _) in stacks)

    lines = [f"/* {name} — {doc.summary}." if doc.summary else f"/* {name}"]
    if doc.operand:
        lines.append(f" * operand: {doc.operand}")
    for label, (value, top) in stacks:
        label_pad = " " * (label_width - len(label))
        value_pad = " " * (value_width - len(value))
        suffix = f"  {top}" if top else ""
        lines.append(f" * {label}{label_pad}  {value}{value_pad}{suffix}")
    if doc.notes:
        note_lines = doc.notes.split("\n")
        lines.append(f" * notes: {note_lines[0]}")
        for note_line in note_lines[1:]:
            lines.append(f" *        {note_line}")
    lines.append(" */")
    return [line.rstrip() for line in lines]


def emit_header(entries: list[tuple[str, int]]) -> str:
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py from RuneStar/cs2 Opcodes.kt */",
        "#ifndef CS2_OPCODE_H",
        "#define CS2_OPCODE_H",
        "",
        f"#define CS2_OPCODE_MAX {max(v for _, v in entries)}",
        f"#define CS2_OPCODE_COUNT {len(entries)}",
        "",
    ]
    emitted_banners: set[int] = set()
    emitted_group_spans: set[tuple[int, int]] = set()
    emitted_aliases: set[int] = set()
    for name, val in entries:
        grouped = span_for_opcode(val)
        if grouped:
            group, span = grouped
            span_key = (span.lo, span.hi)
            if span_key not in emitted_group_spans:
                emitted_group_spans.add(span_key)
                lines.extend(["", *format_group_banner(group, span)])
        if val in SECTION_COMMENTS and val not in emitted_banners:
            emitted_banners.add(val)
            lines.extend(SECTION_COMMENTS[val])
        doc = OPCODE_DOCS.get(name)
        if doc:
            lines.extend(format_opcode_comment(name, doc))
        lines.append(f"#define CS2_OP_{name} {val}")
        if val not in emitted_aliases:
            emitted_aliases.add(val)
            for alias in LOCAL_ALIASES.get(val, ()):
                lines.append(f"#define CS2_OP_{alias} {val}")
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def emit_meta_h() -> str:
    group_enum = "\n".join(
        f"    CS2_OPCODE_GROUP_{group.enum_name}," for group in OPCODE_GROUPS
    )
    return f"""/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py */
#ifndef CS2_OPCODE_META_H
#define CS2_OPCODE_META_H

#include <stdint.h>

enum CS2_OperandKind
{{
    CS2_OPERAND_NONE = 0,
    CS2_OPERAND_INT8,
    CS2_OPERAND_INT32,
    CS2_OPERAND_STRING,
}};

enum CS2_HandlerKind
{{
    CS2_HANDLER_VM = 0,
    CS2_HANDLER_HOST = 1,
}};

enum CS2_OpcodeGroup
{{
    CS2_OPCODE_GROUP_UNKNOWN = 0,
{group_enum}
}};

struct CS2_OpcodeMeta
{{
    const char* name;
    enum CS2_OperandKind operand;
    enum CS2_HandlerKind handler;
}};

extern const struct CS2_OpcodeMeta cs2_opcode_meta_table[];
extern const int cs2_opcode_meta_table_size;

const struct CS2_OpcodeMeta*
cs2_opcode_meta_lookup(int opcode);

enum CS2_OperandKind
cs2_opcode_operand_kind(int opcode);

const char*
CS2_OpCode_String(int opcode);

enum CS2_OpcodeGroup
CS2_OpcodeGroupOf(int opcode);

const char*
CS2_OpcodeGroupName(enum CS2_OpcodeGroup group);

#endif
"""


def emit_group_functions() -> list[str]:
    lines = [
        "enum CS2_OpcodeGroup",
        "CS2_OpcodeGroupOf(int opcode)",
        "{",
    ]
    for group in OPCODE_GROUPS:
        tests = " || ".join(
            f"(opcode >= {span.lo} && opcode < {span.hi})" for span in group.spans
        )
        lines.append(f"    if( {tests} )")
        lines.append(f"        return CS2_OPCODE_GROUP_{group.enum_name};")
    lines += [
        "    return CS2_OPCODE_GROUP_UNKNOWN;",
        "}",
        "",
        "const char*",
        "CS2_OpcodeGroupName(enum CS2_OpcodeGroup group)",
        "{",
        "    switch( group )",
        "    {",
    ]
    for group in OPCODE_GROUPS:
        lines.append(f'    case CS2_OPCODE_GROUP_{group.enum_name}: return "{group.slug}";')
    lines += [
        '    default: return "unknown";',
        "    }",
        "}",
        "",
    ]
    return lines


def emit_meta_c(entries: list[tuple[str, int]]) -> str:
    max_id = max(v for _, v in entries)
    id_to_name = {v: n for n, v in entries}
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py */",
        '#include "cs2_opcode_meta.h"',
        "",
        f"const int cs2_opcode_meta_table_size = {max_id + 1};",
        "",
        "const struct CS2_OpcodeMeta cs2_opcode_meta_table[] = {",
    ]
    emitted_group_spans: set[tuple[int, int]] = set()
    for i in range(max_id + 1):
        grouped = span_for_opcode(i)
        if grouped:
            group, span = grouped
            span_key = (span.lo, span.hi)
            if span_key not in emitted_group_spans:
                emitted_group_spans.add(span_key)
                lines.append("")
                lines.append(f"    /* {group.slug}: {group.label} — {group.summary}. */")
        if i in id_to_name:
            name = id_to_name[i]
            opk = meta_operand_kind(i)
            hdk = handler_kind(i)
            lines.append(
                f'    [{i}] = {{ "{name}", {opk}, {hdk} }},'
            )
        else:
            lines.append(
                f'    [{i}] = {{ "_unknown", CS2_OPERAND_INT32, CS2_HANDLER_HOST }},'
            )
    lines += [
        "};",
        "",
        "const struct CS2_OpcodeMeta*",
        "cs2_opcode_meta_lookup(int opcode)",
        "{",
        "    if( opcode < 0 || opcode >= cs2_opcode_meta_table_size )",
        "        return &cs2_opcode_meta_table[0];",
        "    return &cs2_opcode_meta_table[opcode];",
        "}",
        "",
        "enum CS2_OperandKind",
        "cs2_opcode_operand_kind(int opcode)",
        "{",
        "    return cs2_opcode_meta_lookup(opcode)->operand;",
        "}",
        "",
        "const char*",
        "CS2_OpCode_String(int opcode)",
        "{",
    ]
    negative = [(name, val) for name, val in entries if val < 0]
    if negative:
        lines.append("    switch( opcode )")
        lines.append("    {")
        for name, val in sorted(negative, key=lambda e: e[1]):
            lines.append(f'    case {val}: return "{name}";')
        lines.append("    }")
    lines += [
        "    if( opcode < 0 || opcode >= cs2_opcode_meta_table_size )",
        '        return "_unknown";',
        "    return cs2_opcode_meta_table[opcode].name;",
        "}",
        "",
    ]
    lines += emit_group_functions()
    return "\n".join(lines)


def emit_groups_json() -> str:
    groups = [
        {
            "enum": group.enum_name,
            "slug": group.slug,
            "summary": group.summary,
            "deob_handler": group.deob_handler,
            "spans": [{"lo": span.lo, "hi": span.hi} for span in group.spans],
        }
        for group in OPCODE_GROUPS
    ]
    return json.dumps({"groups": groups}, indent=2) + "\n"


def _c_role_array_name(opcode: int, field: str) -> str:
    return f"cs2_semantics_{opcode}_{field}"


def _c_role_array_ref(opcode: int, field: str, roles: tuple[str, ...]) -> str:
    return _c_role_array_name(opcode, field) if roles else "NULL"


def _c_dialect_mask(dialects: tuple[Dialect, ...]) -> str:
    names = [f"CS2_SEM_DIALECT_{c_enum_suffix(dialect.value)}" for dialect in dialects]
    return " | ".join(names) if names else "0"


def emit_semantics_h() -> str:
    operand_enum = "\n".join(
        f"    CS2_SEM_OPERAND_{c_enum_suffix(kind.value)}," for kind in OperandKind
    )
    target_enum = "\n".join(
        f"    CS2_SEM_TARGET_{c_enum_suffix(kind.value)}," for kind in TargetEffect
    )
    barrier_enum = "\n".join(
        f"    CS2_SEM_BARRIER_{c_enum_suffix(kind.value)}," for kind in BarrierKind
    )
    replay_enum = "\n".join(
        f"    CS2_SEM_REPLAY_{c_enum_suffix(kind.value)}," for kind in ReplayKind
    )
    stack_effect_enum = "\n".join(
        f"    CS2_SEM_STACK_EFFECT_{c_enum_suffix(kind.value)}," for kind in StackEffectKind
    )
    intrinsic_enum = "\n".join(
        f"    CS2_SEM_INTRINSIC_{c_enum_suffix(name)}," for name in INTRINSICS
    )
    return f"""/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py.
 * Source of truth: tools/cs2_gen_opcodes/opcode_semantics.py.
 * The production C switch is intentionally not replaced by this foundation yet. */
#ifndef CS2_OPCODE_SEMANTICS_GEN_H
#define CS2_OPCODE_SEMANTICS_GEN_H

#include <stdint.h>

enum CS2_SemanticsOperandKind
{{
{operand_enum}
}};

enum CS2_SemanticsTargetEffect
{{
{target_enum}
}};

enum CS2_SemanticsBarrierKind
{{
{barrier_enum}
}};

enum CS2_SemanticsReplayKind
{{
{replay_enum}
}};

enum CS2_SemanticsStackEffectKind
{{
{stack_effect_enum}
}};

enum CS2_SemanticsDialectMask
{{
    CS2_SEM_DIALECT_CANONICAL = 1 << 0,
    CS2_SEM_DIALECT_RS2_DAT2 = 1 << 1,
}};

enum CS2_SemanticsIntrinsic
{{
{intrinsic_enum}
}};

/* Stack role arrays are ordered bottom-to-top; the last role is popped first. */
struct CS2_OpcodeSemantics
{{
    int32_t opcode;
    char const* name;
    enum CS2_SemanticsOperandKind operand;
    enum CS2_SemanticsStackEffectKind stack_effect;
    uint8_t int_pop_count;
    uint8_t string_pop_count;
    uint8_t int_push_count;
    uint8_t string_push_count;
    char const* const* int_pops;
    char const* const* string_pops;
    char const* const* int_pushes;
    char const* const* string_pushes;
    enum CS2_SemanticsIntrinsic intrinsic;
    enum CS2_SemanticsTargetEffect target_effect;
    enum CS2_SemanticsBarrierKind barrier;
    uint8_t may_yield;
    enum CS2_SemanticsReplayKind replay;
    uint8_t dialect_mask;
}};

extern struct CS2_OpcodeSemantics const cs2_opcode_semantics[];
extern int const cs2_opcode_semantics_count;

struct CS2_OpcodeSemantics const*
CS2_OpcodeSemanticsLookup(int opcode);

#endif
"""


def emit_semantics_c() -> str:
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py.",
        " * Source of truth: tools/cs2_gen_opcodes/opcode_semantics.py. */",
        '#include "cs2_opcode.h"',
        '#include "cs2_opcode_semantics.gen.h"',
        "",
        "#include <stddef.h>",
        "",
    ]
    role_fields = ("int_pops", "string_pops", "int_pushes", "string_pushes")
    for semantic in OPCODE_SEMANTICS:
        for field in role_fields:
            roles = getattr(semantic, field)
            if not roles:
                continue
            values = ", ".join(json.dumps(role) for role in roles)
            lines.append(
                f"static char const* const {_c_role_array_name(semantic.opcode, field)}[] = "
                f"{{ {values} }};"
            )
    lines += [
        "",
        "struct CS2_OpcodeSemantics const cs2_opcode_semantics[] =",
        "{",
    ]
    for semantic in OPCODE_SEMANTICS:
        lines += [
            "    {",
            f"        CS2_OP_{semantic.name},",
            f"        {json.dumps(semantic.name)},",
            f"        CS2_SEM_OPERAND_{c_enum_suffix(semantic.operand.value)},",
            f"        CS2_SEM_STACK_EFFECT_{c_enum_suffix(semantic.stack_effect.value)},",
            f"        {len(semantic.int_pops)},",
            f"        {len(semantic.string_pops)},",
            f"        {len(semantic.int_pushes)},",
            f"        {len(semantic.string_pushes)},",
            f"        {_c_role_array_ref(semantic.opcode, 'int_pops', semantic.int_pops)},",
            f"        {_c_role_array_ref(semantic.opcode, 'string_pops', semantic.string_pops)},",
            f"        {_c_role_array_ref(semantic.opcode, 'int_pushes', semantic.int_pushes)},",
            f"        {_c_role_array_ref(semantic.opcode, 'string_pushes', semantic.string_pushes)},",
            f"        CS2_SEM_INTRINSIC_{c_enum_suffix(semantic.intrinsic)},",
            f"        CS2_SEM_TARGET_{c_enum_suffix(semantic.target_effect.value)},",
            f"        CS2_SEM_BARRIER_{c_enum_suffix(semantic.barrier.value)},",
            f"        {1 if semantic.may_yield else 0},",
            f"        CS2_SEM_REPLAY_{c_enum_suffix(semantic.replay.value)},",
            f"        {_c_dialect_mask(semantic.dialects)},",
            "    },",
        ]
    lines += [
        "};",
        "",
        "int const cs2_opcode_semantics_count =",
        "    (int)(sizeof(cs2_opcode_semantics) / sizeof(cs2_opcode_semantics[0]));",
        "",
        "struct CS2_OpcodeSemantics const*",
        "CS2_OpcodeSemanticsLookup(int opcode)",
        "{",
        "    int lo = 0;",
        "    int hi = cs2_opcode_semantics_count - 1;",
        "    while( lo <= hi )",
        "    {",
        "        int mid = lo + (hi - lo) / 2;",
        "        int value = cs2_opcode_semantics[mid].opcode;",
        "        if( value == opcode )",
        "            return &cs2_opcode_semantics[mid];",
        "        if( value < opcode )",
        "            lo = mid + 1;",
        "        else",
        "            hi = mid - 1;",
        "    }",
        "    return NULL;",
        "}",
        "",
    ]
    return "\n".join(lines)


def emit_core_dispatch_inc() -> str:
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py.",
        " * Source of truth: tools/cs2_gen_opcodes/opcode_semantics.py.",
        " *",
        " * This is an audited dispatch declaration, not an active replacement for",
        " * CS2VM2_RunOp. Define CS2_VM_CORE_DISPATCH_ROW(opcode, intrinsic, handler)",
        " * before including it. The include deliberately has no guard so it can be",
        " * consumed as an X-macro by validation, tracing, and a later generated switch. */",
        "#ifndef CS2_VM_CORE_DISPATCH_ROW",
        '#error "define CS2_VM_CORE_DISPATCH_ROW before including cs2vm2_core_dispatch.gen.inc"',
        "#endif",
        "",
    ]
    for semantic in OPCODE_SEMANTICS:
        intrinsic = INTRINSICS[semantic.intrinsic]
        lines.append(
            f"CS2_VM_CORE_DISPATCH_ROW(CS2_OP_{semantic.name}, "
            f"CS2_SEM_INTRINSIC_{c_enum_suffix(semantic.intrinsic)}, "
            f"{intrinsic.c_handler})"
        )
    lines += ["", "#undef CS2_VM_CORE_DISPATCH_ROW", ""]
    return "\n".join(lines)


def _ts_string_union(values: Sequence[str]) -> str:
    return " | ".join(json.dumps(value) for value in values)


def _ts_readonly_strings(values: tuple[str, ...]) -> str:
    return "[" + ", ".join(json.dumps(value) for value in values) + "]"


def emit_semantics_ts() -> str:
    intrinsic_names = list(INTRINSICS)
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py.",
        " * Source of truth: tools/cs2_gen_opcodes/opcode_semantics.py.",
        " * Do not add executable behavior here; implement these named intrinsics once. */",
        "",
        f"export type CS2OperandKind = {_ts_string_union([kind.value for kind in OperandKind])};",
        f"export type CS2TargetEffect = {_ts_string_union([kind.value for kind in TargetEffect])};",
        f"export type CS2BarrierKind = {_ts_string_union([kind.value for kind in BarrierKind])};",
        f"export type CS2ReplayKind = {_ts_string_union([kind.value for kind in ReplayKind])};",
        f"export type CS2StackEffectKind = {_ts_string_union([kind.value for kind in StackEffectKind])};",
        f"export type CS2Dialect = {_ts_string_union([kind.value for kind in Dialect])};",
        f"export type CS2CoreIntrinsicName = {_ts_string_union(intrinsic_names)};",
        "",
        "export interface CS2CoreInstruction {",
        "    readonly opcode: number;",
        "    readonly intOperand: number;",
        "    readonly stringOperand: string | null;",
        "}",
        "",
        "export interface CS2OpcodeSemantics {",
        "    readonly opcode: number;",
        "    readonly name: string;",
        "    readonly operand: CS2OperandKind;",
        "    readonly stackEffect: CS2StackEffectKind;",
        "    /** Stack roles are bottom-to-top; the last role is popped first. */",
        "    readonly intPops: readonly string[];",
        "    readonly stringPops: readonly string[];",
        "    readonly intPushes: readonly string[];",
        "    readonly stringPushes: readonly string[];",
        "    readonly intrinsic: CS2CoreIntrinsicName;",
        "    readonly targetEffect: CS2TargetEffect;",
        "    readonly barrier: CS2BarrierKind;",
        "    readonly mayYield: boolean;",
        "    readonly replay: CS2ReplayKind;",
        "    readonly dialects: readonly CS2Dialect[];",
        "}",
        "",
        "export interface CS2CoreIntrinsicHandlers<State, Result> {",
    ]
    for name in intrinsic_names:
        lines.append(
            f"    readonly {name}: (state: State, instruction: CS2CoreInstruction) => Result;"
        )
    lines += ["}", "", "export const CS2_OPCODE_SEMANTICS = ["]
    for semantic in OPCODE_SEMANTICS:
        lines += [
            "    {",
            f"        opcode: {semantic.opcode},",
            f"        name: {json.dumps(semantic.name)},",
            f"        operand: {json.dumps(semantic.operand.value)},",
            f"        stackEffect: {json.dumps(semantic.stack_effect.value)},",
            f"        intPops: {_ts_readonly_strings(semantic.int_pops)},",
            f"        stringPops: {_ts_readonly_strings(semantic.string_pops)},",
            f"        intPushes: {_ts_readonly_strings(semantic.int_pushes)},",
            f"        stringPushes: {_ts_readonly_strings(semantic.string_pushes)},",
            f"        intrinsic: {json.dumps(semantic.intrinsic)},",
            f"        targetEffect: {json.dumps(semantic.target_effect.value)},",
            f"        barrier: {json.dumps(semantic.barrier.value)},",
            f"        mayYield: {'true' if semantic.may_yield else 'false'},",
            f"        replay: {json.dumps(semantic.replay.value)},",
            "        dialects: "
            + _ts_readonly_strings(tuple(dialect.value for dialect in semantic.dialects))
            + ",",
            "    },",
        ]
    lines += [
        "] as const satisfies readonly CS2OpcodeSemantics[];",
        "",
        "export interface CS2CoreDispatchDeclaration {",
        "    readonly opcode: number;",
        "    readonly intrinsic: CS2CoreIntrinsicName;",
        "}",
        "",
        "export const CS2_CORE_DISPATCH_DECLARATIONS = [",
    ]
    for semantic in OPCODE_SEMANTICS:
        lines.append(
            f"    {{ opcode: {semantic.opcode}, intrinsic: {json.dumps(semantic.intrinsic)} }},"
        )
    lines += [
        "] as const satisfies readonly CS2CoreDispatchDeclaration[];",
        "",
        "export const CS2_CORE_DISPATCH_BY_OPCODE: Readonly<",
        "    Record<number, CS2CoreIntrinsicName | undefined>",
        "> = Object.freeze({",
    ]
    for semantic in OPCODE_SEMANTICS:
        lines.append(f"    {semantic.opcode}: {json.dumps(semantic.intrinsic)},")
    lines += ["});", ""]
    return "\n".join(lines)


def emit_wire_opcodes_ts(entries: list[tuple[str, int]]) -> str:
    """Emit every opcode whose raw operand width is known to the C decoder.

    This catalogue is wider than the executable TypeScript semantics table on
    purpose.  Decoding and selecting a whole-script backend happen before
    execution, so a known but unsupported opcode must remain decodable without
    being mistaken for an implemented intrinsic.
    """

    by_opcode = {opcode: name for name, opcode in entries if opcode >= 0}
    for opcode, name in parse_command_opcodes().items():
        by_opcode.setdefault(opcode, name)
    by_opcode.update(WIRE_ONLY_OPCODE_NAMES)
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py.",
        " * Operand widths mirror 3rd/rscache/src/datatypes/clientscript.c.",
        " * This is decode metadata, not a declaration of executable support. */",
        "",
        'export type CS2WireOperandKind = "int8" | "int32" | "int64" | "string";',
        "",
        "export interface CS2WireOpcodeMetadata {",
        "    readonly opcode: number;",
        "    readonly name: string;",
        "    readonly operand: CS2WireOperandKind;",
        "}",
        "",
        "export const CS2_WIRE_OPCODE_METADATA = [",
    ]
    for opcode, name in sorted(by_opcode.items()):
        lines.append(
            "    { "
            f"opcode: {opcode}, name: {json.dumps(name)}, "
            f"operand: {json.dumps(wire_operand_kind(opcode))} "
            "},"
        )
    lines += [
        "] as const satisfies readonly CS2WireOpcodeMetadata[];",
        "",
        "export const CS2_WIRE_OPCODE_METADATA_BY_OPCODE: ReadonlyMap<",
        "    number, CS2WireOpcodeMetadata",
        "> = new Map(CS2_WIRE_OPCODE_METADATA.map((row) => [row.opcode, row]));",
        "",
        "export const CS2_RS2_WIRE_OPCODE_TRANSLATIONS: Readonly<Record<",
        "    number, number | undefined",
        ">> = Object.freeze({",
    ]
    for wire_opcode, canonical_opcode in sorted(RS2_WIRE_OPCODE_TRANSLATIONS.items()):
        lines.append(f"    {wire_opcode}: {canonical_opcode},")
    lines += ["});", ""]
    return "\n".join(lines)


def emit_rscache_decode_h() -> str:
    return """/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py */
#ifndef RSCACHE_DAT2A_CS2_OPCODE_DECODE_H
#define RSCACHE_DAT2A_CS2_OPCODE_DECODE_H

#include <stdint.h>

enum RSCache_CS2_OperandKind
{
    RSCACHE_CS2_OPERAND_NONE = 0,
    RSCACHE_CS2_OPERAND_INT8,
    RSCACHE_CS2_OPERAND_INT32,
    RSCACHE_CS2_OPERAND_STRING,
};

enum RSCache_CS2_OperandKind
rscache_cs2_opcode_operand_kind(int opcode);

#endif
"""


def emit_rscache_decode_c(entries: list[tuple[str, int]]) -> str:
    max_id = max(v for _, v in entries)
    id_to_name = {v: n for n, v in entries}
    kind_map = {
        "CS2_OPERAND_NONE": 0,
        "CS2_OPERAND_INT8": 1,
        "CS2_OPERAND_INT32": 2,
        "CS2_OPERAND_STRING": 3,
    }
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py */",
        '#include "dat2a_cs2_opcode_decode.h"',
        "",
        f"static const int rscache_cs2_operand_kind_table_size = {max_id + 1};",
        "",
        "static const uint8_t rscache_cs2_operand_kind_table[] = {",
    ]
    emitted_group_spans: set[tuple[int, int]] = set()
    for i in range(max_id + 1):
        grouped = span_for_opcode(i)
        if grouped:
            group, span = grouped
            span_key = (span.lo, span.hi)
            if span_key not in emitted_group_spans:
                emitted_group_spans.add(span_key)
                lines.append("")
                lines.append(f"    /* {group.slug}: {group.label} — {group.summary}. */")
        if i in id_to_name:
            opk = decode_operand_kind(i)
        else:
            opk = DECODE_OPERAND_OVERRIDES.get(i, "CS2_OPERAND_INT32")
        val = kind_map[opk]
        lines.append(f"    [{i}] = {val},")
    lines += [
        "};",
        "",
        "enum RSCache_CS2_OperandKind",
        "rscache_cs2_opcode_operand_kind(int opcode)",
        "{",
        "    if( opcode < 0 || opcode >= rscache_cs2_operand_kind_table_size )",
        "        return RSCACHE_CS2_OPERAND_INT32;",
        "    return (enum RSCache_CS2_OperandKind)rscache_cs2_operand_kind_table[opcode];",
        "}",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    if not VENDOR.is_file():
        print(f"missing {VENDOR}", file=sys.stderr)
        return 1
    entries = merge_local(parse_opcodes(VENDOR))
    validate_documented_names(entries)
    validate_group_coverage(entries)
    validate_dispatch_grouping(entries)
    validate_opcode_semantics(entries, DISPATCH_SOURCE, operand_kind, handler_kind)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "cs2_opcode.h").write_text(emit_header(entries), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_meta.h").write_text(emit_meta_h(), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_meta.c").write_text(emit_meta_c(entries), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_groups.json").write_text(emit_groups_json(), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_semantics.gen.h").write_text(
        emit_semantics_h(), encoding="utf-8"
    )
    (OUT_DIR / "cs2_opcode_semantics.gen.c").write_text(
        emit_semantics_c(), encoding="utf-8"
    )
    (OUT_DIR / "cs2vm2_core_dispatch.gen.inc").write_text(
        emit_core_dispatch_inc(), encoding="utf-8"
    )
    CS2DOM_GENERATED_OUT_DIR.mkdir(parents=True, exist_ok=True)
    (CS2DOM_GENERATED_OUT_DIR / "cs2_opcode_semantics.ts").write_text(
        emit_semantics_ts(), encoding="utf-8"
    )
    (CS2DOM_GENERATED_OUT_DIR / "cs2_wire_opcodes.ts").write_text(
        emit_wire_opcodes_ts(entries), encoding="utf-8"
    )
    RSCACHE_OUT_DIR.mkdir(parents=True, exist_ok=True)
    (RSCACHE_OUT_DIR / "dat2a_cs2_opcode_decode.h").write_text(
        emit_rscache_decode_h(), encoding="utf-8"
    )
    (RSCACHE_OUT_DIR / "dat2a_cs2_opcode_decode.c").write_text(
        emit_rscache_decode_c(entries), encoding="utf-8"
    )
    print(f"generated {len(entries)} opcodes -> {OUT_DIR}")
    print(
        f"generated {len(OPCODE_SEMANTICS)} explicit VM semantics -> "
        f"{OUT_DIR}, {CS2DOM_GENERATED_OUT_DIR}"
    )
    wire_ids = {opcode for _, opcode in entries if opcode >= 0}
    wire_ids.update(parse_command_opcodes())
    wire_ids.update(WIRE_ONLY_OPCODE_NAMES)
    print(f"generated {len(wire_ids)} wire opcodes -> {CS2DOM_GENERATED_OUT_DIR}")
    print(f"generated rscache decode operands -> {RSCACHE_OUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
