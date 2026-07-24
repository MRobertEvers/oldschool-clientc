#!/usr/bin/env python3
"""Generate cs2vm2_opcode_stack.gen.h from cs2_opcode.h and cs2_opcode_meta.c."""

import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
OPCODE_H = HERE / "cs2_opcode.h"
META_C = HERE / "cs2_opcode_meta.c"
OUT = HERE / "cs2vm2_opcode_stack.gen.h"

MAX_OPCODE = 7602

MANUAL_STACK: dict[int, tuple[int, int, int, int]] = {
    106: (2, 0, 0, 0),  # CC_CREATECHILD
    107: (2, 0, 0, 0),  # CC_CREATESIBLING
    202: (0, 0, 1, 0),  # CC_FINDROOT
    203: (1, 0, 0, 0),  # CC_CHILDREN_FIND
    204: (0, 0, 1, 0),  # CC_CHILDREN_FINDNEXTID
    205: (2, 0, 0, 0),  # IF_CHILDREN_FIND
    206: (0, 0, 1, 0),  # IF_CHILDREN_FINDNEXTID
    6200: (2, 0, 0, 0),  # VIEWPORT_SETFOV
    6201: (2, 0, 0, 0),  # VIEWPORT_SETZOOM
    6202: (4, 0, 0, 0),  # VIEWPORT_CLAMPFOV
    6203: (0, 0, 2, 0),  # VIEWPORT_GETEFFECTIVESIZE
    6204: (0, 0, 2, 0),  # VIEWPORT_GETZOOM
    6205: (0, 0, 2, 0),  # VIEWPORT_GETFOV
    3328: (0, 0, 1, 0),  # idle-time getter (script 5327 logout warning polls it)
    7253: (0, 0, 1, 0),  # getter polled by toplevel cc_setontimer scripts (7052)
    # _7100 (INT8 operand): a newer client-state getter (7000+ feature range,
    # neighbours HIGHLIGHT_*). Script 5350 does `_7100 0; POP_INT_LOCAL` — one int
    # out — so the {0,0,0,0} default underflowed and aborted the hook (leaving the
    # mobile overlay covering the viewport). Exact semantics unconfirmed; the stub
    # pushes a safe 0 (desktop/off default). Confirm and specialise if needed.
    7100: (0, 0, 1, 0),  # unconfirmed client-state getter -> 1 int
    # _7500..7503: newer host getters used by loot-tools formatter script 7603.
    # {0,0,0,0} left the args on the stack every call -> the stack grew until
    # PUSH_INT_LOCAL overflowed and the hook aborted (runaway cc_create/hover,
    # panel covering the viewport). Signatures derived from 7603's balanced
    # int/string stack trace (args pushed before the op, results popped after):
    7500: (3, 0, 1, 0),  # (id,int,int) -> 1 int
    7501: (0, 0, 1, 0),  # no-arg getter -> 1 int
    7502: (3, 0, 2, 2),  # (id,int,int) -> 2 ints + 2 strings
    7503: (2, 0, 1, 0),  # (id,int) -> 1 int (loop-count getter)
    # FRIEND_COUNT is a no-arg getter (total friends), but the name heuristic gave
    # it (1,0,1,0) like the indexed FRIEND_GET* ops -> the stub popped a non-existent
    # arg and underflowed, aborting the friends-list builder (script 125).
    3600: (0, 0, 1, 0),  # FRIEND_COUNT: no args -> 1 int
    2702: (1, 0, 1, 0),  # IF_HASSUB(component) -> bool; gates gameframe tab reveal (script 908)
}


def parse_stack_line(line: str) -> int:
    rhs = line.split(":", 1)[1]
    if "-" in rhs and not re.search(r"[a-zA-Z0-9_]+", rhs.replace("-", "").strip()):
        return 0
    parts = [p.strip() for p in rhs.split(",") if p.strip() and p.strip() != "-"]
    return len(parts)


