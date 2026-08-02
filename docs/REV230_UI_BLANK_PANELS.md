# Nine broken panels, and what each of them actually was

> **What this is.** A rev-230 gameframe was up and drawing, and most of it was
> wrong: the minimap orbs were black discs, five of the seven sidebar tabs were
> empty or nearly so, the world map orb did nothing, and the only interface in
> the game that could be closed was the bank. This is the diagnosis of each,
> written down because the *symptoms were identical and the causes were not* —
> and because all but one turned out to be client bugs in a subsystem whose
> failures all look like a missing packet.
>
> Read `docs/UI_ERA_PORTING_GUIDE.md` for the model this sits inside (what moved
> between eras, and which reference server answers which question). This is the
> case file.

---

## 0. The one-paragraph version

A 2004 interface is data the client draws. A rev-230 interface is **a program the
client runs**. That changes what a blank panel means: it is no longer "the server
forgot to paint it", it is "the program did not finish". And a CS2 program that
does not finish almost never says so at the point of failure — it says so four
opcodes later, at something innocent, because the real damage was a *shifted
stack*. Three of the nine bugs below are the same bug wearing different clothes:
**the client silently dropped part of a script's data and let the script carry
on.** Two were genuine server bugs, one was a rendering rule, one was a missing
draw, one was a measurement the renderer disagreed with, and one is still open.

---

## 1. The triage rule that fell out of this

The porting guide's §2.4 rule — *before adding a packet, find the clientscript
and read what it consumes* — is still the first move. But it assumes the client
runs the script correctly, and here it mostly did not. So there is a step before
it:

> **A panel whose interface IS mounted and which still draws nothing is a client
> bug until proven otherwise.**
>
> 0. **Confirm it draws nothing at more than one frame.** A headless run samples
>    a single exit frame, and a panel that is *starved* rather than dead reads
>    identically at that frame. The XP-drop panel (interface 122) was diagnosed
>    as blank for two lanes and was not: its dispatch was being held for hundreds
>    of ticks and then delivered merged, so it was empty at frames 830 and 845
>    and showed the summed value `3,600` at 860. Vary `TORIRS_MAX_FRAMES` before
>    entering step 1 — one extra run, and it changes which subsystem you go
>    looking in. See `mock230_player_systems.md` §5.4.
> 1. `TORIRS_DUMP_TREE_EXIT=1` — is the interface in the tree at all, under the
>    slot you expect? If not, it is a packet.
> 2. `TORIRS_DUMP_BOUNDS=<group>` — is the geometry sane? A 190x261 sidebar
>    panel resolving to **500x1 at x=724** is not a missing packet. The server
>    did its job and the client mis-ran the onload.
> 3. `TORIRS_DUMP_SETSIZE=<group>` — *which* call did it, and what did it ask
>    for. BOUNDS shows where the layout landed; this shows who put it there.
> 4. Only then go looking for the packet.

And its corollary, which cost the most time here:

> **When a CS2 abort names an opcode that looks innocent, suspect the stack, not
> the opcode.** `script 2621 failed at opcode 40` reads like a bad gosub target.
> It was a blown call stack. `script 9290 failed at opcode 0` reads like nothing
> at all. It was an int stack that had filled up because an earlier op never
> consumed its arguments.

---

## 2. The nine

| # | symptom | kind | cause |
|---|---|---|---|
| 1 | minimap orbs are black discs | render rule | a zero-height clipping layer was treated as *no clip* instead of *clip everything* |
| 2 | world map orb does nothing | server | the orb was armed on the wiki button |
| 3 | nothing but the bank can be closed | server | `CLOSE_MODAL` only knew about the bank |
| 4 | no stack numbers on items | missing draw | only the TYPE_INV grid drew counts, and rev 230 has no TYPE_INV inventory |
| 5 | quest tab blank | dropped data | runtime hook arguments capped at 32; the journal's carry 44 |
| 6 | combat tab has no attack styles | dropped data | `db_find*` takes three stack arguments, not two |
| 7 | magic tab shows only "Filters" | limit | the spellbook's sort recurses to depth 70; the frame cap was 50 |
| 8 | music tab dies | dropped data (+ open) | string arrays were popping off the int stack; now blocked on an unimplemented opcode |
| 9 | summary panel text is off to the left | measurement | `parawidth` counted the bytes of `<col=…>` markup that the renderer never draws |

