"""Command knowledge this repo has and the vendored RuneStar tables do not.

Two kinds of entry, both layered over `vendor/` by gen_cs2_tables.py:

  LOCAL_NAMES -- an opcode Opcodes.kt never listed, or listed only as a
                 placeholder. Giving it a name lets the decompiler print it
                 instead of failing.
  LOCAL_BASIC -- a signature for a Basic command Command.kt does not carry.
                 Without one, an opcode that appears in a script aborts the
                 decompile of that script, because the interpreter cannot know
                 how many values to pop.

Anything added here must be *established*, not guessed: a wrong pop count does
not fail loudly, it desynchronises the operand stack and produces a plausible
decompile of a different program. Where the count comes from a client that
implements the opcode, say which.

The signature format matches Command.kt's Basic: (args, defs, dot). `args` are
popped bottom-to-top, `defs` are pushed. `dot` marks the commands whose operand
byte selects the "." (active-component) form.
"""

from __future__ import annotations

# id -> NAME. Names are lowercased for output.
LOCAL_NAMES: dict[int, str] = {}

# NAME -> (args, defs, dot)
LOCAL_BASIC: dict[str, tuple[list[str], list[str], bool]] = {
    # Command.kt (2021) never gained these, though Opcodes.kt names them. The
    # pop/push counts are src/cs2vm2/cs2vm2_opcode_stack.gen.h's, i.e. taken
    # from a client that executes them, not from the name.
    #
    # CC_COPY 105 = { int_in 3 }: parent, src_sub, dst_sub. Clones a dynamic
    # child into another slot under the same parent and makes the copy active.
    "CC_COPY": (["COMPONENT", "COMSUBID", "COMSUBID"], [], True),
    # 4016 / 4017 = { int_in 2, int_out 1 }.
    "MIN": (["INT", "INT"], ["INT"], False),
    "MAX": (["INT", "INT"], ["INT"], False),
    # 4122 = { str_in 1, str_out 1 }, the counterpart of LOWERCASE 4039.
    "UPPERCASE": (["STRING"], ["STRING"], False),
}
