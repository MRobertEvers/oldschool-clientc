#!/usr/bin/env python3
"""Generate CS2 opcode tables.

Names come from the rev-239 client's own command declarations (catalogue.py),
layered over the older vendored RuneStar Opcodes.kt for the ids the client does
not declare; LOCAL_NAMES may only name an id the catalogue does not cover.
"""

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
HOST_REQUEST_KINDS = OUT_DIR / "cs2vm2_host_request_kinds.def"

import canonical
import catalogue
from canonical import Instruction
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
    CANONICAL_ONLY_GROUPS,
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
    8003,  # ARRAY_SIZE (VM-owned array handle)
    8010, 8011,  # ARRAY_FILL, ARRAY_GENERATERANGE (VM-owned arrays)
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
    """Resolve one canonical name per opcode id, sorted by id.

    Precedence, lowest to highest:

      vendor Opcodes.kt   -- RuneStar's 2021 table; a placeholder (_NNNN) or an
                             older label for most of what it names.
      catalogue.names()   -- the rev-239 client's own declarations. Authoritative
                             for every id it covers.
      LOCAL_NAMES         -- only for ids the catalogue does NOT cover: the RS2
                             dialect's own ids, the RuneLite callback, and ids
                             this client dispatches that the catalogue lists as
                             a gap. validate_local_names() refuses anything else.
    """
    names: dict[int, str] = {val: name for name, val in entries}
    names.update(catalogue.names())
    names.update(LOCAL_NAMES)
    return sorted(((name, val) for val, name in names.items()), key=lambda e: (e[1], e[0]))


def validate_local_names() -> None:
    """LOCAL_NAMES may not rename an id the client itself declares.

    That is how 200 names came to disagree with the client: each overlay was a
    reasonable guess from call sites, and nothing ever compared the guess with
    the declaration. Some were only spelling (DIV for `divide`); others named a
    different operation at the right arity (1140 was CC_INPUT_SETFOCUS,
    and is `cc_input_setfocus`), which fails silently -- the value lands on the
    wrong property.
    """
    declared = catalogue.names()
    wrong = [
        f"{val}: LOCAL_NAMES says {name}, the client declares {declared[val]}"
        for val, name in sorted(LOCAL_NAMES.items())
        if val in declared and declared[val] != name
    ]
    # The same failure from the other side: a local name for an id the client
    # does not declare, spelled like a command the client declares elsewhere.
    # 1004 was CC_SETPINCH; the client's cc_setpinch is 1309.
    owner = {name: val for val, name in declared.items()}
    wrong += [
        f"{val}: LOCAL_NAMES says {name}, which the client declares as {owner[name]}"
        for val, name in sorted(LOCAL_NAMES.items())
        if val not in declared and name in owner
    ]
    if wrong:
        raise ValueError("LOCAL_NAMES disagrees with the client's command catalogue:\n  " + "\n  ".join(wrong))


def retired_names(entries: list[tuple[str, int]]) -> dict[str, str]:
    """Every name that used to be canonical and no longer is -> its replacement.

    `_NNNN` placeholders are not listed; they are resolved by id wherever they are
    read. The map is emitted as cs2_opcode_renames.gen.json so the decompiler's
    compiler keeps accepting the old spellings in existing sources.
    """
    canonical = {val: name for name, val in merge_local(entries)}
    old: dict[str, str] = {}
    for name, val in entries:
        if not re.fullmatch(r"_\d+", name) and canonical.get(val, name) != name:
            old[name] = canonical[val]
    return dict(sorted(old.items()))


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


def canonical_name_of(name: str) -> str:
    """The canonical instruction an OldSchool (rev 231+) wire command runs as:
    its own name, or the newest version of a versioned family."""

    for versions in canonical.VERSIONED.values():
        if versions[-1].rsplit("_V", 1)[0] == name:
            return versions[-1]
    return name


def dispatch_body(path: Path = DISPATCH_SOURCE) -> str:
    """The text of the VM's per-instruction dispatch (CS2VM2_RunOp's switch)."""

    text = path.read_text(encoding="utf-8")
    try:
        start = text.index("\nCS2VM2_RunOp(")
        end = text.index("\n/* Fills *int_args", start)
    except ValueError as error:
        raise ValueError(f"cannot locate CS2VM2_RunOp in {path}") from error
    return text[start:end]


