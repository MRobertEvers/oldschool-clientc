# The skill guide, and the two seams it needed

> **What this is.** The rev-230 skill guide (`skill_guide_v2`, interface 860),
> opened from the stats sidebar tab. It is a `PORTING_GUIDE.md` §5 feature —
> 2004 RuneScape shipped no skill guide, so there is no LostCity reference for
> any of it — and almost all of it was already in the cache. What was missing
> was one packet the server could not send, one trigger content could not bind,
> and one CS2 opcode the client's VM had never implemented.
>
> Read `UI_ERA_PORTING_GUIDE.md` for the model this sits inside and
> `REV230_UI_BLANK_PANELS.md` for the triage ladder. This is the case file.

---

## 0. The one-paragraph version

Clicking "View Attack guide" on the stats tab is one click that crosses three
layers, and each layer was missing exactly one thing. The **wire** already
carried `IF_BUTTON2`; the **engine** collapsed all ten `IF_BUTTON<n>` opcodes
into one trigger, so content could not tell "View guide" (op 2) from "Toggle
XP" (op 1) on the same component — and there was no `last_verb` command to read
the index out of either. The **opcode surface** had `runclientscript_ss`, fixed
at one int and two strings, and clientscript 1902 takes four ints. And the
**client's CS2 VM** had no handler for `if_callonresize`, which is how a
rev-230 panel that draws none of itself starts its own layout — so the guide
mounted and then took the client down with it.

None of that is visible from the panel. All three look like "nothing happens".

---

## 1. What the client already does

Interface 860 has 23 components and carries exactly two `onload=` hooks, both
clientscript 714 (`thinbox`) — a cosmetic border shared with twenty other
interfaces. It draws nothing of itself. Everything on screen is built by
clientscript **1902** and the chain under it:

```
~script1902(skill, tab, x, y)          <- no caller anywhere in 9,433 scripts
  %varcint1172, %varcint1173 = skill, tab
  if_setonresize("script1903(x, y)", interface_860:0)
  ~script1911(-1, -1, 860:0, 860:1)
    if_setontimer("script1910(w, h, ...)", 860:1)
    if_callonresize(860:0)             <- starts the layout
      script1903 -> ~script1904(x, y)  <- the whole panel
```

`~script1904` builds the title, the close button, the window chrome, the tab
strip (one `cc_create` per `skill_guide_subsections` row, each with its own
`cc_setonop("script1906(tab)")`) and the feature list (`skill_features`,
3,447 rows). Tab clicks never reach the server: script1906 writes
`%varcint1173` and re-runs the layout locally.

So once the guide is open the player can move around inside it and the server
hears nothing. The server's whole job is to open it on the right skill.

**A clientscript with no caller, in front of an interface that cannot draw
itself, is the cache saying "the server runs this."** That is the same
signature `chatbox_multi_init` carries, and it is worth recognising by sight.

---

## 2. What the server owes, and how the cache says so

Two things, both stated in clientscript **393** — the onload every one of the
stats tab's 24 skill cells runs:

```
if_clearops($component0);
if_setop(2, "View <col=ff981f><$string0></col> guide", $component0);
if (~script1972 = 1) { if_setop(1, "Toggle <col=ff981f><$string0></col> XP", $component0); }
else                 { if_setop(1, "", $component0); ... }
```

An op with a label and **no matching `if_setonop`/`cc_setonop`** is how a
rev-230 component says "ask the server" — the same signature `friends:ignore`
carries. So:

1. **Op 2 is armed by the server or it is inert.** Nothing at rev 230 is
   clickable by default; an unarmed op still draws a perfectly good right-click
   row that sends nothing.
2. **Answering it is a mount plus a clientscript, in that order.**
   `if_opensub(toplevel_osrs_stretch:mainmodal, skill_guide_v2, 0)` then
   `runclientscript` 1902. Reversed, 1902 measures components that are not in
   the tree yet and the window lays itself out against nothing.

Op 1 is deliberately **not** armed: `~script1972` is
`%varbit6352 = 1 | clienttype = 7 | on_mobile`, i.e. the mobile layout. On
desktop 393 takes the other branch and sets op 1 to the empty string, so arming
it would arm a menu row the player can never see.