def parse_opcode_h() -> tuple[dict[int, tuple[int, int, int, int]], set[int]]:
    """Returns (entries, documented). `documented` is every opcode carrying a
    stack doc comment, including ones whose counts are all zero — those are
    otherwise indistinguishable from "no comment at all" and would be handed to
    the name heuristics, which guess wrong for e.g. SETKEYINPUTMODE_ALL."""
    text = OPCODE_H.read_text()
    entries: dict[int, tuple[int, int, int, int]] = {}
    documented: set[int] = set()
    pattern = re.compile(r"/\*(.*?)\*/\s*#define\s+CS2_OP_[A-Z0-9_]+\s+(\d+)", re.DOTALL)
    for m in pattern.finditer(text):
        comment, num_s = m.group(1), m.group(2)
        num = int(num_s)
        int_in = str_in = int_out = str_out = 0
        saw_stack_line = False
        for line in comment.split("\n"):
            line = line.strip().lstrip("*").strip()
            if line.startswith("int stack in:"):
                int_in = parse_stack_line(line)
                saw_stack_line = True
            elif line.startswith("str stack in:"):
                str_in = parse_stack_line(line)
                saw_stack_line = True
            elif line.startswith("int stack out:"):
                int_out = parse_stack_line(line)
                saw_stack_line = True
            elif line.startswith("str stack out:"):
                str_out = parse_stack_line(line)
                saw_stack_line = True
        entries[num] = (int_in, str_in, int_out, str_out)
        if saw_stack_line:
            documented.add(num)
    return entries, documented


def parse_meta_names() -> dict[int, str]:
    text = META_C.read_text()
    names: dict[int, str] = {}
    for m in re.finditer(r"\[(\d+)\]\s*=\s*\{\s*\"([^\"]+)\"", text):
        names[int(m.group(1))] = m.group(2)
    return names


