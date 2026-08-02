# `runclientscript*` — pushing a runtime mix of ints and strings to a clientscript

> **What this is.** The one command a rev-230 server has for driving a panel
> that builds itself. This doc is the whole path — the command, the bytecode,
> the packet, the CS2 argument bind — plus the two defects the path had that
> only a *mixed* payload could expose, and where the permanent checks now are.
>
> Two claims in this repo's own docs about this feature were false. Both are
> corrected in place (`LOSTCITY_PORT_TRIAGE.md` §7.4 and its symptom table,
> `UI_ERA_PORTING_GUIDE.md` §4) and restated in §6 here, because both were
> load-bearing enough to have shaped a plan.

---

## 1. What was already there, and what was not

The opcode exists and predates this work. Measured, not assumed:

| layer | where | state |
|---|---|---|
| opcode | `serverscript/ss_opcode.h:424` `SS_OP_RUNCLIENTSCRIPTVARARG = 11003` | present |
| meta | `gen_opcode_meta.py:196` `(11003, 1, 0, 0, 0)` + `STRUCTURAL_VARIADIC:255` | present |
| generated | `ss_meta.gen.h:819` `{1,0,0,0,1,1,…}` — `variadic = 1` | present |
| compiler | `ssc_compile.c:521-536` (the `*` token) and `:645-687` (the vararg list) | present |
| VM host | `mock230_scripts.c` `case SS_OP_RUNCLIENTSCRIPTVARARG` | present |
| encoder | `mock230_encode.c:401-426` `mock230_send_run_clientscript_mixed` | present |
| coverage | `mock230_opcode_coverage.gen.h:287` | present |

Regenerating `gen_opcode_meta.py` and `gen_opcode_coverage.py` produces no diff,
which is the check that the above is not prose.

**What was not there was any use of the string half.** Content had two call
sites and both were all-int:

```
skill_guide.rs2:136     runclientscript*(^clientscript_skill_guide_init)($skill, ^skill_guide_tab_default, 0, 0);
slayer_rewards.rs2:279  runclientscript*(^clientscript_slayer_task_list_init)(slayer_rewards:popup, 0, 0);
```

So the `types[i] == 's'` branch of the host command, the `'s'` branch of the
encoder, and the whole string path through the client had never run outside a
unit test. Two defects were sitting in that path, and §4 is both of them.

## 2. The shape, and why it is this engine's own decision

**The reference is silent.** `[command,runclientscript]` is commented out in
LostCity's `engine.rs2` — the 2004 protocol has no `RUNCLIENTSCRIPT` at all,
because a 2004 interface ships its components and the server addresses them with
`if_settext`. A rev-230 panel is a *program*: `chatmenu` has a root and an empty
layer, `shopmain` has no `onload=`, `farming_view`'s cells are `cc_create`d.
There is nothing for `if_settext` to address, so the server's only way in is to
run the clientscript.

That puts this squarely in `PORTING_GUIDE.md` §5.1 — the client already
implements the feature; the server's job is to drive it — and it means the
command's shape is **this engine's decision, with no reference to port**. The
decision recorded in `gen_opcode_meta.py`, and not re-taken here, is: use the
reference's own *vararg convention* rather than invent a parallel one.

`queue*(queue, delay)(args…)` compiles to `QUEUEVARARG` (2094) as: declared
arguments, then the vararg values, then a type string describing them. `JOIN_STRING`
and `GOSUB_WITH_PARAMS` are the other two members of `STRUCTURAL_VARIADIC`.
`runclientscript*` is the fourth user of that machinery, not a new mechanism —
which is why landing it cost one `EXTRA_OPCODES` row, one set membership, and
one host case, and why `RUNCLIENTSCRIPT_SS` (11002) stays: a command that can be
written without the star is the cheaper thing to read, and content already
spells it.

The declared arity counts **only the fixed part** — the script id. The variadic
flag is what says the rest is on the stack, and it is not cosmetic:
`unimplemented_stub` (`ssvm.c:505-511`) **refuses to stub a variadic opcode** and
aborts instead. A missing implementation is therefore a message, not a corrupted
stack.

