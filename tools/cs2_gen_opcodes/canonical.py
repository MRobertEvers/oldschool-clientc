"""The VM's own instruction set: canonical instructions, and every dialect's map onto it.

A cache spells a command with a WIRE id, and what a wire id means depends on the
era of the cache (rev 229 CC_CREATE pops three ints, rev 230 four; wire 51 is
SWITCH in the 634 client and GET_VARC_LONG in OldSchool). The interpreter never
sees a wire id. Each dialect is a table from wire id to CANONICAL INSTRUCTION,
applied once when a script is loaded (CS2VM2_ScriptResolveDialect); the loaded
script, the jit, the interpreter's switch and the host request kinds all work
in the canonical numbering, which is dense (0 .. CS2_OP_COUNT-1) and belongs to
the VM alone: no cache spells it and no revision defines it.

Naming (stated again at the top of the generated cs2_opcode.h):

  * An instruction whose meaning is the same in every era keeps the OldSchool
    command's name: PUSH_CONSTANT_INT, CC_SETHIDE, INV_GETOBJ.
  * An OldSchool command whose stack shape changed between OldSchool eras is a
    family of versioned instructions, numbered in the order the eras introduced
    them: CC_CREATE_V1 (three arguments, through rev 229) and CC_CREATE_V2 (the
    is_nested flag, rev 230 on). Neither keeps the bare name, so nothing can
    name "CC_CREATE" without saying which.
  * A command of the RS2 634 client that is not OldSchool's command at the same
    id is RS2_<what it does>: RS2_CC_GETPARAM, RS2_WORLDLIST_SPECIFIC. The
    prefix names the client the command belongs to; the rest names the command.
    Where the 634 deob leaves the command unidentified, the name says what is
    known of it (RS2_GETINT_A pushes an int nobody has identified).
  * The jit's instructions (the section after these, CS2_OP_JIT_FIRST ..) are
    named by what they do; see jit_instructions.py.

The canonical order is the OldSchool wire order, so each dispatch group of the
reference client (opcode_groups.py) is one contiguous range of instructions,
named CS2_OPS_<GROUP>_BEGIN .. _END in the generated header. A versioned family
takes its base command's place. The RS2 634 section follows, then
UNDEFINED_WIRE_ID, then the jit's.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib

from opcode_groups import OPCODE_GROUPS, group_for_opcode


# OldSchool commands whose stack shape changed between OldSchool eras. The wire
# id stays; the dialect picks the version. Index 0 is V1.
VERSIONED: dict[int, tuple[str, ...]] = {
    # Rev 230 added the is_nested flag (script 3953 gains the push at 230).
    100: ("CC_CREATE_V1", "CC_CREATE_V2"),
    # Through rev 230 arrays are numbered and shared by every frame of a run;
    # from 231 an array is a handle in a string local of the frame that defined
    # or received it (script 2255 sorts array 0 recursively with no array
    # argument at 230 and passes it as a string argument at 231).
    44: ("DEFINE_ARRAY_V1", "DEFINE_ARRAY_V2"),
    45: ("PUSH_ARRAY_INT_V1", "PUSH_ARRAY_INT_V2"),
    46: ("POP_ARRAY_INT_V1", "POP_ARRAY_INT_V2"),
}

# The RS2 634 client's commands whose wire id names a different command (or a
# command this VM does not implement for 634) than OldSchool's: wire id ->
# canonical name. Their stack signatures are the 634 client's (opcode_docs.py;
# docs/RS2_634_CLIENT_REFERENCES.md section 3.2); all but RS2_CC_GETPARAM are
# balanced stubs whose results are 0 or "".
RS2_634_COMMANDS: dict[int, str] = {
    202: "RS2_IF_REMOVEFROMGROUP",
    1122: "RS2_CC_SETGRAPHICFLAG",
    1311: "RS2_CC_SETANINT713",
    1613: "RS2_CC_GETPARAM",
    2122: "RS2_IF_SETGRAPHICFLAG",
    2314: "RS2_IF_SETANINT719",
    2703: "RS2_IF_GETDYNAMICCHILDCOUNT",
    3329: "RS2_GETABOOLEAN5233",
    3335: "RS2_GETLANGUAGE",
    3340: "RS2_GETABOOLEAN2329",
    3351: "RS2_GETMOUSEBUTTONS",
    3609: "RS2_FRIEND_TEST",
    3619: "RS2_FRIENDSCHAT_JOINCHAT",
    4124: "RS2_TOSTRING_GROUPED",
    4125: "RS2_STRINGWIDTH",
    5003: "RS2_FRIENDRECORD_GETTEXT_A",
    5004: "RS2_FRIENDRECORD_GETINT_A",
    5010: "RS2_FRIENDRECORD_GETTEXT_B",
    5011: "RS2_FRIENDRECORD_GETTEXT_C",
    5012: "RS2_FRIENDRECORD_GETINT_B",
    5019: "RS2_FRIENDRECORD_GETTEXT_D",
    5024: "RS2_FRIENDRECORD_GETINT_C",
    5056: "RS2_ARRAY_GETLENGTH",
    5102: "RS2_KEY_ISDOWN",
    5420: "RS2_GETINT_A",
    5424: "RS2_CHAT_SETSCROLLBARSKIN",
    5428: "RS2_GETINT_OF2_A",
    5504: "RS2_CAM_FORCEANGLE",
    5505: "RS2_CAM_GETANGLE_XA",
    5506: "RS2_CAM_GETANGLE_YA",
    5507: "RS2_CAM_ACTION_A",
    5508: "RS2_CAM_ACTION_B",
    5509: "RS2_CAM_ACTION_C",
    5510: "RS2_CAM_ACTION_D",
    5547: "RS2_GETINT_B",
    6506: "RS2_WORLDLIST_SPECIFIC",
    6510: "RS2_GETINT_C",
    6900: "RS2_GETINT_D",
}

# RS2 634 wire ids that spell an OldSchool command under another number.
RS2_634_RENUMBERED: dict[int, str] = {
    51: "SWITCH",  # OldSchool moved SWITCH to 60 and reused 51 for PUSH_VARC_LONG
    4500: "STRUCT_PARAM",  # OldSchool 6516; same stack shape
}

# Handler kind of each RS2 634 command (cs2_opcode_meta.c); VM for the stubs.
RS2_634_HOST_COMMANDS: frozenset[str] = frozenset({"RS2_CC_GETPARAM"})

# The group the RS2 634 section belongs to (opcode_groups.CANONICAL_ONLY_GROUPS).
RS2_634_GROUP = "RS2_634"

# What a wire id the script's dialect does not define becomes at load: one
# instruction, after the RS2 634 section, which no dialect maps to. The wire id
# is kept beside the script (CS2VM2_Script.undefined_wires); run, the
# instruction takes the path an instruction with no stack signature takes,
# naming that wire id. A script that holds one on a path it never takes loads
# and runs. The jit never fuses it.
UNDEFINED_WIRE_ID = "UNDEFINED_WIRE_ID"
UNDEFINED_GROUP = "UNDEFINED"


@dataclass(frozen=True)
class Instruction:
    """One canonical instruction."""

    name: str
    # The OldSchool wire id this instruction is (or is a version of); for an
    # RS2 634 command, the 634 wire id. None for a jit instruction and for
    # UNDEFINED_WIRE_ID.
    wire: int | None
    # opcode_groups enum name.
    group: str
    # The opcode_docs key its comment comes from.
    doc_name: str
    # The OldSchool wire command this is a version of (VERSIONED), else None.
    version_of: str | None = None
    # True for the RS2 634 section.
    rs2: bool = False


@dataclass(frozen=True)
class Dialect:
    """One era's wire ids -> canonical instruction names."""

    enum_name: str  # CS2VM2_DIALECT_<enum_name>
    name: str  # stable short name (the jit's dialect tag hashes it)
    summary: str
    wire_to_name: dict[int, str] = field(hash=False, compare=False)
    # For a canonical instruction two wire ids reach, the one prints use.
    preferred_wire: dict[str, int] = field(hash=False, compare=False)