def heuristic(name: str) -> tuple[int, int, int, int] | None:
    if name in ("POP_VAR", "POP_VARBIT"):
        return (1, 0, 0, 0)
    if name == "DEFINE_ARRAY":
        return (1, 0, 0, 0)
    if name == "PUSH_ARRAY_INT":
        return (1, 0, 1, 0)
    if name == "POP_ARRAY_INT":
        return (2, 0, 0, 0)
    if name in ("SETBIT", "CLEARBIT", "TESTBIT", "OR", "AND", "INVPOW"):
        return (2, 0, 1, 0)
    if name == "SCALE":
        return (3, 0, 1, 0)
    if name == "RANDOM":
        return (0, 0, 1, 0)
    if name == "RANDOMINC":
        return (1, 0, 1, 0)
    if name == "INTERPOLATE":
        return (5, 0, 1, 0)
    if name == "GETBIT_RANGE":
        return (2, 0, 1, 0)
    if name == "COMPARE":
        return (0, 2, 1, 0)
    if name == "SUBSTRING":
        return (2, 1, 0, 1)
    if name == "STRING_LENGTH":
        return (0, 1, 1, 0)
    if name == "APPEND":
        return (1, 1, 0, 1)
    if name in ("LOWERCASE", "REMOVETAGS", "FROMDATE"):
        return (0, 1, 0, 1)
    if name == "STRING_INDEXOF_STRING":
        return (1, 2, 1, 0)
    if name == "STRING_INDEXOF_CHAR":
        return (2, 1, 1, 0)
    if name == "STRUCT_PARAM":
        return (2, 0, 1, 0)
    if name in ("ON_MOBILE", "CLIENTTYPE", "COORD", "RUNWEIGHT_VISIBLE", "RUNENERGY_VISIBLE"):
        return (0, 0, 1, 0)
    if name in ("CLIENTCLOCK", "REBOOTTIMER"):
        return (0, 0, 1, 0)
    if name in ("MOUSE_GETX", "MOUSE_GETY"):
        return (0, 0, 1, 0)
    if name == "GETCANVASSIZE":
        return (0, 0, 2, 0)
    if name == "GETWINDOWMODE":
        return (0, 0, 1, 0)
    if name == "GETDEFAULTWINDOWMODE":
        return (0, 0, 1, 0)
    if name == "IF_GETTOP":
        return (0, 0, 1, 0)
    if name == "IF_FIND":
        return (1, 0, 1, 0)
    if name.startswith("CC_GET"):
        if name in ("CC_GETTEXT", "CC_GETOP", "CC_GETOPBASE"):
            return (0, 0, 0, 1)
        return (0, 0, 1, 0)
    if name.startswith("IF_GET"):
        if name in ("IF_GETOP",):
            return (2, 0, 0, 1)
        if name in ("IF_GETOPBASE",):
            return (1, 0, 0, 1)
        if name == "IF_GETTEXT":
            return (1, 0, 0, 1)
        return (1, 0, 1, 0)
    if name.startswith("OC_"):
        return (1, 0, 1, 0)
    if name.startswith("STAT"):
        return (1, 0, 1, 0)
    if name.startswith("INVOTHER_"):
        return (2, 0, 1, 0)
    if name.startswith("FRIEND_") or name.startswith("CLAN_"):
        return (1, 0, 0, 1) if "NAME" in name else (1, 0, 1, 0)
    if name.startswith("STOCKMARKET_"):
        return (1, 0, 1, 0)
    if name.startswith("CHAT_"):
        if name == "CHAT_PLAYERNAME":
            return (0, 0, 0, 1)
        if name == "CHAT_GETMESSAGEFILTER":
            return (0, 0, 0, 1)
        if "NAME" in name:
            return (1, 0, 0, 1)
        if "GETHISTORYLENGTH" in name:
            return (1, 0, 1, 0)
        if "HISTORY" in name:
            return (2, 0, 0, 1)
        return (0, 0, 1, 0)
    if name.startswith("VIEWPORT_GET"):
        return (0, 0, 2, 0)
    if name.startswith("VIEWPORT_SET"):
        return (2, 0, 0, 0)
    if name == "VIEWPORT_CLAMPFOV":
        return (4, 0, 0, 0)
    if name == "MES":
        return (0, 1, 0, 0)
    if name == "IF_CLOSE":
        return (0, 0, 0, 0)
    if name == "SOUND_SYNTH":
        return (3, 0, 0, 0)
    if name.startswith("SET") and "SETON" not in name:
        return (1, 0, 0, 0)
    if "SETON" in name or name == "CC_SETTARGETVERB":
        return None
    if name.startswith("IF_SET") or name.startswith("CC_SET"):
        return (0, 0, 0, 0)
    return (0, 0, 0, 0)


def main() -> None:
    entries, documented = parse_opcode_h()
    names = parse_meta_names()

    for op, stack in MANUAL_STACK.items():
        entries[op] = stack
        documented.add(op)

    for op, name in names.items():
        if op in documented:
            continue
        if op in entries and any(entries[op]):
            continue
        h = heuristic(name)
        if h is not None:
            entries[op] = h

    lines = [
        "/* Generated by src/cs2vm2/gen_opcode_stack.py — do not edit by hand. */",
        "#ifndef CS2VM2_OPCODE_STACK_GEN_H",
        "#define CS2VM2_OPCODE_STACK_GEN_H",
        "",
        f"#define CS2VM2_OPCODE_STACK_MAX {MAX_OPCODE}",
        "",
        "struct CS2VM2OpcodeStack {",
        "    unsigned char int_in;",
        "    unsigned char str_in;",
        "    unsigned char int_out;",
        "    unsigned char str_out;",
        "};",
        "",
        "static struct CS2VM2OpcodeStack const g_cs2vm2_opcode_stack[CS2VM2_OPCODE_STACK_MAX] = {",
    ]
    for op in range(MAX_OPCODE):
        ii, si, io, so = entries.get(op, (0, 0, 0, 0))
        lines.append(f"    [{op}] = {{ {ii}, {si}, {io}, {so} }},")
    lines.extend(["};", "", "#endif", ""])
    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT} ({len(entries)} opcodes with metadata)")


if __name__ == "__main__":
    main()
