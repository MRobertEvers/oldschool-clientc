# The Tool Leprechaun's store, and the four things it broke

> **What this is.** The rev-230 farming tool store (`farming_tools` 125 and
> `farming_tools_side` 126), opened from the Tool Leprechaun. It is a
> `PORTING_GUIDE.md` §5 feature — Farming launched five months after the
> reference's 2004 snapshot, so there is nothing to port — and, like the skill
> guide before it, almost all of it was already in the cache.
>
> What was missing was one compiler rule, one client cap, one dead persistence
> path and one config field the loader was throwing away. Three of the four are
> not farming's, and all four had been invisible.
>
> Read `UI_ERA_PORTING_GUIDE.md` for the model this sits inside and
> `REV230_UI_BLANK_PANELS.md` for the triage ladder — §1's first rung is what
> found the first bug here. This is the case file.

---

## 0. The one-paragraph version

"Deposit five rakes with the Tool Leprechaun" crosses four layers and each one
lost something quietly. **sscompile** resolved `farming_tools` to the *loc* of
that name, so the server sent a well-formed mount for interface 7516 and the
client politely skipped it. **The client's CS2 host** ran out of var-transmit
hook slots at 128 — the gameframe alone uses 131 — so the store's repaint
listener was dropped and the panel showed 0/100 over a rake that really was in
it. **Persistence** had never had a caller, and when it got one it still did not
work, because a loaded varp is not a *changed* varp and nothing sent it.
And **`oc_desc`** was a declared opcode with no implementation because
`mock230_objinfo` decoded the examine text and threw it away.

None of the four says anything at the point of failure. Three of them produce a
panel that looks completely correct until you interact with it.

---

## 1. What the client already does

Nearly everything. `farming_tools.if`'s root carries

```
onload = i:1055, i:-2147483645, <13 component uids>
```

and clientscript **1055** lays out all twelve cells — label, `<stored>/<capacity>`
line, item icon, the four quantity radios and the deposit button. **1056** does
the same for the side panel. The server sends a mount and nothing else: no
component ids, no text, no counts.

The repaint is the client's too. Clientscript **2749** hangs

```
if_setonvartransmit("script1059(...){var615, var2084, var439, var967, var1785, var1704}")
```

on every store cell, and **2751** hangs `if_setoninvtransmit{inv_93}` on every
carried cell. So "the number went up and the label turned green" is one varp
packet and one inventory delta the client already knows what to do with.

### 1.1 Storage is varbits, not a container

This is why the feature needs no `container_for` work at all — and it is the
fact the discovery doc got closest to and still undercounted. Clientscript
**1063** reads each cell's stored count out of one to three varbits:

| cell | expression | width |
|---|---|---|
| rake / dibber / spade / secateurs / trowel | `2 * extraN + N` | 1 + 6 = 7 bits |
| empty buckets | `256 * extra2 + 32 * extra + buckets` | 5 + 3 + 2 = 10 bits |
| compost / supercompost | `256 * extra + N` | 8 + 2 = 10 bits |
| ultracompost / plant cure | `N` | 10 bits |
| watering can | `enum_136` key, 0 = empty | 4 bits |
| bottomless bucket | variant index, `> 1` = filled | 3 bits |

Each of the first three rows is **one N-bit integer split across varbits that
did not fit in one varp**, and the pieces are scattered across six varps with
names from six unrelated features (`lotr_region`, `osb5`, `my2arm_perm_1`,
`alternate_spells`, `canoeing_menu` and `farming_tools` itself). The count is 22
varbits, not the "~14" the discovery doc estimated.

The consequence worth stating: **five rakes are `rake = 1` and `extrarakes = 2`
in two different varps.** A save that persists one and not the other reads back
as four, which is the shape of `docs/farming_tools.md`'s own mutation table
below.

### 1.2 The op index is not a quantity

Clientscripts **1060** (store side) and **1062** (carried side) build the ops
by switching on `~script376(2193)` = `%farming_tools_selectedquantity`:

```
mode 0:  op1 = 1     op2 = 5    op3 = X    op4 = All
mode 1:  op1 = 5     op2 = 1    op3 = X    op4 = All
mode 2:  op1 = All   op2 = 1    op3 = 5    op4 = X
mode 3:  op1 = X     op2 = 1    op3 = 5    op4 = All
```

The selected quantity is always op 1 and the rest fill in behind it. So **op 2
means "5" under one mode and "1" under the other three**, and a server with a
fixed op→quantity table stores 1 when the menu row the player read said 5 —
with no error anywhere.

### 1.3 …which is why the server has to own the radio

