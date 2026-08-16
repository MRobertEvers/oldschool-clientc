# Slayer Rewards, and the three client bugs it was hiding

> **What this is.** The rev-230 Slayer reward panel (`slayer_rewards`, interface
> 426) opened from a slayer master's "Rewards" op, plus the task-list popup
> (`slayer_rewards_task_list`, 924) behind its "View List" button. A
> `PORTING_GUIDE.md` §5 feature: Slayer launched in March 2005, six months after
> the reference's snapshot, and there is no `skill_slayer` anywhere in
> `LostCity_Server/content/scripts` — every "slayer" hit there is Dragon Slayer
> or Demon Slayer quest content. There is nothing to port.
>
> Almost all of it was already in the cache. What was missing on the server was
> **nothing at all** — no new opcode, no new packet, no new trigger. What was
> missing on the client was three things, none of them slayer's, and each of
> them produced a panel that mounted, drew, and lied.
>
> Read `UI_ERA_PORTING_GUIDE.md` for the model this sits inside and
> `REV230_UI_BLANK_PANELS.md` for the triage ladder. This is the case file.

---

## 0. The one-paragraph version

The server's whole job here is: set one varbit, send one IF_OPENSUB, arm two
components, and answer one trigger whose `last_slot` names the transaction. All
five of those were expressible on day one — G1 (`SS_TRIGGER_IF_BUTTON1..10`) and
G2 (`runclientscript*`) landed in stage 1, `setbit`/`clearbit`/`testbit` were
already covered, and persistence landed in stage 2. **Every hour of this stage
went into the client.** Three bugs, in the order they surfaced: a fresh CS2
array initialised its cells to 0 instead of -1, so the builder behind all three
catalogue tabs took its "impossible" error branch on iteration zero and returned
— three tabs blank, nothing logged. `event_com` reported a dynamic child's own
runtime id instead of its parent's, so every `cc_find(event_com, event_comsubid)`
in the cache answered false and the points header never repainted. And this
server batched varp packets to the end of the tick while the reference writes
them inside the setter, so `set the master; open the panel` arrived in the wrong
order and the panel laid itself out against the *default* master.

None of the three is visible at the point of failure. Two of them produce a
panel that looks completely correct until you read a number on it.

---

## 1. What the client already does

Nearly everything. `slayer_rewards.if` carries **eleven** `onload=` hooks:

| component | clientscript | what it builds |
|---|---|---|
| `universe` | 405 (8 component uids) | the tab strip, via `~script407` |
| `border` | 227 | the title, from the literal string `Slayer Rewards` |
| `confirm` | 392 | the confirm-dialog chrome |
| `unlock_contents` | 190 → `~script1090(0, …)` | the Unlock catalogue |
| `extend_contents` | 1089 → `~script1090(1, …)` + `~script1092` | Extend, and the extend-all bar |
| `buy_contents` | 321 | the pouch shop |
| `tasks` | 328 → `~script422` | current task, stored task, seven block slots |
| `rewards_contents` | 411 → `~script1090(2, …)` | Cosmetics |
| `tasks_slot_1..6`, `tasks_slot_diary` | 424 ×7 | each slot's number and QP requirement |

The catalogue is the client's too. `~script1090` does

```
db_find_with_count(479329, <list>, 0)     // dbtable 117 slayer_unlock,
                                          // column 6 list_position, tuple 0
```

and draws one tile per row out of the table's own `icon`, `name`, `description`
and `cost`. All 67 unlocks, their prices as *drawn*, their artwork and their
descriptions come out of the cache with no help from the server.

So does the repaint. Every tile hangs
`.cc_setonvartransmit("script412(…){var1076, var1344, var5587, var695}")` and the
header hangs `cc_setonvartransmit("script409(…){var661}")`, so "the tile flipped
to a tick and the points went down" is two varp packets the panel already knows
what to do with.

### 1.1 The transaction protocol is one component and a sub id

This is the whole wire contract of the feature and it is entirely the cache's
invention. Every transaction the panel offers — unlock, disable, extend
everything, cancel, block, store, swap, unstore, unblock ×7 — is confirmed by a
single `cc_create`d overlay on **one** component, `slayer_rewards:confirm_button`
(426:10), and **its sub id is the action**:

```
[clientscript,script414]  cc_create($confirm_button, 3, <unlock bit>, 0)
                          cc_setop(1, "Unlock" | "Disable")
[clientscript,script423]  cc_create($confirm_button, 3, calc(66 + N), 0)
                          cc_setop(1, "Confirm")
[clientscript,script427]  cc_create($confirm_button, 3, calc(66 + N), 0)
                          cc_setop(1, "Unblock")
```

66 is the last unlock bit, so the verbs start above it:

| sub | action | from |
|---|---|---|
| 0..66 | that unlock bit | script414, the bit itself |
| 67 | cancel task | script423 `$int0 = 1` |
| 68 | block task | script423 `$int0 = 2` |
| 69..73 | unblock slots 1..5 | script426 cases 1..5 → 66+3..66+7 |
| 74 | unblock the diary slot | script426 case 7 → 66+8 |
| 75 | extend everything | script1092 → `script414(-1, calc(66 + 9), …)` |
| 76 | store / swap task | script423 `$int0 = 10` |
| 77 | unstore task | script423 `$int0 = 11` |
| 78 | unblock slot 6 | script426 case 6 → **66+12**, out of order |

Slot 6 being 66+12 rather than 66+9 is the only irregularity, and it is the whole
reason the armed range has to reach 78 rather than 77.

**Whether an unlock is being bought or disabled is not on the wire.** 414 puts
the bit in the sub id and the verb only in the label, so the server decides from
the ownership bit — which is the same bit the client drew the label from, so the
two cannot disagree.

### 1.2 The ops are the server's, and the cache says so twice

`if_setop(1, "View List", interface_426:29)` in clientscript 422 sets **no**
`if_setonop`. An op with a label and no matching onop is the positive signature
of a server op at rev 230 — the same signature `friends:ignore` and the stats
tab's "View \<skill\> guide" carry.

The confirm overlay is the other half of the same statement, and it is more
explicit than a missing hook. It *does* carry a `cc_setonop`, and the script it
names is clientscript **319**, whose entire body is

```
if_settext("Requesting...", $confirm_info);
if_setontimer("script320(calc(clientclock + 45), …)", $confirm_button);
```

— print "Requesting…" and arm a 45-tick timeout that reverts the panel. **A
client that draws a timeout for an answer is a client waiting for a server.** A
numbered op runs the local onop hook *and* sends `IF_BUTTON<n>` — both, not
either — with the events mask as the only gate (`src/app.c`,
`app_minimenu_dispatch`).

### 1.3 Ownership is 96 bits, and there is no collision

`~script8048(bit)` — disassembled, not inferred:

```
if (bit < 32)  return testbit(%var1076, bit % 32);
if (bit < 64)  return testbit(%var1344, bit % 32);
if (bit < 96)  return testbit(%var5587, bit % 32);
error("Tried to check a slayer unlock bit greater than 95 but no such variable exists.")
```

Three raw words. Decomposing `configs/all.varbit` against those three basevars
gives **67 single-bit varbits, contiguous 0..66, no gaps and no foreign tenant**:
32 in `slayer_rewards_unlocks`, 32 in `slayer_rewards_unlocks1`, 3 in
`slayer_rewards_unlocks2`. That matches dbtable 117 row for row.

`docs/slayer_rewards_server_reqs.md` called this "a real collision, confirmed" —
`slayer_unlock_storage` (varbit 12442) sharing bit 19 of `slayer_rewards_unlocks1`
with the ownership bitfield. It is not a collision: bit 51 **is**
`slayer_unlock_storage`, it is one of the 67, and it means exactly one thing. The
spec's related claim that "three unlocks (bits 35/43/53) bypass the bitfield
entirely" is also wrong — clientscript 413 reaches those cases only *after*
`~script8048($bit) = 1`, so they are ownership bits with an extra toggle on top.

---

## 2. What the server owes

Five things, and not one of them needed an opcode this tree did not have.

1. **The master, before the mount.** `%slayer_master_in_focus` (varbit 17868)
   then `if_opensub(toplevel_osrs_stretch:mainmodal, slayer_rewards, 0)`. §3.3 is
   why the order is load-bearing.