## 3. The path, end to end

```
runclientscript*(1119)($patch, $produce, $state, $flags, $noun)   content
  ->  declared args, vararg values, then the type string "iiiis"   ssc_compile.c
  ->  pop the type string FIRST, walk it BACKWARDS                 mock230_scripts.c
  ->  types forward, values REVERSED, script id last               mock230_encode.c
  ->  op 84, 2-byte size                                           the wire
  ->  types, then values reversed, str_mask bit i per string arg    osrs230_parse.c
  ->  compact strv, then bind: ints to int locals, strings to       app.c
      string locals, each in its own order                          task_cs2_run.c
```

Four asymmetries, each of which is invisible in an all-int payload of symmetric
values, and each of which now has an assertion:

- **The type string is forward, the values are backward.** Not a quirk of this
  port: the reference's writer pushes the CS2 operand stack, which unwinds
  last-argument-first.
- **The type character is the *static type of the expression*, not of the
  token.** `~answer` is `'i'` because the proc returns int; `^greeting` is `'s'`
  because the constant expands to a string literal.
- **The type string must be copied before the pop loop.** It is a `SSVM_StrPool`
  pointer and the loop pops other pool pointers over it.
- **Strings are indexed by argument on the wire and compacted on the CS2 side.**
  This is §4.2.

## 4. The two defects, both found by the first mixed payload

### 4.1 A too-long packet decoded as garbage instead of failing

`osrs230_parse.c` stopped reading type characters at
`PKT_RUNCLIENTSCRIPT_ARG_MAX` (20) and carried on. The remaining type characters
*and the `'\n'`* stayed in the stream, so argument 19's `p4` was read from those
stray bytes and every argument after it was wrong. `cur.over` never tripped —
the payload was long enough — so the packet was reported as **parsed**, and the
clientscript ran with 20 garbage arguments.

Unreachable from this repo's own server (the host aborts above
`MOCK230_RUNCLIENTSCRIPT_ARG_MAX` = 16, the compiler above
`SSC_MAX_VARARG_TYPES` = 16, both smaller), reachable from any other. It fails
the packet now. A dropped `RUNCLIENTSCRIPT` is a panel that does not appear; a
decoded one is a panel drawn from garbage, and the second is not diagnosable
from the client.

### 4.2 The string reached the clientscript empty

`strv[i]` is indexed by **argument** — `str_mask` bit `i` and `strv[i]` are the
same `i`, which is what the parser writes and what `revpacket.h` documents.
`RS_CS2_RunScript` wants the other convention, the one the CS2 hook path already
uses: a **compacted** list, string 0 first, because `task_cs2_run.c` walks the
arguments with its own running string counter and reads `str_args[str_i]`.

`App_RunClientScript` handed over the sparse array. So string N of the *packet*
bound to string local N of the *clientscript*, and a payload whose only string
is the last argument bound string local 0 to `strv[0]`, which is `""`.

This is exactly the shape every measured consumer has —
`shop_main_init(inv, int, int, int, string)`,
`farming_view_setpanel(int, obj, coord, int, string)`,
`deathkeep_init(9 ints, string)` — and it could not have been noticed before:
an all-int payload has no string to place, and an all-string one is already
compact, so sparse and compact are the same array for both shapes content sent.

It rendered as **"A gardener is protecting this ."** — a sentence with a hole in
it, which reads exactly like a content bug. The fix is
`pkt_runclientscript_compact_strings` (`revpacket.h`), inline beside the struct
whose convention it is, so it is testable without linking the app.

## 5. The permanent checks

Two targets, and both were proved by mutation rather than assumed.

