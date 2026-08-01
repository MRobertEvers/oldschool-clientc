# Slayer Rewards (`slayer_rewards` 426, `slayer_rewards_task_list` 924): what the server owes

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
| mock230 | zero implementation, confirmed clean unstarted slice |
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

| state/mechanism | delivery | mock230 status |
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

## 9. Landed vs. gap in mock230

`grep -rniE "slayer" src/net/mock/ src/game/` — exactly 5 hits, all incidental: an unrelated equipment-stats "slayer bonus" combat modifier, a content-namespace-prefix string in the packer, and a doc comment about object info carrying slayer categories as a data field. **Zero implementation, zero design coverage of the reward-points/unlock/task system** — a clean unstarted slice, same class as shop/skill-guide.

## 10. LostCity precedent — confirmed absent, historically correct

`ls LostCity_Server/content/scripts | grep -i "^skill_"` lists all 15 implemented skills — **no `skill_slayer` directory exists**, confirmed directly. Every "slayer" grep hit in the content tree resolves to Dragon Slayer/Demon Slayer quest content (proper nouns, unrelated to the skill) or a tutorial NPC whose name (Vannaka) was reused from a much later slayer-master role. This is not a gap in the port — Slayer launched March 2005, after LostCity's September 2004 snapshot. Same "modern feature, no LostCity reference" class as clan chat and the skill guide — there is no `[proc,...]` to port the shape of, only the intent (points economy, per-master task pools, block/cancel costs) to reconstruct against the osrs239 dbtables and varp/varbit layout traced above.

## 11. What this doc does not cover

- `script9100`/`script9101`/`script8942` (the unlock-prerequisite chain) and 924's entry point/Block handler — present-by-reference, not read in full; re-verify before implementing.
- The exact `+16`-per-column dbtable field-constant derivation — inferred from a consistent stride across four call sites, not independently confirmed against the packer's encoding scheme.
- `slayer_task`/`slayer_area`/`slayer_task_sublist` (dbtables 113/115/116) — back task assignment/area unlocks, which live with the (currently nonexistent) slayer-master NPC scripts, out of scope here.