def all_group_slugs() -> list[tuple[str, str]]:
    """(enum name, slug) of every group, ranges first, in canonical order."""

    return [(g.enum_name, g.slug) for g in OPCODE_GROUPS] + [
        (enum_name, slug) for enum_name, slug, _ in CANONICAL_ONLY_GROUPS
    ]


def validate_dispatch_grouping(instructions: list[Instruction]) -> None:
    """Keep the hand-written runtime switch in the generated group order.

    Every `case CS2_OP_X:` must sit under the banner of X's group, and the
    banners must appear in canonical order (the jit's group has no cases in
    this switch: its instructions are the interpreter loop's own)."""

    run_op = dispatch_body()
    group_by_name = {f"CS2_OP_{ins.name}": ins.group for ins in instructions}
    by_wire = {ins.wire: ins for ins in instructions if ins.wire is not None and not ins.rs2}
    for opcode, aliases in LOCAL_ALIASES.items():
        for alias in aliases:
            group_by_name[f"CS2_OP_{alias}"] = by_wire[opcode].group
    slug_to_enum = {slug: enum_name for enum_name, slug in all_group_slugs()}

    seen_markers: list[str] = []
    current_group: str | None = None
    marker_pattern = re.compile(r"/\* === CS2 opcode group: ([a-z0-9-]+) \(")
    case_pattern = re.compile(r"\bcase\s+(CS2_OP_[A-Z0-9_]+)\s*:")

    for lineno, line in enumerate(run_op.splitlines(), 1):
        marker = marker_pattern.search(line)
        if marker:
            slug = marker.group(1)
            current_group = slug_to_enum.get(slug)
            if current_group is None:
                raise ValueError(f"unknown dispatch group marker {slug!r} at line {lineno}")
            seen_markers.append(slug)

        for macro in case_pattern.findall(line):
            if macro not in group_by_name:
                raise ValueError(f"dispatch case {macro} is not a canonical instruction")
            expected = group_by_name[macro]
            if current_group != expected:
                raise ValueError(
                    f"dispatch case {macro} is under {current_group or 'none'}, expected {expected}"
                )

    expected_markers = [slug for enum_name, slug in all_group_slugs() if enum_name != "JIT"]
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
    elif not group.deob_handler:
        lines.append(" * Not a client range: the VM's own ids, given at load; no cache spells them.")
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


HEADER_PREAMBLE = """\
/*
 * THE VM'S OWN INSTRUCTION SET. A loaded script holds these, never a cache's
 * wire ids: each dialect (cs2vm2_dialect.h) is a table from its wire ids to
 * these instructions, applied once at load by CS2VM2_ScriptResolveDialect;
 * a wire id the dialect does not define becomes UNDEFINED_WIRE_ID, which does
 * what an instruction with no stack signature does if it is run. The
 * numbering is dense (0 .. CS2_OP_COUNT-1), belongs to the VM alone, and may
 * change whenever this file is regenerated: nothing outside the process may
 * hold a CS2_OP_ value (a log or a record prints the script's wire id,
 * CS2VM2_DialectWireOf, or the name, CS2_OpCode_String).
 *
 * NAMING, one scheme for every instruction:
 *   - an instruction whose meaning is the same in every era keeps the
 *     OldSchool command's name (PUSH_CONSTANT_INT, CC_SETHIDE, INV_GETOBJ);
 *   - an OldSchool command whose stack shape changed between OldSchool eras is
 *     a versioned family, numbered in the order the eras introduced them, and
 *     neither keeps the bare name (CC_CREATE_V1 pops three ints, through rev
 *     229; CC_CREATE_V2 four, from 230);
 *   - a command of the RS2 634 client that is not OldSchool's command at the
 *     same wire id is RS2_<what it does> (RS2_CC_GETPARAM);
 *   - the jit's instructions (the section from CS2_OP_JIT_FIRST) are named by
 *     what they do: a fused instruction joins the names of the instructions
 *     it replaces, in order, with "__" (PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD__POP_INT_LOCAL);
 *     a load-proven one is the instruction's name followed by what was proven
 *     (PUSH_INT_LOCAL_DECLARED: its local is below the script's declared count).
 *
 * ORDER: the OldSchool wire order, so each dispatch group of the reference
 * client is one contiguous range, CS2_OPS_<GROUP>_BEGIN .. _END (exclusive);
 * a versioned family takes its command's place. Then the RS2 634 section,
 * then UNDEFINED_WIRE_ID, then the jit's. CS2_OP_PLAIN_COUNT instructions
 * can appear in a loaded script; the jit's appear only in its overlay
 * (script->run_code).
 */
"""