| target | covers | mutation that reddens it |
|---|---|---|
| `make -C src test-ssc` | compiler -> bytecode -> reader -> VM -> host: the type string, every value in declaration order, and **both stack pointers back at zero** (a variadic that pops a wrong count does not fail at the call — it fails somewhere else) | `ssc_compile.c`: `types[type_count++] = arg_is_string ? 's' : 'i'` -> `'i'`. Result: `int stack underflow at [proc,push_mixed]` |
| `make -C src test-runclientscript` | encoder layout -> `osrs230_parse` -> the compaction: mixed, all-int, all-string, empty, exactly-at-cap, over-cap, unterminated | `osrs230_parse.c`: restore the `break` at the cap. Result: *"one more than the cap is REJECTED"* fails, got 1 want 0. And `revpacket.h`: `out[count++]` -> `out[i]`. Result: 5 assertions fail |

`test-ssc` also pins the compiler's two refusals: more than
`SSC_MAX_VARARG_TYPES` values, and a `*` on a command with no vararg form
(`mes*` must not silently become `mes`).

The **encoder** side is not duplicated in `test-runclientscript` — it writes the
same layout with the same `rsareabuf` primitives, and states so. The encoder's
own output is asserted byte by byte by `mock230 --selftest`, on the skill
guide's four-int payload.

## 6. Two corrections to this repo's own docs

Both were stated as measured fact and both were false. They are struck through
in place; repeated here because either could still shape a plan.

1. **"`mock230`'s `RUNCLIENTSCRIPT` sender takes ints only."**
   (`LOSTCITY_PORT_TRIAGE.md` §7.4, its symptom table, and
   `UI_ERA_PORTING_GUIDE.md` §4 item 2.) The *sender* never did —
   `mock230_send_run_clientscript_mixed` has taken a type string since it was
   written. What was fixed at one int and two strings was the *opcode*,
   `runclientscript_ss`.

2. **"It blocks `~p_choice*` (879 call sites)."** It does not and did not.
   `~p_choice_open` ships today on `runclientscript_ss`, because
   `chatbox_multi_init` takes exactly the one-int-two-string shape. What bounds
   those call sites is three caps, none of them this opcode: `script_58` parses
   at most five options, the `|`-joined list rides one 512-byte string
   (`PKT_RUNCLIENTSCRIPT_STR_LEN`), and `MOCK230_RESUME_SUB_MAX` is 15.

## 7. The consumer, and why it is that one