Clientscript **2005** answers a radio click by calling **377**, whose entire
body is `%varbit7792 = $mode` — a CS2 `pop_varbit`, which in this client is a
stack-balanced no-op (`CS2VM2_Op_PopVarbit`, verified). **The client cannot
change its own quantity mode.** Whatever the server last transmitted is what
1060 drew the labels from.

So the server arms op 1 on the four radios, writes the varbit, and the varp
transmit repaints them. Get that backwards — let the client "own" it — and the
labels and the op-index table disagree permanently.

### 1.4 The ops are the server's, and the cache says so plainly

Every tool cell in both `.if` files carries `op1=* op2=* op3=* op4=*` (plus
`op9`/`op10`) with **no `onop=`**. What the client *does* attach is
`if_setonop("script487(event_com, cc_getid, 125, 0)")`, and script487 is six
lines of "dim the cell for ten frames and put it back". A click flash, not a
handler.

The discovery doc read that gap as a *missing* handler ("no click handler body
exists in this corpus, same gap class as shop's buy-op"). It is the opposite: an
op with a label and no `onop` is the positive signature of a server op at rev
230, the same signature `friends:ignore` and the stats tab's "View \<skill\>
guide" carry. A numbered op runs the local onop hook **and** goes to the server
as `IF_BUTTON<n>` — both, not either — with the events mask as the only gate.

---

## 2. The four seams

### 2.1 `if_openmain_side(farming_tools, …)` opened interface 7516

The first symptom was the whole store panel missing while the sidebar half drew
perfectly. `REV230_UI_BLANK_PANELS.md` §1 rung 1 answered it in one run:
`TORIRS_DUMP_BOUNDS=125` printed **nothing**, so the interface was not in the
tree at all, so it was a packet. `TORIRS_NET_DEBUG=1` then said:

```
if-opensub: iface=7516 target=0x00a10010 (161<<16|16) type=0
interface open: pack 7516 missing from cache; skipping mount
```

`farming_tools` is **interface 125, varp 615 and loc 7516**, and
`SSC_SymbolsFind` with no kind returns the lowest-numbered kind that has the
name — `SSC_SYM_LOC` sorts before `SSC_SYM_INTERFACE`. The script compiled, the
server sent a perfectly well-formed packet, and the client did the right thing
with an interface that does not exist.

This is exactly what `LOSTCITY_PORT_TRIAGE.md` §7.5 means by *"the danger is the
ones that resolve"*, and the fix is the mechanism the compiler already had for
the identical problem with stat names:

```c
else if( op_name && strncmp(op_name, "IF_OPEN", 7) == 0 )
    base_hint = SSC_SYM_INTERFACE;
```

`parse_command` sets an argument-kind hint for the duration of one command's
argument list; a miss falls through to the unhinted lookup, so IF_OPENSUB's
*component* first argument (`toplevel_osrs_stretch:mainmodal`) is unaffected —
no interface carries that name.

**This was latent for every interface whose name collides, not just this one.**

### 2.2 The CS2 host ran out of var-transmit hooks at 128

With the mount fixed the panel drew, the counts were right, a deposit went
through — and the store cell stayed at `0/100` while the sidebar beside it
counted down correctly. Same click, two panels, one of them stale.

Measured rather than guessed: a debug print in `Task_CS2VarTransmitDispatch_Run`
showed **131 distinct components** holding a var-transmit hook, and
`RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX` was **128**. The rev-230 gameframe fills the
table before any panel opens. `rs_cs2_acquire_var_transmit_hook` compacts dead
entries, finds none, returns NULL — and `exec_set_on_var_transmit` returns
`CS2VM_EXECNO_OK` without registering anything. The script that asked carries
on; the panel draws; nothing ever updates it again.

The side panel worked because its listener is an **inv**-transmit hook, a
different table with room left. Two identical-looking panels, one correct, and
the difference was which array was full.

Fixed by raising all three caps to 512 **and by making overflow say so once**:

```
cs2 host: var-transmit hooks full (512); component 0x007d0008 will never update
```

A cap reached quietly is a cap that gets diagnosed as a missing packet.

### 2.3 Persistence: two callers, and then a third thing

`mock230_save_player` / `mock230_load_player` were written complete and had
**no callers anywhere** — dead code since they landed, which is why
`player/newplayer.rs2`'s `%newplayer_seeded` gate reads as a declaration of
intent. Three changes made them real:

1. **Load** at the top of `mock230_world_login`, before step 1. Everything below
   *sends* what the load changed, and `[login,_]` (phase 7, next tick) needs
   `%newplayer_seeded` already restored or a returning player is re-dealt the
   opening kit.
2. **Save** at the top of `mock230_world_remove_player`, while the player is
   still whole — the bank is freed and the slot released below it. Both hosts
   call it, so both get persistence from one call.
3. `mock230_embed_stop` now **logs its clients out** instead of freeing their
   sessions where they stand. It never mattered while `remove_player` only
   released a slot the process was about to drop; it matters the moment the save
   lives there, because closing the embedded client — which is how anyone
   actually plays this — threw the session away while a socket logout kept it.

And then the part that is easy to miss and cost a second round of screenshots:
**a loaded varp is not a changed varp.** The loader writes `player->varps[]`
directly, so nothing marks it, and phase 10 sends only what moved *this tick*.
The state was restored perfectly and the client was never told: a returning
player's store read `0/100` over five rakes that really were in it. Step 4b of
the login burst now sends every declared-`transmit` varp with a non-zero value,
directly — the same shape, and for the same reason, as the containers sent in
full beside it. Non-zero is the right filter: the client starts every session
with a zeroed varp table, so a zero is already agreed.

Two side effects worth knowing about, both handled: `mock230 --selftest` and
`mock230_embed_test` now read a save at login, so both point `MOCK230_SAVES` at
their own directory and wipe it — a run whose result depends on the previous
run's file is not a test. And `save_dir()` needed `mkdir -p`: one `mkdir` was
enough for the bare `saves` and silently failed for anything nested.

### 2.4 `oc_desc` — the examine text was decoded and discarded

Op 10 is "Examine" on nearly every panel in the game and had no server-side
answer at all: `SS_OP_OC_DESC` (4204) was declared and uncovered, and the note
beside the config-query batch said *"the obj record's examine text is read by
nothing here"*. It was true, and it was a load-time drop rather than a decoder
gap — `RSCache_Dat2ConfigObj.examine` (config opcode 3) has been decoded all
along and `mock230_objinfo`'s `record()` simply did not copy it.

Three lines to store it, one host case to push it, and a note record inherits
the item's line the same way it already inherits its name (`[cert_rake]` is
`certlink` plus `certtemplate` and nothing else). An absent examine pushes `""`
rather than NULL, so a script that prints unconditionally prints a blank line
instead of dereferencing one.