`%varcint1172` is **not** a stat id. It is a key of the cache's `enum_681`, the
same number each cell's own onload carries as its fourth argument
(`onload=i:393,i:-2147483645,i:20971553,i:<index>,i:<offset>`), and the same
number `skill_guide_subsections.skill` and `skill_features.skill` are keyed by.
Attack is 1, Ranged is 3, Defence is 5 — the 2004 sidebar's column-major order
fossilised into an enum. Using the sidebar's order instead compiles, runs, and
opens the Prayer guide for Magic.

---

## 3. The three seams

### 3.1 `IF_BUTTON1..IF_BUTTON10` — the op index had no path into content

`handle_if_button_op` received all ten `IF_BUTTON<n>` opcodes, computed the op
index, stored it in `player->last_verb` — and routed every one of them into the
single `SS_TRIGGER_IF_BUTTON`. `last_verb` is written by the engine and **read
by nothing**: there is no `last_verb` command in `ss_opcode.h` and none in the
reference either. So a script bound to `stats:attack` could not tell op 1 from
op 2, and the op index simply had no way to reach a script.

The reference names the fix itself, in `ClientGameProt.ts:71`:

```ts
static readonly INV_BUTTON1 = new ClientGameProt(181, 6);
    // NXT has "IF_BUTTON1" but for our interface system, this makes more sense
```

The numbered form of this packet family *is* called `IF_BUTTON<n>` at the
revision this client speaks; LostCity renamed it `INV_BUTTON<n>` because the
only multi-op components its 2004 interface system has are inventories. Those
five triggers already existed here (149..153) and already dispatched per op.

So: `SS_TRIGGER_IF_BUTTON1..IF_BUTTON10` at **168..177**, allocated by
`EXTRA_TRIGGERS` in `gen_opcode_meta.py` — strictly above the reference's
highest id (167), the same rule `EXTRA_OPCODES` follows at 11000, so a future
LostCity trigger can never land on one. `ToriRSServer_ScriptsRunIfButton` now
takes the op index and tries four lookups in order:

```
[if_button<n>, <uid>]   by key
[if_button<n>, <name>]  by name
[if_button,   <uid>]    by key
[if_button,   <name>]   by name
```

Both spellings, for every rung, because which one a script compiled under is
decided by arithmetic rather than by the author: `SSVM_LookupKey` puts the
subject at bit 10 of an i32, so a component uid `(interface << 16) | child`
only fits for interfaces below 32. `stats:attack` is 20,971,521 and compiles
name-addressed; `chatmenu:options` fits and compiles keyed.

The unnumbered rung stays as a *fallthrough*, not a replacement — every
`[if_button,…]` in the tree was written when one trigger answered every op, and
those still answer every op. That widens no engine fallback: both rungs are
content, and the C rung below (`TORIRSSERVER_FALLBACK_IF_BUTTON`) is the same single
one it always was.

The op-*less* click (`handle_if_button`, events bit 0) passes op 0, which skips
the numbered rung entirely, so its behaviour is byte-for-byte what it was.

### 3.2 `runclientscript*` — a RUNCLIENTSCRIPT that can carry ints

`SS_OP_RUNCLIENTSCRIPT_SS` (11002) is fixed at `(1 int, 2 strings)` because the
one caller it was written for — `chatbox_multi_init` — takes exactly that. The
wire never was: `RUNCLIENTSCRIPT` carries a per-argument type string, and
`ToriRSServer_SendRunClientscriptMixed` has taken one since it was written.