`interface_farming/scripts/farming_view.rs2` — interface 179, the farming patch
grid. `[clientscript,farming_view_setpanel]` (1119) takes
`(int, obj, coord, int, string)`: four ints and a string, in one call, and it is
server-driven only (the definition is the only hit for its name across all 9,725
decompiled clientscripts, and 179's own onload builds the tabs and no patch).

The panel is driven from a `[debugproc]` and that is deliberate, not a shortcut.
Two facts decide it:

- **The real entry point is a confirmed corpus gap.** Nothing in this cache opens
  179 (`farming_server_reqs.md` §5).
- **There is no crop-growth simulation in this server** — no growth clock, no
  disease roll, no compost decay — so there is no patch state to render.
  Inventing 107 patches' worth so a panel has something to draw is the wrong
  order of work, and the tree already says so in the Tool Leprechaun's own
  placement comment ("Move him the moment the patches land").

A debugproc renders exactly what the operator asks and stores nothing, so it
implies no world state; cheats are content in the reference and here
(`PORTING_GUIDE.md` §2.3). `~farming_view_setpanel` is the call the growth code
drives when it lands, unchanged.

**Verified in the headless client** (`SDL_VIDEODRIVER=dummy`, `TORIRS_MAX_FRAMES`
mandatory — no quit event arrives under the dummy driver):

```sh
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=900 \
TORIRS_NET_CHEAT="farmingview 8 guam_leaf 3 4" \
TORIRS_SIM_HOVER="75,140" TORIRS_EXIT_BMP=/tmp/fv.bmp \
./src/torirs --manifest manifest_osrs230_embed.ini --user testbl --pass test
```

`runclientscript: script=1119 argc=5 str_mask=0x10` on the wire, and the cell
draws: **"Falador (NW)"** (heading, from the cache's `enum_1233` keyed by the
patch id), **"Guam leaf"** (from `enum_1238` keyed by the obj), a green progress
bar (stage 3 of 4, unpacked from the coord), the gardener shield (flag bit 2),
and on hover **"Guam leaf (State: 3 / 4) / A gardener is protecting this herb
patch."** — the last four words being the string argument, which is what §4.2
was.

**Patch ids are not tab-ordered.** Patch 0 is `tree_1` and patch 8 is
`allotment_1`; the mapping is the cache's `enum_1235/1236/1237` and the server
resolves none of it. An id whose enum row is absent makes 1119 return at its
first guard, so a sweep is safe.

## 8. What is still capacity-limited, and what would need raising

Four caps, measured:

| limit | value | where |
|---|---|---|
| compiler vararg list | 16 | `ssc.h:70` `SSC_MAX_VARARG_TYPES` |
| mock host / encoder | 16 | `mock230.h:289` `MOCK230_RUNCLIENTSCRIPT_ARG_MAX` (aborts above; deliberately not a truncate) |
| **client wire parser** | **20** | `revpacket.h:517` `PKT_RUNCLIENTSCRIPT_ARG_MAX` |
| hard ceiling | 32 | `str_mask` is `uint32_t`; ARG_MAX cannot exceed it without widening |

Consumers, re-measured from each `.cs2`'s declaration line:

| consumer | script | ints | strings | fits |
|---|---|---:|---:|---|
| `shop_main_init` | 1074 | 4 | 1 | yes |
| `farming_view_setpanel` | 1119 | 4 | 1 | yes |
| `deathkeep_init` | 972 | 9 | 1 | yes |
| `interface_inv_init` | 149 | 6 | 5 | yes |
| `ge_history_addline` | 1645 | 6 | 0 | yes |
| `ge_pricechecker_prices` | 785 | 28 | 0 | **no** |
| `script1216` / `script1217` | 1216/1217 | 28 | 0 | **no** |

**All three over-cap consumers are int-only, and all three belong to GE or
trading**, which no current lane is building. So raising the caps is capacity
with no consumer, and it is *not free*: `sizeof(struct RevPacket)` is 10344
today, of which `strv[20][512]` is 10240, and `struct RevPacket` is
stack-allocated in the per-frame pump (`app.c`, `net.c`). A naive
`strv[32][512]` is 16 KB of stack per frame and must be checked against the
emscripten stack (`PLATFORM=web`) before landing.

Because every over-cap consumer is int-only, the cheap form is to **decouple**:
a small `PKT_RUNCLIENTSCRIPT_STR_MAX` (4 covers every string-bearing consumer
measured, whose maximum is 5 — `interface_inv_init`) with a per-argument string
index, which takes `RevPacket` *down* while raising the int arity. Prefer that;
it is better on both axes. Either way it should land with a test that pushes 28
ints through compiler -> VM -> encoder -> parser and asserts all 28 arrive,
so the capacity is proven rather than asserted.

## 9. What a mixed push still cannot reach

For the record, because the search cost more than the implementation: **no
feature currently buildable in this tree needs a mixed payload.** Every
mixed-signature, server-driven clientscript in this cache belongs to one of
three fenced categories, and this is the whole list at ≥3 ints + ≥1 string with
no CS2 caller:

- **barred by lane scope** — `shop_main_init` (1074), the GE and trading scripts;
- **needs the container registry** — `deathkeep_init` (972),
  `interface_inv_init` (149), `interface_inv_init_big` (150),
  `interface_invother_init` (158);
- **needs a simulation that does not exist** — `farming_view_setpanel` (1119).

The rest are client-installed op handlers (`friend_op`, `stats_setlevels`,
`chat_broadcast_op`, the `meslayer_*` family) that a server never calls, or
belong to game modes this world has no model of (leagues, deadman, raids).
That is why the consumer here is a debugproc on the third category rather than a
finished feature: the opcode is ahead of everything that would use it, and
pretending otherwise would mean half-building a fenced feature.
