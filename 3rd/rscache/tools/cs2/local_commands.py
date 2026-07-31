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
LOCAL_NAMES: dict[int, str] = {
    # First seen in OldSchool 239's gameframe scripts; neither vendored table
    # names it, and its meaning is still unknown. Named so the decompiler can
    # print it — the signature below is what lets it get that far.
    210: "_210",
}

# NAME -> (args, defs, dot)
LOCAL_BASIC: dict[str, tuple[list[str], list[str], bool]] = {
    # Command.kt (2021) never gained these, though Opcodes.kt names them. The
    # pop/push counts are src/cs2vm2/cs2vm2_opcode_stack.gen.h's, i.e. taken
    # from a client that executes them, not from the name.
    #
    # CC_COPY 105 = { int_in 3 }: parent, src_sub, dst_sub. Clones a dynamic
    # child into another slot under the same parent and makes the copy active.
    "CC_COPY": (["COMPONENT", "COMSUBID", "COMSUBID"], [], True),
    # _210 = { int_in 6 }. Established by `cs2 infer-arity` over cache.osrs239,
    # not by a client: ten call sites, every one solving to the same six-int pop
    # with nothing pushed, no other candidate surviving at any of them. The types
    # are plain INT because the method establishes counts, not meanings — the
    # first argument is a component in every site traced, but one shape is not a
    # signature. src/cs2vm2 carries the same counts, and drops the arguments.
    "_210": (["INT", "INT", "INT", "INT", "INT", "INT"], [], False),
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
    # ---------------------------------------------------------------
    # Round 3, solved by `cs2 infer-arity` against cache.osrs239 after the DB
    # family stopped mis-shaping the stack (LOCAL_KINDS below). That fix is what
    # made these solvable: db_getfield's wrong push count desynchronised the
    # scripts these opcodes appear in, so every candidate arity failed and the
    # solver reported "no arity works" for opcodes whose arity was in fact
    # pinned.
    #
    # Single-witness solutions where the tool reported `unique`: exactly one
    # (int in, str in, int out, str out) let the script interpret to its end
    # with every `return` matching the arity its own epilogue declares.
    "_2214": (["INT", "INT"], [], False),
    "_2215": (["INT", "INT"], [], False),
    "_1129": (["STRING"], [], False),
    "_1214": (["INT"], [], False),
    "_6531": ([], ["INT", "INT"], False),
    "_7819": (["STRING"], ["INT"], False),
    "_7824": (["INT"], [], False),
    # Solved jointly, and each of these was reached from more than one partner:
    # 1704 balances at three ints in and nothing out against 222, 1703 and 8003
    # across eleven scripts, and no other arity does in any of them. The rest
    # agree across two pairings each.
    "_1704": (["INT", "INT", "INT"], [], False),
    "_2929": (["INT", "INT", "INT", "INT", "STRING"], [], False),
    "_1506": ([], ["INT"], False),
    "_213": ([], ["INT"], False),
    "_214": ([], ["INT"], False),
    "_222": ([], [], False),
    "_63": ([], ["STRING"], False),
    "_8003": (["STRING"], ["INT"], False),
    "_8021": (["INT", "INT"], ["STRING"], False),
    # Round 4, after round 3 turned two-unknown scripts into one-unknown ones.
    "_7400": (["INT", "STRING"], [], False),
    "_7802": (["STRING"], ["INT"], False),
    "_209": ([], ["INT"], False),
    "_1140": (["INT"], [], False),
    "_1141": (["INT"], [], False),
    # 8022 was not solved by search -- it appears alongside other unknowns in
    # every script -- but its call sites settle it on their own. In script 8153
    # it occurs three times in a row at a statement boundary, each time as
    # exactly three `push_constant_int`/`push_int_local` followed by
    # `pop_string_local`, and 38 further sites have the same shape. Three ints
    # in, one string out is the only reading, and it is worth 40 scripts.
    "_8022": (["INT", "INT", "INT"], ["STRING"], False),
    # Round 5. The search converges here: rounds 6 and 7 solve nothing new.
    "_7801": (["STRING"], ["INT"], False),
    "_1143": (["INT"], [], False),
    # Round 6. Round 7 solves nothing new: the search has converged, and what
    # remains needs an opcode identified in a client rather than inferred (G4).
    "_1628": ([], ["INT"], False),
    "_1139": (["INT"], [], False),
}

# opcode -> handler kind, for commands whose stack shape is not a fixed
# signature at all. `gen_cs2_tables.py` applies these last, over anything the
# vendored tables or the client stack table said.
#
# The DB_* family is the whole of it. A `dbcolumn` literal packs
# (table << 12) | (column << 4) | (field + 1), and field 0 means "the whole
# tuple" -- so `db_getfield` on a four-field column pushes four values, of that
# column's four types, while the same opcode on a single-field column pushes
# one. The client resolves it from the dbtable config at run time
# (src/game/rs_cs2_host.c exec_db); the decompiler resolves it from the same
# config through RSCache_CS2_DecompileOptions.db_columns.
#
# Measured on cache.osrs239: 0xa6200 (table 166, column 32, whole tuple) is
# followed by four int pops at every one of its 26 call sites, and 0xa6204
# (the same column, field 3) by exactly one at all 11 of its. A fixed signature
# cannot be right for both, and the one the stack table carried -- three in,
# one out -- desynchronised the operand stack of every script that read a
# multi-field column, which is what took script 7603 and 79 others.
LOCAL_KINDS: dict[int, str] = {
    7502: "DB_GETFIELD",
    7500: "DB_FIND",
    7507: "DB_FIND",
    7508: "DB_FIND",
    7510: "DB_FIND",
}
