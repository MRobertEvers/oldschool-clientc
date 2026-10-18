# CoX supplies: the solo prep plan the raid bot follows

Plan only (phase 4); no solver code yet. Content facts and their sources are in
[`supplies_spec.md`](supplies_spec.md). This file is how a strong solo human
preps, turned into a RESOURCE-layer plan (`CONTENT_SOLVER_MANUAL.md` Part 2,
Phase 6: "RESOURCE: stock / need / reserve of supplies across rooms") for
`cox2_raid.lua` / the shared CoX layer (F4).

## 1. What a solo needs at the Great Olm

| source | what it says |
|---|---|
| wiki Strategies (Great Olm) | "each player should have at least one overload, one prayer enhance, 2-3 restores and 6-7 brews"; one stamina for a solo |
| TheEdB0ys 1:04:26 | per person "eight juice, four shrooms and two sicily"; "10 to 15 buchu per person"; each overload is 3 golpar + 1 noxifer |
| yt_0a61znGm0pc (team guide) | "about 10 brews per person, four to five restores, one overload and one prayer enhance" |
| Beleti 0:10:38 (solo **CM**) | "13 endarkened juice and eight mushrooms" -> 13 brews, 8 restores; plants buchu in both patches |
| Synq 0:01:14..0:12:44 | 78 Herblore for (+) brews/restores/enhances, 90 for overload (+); "Muttadiles, Tekton, Vasa, Vespula and Vanguards all drop overloads"; enhance "always paired with the overload"; re-brew inside the 15 s overload window |
| Tubby 0:35:44 | a learner's first Olm: "two whole inventories of brews and restores" |

**The bot's target (normal mode, solo, maxed, the committed kit):** at the Olm door
carry **1 overload (+) (4 doses) + 1 spare dose, 1 prayer enhance (+), 3 revitalisation
(+), 8 Xeric's aid (+)**. That is the wiki minimum with the brew count raised to the
two guides' 8-10 for a bot that has not yet earned "fewer brews". It is a number the
budget layer can lower per seed once the tick logs show what Olm really costs
(`hp_worst` per room, `tob2_budget.lua`).

Overloads come from bosses first (every layout has at least one of
Tekton/Vasa/Vespula/Muttadiles/Vanguards in most rotations): brew one only when the
layout's combat rooms drop none before Olm.

## 2. The prep, room by room

```
floor 1:  [scavenger room]  kill scavs until secondaries >= need; take 2 planks
          [combat rooms]    pick up every potion and seed dropped
          [resource room]   tools, plant, gourds, fill, (scav top-up), harvest, brew
          [floor end]       rake weeds only if seeds are short; energy well
floor 2:  [resource room]   second prep only if the budget says so (after the floor's
                            fights), else pass through; energy well
          [floor end]       energy well -> Olm
```

Needs, computed once at the first scavenger room from the layout (every room is
known: `::coxlayout` is a cheat, the bot reads the rooms it walks into) and the
target above:

| need | formula (solo) |
|---|---|
| endarkened juice | aids_needed (8) - aids dropped by bosses ahead (Vasa 2, Vespula 2, big Muttadile 1, Vanguards 4) ; + 1 per overload to brew (kodai) |
| stinkhorn | revits_needed (3) - revits ahead (Tekton 1, Vespula 1, big Mutt 1, Vanguards 3) ; + 1 per overload (elder) |
| cicely | enhances_needed (1) - enhances ahead (Tekton 1, Vespula 1, small Mutt 1, Vanguards 1) ; + 1 per overload (twisted) |
| buchu seeds | ceil((aids + revits + enhances to brew) / 12) -- one patch gave 12 at 99 Farming in the probe; two patches cover the worst case |
| golpar / noxifer seeds | 1 each per overload to brew (one patch of golpar gives the three) |
| water vials | potions to brew |

A scavenger kill gives the secondary bundle with p = 1 - (12/18)^2 = 5/9 (two rolls of
6/18); one bundle averages 9.5 juice, 7 stinkhorn, 4.5 cicely, so **two bundles cover
any solo target** -- expected ~4 kills in a 1-scav solo room (respawn 2 ticks, near
the raider).

### Resource-room sequence (both rooms have the same furniture)

1. `world_op` Old tools op1 (Take): rake, spade, seed dibber.
2. `world_use` each seed on an empty patch (dibber in pack). Buchu first, then
   golpar/noxifer if an overload is to be brewed. Growth is 50 ticks.
