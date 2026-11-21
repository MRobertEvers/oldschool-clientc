# Desert Treasure I: Damis, both forms (the Diamond of Shadow)

Fight file `OSRS-Content/osrs-content/server/scripts/quests/quest_deserttreasure/scripts/dt_damis.rs2`,
npc blocks `configs/deserttreasure.npc` (`fd_damis_normal` 682, `fd_damis_tougher` 683), test
`test/quests/_dt_damis.lua` (private scriptrun `dtdamis3`: 8/8). Lead-up: `desert_treasure_i_shadow.md` §8.

## 1. Sessions

`rsprox_index.py search npc 682` / `683`: **index 4626 only** (rev 242.1, w442, normal world,
"Desert Treasure 1 - damis 6/x"). `search mapsquare 10831` adds 3907/3909 (rev 236), 4627, 330, 2998
and three Leagues V sessions: none has Damis (no 682/683 row). One recording is one roll: every
rate below is a count from one fight, [746..1154].

## 2. Per-life record (index 4626)

| | recorded | ours (dtdamis3) |
|---|---|---|
| trigger | player enters zone 48_32 (x 2736..2743, z 5088..5095) at [746]; add at [747] | zone trigger, add on the crossing + 1 (t37 after t36) |
| add | (2740,5089), spawnangle south, creationcycle; `[say]` "You should... have listened to me!" | same tile, `facing=south`, same say on the add tick |
| add-tick extras | `map_anim smokepuff` (86) h0; `sound_area smokepuff` (1930) range 10, his tile; `current_hint_arrow` 0->7, hint reset, hint on npc | same (spotanim_map, sound_area, varbit, hint_stop + hint_npc) |
| movement | walk only, 1 tile/tick (form 1: 33 steps, form 2: 65); chases up to 14 tiles (Chebyshev) from the add tile | engine chase; `maxrange=14`, `defaultmode=none`, `wanderrange=0` (generated: wander 5, leash 7) |
| facing | `[facing] player=` with each re-target (20 form 1, 47 form 2); held through swings; none on the transformation tick; `type=reset` at kill + 1 | held; none on the transformation tick; reset at kill + 1. **First swing: ours sends `reset` on the swing tick and the face a tick later** (Open 1) |
| seqs | swing `human_dhsword_chop` 406 (11 form 1 incl. the transformation tick, 36 form 2); block `human_dhsword_block` 410, always with the projectile's delay (37..146); death `human_death` 836 | same ids (generated: `human_unarmedpunch` / `human_unarmedblock`) |
| transformation | [875] = kill [874] + 1: `[transformation]` 683, `level_change` 174, `enabled_ops 0b11111`, no `[del]`, no death seq | kill + 1 (t153 after t152): retype, level 174, ops (npc_changetype's own), heal to 200 |
| says | add; [876] "This armour... is for restraint, not... protection..."; [877] "Now I show... you... my true... power!"; 16 taunts (form 2) | add; kill + 2; kill + 3; taunts (rule 4.3) |
| headbar | id 0 (standard 30) | `healthbar_standard_30` |
| death | kill [1146] (on a step); [1147] facing reset + `enabled_ops 0b00000`; [1149] seq 836 + synth `human_death` 512; [1151] `[del]` | engine schedule: he stood still on the kill, so ops + reset + seq + synth at kill + 1, `[del]` kill + 3 (t525/526/528); the recorded walking case is +1/+3/+5 under the same rule (arrive delay 2) |

## 3. Block kinds (`npc_block_inventory.py --values`)

| kind | form 1 rec | form 2 rec | ours |
|---|---|---|---|
| say | 1 | 18 (2 scripted + 16 taunts, 7 distinct) | every line, scripted ticks exact |
| sequence | 28 | 76 | swing/block/death ids as recorded |
| spotanim | 18 | 39 | the recorder's spell impacts (waterblast 137, snare 180, failedspell 85): kit |
| facing | 20 | 47 | entity face held (Open 1) |
| hit / headbar | 22 / 22 | 51 / 51 | engine |
| transformation, level_change | 1 / 1 | - | 1 / 1 |
| enabled_ops | 1 (0b11111) | 1 (0b00000) | 1 / 1 |
| bas, body_customisation, transparency, exact_move | none | none | none |

## 4. Attacks and rules

### 4.1 The swing (both forms)

| | form 1 | form 2 | ours |
|---|---|---|---|
| seq + sound | 406 + synth `2h_slash` 2503 on the swing tick, 11/11 | 36/36 | params `attack_anim`, `attack_sound` (default `[ai_opplayer2,_]`) |
| cadence | 4 ticks, 10/10 back-to-back gaps | 3 ticks, 31/31 | 4 then 3 (t38..t153 by 4, t156.. by 3); the gap after the very first swing is 3 in ours (engine hand-over from the mode swing to the combat clock; the recording has no back-to-back first gap) |
| first swing | add + 1 ([748]) | the transformation tick ([875]) is on form 1's cadence | add + 1; kill + 1 on the cadence |
| splat | same tick as the swing (e.g. [748]); [875]'s lands [876] (the player is held that tick) | same tick | same tick; the transformation swing's 0 lands on kill + 1 (Open 2) |
| damage | 47 swings, 47 zeros, all under Protect from Melee (`prayer_allactive` 16384 from before [744] to [1151]) | | 0 under Protect from Melee, every swing |
| levels | cache stat1..4 90/90/90/90, str bonus 80: max 22 (wiki 22) | 160/160/100/200, str bonus 100: max 28 (wiki 28) | blocks match the cache; no unprayed hit recorded to fit |

Defend sound: form 1 `sword_hit2` (12) on 17 of 22 splat ticks, form 2 `human_hit` (513) on 45 of 51;
every tick without it is one the recorder cast a new water blast on (11/11: engine-generic).
Ranged defence of form 2 is the wiki's 120 (UNVERIFIED; the cache states none).

### 4.2 The prayer drain (form 2)

Fitted per tick on [875..1146]: the player's tile at the start of the tick against Damis's tile after
his own step. **100 of 100** ticks with the player a tile N/S/E/W of him lost `floor(prayer/20) + 1`,
on top of Protect from Melee's own point every 5-6 ticks; **0 of 167** other ticks did. [882] is the
deciding tick: he stepped beside the player and drained that tick. No diagonal tick recorded. Form 1
never drains (its adjacent ticks [847..874] show only the 5.5-tick prayer drain).
Ours: kill + 1, + 2, + 3 from the transformation scripts, then `[softtimer,dt_damis_drain]` (the
player phase: after npcs move, before the player does). dtdamis3 t153..t157: 47->44->41->37->35->33
(rule + one normal point at t155). The old per-swing drain and its invented message are deleted.

### 4.3 Taunts (form 2)

All 16 on the 100 adjacent ticks, none on the 167 others; offsets 0/+1/+2 from his swing evenly (not
tied to it). One in six per adjacent tick (`random(6)`; 1/7 fits as well). Lines 3,3,2,2,2,2,2 times,
repeats allowed: uniform `random(7)`.

### 4.4 Other rules

| rule | evidence | ours |
|---|---|---|
| other npcs pass through him | [877]: two skeletons and a dog idle on his tile (2740,5084); wiki | `blockwalk=none` |
| multi-combat room | `multiway_indicator` 0->1 at x 2728 on z 5072 (4626 [734], 4627 [641], [674], [697]), back at 2727 ([1174], [663], [692]); 7 splats on one tick [882] | **fixed:** 11 zones appended to `maps/multiway.csv` (the rev-254 file has no Shadow Dungeon); before it, dtdamis1's Damis dropped his mode after one swing and every cast was refused |
| the player held one tick | `busy` 0->1 [875], 1->0 [876] | `%varb12393_busy` hold on kill + 1, released by a player queue on kill + 2 (wire: varp 3077 bit 16) |
| immune / weakness / heal | none recorded ("Your target is currently immune to that spell" [770] is a snare on a snared npc: generic) | none |

## 5. Hit causes (`rsprox_hit_causes.py --npc 'damis|skeleton|shadow_dog' --by-prayer`)

| cause | rec | ours | verdict |
|---|---|---|---|
| Damis swing, adjacent | 41 splats, all 0 | all 0 | matched (prayed) |
| skeletons (both types) / shadow dogs | 336 / 49, mostly 0; the nonzero ones after the recorder's own prayer click [1151] | filed under Damis's seqs by proximity (the player stood beside him) | the dungeon's npcs, not this fight's; not audited here |
| heals | none | none | matched |

## 6. The kill and the variables

| | recorded | ours |
|---|---|---|
| `fd_shadowwarrior_quest` 383 | 3->4 [875] (kill + 1), 4->5 [1151] (the `[del]` tick) | same ticks relative to each kill |
| `current_hint_arrow` 14192 | 0->7 [747], 7->0 [1151] with the arrow reset | same |
| drop | `obj_add_specific fd_dark_diamond` count 1, despawn 200, owner self, never public, his death tile (2733,5096) [1151] | `obj_add_private(npc_coord, .., 1, 200, 200)` on the corpse tick |
| pickup | [1153] op 3, [1154] anim -1, `pick2`, obj gone | generic floor pickup; then a queued `~dt_diamond_gained` (dt_blood.rs2) for a shadow diamond taken last (unrecorded tick) |
| player death / leaving mid-fight | not recorded | lifespan `^dt_boss_lifespan`; at 383=4 with no Damis alive the zone brings back the second form (UNVERIFIED) |

## 7. Changes outside the fight file

- `configs/deserttreasure.npc` (Damis blocks only): anims, sounds, `death_script`, leash, wander,
  `blockwalk`, form-2 ranged defence; the same keys deleted from the two Damis blocks of
  `_explicit/configs/server_fields.generated.npc`.
- `configs/deserttreasure.constant`: `^dt_damis_stand` 0_42_79_51_32 -> 0_42_79_52_33 (the recorded add
  tile); the drain constant's comment.
- `maps/multiway.csv`: the Shadow Dungeon's east zones (rule 4.4).
- Old varps no longer read here: `varp6622_dt_shadow_stage` (3 -> 383=3, 100 -> 383=5; mapping in the
  shadow doc), `varp6621_dt_damis_fighting` (temp; replaced by `npc_find`, nothing to migrate).

## 8. Open

1. First swing facing: ours sends `[facing] type=reset` on the swing tick and the player face a tick
   later (engine: an `npc_setmode(opplayer2)` swing clears the mode before the trigger); recorded:
   the face on the swing tick [748]. Engine-generic.
2. The transformation swing's 0 lands on kill + 1 in ours, kill + 2 recorded: Jagex held the player
   ([875] busy) and the queued hit waited; our hold is the varbit only.
3. The 3-tick gap after the first swing (engine's hand-over; no recorded counterpart).
4. Diagonal adjacency for the drain and taunts, the drain when prayer is 0, the drain after the
   killing splat, re-aggression after escape (`huntmode` left none): unrecorded.
5. Zone 56_32 as a second spawn trigger and the ring gate are the 2009scape port's (UNVERIFIED).
6. Multi-combat for room zones the recorder never stood in (40_48, 56_24..56_48) is unstated.
7. The skeletons' and dogs' own fight shapes (multi-combat now on) were not audited (shadow section /
   generic npcs).