def build_instructions(entries: list[tuple[str, int]]) -> list[Instruction]:
    """The canonical instruction list, in canonical order (see module doc)."""

    out: list[Instruction] = []
    seen: set[str] = set()
    for name, wire in sorted(((n, v) for n, v in entries if v >= 0), key=lambda e: e[1]):
        group = group_for_opcode(wire)
        if group is None:
            raise ValueError(f"wire {wire} ({name}) has no dispatch group")
        if wire in VERSIONED:
            for version in VERSIONED[wire]:
                out.append(Instruction(version, wire, group.enum_name, version, version_of=name))
        else:
            out.append(Instruction(name, wire, group.enum_name, name))
    for wire, name in sorted(RS2_634_COMMANDS.items()):
        out.append(Instruction(name, wire, RS2_634_GROUP, name, rs2=True))
    out.append(Instruction(UNDEFINED_WIRE_ID, None, UNDEFINED_GROUP, UNDEFINED_WIRE_ID))
    for ins in out:
        if ins.name in seen:
            raise ValueError(f"canonical instruction {ins.name} is named twice")
        seen.add(ins.name)
    return out


def build_dialects(entries: list[tuple[str, int]]) -> list[Dialect]:
    """Every dialect CS2_DialectForCache can return, each a full wire table."""

    names_by_wire = {wire: name for name, wire in entries if wire >= 0}

    def base(versions: dict[int, int]) -> dict[int, str]:
        table: dict[int, str] = {}
        for wire, name in names_by_wire.items():
            if wire in VERSIONED:
                table[wire] = VERSIONED[wire][versions[wire]]
            else:
                table[wire] = name
        return table

    arrays_v1 = {44: 0, 45: 0, 46: 0}
    arrays_v2 = {44: 1, 45: 1, 46: 1}

    osrs = base({100: 1, **arrays_v2})
    osrs230 = base({100: 1, **arrays_v1})
    osrs229 = base({100: 0, **arrays_v1})
    rs2 = base({100: 0, **arrays_v2})
    rs2_preferred: dict[str, int] = {}
    for wire, name in RS2_634_RENUMBERED.items():
        rs2[wire] = name
        rs2_preferred[name] = wire
    for wire, name in RS2_634_COMMANDS.items():
        rs2[wire] = name

    return [
        Dialect("OSRS", "osrs", "OldSchool from rev 231: CC_CREATE_V2, handle arrays (V2)", osrs, {}),
        Dialect("OSRS_230", "osrs230", "OldSchool rev 230: CC_CREATE_V2, numbered arrays (V1)", osrs230, {}),
        Dialect("OSRS_229", "osrs229", "OldSchool through rev 229: CC_CREATE_V1, numbered arrays (V1)", osrs229, {}),
        Dialect(
            "RS2_634",
            "rs2_634",
            "RS2 on dat2, verified against the 634 client: CC_CREATE_V1, handle arrays (V2), "
            "two renumbered commands and the RS2_ section",
            rs2,
            rs2_preferred,
        ),
    ]