---

## 3. What is content

All of it, except the four seams above. `server/scripts/interface_farming/`:

| file | what |
|---|---|
| `configs/farming_tools.constant` | the capacities (transcribed from `enum_2193`), the four quantity modes and what an op index means under each, the watering-can charge range, the bottomless-bucket variants |
| `configs/farming_tools.varp` | the seven basevars, `transmit=yes scope=perm`, each under the **cache's** name |
| `configs/farming_tools.npc` | `wanderrange=0` — a store that walks away is a bug |
| `configs/farming_tools.spawn` | one leprechaun, marked as a test-world placement |
| `scripts/farming_tools.rs2` | pack/unpack, the quantity table, store, remove, examine, deposit-all, the messages, the arming, `::farmkit` |
| `scripts/farming_tools_ops.rs2` | 118 one-line `[if_button<n>,…]` bindings |

Two things in there used to be transcriptions of cache data rather than reads
of it, and both are marked as such: the twelve capacities (`enum_2193`) and the
watering can's charge table (`enum_136`). **That limitation is retired** —
`mock230_content.c` now loads `configs/all.enum` as rank-0 config (authored
`.enum` under `server/scripts` still wins on name), so
`enum_getoutputcount(enum_2193)` is expressible. The constants remain until a
follow-up rewrites those two tables to read the cache; the Character Summary /
chrome popout path already consumes the loader
(`docs/account_summary_server_reqs.md` §1).

### 3.1 Why 118 trigger blocks

Because at rev 230 **the op index *is* the trigger**. `[if_button1,farming_tools:rake]`
and `[if_button3,farming_tools:rake]` arrive on the same component from the same
player and mean different things, and there is no `last_verb` command to read
the index out of (see `docs/skill_guide.md` §3.1, which is where the numbered
`SS_TRIGGER_IF_BUTTON1..10` came from). They are generated mechanically and each
body is one call.

### 3.2 `::farmkit`

A `[debugproc]`, which is where `PORTING_GUIDE.md` §2.3 puts every cheat beyond
a raw engine poke. It exists because there is nowhere in this world to *get* a
rake — the opening kit is combat gear, no shop sells one, nothing drops one — so
without it the store cannot be exercised from a standing start, by a person or
by the headless harness. Five rakes rather than one, because Store-5 and
Store-All are indistinguishable from Store-1 when the player carries exactly
one.

---

## 4. Verified in the client

Headless, embedded server, real clicks. BMPs read, not tree dumps.