2. **Arm two components, over ranges.** `slayer_rewards:confirm_button` for op 1
   across sub 0..78 and `slayer_rewards:view_tasks` for op 1 across 0..1. A
   dynamic child's events come from its parent's `(from..to)` range
   (`app_if_events_for_node`), so `if_setevents(com, 0, 0, mask)` on a container
   full of dynamic children arms slot 0 and nothing else.
3. **Own the state.** The three ownership varps and `slayer_points` (varbit
   4068, bits 6..22 of varp 661 — 17 bits, so 131,071 is the cache's ceiling).
4. **Answer one trigger.** `[if_button1,slayer_rewards:confirm_button]`, with
   `last_slot` as the verb.
5. **Mount 924 and tell it which slot it went into.** `if_opensub` into
   `slayer_rewards:popup` (426:4 — the panel declares that slot for exactly
   this), then `runclientscript*(8059)(<that component>, 0, 0)`. 8059 hands its
   first argument down to `~script4206` → `~script612`, which hangs
   `if_setonsubchange`/`if_setondialogabort` on it so the popup closes itself
   when what is under it changes.

Clientscript 8059 has **no caller in any of the 9,433 scripts in this cache**,
which is the cache's way of saying the server runs it — the same signature
clientscript 1902 carries for the skill guide (`docs/skill_guide.md` §1).

---

## 3. The three client bugs

### 3.1 A fresh CS2 array is -1, not 0

Symptom: the panel mounts, the title draws, the tab strip draws, and the Unlock,
Extend and Cosmetics tabs are **empty**. `TORIRS_CC_DEBUG=1` shows `CC_CREATE`
on 426:13 (`tabs`), 426:21, 426:24..27 and the seven task slots — and none at all
on 426:16, 426:19 or 426:50, the three `~script1090` builds. A `db_find` trace
shows the query answering correctly: 24, 29 and 10 rows.

`~script1090`'s second act is:

```
def_dbrow $dbrowarray0($length9);
def_dbrow $dbrow10 = db_findnext;
while ($dbrow10 ! null) {
    if ($dbrowarray0($length8) = null) { $dbrowarray0($length8) = $dbrow10; … }
    else { ~script4078("ERROR: Multiple overlapping reward ids …"); return; }
}
```

`CS2VM2_Op_DefineArray` did `memset(&array->cells, 0, …)`. Cell 0 therefore
compared unequal to `null`, the script took the "impossible" branch on iteration
zero and returned, and three tabs of a correctly-mounted panel drew nothing —
with nothing logged, because the failure is a *successful* early return.

-1 is `null` for every reference-typed RuneScript base type, and 0 is a
perfectly good dbrow / component / obj id, so the guard is only meaningful under
-1. Fixed in `CS2VM2_Op_DefineArray`; string arrays are left NULL because no
script in this cache reads an unwritten string cell and there is nothing here to
verify a change against.

The operand's low half is the element type char: `i` 105, `s` 115, and **`Ð` 208
for dbrow** — which is what `is_string` is tested against and why this array
lived on the int stack all along.

### 3.2 `event_com` on a dynamic child was the child, not its parent

Symptom: buy an unlock, the tile flips to a green tick and the chat says "You
have 990 points left", and the header keeps saying **Reward points: 1,000**.

The header's repaint is `cc_setonvartransmit("script409(event_com,
event_comsubid){var661}")`, and script409 is two lines:

```
[clientscript,script409](component $com, int $sub)
if (cc_find($com, $sub) = ^true) { ~script410; sound_synth(synth_73); }
```

`~script410` is a bare `cc_settext(…)` on whatever `cc_find` just made active. A
trace showed the hook firing, the varp arriving with the right value, and
`cc_find: parent=0x1aa9871 sub=10 -> 0` — the script asking a *leaf* for a child
it does not have.

`event_com` is a component's **address**, not its runtime identity: for a dynamic
child that is the parent's packed `(interface << 16) | child`, with
`event_comsubid` carrying the index within it. It is the same `(container, sub)`
pair `app_if_button_target` already puts on the wire for IF_BUTTON, and for the
same reason — a dynamic child's own component id is a runtime allocation nothing
outside the process has a name for.

**This was general, not slayer's.** `script85` — `cc_setonmouseover("script85(event_com,
.cc_getid, 16777215)")` — is every hover highlight in the game and is unreadable
under any other convention, as is `script412`, which passes `event_com` beside a
*sibling's* `cc_getid`. Fixed in `task_cs2_set_int_local`'s
`CS2VM_SCRIPT_ARG_WIDGET_ID` case, which now resolves a dynamic component to its
parent.