---

### 1. The orbs were black discs

**Symptom.** Health, prayer, run and spec all drew as a flat black circle with
the correct icon on top.

**What an orb is.** Four components stacked (`interfaces/orbs.if`):

```
health_backing         graphic 1071   the plaque
health_indicator       graphic 1060   the FULL orb (red)
orb_health_empty       layer,  26x?   <- clips
  health_empty_contents graphic 1059  the EMPTY orb (black)
orb_health_heart_icon  graphic 1067   the heart
```

The fill level is not a sprite choice. The empty orb is drawn *over* the full
one, and the clientscript expresses "how empty" by setting the **height of the
clipping layer above it**. At full health that height is `0`.

**Root cause.** `UITree_LayerChildClip` bailed on a degenerate box:

```c
if( !UITree_ComponentClipsChildren(component) || box_w <= 0 || box_h <= 0 )
    return false;                       /* "not a clipping component" */
```

`false` means *this node does not clip*, so the caller kept its inherited clip
and `health_empty_contents` drew at its full 26x26 over the fill. The `BOUNDS`
dump says it plainly — `com=0x00a0000e (160|14) 26x0` with a 26x26 child.

It could not be answered with a rect either: in this API an empty clip rect means
"unbounded", which is the exact opposite of what a zero-sized clip means.

**Fix.** A separate predicate, `UITree_LayerCullsChildren`, asked *before*
`UITree_LayerChildClip` by all four walkers — emit, hit-test, hover and drop — so
drawn pixels and hitboxes cannot disagree.

`src/ui/uitree_scroll.{c,h}`, `uitree_emit.c`, `uitree_input.c` (two sites),
`uitree_hover.c`, `uitree.c`.

---

### 2. The world map orb was the wiki button

**Symptom.** Clicking the globe on the minimap did nothing. No packet left.

**Root cause.** `mock230_worldmap.c` armed **160:53**, and had a paragraph of
reasoning for it: OpenRune's rev-235 gameval table names `160:55`
`orbs:worldmap` and `160:53` `wiki_icon`, so components must have been inserted
between the revisions and the rev-230 orb must be the lower id.

The cache being read says the opposite, twice:

```
interfaces/orbs.compack:   53=wiki_icon_graphic
                           55=worldmap
```

```
[orb_worldmap] onload = i:1492, i:-2147483645, i:10485814, i:10485815, ...
                                     ^self          ^160:54     ^160:55

script 1492 -> ~script1700(...)
script 1700:  if_setop(2, "Floating <col=ff9040>World Map</col>",   $component2)
              if_setop(3, "Fullscreen <col=ff9040>World Map</col>", $component2)
                                       ^ third argument = 10485815 = 160:55
```

So the server armed the wiki button; the orb kept its cache-declared ops
(`clickmask=30`, op1..op4), the client hovered and highlighted it, and the click
went nowhere. Exactly the §2.2 failure mode the porting guide describes, arrived
at from the other direction.

**Fix.** `MOCK230_ORB_WORLDMAP_CHILD` 53 → 55. The literal stays — a symbol from
a different revision is what caused this — but it is now the one the cache's own
script names, with the evidence beside it.

**The general rule.** A gameval table from a neighbouring revision is a
*hypothesis*. The compack and the onload are the revision you are running.

---

### 3. Only the bank could be closed

**Symptom.** The X on the equipment screen, and Escape, did nothing. The
interface stayed up for the rest of the session.

**Root cause.** The client half was fine: `if_close` (CS2 3103) sets
`close_modal_requested`, the tick drains it and sends `CLOSE_MODAL`. The server
half was:

```c
if( !srv->player->bank.open )
    return;
```

`CLOSE_MODAL` is a *request* — the server is what unmounts — so an interface the
server had no record of opening had nowhere to go. The bank was the only one it
tracked, and so the only one that could be closed. Anything a content script
opened with `if_openmain` was permanent.

**Fix.** Two fields on the player recording what sits in each of the gameframe's
modal slots, written by `mock230_note_modal_mount` — called from the
**`IF_OPENSUB` / `IF_CLOSESUB` encoders**, not from each opener, so a new opener
cannot forget to register. `CLOSE_MODAL` then:

1. offers the open interface's `[if_close]` script first (the order the bank
   already had, now generalised);
