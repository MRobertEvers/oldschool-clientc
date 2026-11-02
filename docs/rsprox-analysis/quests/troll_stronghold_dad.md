# Troll Stronghold: Dad, the arena champion — RSProx index 4455 [698..997]

Recording: index **4455** (rev 241.1, w495, 2026-11-02, "Troll Stronghold and Freeing Edgar"),
the arena [698..997]. The recorder fought with a staff of earth on autocast (earth strike,
`human_caststrike_staff_walkmerge` + synth 132/133/227 — the recorder's kit) and Protect from
Melee, safespotted Dad from the west door tile (2897,3619) [861..925], answered the surrender
with "I'm not done yet! Prepare to die!" and left through the north gate [978..980]. **Dad never
dies in this recording**: after the surrender Attack is refused, and his `[del]` at [997] is the
recorder walking out of view (18 tiles). A second source for the throw only: index **945**
(rsprox.net 948, rev 235, normal world), the Sailing charting event's Dad
(`sailing_charting_drink_troll_dad`, 15167) in the same arena, eight throws [9314..9522].

Ours: `quests/quest_troll/scripts/troll_champion.rs2` (rewritten), `troll_spectator.rs2`,
`configs/quest_troll.npc` ([troll_champion]), the generated blocks
(`_explicit/configs/server_fields.generated.npc` [troll_champion]: `facing`, `wanderrange`,
`maxrange` moved out; `npc/configs/npc_movement.generated.npc` [troll_champion] `wanderrange=0`;
`npc_movement/t/troll_champion.move` authored stationary), `tools/check_quest_combat_contract.py`
(the Dad tuple). The arena doors, Dad's spawn (`~troll_ensure_dad`) and the quest stage machine
are `quest_troll.rs2` (`troll_stronghold.md`).

Tests (scriptrun, private packs, `src/build_dad_opt/torirsserver`, `TORIRSSERVER_DEV=1`):
- `test/quests/troll_dad.lua`: stage 10 inside the west door; leg A prayed, "I'll be going now.";
  leg B unprayed, "I'm not done yet! Prepare to die!", then out of the arena. 24/24 rows, with
  `t.ticklog.start()` before Dad's spawn (run `dad12`, 306 ticks).
- `test/quests/troll.lua`: its Dad section now takes the recorded route (Attack, the page, the
  recorded answer). Every Dad row passes; the run's one FAIL is `quest.journal` (no quest list on
  scriptrun).

## 1. The recorder's actions in the window

| tick | action | verdict |
|---|---|---|
| 699 x4, 978 | arena entrance/exit doors | `quest_troll.rs2` (`troll_stronghold.md` §4) |
| 710 | wield staff of earth (autocast earth strike) | the recorder's kit |
| 712, 947, 957 | Protect from Melee on/off | the recorder's kit (prayer: generic) |
| 714, 944, 985 / 959, 966 | prayer restore sips / lobsters | generic consumption |
| 717 x25 | Attack Dad | **this content**: the challenge [718], the fight, refusals after [925] |
| 950, 974 | Talk-to Dad | **this content**: "He doesn't seem interested in talking right now." 2/2 |
| 968 | take `cert_blankrune_high` x25 at 2909,3617 | generic pickup, **not Dad's drop**: it and a `bones` appeared there at [926] as `obj_add_specific ... despawn=100, ownership=other` — the drop of the clue wizard `trail_cabbage_mage` another player (TheWisco Kid) killed on that tile at [824], turned public. The 5 Camelot tablets re-sent in backpack slot 0 at [960]/[967] were there since login (count 5 -> 5). |
| 996 | quest list | `troll_journal.rs2` |

Teleports in the window: the six throws [726], [737], [753], [817], [825], [952] (§4); no other.

## 2. Pages, messages and vars