def emit_header(
    instructions: list[Instruction],
    numbering: int,
    jit_instructions: list[Instruction] | None = None,
) -> str:
    jit_instructions = jit_instructions or []
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py from the rev-239 command catalogue */",
        "#ifndef CS2_OPCODE_H",
        "#define CS2_OPCODE_H",
        "",
        HEADER_PREAMBLE.rstrip("\n"),
        "",
        "enum CS2_Op",
        "{",
    ]
    aliases_by_name: dict[str, tuple[str, ...]] = {}
    by_wire = {ins.wire: ins for ins in instructions if ins.wire is not None and not ins.rs2}
    for wire, aliases in LOCAL_ALIASES.items():
        aliases_by_name[by_wire[wire].name] = aliases
    emitted_spans: set[tuple[int, int]] = set()
    current_only: str | None = None
    only_groups = {enum_name: (slug, summary) for enum_name, slug, summary in CANONICAL_ONLY_GROUPS}
    everything = instructions + jit_instructions
    for value, ins in enumerate(everything):
        if ins.group in only_groups:
            if current_only != ins.group:
                current_only = ins.group
                slug, summary = only_groups[ins.group]
                lines += ["", f"    /* === CS2 opcode group: {slug} (no wire range) ===", f"     * {summary}.", "     */"]
        else:
            grouped = span_for_opcode(ins.wire)
            if grouped:
                group, span = grouped
                key = (span.lo, span.hi)
                if key not in emitted_spans:
                    emitted_spans.add(key)
                    lines.extend(["", *("    " + l for l in format_group_banner(group, span))])
        if ins.wire is not None and not ins.rs2 and ins.wire in SECTION_COMMENTS and ins.version_of is None:
            lines.extend("    " + l for l in SECTION_COMMENTS[ins.wire])
        doc = OPCODE_DOCS.get(ins.doc_name) or (OPCODE_DOCS.get(ins.version_of) if ins.version_of else None)
        if doc:
            lines.extend("    " + l for l in format_opcode_comment(ins.name, doc))
        if ins.wire is None:
            tail = ""
        elif ins.rs2:
            tail = f" /* rs2_634 wire {ins.wire} */"
        else:
            tail = f" /* wire {ins.wire} */"
        lines.append(f"    CS2_OP_{ins.name} = {value},{tail}")
        for alias in aliases_by_name.get(ins.name, ()):
            lines.append(f"    CS2_OP_{alias} = CS2_OP_{ins.name},")
    lines += ["};", ""]
    lines += [
        "/* Instructions a loaded script can hold (its opcodes[]); the jit's follow. */",
        f"#define CS2_OP_PLAIN_COUNT {len(instructions)}",
        "/* Every instruction, the jit's included: what the interpreter switches on. */",
        f"#define CS2_OP_COUNT {len(everything)}",
        "/* Names this exact numbering (canonical.numbering_hash); the jit's blob",
        " * format folds it in, so a regenerated numbering never reads an old blob. */",
        f"#define CS2_OP_NUMBERING_HASH 0x{numbering:08x}u",
        "",
        "/* Each group's instructions: [BEGIN, END). A component group the reference",
        " * client gives two ranges (the active-component CC_ commands and the",
        " * explicit-component IF_ ones) has a range for each. */",
    ]
    for group in OPCODE_GROUPS:
        for idx, span in enumerate(group.spans):
            suffix = "" if len(group.spans) == 1 else ("_CC" if idx == 0 else "_IF")
            members = [i for i, ins in enumerate(instructions) if not ins.rs2 and ins.wire is not None and span.contains(ins.wire)]
            begin = members[0] if members else next(
                (i for i, ins in enumerate(instructions) if ins.rs2 or ins.wire is None or ins.wire >= span.hi), len(instructions)
            )
            end = members[-1] + 1 if members else begin
            lines.append(f"#define CS2_OPS_{group.enum_name}{suffix}_BEGIN {begin}")
            lines.append(f"#define CS2_OPS_{group.enum_name}{suffix}_END {end}")
    for enum_name, _slug, _summary in CANONICAL_ONLY_GROUPS:
        members = [i for i, ins in enumerate(everything) if ins.group == enum_name]
        begin = members[0] if members else len(everything)
        end = members[-1] + 1 if members else begin
        lines.append(f"#define CS2_OPS_{enum_name}_BEGIN {begin}")
        lines.append(f"#define CS2_OPS_{enum_name}_END {end}")
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def emit_meta_h() -> str:
    group_enum = "\n".join(
        f"    CS2_OPCODE_GROUP_{enum_name}," for enum_name, _ in all_group_slugs()
    )
    return f"""/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py */
#ifndef CS2_OPCODE_META_H
#define CS2_OPCODE_META_H

#include <stdint.h>

/* Everything here is indexed by canonical instruction (cs2_opcode.h), except
 * CS2_WireOpcodeGroupOf, which answers for an OldSchool wire id. */

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
    enum CS2_OpcodeGroup group;
    /* The wire id of the command this instruction is: OldSchool's for an
     * OldSchool command (both versions of a versioned family share it), the
     * 634 client's for an RS2_ one, -1 for a jit instruction. Which dialect
     * spells it how is cs2vm2_dialect.h's business; this names the command. */
    int32_t wire;
}};

/* [CS2_OP_COUNT] rows. */
extern const struct CS2_OpcodeMeta cs2_opcode_meta_table[];
extern const int cs2_opcode_meta_table_size;

/* The row of canonical instruction `opcode`; a value outside the set (the
 * VM's -1 for "no instruction", say) reads a row named "_unknown". */
const struct CS2_OpcodeMeta*
cs2_opcode_meta_lookup(int opcode);

enum CS2_OperandKind
cs2_opcode_operand_kind(int opcode);

/* The canonical instruction's name, "_unknown" outside the set. */
const char*
CS2_OpCode_String(int opcode);

/* The group of canonical instruction `opcode`. */
enum CS2_OpcodeGroup
CS2_OpcodeGroupOf(int opcode);

/* The reference client's dispatch group of OldSchool WIRE id `wire`
 * (opcode_groups.py's ranges); UNKNOWN outside them. */
enum CS2_OpcodeGroup
CS2_WireOpcodeGroupOf(int wire);

const char*
CS2_OpcodeGroupName(enum CS2_OpcodeGroup group);

#endif
"""