2. dispatches to the bank's or the equipment screen's own closer if one of those
   is up — they own state beyond the mount;
3. otherwise drops the mount, which for everything else is the whole of closing
   it.

`mock230.h`, `mock230_encode.c`, `mock230_world.c`.

---

### 4. Items had no stack counts

**Symptom.** Coins and arrows drew their icon with no number on them, anywhere —
backpack, bank, shop.

**Root cause.** Two emit paths drew counts: `emit_rs_inv_slots` (the TYPE_INV
grid) and `emit_rs_inv_text_slots` (the IF1 inventory-text component).

**Rev 230 has neither.** The gameframe's CS2 `cc_create`s one widget per slot and
hangs the obj on it with `SETOBJECT`; the item lives in `item_id` /
`item_scene_id` on an ordinary `RS_GRAPHIC`. That path emitted a sprite and
stopped. There was no third place for the number to come from, so it came from
nowhere.

**Fix.** `emit_obj_stack_count` on the generic emit path, appended straight after
the icon's desc so it inherits every offset the icon took (scroll, drag,
ghosting) by construction. Same conventions as the grid path: p11 yellow with a
drop shadow, baseline at `slotY + 9`, and the reference's `invNumber` K/M
abbreviation. Gated to the two kinds that actually carry an obj icon, so a plain
`SETGRAPHIC` sprite on a node with a stale `item_id` cannot sprout a number.

`src/ui/uitree_emit.c`.

---

### 5. The quest tab: 44 arguments into a 32-slot hook

**Symptom.** The side journal (629) mounted, its sub-panel (712) mounted, every
node was in the tree — and the panel drew nothing.

**How it looked in the dump.** Not like a missing packet at all:

```
BOUNDS com=0x02750000 (629|0)  abs=724,205  500x1   wh=500,1 modes=w0,h0
```

629's universe is `widthmode=1 heightmode=1` with no explicit size, and it had
resolved to a hard 500x1 sitting off the right edge of the screen. Everything
below it cascaded from that, so the tab icons landed at x=1100..1274.

**Tracing it.** `TORIRS_DUMP_SETSIZE=629` showed the writes, and
`TORIRS_CC_DEBUG=1` showed what was missing:

```
CC_CREATE parent=629|35 sub=0 -> com=0x02759272     SETSIZE (629|37490)   8x1
CC_CREATE parent=629|35 sub=1 -> com=0x02759273     SETSIZE (629|37491) 500x1
...
                        (no CC_CREATE)              SETSIZE (629|0)       8x1
                        (no CC_CREATE)              SETSIZE (629|0)     500x1
```

`cc_create` had stopped running, and the `cc_setsize` that followed it landed on
whatever the active component still was — the hook's own component, the
interface **root**.

**Root cause.** Script 2595 does `cc_create($component7, 9, 0, 0)` where
`$component7` is the `tab_line` component, threaded down from the hook's argument
list. That argument sits at **position 36**. The runtime hook path stored 32:

```c
int int_args[32];              /* CS2VM2_Op_IF_SetOnEventHandler          */
int argv[UITREE_HOOK_ARG_MAX]; /* UITREE_HOOK_ARG_MAX == 32               */
int int_args[32];              /* RS_CS2VarTransmitHook                   */
uint32_t str_arg_mask;         /* one bit per position — also 32          */
```

So `tab_line` arrived as `0`, `cc_create(0, …)` found no such parent and
silently no-op'd, and the writes meant for a new child hit the root.

The *onload* path was already 64 wide (`TORIRS_COMPONENT_HOOK_ARG_MAX`), which is
why 629's own onload worked and only the `if_setonvartransmit` re-dispatch broke
— and why the panel drew correctly for a moment and then collapsed.

**How wide does it need to be?** Measured, not guessed. Scanning every
`if_seton*` in the decompiled cache:

```
max hook argument count: 44   (script3040, the side journal's var-transmit hook)
registrations over 16 args: 94
registrations over 32 args: 14
```

**Fix.** One constant, `CS2VM_SETON_INT_ARG_MAX = 64`, used by every stage of the
chain, and `str_arg_mask` widened to `uint64_t` to match:

- `cs2vm2_host.h` — the four `IF_SetOn*` / `CC_SetOnOp` request structs
- `cs2vm2.c` — four pop/copy sites
- `uitree.h` — `UITREE_HOOK_ARG_MAX`, `UITreeRuntimeScriptHook.str_mask`
- `rs_cs2_host.h` — the inv / var / stat transmit hooks (the copy is a
  `sizeof(dest)` memcpy, so the two cannot differ)
