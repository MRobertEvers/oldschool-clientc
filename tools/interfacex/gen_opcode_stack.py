#!/usr/bin/env python3
"""Generate interfacex_opcode_stack.gen.h from cs2_opcode.h and cs2_opcode_meta.c."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OPCODE_H = ROOT / "src2/vm/cs2_opcode.h"
META_C = ROOT / "src2/vm/cs2_opcode_meta.c"
OUT = Path(__file__).resolve().parent / "interfacex_opcode_stack.gen.h"

MAX_OPCODE = 7602


def parse_stack_line(line: str) -> int:
    rhs = line.split(":", 1)[1]
    if "-" in rhs and not re.search(r"[a-zA-Z0-9_]+", rhs.replace("-", "").strip()):
        return 0
    parts = [p.strip() for p in rhs.split(",") if p.strip() and p.strip() != "-"]
    return len(parts)


def parse_opcode_h() -> dict[int, tuple[int, int, int, int]]:
    text = OPCODE_H.read_text()
    entries: dict[int, tuple[int, int, int, int]] = {}
    pattern = re.compile(r"/\*(.*?)\*/\s*#define\s+CS2_OP_[A-Z0-9_]+\s+(\d+)", re.DOTALL)
    for m in pattern.finditer(text):
        comment, num_s = m.group(1), m.group(2)
        num = int(num_s)
        int_in = str_in = int_out = str_out = 0
        for line in comment.split("\n"):
            line = line.strip().lstrip("*").strip()
            if line.startswith("int stack in:"):
                int_in = parse_stack_line(line)
            elif line.startswith("str stack in:"):
                str_in = parse_stack_line(line)
            elif line.startswith("int stack out:"):
                int_out = parse_stack_line(line)
            elif line.startswith("str stack out:"):
                str_out = parse_stack_line(line)
        entries[num] = (int_in, str_in, int_out, str_out)
    return entries


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
        return (1, 1, 1, 0)
    if name == "STRUCT_PARAM":
        return (2, 0, 1, 0)
    if name in ("ON_MOBILE", "CLIENTTYPE", "COORD", "RUNWEIGHT_VISIBLE", "RUNENERGY_VISIBLE"):
        return (0, 0, 1, 0)
    if name == "CLIENTCLOCK":
        return (0, 0, 1, 0)
    if name in ("MOUSE_GETX", "MOUSE_GETY"):
        return (0, 0, 1, 0)
    if name == "GETWINDOWMODE":
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
        if "NAME" in name or "HISTORY" in name:
            return (1, 0, 0, 1) if "NAME" in name else (2, 0, 0, 1)
        return (0, 0, 1, 0)
    if name.startswith("VIEWPORT_"):
        return (1, 0, 1, 0)
    if name in ("MES", "IF_CLOSE", "SOUND_SYNTH"):
        return (0, 1, 0, 0)
    if name.startswith("SET") and "SETON" not in name:
        return (1, 0, 0, 0)
    if "SETON" in name or name == "CC_SETTARGETVERB":
        return None
    if name.startswith("IF_SET") or name.startswith("CC_SET"):
        return (0, 0, 0, 0)
    return (0, 0, 0, 0)


def main() -> None:
    entries = parse_opcode_h()
    names = parse_meta_names()

    for op, name in names.items():
        if op in entries and any(entries[op]):
            continue
        h = heuristic(name)
        if h is not None:
            entries[op] = h

    lines = [
        "/* Generated by tools/interfacex/gen_opcode_stack.py — do not edit by hand. */",
        "#ifndef INTERFACEX_OPCODE_STACK_GEN_H",
        "#define INTERFACEX_OPCODE_STACK_GEN_H",
        "",
        f"#define INTERFACEX_OPCODE_STACK_MAX {MAX_OPCODE}",
        "",
        "struct InterfacexOpcodeStack {",
        "    unsigned char int_in;",
        "    unsigned char str_in;",
        "    unsigned char int_out;",
        "    unsigned char str_out;",
        "};",
        "",
        "static struct InterfacexOpcodeStack const g_interfacex_opcode_stack[INTERFACEX_OPCODE_STACK_MAX] = {",
    ]
    for op in range(MAX_OPCODE):
        ii, si, io, so = entries.get(op, (0, 0, 0, 0))
        lines.append(f"    [{op}] = {{ {ii}, {si}, {io}, {so} }},")
    lines.extend(["};", "", "#endif", ""])
    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT} ({len(entries)} opcodes with metadata)")


if __name__ == "__main__":
    main()
