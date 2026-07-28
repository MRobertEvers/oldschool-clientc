#!/usr/bin/env python3
"""Generate the ServerScript opcode/trigger tables from the LostCity engine.

    python3 src/serverscript/gen_opcode_meta.py [path-to-LostCity_Server]

Outputs (checked in; this never runs at build time):

    ss_opcode.h        SS_OP_* ids, SS_OPCODE_MAX, SS_OPCODE_COUNT
    ss_trigger.h       SS_TRIGGER_* ids, SS_TRIGGER_MAX
    ss_meta.gen.h      opcode-name table, opcode-meta table, trigger-name table

Sources, all inside the reference server:

    engine/src/engine/script/ScriptOpcode.ts          opcode id <-> name
    engine/src/engine/script/ServerTriggerType.ts     trigger id <-> name
    engine/src/engine/script/ScriptOpcodePointers.ts  require / require2
    content/scripts/engine.rs2                        arity and argument types

engine.rs2 is the authoritative signature file: every command the language can
call is declared there as `[command,name](args)(returns)`, so the arity table is
derived rather than hand-typed. That matters because a wrong arity is not a
crash — it silently pops the wrong number of values and corrupts the stack for
everything after it.

Modeled on src/cs2vm2/gen_opcode_stack.py, including its convention that manual
overrides live in a dict up top and each carries a comment saying why.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_REF = HERE.parent.parent.parent / "LostCity_Server"

# ---------------------------------------------------------------------------
# Manual overrides
# ---------------------------------------------------------------------------

# Opcodes with no `[command,...]` entry in engine.rs2. Two kinds:
#
#   - core language ops (0..46), whose arity is structural rather than declared;
#   - a handful of commands the reference implements but never declared.
#
# (int_in, str_in, int_out, str_out). Anything absent here AND absent from
# engine.rs2 gets known=0 and dispatches to the loud stub.
MANUAL_META: dict[str, tuple[int, int, int, int]] = {
    # --- core language (ids 0..46) -----------------------------------------
    # Operand-carrying pushes take nothing off the stack.
    "PUSH_CONSTANT_INT": (0, 0, 1, 0),
    "PUSH_CONSTANT_STRING": (0, 0, 0, 1),
    "PUSH_VARP": (0, 0, 1, 0),
    "POP_VARP": (1, 0, 0, 0),
    "PUSH_VARN": (0, 0, 1, 0),
    "POP_VARN": (1, 0, 0, 0),
    "PUSH_VARS": (0, 0, 1, 0),
    "POP_VARS": (1, 0, 0, 0),
    "PUSH_VARBIT": (0, 0, 1, 0),
    "POP_VARBIT": (1, 0, 0, 0),
    "PUSH_INT_LOCAL": (0, 0, 1, 0),
    "POP_INT_LOCAL": (1, 0, 0, 0),
    "PUSH_STRING_LOCAL": (0, 0, 0, 1),
    "POP_STRING_LOCAL": (0, 1, 0, 0),
    "POP_INT_DISCARD": (1, 0, 0, 0),
    "POP_STRING_DISCARD": (0, 1, 0, 0),
    "BRANCH": (0, 0, 0, 0),
    "BRANCH_NOT": (2, 0, 0, 0),
    "BRANCH_EQUALS": (2, 0, 0, 0),
    "BRANCH_LESS_THAN": (2, 0, 0, 0),
    "BRANCH_GREATER_THAN": (2, 0, 0, 0),
    "BRANCH_LESS_THAN_OR_EQUALS": (2, 0, 0, 0),
    "BRANCH_GREATER_THAN_OR_EQUALS": (2, 0, 0, 0),
    "SWITCH": (1, 0, 0, 0),
    # RETURN pops nothing: the callee's return values are already on the stack
    # and the caller reads them there.
    "RETURN": (0, 0, 0, 0),
    # GOSUB/JUMP pop the script id (they are declared in engine.rs2 as taking a
    # proc/label, but the operand is the dot flag, not the id).
    "GOSUB": (1, 0, 0, 0),
    "JUMP": (1, 0, 0, 0),
    # GOSUB_WITH_PARAMS/JUMP_WITH_PARAMS carry the script id as the operand and
    # pop as many args as the callee declares, which is only knowable at run
    # time. Handled structurally by the VM; the table must not claim an arity.
    "GOSUB_WITH_PARAMS": (0, 0, 0, 0),
    "JUMP_WITH_PARAMS": (0, 0, 0, 0),
    # JOIN_STRING pops `operand` strings. Variadic on the operand, not on a type
    # string, so it gets its own handling in the VM.
    "JOIN_STRING": (0, 0, 0, 1),
    # Arrays: the reference throws 'unimplemented' on all three and the corpus
    # never emits them. Declared so the verifier's depth model stays honest.
    "DEFINE_ARRAY": (1, 0, 0, 0),
    "PUSH_ARRAY_INT": (1, 0, 1, 0),
    "POP_ARRAY_INT": (2, 0, 0, 0),

    # --- commands the reference implements but never declared ---------------
    # Read straight off their handlers; engine.rs2 has no entry for any of them.
    "STAT_TOTAL": (0, 0, 1, 0),   # PlayerOps.ts: sums baseLevels -> int
    "NC_VISLEVEL": (1, 0, 1, 0),  # NpcConfigOps.ts: npc -> vislevel
    "TEXT_SWITCH": (1, 2, 0, 1),  # StringOps.ts: (int, str, str) -> str
    #
    # LC_OP, OC_IOP and OC_OP are deliberately absent: the reference declares
    # the opcode ids but implements no handler, so their arity is genuinely
    # unknown. Leaving them known=0 routes them to the loud stub, which is the
    # correct outcome — guessing an arity here would silently corrupt the stack.
}

# Opcodes whose operand is the script id / an index rather than the dot flag.
# The VM needs this to know when NOT to treat the operand as a pointer select.
NON_DOT_OPERAND = {
    "PUSH_VARP", "POP_VARP", "PUSH_VARN", "POP_VARN", "PUSH_VARS", "POP_VARS",
    "PUSH_VARBIT", "POP_VARBIT", "GOSUB_WITH_PARAMS", "JUMP_WITH_PARAMS",
    "JOIN_STRING", "SWITCH", "PUSH_INT_LOCAL", "POP_INT_LOCAL",
    "PUSH_STRING_LOCAL", "POP_STRING_LOCAL",
}

# JOIN_STRING and GOSUB/JUMP_WITH_PARAMS pop a count the table cannot express.
STRUCTURAL_VARIADIC = {"JOIN_STRING", "GOSUB_WITH_PARAMS", "JUMP_WITH_PARAMS"}

# ScriptPointer enum order, from engine/src/engine/script/ScriptPointer.ts.
# The names on the left are what ScriptOpcodePointers.ts writes.
POINTER_BITS = {
    "active_player": 0,
    "active_player2": 1,
    "p_active_player": 2,   # ProtectedActivePlayer
    "p_active_player2": 3,  # ProtectedActivePlayer2
    "active_npc": 4,
    "active_npc2": 5,
    "active_loc": 6,
    "active_loc2": 7,
    "active_obj": 8,
    "active_obj2": 9,
}

# ScriptVarType: every type except `string` lives on the int stack. `any` means
# the command decides at run time what it pushes.
STRING_TYPES = {"string"}
RUNTIME_TYPES = {"any"}


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

def parse_opcodes(path: Path) -> dict[str, int]:
    """Parse the `const enum ScriptOpcode` block.

    TypeScript enum members without an explicit `= N` take the previous value
    plus one, so this has to be a running counter — a regex for `NAME = N`
    alone would silently miss the ~340 implicit members.
    """
    text = path.read_text(encoding="utf-8")
    start = text.index("enum ScriptOpcode {")
    body = text[start:]
    body = body[: body.index("\n}")]

    out: dict[str, int] = {}
    nxt = 0
    member = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*(?:=\s*(-?\d+))?\s*,?\s*(?://.*)?$")
    for line in body.split("\n")[1:]:
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith("/*"):
            continue
        m = member.match(line)
        if not m:
            continue
        name, explicit = m.group(1), m.group(2)
        value = int(explicit) if explicit is not None else nxt
        out[name] = value
        nxt = value + 1
    return out


def parse_triggers(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    start = text.index("enum ServerTriggerType {")
    body = text[start:]
    body = body[: body.index("\n}")]

    out: dict[str, int] = {}
    nxt = 0
    member = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*(?:=\s*(\d+))?\s*,?\s*(?://.*)?$")
    for line in body.split("\n")[1:]:
        m = member.match(line)
        if not m:
            continue
        value = int(m.group(2)) if m.group(2) is not None else nxt
        out[m.group(1)] = value
        nxt = value + 1
    return out


def split_params(text: str) -> list[str]:
    """Split `int $a, coord $b` into ['int', 'coord'].

    Returns are declared without a `$name`, arguments with one; taking the first
    whitespace-separated token handles both.
    """
    text = text.strip()
    if not text:
        return []
    return [part.strip().split()[0] for part in text.split(",") if part.strip()]


def parse_engine_rs2(path: Path) -> dict[str, dict]:
    """Parse `[command,name](args)(returns)` declarations.

    Three shapes appear:
        [command,cam_reset]                       no parens at all
        [command,map_clock]()(int)                empty args, one return
        [command,stat](stat $stat)(int)           the common case

    A leading `.` marks the secondary-pointer variant, which shares the base
    name's opcode and only contributes has_dot. A trailing `*` marks the vararg
    form, which is a *separate* opcode named <BASE>VARARG — `queue*` is
    QUEUEVARARG (2094), not QUEUE (2093). Merging them would both mislabel QUEUE
    as variadic and leave QUEUEVARARG with no signature at all.
    """
    decl = re.compile(
        r"^\[command,(\.?)([a-z0-9_]+)(\*?)\]"
        r"(?:\(([^)]*)\))?"
        r"(?:\(([^)]*)\))?"
    )
    # A bare `[command,x]` normally means genuinely zero-arity (cam_reset,
    # if_close, p_pausebutton...). But at least one entry parks its real
    # signature in a trailing comment:
    #
    #     [command,enum] // (type $input, type $output, enum $enum, dynamic $key)(dynamic)
    #
    # Reading that as zero-arity is not a harmless approximation: `enum` pops
    # four values and pushes one, so the stack silently slides by five for
    # everything after it. Recovering the signature from the comment keeps this
    # self-maintaining — if upstream un-comments it, nothing here changes.
    commented = re.compile(r"//\s*\(([^)]*)\)\s*(?:\(([^)]*)\))?")

    out: dict[str, dict] = {}
    for line in path.read_text(encoding="utf-8").split("\n"):
        m = decl.match(line.strip())
        if not m:
            continue
        dot, name, star, args_s, rets_s = m.groups()

        if args_s is None:
            recovered = commented.search(line)
            if recovered:
                args_s, rets_s = recovered.group(1), recovered.group(2)
        args = split_params(args_s or "")
        rets = split_params(rets_s or "")

        key = name.upper() + ("VARARG" if star else "")
        entry = out.setdefault(key, {
            "int_in": 0, "str_in": 0, "int_out": 0, "str_out": 0,
            "has_dot": 0, "variadic": 0, "runtime_typed": 0,
        })
        if dot:
            entry["has_dot"] = 1
        if star:
            # The vararg form pops a type string on top of its declared args and
            # decides from that string's characters what to pop next, so the
            # declared arity is only a lower bound. Callers must not use it.
            entry["variadic"] = 1
        # The base declaration wins; the `.dot` one only contributes has_dot.
        if dot and entry["int_in"] + entry["str_in"] + entry["int_out"] + entry["str_out"]:
            continue

        entry["int_in"] = sum(1 for t in args if t not in STRING_TYPES)
        entry["str_in"] = sum(1 for t in args if t in STRING_TYPES)
        entry["int_out"] = sum(1 for t in rets if t not in STRING_TYPES)
        entry["str_out"] = sum(1 for t in rets if t in STRING_TYPES)
        if any(t in RUNTIME_TYPES for t in rets):
            entry["runtime_typed"] = 1
    return out


def parse_pointers(path: Path, opcodes: dict[str, int]) -> dict[str, tuple[int, int]]:
    """Extract require / require2 masks per opcode.

    `set`, `corrupt` and `conditional` are deliberately ignored: they are
    semantic (npc_find only sets active_npc when it actually found one), so they
    stay handler responsibilities. Only `require` is table-drivable.
    """
    text = path.read_text(encoding="utf-8")
    entry = re.compile(
        r"\[ScriptOpcode\.([A-Z0-9_]+)\]:\s*\{(.*?)\n    \}", re.DOTALL
    )
    out: dict[str, tuple[int, int]] = {}
    for m in entry.finditer(text):
        name, body = m.group(1), m.group(2)
        if name not in opcodes:
            continue

        def mask_of(field: str) -> int:
            fm = re.search(rf"\b{field}\s*:\s*\[(.*?)\]", body, re.DOTALL)
            if not fm:
                return 0
            mask = 0
            for ptr in re.findall(r"'([a-z_0-9]+)'", fm.group(1)):
                # find_* / last_* are compiler-side validation concepts with no
                # ScriptPointer bit; the runtime cannot check them.
                if ptr in POINTER_BITS:
                    mask |= 1 << POINTER_BITS[ptr]
            return mask

        out[name] = (mask_of("require"), mask_of("require2"))
    return out


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------

BANNER = """/*
 * Generated by src/serverscript/gen_opcode_meta.py from the LostCity reference
 * server. Do not edit by hand — re-run the generator.
 */
