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
    # The official deob's older method12650/210 is 5 -> 0, but every rev-239
    # cache call site has the evolved ABI: six inputs and one boolean-like
    # result (the same net -5). Both the six-push and seven-push/outer-value
    # shapes then reach their following comparison with exactly two operands.
    "_210": (["INT", "INT", "INT", "INT", "INT", "INT"], ["INT"], False),
    # The cache's 7200-range collection iterators consume their selectors;
    # vendored Command.kt recorded only the result side.
    "_7206": (["INT"], ["INT"], False),
    "_7209": (["INT", "INT"], ["INT"], False),
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
    # 7622/7623: source-ignore add/remove (script 1791); mirror 7616/7617.
    "_7622": (["STRING"], [], False),
    "_7623": (["STRING"], [], False),
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
    # The rev-239 client pops the final base-type code, then a value from the
    # selected int/string/long stack, then the param id. The active component
    # comes from the handler preamble and the operand selects its dot form.
    "_1704": (["INT", "NONE", "INT"], [], True),
    # 2929 has no fixed signature. The rev-239 client calls `method989` — pop a
    # descriptor string, then one value per character, 'i' from the int stack
    # and anything else from the string stack — then pops three fixed ints.
    # DESCRIPTOR_ARGS handles that shape; this tuple only supplies its name.
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
    # The final base-type code tells method6560 which stack holds the inserted
    # value. `NONE` is a deliberate placeholder interpreted by TYPED_POP.
    "_8024": (["STRING", "NONE", "INT"], [], False),
    # 2704: IF_SETPARAM. xrsps WidgetOps: (param, value, uid, child, type)
    # with typed value. Script 9176: five ints, nothing consumed. Override
    # scoring `--override 2704:5,0,0,0` recovers 9176; the vendored
    # if_haschild_modal (2i->1i) from an older deob does not. LOCAL_NAMES
    # renames the row away from that stale spelling.
    # method3056 pops (uid, child, type) together, then a typed value and the
    # param. Source order is therefore (param, value, uid, child, type).
    "IF_SETPARAM": (["INT", "NONE", "INT", "INT", "INT"], [], False),

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
    # Dormant notification ABI in this cache: two strings, two ints, then an
    # int result. The rev-239 client handler is only a no-argument stub that
    # pushes 0, so the cache call sites are the source-order evidence.
    "local_notification": (["STRING", "STRING", "INT", "INT"], ["INT"], False),
    # Two menu indices, generated submenu text, then the explicit component.
    # The older official client has no 2311 case, but both cache witnesses use
    # this same inter-bank order.
    "if_setopsubmenu": (["INT", "INT", "STRING", "COMPONENT"], [], False),
    # Companion clear/reset form: option index and explicit component.
    "_2310": (["INT", "COMPONENT"], [], False),
    # 4123 pushes a string too, which the call-site pass missed because every
    # site it could use had a `gosub` in the same run. `--override 4123:0,3,0,1`
    # decompiles 14 more scripts than `0,3,0,0`; nothing else in the space beats it.
    "_4123": (["STRING", "STRING", "STRING"], ["STRING"], False),
    "_7041": (["STRING", "INT"], [], False),
    # Source order is string then selector. The physical client pops the two
    # independent banks directly, so a net-only table cannot recover this.
    "_7042": (["STRING", "INT"], [], False),
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
    # Tier-B loot row painters (scripts 4298 / 4452) and the remove twin of
    # 7401. Settled from call-site stack deltas + `cs2 decompile --override`
    # where the push order is ints-then-strings-legal; interleaved signatures
    # are call-site only (same class as 7401). See chrome_panels_server_reqs
    # and the chrome-panels plan.
    #   7404: twin of 7401 at 7199/7222/1791 — (kind, string, flag) → ()
    #   7608: () → int — script 7179 `_7605(0, _7608, 2)`
    #   7609: (string) → int — script 4298 count for a string-keyed group
    #   7610: (int) → int — script 4452 count for an int-keyed group
    #   7611: (string, int) → (int, int) — 4298 index → (obj, qty)
    #   7612: (int, int) → (int, int) — 4452 index → (obj, qty)
    "_7404": (["INT", "STRING", "INT"], [], False),
    "_7608": ([], ["INT"], False),
    "_7609": (["STRING"], ["INT"], False),
    "_7610": (["INT"], ["INT"], False),
    "_7611": (["STRING", "INT"], ["INT", "INT"], False),
    "_7612": (["INT", "INT"], ["INT", "INT"], False),
    # 7628: script 7192 (LOOTTRACKER_ADD_LOOT) — push name, obj, qty, eventId
    # then native store write. RuneLite ScriptPreFired args match that order.
    "_7628": (["STRING", "INT", "INT", "INT"], [], False),

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
    # method12438/1132 calls method7303(popString(), popInt()).
    "_1132": (["STRING", "INT"], [], True),
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
    # method7565 always consumes the string key and pushes an int result; the
    # component itself is implicit in this active-form opcode.
    "_1708": (["STRING"], ["INT"], True),
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
    # method3100/6639 and /6640 push element id then packed coord: two ints.
    "WORLDMAP_LISTELEMENT_START": ([], ["INT", "INT"], False),
    "WORLDMAP_LISTELEMENT_NEXT": ([], ["INT", "INT"], False),
    # method3100/6693 pushes one text string (or an empty default).
    "MEC_TEXT": (["INT"], ["STRING"], False),
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
    # method12337 resolves the param config and pushes its declared int/string/
    # long value from the active component. Its result is not statically int.
    "_1703": (["INT"], [], True),

    # ---------------------------------------------------------------
    # Read out of the *rev-239* client, which the block above did not have.
    #
    # `Deobfuscator/src_osrs239_rl1_12_33/deob/Statics.java`, the same tree
    # `Deobfuscator/instr/src/` compiles. `Statics.method6889` is a range ladder
    # from opcode to group handler; the citation on each line is the handler and
    # the case. Ranges used here, re-derived from the ladder rather than read by
    # eye (reading it by eye put the 4000s off by one hundred):
    #
    #     100..1000 method4548   1700..1800 method12337   2500..2600 method4787
    #     2600..2700 method8067  4100..4200 method5814    4200..4300 method2965
    #     8000..8100 method12336
    #
    # The three stacks, and which is which, are not guessed: `method7522` is
    # `pushValueOfType` -- the deobfuscation left its exception strings intact --
    # and it switches on the base var type to
    #
    #     type 1 -> class42.field258 / field259    (Long) ..... the long stack
    #     type 2 -> class42.field252 / field3897   Object ..... the STRING stack
    #     type 3 -> class42.field254 / field257    (Integer) .. the INT stack
    #
    # so `field252` is where strings live, despite `field258` being the one that
    # looks like a string stack from its neighbours. One slot is 818837815 on
    # field257, 1907375409 on field259 and -1540699919 on field3897; a multi-slot
    # move is written as the multiple wrapped to signed 32 bits, so
    # `field257 -= -1838453851` is a pop of three ints, not a push.
    #
    # Two things make counting the idioms in a case body wrong on its own, and
    # both bit before they were caught:
    #
    #  - a handler can pop in its *preamble*, before it looks at the opcode.
    #    method4787 and method8067 both open by popping a component id, so every
    #    opcode in 2500..2699 takes one more argument than its own case shows.
    #    `if_getx` 2500 in the vendored table confirms it: one COMPONENT in.
    #  - a case can push through a helper. `method2627` pushes 0 or 1 on every
    #    path -- it is "set the active component and push whether it was found" --
    #    so opcodes 210 and 217..221 push an int no idiom in their body shows.
    #    Every opcode below was checked against a transitive closure of which
    #    methods in the tree touch a stack pointer; none of them reach one.
    #
    # `dot` is whether the case reads the handler's `var2`, the per-instruction
    # flag the interpreter builds from `Script.field272[pc]`. That matches the
    # corpus: 208 reads it and is the one opcode here whose operand byte is 1 at
    # its only call site, and 216, which does not read it, is 0 at all 56 of its.
    "_208": ([], ["INT"], True),                    # method4548 case 208
    "_216": (["INT", "NONE", "INT"], ["INT"], False),  # method4548 case 216
    "_1707": ([], ["INT"], True),                   # method12337 case 1707
    # 2506/2624: nothing in the case body, one int from the handler preamble.
    "_2506": (["COMPONENT"], ["INT"], False),       # method4787 preamble + case
    "_2624": (["COMPONENT"], ["INT"], False),       # method8067 preamble + case
    # 4127 is the least ambiguous case in the client: it is a literal
    # `Integer.parseInt(String)` in a try/catch, pushing the value then 1, or
    # 0 then 0 on NumberFormatException. One string in, two ints out.
    "_4127": (["STRING"], ["INT", "INT"], False),   # method5814 case 4127
    "_4223": ([], [], False),                       # method2965 case 4223
    # The 8000 block is the array family (_8022 ARRAY_NEW above hands out the
    # handles). The handle is a string -- `field252` -- and is the first push at
    # every call site, so it leads the argument list, matching _8003/_8018/
    # _8019/_8023/_8024. The trailing int of the four is a *type code*: the
    # client feeds it to `method6560`, which resolves a base var type rather
    # than reading a value, so all four arguments are int-stack.
    "_8002": (["STRING"], ["INT"], False),                       # method12336
    "_8005": (["STRING", "NONE", "INT", "INT", "INT"], ["INT"], False),
    "_8006": (["STRING", "NONE", "INT", "INT", "INT"], ["INT"], False),
    "_8007": (["STRING", "NONE", "INT", "INT", "INT"], ["INT"], False),
    "_8010": (["STRING", "NONE", "INT", "INT", "INT"], [], False),
    "_8011": (["STRING", "INT", "INT", "INT"], [], False),       # method12336
    "_8014": (["STRING", "INT", "INT"], [], False),              # method12336
    "_8015": (["STRING", "STRING", "INT", "INT", "INT"], [], False),  # method12336
    "_8020": (["INT", "INT"], ["STRING"], False),                # method12336
    # 8026 is polymorphic in the client -- it ends in `method7522`
    # (pushValueOfType), so what it pushes is the array's element type, not a
    # fixed stack. It is recorded statically anyway because every one of its 61
    # call sites in cache.osrs239 indexes an int-typed array: each is followed
    # immediately by `pop_int_discard` or an int-consuming op. If a string-typed
    # array ever reaches it this needs a LOCAL_KINDS entry, as db_getfield has.
    "_8026": (["STRING", "INT"], ["INT"], False),                # method12336
    # Second pass, once the block above stopped hiding these behind other
    # unknowns. Same tree, same method.
    "_4224": (["INT"], ["INT"], False),          # method2965, the `var0 != 4224` arm
    "_8025": (["STRING", "INT", "NONE", "INT"], [], False),
    "_8027": (["STRING", "STRING"], [], False),                  # method12336
    # 8009 is polymorphic like 8026 — it pushes to the int stack or the string
    # stack depending on the array's element type. Recorded as int because all
    # three of its call sites in cache.osrs239 are followed by `pop_int_local`
    # or `max`, which only take one.
    "_8009": (["STRING"], ["INT"], False),                       # method12336
    # 7627 has no case in this client (method10020, the 7600 range, is a bare
    # `return 2`). Solved instead by `cs2 infer-arity` against cache.osrs239:
    # one witness script, and exactly one arity in the space lets it interpret
    # to its end with every return matching its own epilogue.
    "_7627": ([], [], False),

    # ---------------------------------------------------------------
    # No case in the rev-239 client either (their handlers exist but skip these
    # ids), and `cs2 infer-arity` calls each one "2 candidates, under-determined".
    # The two candidates are always the same pair — (n in, 1 out) against
    # (n-1 in, 0 out) — which balance identically because they differ by one
    # slot at each end. Balance alone cannot separate them; the call sites can,
    # and they are unanimous. Every single use of all three reads
    #
    #     push <args> ; <opcode> ; push_constant_int 1 ; branch_equals
    #
    # and `branch_equals` pops two ints, one of which is that constant. So the
    # opcode leaves an int behind, which settles it as the (n in, 1 out) member
    # of each pair. All three are boolean queries, which is also what the rest
    # of the 6700–6999 range looks like.
    "_6758": (["INT"], ["INT"], False),          # 9 witnesses, every site
    "_6803": (["INT", "INT"], ["INT"], False),   # 9 witnesses, every site
    "_6951": (["INT"], ["INT"], False),          # 5 witnesses, every site

    # ---------------------------------------------------------------
    # The rest of the ids this client's handlers skip, settled the same way:
    # what the ops around a call site consume is what the call site left.
    # `infer-arity` reports "no arity works" for most of these because the
    # scripts holding them hold other unknowns too, so no witness interprets
    # end to end — but a single run between two statement boundaries still
    # pins one opcode when everything else in it is known.
    #
    # The consumer named on each line is the whole argument:
    #   6854/6859   `push $a; push $b; <op>; push 1; branch_equals` — and
    #               branch_equals pops two ints, one of them that constant.
    #   7043        `<a string>; push 6; <op>; push 1; branch_equals`. The
    #               string is `push_string_local` at one site and `~script6690`
    #               at the other, which returns one (its caller stores the
    #               result with `pop_string_local`).
    #   7451        `push $a; <op>; switch` — a switch pops exactly one int.
    #   7600        four pushes, then a new statement begins; nothing consumes
    #               a result. Same shape as _7628 beside it.
    #   7800        a string and an int, then a new statement (`cc_find`,
    #               `if_sethide`) which supplies its own operands.
    #   7803/7804/7805/7813/7814
    #               one int in, one out: each is followed either by
    #               `pop_int_local` directly or by `push 1; add`. 7813/7814 sit
    #               under `~script1045`, whose trailer says 2i — which is what
    #               makes the push before them that proc's argument and not
    #               theirs.
    #   7807        no push before it at script 7551 pc 0 — it is the script's
    #               first instruction — and `pop_int_local` after.
    #   7820        `lowercase` gives it a string, and the `~script46` that
    #               follows has a 1i/1s trailer: the int can only have come
    #               from here, so it pushes one and not a string.
    "_6854": (["INT", "INT"], ["INT"], False),
    "_6859": (["INT", "INT"], ["INT"], False),
    # `method3101` handles only 6809, so the official runtime reports 6860 and
    # 6861 as unhandled. In dormant cache paths both are no-argument int
    # producers: each follows the other operand and is consumed by multiply.
    "_6860": ([], ["INT"], False),
    "_6861": ([], ["INT"], False),
    "_7043": (["STRING", "INT"], ["INT"], False),
    "_7451": (["INT"], ["INT"], False),
    # The official rev-239 dispatch (`method12357`) handles only 7463. Targeted
    # `cs2trace` runs therefore record this 7453..7456 block as unhandled
    # (return code 2,
    # no stack mutation). The cache nevertheless contains them behind paths
    # that are unreachable in this client build. Their call sites settle the
    # compiler contract: each consumes one int and leaves the int immediately
    # compared with 1.
    "_7453": (["INT"], ["INT"], False),
    "_7454": (["INT"], ["INT"], False),
    "_7455": (["INT"], ["INT"], False),
    "_7456": (["INT"], ["INT"], False),
    "_7600": (["STRING", "INT", "INT", "INT"], [], False),
    "_7800": (["STRING", "INT"], [], False),
    "_7803": (["INT"], ["INT"], False),
    "_7804": (["INT"], ["INT"], False),
    "_7805": (["INT"], ["INT"], False),
    # `method11128` likewise implements only 7900/7901; the official runtime
    # refuses this older hiscores block. Corpus inference supplies the dormant
    # source contract: lookup by game index returns rank, overall returns the
    # rank and split score, and 7816 supplies a status/result int.
    "_7806": (["INT"], ["INT"], False),
    "_7807": ([], ["INT"], False),
    "_7808": ([], ["INT", "INT"], False),
    "_7813": (["INT"], ["INT"], False),
    "_7814": (["INT"], ["INT"], False),
    "_7816": ([], ["INT"], False),
    "_7820": (["STRING"], ["INT"], False),

    # ---------------------------------------------------------------
    # Measured in the running client, not read from it.
    #
    # `Deobfuscator/instr/src/CS2Trace.java` samples the three operand-stack
    # pointers either side of every executed opcode; `3draster` drives the
    # client through all 9,725 scripts in the cache with
    # `tools/perf/cs2_sweep.sh`, so an opcode no screen reaches still runs.
    # 560 opcodes executed; 307 of them agreed with this table exactly, which
    # is the reason to believe the disagreements.
    #
    # A measured *net* is `pushes - pops`, so it does not by itself split an
    # entry into arguments and results. Where the split below is stated it is
    # because only one split is consistent with both the net and the handler
    # source; where it was not, the entry was left alone rather than guessed.
    "cc_setop": (["INT", "STRING"], [], True),            # net (-1,-1) x51047
    # method12438 pops the explicit COMPONENT in the >=2000 preamble, then
    # case 1300 pops OPINDEX and OP. Keep Command.kt's source/bytecode order:
    # the measured stack deltas establish the banks, but cannot establish how
    # values from the independent int and string stacks were interleaved.
    "if_setop": (["INT", "STRING", "COMPONENT"], [], False),  # net (-2,-1) x3725
    "if_getop": (["COMPONENT", "INT"], ["STRING"], False),    # net (-2,+1) x11
    "oc_iop": (["OBJ", "INT"], ["STRING"], False),        # net (-2,+1) x47
    "openurl": (["STRING", "INT"], [], False),            # net (-1,-1) x11
    "mes": (["STRING"], [], False),                       # net (0,-1)
    "clan_kickuser": (["STRING"], [], False),             # net (0,-1)
    "chat_sendpublic": (["STRING", "INT"], [], False),    # net (-1,-1)
    "chat_playername": ([], ["STRING"], False),           # net (0,+1) x186
    "clan_getchatownername": ([], ["STRING"], False),     # net (0,+1) x7
    "_4124": ([], ["INT"], False),                        # net (+1,0) x30 — was a string
    "_6623": (["INT"], ["INT"], False),                   # net (0,0)
    "mec_category": (["INT"], ["INT"], False),            # net (0,0)
    "_8000": (["STRING", "STRING"], [], False),           # net (0,-2) x26
    "_8001": (["STRING", "INT", "INT"], [], False),       # net (-2,-1)
    # method10962/5031 pushes into independent physical banks in this source
    # order. The interleaving matters to the tuple reconstructed by the IR.
    "chat_gethistoryex_byuid": (["INT"], ["INT", "INT", "STRING", "STRING",
                                          "STRING", "INT", "STRING", "INT"], False),
    # Rev-239 gameframe scripts pass (x, y) and consume the resulting bound.
    "safearea_getmaxy_alt": (["INT", "INT"], [], False),
    # The neighbouring safe-area host command consumes its mode selector. The
    # vendored camera-era name/signature predates this opcode assignment.
    "cam_getyaw": (["INT"], [], False),

    # ---------------------------------------------------------------
    # Active-component ("dot") forms these rows had marked False.
    #
    # For a cc_* command the operand byte is not data, it is the flag that picks
    # `.cc_foo` over `cc_foo` — the interpreter builds it from
    # `Script.field272[pc]` and passes it to the group handler as `var2`, which
    # selects `Statics.field5113` over `field2369`. The generator already writes
    # `dot = 100 <= opcode < 2000` for anything it takes from the client's stack
    # table; these eleven came from LOCAL_BASIC instead, which had to state it
    # and stated False.
    #
    # A False here is not a harmless mislabel: the decompiler stops printing the
    # '.', so the compiler writes operand 0 where the cache has 1.
    #
    # Membership is the client's, not the operand's. Thirteen BASIC opcodes hold
    # operand 1 somewhere in cache.osrs239, but "holds a 1" is not evidence of a
    # dot form, and taking it as such is wrong for four of them:
    #
    #   102  cc_deleteall  `method4548`'s case for it pops the component id off
    #                      the int stack and never touches `var2`. Not a dot
    #                      form, on the client's own evidence. (The RuneStar
    #                      reference happens to agree; the client is why.)
    #   103                no case in this client and no preamble flag: unknown,
    #                      so left alone rather than guessed either way.
    #   4123, 4124         outside the cc_ range, and `method5814` never reads
    #                      `var2` — the client ignores that byte for them. Worth
    #                      22 scripts, deliberately not taken: printing
    #                      `._4123` would assert an active-component form that
    #                      does not exist.
    #
    # The nine below are each confirmed at the client: 209/210/212/213 and 1927
    # read `var2` in their own case, and 1128/1506/1624/1704 are in ranges whose
    # handler preamble (method4754/method1470/method6296/method12337) selects
    # field5113 over field2369 before it looks at the opcode at all.
    "_209": ([], ["INT"], True),
    "_210": (["INT", "INT", "INT", "INT", "INT", "INT"], ["INT"], True),
    "_212": (["INT"], ["INT"], True),
    "_213": ([], ["INT"], True),
    "_1128": (["INT", "INT"], [], True),
    "_1506": ([], ["INT"], True),
    "_1624": ([], ["INT"], True),
    "cc_setcomponentparam": (["INT", "NONE", "INT"], [], True),
    # method1005/1927 reads only the implicit active component; opcode 2927's
    # explicit component is supplied by the generic IF_* preamble.
    "cc_callonresize": ([], [], True),
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
    # Rev-239 call sites carry either six or seven int operands and immediately
    # consume one int result. Preserve the cache's variable payload explicitly;
    # the official deob's older 5 -> 0 implementation documents the ABI drift.
    210: "VARIADIC_INT_RESULT",
    216: "TYPED_POP",
    2929: "DESCRIPTOR_ARGS",
    1703: "ACTIVE_PARAM",
    2703: "COMPONENT_PARAM",
    1704: "TYPED_POP",
    2704: "TYPED_POP",
    8005: "TYPED_POP",
    8006: "TYPED_POP",
    8007: "TYPED_POP",
    8010: "TYPED_POP",
    8024: "TYPED_POP",
    8025: "TYPED_POP",
    # method4380 parses the descriptor/script-id payload just like the other
    # 1400-range callbacks; the vendored table mislabeled these as Basic.
    1434: "CLIENTSCRIPT",
    1435: "CLIENTSCRIPT",
    7502: "DB_GETFIELD",
    7500: "DB_FIND",
    7507: "DB_FIND",
    7508: "DB_FIND",
    7510: "DB_FIND",
}