### 3.3 A varp write is a packet, not a dirty flag

Symptom: the Tasks tab draws "(100 points)" where Turael's block price is 40, and
"View List" opens an empty window.

`[proc,slayer_rewards_open]` is two statements — write the master varbit, mount
the panel — and 426's onload chain reads that varbit four times as it lays itself
out (`~script823` block price, `~script824` block eligibility, `~script8025`
block slots, `[clientscript,script8061]`'s `db_find` over the master's task
table). This server queued varps into `player->varp_changed[]` and encoded them
in phase 10, so the mount went out first and the panel drew the default master's
prices with every packet on the wire present and correct.

The reference does not batch (`Player.ts:1763`):

```ts
setVar(id, value) { … this.vars[varp.id] = value;
                        if (varp.transmit) this.writeVarp(id, value); }
```

`ifOpenSub` writes immediately too, so **a script's source order is the packet
order**, and content relies on it. `mock230_world_mark_varp` encodes the packet
now; the fixed 64-entry change list and its silent drop-past-the-end are gone
with it. What batching bought was a dedupe — ten varbit writes into one varp used
to queue it ten times — and sending ten packets is what the reference does, at
six bytes each.

Nothing downstream could have repaired this: the Tasks tab's own
`if_setonvartransmit` does not list that varp, so a late write does not even
repaint. The panel is built once, from whatever the client knew at mount.

---

## 4. What is content

All of it. `server/scripts/interface_slayer/`:

| file | what |
|---|---|
| `configs/slayer_rewards.constant` | the action sub-id space (§1.1), the extend-all discount, the master numbering, the clientscript id |
| `configs/slayer_unlock.enum` | **generated** — bit → cost / list / refundable, transcribed from dbtable 117 (§6) |
| `configs/gen_slayer_unlock.py` | the generator, so the transcription can be re-derived rather than audited |
| `configs/slayer_rewards.varp` | the three ownership varps `wholewrite=allow transmit perm`, plus varp 661 and varp 4844 |
| `configs/slayer_rewards.npc` | `wanderrange=0` — a shopkeeper-shaped npc that walks off is a test that fails for a reason no screenshot explains |
| `configs/slayer_rewards.spawn` | Turael, marked as a test-world placement |
| `scripts/slayer_rewards.rs2` | ownership, the transactions, the two opens, the arming, `::slayerpoints` |

Plus one line in `player/login.rs2` (`~slayer_rewards_login;`).

### 4.1 Why `wholewrite=allow` on three varps

`sscompile` refuses a whole-varp write to a varp that has varbits based on it,
because at rev 230 a 2004-era varp is usually a bit range and the wrong
granularity compiles, runs, transmits and corrupts
(`LOSTCITY_PORT_TRIAGE.md` §7.5). These three are the case the exemption exists
for, and the claim is measured rather than judged: 67 single-bit varbits,
contiguous, no foreign tenant (§1.3). The client does not read them as varbits
either — `~script8048` reads the raw word — so a whole-varp write is the write
that read is for. The alternative, a 67-case switch mapping bit index to varbit
name, would restate the one number the wire already carries.

### 4.2 What this slice does not do

Cancel (67), Block (68), Store/Swap (76), Unstore (77) and the seven Unblock
slots (69..74, 78) all act on a **task**, and this world has no task assigner:
`%var261..266` — the cache's generically-named `if1..if6`, repurposed as the task
struct — are zero and nothing writes them.

That is a shape rather than a hole, because the client already handles it.
Clientscript 422 calls `cc_setop`/`cc_setonop` on Cancel and Block only when
there *is* a task, and clientscript 426 arms Unblock only when the slot holds
one. With no task the Tasks tab draws "Current assignment: None", "No stored
task" and seven "You need N QP to use this slot", every button on it is grey, and
none of those sub ids can be sent at all. `~slayer_confirm` names them anyway, so
the day a task assigner lands the only edit is the body of one branch.

The **Buy** tab (pouches) is likewise untouched: it is a fixed CS2 enum with no
ownership state, and its rows are `cc_setonop`-only in this cache.

---

## 5. Verified in the client

Headless, embedded server, real clicks. BMPs read, not tree dumps.