3. Gourd tree op2 (Pick-lots), then `world_use` empty gourd on the geyser (or op1 Use):
   keep exactly `potions_to_brew` filled vials, drop the rest.
4. While the herbs grow: energy well; top up secondaries in the scavenger room only if
   it is adjacent and short; else wait (the guides do not leave the room).
5. Harvest: patch op1 (Pick) until the patch empties (watch the patch loc change back
   to `raids_patch_empty`); clean every grimy herb (inv op1).
6. Brew: `inv_use_on` herb on water vial; when the make menu (interface `skillmulti`)
   opens, `if_click` the product cell with the quantity sub-id (scriptrun's
   `RESUME_PAUSEBUTTON` path). Overload: `inv_use_on` noxifer on the 4-dose elder.
7. Drop surplus gourds/secondaries/tools before the next fight (raid items are lost on
   death anyway).

Floor end: weeds `world_op` op1 only when seeds are short (each rake 1-3 of a rotating
seed every 3 ticks; stop by clicking away once the count is met).

Large scavenger room shortcut: take it only if the level check passes with the
current (boosted) stat and the tool is carried (pickaxe/axe from the kit or scav
drops) -- worth level x 5 points. Never spec for it (Synq).

## 3. Perception: what the bot reads (all client-sent)

| fact | verb |
|---|---|
| backpack counts (seeds, herbs, vials, secondaries, potions by tier) | `inv_count` / `inv_slot` |
| ground drops (secondaries with quantities, planks, potions, bones) | `objs` |
| patch stage, tools, gourd tree, geyser, wells, storage tier, Bubbles, shortcut kind and cleared form | `locs`, `loc_variants` |
| scavengers, bats, the cave snake rising | `npcs`, `npc_record` |
| "This seems like a very bad moment to fish here.", "The herb patch is now empty.", "You need ..." refusals | `messages` |
| Herblore/Farming/Fishing/Hunter/Construction levels, current boosted stats | `skill` |
| the make menu | `modal_group`, `widget_find_op`, `if_click` |
| run energy | `run_energy` |

Not perceivable, by design (as in OSRS): a patch's remaining lives, which Bubbles hides
the snake, the shortcut's rolled level (the refusal message names it).

**Missing `api_drive` verbs: none for this plan** -- `world_use` (item on loc),
`inv_use_on` (item on item), `world_op` (loc op1..op5, obj Take) and `if_click` with a
quantity sub-id exist on both lanes. When the storage units' Shared/Private stores are
implemented, the bot needs their interface rows (`widget_find_op` + `if_click`), which
also exist; nothing new.

## 4. PASS criteria (written before any sweep)

For `test/raids/cox_supplies.lua` (one floor: first scavenger room -> resource room ->
floor end), solo, the committed kit, scriptrun, 48 seeds:

1. **Stock:** at the floor end the pack holds >= the target (section 1) minus what
   the layout's remaining bosses drop, every potion at the (+) tier.
2. **Time:** prep (first scav kill to last brew) <= 400 ticks (4 min) median, <= 600
   max -- the guides' prep is "quick and clean" (Beleti) and a 50-tick grow bounds it.
3. **No waste:** no seed planted that the plan did not need; no Clear; no secondary
   picked up past need + 1 bundle.
4. **Safety:** 0 deaths; damage taken from scavengers <= 30 hp total; 0 cave-snake
   bites (a bite means the bot fished, which the plan never does).
5. **Server log:** 0 `abort`, 0 `no primary player bound`, 0 `CONTENT CONTRACT`.
6. **Lane parity:** one live seed whose tick log matches scriptrun's.

Then the raid test (F5) consumes this as its RESOURCE layer: green there is the Olm
kill with the supplies this plan made.

## 5. Round 2: the solver as built (2026-10-18)

`script/plugins/quest_driver/cox2_supplies.lua` (`QD.SUP`):

