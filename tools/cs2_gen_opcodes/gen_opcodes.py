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
    4000, 4001, 4002, 4003, 4011, 4014, 4015,  # math
    4010,  # TESTBIT (int stack)
    3408,  # ENUM - complex, host for now
}

INT8_OPERAND = {21, 38, 39}  # RETURN uses int8 offset; also >= 100


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
        f" * rev-239 dispatch: Statics.method6889 -> {group.deob_handler}.",
    ]
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
    validate_group_coverage(entries)
    validate_dispatch_grouping(entries)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "cs2_opcode.h").write_text(emit_header(entries), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_meta.h").write_text(emit_meta_h(), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_meta.c").write_text(emit_meta_c(entries), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_groups.json").write_text(emit_groups_json(), encoding="utf-8")
    RSCACHE_OUT_DIR.mkdir(parents=True, exist_ok=True)
    (RSCACHE_OUT_DIR / "dat2a_cs2_opcode_decode.h").write_text(
        emit_rscache_decode_h(), encoding="utf-8"
    )
    (RSCACHE_OUT_DIR / "dat2a_cs2_opcode_decode.c").write_text(
        emit_rscache_decode_c(entries), encoding="utf-8"
    )
    print(f"generated {len(entries)} opcodes -> {OUT_DIR}")
    print(f"generated rscache decode operands -> {RSCACHE_OUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