- `rs_cs2_dispatch.{c,h}`, `uitree.c`, `task_interface_open.c`

`cs2vm2_trigger_args.c` was fixed too. It is not on the live path, but it is
worse than the others and would have been a trap for whoever wired it up: its
pop loop is bounded by the cap, so a long signature left the surplus arguments
**on the stack**.

---

### 6. The combat tab: the DB opcodes had the wrong stack shape

**Symptom.** "Unarmed / Combat Lvl: 3" and an Auto Retaliate button. No Punch,
no Kick, no Block.

**Where the styles come from.** Not a packet. Interface 593's onload
(7592 → 7593) ends with:

```
$op3, $string7, $graphic16, ... = ~script7603(%varbit357);
...
if ($graphic16 = null) { ... }
if ($graphic16 ! null) { if_sethide(false, interface_593:6); ... }
else                   { if_sethide(true,  interface_593:6); }
```

and `script7603` is a client-database query:

```
db_find_with_count(319488, $int0, 0);      // table 78, column 0 = weapon category
$int2 = db_findnext;
$int10 = db_getfieldcount($int2, 319504);  // table 78, column 1 = the style tuples
$int3, $string0, $string1, $int4 = db_getfield($int2, 319504, $int9);
```

A query that returns nothing leaves all four graphics `null`, and all four style
buttons hide themselves. Which is exactly what was on screen.

**Three separate bugs, all in `exec_db`.**

**(a) `db_find*` takes three stack arguments, not two.** The disassembly is
unambiguous — three pushes, and the stack has to balance:

```
0  push_constant_int  319488       (the dbcolumn)
1  push_int_local     0            (the value)
2  push_constant_int  0            (???)
3  db_find_with_count
4  pop_int_local      1            <- consumes the count; net must be 3 in, 1 out
```

The third is a **value-type tag**: `2` means the search value is on the string
stack, anything else means the int stack. xrsps's `src/rs/cs2/handlers/DbOps.ts`
is explicit about it (`const isString = ctx.intStack[--ctx.intStackSize] === 2`),
and it removes the need our implementation had invented — to load the table index
*before* popping the value, just to find out which stack it was on.

Our code popped the column first (getting the tag, `0`), then the value, and left
the third argument on the stack. So the query asked the weapon-style table for
category `0` and every later read was shifted by one.

**(b) The tuple nibble is 1-based.** A dbcolumn packs
`(table << 12) | (column << 4) | tuple`, and `tuple` selects one field of the
column's tuple — except `0`, which means *the whole tuple*. Ours read it
0-based, which gets both ends wrong: the common `0` case pushed one value where
the script pops several, and the highest field read as out-of-range and pushed
everything.

The corpus settles it without ambiguity. Table 166 column 32 is a 4-tuple:

```
$a,$b,$c,$d = db_getfield(row, 680448, i)   // 0xA6200, nibble 0 -> four values
$a          = db_getfield(row, 680452, i)   // 0xA6204, nibble 4 -> one value
```

Across every single-call assignment in the cache: nibble 0 appears with return
arity 1, 2, 3, 4, 5 and 7; every non-zero nibble appears with arity 1.

**(c) `db_getrow` is indexed access into the query, not a row lookup.** Scripts
call it as `while ($i < $count) { $row = db_getrow($i); if ($row ! -1) {…} }`.
Ours treated the argument as a row id, ensured residency, and **pushed nothing**
— so the assignment read whatever was underneath.

**Also fixed:** `db_find_filter*` now intersects the query in flight rather than
replacing it, which is what "filter" means and how a two-column query is
expressed (`db_find_with_count(catcol, …); db_find_filter_with_count(subcol, …);
db_findnext`).

`src/game/rs_cs2_host.c`.

---

### 7. The magic tab: a call stack two thirds deep enough

**Symptom.** The spellbook mounted and drew its "Filters" button and nothing
else, with one abort:

```
Task_CS2Run: script 2621 failed at opcode 40 pc 92 (invoked as script 2262 for component 0xda0000)
```

**Root cause.** Opcode 40 is `gosub_with_params`, and pc 92 is script 2621
calling **itself** — it is a quicksort, recursing once per partition. The only
way that call fails is the frame cap:

