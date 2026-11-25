#!/usr/bin/env python3
"""The FAST list (fast_instructions.py), the generated FAST range and the loop's cases agree.

Fails (exit 1, one line per difference) when an instruction marked FAST has no
case in the interpreter's loop (src/cs2vm2/cs2vm2_hot_cases.inc), when the loop
has a case for an instruction not marked FAST, or when the generated
cs2_opcode.h's [0, CS2_OP_FAST_END) is not exactly the FAST list. Run by
gen_opcodes.py after it writes the header, and by `make test-cs2-opcode-groups`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CS2VM2 = HERE.parent.parent / "src" / "cs2vm2"
HOT_CASES = CS2VM2 / "cs2vm2_hot_cases.inc"
OPCODE_H = CS2VM2 / "cs2_opcode.h"

sys.path.insert(0, str(HERE))
from fast_instructions import FAST  # noqa: E402


def loop_arms(text: str) -> set[str]:
    """The instruction each case of the loop's FAST arms is for."""
    return set(re.findall(r"^\s*case CS2VM2_HOT_([A-Z0-9_]+):", text, re.MULTILINE))


def header_fast_range(text: str) -> set[str]:
    m = re.search(r"#define CS2_OP_FAST_END (\d+)", text)
    if not m:
        raise SystemExit(f"check_fast_arms: no CS2_OP_FAST_END in {OPCODE_H}")
    end = int(m.group(1))
    values = {name: int(v) for name, v in re.findall(r"^\s*CS2_OP_([A-Z0-9_]+) = (\d+),", text, re.MULTILINE)}
    return {name for name, v in values.items() if v < end}


def header_aliases(text: str) -> dict[str, str]:
    """A second spelling of an instruction (CS2_OP_A = CS2_OP_B) -> its name."""
    return dict(re.findall(r"^\s*CS2_OP_([A-Z0-9_]+) = CS2_OP_([A-Z0-9_]+),", text, re.MULTILINE))


def problems() -> list[str]:
    out: list[str] = []
    header = OPCODE_H.read_text(encoding="utf-8")
    aliases = header_aliases(header)
    arms = {aliases.get(name, name) for name in loop_arms(HOT_CASES.read_text(encoding="utf-8"))}
    for name in sorted(FAST - arms):
        out.append(f"{name} is marked FAST but the loop has no case for it ({HOT_CASES.name})")
    for name in sorted(arms - FAST):
        out.append(f"the loop has a case for {name}, which is not marked FAST (fast_instructions.py)")
    in_range = header_fast_range(header)
    for name in sorted(FAST - in_range):
        out.append(f"{name} is marked FAST but is not below CS2_OP_FAST_END (regenerate cs2_opcode.h)")
    for name in sorted(in_range - FAST):
        out.append(f"{name} is below CS2_OP_FAST_END but is not marked FAST (regenerate cs2_opcode.h)")
    return out


def main() -> int:
    found = problems()
    for line in found:
        print(f"check_fast_arms: {line}", file=sys.stderr)
    if found:
        return 1
    print(f"check_fast_arms: {len(FAST)} FAST instructions, each with a case in the loop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