`SS_OP_RUNCLIENTSCRIPTVARARG` (11003) is the general form, in the reference's
own vararg convention — `queue*(queue, delay)(args…)` compiles to `QUEUEVARARG`
with the declared arguments first, the vararg values next, and a type string on
top. The compiler already built all of that (`ssc_compile.c`'s vararg block
derives the type string from each expression's static type); what was missing
was one `EXTRA_OPCODES` row, membership in `STRUCTURAL_VARIADIC`, and a host
case. Content spells it:

```
runclientscript*(^clientscript_skill_guide_init)($skill, ^skill_guide_tab_default, 0, 0);
```

The declared arity counts only the fixed part (the script id), exactly as
`QUEUEVARARG`'s `{2,0,0,0,1,1,…}` counts only `queue, delay`. The variadic flag
is what makes the VM *refuse to stub it* rather than pop a wrong number of
values — `unimplemented_stub` aborts on a variadic, which is the behaviour that
turns a missing implementation into a message instead of a corrupted stack.

The host case pops the type string first and walks it **backwards**, because
the last value pushed is the first one off — which is also the order the packet
writes its arguments. The type string is copied before the loop: it is a
`str_pool` pointer and the loop pops other pool pointers on top of it. A type
string longer than `TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX` **aborts** rather than
truncating: a clientscript run with three of its four arguments does not fail,
it draws the wrong panel.

### 3.3 `if_callonresize` — the client could not start its own layout

With the two server seams in, the packets went out, 860 mounted, and the client
aborted:

```
CS2VM2: unimplemented opcode 2927 (IF_CALLONRESIZE) — no stack signature
  in script 1911 ... pc=24 op=2927 IF_CALLONRESIZE
Assertion failed: (0 && "unimplemented CS2 opcode reached StackMetaStub")
```

`if_setonresize` was implemented and the hook was stored; nothing ever *called*
one outside the interface-open task. `if_callonresize(component)` is how
seventeen scripts in `cache.osrs239` start a panel that lays itself out.

Its arity is read, not inferred: script 1911 ends
`PUSH_INT_LOCAL 2; IF_CALLONRESIZE; RETURN`, and every call site has that shape.
`(1 int in, nothing out)`.

It **queues** rather than running the listener in place, because the request is
handled from inside a running CS2 script and `RS_CS2Host` has no task runner to
nest a second one on — the same arrangement `close_modal_requested` and
`social_send` use. The App's tick drains it beside the `onTimer` loop, and the
drain loops because a listener may queue another (a tab click does). In this
cache the call is the last statement of every site that makes it, so deferring
reorders nothing observable; a site that needed the listener to have *finished*
before the next statement would need a real nested run, and would be a finding
rather than a tweak.

`cc_callonresize` (1927) is deliberately still unimplemented. Its row in
`cs2_command.gen.h` claims one argument, which is not the shape any other `cc_*`
component op has, and no script in this cache calls it — so there is nothing to
verify an arity against, and a guess is what `StackMetaStub` exists to catch.

### 3.4 `if_getcomponentparam` (2703) — an opcode in neither table

Next abort, one layer down, in `script 8304` (reached from `~script8302`, which
`script1904` calls to place the title icon):

```
push 2356; push $component; push -1; 2703; return
```

Opcode 2703 is absent from the vendored opcode table **and** from
`3rd/rscache`'s `cs2_command.gen.h` — which is why 20 scripts in this cache
fail to decompile at it, and why the disassembler prints it as `?`.

It is the IF form of `CC_GETCOMPONENTPARAM` (1703): the runtime param table an
IF3 component owns, read for a component named by argument instead of the
active one. Three ints in, one out, and the arity is *pinned* by the bytecode
rather than inferred — `cs2 infer-arity` calls it under-determined, but script
9181 settles it on its own:

```
if_getwidth        -> depth 1
push 2524, $com, -1 -> depth 4
2703                -> depth ?
push $com           -> depth ?+1
if_setscrollsize    -> pops 3, must leave 0
```

Only `(3 in, 1 out)` balances. The **third** argument is the literal `-1` at
all 16 call sites, so "fallback for a miss" and "sub-id, -1 meaning the
component itself" cannot be told apart from this cache. It is read as the
fallback, because every read site guards the result against -1 and an OldSchool
IF3 component's param table starts empty — every one of the 24,382 IF3
components in `cache.osrs239` consumes its bytes exactly, with no param section.

---

## 4. Verified in the client

Headless, embedded server, one real right-click on the stats tab and one real
click on the menu row:

```
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=400 \
TORIRS_SIM_CLICK_AT="150,536,186;230,535,220,1;242,535,240" \
TORIRS_EXIT_BMP=/tmp/guide.bmp TORIRSSERVER_VERBOSE=1 \
  ./src/torirs --manifest manifests/manifest_osrs230_embed.ini --user testc --pass test
```

`TORIRS_MAX_FRAMES` is not optional under the dummy video driver: no quit event
ever arrives, so without it the loop never reaches teardown and the recipe hangs
rather than writing the BMP (the farming and slayer recipes already carry it).
It has to clear the last frame in `TORIRS_SIM_CLICK_AT` by enough for the reply
to arrive and the panel to lay itself out — 400 against a last click at 242.

```
torirsserver: <- IF_BUTTON2 320:1 sub=-1
torirsserver: -> IF_OPENSUB         op=6   payload=7
torirsserver: -> RUNCLIENTSCRIPT    op=84  payload=25
```

25 bytes is the shape: 4 type characters + newline + 4 ints + the script id.

The BMP shows the guide open, titled **"Attack – Weapons"**, with the tab strip
(Overview / Weapons / Armour) down the left and the feature rows built
(`1 Bronze weapons`, `5 Steel weapons`, `10 Black weapons`, …) with their level
requirements. ~~and item icons~~ — **that half was wrong when it was written**;
the icon column was blank in that BMP and stayed blank for a month. See §8.
Clicking `stats:magic` instead (535,370 → 535,390)
gives **"Magic – Standard Spellbook"** with nine tabs and the spell list. Two
different skills, two correct panels — which is the check the selftest cannot
make (§6).

---

## 5. Overview tab (remeasured 2026-08-03)

**Abort is fixed.** Opening Overview (clientscript **9176** + `9150..9199`) no
longer takes the client down. LostCity has no reference (PORTING_GUIDE §5).
Arities were established from call-site balance / `cs2 decompile --override`
scoring against `cache.osrs239`, with names from xrsps where present — not
invented. Tables: `local_commands.py` → `cs2_command.gen.h`, and
`gen_opcode_stack.py` → `cs2vm2_opcode_stack.gen.h`.

| Op | Arity | Source | Runtime |
|---|---|---|---|
| 211 | `(3i)->(1i)` | script 9181 | **real** — `IF_CHILDREN_COLLECT` |
| 212 | `(1i)->(1i)` | 9179/9186 after `.cc_find` | real — children-find + count |
| 213 | `()->(1i)` | 9179/9186 boolean find-next | **real** — `CC_CHILDREN_FINDNEXT` (set target, push 1/0; not FINDNEXTID) |
| 215 | `()->(1s)` | 9181 → array handle for 8003 | **real** — `CHILDREN_ARRAY` |
| 4036 | `(1s)->(1i)` | xrsps `STRING_TO_INT` | real |
| 8003 | `(1s)->(1i)` | xrsps `ARRAY_LENGTH` | real |
| 8012 | `(1s)->()` | 9194 after sort; meaning unknown | StackMetaStub (known) |
| 8018 | `(2s)->(1s)` | 9183 split → handle | real |
| 8019 | `(2s)->(1s)` | 9153→gosub 9182; xrsps `ARRAY_JOIN` | real |
| 8022 | `(3i)->(1s)` | xrsps `ARRAY_NEW` | real |
| 8023 | `(1s,1i)->()` | resize before N `pop_array_int` | real |
| 8024 | `(1s,2i)->()` | append (int-typed form) | real |

`cs2 infer-arity` is not the route, and that is measured: a full run over
`cache.osrs239` examines 28 opcodes and solves **0**. It calls `211` and `212`
under-determined (2 surviving candidates each) and says of `8018`, `8023` and
`8024` "no arity works; the signature is not the only problem" — they only ever
appear beside another unknown, so no witness script interprets end to end. Every
row above therefore comes from call-site balance or `--override` scoring.

**Collateral: opcode 2704 was misidentified.** Vendored / older deob called it
`IF_HASCHILD_MODAL` `(2i)->(1i)`. At this revision it is **`IF_SETPARAM`**
(xrsps WidgetOps) — the IF form of `CC_SETCOMPONENTPARAM` (1704). Script 9176
call sites are five ints and nothing out; `--override 2704:5,0,0,0` recovers
9176 end-to-end (`if_setparam(2525, 3, $com, -1, 0); …`). Wrong arity left the
Overview body blank / desynced after the twelve above stopped aborting. Handler
reuses `CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM`. 2705 remains
`IF_HASCHILD_OVERLAY`.

### Verified (headless embed Soft3D)

```
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=500 \
TORIRS_SIM_CLICK_AT="150,536,186;230,535,220,1;242,535,240;300,70,52" \
TORIRS_EXIT_BMP=/tmp/guide_ov_click.bmp TORIRSSERVER_VERBOSE=1 \
  ./src/torirs --manifest manifests/manifest_osrs230_embed.ini --user testc --pass test
```

Exit **0**. No `unimplemented` / `Assertion failed` / `cs2-stub`. BMP title
reads **Attack - Overview**; Overview tab selected; content draws (training
blurb + Quest XP rows). Weapons→Overview content region ~31% pixel diff.

### Landed (2026-08-03) — overlap + `<u=…>`

- **211 / 215** implemented as `IF_CHILDREN_COLLECT` / `CHILDREN_ARRAY` in
  `cs2vm2.c`: script 9181 walks `overview_tabs` children and `if_sethide`s the
  non-selected content panel, so Overview and Quest XP no longer stack.
- **`<u=RRGGBB>` / `</u>`** (colored underline only) in `toridraw_font.c`;
  measure skips the same tokens. Shared with Slayer confirm titles.
- `^skill_guide_tab_default` set back to **0** (Overview).

Still stubbed, not required for the hide fix: **8012** (post-sort in 9194).

### Landed (2026-08-07) — initial Overview panel stays selected

The rev-239 RuneLite deob separates the guide's main-tab varc (`1173`) from
the Overview panel selector (raw varc `1345`). Script 9176 tests the latter
against `-1`: when it is unset, it calls script 9181 with
`skill_guide_v2:overview_content`; otherwise it passes the stored component
UID. The C `VarCManager` previously returned `0` for every unset integer varc,
so the first frame drew Overview, then script 9181 treated `0` as a component
UID and hid both Overview and Quest XP.

`VarCManager` now matches RuneLite's rev-239 `Varcs` map: an absent integer
reads as `-1`, while an explicitly stored zero remains zero. New/grown slots
and `ResetAll` use the same unset sentinel. Headless opening Attack now takes
script 9176's fallback and leaves the Overview content panel visible.

### Landed (2026-08-03) — Overview / Quest XP sub-tabs clickable

Two CS2/host bugs left the sub-tabs looking live but doing nothing useful:

1. **Opcode 211 pop order.** Script 9181 pushes `211(1, $tabs, -1)` (start on
   top). The handler popped unused first, so `start` became `1` and skipped
   tab sub-id `0` (Overview). Switching to Quest XP never hid Overview's
   panel; switching back had nothing to re-arm.
2. **`UITree_CollectDynamicChildIndices` exclusive bound.** `212(1)` in script
   9179 walks tab chrome children whose sub-ids start at `1`. Filtering with
   `i > start` dropped the first child and left the fifth `213` (the
   `cc_setonop(9180)` arm) unreachable on the Overview tab. Inclusive
   `i >= start` matches v1 and the call sites; Quest XP ↔ Overview both arm.

Verified headless (embed Soft3D): open Attack Overview, click Quest XP at the
hit target (`~353,67`), then Overview (`~233,67`) — content swaps both ways,
each click resolves `View …` / script 9180. Title stays "Attack - Overview"
until a left-rail rebuild (cache title script); the content chrome still draws
through the tab labels (layout/z-order, separate from click arming).

### Landed (2026-08-03) — Quest XP View-journal

Quest XP rows arm `View-journal` in clientscript 9195 with
`cc_setonop("script9189(quest:id)")`. Script 9189 calls CS2 opcode **2929**
(`IF_TRIGGEROPLOCAL`) on `skill_guide_v2:quest_journal_button_trigger`
(860:17) with the quest id as a typed `"i"` arg.

Real rev-239 wire for 2929 is `IF_SCRIPT_TRIGGER` (var-short; crc + component +
typed args). This client still speaks rev-230, so 2929 adapts to
**`IF_BUTTON1(component, sub=questId)`** — `last_slot` carries the quest id,
same shape as questlist op 2.

Content:

- `~skill_guide_login` arms `quest_journal_button_trigger` for op 1 over
  quest ids `1..N`.
- `[proc,quest_journal_open_by_id]` is shared by
  `[if_button2,questlist:list]` and
  `[if_button1,skill_guide_v2:quest_journal_button_trigger]`.

Verified: `make -C src test-cs2-triggerop` (2929 stack → host request);
ToriRSServer skill-guide selftest arms the trigger and opens Cook's Assistant
journal via `IF_BUTTON1` on the trigger with `sub=1`.

---

## 6. The permanent check

`ToriRSServer --selftest`, section "skill guide" (`torirs_server_world.c`). It sends a real
`IF_BUTTON2` for each of the 24 cells and asserts, per cell:

- an `IF_OPENSUB` and a `RUNCLIENTSCRIPT` came out, and **the mount is first**;
- the mount is `skill_guide_v2` into `toplevel_osrs_stretch:mainmodal` as a
  modal (type 0);
- the `RUNCLIENTSCRIPT` type string is exactly `"iiii"` — the assertion that
  `runclientscript_ss` cannot satisfy;
- the skill index is one no other cell used;
- the tab and the clientscript id are the same for all 24, and 1902's last two
  arguments are 0;
- all 24 cells answered.

Plus one negative: `IF_BUTTON1` on `stats:attack` must produce nothing, because
nothing binds op 1 — running op 2's script there is exactly the failure one
un-numbered trigger produced.

Proven to fail, by mutation:

| mutation | what the selftest said |
|---|---|
| skip the numbered rung in `ToriRSServer_ScriptsRunIfButton` | `op 2 on stats:attack should mount the guide` (×24, IF_OPENSUB -1) |
| swap the two statements in `~skill_guide_open` | `stats:attack: the mount must precede the clientscript that lays it out (IF_OPENSUB 1, RUNCLIENTSCRIPT 0)` |
| point `^skill_guide_magic` at Prayer's index | `stats:magic reuses skill index 7 — two cells would open the same guide` |

**What it deliberately does not assert** is that index N is the skill whose name
the cell carries. That mapping's ground truth is the fourth argument of each
cell's onload in `interfaces/stats.if`, and the same numbers in the cache's
`enum_681`. Nothing server-side parses an `.if`. Cache enums *are* loaded now
(`configs/all.enum` rank-0 in `torirs_server_content.c` — see
`docs/account_summary_server_reqs.md` §1); the skill-guide selftest still leaves
the index↔name check to §4's screenshots rather than asserting it here.

---

## 7. Three things found along the way

- **`player->last_verb` is written and read by nothing.** No `last_verb`
  command exists here or in the reference. The comment beside it claimed it was
  "where a RuneScript trigger reads them"; it is corrected.
- **`configs/all.enum` is loaded by the server (rank-0).** Authored
  `server/scripts/**/*.enum` still override on name. The claim that
  `torirs_server_content.c` walked `.enum` under `server/scripts` only is retired —
  see `docs/account_summary_server_reqs.md` §1. `enum(int, stat, enum_681, $i)`
  is expressible; the skill guide still does not need it server-side.
- **The server's copy of every cache dbtable is an empty shell.** The `.dbtable`
  parser reads `column=name,type…` and `data=column,value…` (the authored
  grammar); the machine-exported `configs/all.dbtable` / `all.dbrow` use
  `columndef=N:name,type` / `values=N:0:v`. So `skill_guide_subsections` loads
  its *name and id* and zero columns, and its 196 rows carry no data. The
  skill guide does not care — the client reads those tables out of the cache —
  but `docs/skill_guide_server_reqs.md`'s "already loading generically —
  **landed**" was wrong about it, and any feature that wants a cache dbtable
  server-side will hit it.

---

## The click was dead, and the cause was not in this feature

Reported 2026-08-02 as "clicking on a skill in the interface is still not
working", against a build where everything this doc describes was present and
correct: `~skill_guide_login` ran, all 24 `if_setevents(stats:<skill>, 0, 0,
^if_event_op2)` left the server, and clientscript 393 set the op text
(`if_setop(2, "View <col=ff981f><skill></col> guide", $component0)`).

**Nothing was wrong server-side. The client could not hit the cell at all.**

`stats:attack` is a `type=0` **layer** carrying `clickmask=6`, `op1=*` and
`op2=*` — the whole 62x30 cell is the button, and the graphics and numbers
script 393 `cc_create`s on top of it are ops-less decorative children.
`UITree_ComponentIsPassThrough` (`src/ui/uitree_input.c`) had:

```c
case UIELEM_RS_LAYER:
    return true;          /* a layer is never a click target */
```

So the children correctly refused the hit (no click mask, no ops) and the
parent that owns the ops refused it too. The click resolved to **no component
at all** — `out.clicked_com_id` stayed -1, the minimenu was never built, and
`TORIRS_CLICK_DEBUG` printed nothing whatsoever. A widget that produces *no*
debug output is a different failure from one that produces a menu and sends
nothing, and it is what made this look like a server problem.

### The fix, and why it is narrow

A layer is now a click target when the cache armed it — a non-zero
`click_mask`, a menu option, or any op text — and pass-through otherwise.

It deliberately does **not** reuse `rs_node_is_decorative_passthrough`, which
the graphic/text/rect arms use, because that predicate also promotes a node for
a non-zero `button_type` or `client_code`. On a *layer* those catch structural
slots: measured, reusing it verbatim made `toplevel:sidemodal` (161:74,
`clientcode=1354`, 190x261 across the whole sidebar) start picking, so empty
sidebar space answered with a Cancel-only menu where it used to fall through.
Nothing broke — real components mounted inside it still win, because the hit
test takes the topmost — but it was a behaviour change with no reason behind
it.

Verified by a 12-point click sweep across world, minimap, chat, sidebar, orbs
and both tab rows, diffed against a control binary with the arm reverted:

| | picks |
|---|---|
| new picks vs control | **0** |
| lost picks vs control | **0** |

and the discriminating test, same sweep harness:

| | `mount iface=860` |
|---|---|
| control (arm reverted) | **0** |
| new | **1** |

attack / strength / defence each open the guide and reach the server as
`IF_BUTTON2 320:1`, `320:2`, `320:3`. A left click is enough: op 2 is the
default row, because op 1's text is empty unless the XP-tracker toggle applies.

### Blast radius

This is general, not skill-guide-specific — **any** cache layer with a click
mask or ops was unclickable, and the stats tab is simply the first place
content armed one. Worth re-testing anything that looked inert and was assumed
to be a missing packet.

---

## 8. The icons were missing, and the cause was not in this feature either

Reported 2026-08-02 as *"object sprites are still not rendering in the skill
guide"*, against a build where the panel opened, laid itself out, filled in every
row's level and text — and drew nothing in the 36×32 column between them.

**Nothing was wrong with the obj-icon path. It was never entered.**

`[proc,script9347]` builds one row as four `cc_create`s (rectangle, level text,
icon, body text) and picks the icon's source from the `skill_features` dbtable:

```
def_int $int12 = db_getfield($int0, 872448, 0);        // column 0: icon,obj
$int13, $int14, $int15, $int16, $int17 =
        db_getfield($int0, 872464, 0);                 // column 1: sprite,graphic,int,int,int,int
...
if ($int13 ! -1) { cc_setgraphic($int13); ... }        // a sprite row
else             { cc_setobject($int12, -1); ... }     // an obj row
```

`TORIRS_OBJICON_DEBUG=1` on the client prints one line per `CC_SETOBJECT` and one
per `CC_SETGRAPHIC`. Against the guide it printed **zero** `CC_SETOBJECT` for
interface 860 and, at a stride of four, one `CC_SETGRAPHIC ... gfx=0` per row:

```
GFXDBG: com=0x035ca05d gfx=0
GFXDBG: com=0x035ca061 gfx=0
GFXDBG: com=0x035ca065 gfx=0        ← +4 per row, forever
```

`$int13` was 0, so the `!= -1` guard chose the sprite branch, with sprite 0.

### Why `$int13` was 0

**No row in `skill_features` sets column 1 at all**, and a dbrow lists only the
columns it sets. The client answered the read from the row and stopped there: it
never loaded the **dbtable** (config group 39), which is the only record that
states a column's field types and defaults. So the read fell to a
"nothing resolved" path that pushed a single integer `0` — where the script pops
**five** values and expects the first to be `-1`.

Two defects in one, and the arity one is the nastier: `$int13` got a value from
four slots deeper in the stack rather than a wrong value of its own.

The fix is the resolution chain the reference implies and this client never had —
row value, then table default, then per-type default (`-1`, or `""`) — with the
DBTABLE loaded on demand like any other config. It is written up in
[`DBTABLES.md` §9](DBTABLES.md#9-what-a-read-resolves-to--row-then-table-then-type),
including why `-1` is measured rather than chosen and the single-`awaited`-slot
trap that comes with an opcode that can now yield twice.

### Verified in the client, by pixels

Same headless recipe as §4, before and after, both BMPs 765×503:

| region | changed pixels |
|---|---|
| the guide's icon column (x 180–222, y 58–318) | **1785 / 10920 = 16.35%** |
| the guide's text column (x 222–460) | 0 |
| chatbox | 0 |
| minimap / orbs | 16 |
| whole screen | 1801 |

The run-to-run noise floor, measured by diffing two runs of the *same* binary, is
those same 16 minimap pixels and **zero** everywhere else — so 1785 of the 1801
changed pixels are the icons and the remaining 16 are the minimap animating.

The control is the **Magic** guide, whose rows are the sprite branch (spell
icons, column 1 set): before vs after is **0 changed pixels anywhere**. The
branch that already worked is untouched; the branch that never ran now runs.

### Blast radius

This is general, not skill-guide-specific. Every one of the 422 clientscripts
that read the database was getting `0` and an arity of 1 for any column its row
omits — 128 of the cache's 246 tables are read by at least one of them. Worth
re-testing anything that reads a dbtable and looked half-populated.

---

## 9. Outline/shadow on obj rows (remeasured 2026-08-03)

After §8, `CC_SETOBJECT` applies with real `item_id`/`item_scene_id`, layout is
36×32 at the icon column, emit includes those sprites, and Soft3D / GL3 both
blit sword pixels into the EXIT / `TORIRS_GL3_READBACK` BMPs. The earlier
"still blank" report does not survive that measurement: zero-size, clip,
wrong layer, and yield/active-component were ruled out again with
`TORIRS_DUMP_BOUNDS=860` + `TORIRS_DUMP_EMIT_EXIT=860` (no `OBJICON_DEBUG`
flood).

What *was* still wrong: `uitree_emit.c`'s item-overlay arm forced
`outline = 0` and `graphic_shadow = 0`. Script 9347, after both the sprite and
obj branches, does `cc_setgraphicshadow(0x333333)` then `cc_setoutline(1)`.
Inventory cells leave those fields at 0, so they were fine; skill-guide obj
rows discarded the same post-process the spell-icon branch keeps.

**Fix (first pass).** Pass `rs_graphic.outline` / `graphic_shadow` through on
the item overlay. Also raise `TRSPK_GL3_SPRITE_CAP` 256→2048 (with a one-shot
stderr when the table is full) so a large unique-icon burst cannot silently
drop late scene sprites on the GL path.

**Fix (double-shadow, same day).** `item_scene_id` icons are baked by
`UITreeSceneBridge_EnsureObjIcon` with the SHADOW post-process already in the
pixels (reference `outlineRgb == 0`). Forwarding `graphic_shadow` on top ran
`ToriDraw_SpriteNewGraphicShadow` again at draw time → a second edge at +2px.
Reference `ObjType.getSprite` never stacks: `outlineRgb` selects plain /
shadow / white, never two of them.

Emit now **selects the baked flavour**:

- `outline == 0 && graphic_shadow == 0` (inventory/bank): keep the SHADOW-baked
  `item_scene_id` and force both emit fields to 0.
- either non-zero (skill-guide rows): resolve
  `UITreeSceneBridge_EnsureObjIconPlain` (`BRIDGE_ICON_OUTLINE_NONE`, host
  `UITREE_HOST_GET_OBJ_ICON_PLAIN`) and forward the component fields so Soft3D
  / GL3 apply the post-process once.

### Pixel proof (outline pass-through)

Same §4 headless recipe (embed Soft3D); before = emit still zeroing
outline/shadow, after = pass-through. Icon cells are 36×32 at x=181
(`TORIRS_DUMP_BOUNDS=860`, 82 rows / 82 emit sprites).

| region | changed pixels |
|---|---|
| icon column (x 180–222, y 58–318) | **1138 / 10920 = 10.42%** |
| text column (x 222–460) | 0 |
| whole screen | 1173 |

Icon-column pure black went **0 → 809** (the `cc_setoutline(1)` edge).
Same-binary noise floor on that icon column is **0%**.

### Re-verify (2026-08-03, same recipe)

Candidates re-checked against a live Attack Weapons dump: 82× `36×32` bounds at
`x=181`, 82 emit sprites (`scene=569…`), clip `150,55 306×261`, and the icon
column BMP shows tiered sword pixels with outline+shadow. Forcing the item
overlay back to `outline=0`/`graphic_shadow=0` and diffing again reproduces
**1138 / 10920 = 10.42%** and black **0 → 809**; metal/sword pixels remain in
the before BMP (icons were never blank after §8 — only the post-process was
missing). The flavour-select path keeps that outline edge without stacking a
second shadow on inventory icons.
