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
    # Vendored Opcodes.kt still calls this if_haschild_modal (rev-634 name).
    # At osrs239 it is IF_SETPARAM (xrsps WidgetOps); Overview script 9176
    # call sites are five ints and nothing out — haschild's (2i->1i) desyncs.
    2704: "if_setparam",
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
    # xrsps names it ARRAY_NEW (typeCode, length, capacity) -> handle.
    "_8022": (["INT", "INT", "INT"], ["STRING"], False),
    # Round 5. The search converges here: rounds 6 and 7 solve nothing new.
    "_7801": (["STRING"], ["INT"], False),
    "_1143": (["INT"], [], False),
    # Round 6. Round 7 solves nothing new: the search has converged, and what
    # remains needs an opcode identified in a client rather than inferred (G4).
    "_1628": ([], ["INT"], False),
    "_1139": (["INT"], [], False),

    # ---------------------------------------------------------------
    # Skill-guide Overview tab (scripts 9150..9199 / 9176), remeasured
    # 2026-08-03. LostCity has no reference (PORTING_GUIDE §5). Names from
    # xrsps Opcodes.ts where it has them; arities from call-site balance and
    # `cs2 decompile --override` scoring against cache.osrs239.
    #
    # 211/215: script 9181 — `push 1; push $com; push -1; 211; pop_int_discard;
    # 215; pop_string_local; 8003`. Only (3i->1i)+(()->1s) and two weaker
    # pairings decompile; the three-arg form matches the IF_GETCOMPONENTPARAM
    # shape and is the one kept. 215's string is an array handle (8003'd).
    "_211": (["INT", "INT", "INT"], ["INT"], False),
    "_215": ([], ["STRING"], False),
    # 212: scripts 9179/9186 — after `.cc_find`, `push 1; 212; pop_int_discard;
    # 213…`. (1i->1i) and (0i->0i) both decompile; (1i->0i) fails on the
    # discard. Kept as (1i->1i): start_index in, child-count out — the CC
    # children-find family (203/205) plus a count the scripts discard.
    "_212": (["INT"], ["INT"], False),
    # 8018: script 9183 — `push $s; push "||"; 8018; pop_string_local; 8003`.
    # Two strings in, one string (array handle) out: split.
    "_8018": (["STRING", "STRING"], ["STRING"], False),
    # 8012: script 9194 — after ARRAY_SORT, `push $arr; 8012` twice. One
    # string in, nothing out. Meaning unknown (mutate-in-place); arity only.
    "_8012": (["STRING"], [], False),
    # 8023: every Overview site is `push $arr; push N; 8023` then N
    # `pop_array_int` writes. Array handle + length in, nothing out: resize.
    "_8023": (["STRING", "INT"], [], False),
    # 8024: script 9194 — `push $arr; push $value; push 0; 8024` (type 0 =
    # int). Array + int value + type in, nothing out: append. Typed like
    # ARRAY_COUNT_MATCHES when the type names a string; Overview only uses
    # int. xrsps names the broader op ARRAY_INSERT (with an index); this
    # cache's sites have no index.
    "_8024": (["STRING", "INT", "INT"], [], False),
    # 2704: IF_SETPARAM. xrsps WidgetOps: (param, value, uid, child, type)
    # with typed value. Script 9176: five ints, nothing consumed. Override
    # scoring `--override 2704:5,0,0,0` recovers 9176; the vendored
    # if_haschild_modal (2i->1i) from an older deob does not. LOCAL_NAMES
    # renames the row away from that stale spelling.
    "IF_SETPARAM": (["INT", "INT", "INT", "INT", "INT"], [], False),

    # ---------------------------------------------------------------
    # Settled from call sites rather than by search, against cache.osrs239.
    #
    # `cs2 infer-arity` needs a witness script to interpret end to end, so an
    # opcode that only ever appears beside another unknown is invisible to it.
    # A call site says a great deal on its own: between two statement boundaries
    # the operand stack starts and ends empty, so where every other op in the
    # run has a signature, the pushes before and the pops after pin what the
    # unknown took and left. `cs2 disassemble` prints the per-op int/string
    # effects this reads.
    #
    # Only opcodes where one arity survives at *every* such site are here.
    "_1137": (["INT"], [], False),
    "_1145": (["INT"], [], False),
    "_1151": (["STRING", "STRING", "STRING"], [], False),
    "_3221": (["INT", "INT", "INT", "INT", "INT", "INT"], [], False),
    # 4123 pushes a string too, which the call-site pass missed because every
    # site it could use had a `gosub` in the same run. `--override 4123:0,3,0,1`
    # decompiles 14 more scripts than `0,3,0,0`; nothing else in the space beats it.
    "_4123": (["STRING", "STRING", "STRING"], ["STRING"], False),
    "_7041": (["INT", "STRING"], [], False),
    "_7042": (["INT", "STRING"], [], False),
    "_7615": (["INT"], [], False),
    # 8019: was recorded as (str,str)->() from an incomplete call-site pass.
    # Script 9153 pins the push: after `push ""; push $arr; push "||::p||";
    # 8019` the next gosub is 9182 which takes (int, str, str). Only a string
    # out leaves the stack right; xrsps ARRAY_JOIN agrees (array, sep) -> str.
    "_8019": (["STRING", "STRING"], ["STRING"], False),
    # Loot-tracker native store (rev-239). Settled from script 7166's
    # bytecode, not a client: every site sits between empty-stack statement
    # boundaries (or is pinned by the pushes immediately before and the
    # typed pop immediately after). `cs2 decompile` with these five overrides
    # and nothing else recovers 7166 end-to-end; flipping any one arity fails
    # it. The older deob that supplied the 7500/8000 block below predates this
    # range (local_commands.py header / tools/README.md G4), which is why
    # infer-arity alone never saw them — they only ever appear beside each
    # other until the call-site pass breaks the cycle.
    #   $n = _7605(0, ~script1046(_7601, 10), 1);
    #   while ($i < $n) { $id = _7606($i); $name = _7602($id); … }
    #   $name = _7630($id);   # second list, string-keyed
    "_7601": ([], ["INT"], False),
    "_7602": (["INT"], ["STRING"], False),
    "_7605": (["INT", "INT", "INT"], ["INT"], False),
    "_7606": (["INT"], ["INT"], False),
    "_7630": (["INT"], ["STRING"], False),
    # Same call-site method, scripts 7200 / 1792 (loottools chrome helpers
    # gosub'd from 7166). 7200 and 1792 are structural twins: begin→count,
    # index→name, then a three-arg write. 7401's args are INT,STRING,INT —
    # the two stacks make that (2,1,0,0) at the VM regardless of interleaving.
    "_7619": ([], ["INT"], False),
    "_7620": (["INT"], ["STRING"], False),
    "_7625": ([], ["INT"], False),
    "_7626": (["INT"], ["STRING"], False),
    "_7401": (["INT", "STRING", "INT"], [], False),
    # 7408: every site is `_7408(<kind>, <string>, <a>, <b>) -> int` — three
    # ints and one string in, one int out, read off the local stack deltas at
    # 7133 pc149, 4298 pc84, 7158 pc18 and 7199 pc9 (four sites, one reading).
    # Call-site evidence ONLY: it cannot be scored, because every script that
    # uses it also holds other unknowns (7609/7404), so `cs2 decompile` fails
    # on all of them under every candidate. `cs2 infer-arity` says "no arity
    # works" for the same reason. Weaker evidence than the rest of this block;
    # revisit if a client that implements the 7400 range turns up.
    # Documented in chrome_panels §7.6 as un-tryable via `--override`
    # (ints-then-strings only); LOCAL_BASIC keeps the interleaved order.
    "_7408": (["INT", "STRING", "INT", "INT"], ["INT"], False),
    # 7407: list/store length for a kind id — `_7407($kind) -> $n`, sites
    # 7202/7212/7221 with nothing else on the stacks. `cs2 infer-arity` calls
    # this "2 candidates, under-determined"; scoring settles it, which is the
    # documented tiebreak (tools/README.md, method 3): over its six witnesses
    # (1,0,1,0) decompiles 5 and every other candidate tried — (0,0,1,0),
    # (1,0,0,0), (2,0,1,0), (1,0,0,1) — decompiles 0.
    "_7407": (["INT"], ["INT"], False),

    # ---------------------------------------------------------------
    # Read out of a deobfuscated client, not inferred.
    #
    # The interpreter dispatches by opcode hundred and each handler works the
    # operand stacks directly, in fixed idioms:
    #
    #     field870[++Statics.field3297 - 1] = v      push int
    #     field870[--Statics.field3297]              pop  int
    #     Statics.field3297 -= N                     pop  N ints
    #     field877[++Statics.field4953 - 1] = v      push string   (etc.)
    #
    # so an arity can be read rather than searched for. Only straight-line
    # handlers are taken: a block with a branch may push on one path and not the
    # other, and counting the text would report both.
    #
    # Checked before it was trusted. Over the opcodes both this client and the
    # vendored tables describe, 358 agree. The rest are era drift (SOUND_SONG
    # gained arguments between the two) or the IF_* family's shared component
    # pop, which happens outside the per-opcode block. None of those opcodes are
    # taken from here -- only the ones nothing else describes at all.
    #
    # The client is *older* than cache.osrs239: it implements 7500..7507 and
    # 8000..8001 and nothing above, while the cache uses opcodes up to 8026. So
    # this settles the long-established ranges and says nothing about the new
    # ones, which is why the 7600+ and 8005+ opcodes are still unknown.
    "_1130": (["STRING"], [], True),
    "_1131": (["INT", "INT"], [], True),
    "_1132": (["INT", "STRING"], [], True),
    "_1133": (["INT"], [], True),
    "_1134": (["INT"], [], True),
    "_1135": (["STRING"], [], True),
    "_1136": (["INT"], [], True),
    "_1138": (["INT"], [], True),
    "_1142": (["INT", "INT"], [], True),
    "_1144": (["INT"], [], True),
    "_1146": (["INT"], [], True),
    "_1147": (["INT"], [], True),
    "_1148": (["INT", "INT"], [], True),
    "_1149": (["INT", "INT"], [], True),
    "_1150": (["STRING"], [], True),
    "_1207": (["INT"], [], True),
    "_1208": (["INT"], [], True),
    "_1209": (["INT", "INT"], [], True),
    "_1210": (["INT"], [], True),
    "_1434": ([], [], True),
    "_1435": ([], [], True),
    "_1708": ([], [], True),
    "_2708": (["INT"], [], False),
    "_2709": (["INT"], [], False),
    "_3146": (["INT"], [], False),
    "_3148": ([], [], False),
    "_3149": ([], ["INT"], False),
    "_3150": ([], ["INT"], False),
    "_3151": ([], ["INT"], False),
    "_3152": ([], ["INT"], False),
    "_3153": ([], ["INT"], False),
    "_3154": ([], ["INT"], False),
    "_3155": (["STRING"], [], False),
    "_3156": ([], [], False),
    "_3158": ([], ["INT"], False),
    "_3159": ([], ["INT"], False),
    "_3160": ([], ["INT"], False),
    "_3161": (["INT"], ["INT"], False),
    "_3162": (["INT"], ["INT"], False),
    "_3163": (["STRING"], ["INT"], False),
    "_3164": (["INT"], ["STRING"], False),
    "_3165": (["INT"], ["INT"], False),
    "_3166": (["INT", "INT"], ["INT"], False),
    "_3167": (["INT", "INT"], ["INT"], False),
    # 3168: skipped, 2i/0s -> 0i/9s is a loop, not an arity
    "_3169": ([], [], False),
    "_3174": (["INT"], [], False),
    "_3175": ([], ["INT"], False),
    "_3176": ([], [], False),
    "_3177": ([], [], False),
    "_3178": (["STRING"], [], False),
    "_3179": ([], [], False),
    "_3180": (["STRING"], [], False),
    "_3185": (["INT"], [], False),
    "_3186": ([], ["INT"], False),
    "_3211": ([], [], False),
    "_3219": (["INT"], [], False),
    "_3220": (["INT", "INT"], [], False),
    "_3222": (["INT", "INT", "INT", "INT"], [], False),
    "_3331": ([], ["INT"], False),
    "_3332": (["INT"], ["INT"], False),
    "_3333": ([], ["STRING"], False),
    "_6764": (["INT", "INT"], ["INT", "INT"], False),
    "_7463": (["INT"], [], False),
    "_7900": (["INT"], [], False),
    "_7901": ([], ["INT"], False),
    "_8000": (["INT"], [], False),
    "_8001": (["INT", "INT", "INT"], [], False),
    # ---------------------------------------------------------------
    # Stale signatures, corrected against cache.osrs239.
    #
    # These are opcodes the vendored tables *do* describe and describe wrongly
    # for this revision -- G4's "some known opcode's signature has drifted"
    # residue, finally isolated. Each was found by taking the deobfuscated
    # client's reading, installing it with `cs2 decompile --override`, and
    # counting: every one below decompiles strictly more scripts than the
    # recorded signature does. The gain per opcode is noted.
    #
    # Mostly one shape of error -- a command that returns a pair or a quad
    # recorded as returning one. WORLDMAP_LISTELEMENT_START reads four values,
    # not one, which is why nothing in the world-map panel decompiled.
    "DB_GETROW": (["INT"], ["INT"], False),                                # +19
    "_6618": (["INT"], ["INT", "INT", "INT", "INT"], False),               # +14
    "_6638": (["INT", "INT"], ["INT", "INT"], False),                      # +14
    "MEC_SPRITE": (["INT"], ["INT", "INT"], False),                        # +14
    "_6623": (["INT"], ["INT", "INT"], False),                             # +13
    "WORLDMAP_LISTELEMENT_START": ([], ["INT", "INT", "INT", "INT"], False),  # +13
    "WORLDMAP_LISTELEMENT_NEXT": ([], ["INT", "INT", "INT", "INT"], False),   # +13
    "MEC_TEXT": (["INT"], ["STRING", "STRING"], False),                    # +13
    "MEC_CATEGORY": (["INT"], ["INT", "INT"], False),                      # +10
    "SOUND_SONG": (["INT", "INT", "INT", "INT", "INT"], [], False),        # +1
    # ---------------------------------------------------------------
    # Solved by scoring, against cache.osrs239.
    #
    # `infer-arity` needs a witness script to interpret end to end and reported
    # "no arity works" for these; the call-site pass could not use them either,
    # because every run they appear in has a `gosub` or another unknown in it.
    # What settles them is the count: install a candidate with
    # `cs2 decompile --override`, decompile only the scripts that *use* that
    # opcode, and take the arity that decompiles the most -- accepted only where
    # one candidate stands alone.
    #
    # 1703 is the largest single blocker in the cache: one int in, one int out,
    # decompiling 45 of its 87 scripts where no other arity in the space
    # decompiles any, and 45 more scripts across the cache besides.
    "_1703": (["INT"], ["INT"], True),
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