```
SDL_VIDEODRIVER=dummy MOCK230_SAVES=$S/saves \
TORIRS_NET_CHEAT="farmkit" TORIRS_MAX_FRAMES=1200 \
TORIRS_SIM_CLICK_AT="300,455,160,1;310,400,190;450,375,291;600,555,228" \
TORIRS_EXIT_BMP=$S/all.bmp MOCK230_VERBOSE=1 \
  ./src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test
```

- Right-click the leprechaun → **Talk-to / Exchange / Deposit-all / Examine**;
  "Exchange" walks the player over and mounts both halves.
- The store draws with all twelve cells and their real capacities —
  `0/100` for the tools, `0/1000` for the bulk, `0/1` for the watering can and
  the bottomless bucket — which is `enum_2193` read by the client and the
  content constants agreeing without ever having met.
- `::farmkit` → the carried side shows **Rake 5**, **Compost 5**,
  **Secateurs 1** (drawn as the *enchanted* pair, the variant preference), and
  **Watering can(6) 1** (named from the carried can, not the cell).
- Click the "All" radio → it lights and "1" goes out, from the varp transmit
  alone.
- Click the carried rake → store **5/100** green, carried **0**. Under mode "1"
  the same click stores exactly one.
- Click the store rake → all five come back, as five slots.
- Log out, log in, `::talk farming_tools_leprechaun 3` → **5/100**, still on
  "All", carried items restored.

Screenshots at `scratchpad/ui2/{open2,kit,store2,all,rm,relog3}.png`.

---

## 5. The permanent check

`mock230 --selftest`, section **"the tool leprechaun's store"**
(`mock230_world.c`). It walks the player to the leprechaun, sends a real
`OPNPC3`, and asserts:

- **two** IF_OPENSUBs, `farming_tools` → `mainmodal` type 0 and
  `farming_tools_side` → `sidemodal` type 3, **by name** — this is the assertion
  the loc/interface collision fails;
- at least 29 IF_SETEVENTS, and `farming_tools:rake`'s mask is exactly
  `1566` — the `.if`'s own clickmask;
- six op/mode combinations on one component: mode 1 op 1 stores one and op 2
  stores five, mode All op 1 stores five and op 2 stores one, mode 5 the
  mirror. This is the quantity table, and it is the assertion a fixed
  op→quantity map fails;
- five rakes pack as `rake=1 extrarakes=2`, and 300 buckets as `12/1/1` — the
  pieces, not the total, so a pack/unpack that agrees with itself still fails;
- Remove-All hands back five rakes **in five slots**, because a rake does not
  stack;
- a full store refuses a deposit and keeps exactly its 100;
- a save/load round trip restores both varps and the quantity mode.

Proven to fail, by mutation:

| mutation | what the selftest said |
|---|---|
| swap op2/op3 under mode 1 in `~farming_quantity` | `mode 1, op 2 = Store-5 should store 5 rake(s), stored 0` |
| `%farming_tools_rake = $count` (collapse the split) | `5 rakes should pack as rake=1 extrarakes=2, got 1 and 0` (+3 quantity cases) |
| drop `^if_event_op9` from `~farming_events_store` | `farming_tools:rake should be armed for ops 1-4, 9 and 10 … got 1054` |
| `scope=perm` → `scope=temp` on `farming_tools` | `5 rakes should survive a logout, 4 did` |
| disable the `IF_OPEN*` interface hint in sscompile | `mount 0 should be farming_tools (125), got interface 7516` |

The fourth is the one worth keeping: the low bit of the count lives in the varp
that was un-declared, so five came back as **four** — a state that is neither
right nor obviously wrong.

---

## 6. Still open

**Ops 6..10 are pickable.** `UITREE_MENU_OPTION_SLOTS` is 10; CS2 `cc_setop`
writes through, the minimenu builder emits `INV_BUTTON1..5` for slots 0..4 and
`IF_BUTTON` (ops 6..10 on the wire) for slots 5..9. Bank Withdraw-X/All and the
farming tools' "Banknotes" (op 9) / "Examine" (op 10) labels now appear. The
remaining farming gap for those two is behaviour (how much "Banknotes"
withdraws), not visibility.

**"Banknotes" has no source.** Clientscript 1060 gives op 9 a label and no
number, and nothing in the cache or the reference says how much it withdraws.
Read here as "the selected default quantity, noted", stated as a content
decision in one place and changeable in one line.

**`farming_view` (179) is untouched** — the 107-patch grid, and the crop-growth
simulation behind it, which is a gameplay system rather than a UI. The discovery
doc's account of it is accurate as far as it goes; note that its
`farming_view_setpanel`'s caller "not present in this corpus" is the usual
decompiler gap, not a cache gap.

**The leprechaun stands beside Lumbridge castle** because there is no farming
patch in any map square this world loads. The spawn file says so; move him when
the patches land. Nothing in the scripts knows where he is.