```
SDL_VIDEODRIVER=dummy MOCK230_SAVES=$S/saves TORIRS_MAX_FRAMES=900 \
TORIRS_NET_CHEAT="slayerpoints 5000;talk slayer_master_1_tureal 5" \
TORIRS_SIM_CLICK_AT="500,278,88;600,294,259" \
TORIRS_EXIT_BMP=$S/buy.bmp MOCK230_VERBOSE=1 \
  ./src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test
```

- Right-click Turael → the cache's five ops; **"Rewards"** mounts 426 with the
  tab strip (Unlock / Extend / Buy / Tasks / Cosmetics), the title and
  "Reward points: 5,000".
- The Unlock tab draws all 24 tiles with their icons, descriptions and prices —
  `Gargoyle Smasher (120 points)`, `Slug Salter (10 points)`, `Malevolent
  Masquerade (400 points)` — which is dbtable 117 read by the client agreeing
  with content constants that never met it.
- Click Slug Salter's padlock → the confirm dialog: *"Slug Salter — Automatically
  salt Rockslugs… Pay 10 points?"* with Back / Confirm.
- Click **Confirm** → `mock230: <- IF_BUTTON1 426:10 sub=1`, the padlock becomes
  a green tick, the header reads **4,990**, the chat reads "You unlock that
  Slayer reward. You have 4990 points left." (Slug Salter is the cache's 10, off
  the 5,000 the cheat above grants — an earlier draft of this line said 990,
  which is the balance from a 1,000-point start, not from the command printed
  here. Re-verified 2026-08-02 against the BMP.)
- Extend tab → 29 rows and **"Extend remaining 29 tasks: 2,731 points"**. Click
  it, Confirm → `IF_BUTTON1 426:10 sub=75`, every extend tile ticks, the bar
  becomes "Nothing more to extend", the header drops by 2,731. The hover text
  now offers **Disable** on the refundable ones.
- Tasks tab → "Current assignment: None", the seven block slots with their QP
  requirements, Cancel/Block/Store greyed and **View List** live.
- **View List** → `IF_BUTTON1 426:29 sub=0`, then `IF_OPENSUB` + `RUNCLIENTSCRIPT`
  (20 bytes: 3 type chars + newline + 3 ints + the script id), and 924 mounts
  into `slayer_rewards:popup` and draws its frame and title. Its rows do not
  populate — §6.
- Kill the process, start a new one, `::talk slayer_master_1_tureal 5` → 2,269
  points and Slug Salter still ticked, with no cheat run.

Screenshots at `scratchpad/ui3/{open2,dlg,buy3,ext,xall,tasks,vlist2,f_relog}.png`.

---

## 6. Still open

**924's rows do not populate, and it is a client DB bug two layers down.**
Measured, not guessed: the mount and the `RUNCLIENTSCRIPT` are correct on the
wire and asserted in the selftest, 924's chrome draws, and
`db_find(table 114, col 0, val 1)` answers with Turael's 24 tasks. Execution then
stops in `[clientscript,script9620]`:

```
Task_CS2Run: script 9620 failed at opcode 40 pc 18 (invoked as script 8059 …)
```

pc 18 is `GOSUB_WITH_PARAMS 8048`, and the operand stack is short by one. Walking
back: pc 4-7 read dbtable 113 (`slayer_task`) **column 17** — `unlock_weighting`,
declared `dbrow,int`, a 2-tuple — and pop two locals from it. That column is
*absent* from most rows, and `db_push_missing` answers an absent column with a
single integer 0, because it takes its arity from the **row**, which does not
carry a column it does not have. The arity lives in the *table*'s `columndef`,
which the client does not consult here. Two pops off one push leaves a garbage
dbrow, the guard `if ($row ! null)` passes on it, and the next `db_getfield`
finds no row, pushes nothing, and the gosub underflows.

The fix is the same shape as the param-defaults fix (`PORTING_GUIDE.md` §3.6
item 3): a missing column must answer with the table's declared `defaults=` and,
failing that, one null per declared tuple type. It needs the client to hold
dbtable column metadata at `db_getfield` time, which it does not today. **This is
the top follow-on and it is not slayer-specific** — every `db_getfield` on an
optional column in the cache is on it.

