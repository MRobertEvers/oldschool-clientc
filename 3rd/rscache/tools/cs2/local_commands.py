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

    # ---------------------------------------------------------------
    # Solved from the corpus by `cs2 infer-arity`, not transcribed from a
    # source. For each opcode the tool takes the osrs230 scripts where it is
    # the only unknown, tries every plausible (int in, str in, int out, str
    # out), and keeps the ones under which the script interprets *and* every
    # `return` matches the arity the script's own epilogue declares. Listed
    # here are the ones where a single candidate survived, or where the
    # survivors all produce identical source.
    #
    # The witness count is the evidence. One witness is weaker than thirty and
    # is marked as such; re-run the tool after adding knowledge and it will
    # narrow further. Prototypes are plain int/string because the method
    # establishes counts, not meanings.
    # ---------------------------------------------------------------
    # 1152: 2 witnesses, unique.
    "_1152": ([], ["INT"], False),
    # 1928: 5 witnesses, unique.
    "cc_triggerop": (["INT"], [], False),
    # 3102: 2 witnesses, unique.
    "_3102": (["INT", "STRING"], [], False),
    # 3189: 1 witness, unique.
    "_3189": (["INT"], [], False),
    # 3223: 2 witnesses, unique.
    "_3223": (["INT"], [], False),
    # 3224: 1 witness, unique.
    "_3224": ([], [], False),
    # 3225: 1 witness, unique.
    "_3225": ([], [], False),
    # 3329: 1 witness, unique.
    "_3329": ([], [], False),
    # 3931: 1 witness, unique.
    "_3931": ([], [], False),
    # 4036: 2 witnesses, unique.
    "_4036": (["STRING"], ["INT"], False),
    # 4124: 30 witnesses, output-equivalent.
    "_4124": ([], ["STRING"], False),
    # 6863: 1 witness, unique.
    "_6863": ([], ["INT"], False),
    # 7040: 2 witnesses, unique.
    "_7040": (["INT", "INT", "INT", "INT", "INT"], [], False),
    # 7044: 1 witness, unique.
    "_7044": (["INT"], [], False),
    # 7406: 2 witnesses, unique.
    "_7406": (["INT", "INT"], ["STRING"], False),
    # 7409: 2 witnesses, unique.
    "_7409": (["INT"], [], False),
    # 7460: 1 witness, unique.
    "_7460": ([], ["INT"], False),
    # 7462: 3 witnesses, unique.
    "_7462": (["INT", "INT"], [], False),
    # 7465: 1 witness, unique.
    "_7465": ([], [], False),
    # 7466: 1 witness, unique.
    "_7466": (["INT"], [], False),
    # 7470: 1 witness, unique.
    "_7470": ([], [], False),
    # 7613: 2 witnesses, unique.
    "_7613": ([], [], False),
    # 7614: 1 witness, unique.
    "_7614": (["STRING"], [], False),
    # 7616: 2 witnesses, unique.
    "_7616": (["STRING"], [], False),
    # 7617: 2 witnesses, unique.
    "_7617": (["STRING"], [], False),
    # 7621: 1 witness, unique.
    "_7621": ([], [], False),
    # 7809: 1 witness, unique.
    "_7809": ([], ["INT"], False),
    # 7810: 2 witnesses, unique.
    "_7810": ([], [], False),
    # 7811: 1 witness, unique.
    "_7811": ([], ["STRING"], False),
    # 7812: 1 witness, unique.
    "_7812": (["INT"], [], False),
    # 3932: 4 witnesses, unique (round 1).
    "_3932": ([], ["INT"], False),
    # 7471: 1 witness, unique (round 1).
    "_7471": ([], ["INT"], False),
    # 7823: 1 witness, unique (round 1).
    "_7823": (["INT"], ["STRING"], False),
    # 1624: solved jointly with 2624; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_1624": ([], ["INT"], False),
    # 6761: solved jointly with 6762; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_6761": (["INT", "INT", "INT"], ["STRING"], False),
    # 6762: solved jointly with 6761; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_6762": (["INT", "INT", "INT"], ["STRING"], False),
    # 6806: solved jointly with 6807; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_6806": (["INT", "INT"], ["STRING"], False),
    # 6807: solved jointly with 6806; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_6807": (["INT", "INT", "INT"], ["STRING"], False),
    # 6857: solved jointly with 6858; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_6857": (["INT", "INT"], ["STRING"], False),
    # 6858: solved jointly with 6857; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_6858": (["INT", "INT", "INT"], ["STRING"], False),
    # 7603: solved jointly with 7604; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_7603": (["STRING"], ["INT"], False),
    # 7604: solved jointly with 7603; only one arity for each lets the
    # pair balance the stack in the script they share.
    "_7604": (["STRING"], ["INT"], False),
    # 6809: 3 witness(es), unique.
    "_6809": (["INT"], ["STRING"], False),
}