def emit_group_functions() -> list[str]:
    lines = [
        "enum CS2_OpcodeGroup",
        "CS2_WireOpcodeGroupOf(int wire)",
        "{",
    ]
    for group in OPCODE_GROUPS:
        tests = " || ".join(
            f"(wire >= {span.lo} && wire < {span.hi})" for span in group.spans
        )
        lines.append(f"    if( {tests} )")
        lines.append(f"        return CS2_OPCODE_GROUP_{group.enum_name};")
    lines += [
        "    return CS2_OPCODE_GROUP_UNKNOWN;",
        "}",
        "",
        "enum CS2_OpcodeGroup",
        "CS2_OpcodeGroupOf(int opcode)",
        "{",
        "    return cs2_opcode_meta_lookup(opcode)->group;",
        "}",
        "",
        "const char*",
        "CS2_OpcodeGroupName(enum CS2_OpcodeGroup group)",
        "{",
        "    switch( group )",
        "    {",
    ]
    for enum_name, slug in all_group_slugs():
        lines.append(f'    case CS2_OPCODE_GROUP_{enum_name}: return "{slug}";')
    lines += [
        '    default: return "unknown";',
        "    }",
        "}",
        "",
    ]
    return lines


def instruction_operand_kind(ins: Instruction) -> str:
    if ins.wire is None:
        return "CS2_OPERAND_INT32"
    if ins.rs2:
        return operand_kind(ins.wire)
    return meta_operand_kind(ins.wire)


def instruction_handler_kind(ins: Instruction) -> str:
    if ins.wire is None:
        return "CS2_HANDLER_VM"
    if ins.rs2:
        return "CS2_HANDLER_HOST" if ins.name in canonical.RS2_634_HOST_COMMANDS else "CS2_HANDLER_VM"
    return handler_kind(ins.wire)


