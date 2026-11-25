#!/usr/bin/env python3
"""The interpreter loop's cases are exactly the FAST and JIT instructions.

The loop (cs2vm2_run_script_body in src/cs2vm2/cs2vm2.c) switches on the array
a script runs. Its cases are two included files: cs2vm2_fast_cases.inc, one
case per FAST instruction (fast_instructions.py), and cs2vm2_jit_cases.inc,
one case per JIT-section instruction (jit_instructions.py). Every GENERAL
instruction is the switch's default.

Fails (exit 1, one line per difference) when a FAST or JIT instruction has no
case, when a case names an instruction outside its file's section (a GENERAL
one included), or when the generated cs2_opcode.h's FAST range or JIT section
is not exactly its list. Run by gen_opcodes.py after it writes the header, and
by `make test-cs2-opcode-groups`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CS2VM2 = HERE.parent.parent / "src" / "cs2vm2"
FAST_CASES = CS2VM2 / "cs2vm2_fast_cases.inc"
JIT_CASES = CS2VM2 / "cs2vm2_jit_cases.inc"
OPCODE_H = CS2VM2 / "cs2_opcode.h"

sys.path.insert(0, str(HERE))
from fast_instructions import FAST  # noqa: E402
from jit_instructions import JIT_INSTRUCTIONS  # noqa: E402

JIT = [name for name, _ in JIT_INSTRUCTIONS]


def arms(path: Path) -> list[str]:
    """The instruction of every case in `path`, in order."""
    return re.findall(r"^\s*case CS2_OP_([A-Z0-9_]+):", path.read_text(encoding="utf-8"), re.MULTILINE)


def header_values(text: str) -> dict[str, int]:
    return {name: int(v) for name, v in re.findall(r"^\s*CS2_OP_([A-Z0-9_]+) = (\d+),", text, re.MULTILINE)}


def header_define(text: str, name: str) -> int:
    m = re.search(rf"#define {name} (\d+)", text)
    if not m:
        m = re.search(rf"#define {name} ([A-Z0-9_]+)", text)
        if not m:
            raise SystemExit(f"check_loop_arms: no {name} in {OPCODE_H}")
        return header_define(text, m.group(1))
    return int(m.group(1))


def header_aliases(text: str) -> dict[str, str]:
    """A second spelling of an instruction (CS2_OP_A = CS2_OP_B) -> its name."""
    return dict(re.findall(r"^\s*CS2_OP_([A-Z0-9_]+) = CS2_OP_([A-Z0-9_]+),", text, re.MULTILINE))


def section_problems(label: str, path: Path, want: set[str], aliases: dict[str, str]) -> list[str]:
    out: list[str] = []
    found = [aliases.get(name, name) for name in arms(path)]
    for name in sorted({n for n in found if found.count(n) > 1}):
        out.append(f"{path.name} has two cases for {name}")
    for name in sorted(want - set(found)):
        out.append(f"{name} is a {label} instruction but {path.name} has no case for it")
    for name in sorted(set(found) - want):
        out.append(f"{path.name} has a case for {name}, which is not a {label} instruction")
    return out


def problems() -> list[str]:
    header = OPCODE_H.read_text(encoding="utf-8")
    aliases = header_aliases(header)
    out = section_problems("FAST", FAST_CASES, set(FAST), aliases)
    out += section_problems("JIT", JIT_CASES, set(JIT), aliases)

    values = header_values(header)
    fast_begin = header_define(header, "CS2_OP_FAST_BEGIN")
    jit_first = header_define(header, "CS2_OP_JIT_FIRST")
    count = header_define(header, "CS2_OP_COUNT")
    in_fast = {name for name, v in values.items() if fast_begin <= v < jit_first}
    for name in sorted(set(FAST) ^ in_fast):
        out.append(f"{name}: the FAST list and [CS2_OP_FAST_BEGIN, CS2_OP_JIT_FIRST) disagree (regenerate cs2_opcode.h)")
    in_jit = sorted((v, name) for name, v in values.items() if jit_first <= v < count)
    if [name for _, name in in_jit] != JIT or (in_jit and in_jit[0][0] != jit_first):
        out.append("the JIT section of cs2_opcode.h is not jit_instructions.py in order (regenerate cs2_opcode.h)")
    return out


def main() -> int:
    found = problems()
    for line in found:
        print(f"check_loop_arms: {line}", file=sys.stderr)
    if found:
        return 1
    print(f"check_loop_arms: {len(FAST)} FAST and {len(JIT)} JIT instructions, each with one case in the loop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