"""


def emit_opcode_h(opcodes: dict[str, int], out: Path) -> None:
    ordered = sorted(opcodes.items(), key=lambda kv: (kv[1], kv[0]))
    hi = max(opcodes.values())
    lines = [
        BANNER,
        "#ifndef SRC_SERVERSCRIPT_SS_OPCODE_H",
        "#define SRC_SERVERSCRIPT_SS_OPCODE_H",
        "",
        "/* ServerScript opcode ids (LostCity ScriptOpcode.ts).",
        " *",
        " * Ranges: core language 0-46, server 1000+, player 2000+, npc 2500+,",
        " * loc 3000+, obj 3500+, npc/loc/obj config 4000+, inv 4300+, enum 4400+,",
        " * string 4500+, number 4600+, struct 4700+, db 7500+, debug 10000+. */",
        "",
    ]
    last_band = None
    for name, value in ordered:
        band = value // 500
        if last_band is not None and band != last_band:
            lines.append("")
        last_band = band
        lines.append(f"#define SS_OP_{name} {value}")
    lines += [
        "",
        f"/** One past the highest opcode id; the size of any opcode-indexed table. */",
        f"#define SS_OPCODE_MAX {hi + 1}",
        f"/** Opcodes the reference actually defines (the table is sparse). */",
        f"#define SS_OPCODE_COUNT {len(opcodes)}",
        "",
        "#endif",
        "",
    ]
    out.write_text("\n".join(lines), encoding="utf-8")


def emit_trigger_h(triggers: dict[str, int], out: Path) -> None:
    ordered = sorted(triggers.items(), key=lambda kv: (kv[1], kv[0]))
    hi = max(triggers.values())
    lines = [
        BANNER,
        "#ifndef SRC_SERVERSCRIPT_SS_TRIGGER_H",
        "#define SRC_SERVERSCRIPT_SS_TRIGGER_H",
        "",
        "/* ServerScript trigger ids (LostCity ServerTriggerType.ts).",
        " *",
        " * A script's lookup key is trigger | (kind << 8) | (subject << 10); see",
        " * SSVM_LookupKey in ss_meta.h. The op form of an interaction trigger is",
        " * always the ap form + 7 (APNPC1 3 -> OPNPC1 10, APLOC1 59 -> OPLOC1 66),",
        " * which is why the engine stores the ap id and adds 7 rather than",
        " * carrying both. */",
        "",
    ]
    for name, value in ordered:
        lines.append(f"#define SS_TRIGGER_{name} {value}")
    lines += [
        "",
        f"/** One past the highest trigger id. */",
        f"#define SS_TRIGGER_MAX {hi + 1}",
        "",
        "/** Distance from an ap trigger to its matching op trigger. */",
        "#define SS_TRIGGER_AP_TO_OP 7",
        "",
        "#endif",
        "",
    ]
    out.write_text("\n".join(lines), encoding="utf-8")


def emit_meta_gen_h(
    opcodes: dict[str, int],
    triggers: dict[str, int],
    sigs: dict[str, dict],
    pointers: dict[str, tuple[int, int]],
    out: Path,
) -> tuple[int, int]:
    op_max = max(opcodes.values()) + 1
    trig_max = max(triggers.values()) + 1
    by_id = {v: k for k, v in opcodes.items()}
    trig_by_id = {v: k for k, v in triggers.items()}

    known = 0
    rows: list[str] = []
    for value in sorted(by_id):
        name = by_id[value]
        sig = sigs.get(name)
        manual = MANUAL_META.get(name)

        if manual is not None:
            int_in, str_in, int_out, str_out = manual
            is_known = 1
        elif sig is not None:
            int_in = sig["int_in"]
            str_in = sig["str_in"]
            int_out = sig["int_out"]
            str_out = sig["str_out"]
            is_known = 1
        else:
            int_in = str_in = int_out = str_out = 0
            is_known = 0
        known += is_known

        variadic = 1 if name in STRUCTURAL_VARIADIC else (sig["variadic"] if sig else 0)
        runtime_typed = sig["runtime_typed"] if sig else 0
        has_dot = sig["has_dot"] if sig else 0
        req, req2 = pointers.get(name, (0, 0))

        rows.append(
            f"    [{value}] = {{ {int_in}, {str_in}, {int_out}, {str_out}, "
            f"{is_known}, {variadic}, {runtime_typed}, {has_dot}, "
            f"0x{req:03x}, 0x{req2:03x} }}, /* {name} */"
        )

    lines = [
        BANNER,
        "/* Included by exactly one translation unit: ss_meta.c. */",
        "",
        "/* Opcode names, for traces and the loud stub's report. */",
        f"static const char* const g_ss_opcode_names[{op_max}] = {{",
    ]
    for value in sorted(by_id):
        lines.append(f'    [{value}] = "{by_id[value]}",')
    lines += ["};", ""]

    lines += [
        "/* Per-opcode stack signature and runtime-safety metadata.",
        " *",
        " * Fields, in order: int_in, str_in, int_out, str_out, known, variadic,",
        " * runtime_typed, has_dot, require, require2. See struct SSVM_OpcodeMeta.",
        " *",
        " * known == 0 means neither engine.rs2 nor MANUAL_META declared this",
        " * opcode, so its arity is unknown and it must not be executed. */",
        f"static const struct SSVM_OpcodeMeta g_ss_opcode_meta[{op_max}] = {{",
    ]
    lines += rows
    lines += ["};", ""]

    lines += [
        "/* Trigger names, for script-name parsing and diagnostics. */",
        f"static const char* const g_ss_trigger_names[{trig_max}] = {{",
    ]
    for value in sorted(trig_by_id):
        lines.append(f'    [{value}] = "{trig_by_id[value].lower()}",')
    lines += ["};", ""]

    out.write_text("\n".join(lines), encoding="utf-8")
    return known, len(opcodes)


# ---------------------------------------------------------------------------

def main() -> int:
    ref = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_REF
    script_dir = ref / "engine/src/engine/script"
    for required in (
        script_dir / "ScriptOpcode.ts",
        script_dir / "ServerTriggerType.ts",
        script_dir / "ScriptOpcodePointers.ts",
        ref / "content/scripts/engine.rs2",
    ):
        if not required.exists():
            print(f"missing input: {required}", file=sys.stderr)
            print(f"usage: {sys.argv[0]} [path-to-LostCity_Server]", file=sys.stderr)
            return 1

    opcodes = parse_opcodes(script_dir / "ScriptOpcode.ts")
    triggers = parse_triggers(script_dir / "ServerTriggerType.ts")
    sigs = parse_engine_rs2(ref / "content/scripts/engine.rs2")
    pointers = parse_pointers(script_dir / "ScriptOpcodePointers.ts", opcodes)

    emit_opcode_h(opcodes, HERE / "ss_opcode.h")
    emit_trigger_h(triggers, HERE / "ss_trigger.h")
    known, total = emit_meta_gen_h(opcodes, triggers, sigs, pointers, HERE / "ss_meta.gen.h")

    unmatched = sorted(set(sigs) - set(opcodes))
    print(f"opcodes   {total} defined, {known} with a known signature")
    print(f"triggers  {len(triggers)}")
    print(f"pointers  {len(pointers)} opcodes carry a require mask")
    print(f"engine.rs2 {len(sigs)} commands, {len(unmatched)} with no matching opcode")
    if unmatched:
        print(f"           {', '.join(unmatched)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
