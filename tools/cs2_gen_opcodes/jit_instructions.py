"""The JIT section of the VM's instruction set: what only the loader writes.

These instructions follow every instruction a loaded script can hold
(CS2_OP_JIT_FIRST == CS2_OP_PLAIN_COUNT in the generated cs2_opcode.h). No
cache spells them and no dialect maps to them: they appear only in the array
the interpreter's loop runs (CS2VM2_Script.run_ops), written by
CS2VM2_ScriptPrepare (END_OF_SCRIPT, RUN_HANDLER) or by the load-time jit
(the rest; docs/CS2_JIT.md). Each has one case in the loop
(cs2vm2_jit_cases.inc), checked by check_loop_arms.py.

THE ORDER IS PART OF THE JIT BLOB FORMAT: a blob stores, per pc, the
instruction's position in this list (0, END_OF_SCRIPT's, which no blob holds,
standing for "the pc's own instruction"). Changing the order, removing an
entry or changing what one means bumps CS2VM2_JIT_FORMAT_VERSION
(cs2vm2_jit.h); appending one does too, since an older build would reject it
anyway.

Naming: a fused instruction joins the names of the instructions it replaces,
in order, with "__"; a load-proven one is the instruction's name followed by
what was proven. Inside a fused instruction, PUSH_INT_LOCAL and POP_INT_LOCAL
are always the declared-local forms.
"""

from __future__ import annotations

# (name, one-line description), in blob order.
JIT_INSTRUCTIONS: tuple[tuple[str, str], ...] = (
    # Control: written by CS2VM2_ScriptPrepare as well as by the jit.
    ("END_OF_SCRIPT", "at index op_count of every run array: running off the script's last instruction ends the run"),
    ("RUN_HANDLER", "gives the pc's own instruction (opcodes[pc]) to its handler: a FAST instruction whose loop case cannot be trusted there"),
    # Load-proven single instructions.
    ("PUSH_INT_LOCAL_DECLARED", "PUSH_INT_LOCAL of a local below the script's declared int-local count"),
    ("POP_INT_LOCAL_DECLARED", "POP_INT_LOCAL of a local below the script's declared int-local count"),
    ("PUSH_CONSTANT_STRING_PRESENT", "PUSH_CONSTANT_STRING whose constant exists: pushed by pointer"),
    # The fused instructions.
    ("PUSH_INT_LOCAL__PUSH_INT_LOCAL", "push loc[a], loc[b]"),
    ("PUSH_INT_LOCAL__PUSH_CONSTANT_INT", "push loc[a], x"),
    ("PUSH_CONSTANT_INT__PUSH_INT_LOCAL", "push x, loc[a]"),
    ("PUSH_CONSTANT_INT__PUSH_CONSTANT_INT", "push x, y"),
    ("PUSH_INT_LOCAL__PUSH_INT_LOCAL__PUSH_INT_LOCAL", "push loc[a], loc[b], loc[c]"),
    ("PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD__POP_INT_LOCAL", "loc[b] = add(loc[a], x)"),
    ("PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD", "push add(loc[a], x)"),
    ("PUSH_CONSTANT_INT__ADD", "top = add(top, x)"),
    ("ADD__POP_INT_LOCAL", "loc[b] = add(s[-2], s[-1]); pop 2"),
    ("PUSH_CONSTANT_INT__POP_INT_LOCAL", "loc[b] = x"),
    ("PUSH_INT_LOCAL__POP_INT_LOCAL", "loc[b] = loc[a]"),
    ("POP_INT_LOCAL__BRANCH", "loc[b] = pop; branch"),
    ("PUSH_CONSTANT_INT__BRANCH_EQUALS", "v = pop; branch if v == x"),
    ("PUSH_CONSTANT_INT__BRANCH_NOT", "v = pop; branch if v != x"),
    ("PUSH_CONSTANT_INT__BRANCH_LESS_THAN", "v = pop; branch if v < x"),
    ("PUSH_CONSTANT_INT__BRANCH_GREATER_THAN", "v = pop; branch if v > x"),
    ("PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_EQUALS", "branch if loc[a] == x"),
    ("PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_NOT", "branch if loc[a] != x"),
    ("PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_LESS_THAN", "branch if loc[a] < x"),
    ("PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_GREATER_THAN", "branch if loc[a] > x"),
    ("PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_EQUALS", "branch if loc[a] == loc[b]"),
    ("PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_NOT", "branch if loc[a] != loc[b]"),
    ("PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_LESS_THAN", "branch if loc[a] < loc[b]"),
    ("PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_GREATER_THAN", "branch if loc[a] > loc[b]"),
)

# The first fused instruction: [CS2_OPS_JIT_FUSED_BEGIN, CS2_OP_COUNT).
FIRST_FUSED = "PUSH_INT_LOCAL__PUSH_INT_LOCAL"


def validate(plain_names: set[str]) -> None:
    names = [name for name, _ in JIT_INSTRUCTIONS]
    if len(set(names)) != len(names):
        raise ValueError("jit_instructions: a name is listed twice")
    if names[0] != "END_OF_SCRIPT":
        raise ValueError("jit_instructions: END_OF_SCRIPT must be first (a blob's 0 is the plain instruction)")
    clash = sorted(set(names) & plain_names)
    if clash:
        raise ValueError(f"jit_instructions: names a loaded script's instruction too: {clash}")
    fused = names[names.index(FIRST_FUSED):]
    for name in fused:
        for part in name.split("__"):
            if part not in plain_names:
                raise ValueError(f"jit_instructions: {name} joins {part}, which is no instruction")
