# Slayer Rewards (`slayer_rewards` 426, `slayer_rewards_task_list` 924): what the server owed

> **BUILT — 2026-08-02, verified in the client:** `slayer_rewards` (426).
> `slayer_rewards_task_list` (924) mounts and draws its chrome; its rows are
> cut, for a reason that is not slayer's — see below. The case file is
> [`slayer_rewards.md`](slayer_rewards.md). The discovery pass is kept below
> as written; what landed and what it got wrong come first.

## What landed

Interface 426 opened from Turael's "Rewards" op — op 5, the cache's own label
on every slayer master — plus 924 behind its "View List". Buy, disable,
extend-everything, the points economy, persistence.

**The server needed no new opcode, no new packet and no new trigger.**
`SS_TRIGGER_IF_BUTTON1..10` and `SS_OP_RUNCLIENTSCRIPTVARARG` from stage 1 and
persistence from stage 2 were exactly the surface this wanted, and
`setbit`/`clearbit`/`testbit` were already covered. Every hour went into three
*client* bugs, none of them slayer-specific:

| bug | symptom | fix |
|---|---|---|
| `CS2VM2_Op_DefineArray` `memset`s cells to **0**, not **-1** | all three catalogue tabs drew nothing, silently. `[clientscript,script1090]` — the builder behind Unlock, Extend *and* Cosmetics — guards each slot with `if ($rows($i) = null)`; cell 0 compared unequal to null on iteration zero and took the `~error(); return` branch. `db_find` had already answered with its 24/29/10 rows | int cells initialise to **-1**, which is `null` for every reference-typed base type. Strings left NULL: no script in this cache reads an unwritten string cell, so there is nothing to verify a change against |
| `event_com` reported a dynamic child's own runtime id, not its parent's | `cc_setonvartransmit("script409(event_com, event_comsubid)…")` does `cc_find($com, $sub)` — asking a leaf for a child, `-> 0`. **General**: `script85` is every hover highlight in the game and is unreadable under any other convention | `event_com` is a component's *address*: for a dynamic child, the parent's packed id, with `event_comsubid` the index — the same `(container, sub)` pair `app_if_button_target` already puts on the wire. Fixed in `task_cs2_set_int_local` |
| varps batched to phase 10; the reference writes them in the setter | `~slayer_rewards_open` is "set the master varbit; mount the panel", and 426's onload reads that varbit four times as it lays itself out. Batched, the panel drew the *default* master's prices (100 instead of Turael's 40) and 924 came up empty — with every packet on the wire present and correct | `ToriRSServer_WorldMarkVarp` encodes immediately. `Player.ts:1763` — `setVar` calls `writeVarp` inline — so **a script's source order is the packet order**. `varp_changed[]` and `TORIRSSERVER_VARP_DIRTY_MAX` (**64**) are gone, and so is the silent drop past 64. What batching bought was a dedupe; the reference sends the duplicates |

Content — `interface_slayer/`, 890 lines, plus one line in `player/login.rs2`
and three enum ids allocated by `ss_allocate.py`:
`slayer_rewards.constant` (93), `slayer_unlock.enum` (244, **generated**),
`gen_slayer_unlock.py` (100), `slayer_rewards.varp` (82),
`slayer_rewards.npc` (23), `slayer_rewards.spawn` (19),
`slayer_rewards.rs2` (329).

Permanent check: `ToriRSServer --selftest` section **"slayer rewards"**
(`torirs_server_world.c:7917`) — real `OPNPC5`, real `IF_BUTTON1`s, and it asserts
the varp precedes the mount, that bit 51 lands in `unlocks1` bit 19 and bit 66
in `unlocks2` bit 2 with the first word untouched, that extend-all charges
exactly 2,731, and that all three words plus the balance survive a save/load.
Five mutations, five distinct failures.

## What it cost

The wire contract is stated *completely* by the cache, and reading it was the
cheap part: every transaction funnels through one component,
`slayer_rewards:confirm_button` (426:10), as a single `cc_create`d overlay
whose **sub id is the action** — 0..66 the unlock bit, `calc(66 + N)` for the
ten task verbs. One `[if_button1,…]` trigger; `last_slot` is the verb. Slot
6's unblock is `66+12` while everything else stops at `66+11`, which is the
only reason the armed range has to reach **78**.

The `cc_setonop` beside those ops is clientscript 319, whose whole body is
`if_settext("Requesting...")` plus a 45-tick `if_setontimer`. **A client that
draws a timeout for an answer is a client waiting for a server** — the
discovery pass read that as UI polish and drew the opposite conclusion.

## What was deliberately left

- **924's rows.** The mount and the clientscript are right and asserted, the
  chrome and title draw, and `db_find(114, col 0, val 1)` returns Turael's 24
  tasks — then `script 9620 failed at opcode 40 pc 18`, a `GOSUB_WITH_PARAMS`
  one int short. Root cause traced and **not slayer-specific**:
  `[clientscript,script9620]` reads dbtable 113 column 17 (`unlock_weighting`,
  declared `dbrow,int` — a 2-tuple) and pops **two**; that column is absent
  from most rows, and `db_push_missing` answers an absent column with **one**
  integer 0 because it takes the arity from the *row*, which does not carry a
  column it does not have. The arity lives in the table's `columndef`, which
  the client does not consult at `db_getfield` time. Same shape as the
  param-defaults fix (`PORTING_GUIDE.md` §3.6 item 3), and **every
  `db_getfield` on an optional column is on it**. Top follow-on.
- **Cancel/Block/Store/Swap/Unstore/Unblock ×7.** They act on a task and
  nothing assigns one. Not a hole: clientscript 422 arms Cancel/Block only when
  a task exists and 426 arms Unblock only when a slot holds one, so those sub
  ids cannot be sent. `~slayer_confirm` names them so the day a task assigner
  lands, the edit is one branch body.
- **The Buy tab** (pouches) — a fixed CS2 enum, `cc_setonop`-only, no
  ownership state.
- **The cache-dbtable parser.** Measured before deciding: 16,711 rows at ~528 B
  of row header is ~8.8 MB before values, 241k lines per boot, and six type
  words (`graphic model idkit mapelement track synth`) the runtime does not
  resolve — which `torirs_server_db.c`'s own header says must be fixed by *naming
  those namespaces*, not by widening the type list. So the 67 prices are a
  generated transcription, marked, with the generator checked in, and the
  selftest charges the cache's price for five named unlocks as the
  cross-check.
- Cosmetic and general, not slayer's: confirm titles use `<u=ff981f>…</u>`
  (colored underline); handled in `toridraw_font.c` with the skill-guide fix.
- Untouched on purpose: `container_for`, `POP_VAR`/`POP_VARBIT`,
  `CS2VM2_OPCODE_STACK_MAX`.

## What the discovery pass got wrong

> The body below is kept as written. Its shape is right — five tabs over one
> confirm subtree, a dbtable catalogue, a bitfield of ownership, and a points
> balance — and its "confirmed absent from LostCity, historically correct"
> conclusion is right too. **Seven things in it are wrong, and the headline is
> one of them.**
>
> 1. **§2's "a real collision, confirmed" is fabricated.** The claim is that
>    varbit `slayer_unlock_storage` (12442) shares bit 19 of
>    `slayer_rewards_unlocks1` with the ownership bitfield, so an ownership
>    read-modify-write would clobber an unrelated feature. Decomposing
>    `configs/all.varbit` against the three basevars gives **67 single-bit
>    varbits, contiguous 0..66, no gaps and no foreign tenant** — 32 + 32 + 3,
>    matching dbtable 117 row for row. Bit 51 **is** `slayer_unlock_storage`, it
>    is one of the 67, and it means exactly one thing. This is what let the
>    whole feature use `wholewrite=allow` on the three varps, which is also what
>    the client does (`~script8048` reads the raw word).
>
> 2. **§2's "three unlocks (bits 35/43/53) bypass the bitfield entirely" is
>    wrong.** `[clientscript,script413]` reaches those three cases only *after*
>    `~script8048($bit) = 1`. They are ownership bits with an extra toggle on
>    top, not a parallel mechanism.
>
> 3. **§5's "the exact op/sub-id contract is a corpus gap, not something to
>    guess" is exactly backwards — the cache states it completely.** Every
>    transaction the panel offers is confirmed by one `cc_create`d overlay on
>    `slayer_rewards:confirm_button` (426:10) whose **sub id is the action**:
>    0..66 the unlock bit (clientscript 414), and `calc(66 + N)` for the ten
>    task verbs (clientscripts 423 and 427). The full table is in
>    `slayer_rewards.md` §1.1. Reading the 45-tick timeout as "UI polish" and
>    the fresh `cc_setonop` as "no visible op index" missed both halves of a
>    protocol that is entirely spelled out.
>
> 4. **§0/§8's "torirsserver: zero implementation… clean unstarted slice" was right
>    about the server and wrong about the work.** The server needed **no new
>    opcode, no new packet and no new trigger** — everything it owes was already
>    expressible. Every hour went into three *client* bugs, none of them
>    slayer-specific: zero-initialised CS2 arrays (which blanked all three
>    catalogue tabs), `event_com` reporting a dynamic child instead of its
>    parent (which killed every `cc_find(event_com, event_comsubid)` repaint in
>    the cache), and varps batched to end-of-tick instead of written at the
>    setter (which mounted the panel before it knew which master it was for).
>    See `slayer_rewards.md` §3.
>
> 5. **§6's "no onload anywhere" for 924 is right but incomplete, and the entry
>    point is not a corpus gap.** `[clientscript,script8059]` decompiles, takes
>    three ints, and has **no caller in any of the 9,433 scripts in this cache**
>    — which is the cache saying the server runs it. Its first argument is the
>    component the server mounted 924 into (8059 → `~script4206` → `~script612`,
>    which hangs `if_setonsubchange` on it), and that component is
>    `slayer_rewards:popup`, 426:4, which 426 declares for exactly this.
>    `script8065` is not a "missing body" either: it decompiles to one line,
>    `sound_synth(synth_2266, 1, 0)`.
>
> 6. **§8's "Unlock catalogue (dbtable 117) — landed mechanically, no server
>    code needed" is wrong in a way that matters.** The *client* reads it out of
>    the cache, which is why the tiles draw. The **server** cannot read a cache
>    dbtable at all: `torirs_server_db.c` parses `column=`/`data=` and
>    `configs/all.dbtable`/`all.dbrow` are machine exports in a
>    `columndef=`/`values=` grammar it walks and silently skips, so all 259
>    tables load as an id with zero columns. The 67 costs are therefore
>    transcribed into a generated content enum. `slayer_rewards.md` §6 has the
>    measurement and the reason the parser was not widened here.
>
> 7. **§9's "grep -rniE slayer src/torirsserver/ src/game/ — exactly 5 hits"
>    describes a search, not a state.** Nothing in this feature put the word
>    "slayer" into `src/` and nothing needed to: the whole of it is content plus
>    three general client fixes. A grep for a feature's *name* in the engine is
>    not a measure of whether the engine can express it.
>
> 8. **§11's "+16-per-column field-constant derivation, inferred" now has a
>    measurement, and it is off by one.** The encoding is
>    `(table << 12) | (column << 4) | tuple`, and the low nibble is
>    **one-based** — 0 means the whole tuple — which `rs_cs2_host.c` already
>    documents. So `479329` is table 117, column 6, *tuple 0*, not tuple 1:
>    the difference between "find by list" (24/29/10 rows) and "find by
>    position" (3 rows).
>
> Also minor: §1's caution about `slayer_killerwatt_var` reading "like an
> unrelated leftover" is worth keeping — it does, and it has four other tenants,
> so declaring it `scope=perm` persists them too (correctly: a varp is the unit
> the save format stores). §4's `if1..if6` observation is accurate and is why
> the task half of this panel is out of scope until something assigns a task.

---

# The discovery pass, as written

> Companion to `docs/questlist_chatmenu_levelup.md` and
> `docs/skill_guide_server_reqs.md`, same discovery pass. Slayer as a
> trainable skill (task assignment, reward points, Turael onward) launched
> March 2005 — **after** LostCity's frozen rev-254 (September 2004)
> snapshot. Confirmed below: LostCity has no Slayer skill at all, not a
> stale-reference gap.

## 0. Status at a glance

| aspect | finding |
|---|---|
| interface shape | 5 tabs (Unlock/Extend/Buy/Tasks/Cosmetics) sharing one confirm-dialog subtree, plus a separate popup (924) for a slayer master's weighted task list |
| points balance | one varbit (`slayer_points`, 4068) backed by varp `slayer_killerwatt_var` (661) — same `%qp`-shaped idiom as `questlist` |
| purchasable unlocks | dbtable 117 `slayer_unlock` (67 rows) + a 96-bit ownership bitfield across 3 varps — same *shape* as `skill_guide_v2`'s dbtable+varbit pattern, but ownership is a raw bitfield, not one varbit per unlock |
| "Buy" tab (pouches) | **not** dbtable-driven — a fixed CS2 enum table, no ownership state at all |
| current task | 6 generically-named scratch varps (`if1..if6`, repurposed) holding creature id + qty for active and stored task |
| corpus gaps | the real buy/unlock/cancel round-trip, "View List"'s open path into 924, and 924's own entry point are all missing |
| ToriRSServer | zero implementation, confirmed clean unstarted slice |
| LostCity precedent | **confirmed absent** — no `skill_slayer` directory anywhere in the reference tree |

---

## 1. Points balance — the `%qp`-equivalent

```
[proc,slayer_rewards_setpoints]
cc_settext("Reward points: <tostring_spacer(%varbit4068,",")>")
```
**Varbit 4068 = `slayer_points`**, packed into **varp 661 = `slayer_killerwatt_var`** (both confirmed by name). The backing varp's name reads like an unrelated leftover — flag before writing the `.varp`/`.varbit` overlay, same caution as shop's `bank_closing` collision (`docs/shop_server_reqs.md` §1.1).

`slayer_tasks_completed` (varbit 4069, confirmed present, own dedicated backing varp) is declared but **not read anywhere in this interface's traced CS2** — the streak/lifetime counter exists as config but this panel doesn't display it; likely surfaced elsewhere (a task-completion chat message).

## 2. Unlock / Extend / Cosmetics tabs — dbtable + bitfield

All three tabs share one builder proc keyed by a list argument (0/1/2), reading **dbtable 117 `slayer_unlock`** (confirmed present, 67 rows: bit/cost/icon/name/description/refundable/list_position/related_task) filtered by the `list_position` column.

**Ownership** is a raw 96-bit flag split across three 32-bit varps — confirmed by name: `slayer_rewards_unlocks` (var 1076), `slayer_rewards_unlocks1` (1344), `slayer_rewards_unlocks2` (5587) — indexed by the dbtable's own `bit` column via `testbit(flags, bit % 32)`. This is config-driven ownership, just packed as a bitfield rather than one-varbit-per-unlock.

**A real collision, confirmed**: varbit `slayer_unlock_storage` (12442, gates the Store/Swap/Unstore feature) is packed at `basevar=slayer_rewards_unlocks1, startbit=19, endbit=19` — **the exact same 32-bit varp used as the ownership bitfield for bits 32-63**. Bit 19 of that word means two different things depending on which system reads it. Any read-modify-write on `slayer_rewards_unlocks1` for ownership purposes must not clobber this unrelated bit — same collision class as shop's `bank_closing`/`shop_quantity` (`docs/shop_server_reqs.md` §1.1).

**Availability vs. ownership are distinct checks**: a prerequisite chain (`~script9102`→`script9100`/`script9101`/`script8942`, not fully traced past this depth) plus a Leagues-availability varbit determine "(Unavailable)" vs "(N points)" — separate from whether it's already owned.

Three unlocks (bits 35/43/53) bypass the bitfield entirely, backed by their own dedicated toggle varbits instead.

## 3. "Buy" tab — no dbtable, no ownership state

Unlike the other three tabs, this one walks a **fixed CS2 enum** (item, price, description), not a dbtable. The server obligation here is just "spend N points, give the obj" — no ownership/prerequisite state to track at all.

## 4. Current task state — flat, repurposed scratch varps

Active task (creature/category id + quantity) and a stored task (Store/Swap/Unstore, gated behind `slayer_unlock_storage`) both live in **6 varps declared generically as `if1..if6`** in config — a naming collision worth flagging exactly like `bank_closing`: these are generic scratch varps repurposed as the entire task-state model, with no dedicated names of their own.

**Active slayer master** is a separate varbit, `slayer_master_in_focus` (17868, confirmed), which scales Cancel/Block cost by tier (Cancel is a flat 30 points; Block is 40-100 depending on master).

**Blocked-task slots** (7 fixed + 1 diary slot): dedicated varbits per slot (cleanly named, no collision) plus per-slot storage varps. Slot-unlock gating uses **`%var101` (`qp`) for slots 1-6 — the same quest-points varp `questlist` already needs**, and a diary-completion varbit for the 8th slot.

## 5. Confirm/round-trip — UI polish only, real transaction unconfirmed

The confirm dialog's Confirm button is bound to a fresh `cc_setonop` per open with no visible op index in this corpus, and after clicking, the client just arms a client-side 45-tick timeout that reverts the panel regardless of outcome. **No CS2 anywhere sends a request or awaits a reply** — the actual point-spend/ownership-toggle must happen through a plain server-interpreted button click, with the result reaching the client only via the already-wired `if_setonvartransmit` hooks. This is the same shape as shop's buy-op ambiguity (`docs/shop_server_reqs.md` §3), and the exact op/sub-id contract is a corpus gap, not something to guess.

## 6. `slayer_rewards_task_list` (924) — corpus gap, structurally

No onload anywhere; it's the same generic small-popup-overlay template shared by a dozen unrelated interfaces. What populates it (found by grepping the interface id directly, not the `.if`) has its entry point (`script8061`, called by `script_8059`) and its per-row Block handler (`script8065`) **both missing bodies from this corpus** — same class of gap as `~questlist_draw`. The row data lines up with **dbtable 114 `slayer_master_task`**'s weight/min/max columns — this is "possible tasks and their odds for your current master," toggle-blockable per row, distinct from the 7 fixed blocked slots in 426.

## 7. Task assignment — absent from the corpus entirely, not merely unported

No `[opnpc*,turael]`/`vannaka`/`duradel`-style script exists anywhere. The only "slayer"-substring hits among 141 files are an unrelated dialogue-widget family (`meslayer_*`, already covered in `docs/friends_pm_chat_server_reqs.md`) and Dragon Slayer/Demon Slayer **quest** content — false positives, not slayer-master content. There is nothing here to port for task assignment itself.

## 8. Server obligations

| state/mechanism | delivery | ToriRSServer status |
|---|---|---|
| Reward points (`slayer_points`/`slayer_killerwatt_var`) | varp/varbit transmit, `%qp`-equivalent idiom | **not declared** |
| Unlock catalogue (dbtable 117) | generic dbtable load | **landed mechanically**, no server code needed for the catalogue itself |
| Unlock ownership (96-bit bitfield across 3 varps) | varp transmit | **not declared**; must read-modify-write around the bit-19 collision |
| Unlock prerequisite/availability chain | proc chain, partially traced | **not fully traced** past `script9100`/`9101`/`8942` — flag before implementing |
| "Buy" tab (pouches) | plain points-spend + give-obj | **not implemented**, simplest of the four tabs |
| Purchase/unlock confirm round-trip | server-interpreted click, op/sub-id unconfirmed | **not implemented**, corpus gap on the exact contract |
| Current/stored task state (6 repurposed varps) | varp transmit | **not declared** — no dedicated task struct exists, it's flat varps |
| Active slayer master | varbit transmit | **not declared** |
| Blocked-task slots + gating | varbit/varp transmit | **not declared**; slot 1-6 gating shares `qp` with `questlist` |
| Task-list popup (924) | inferred `runclientscript_ss`-style populate | **entirely missing** — entry point and per-row handler are corpus gaps |
| Task assignment from an NPC | — | **absent from the corpus entirely** |

## 9. Landed vs. gap in ToriRSServer

`grep -rniE "slayer" src/torirsserver/ src/game/` — exactly 5 hits, all incidental: an unrelated equipment-stats "slayer bonus" combat modifier, a content-namespace-prefix string in the packer, and a doc comment about object info carrying slayer categories as a data field. **Zero implementation, zero design coverage of the reward-points/unlock/task system** — a clean unstarted slice, same class as shop/skill-guide.

## 10. LostCity precedent — confirmed absent, historically correct

`ls LostCity_Server/content/scripts | grep -i "^skill_"` lists all 15 implemented skills — **no `skill_slayer` directory exists**, confirmed directly. Every "slayer" grep hit in the content tree resolves to Dragon Slayer/Demon Slayer quest content (proper nouns, unrelated to the skill) or a tutorial NPC whose name (Vannaka) was reused from a much later slayer-master role. This is not a gap in the port — Slayer launched March 2005, after LostCity's September 2004 snapshot. Same "modern feature, no LostCity reference" class as clan chat and the skill guide — there is no `[proc,...]` to port the shape of, only the intent (points economy, per-master task pools, block/cancel costs) to reconstruct against the osrs239 dbtables and varp/varbit layout traced above.

## 11. What this doc does not cover

- `script9100`/`script9101`/`script8942` (the unlock-prerequisite chain) and 924's entry point/Block handler — present-by-reference, not read in full; re-verify before implementing.
- The exact `+16`-per-column dbtable field-constant derivation — inferred from a consistent stride across four call sites, not independently confirmed against the packer's encoding scheme.
- `slayer_task`/`slayer_area`/`slayer_task_sublist` (dbtables 113/115/116) — back task assignment/area unlocks, which live with the (currently nonexistent) slayer-master NPC scripts, out of scope here.