**The server cannot read a cache dbtable, so 67 prices are transcribed.**
`mock230_db.c` parses the *authored* grammar (`column=name,type` /
`data=column,value`); `configs/all.dbtable` and `configs/all.dbrow` are machine
exports in a different one (`columndef=N:name,type` / `values=N:tuple:value`).
`mock230_db_load` walks them — they are under the content dir — and skips every
line it does not recognise, so all 259 cache tables load as a name and an id with
zero columns and all 16,711 cache rows load with no data.
`docs/skill_guide.md` §7 found the same thing from the other direction; this is
the first feature that needed the rows.

Teaching the parser the export grammar is bounded but not free, and the cost was
measured before deciding against it here: 16,711 rows at ~528 bytes of row header
each is ~8.8 MB before values, 241k lines to parse at every boot, and six type
words the runtime does not resolve (`graphic`, `model`, `idkit`, `mapelement`,
`track`, `synth`) — which `mock230_db.c`'s own header says must be fixed by
naming those namespaces, *not* by widening the type list. So the economy lives in
`slayer_unlock.enum`, generated, marked, and cross-checked by §7's selftest,
which charges the cache's price for five named unlocks.

**`<u=…>` underline markup** is handled in `toridraw_font.c` (same fix as the
skill-guide Overview body). Confirm titles use `<u=ff981f>…</u>` for a colored
underline without recoloring glyphs.

**Ops 6..10 still cannot be picked** (`UITREE_MENU_OPTION_SLOTS` is 5,
`docs/farming_tools.md` §6). Slayer does not use them.

---

## 7. The permanent check

`mock230 --selftest`, section **"slayer rewards"** (`mock230_world.c`). It sends
a real `OPNPC5` at Turael and real `IF_BUTTON1`s at the confirm overlay, and
asserts:

- the master varp packet comes out **before** the mount, and the mount is
  `slayer_rewards` → `toplevel_osrs_stretch:mainmodal` type 0, by name;
- `%slayer_master_in_focus` is 1 — the cache's own numbering for Turael;
- `confirm_button` is armed for op 1 over a sub range reaching the highest action
  code, because arming slot 0 arms one of seventy-nine buttons;
- confirming sub 1 unlocks Slug Salter and charges the cache's 10 points;
- confirming it again does **not** clear it — it is not refundable, so
  clientscript 413 gives it no "Disable" op at all;
- 119 points does not buy a 120-point unlock, and does not spend;
- bit 51 lands in `slayer_rewards_unlocks1` bit 19 and bit 66 in `..._unlocks2`
  bit 2, and neither touches the first word;
- a refundable unlock disables on a second confirm and there is no refund;
- Extend-all charges exactly 2,731 (95% of 2,875, floored) and unlocks exactly
  the 29 Extend rows and none of the Unlock list, and a second press is free;
- an action sub id that needs a task costs nothing in a world with no tasks;
- "View List" mounts 924 into `slayer_rewards:popup` and runs a `"iii"`
  clientscript whose first argument is that same component;
- all three ownership words and the balance survive a save/load round trip.

Proven to fail, by mutation:

| mutation | what the selftest said |
|---|---|
| `^slayer_extend_all_percent` 95 → 100 | `"Extend remaining 29 tasks" costs 2731 points at this revision (95% of 2875, floored) — 2125 left, expected 2269` |
| drop the `bit / 32` split in `~slayer_unlock_set` | `bit 51 belongs in slayer_rewards_unlocks1 bit 19, that varp reads 0x0` (+6 more) |
| swap the two statements in `~slayer_rewards_open` | `the master varp must precede the mount (varp 20, mount 19)` |
| `mock230_world_mark_varp` returns without sending (i.e. the old phase-10 batching) | `the open should transmit slayer_misc (4844)` + `(varp -1, mount 19)` + two login-burst failures |
| `scope=perm` → `scope=temp` on `slayer_rewards_unlocks1` | `all three ownership words should survive a logout (0x2 0x0 0x4)` |

The last is the one worth keeping: two thirds of the catalogue comes back and one
third does not, which is neither right nor obviously wrong.

What it deliberately does not assert is that bit N is the unlock whose *name* the
tile carries. That ground truth is dbtable 117, which the server cannot read
(§6). §5's screenshots are the check for that — the tile that ticked said "Slug
Salter" and the price the server charged was the price the tile drew.