```c
if( vm->frame_sp >= CS2VM_MAX_FRAMES )
    return CS2VM_EXECNO_ERROR;
```

`CS2VM_MAX_FRAMES` was 50, with a comment claiming it matched the reference
client's `Interpreter_frames[50]`. The cache falsifies that: instrumenting the
peak showed **70**, sorting the rev-239 standard spellbook.

**Fix.** 128 — well above the measured 70 rather than at it, because the depth is
O(spells) in the worst case and the other spellbooks are larger. Frames are fat
(~12 KB each), so the comment now says what the ceiling costs before anyone
raises it again.

And the overflow now reports itself:

```
CS2VM2: call depth 128 exhausted calling script N from script M (raise CS2VM_MAX_FRAMES)
```

which is the line that would have turned this from an afternoon into a minute.

`src/cs2vm2/cs2vm2.{c,h}`.

---

### 8. The music tab — half fixed, and still open

**Symptom.** `script 9290 failed at opcode 46 pc 82`.

**First cause (fixed).** Opcode 46 is `pop_array_int`, and the array it writes is
declared:

```
define_array  operand 262259 = 0x00040073   ->  slot 4, element type 0x73 = 's'
```

`DEFINE_ARRAY` carries the element type in the low half of its operand, and it is
not decoration: an array declared `s` lives on the **string** stack. Ours threw
the type away and always popped an int, so `$names($i) = ""` popped an integer
that had never been pushed.

Fixed: `CS2VM2_Array` now carries `is_string` and a union of `int[]` / `char*[]`
(one slot is only ever one type, so the storage is shared), `PUSH_ARRAY_INT` /
`POP_ARRAY_INT` route to the matching stack, and the yield-undo log records
whichever the cell held. String cells hold pool pointers, which the thread frees
as a unit at script start and never individually.

**Second cause (open).** With that fixed the script runs 99 more opcodes and
stops at:

```
180  8007  ?   UNKNOWN
181  0     push_constant_int   <- fails: the int stack is full
```

**Opcode 8007 is `ARRAY_COUNT_MATCHES`** (xrsps `Opcodes.ts`), and it is absent
from our opcode table entirely — not stubbed, not asserted, just unknown, so it
consumes nothing and the stack fills up in the loop. Implementing it is new
opcode work and has not been done.

---

### 9. The summary panel's numbers sat in the wrong place

**Symptom.** Every value in the journal's Character Summary was jammed against
the left edge of its cell instead of sitting under its label, and the icon that
belongs beside it had drifted so far left it was outside the panel entirely.

```
┌──────────────────┬──────────────────┐        ┌──────────────────┬──────────────────┐
│  Combat Level:   │   Total Level:   │        │  Combat Level:   │   Total Level:   │
│ 0            📊  │ 33               │  ->    │      ⚔ 0         │     📊 33        │
└──────────────────┴──────────────────┘        └──────────────────┴──────────────────┘
        before                                            after
```

**What the panel is doing.** Nothing is placed by the cache. Each cell is built
by `~script3950`, which centres an icon and its value **as a pair**:

```
def_int $width13 = parawidth($string2, if_getwidth($component0), fontmetrics_494);
def_int $int15   = 18;                                   // icon size
def_int $int16   = calc($int3 + $int12 / 2);             // cell centre
def_int $int17   = calc($int16 - ($width13 + $int15 + 4) / 2);   // icon x
def_int $x18     = calc($int17 + $int15 + 4);            // value x
cc_setposition($int17, …)    // icon
.cc_setposition($x18,  …)    // value
```

Everything on that line hangs off `parawidth` — the measured pixel width of the
value string. Get it wrong and the pair is centred on a phantom.

**Root cause.** The values are colour-tagged. `"<col=0dc10d>0</col>"` is
**19 bytes that render one glyph**. Our `parawidth` walked the string a byte at
a time adding `font->draw_width[c]` for every one of them, markup included:

```
parawidth("<col=0dc10d>0</col>", 184, font 494)  ->  104     (should be ~6)
```

Feed 104 back through the script's arithmetic and the answer falls out exactly:
cell centre is 45, so the icon goes to `45 - (104 + 18 + 4)/2` = **-18**, i.e.
eighteen pixels left of the panel's own left edge — which is precisely where the
`BOUNDS` dump had it (`abs=492` for a panel starting at `510`). The value text
followed at `-18 + 22 = 4`, hard against the left edge, inside a 104-wide box.