def emit_meta_c(instructions: list[Instruction]) -> str:
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py */",
        '#include "cs2_opcode_meta.h"',
        '#include "cs2_opcode.h"',
        "",
        "const int cs2_opcode_meta_table_size = CS2_OP_COUNT;",
        "",
        "const struct CS2_OpcodeMeta cs2_opcode_meta_table[CS2_OP_COUNT] = {",
    ]
    current = None
    for ins in instructions:
        if ins.group != current:
            current = ins.group
            lines.append(f"    /* {ins.group} */")
        lines.append(
            f'    [CS2_OP_{ins.name}] = {{ "{ins.name}", {instruction_operand_kind(ins)}, '
            f"{instruction_handler_kind(ins)}, CS2_OPCODE_GROUP_{ins.group}, "
            f"{ins.wire if ins.wire is not None else -1} }},"
        )
    lines += [
        "};",
        "",
        "static const struct CS2_OpcodeMeta cs2_opcode_meta_unknown = {",
        '    "_unknown", CS2_OPERAND_INT32, CS2_HANDLER_HOST, CS2_OPCODE_GROUP_UNKNOWN, -1',
        "};",
        "",
        "const struct CS2_OpcodeMeta*",
        "cs2_opcode_meta_lookup(int opcode)",
        "{",
        "    if( opcode < 0 || opcode >= CS2_OP_COUNT )",
        "        return &cs2_opcode_meta_unknown;",
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
        "    return cs2_opcode_meta_lookup(opcode)->name;",
        "}",
        "",
    ]
    lines += emit_group_functions()
    return "\n".join(lines)


def emit_dialects_inc(instructions: list[Instruction], dialects: list[canonical.Dialect]) -> str:
    """Every dialect's wire table, for cs2vm2.c (cs2vm2_dialect.h)."""

    index = {ins.name: i for i, ins in enumerate(instructions)}
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py (canonical.py's dialects).",
        " * Not a header: cs2vm2.c includes it once. Each dialect is a complete table",
        " * from the wire ids its caches spell to canonical instructions (sorted by",
        " * wire id, searched at load), and the inverse the prints use. */",
        "",
    ]
    for dialect in dialects:
        lower = dialect.enum_name.lower()
        rows = sorted(dialect.wire_to_name.items())
        to_wire = canonical.canonical_to_wire(dialect)
        lines.append(f"/* {dialect.name}: {dialect.summary}. {len(rows)} wire ids. */")
        lines.append(f"static struct CS2VM2_DialectWire const g_cs2vm2_{lower}_wire[{len(rows)}] = {{")
        for wire, name in rows:
            lines.append(f"    {{ {wire}, CS2_OP_{name} }},")
        lines.append("};")
        lines.append(f"static uint16_t const g_cs2vm2_{lower}_to_wire[CS2_OP_PLAIN_COUNT] = {{")
        for ins in instructions:
            wire = to_wire.get(ins.name)
            if wire is None:
                lines.append(f"    [CS2_OP_{ins.name}] = CS2VM2_DIALECT_NO_WIRE,")
            else:
                lines.append(f"    [CS2_OP_{ins.name}] = {wire},")
        lines.append("};")
        lines.append(f"struct CS2VM2_Dialect const CS2VM2_DIALECT_{dialect.enum_name} = {{")
        lines.append(f'    .name = "{dialect.name}",')
        lines.append(f"    .wire_to_op = g_cs2vm2_{lower}_wire,")
        lines.append(f"    .wire_count = {len(rows)},")
        lines.append(f"    .op_to_wire = g_cs2vm2_{lower}_to_wire,")
        lines.append("};")
        lines.append("")
        assert all(name in index for _, name in rows)
    return "\n".join(lines)


def parse_host_request_rows() -> list[tuple[str, int]]:
    """(kind name, wire opcode) of every cs2vm2_host_request_kinds.def row."""

    text = HOST_REQUEST_KINDS.read_text(encoding="utf-8")
    return [
        (name, int(wire))
        for name, wire in re.findall(
            r"^CS2VM_HOST_REQUEST_KIND\(\s*([A-Za-z0-9_]+)\s*,\s*(\d+)", text, re.M
        )
    ]