| tick | recorded | ours | verdict |
|---|---|---|---|
| 704 | Dad "No human pass through arena without defeating Dad!" chatang1, troll_told 1 | `quest_troll.rs2` | not this file |
| 717->718 | Attack: `troll_accepted_challenge` 0->1 + Dad "Tiny human brave. Dad squish!" **chathap1** (chat_left, his head) on the op tick | `[proc,troll_champion_challenge]` from `[opnpc2]`/`[apnpc2]` | **fixed** (was an overhead `npc_say`); wirelog diff: varbit, echo, 2379, opensub, npchead 4130, anim 567, texts, 600 all `=` at A+0 |
| 719 | busy 1; player `[facing] coord` = Dad's middle tile | facesquare(centre) | matched form; ours sends it at A+0 and A+1 (generic, §8) |
| 721 | continue: busy 0, page closed, `current_hint_arrow` 0->7, `[hint_arrow] reset` + `npc` | `~troll_dad_hint` after the page | **fixed** (the reset and the varbit were never sent) |
| 925 | on the hit 25->16: `troll_to_the_death` 0->1 and `troll_quest` 10->20 **before** the answer, the arrow re-armed then off (7->0->7->0, reset/npc/reset), busy 1, Dad "Stop! You win. Not hurt Dad." **chatscared1**, player faces Dad's middle (2902,3620) | `[label,troll_dad_surrender]` | **fixed**: wirelog A+0 all `=` (16 lines); the old port set `troll_to_the_death` only on answer 2 and clamped the hit |
| 926 | Dad `[facing] type=reset` | Dad `npc_setmode(none)`; he then re-faces his **owner** (engine, §8) | OPEN |
| 933 | menu "Select an option": "I'll be going now." / "I'm not done yet! Prepare to die!" | `~p_choice2` | matched |
| 938 | player "I'm not done yet! Prepare to die!" **chatang1** | `~chatplayer_anim(^chat_angry, ..)` | matched (wirelog A+0 `=`) |
| 939 | continue: `current_hint_arrow` 0->7, reset + npc, busy 0 | `~troll_dad_hint` | matched (A+1 `=`) |
| 940 | Dad `[facing] player=` | `[ai_queue4]` `npc_attackplayer` | matched (owner face already up, §8) |
| 942, 948, 954, 955, 961, 972 | Attack -> "You don't need to fight him again." (after answer **2**) | `~troll_champion_attack_gate`: `troll_quest >= 20` | **fixed** (the old gate let "to the death" fight him) |
| 951, 975 | Talk-to -> "He doesn't seem interested in talking right now." | `[opnpc1]` | matched |
| — | Talk-to before the challenge | LostCity/wiki pages | UNVERIFIED (no recording) |
| — | "I'll be going now." page mood, Dad after it | LostCity: chathap1, Dad stands | UNVERIFIED |

