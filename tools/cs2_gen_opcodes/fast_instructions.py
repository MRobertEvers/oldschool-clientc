"""The FAST instructions: the ones the interpreter's loop runs itself.

A FAST instruction runs as a case of the loop (cs2vm2_run_script_body in
src/cs2vm2/cs2vm2.c, its cases in cs2vm2_fast_cases.inc) on the loop's own
registers: the pc, the int stack's top and the int locals. It never asks the
host, never yields and never pushes or pops a frame. A case that cannot run as
it stands (a stack too shallow or too deep, a divide by zero, a local out of
range) changes nothing and gives the instruction to its handler, which runs it
as every other instruction is run.

The generator numbers these last of what a loaded script can hold, in wire
order: [CS2_OP_FAST_BEGIN, CS2_OP_PLAIN_COUNT) in the generated cs2_opcode.h,
right below the JIT section (canonical.py says why). Everything else a loaded
script can hold is GENERAL, [0, CS2_OP_FAST_BEGIN), and runs only through its
handler.

This list and the loop's cases are checked against each other
(check_loop_arms.py, run by the generator and by `make test-cs2-opcode-groups`):
a name here with no case, or a case for a name not here, fails.
"""

from __future__ import annotations

FAST: frozenset[str] = frozenset(
    {
        # Constants and locals.
        "PUSH_CONSTANT_INT",
        "PUSH_INT_LOCAL",
        "POP_INT_LOCAL",
        # The int branches.
        "BRANCH",
        "BRANCH_NOT",
        "BRANCH_EQUALS",
        "BRANCH_LESS_THAN",
        "BRANCH_GREATER_THAN",
        "BRANCH_LESS_THAN_OR_EQUALS",
        "BRANCH_GREATER_THAN_OR_EQUALS",
        # Int arithmetic (cs2_math_*, the same functions the handlers call).
        "ADD",
        "SUB",
        "MULTIPLY",
        "DIVIDE",
        "MODULO",
        "AND",
        "OR",
        "MIN",
        "MAX",
        "ADDPERCENT",
        "SCALE",
        "ABS",
        "BITCOUNT",
        "SETBIT",
        "CLEARBIT",
        "TESTBIT",
        "TOGGLEBIT",
        "SETBIT_RANGE",
        "CLEARBIT_RANGE",
        "GETBIT_RANGE",
        "SETBIT_RANGE_TOINT",  # also spelled SETBIT_RANGE_VALUE (local_opcodes.LOCAL_ALIASES)
    }
)