Nothing about the symptom said "measurement". It looked like a positioning bug,
and it is worth noticing that the numbers *were* readable and roughly in their
cells — this is the failure mode that survives a glance.

**Why the two disagreed.** The renderer already knows what markup is. It has a
proper tokeniser, `font_try_consume_markup` in `3rd/toridraw/toridraw_font.c`,
handling `@xxx@`, `<lt>`, `<gt>`, `<col=RRGGBB[AA]>` and `</col>`. The CS2 host
had its own, separate, markup-unaware byte loop for measuring. Two
implementations of "what does this string look like", and only one of them had
ever heard of tags.

**Fix.** Give them one implementation. `ToriDraw_FontMarkupTokenLength` exposes
the renderer's tokeniser — font-independent, since the grammar needs no font —
and the host's `parawidth` / `paraheight` now skip exactly the tokens the glyph
walk skips, emitting the character for the two tokens that render one (`<lt>`,
`<gt>`). The two measurement helpers also collapsed into one wrap pass, since
they only ever differed in which half of the result they returned.

`3rd/toridraw/toridraw_font.{c,h}`, `src/game/rs_cs2_host.c`.

**The rule.** Any code that walks a string to work out how wide it will be is a
*second implementation of the renderer*, and it will drift. Make it call the
renderer's tokeniser or it is wrong the first time someone passes it a tag —
which, at rev 230, is most strings, because the colour is in the text.

---

## 3. What made this tractable

Every one of these was found with the harness that already existed. Worth
knowing they are there:

| knob | answers |
|---|---|
| `TORIRS_DUMP_TREE_EXIT=1` | is the interface mounted, and under what? (needs `TORIRS_EXIT_BMP` set — it lives inside that block) |
| `TORIRS_DUMP_BOUNDS=<group>` | resolved geometry, size modes, scroll extents |
| `TORIRS_DUMP_SETSIZE=<group>` | every size write a script made, in order |
| `TORIRS_CC_DEBUG=1` | every `cc_create`, its parent and the uid it produced |
| `TORIRS_SIM_CLICK_AT="frame,x,y[;…]"` | drive the UI headlessly |
| `TORIRS_NET_DEBUG=1` | `if-opensub` / `if-closesub` / `if_setevents` as the client sees them |
| `MOCK230_VERBOSE=1` | the server's side of the same |
| `3rd/rscache/tools/cs2/cs2 decompile \| disassemble` | what the panel is actually asking for |

The disassembler earned its keep twice: the DB stack shape and the array element
type are both things the decompiled source *hides* (it prints
`db_find_with_count(col, value, 0)` without saying which end of that is popped
first, and `def_string` without the operand). The raw ops say it exactly.

---

## 4. State after this

Working: all seven sidebar tabs, the four minimap orbs, item stack counts, the
journal summary's cell layout, the world map opening and closing, and
`CLOSE_MODAL` on any interface.

Open, in the order they block things:

1. **Opcode 8007 `ARRAY_COUNT_MATCHES`** — the music tab, above.
2. **The world map surface is black.** The panel, legend, search box, zoom
   buttons and close button all draw; the map area does not. Separate from
   "the world map won't open", and untouched here.
3. **`test-ui-slots` and `test-db` fail**, and did before any of this — both
   assert on `[cache:boot] identity`, i.e. the test harness points at a cache
   without stating a profile.
4. **A stale `src/build/net_transport_embed.o`** from an old `EMBED_SERVER=1`
   build breaks the link whenever a header it depends on changes. `rm` it. It is
   a build hazard, not a code one, but it looks like a code one.

---

## 5. chatmenu (219) reported as spread out vertically — investigated, does not reproduce

**Report.** Screenshots showed the multi-option dialogue ("Select an Option",
`chatbox_multi_init` script 58, interface 219) rendering with option rows far
apart, the header-to-first-row gap too big, and the whole panel appearing to
overflow down into the chatbox's mode-button row (`All`/`Game`/`Public`/...).

**What was measured instead.** Driving Hans's real `[opnpc1,hans]` →
`~p_choice3` end to end (headless, `SDL_VIDEODRIVER=dummy`, `::talk hans 1`
typed through `TORIRS_SIM_TYPE`, continued past the greeting with
`TORIRS_SIM_CLICK_AT` on `chat_left:continue`), `TORIRS_DUMP_BOUNDS=219`
matches script 58's own pixel math exactly for the 3-option branch:

```
BOUNDS com=... (219|1)     type=18 abs=40,373 479x122   <- the options container
BOUNDS com=... (219|40043) type=14 abs=40,383 479x20     <- header, rel y=10
BOUNDS com=... (219|40044) type=14 abs=40,407 479x20     <- row 1, rel y=34
BOUNDS com=... (219|40045) type=14 abs=40,431 479x20     <- row 2, rel y=58
BOUNDS com=... (219|40046) type=14 abs=40,455 479x20     <- row 3, rel y=82
BOUNDS com=... (219|40047) type=15 abs=111,385 57x13      <- sworddecor left
BOUNDS com=... (219|40048) type=15 abs=391,385 57x13      <- sworddecor right
```

`34, 58, 82` (desktop, `~int_device`'s first argument) relative to the
container's `abs_y=373` is exactly `407, 431, 455` — a 24px row pitch, 20px
box, 4px gap, matching the real client's tight look. The corresponding
`TORIRS_EXIT_BMP` screenshot shows the rows tightly packed against each
other, snug against the mode-button row below (which is itself baked at
`abs_y=480` in interface 162 — the options container's `373+122=495` bottom
edge sitting just above it is correct, cache-baked geometry, not overflow).

The same result held for every variant tried, all against the real
`chatbox_multi_addoption` (script **59**, confirmed by disassembling script
58's `gosub_with_params ... 59` and independently disassembling script 59
itself — its `cc_settextalign` call passes a literal `0` for `line_height`,
same as script 58's header, so the "inflated line height" and "script 59
doesn't really run" hypotheses are both closed):

- 2-option (`~p_choice2`) and 5-option (`~p_choice5`, by temporarily editing
  `hans.rs2` for the run and reverting it — see the diff discipline note
  below) branches: same exact-match result, rows touching edge-to-edge in
  the tightest (5-option, 16px-tall rows) case.
- Option text long enough to overflow the 479px row width (~120 chars):
  positions unchanged; the renderer does not wrap it (it draws past the row
  edge instead) but does not push any other row's position either.
- Sampled every few frames from the click that dismisses the greeting
  through settling (multiple `chatbox_multi_init` re-runs happen in that
  window, each `cc_deleteall` + rebuild, dynamic ids climbing each time) —
  every intermediate frame's row geometry was internally consistent with
  script 58's math, never a stale/duplicate/mis-sized set.
- Real TCP transport (`src/build/mock230` + `manifest_osrs230.ini`) gave
  byte-identical `BOUNDS` output to the embedded-server transport
  (`manifest_osrs230_embed.ini`) for the same interaction.

**Conclusion.** On this HEAD, the reported symptom does not reproduce through
the `p_choice2..5` → `chatbox_multi_init` (58) → `chatbox_multi_addoption`
(59) path, in any option count, with short or very long option text, over
either transport. If this is still visible live, the next things to get from
whoever saw it (rather than re-guessing): the exact NPC/dialogue, whether the
window was resized/stretched from the fixed 765x503 layout root
(`UITree_LayoutRootWidth/Height`, `src/ui/uitree_layout.c:10-11`), and a fresh
screenshot with `TORIRS_DUMP_BOUNDS=219` from the same session — because
every number this investigation could check came back matching the
CS2 script that owns the layout.

**Diff discipline note.** Reproducing the 2- and 5-option branches required a
temporary edit to `OSRS-Content/osrs239-content/server/scripts/areas/lumbridge/scripts/hans.rs2`
(swapping `~p_choice3` for `~p_choice2`/`~p_choice5` with placeholder option
text) so `mock230-scripts` would compile a script pack exercising them; the
file was reverted to its original `~p_choice3` form afterward and the diff
confirmed empty before finishing. No content or engine change shipped from
this investigation.

**Also found and fixed in passing:** `make -C src test-chat-widgets` failed
to *link* (`_strtobase37` undefined), independent of any of the above — the
target's source list (`src/makefile`) had `game/rs_social.c` but not
`net/jbase37.c`, which `rs_social.c`'s `name_hash` calls into. One line added;
the test builds and passes clean (`chat widgets: all checks passed`).
