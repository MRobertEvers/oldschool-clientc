# Desert Treasure I: the Shadow diamond (Rasolo, the chest, the ring, the dungeon road)

Rebuilt 2026-11-21 from RSProx index 4624 (Rasolo, whole), 4625 (the Bandit Camp chest, whole), 4626
(the ring, the ladder, the dungeon, Damis's room; the fight itself is `dt_damis.rs2`, not this section) and
4627 [0..720] (the return). Our port was a 2009scape/wiki one that never saw a recording (see skill §16).
Code: `quest_deserttreasure/scripts/dt_shadow.rs2`. Not run: the tree was shared and mid-edit, so the only
check is the private `sscompile` (clean); every run row is `-- UNRUN:` in `test/quests/deserttreasure.lua`.

## 1. Variables (every recorded write)

| varbit | base varp (scope, transmit) | writes recorded | ours |
|---|---|---|---|
| `fd_shadowwarrior_quest` 383 | 442 (perm, yes: stated in `deserttreasure.varp`) | 4624 [358] 0->1 on the page "How can I find this 'Damis' then?"; 4624 [504] 1->2 on the page "Not a problem..."; 4626 [381] 2->3 with the ring added, on the page "Yes I have!"; 4626 [875] 3->4 Damis transforms into `fd_damis_tougher`; 4626 [1151] 4->5 (hint arrow reset, diamond `obj_add_specific`, Damis deleted) | 0,1,2,3 written (`^dt_sw_*`); 4 and 5 are the fight's (dt_damis.rs2, not yet rewritten) |
| `fd_banditchest_disarmed` 384 | 442 | 4625 [1493] 0->1, the tick of "You manage to pick the final lock." | written there |
| `fd_shadowwar_intro` 385 | 442 | 4626 [734] 0->1 with the voice (three messages) | written by the zone below |
| `shadow_realm_visibility` 15152 | 3954 (perm, yes: stated) | ring on 0->1, ring off 1->0 (4626 [413]; 4627 [484], [487]); sent at login with the ring worn (4627 [0]) | was written as **2** and never cleared a second varbit; now 1/0 |
| `shadow_maj_visibility` 14716 | 976 (was `scope=temp transmit=no` in the generated block, so never sent) | written together with 15152 on every ring on/off, and at login | new: stated perm/yes in `deserttreasure.varp`, the generated lines deleted |
| `poison` (varp 102) | engine | 0 -> 30 on a miss ("You have been poisoned!") | same shape |

Old -> new (private varp `%varp6622_dt_shadow_stage`, left DEFINED): 0 -> varb383 0; 1 (fetch) -> varb383 2;
2 (unlocked) -> varb383 2 and varb384 1; 3 (ring) -> varb383 3; 100 (complete) -> varb383 5. No saved state
for "declined" (new value 1) or for `fd_shadowwar_intro` (0: the voice replays once). dt_damis.rs2 and
dt_journal.rs2 still read `varp6622`; nothing writes it now, so Damis does not spawn until they are rewritten.

## 2. Action inventory (every row has a verdict)

| session [tick] xN | action | verdict |
|---|---|---|
| 4624 [291] x3, [328], [477] | `opnpc1` Rasolo (679) | this content: `[opnpc1,shadow_warrior_rasool]`, stage 0/1 branches, 4624 pages |
| 4624 [474], 4626 [393] | `opnpc3` Rasolo, the shop opens `rasoolshop1` (300) | bound and looked right (`rasolo_the_wandering_merchant.rs2`, generated shop; stock list identical, 25 rows) |
| 4624 [298] | menu answer 1 "Yes" opens the same shop on the answer tick | this content: `@rasolo_the_wandering_merchant_open` |
| 4624 [306],[308] shop buy/value `newcomer_map`, [311] inventory op, [325] `aide_map` | generic shop and map | not exercised further; shop interface generic, bound |
| 4624 [268] Gerald (waterfall quest) | `opnpc1_v2` 4180, two pages | Waterfall Quest's: one verdict row, not this quest |
| 4624 [59] Varrock teleport, [153]/[227] tablet+stamina, [112] bank booth, [125]..[149] bank | generic | bound and looked right (teleport shape not diffed here) |
| 4625 [486] x7 `oploc1` `fd_bandit_shutchest` 6448 | the chest, first press | this content (box, menu, three locks) |
| 4625 [506] x54 `oploct` lockpick (1523) on the chest | use-on | this content |
| 4625 [1511]..[1514], [1526] `oploc1` after the disarm | search | this content |
| 4625 [1335],[1341] `opnpct` `cert_lockpick` on Tiles / bandit | noted lockpicks | start section's (camp pages) |
| 4625 [81],[572] bank chests, [951] GE, [141]/[143]/[153] Shantay + carpet, [280]..[449] camp npcs, [1337] Tiles | camp and bank | camp = start section; the rest generic, bound |
| 4625 [481] `oploc1` `crate2` | "You search the crate but find nothing." | bound and looked right (`general_use/scripts/crates.rs2:43`) |
| 4625 [1509],[1535] `opnpct` cross on a bandit / [1520] drop cross, [1533] pickup | "Nothing interesting happens." / put_down / pick2 | generic, bound and looked right |
| 4626 [376] x2 | Rasolo Talk-to with the cross (stage 2) | this content |
| 4626 [391] `opnpct` cross on Rasolo | engine "Nothing interesting happens." | generic default, left (no binding) |
| 4626 [396] ring op 7 -> `confirmdestroy` | Destroy | this content (`[opheld5,fd_ring_visibility]`, the note "You can get another from Rasolo.") |
| 4626 [400] Rasolo after a destroyed ring | "I lost that ring you gave me..." | this content |
| 4626 [413], 4627 [487] ring op 3 | Wear | generic `[opheld2,_]`; our invented gate in the ring's own binding was deleted |
| 4627 [484] worn slot 12 op 1 | Remove | generic; `dt_refresh_ring_visibility` clears both varbits |
| 4626 [421] `oaktree` (clicked past), [430] x2 / 4627 [489] `fd_shadowladder1` 6560 | the ladder | this content (`climb_ladder_to`) |
| 4626 [519] x2 `boxes`, [538] `sacks` (dungeon) | search | bound and looked right (messages identical, `general_use`) |
| 4626 [447]..[490] prayer15/quick prayer, [449] shark, [516] stamina | recorder's own kit | generic / the recorder's |
| 4626 [751]..[906] snare, water_blast, `opnpc2` on skeletons/Damis, dart | the fight | dt_damis.rs2 (not this section) |
| 4626 [1153] `opobj3` `fd_dark_diamond` | pickup | generic (4626 [1153]-[1154]: inv slot 2, obj_del_specific) |
| 4626 [1200] ring of dueling, [1213]..[1258] bank, 4627 [9]..[43] bank/potion mixing, [155] tablet | recorder's kit | generic / the recorder's |
| 4627 [636] `fd_dark_diamond` op 7 (Drop), [665] `opobj3` | drop and pickup | generic; see section 4 for why a diamond lay there |
| 4627 [484]/[487] ring off/on, [489] ladder | the return | this content |

## 3. Teleports

| session [tick] | from -> to | cause | verdict |
|---|---|---|---|
| 4624 [62] / [157] | (3307,9376)->(3211,3422) / (3185,3436)->(2660,3303) | Varrock teleport spell / Ardougne tablet | generic |
| 4625 [20],[620],[1050] etc. | ring of dueling (Duel Arena / Castle Wars) | worn ring op | generic |
| 4625 [165].. | magic carpet rides | carpet seller | start section's |
| 4626 [433] / 4627 [493] | (2547,3422,0) -> (2630,5071,0) | the shadow ladder | this content: shape face L+1, `human_pickupfloor` (827) L+2, move L+3 via `~climb_ladder_to`; **our constant was one tile off (5072): `^dt_shadow_dungeon` is now 0_41_79_6_15** |
| 4626 [1195],[1204], 4627 [721] | tablet / dueling ring / Varrock teleport out | generic |

## 4. Scenes, ambience, the "diamond on the floor" in 4627

No scene: the only cam/minimap packets in these windows are the login and teleport `cam_reset`, `minimap_toggle` pair
(generic). Zone ambience: nothing recorded near Rasolo beyond the fish `sound_area` (the generic fishing spot).
The dungeon plays "Path of Peril" (`midi_song_v2` 393) on arrival, with the "You have unlocked a new music track" line
the first time only (4626 [433]; absent 4627 [493]): generic music, not verified in ours.

**Why a diamond was on the floor in 4627.** The character already held the diamond (stage 5). At [636] he dropped it
(op 7) at (2720,5069) (`despawn=300`). At [641] and again at [658] the game sent "The Diamond of Shadow seems to have
mystically found its way back here..." with an `obj_add_specific` of a second diamond at **(2740,5089)**
(`despawn=2000`, owner self, never public). Both are one mechanic: on entering a fitted area with stage 5 and no diamond
held, the diamond is put at Damis's feet. At [665] the player picked up his own dropped one. The trigger is the same area
as the dungeon voice: 4626 [734], 4627 [641] and [658] each fire the tick after the player's tile enters
x 2728..2735, z 5072..5079 (tiles read before the tick's move: (2729,5072), (2728,5072), (2735,5075)); [658] is a re-entry
from the east. That is exactly the 8x8 zone `0_42_79_40_16`; the player was never in it earlier (checked 4626 [728..733],
4627 [630..640]). Ours: `[zone,0_42_79_40_16]` in dt_shadow.rs2 does the voice (once) and the return. The old copy of the
return in dt_damis.rs2 (`[zone,0_42_79_48_32]`, gated on the old varp) is dead and should go.

## 5. Pages (distinct recorded strings of my windows)

Recorded n = 47 (4624 Rasolo, the chest box and menu of 4625, 4626 Rasolo; Gerald's two and the camp pages excluded).
Before: about 7 matched (the old Rasolo spoke the same words in different pages and carried invented ones).
After: 44 of 47 verbatim; the 3 missing are "Hello." x2 (to Gerald, 4624 [268], and to Jerico, 4626 [251]) and "Why do you say that?"
(to Gerald): Waterfall Quest's and a bank-side npc's, none this quest's. Wiki-only, marked `// wiki, mood
unverified`: the stage-2 "No, not yet..." branch (6 pages and one menu); the pre-hunting menu (Yes/No). Deleted as in
neither source: "Excellent, excellent. Here, take this ring..." and its follow-up, "Laheeb hides
his loot...", the Thieving-level message, "The chest is empty.", "The chest is unlocked.", the ring's equip gate.

| item | recorded | ours |
|---|---|---|
| Rasolo menu | `Select an option`: Yes / No / Ask about the Diamonds of Azzanadra (4624 [295],[329]) | same order, title |
| the offer "does this seem fair" | `Yes|No` (4624 [466]) | same |
| the chest menu | title `Try to open the chest?`, `Yes|No` (4625 [491]) | `p_choice2_header` |
| messages | "You attempt to pick the first/second/final lock...", "...You successfully picked the first/second lock!", "You manage to pick the final lock." (note: no "the" in the wiki's "pick final lock"), "...and fail. The locking mechanism has reset itself.", "Your lock pick snapped while attempting to pick the lock.", "You will need a lockpick for this lock.", "Inside the chest, hidden under some rags, you find a Gilded Cross.", "You find nothing of interest inside the chest." | verbatim |

## 6. Shapes (the chest, timed)

Press (`oploc1`) T: mesbox T+1, menu 3-5 ticks later at the player's click, "attempt" message the tick after the answer.
Use-on (`oploct`) T: "attempt" on T. Then one message a tick: first result T+1; success -> "second" attempt T+2, result T+3,
"final" attempt T+4, result T+5 (4625 [1488]..[1493], [858]..[862]). On a miss at any lock, the result tick carries: the two lines, the lockpick gone from
its slot, a hit id 65 (the poison hitsplat sprite) of 2 or 3 (30 twos, 25 threes in 55 misses), a healthbar, `map_anim shilomist` (184) height 124 on the
player's own tile, `smokepuff` (1930); 3 of the 21 misses taken while neither poisoned nor immune also say "You have been
poisoned!" and write varp 102 = 30 (none of the 14 taken poisoned, none of the 20 taken immune). Observed success: 22 of 56 first locks, 5 of 22
second, 1 of 5 final (the kept `stat_random(thieving,52,128)` at level 53 gives about 36%: consistent). The final success
tick: message, varbit 384, synth `pick` (2581), Thieving +150 xp; T+2 `human_lockedchest` (537) + `chest_open` (52);
T+4 the find message, `fd_bandit_openchest` for 9 ticks (4625 [1497]->[1506], [1515]->[1524]), cross into the pack, synth
`clock_tick_1` x3 (first find) or `light_creature_down` (a repeat). A later press with the cross held: `pick` on T,
anim T+2, "You find nothing of interest..." T+4 with the open chest. A lost cross is handed over again, no picking.
The cross stays in the pack when given to Rasolo (4626 [393] still lists it); the ring arrives on the page tick.

## 7. npc info (Rasolo, 679)

Recorded blocks: `[add]` x3 (one per login, spawnangle south/east/north-east), `[walk]` 12 steps (one tile a tick, runs of up to
6 north), `[idle]`, `[facing]` x6 (faces the player on talk, `fallbackangle`). No `[say]`, `[bas]`, transformation or op change.
Written: the generated defaults (`defaultmode=wander`, facing south), his op1/op3 binding; wander range raised 5 -> 8,
maxrange 7 -> 10 in `deserttreasure.npc` (the generated lines removed): recorded tiles x 2533..2541, z 3434..3440 against
the spawn (2535,3432) exceed 5 and 7 (z is 8 away). 8 is the smallest range that covers them; Jagex's value is unrecorded.
Dungeon npcs (`sword_skeleton_3`/`3b`, `shadow_dog_wild`, bats, rats): spawned from `m41_79.spawn`/`m42_79.spawn`, no multinpc; the hidden ones
are the `secret_ghost*` and `shadow_maj_multi_*` multinpcs on 15152/14716 (the ring's two varbits).

## 8. What leads up to Damis's first tick (for the fight agent)

4626: voice [734] at (2730,5073) (see section 4), then the player runs on: [737] (2735,5071), [741] (2737,5079), [746] (2739,5089).
[747] `[add]` `fd_damis_normal` (682) at **(2740,5089)**, spawnangle south, `creationcycle` set, `[say]` "You should... have listened
to me!" (ellipsis, ours drops it), `sound_area smokepuff` range 10 at his tile, `[hint_arrow]` reset then npc, varbit
`current_hint_arrow` (14192) 0->7; [748] idle; [749] first step (to 2739,5090). The area is `0_42_79_48_32` (x 2736..2743, z 5088..5095):
the player was first inside at [746]. Skeletons and dogs (multi-combat, `multiway_indicator` 4605 set on the voice tick; the first skeleton hit lands 4 ticks after
the arrival, 4627 [497]) attack on the way (block anim, `armed_skeleton` 775). At [875] Damis `[transformation]`s to 683 (chop anim 406,
synth `2h_slash`), says "This armour... is for restraint, not... protection...", then "Now I show... you... my true... power!";
at [1151] hint arrow reset, `current_hint_arrow` 7->0, `obj_add_specific fd_dark_diamond` (`despawn=200`, owner self,
never public) at his death tile (2733,5096), varbit 383 4->5, npc deleted; [1153] the pickup.

## 9. Open

1. `dt_damis.rs2` and `dt_journal.rs2` read `%varp6622_dt_shadow_stage` (old 3/100): rewrite to varbit 383 (3 -> 4 at the
   transformation, 4 -> 5 at death) and drop the dead diamond-return block. Journal text recorded: 4626 [384] (stage 3,
   the ring "I should put the Ring of Visibility on...") and 4625 [310]..[319]/[1540] (stage 2, "I need to find Laheeb's loot...").
2. Chest and Rasolo at stage < 2, Rasolo after the ring at stage 4/5 with the ring held, the "No" to the shop, the pre-hunting
   menu: unrecorded (wiki or default).
3. Poison chance 1/7 (3 of 21) and 2-3 damage are fitted, not read from code.
4. Wander range of Rasolo is a fit; his spawn tile is Jagex's `m39_53.spawn`.
5. Opening via `oplocu` with a lockpick after the disarm, and the exit ladder (up), were not exercised (the recorder used tablets).
6. `shadow_maj_multi_khazard` (another quest's) now draws with the ring: that is what Jagex writes.
7. The dungeon's first-visit music-unlock line is generic and unchecked.
