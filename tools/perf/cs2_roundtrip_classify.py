#!/usr/bin/env python3
"""Classify every non-exact script from a CS2 round-trip dump.

The round-trip command says whether a script differs.  Its ``--dump`` output,
disassembled once per side, says *how* it differs.  This tool joins those two
views into the per-script inventory used by the rev-239 closure document.
"""

from __future__ import annotations

import argparse
import difflib
import re
from dataclasses import dataclass, field
from pathlib import Path


FAILURE = re.compile(r"^(DECOMPILE|COMPILE|DIFF) (\d+):\s*(.*)$")
SUMMARY = re.compile(
    r"^round-trip: (\d+)/(\d+) decompiled, (\d+) compiled, "
    r"(\d+) same-length, (\d+) exact$"
)
HEADER = re.compile(
    r"^; script (\d+)\s+locals (\S+)\s+args (\S+)\s+ops (\d+)\s+switches (\d+)$"
)
INSN = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\S+)\s+(.+?)\s+([+-]\d+)\s+(.*)$")
SWITCH = re.compile(r"^; switch (\d+):\s*(.*)$")

BRANCH_OPCODES = {6, 7, 8, 9, 10, 31, 32}


@dataclass(frozen=True)
class Instruction:
    opcode: int
    name: str
    operand: str


@dataclass
class Script:
    locals: str = ""
    arguments: str = ""
    instructions: list[Instruction] = field(default_factory=list)
    switches: list[str] = field(default_factory=list)


def parse_disassembly(path: Path) -> dict[int, Script]:
    scripts: dict[int, Script] = {}
    current: Script | None = None
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = HEADER.match(raw)
        if match:
            script_id, locals_, arguments, _ops, _switches = match.groups()
            current = Script(locals=locals_, arguments=arguments)
            scripts[int(script_id)] = current
            continue
        if current is None:
            continue
        match = INSN.match(raw)
        if match:
            _pc, opcode, name, _stack, _depth, operand = match.groups()
            current.instructions.append(Instruction(int(opcode), name, operand))
            continue
        match = SWITCH.match(raw)
        if match:
            current.switches.append(match.group(2))
    return scripts


def instruction_edits(original: Script, rebuilt: Script) -> tuple[list[int], list[int]]:
    before = [insn.opcode for insn in original.instructions]
    after = [insn.opcode for insn in rebuilt.instructions]
    removed: list[int] = []
    added: list[int] = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(a=before, b=after).get_opcodes():
        if tag in ("delete", "replace"):
            removed.extend(before[i1:i2])
        if tag in ("insert", "replace"):
            added.extend(after[j1:j2])
    return removed, added


def format_counts(values: list[int]) -> str:
    counts = {value: values.count(value) for value in sorted(set(values))}
    return ",".join(f"{value}x{count}" for value, count in counts.items()) or "none"


def classify_diff(original: Script, rebuilt: Script) -> tuple[str, str]:
    before_ops = [insn.opcode for insn in original.instructions]
    after_ops = [insn.opcode for insn in rebuilt.instructions]

    if before_ops != after_ops:
        removed, added = instruction_edits(original, rebuilt)
        changed = set(removed + added)
        detail = f"opcode edits: remove {format_counts(removed)}; add {format_counts(added)}"

        if changed <= {3, 37, 4106}:
            return "string_segmentation", detail
        if changed <= BRANCH_OPCODES:
            return "control_flow_structure", detail
        if changed <= {21}:
            return "redundant_return_elision", detail
        if changed <= {0, 3, 21}:
            return "epilogue_shape", detail
        if changed <= BRANCH_OPCODES | {21}:
            return "control_flow_return_shape", detail
        if changed <= BRANCH_OPCODES | {0, 3, 21}:
            return "control_flow_epilogue_shape", detail
        return "mixed_instruction_shape", detail

    differences: list[tuple[int, Instruction, Instruction]] = []
    for pc, (before, after) in enumerate(zip(original.instructions, rebuilt.instructions)):
        if before.operand != after.operand:
            differences.append((pc, before, after))

    switch_changed = original.switches != rebuilt.switches
    if original.locals != rebuilt.locals or original.arguments != rebuilt.arguments:
        return (
            "frame_metadata",
            f"locals {original.locals} -> {rebuilt.locals}; "
            f"args {original.arguments} -> {rebuilt.arguments}",
        )
    non_branch = [difference for difference in differences if difference[1].opcode not in BRANCH_OPCODES]

    def operand_detail(items: list[tuple[int, Instruction, Instruction]]) -> str:
        shown = "; ".join(
            f"pc {pc} {before.name}: {before.operand} -> {after.operand}"
            for pc, before, after in items[:3]
        )
        if len(items) > 3:
            shown += f"; and {len(items) - 3} more"
        return shown

    if not non_branch and differences:
        return "branch_targets", operand_detail(differences)
    if not differences and switch_changed:
        return "switch_table_order", "opcode stream identical; switch entries differ"
    if not differences:
        return (
            "unprinted_serialized_metadata",
            "disassembled instructions and visible frame metadata are identical",
        )

    opcodes = {before.opcode for _pc, before, _after in non_branch}
    values = {(before.operand, after.operand) for _pc, before, after in non_branch}
    detail = operand_detail(non_branch or differences)
    branch_count = len(differences) - len(non_branch)
    branch_suffix = f"; plus {branch_count} branch target(s)" if branch_count > 0 else ""

    if opcodes == {0} and values <= {("-1", "0")}:
        return "erased_narrow_return_type", detail + branch_suffix
    if opcodes <= {4123, 4124}:
        return "ignored_basic_operand", detail + branch_suffix
    if opcodes == {3}:
        if all(
            before.operand.strip('"').endswith(("W", "X"))
            and after.operand.strip('"').endswith("s")
            for _pc, before, after in non_branch
        ):
            return "hook_descriptor_normalization", detail + branch_suffix
        return "string_literal_encoding", detail + branch_suffix
    if opcodes <= BRANCH_OPCODES:
        return "branch_targets", detail
    if switch_changed and not non_branch:
        return "switch_table_order", "switch entries differ"
    if switch_changed:
        detail += "; switch entries also differ"
    return "other_operand_encoding", detail + branch_suffix


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("original_disassembly", type=Path)
    parser.add_argument("rebuilt_disassembly", type=Path)
    args = parser.parse_args()

    original = parse_disassembly(args.original_disassembly)
    rebuilt = parse_disassembly(args.rebuilt_disassembly)
    rows: list[tuple[int, str, str, str]] = []
    summary = ""

    for raw in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = FAILURE.match(raw)
        if match:
            stage, script_text, diagnostic = match.groups()
            script_id = int(script_text)
            stage = stage.lower()
            if stage == "diff":
                if script_id not in original or script_id not in rebuilt:
                    parser.error(f"script {script_id} is absent from one disassembly")
                category, detail = classify_diff(original[script_id], rebuilt[script_id])
            elif script_id == 0 and "not in this cache" in diagnostic:
                category, detail = "absent_cache_entry", diagnostic
            else:
                category, detail = f"{stage}_failure", diagnostic
            rows.append((script_id, stage, category, detail.replace("\t", " ")))
        elif SUMMARY.match(raw):
            summary = raw

    if not summary:
        parser.error("input has no round-trip summary")

    print(f"# {summary}")
    print("script_id\tstage\tcategory\tdetail")
    for row in sorted(rows):
        print("\t".join(map(str, row)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