All three vars ride `varp318_troll_varbit` (transmit, perm: `quest_troll.varp`, the other
session's).

## 3. Npc info: Dad (troll_champion, size 3)

| | recorded (4455) | ours (`dad12`) | verdict |
|---|---|---|---|
| spawn | `[add]` 2901,3618 **west**, no `creationcycle`, [698] (14 tiles off, entering view) | `npc_add` 2912,3613 west on `[mapzone,0_45_56]`, owner-private | angle matched (`facing=west`); tile and owner OPEN (§8, `quest_troll.rs2`) |
| idle movement | none [698..726], none [984..997] | none (`wanderrange=0`, `.move` authored) | **fixed** (generated `wander 5`) |
| chase | walk, 1 tile/tick; 16 tiles from his start ([841]) | combat chase; `maxrange=30` | matched (LostCity's 30; generated 7 would stop him) |
| first swing | [718] challenge -> [726] (8) | challenge -> +8 (21 -> 29) | **fixed** (`opplayer2` swung at once) |
| cadence | 13 of 16 gaps = 8 (others: walking back, safespotted) | engine attackrate 8 | matched |
| seqs | `troll_attack` 284 x10, `troll_treetrunk_attack` 1158 x7, `troll_block` 285 (delay = the spell's impact) x37; **never** `troll_death` | same three; block on the player's arrows | matched (the plain swing played no seq or sound before) |
| headbar | id 0 (`healthbar_standard_30`), 23 fills | id 0 | matched |
| hitpoints | 23/23 fills fit `1 + 29 hp/120` only with **+1 hp every 10 ticks** (5 phases); the engine's 100 fits 2/23, none 1/23 | `param=regenrate,10` | **fixed** (LostCity: regenrate=10) |
| facing | `player=` while fighting; `reset` [926], [981] | entity face while fighting; never reset while owned | OPEN (owner re-latch, §8) |
| spotanims | `earthstrike_impact`/`failedspell_impact` x38 | none | the recorder's kit |
| block kinds (`npc_block_inventory.py`) | facing, headbar, hit, sequence, spotanim | same minus spotanim | kit only |
| stats | cache 60/50/120/120/0/0, attackrate 8, strbonus 70 | `combat_stats.generated.npc` | `check_npc_cache_levels.py`: 0 overrides |
| sounds | 862 attack (10/10), 865 swing (7/7), 864 hit (23/23 on Dad); 863 death never | 862, 865, 864 | matched |

## 4. The treetrunk throw (7 in 4455, 8 in 945)

On the swing tick S: Dad `troll_treetrunk_attack` + synth `troll_champion_swing` 865; the
player `[facing] angle`, `human_troll_flyback` delay 20, `[exactmove] to1=<tile> delay1=20
to2=<landing> delay2=40 angle=<same>`, `[teleport]` to the landing, busy 1; S+1 nothing; S+2
busy 0, the hit (no block seq), the player back on Dad. The direction is the player's side of
Dad's middle tile per axis; the angle faces back along x unless level with him.

| session [tick] | Dad SW / middle | player | dir | landing | tiles | angle |
|---|---|---|---|---|---|---|
| 4455 [726] | 2901,3618 / 2902,3619 | 2902,3617 | S | 2902,3612 | 5 | 1024 |
| 4455 [737] | 2907,3613 / 2908,3614 | 2910,3613 | SE | 2915,3608 | 5 | 512 |
| 4455 [753] | 2913,3612 / 2914,3613 | 2913,3615 | NW | 2908,3620 | 5 | 1536 |
| 4455 [769] | 2906,3619 / 2907,3620 | 2905,3620 | W | none: 2900,3620 is blocked floor though 2901..2904 are open | 0 | 1536 |
| 4455 [817] | 2905,3613 / 2906,3614 | 2907,3612 | SE | 2912,3607 | 5 | 512 |
| 4455 [825] | 2909,3609 / 2910,3610 | 2912,3609 | SE | 2917,3604 | 5 | 512 |
| 4455 [952] | 2901,3618 / 2902,3619 | 2900,3618 | SW | 2899,3617 (west wall) | 1 | 1536 |
| 945 [9314] | 2912,3612 / 2913,3613 | 2911,3612 | SW | 2906,3607 | 5 | 1536 |
| 945 [9418] | 2909,3608 / 2910,3609 | 2911,3607 | SE | 2916,3602 | 5 | 512 |
| 945 [9442] | 2911,3606 / 2912,3607 | 2910,3607 | W | 2905,3607 | 5 | 1536 |
| 945 [9450] | 2906,3607 / 2907,3608 | 2905,3607 | SW | 2902,3604 (corner) | 3 | 1536 |
| 945 [9474], [9490], [9506], [9522] | 2903,3604 / 2904,3605 | 2902,3604 | SW | none (corner) | 0 | — |

Rule (`~troll_dad_throw_dest`): diagonal — the furthest of 5..1 with a clear line of walk (12/12);
straight — 5 or nothing (3/3). Ours on the wire (`dad8`): [84] `face_angle 1536` + `exactmove
2901,3613 -> 2900,3612 20 40 1536` beside 4455 [952]'s 1-tile throw (`wirelog_tells.py`: busy,
face_angle, synth 865 `=`); [148] the blocked throw beside [769] (busy, face_angle 1536, 865 `=`,
no exactmove on both). Hits at S+2: 7/7 recorded, 3/3 ours ([31]->[33], [103]->[105],
[224]->[226]). Choice of swing: 7 of 17 treetrunk, LostCity's 1 in 3 kept [rate unverified].

## 5. Damage (§13) and every splat (§13b)

| attack | recorded n | prayed (Melee) | unprayed | ours (`dad12`) | verdict |
|---|---|---|---|---|---|
| plain swing, splat on S | 10 | 10, all 0 | — | prayed 8: all 0; unprayed 15: 0..27 | prayed matched; max 27 = `floor(0.5 + 128 x 134 / 640)` |
| throw, splat on S+2 | 7 | 6, all 0 | 1: 16 ([954]) | 3 throws, splats on S+2 | timing matched; n too small for a max or zero rate |

`rsprox_hit_causes.py --npc troll_champion` (4455 vs `dad12`): `troll_attack@0` 10 rec / 23 ours;
`troll_treetrunk_attack@2` 6 rec (ours land on S+2 but the tool names Dad's own block seq on the
same tick first: our ranger hits him every 3-4 ticks); `none` 2 = [982], [990], the trolls north of
the gate (`troll_guard_attack` 867), not Dad. No heal splats either side; Dad's regeneration is
silent on both.

## 6. The surrender rule

The splat lands, then the check (4455 [925]: 9, fill 4). Hitpoints after each recorded hit (fit
above): [920] 33->25 no surrender, [925] 25->16 surrender: the threshold lies in 17..25; LostCity's
`< 20` is used. LostCity checked BEFORE the damage (the surrender hit never landed) and our old
port clamped every hit to leave 19: both wrong against [925]. One hit from 20+ to 0 would still
kill him (no recording; LostCity's death handler kept, UNVERIFIED).

## 7. The spectators (troll_spectator1..7)

| | recorded | ours | verdict |
|---|---|---|---|
| movement | none in 26 lives, no seqs | stationary (generated `wanderrange=0`) | matched |
| face | Dad's south-west tile, re-sent every 8..16 ticks (53 gaps: 8:6 9:3 10:5 11:11 12:6 13:4 14:4 15:5 16:9) | timer `random_range(8, 16)` | **fixed**: the owner-private Dad was invisible to the spectators' find, so they never turned (0 faces); now through each player in reach |
| add | spawn angle toward Dad + `[facing] coord=` Dad (26/26) | `npc_facedir` with the face | matched |
| Attack | not recorded | LostCity gate | UNVERIFIED |

## 8. Open

1. **Dad's spawn** (`quest_troll.rs2`, not mine): recorded as a world npc at 2901,3618 facing west
   (no `creationcycle`, there before the recorder, walking after he left); ours `npc_add`s him at
   2912,3613, owner-private. With `troll_to_the_death` now set at every surrender,
   `~troll_ensure_dad`'s `(stage >= defeated_dad & to_the_death = false) -> return` re-adds him
   for every post-surrender player.
2. **Owner re-latch (engine)**: an owned npc with no fight faces its owner
   (`torirs_server_world.c`, the familiar rule), so Dad faces the player from the add and never
   sends the recorded resets ([926], [981]).
3. **A delayed player re-paths to its target (engine)**: `ToriRSServer_CombatPlayerApproach` and
   `interaction_path_to_pathing_target` run while delayed (LostCity `pathToPathingTarget` returns
   while `!canAccess()`), so a melee player steps off the throw's landing on S. Ranged/magic in
   reach (the recorder, both tests) do not move. Not patched in content: a stun ends the fight
   and the face release overwrites the angle.
4. **Generic chat/face (not Dad's)**: `~chatnpc*` page faces go out on the page's tick and again
   the next (Jagex: the next only); `if_setevents` on continue (0..28 vs -1..-1); varbit 10670.
5. **Engine hint arrow**: `hint_npc` sends no reset, no `current_hint_arrow` and nothing for an
   arrow already up (Jagex: reset + set + varbit, every time; 4426, 4451, 4452, 4456 too) —
   worked around in `~troll_dad_hint`; the priority gate spans one tick (`p_animprotect` for the
   S+2 hit); dotted npc finds from an npc trigger see no owned npc.
6. UNVERIFIED: Talk-to before the challenge, answer 1's mood, Dad's hitpoints after "Prepare to
   die" (LostCity restores the surrender's), the 1-in-3 rate, his death and drop, the spectators'
   Attack, the arena zone (LostCity's; the recorded hint reset is on the gate tick [980], ours
   the tick after the player's tile leaves it).