def emit_host_kind_of_inc(instructions: list[Instruction]) -> str:
    """Canonical instruction -> request kind + 1, for CS2VM_HostRequestKindOf.

    A .def row is a wire-level command (its name and OldSchool wire id); every
    canonical instruction that is that command asks its kind: the command
    itself, or each version of a versioned one (CC_CREATE_V1 and _V2 both ask
    CC_CREATE). The RS2 634 section asks nothing through this table: the one
    RS2 command that reaches the host names its kind in its body."""

    rows = parse_host_request_rows()
    by_wire: dict[int, list[Instruction]] = {}
    for ins in instructions:
        if ins.wire is not None and not ins.rs2:
            by_wire.setdefault(ins.wire, []).append(ins)
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py from cs2vm2_host_request_kinds.def.",
        " * Not a header: the body of CS2VM_HostRequestKindOf's table (cs2vm2_host.h). */",
    ]
    for kind, wire in rows:
        targets = by_wire.get(wire)
        if not targets:
            raise ValueError(f"host request {kind}: wire {wire} is no canonical instruction")
        expect = {canonical_name_of(kind), kind}
        aliases = set(LOCAL_ALIASES.get(wire, ()))
        if not any(t.name in expect or t.version_of == kind for t in targets) and kind not in aliases:
            raise ValueError(f"host request {kind}: wire {wire} is {[t.name for t in targets]}")
        for target in targets:
            lines.append(f"[CS2_OP_{target.name}] = (uint16_t)(CS2VM_HOST_REQUEST_{kind} + 1),")
    lines.append("")
    return "\n".join(lines)


def emit_host_instruction_inc(instructions: list[Instruction]) -> str:
    """Request kind -> the canonical instruction a native (rev 231+) script asks
    it with, in .def row order, for CS2VM_HostRequestInstruction."""

    by_wire: dict[int, list[Instruction]] = {}
    for ins in instructions:
        if ins.wire is not None and not ins.rs2:
            by_wire.setdefault(ins.wire, []).append(ins)
    lines = [
        "/* Generated by tools/cs2_gen_opcodes/gen_opcodes.py from cs2vm2_host_request_kinds.def.",
        " * Not a header: the body of CS2VM_HostRequestInstruction's table (cs2vm2_host.h). */",
    ]
    for kind, wire in parse_host_request_rows():
        targets = by_wire[wire]
        native = next(t for t in targets if t.version_of is None or canonical_name_of(t.version_of) == t.name)
        lines.append(f"CS2_OP_{native.name}, /* {kind} */")
    lines.append("")
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
            f"        CS2_OP_{canonical_name_of(semantic.name)},",
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
            f"CS2_VM_CORE_DISPATCH_ROW(CS2_OP_{canonical_name_of(semantic.name)}, "
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


def build_canonical(entries: list[tuple[str, int]]):
    """(instructions, dialects, numbering hash), validated."""

    instructions = canonical.build_instructions(entries)
    dialects = canonical.build_dialects(entries)
    canonical.validate(instructions, dialects)
    return instructions, dialects, canonical.numbering_hash(instructions)


def main() -> int:
    if not VENDOR.is_file():
        print(f"missing {VENDOR}", file=sys.stderr)
        return 1
    validate_local_names()
    vendor_entries = parse_opcodes(VENDOR)
    entries = merge_local(vendor_entries)
    validate_documented_names(entries)
    validate_group_coverage(entries)
    instructions, dialects, numbering = build_canonical(entries)
    validate_dispatch_grouping(instructions)
    validate_opcode_semantics(
        entries, DISPATCH_SOURCE, operand_kind, handler_kind, canonical_name_of
    )
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "cs2_opcode.h").write_text(emit_header(instructions, numbering), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_renames.gen.json").write_text(
        json.dumps(retired_names(vendor_entries), indent=2) + "\n", encoding="utf-8"
    )
    (OUT_DIR / "cs2_opcode_meta.h").write_text(emit_meta_h(), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_meta.c").write_text(emit_meta_c(instructions), encoding="utf-8")
    (OUT_DIR / "cs2_opcode_groups.json").write_text(emit_groups_json(), encoding="utf-8")
    (OUT_DIR / "cs2vm2_dialects.gen.inc").write_text(
        emit_dialects_inc(instructions, dialects), encoding="utf-8"
    )
    (OUT_DIR / "cs2vm2_host_request_kind_of.gen.inc").write_text(
        emit_host_kind_of_inc(instructions), encoding="utf-8"
    )
    (OUT_DIR / "cs2vm2_host_request_instruction.gen.inc").write_text(
        emit_host_instruction_inc(instructions), encoding="utf-8"
    )
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
    print(
        f"generated {len(instructions)} canonical instructions ({len(entries)} wire commands, "
        f"{len(dialects)} dialects) -> {OUT_DIR}"
    )
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