| verb | what it does |
|---|---|
| `t.raid.cox_scavengers_solve{ want, max_ticks, cx, cz }` | large room: the shortcut first (rocks need a pickaxe, the sapling an axe; the boulder Strength). Then, each step: heal below 45 (Xeric's aid), take the nearest wanted ground drop (secondaries, planks, a shortcut tool), else attack the nearest live scavenger (op 2, one packet, re-pressed every 8 ticks) until its bar reads 0. A drop that cannot be reached is not tried again. |
| `t.raid.cox_resource_solve{ make, food, max_ticks, cx, cz }` | a reactive task machine, one decision per step from the backpack and the room's locs: brew (herb on water vial, the `skillmulti` cell answered) > clean a needed grimy herb > take a seed dibber / spade (Old tools op 4 / 3) > plant the most-needed seed (seed used on the patch) > fill empties at the geyser > pick gourds up to what is left to brew, keeping 4 slots > pick a grown patch (the server picks until it is spent; overflow lands on the floor) > take a needed herb off the floor > food while waiting (A: a non-snake Bubbles spot, a refused spot skipped 50 ticks; B: wield the net, catch a bat, re-wield the weapon). |
| `t.raid.cox_weeds_solve{ want }` | a floor room's weeds: rake until the seeds are carried, then a walk order to stop. |

Not a `QD.T2` room: prep has nothing to dodge, and its acts (loc ops, item on loc,
item on item, ground takes, the make menu) are channels the T2 executive does not
have. It is one-packet ops (`api_drive.world_op`, the ToB idiom) plus the backpack
verbs, all client-sent perception.

Tests: `test/raids/cox_scavengers.lua` (n even: small room, odd: large;
`::coxtest <room> n/2`) and `test/raids/cox_resource.lua` (n even: Resources A, odd:
B). GREEN as written in section 4, with the targets the plan names.

### Results (scriptrun, 2026-10-18, private build `build_cox_supplies`)

| test | seeds | pass | ticks median / max | notes |
|---|---|---|---|---|
| `cox_scavengers` | sc0..sc47 (24 small, 24 large) | **48/48** solve + alive + room clear | 104 / 296 | 0 aborts; 24/24 large-room shortcuts cleared (rocks, roots, boulder) with the test's pickaxe + axe |
| `cox_resource` | rs0..rs47 (24 A, 24 B) | **48/48** | 446 / 502 | 8 aid (+), 3 revit (+), 1 enhance (+) and 1 raw fish/bat every seed; 0 aborts |

(The 48-seed room-clear column was first read with a check that counted the last
kill's falling corpse, bar 0/30, as standing; every one of the 48 "misses" was that
row; the corrected check passed 8/8 on re-run.)

Failures found on the way, all fixed in content or solver: beasts placed inside
pillars at raid build (no collision scene for far rooms), a beast placed in a walled
pocket (search from a random tile), a beast that died over the room's edge
respawning next door, an unreachable drop retried for 400 ticks, a bat never caught
(the net must be wielded), and npc rows with no health bar (-1) read as dead.

Whole-raid notes: the solvers need no coordinates (they find the room they stand in
or the one ahead) and carry no state. With the Olm kit alone there is no pickaxe or
axe: the large room's rocks/sapling shortcut is skipped unless a scavenger drops an
iron one while the plan is still short (the boulder needs only Strength). The relay
should not expect `food` without a rod/worms or a net. A beast respawns 2 ticks after
each kill near the raider, at least 5 tiles inside the room.

## 6. Round 3: inside the whole raid (2026-10-18)

- **Needs are the plan minus stock.** `QD.SUP.todo` counts every carried potion of a
  family (any tier, any dose); a scavenger room loots the secondaries for what is
  still short (`QD.SUP.secondaries_wanted`), planks only when asked.
- **Scavengers stop** when the needs are met, when nothing more fits (a stack needs a
  slot unless already carried; only spent empty vials/gourds are dropped for room,
  never kit), at 30 kills, or after 12 kills with no needed drop. Then the beasts
  still standing die (at most 3 more) and it answers "ok".
- **Resource stops** when nothing more can be made here (no secondary, or no herb,
  seed, growing patch or floor herb for it) or nothing has changed for 150 ticks,
  answering "ok"; met first in the raid with no seeds it answers at once.
- Tests gained the whole raid's kit and call (n/2 odd), with the relay's potions
  (n/4 even) and without (odd): scavengers 48/48 (median 56 ticks), resource 48/48
  (raid-kit variants answer "ok" in 0 ticks; light-kit seeds brew 12 every time).

## 7. Round 3b: the raid's own potions, made in the raid (2026-10-18)

The relay copy `test/raids/cox_raid_solo_prep.lua` (cox_raid_solo's kit minus
every raid potion; `cox_raid_solo.lua` and `cox2_raid.lua` untouched) calls:

| where | call | what it does |
|---|---|---|
| after a solver's room | `cox_supplies_loot` | seeds, secondaries (stacks), raid potions the boss dropped (keeps 1 slot; a seed or secondary may take it) |
| scavenger room | `cox_scavengers_solve{make=PLAN}` | the secondaries the whole plan needs |
| resource room, floor 1 | `cox_prep_solve{make=PLAN, take_tools={"rake"}}` | the rake taken and carried (a slot made by storing an overload when full); brews only what boss seeds allow |
| floor-1 end, floor-2 start | `cox_weeds_solve{plan=PLAN}` | raked for the seeds the plan still needs; one free slot per seed type not carried (made at the end room's unit) |
| resource room, floor 2 | `cox_prep_solve{make=PLAN, final=true}` | gear switches PARKED in private storage (25 items unbuilt), the plan brewed, finished potions banked mid-prep when the pack fills, tools/seeds/secondaries dropped, gear back, overflow left stored |
| floor-2 end | `cox_olm_pack{leave=...}` | sword, pickaxe, salve, antidote, junk into storage; every potion in; the Olm kit's quota back (overload 2, enhance 4, aid 6, revit 4), then aid in every free slot |

PLAN = aid 12, revit 5, enhance 5, overload 2 (the Olm kit plus the way).
Solver changes: the pick stops at the need; a grown patch whose herb is no
longer needed is cleared (op3) for the next seed; gourds take half the free
slots; brew order is the kit's (overload parts, enhance, aid, revit).

`cox_resource.lua` n/2 even, n/4 odd is the Olm-kit room test (gear, rake,
2/2/1 seeds, 12/7/7 secondaries): 16/16 sweep variants pass; the Olm kit is
carried after `cox_olm_pack` in ~850-1250 ticks.

THE FLOOR-1 WALL. No seed source exists before the first resource room (wiki
Strategies: seeds come only from guardians, mystics and shamans and the weeds of
the floor-1 end and floor-2 start/end; scavengers drop none). raid1/2/4/6 deal
it as room 1, so floor 1 is fought with boss drops alone, and the room solvers
drink only raid potions. In OSRS a raider brings outside supplies (brews,
restores) for this. raidN (no potions): raid1 crabs, raid2 icedemon, raid3
tightrope, raid5/raid6 vespula; raid4 survives floor 1 on drops and dies at
floor 2's tightrope (the solver timed out). The diagnostic `raidaN` (floor-1
allowance only) shows floor 2: raida1 and raida4 kill Olm and loot the chest
with in-raid kits (Olm drank 52 and 69 doses).

## 8. Outside supplies for floor 1 (2026-10-18)

The coordinator's ruling: the OSRS way. The prep relay's kit brings what the
wiki's minimum setup brings (Chambers_of_Xeric/Strategies "Setups": seven
Saradomin brews, two super restores, a sanfew serum -- here the antidote++
plus a third super restore); no raid potion. `QD.COX.SUPPLY` /
`QD.COX.supply_ids(family)` (cox2_raid.lua) list every drink by need, the
raid's own first: heal (Xeric's aid (+, plain, -), Saradomin brew), restore
(revitalisation, super restore), prayer (prayer enhance, prayer potion, super
restore), overload. Every solver but mystics takes its lists from it; the
dose counters that read a list's index as the dose now read `(i-1)%4+1`.
Tightrope's top-up waited forever with a drained Ranged and only brews (raid4
t1799-5797, raid6 t602-2402): each need now waits only on its own cure.
cox2_supplies.lua knows the outside potions (families brew, superrestore,
prayerpot): parked with the gear during prep, stored one at a time to make a
slot, and taken after the Olm kit's quota at the floor's end.

## 9. Round 4: restores (2026-10-18)

`QD.COX.restore_due(F, stats, gap)` (cox2_raid.lua, beside QD.COX.SUPPLY): a
restore only once a fighting stat is RESTORE_GAP (20) under base -- one
restore covers about 2.7 heal sips (the Muttadiles unit's rule).
`QD.COX.brew_floor` drops the heal floor to 40 with no restore left;
`QD.COX.restore_intent` sets the restore when due. On it now: tightrope
(was 6 under), icedemon and vespula (were the shared ToB channel's boost_stat,
10 under), vespula's pre-fight revitalisation (3 under), vasa (3 under),
muttadiles (its own 20, pointed at the helper), and Olm/Tekton's
CO.restore_due when no overload runs (was: any stat under base -- a restore
per sip in Tekton). Olm with an overload running keeps its own re-boost rule.
Prep: the gear comes back by swapping one item at a time (a full store had
kept the pickaxe and the blowpipe); the seeds and secondaries wait in storage
between preps; the pack takes a share of restores with its heals
(QD.SUP.KEEP_QUOTA: aid 5, revitalisation 2, super restore 1, brew 3,
enhance 1, then the rest).