def canonical_to_wire(dialect: Dialect) -> dict[str, int]:
    """Canonical instruction name -> the wire id prints use, for `dialect`."""

    out: dict[str, int] = {}
    for wire, name in sorted(dialect.wire_to_name.items()):
        out.setdefault(name, wire)
    out.update(dialect.preferred_wire)
    return out


def numbering_hash(instructions: list[Instruction]) -> int:
    """32 bits naming this exact numbering: every instruction's name in order.

    The jit's blobs hold canonical instructions, so a numbering change is a
    format change; the dialect tag folds this in (cs2vm2_jit.c)."""

    digest = hashlib.sha256("\n".join(i.name for i in instructions).encode()).digest()
    return int.from_bytes(digest[:4], "little")


def validate(instructions: list[Instruction], dialects: list[Dialect]) -> None:
    names = {i.name for i in instructions}
    for dialect in dialects:
        missing = sorted({n for n in dialect.wire_to_name.values() if n not in names})
        if missing:
            raise ValueError(f"dialect {dialect.name} maps to unknown instructions: {missing}")
        if any(w < 0 or w > 0xFFFF for w in dialect.wire_to_name):
            raise ValueError(f"dialect {dialect.name} has a wire id outside u16")
    # Every instruction is reachable from some dialect (the jit's are not wire
    # instructions and are not in this list; UNDEFINED_WIRE_ID is what the wire
    # ids no table defines become, and no table may name it).
    reached = {n for d in dialects for n in d.wire_to_name.values()}
    if UNDEFINED_WIRE_ID in reached:
        raise ValueError(f"a dialect maps a wire id to {UNDEFINED_WIRE_ID}")
    unreached = sorted(names - reached - {UNDEFINED_WIRE_ID})
    if unreached:
        raise ValueError(f"canonical instructions no dialect reaches: {unreached}")
    groups = {g.enum_name for g in OPCODE_GROUPS}
    for ins in instructions:
        if ins.group not in groups and ins.group not in (RS2_634_GROUP, UNDEFINED_GROUP):
            raise ValueError(f"{ins.name}: unknown group {ins.group}")
