# The raiders' weapons: what each one sends, from RSProx, against ours

Every weapon any raider swung in the Fractured Archive recordings, index
4294..4316 (17 sessions transcribed `--all-players --bas`), not only the raid's
rewards. 43 weapons attacked outside the lobby; the magic weapons cast spells
whose looks are the spell's, so 33 are compared here.

Tools:

- `tools/rsprox/rsprox_weapon_sigs.py <idx*.txt>`: the recorded side. Each
  player's weapon from their appearance block, every non-BAS sequence
  attributed to it, and what came with that sequence on its tick (spotanims,
  map anims, projectiles with a distance table, sounds).
- `test/combat/raid_weapons_tells.lua` + `tools/rsprox/weapon_tells.py`: ours.
  The scriptrun wields each weapon, attacks a passive target (an Archive
  battery), specs where the recordings show a special, and marks the tick log
  per weapon; `TORIRSSERVER_PROJ_DEBUG=1` adds each shot's heights, angle and
  progress.

## Flight times: the rule is by the target's nearest tile

Measured to the shot's target coord (an npc's centre) the end times scatter;
measured as the Chebyshev distance d to the target's nearest tile every
recorded shot fits exactly:

| shot | start | end | shots |
|---|---|---|---|
| arrows, bolts (bows, crossbows) | 41 | 46 + 5d | 9,900+ (1:51 .. 10:96, 100% each) |
| chinchompas | 21 | 32 + 5d | 336 |
| darts (blowpipe) | 32 | 32 + 5d | 549 |
| ascension, right crossbow | 30 | 32 + 5d | 118 |
| ascension, left crossbow | 32 | 30 + 5d | 105 |
| venator bow | 31 | 46 + 2d | 334 |
| Ralos throw / Division | 24 / 36 | 40 + 2d, back + 10 + 2d | 10 / 203 |
| scorching bow special | 46 | 50 + 4d | 19 |

Our `~npc_projectile` already times by `npc_range` (nearest tile), and the bows'
and crossbows' 41 + 5 + 5d is the recorded rule.

## Per weapon

R = recorded (counts are actions with that look / actions of that seq).
"was" is ours before this pass, from the same harness.

| weapon | R | was | now |
|---|---|---|---|
| **crimson kisten** | attack `human_weapons_crimson_kisten_attack` 39/39; special `..._special` + `vfx_crimson_kisten_special` slot 1 131/131; block `..._def` 60/60; own stance set 263/266 | punch (423); special swung the generic slash, no graphic, rolled slash | all recorded; special rolls crush (wiki) |
| **obligator** | thrust `tfa_human_obligator_multihit` + `vfx_obligator_charged_thrust_male` slot 1 (511/511), female forms (906/906); block `..._def`/`_female` 844; stance set per gender 1,735 | nothing (no anim, unarmed style row, unarmed combat tab) | all recorded, by gender; style row Thrust/Thrust/Smash (crush); combat tab layout 36 (DBTable 78 `combat_interface_extending_chainmace`); Smash at 3 ticks, Thrust 5 (wiki) |
| **ascension crossbows** | `..._fire_right_pvn` 118 / `..._fire_left_pvn` 105 alternating; `ascension_crossbowbolt_onyx_travel` angle 5, progress 126, heights 167/146; no launch graphic; stance set 115/115 | could not fire (ascension bolts refused) | all recorded; only ascension bolts (wiki); side kept in `%varp9200_ascension_crossbows_side` |
| crystal halberd | special: map anim `dragon_halberd_special_north_white` height 92, no delay, one tile toward the target (2/2) | player graphic `..._west_red` height 100 | map anim, scythe's direction rule, white |
| dragon warhammer | special graphic slot 1 (29/29) | slot 0 | slot 1 |
| elder maul | special graphic slot 1 (18/18) | slot 0 | slot 1 |
| scorching bow | special `human_weapon_bow_scorched_01_spec` + `vfx_human_scorching_bow_special_attack_01` slot 1; shot 2807 from 46, angle 255, progress 80 | seq 11132 (the graphic's own), no graphic, arrow shape | all recorded |
| tonalztics of ralos | throw `..._charged_throw` + `vfx_glaive_charged_regular` slot 1; glaive out (progress 117) and back from the target (progress 0); target impact graphic slot 1, height 109, at the landing. Division: `..._charged_special` + `vfx_glaive_charged_special`, the same out-and-back | uncharged throw seq, no glaive, no graphics | all recorded |
| soulreaper axe (+ ornament) | `ancient_axe_slash` 245/245; stance turn on the idle, run `ancient_axe_run` | crush on chop/hack/block; the ornament had no looks at all | slash/crush the right way round; stance; ornament copied |
| venator bow (+ echo) | `arrow_venator01_launch01` slot 0; `arrow_venator01_travel01` (echo: `league_5_ventator_proj`) from 31, angle 255; no arrow launch graphic | the arrow's launch graphic and travel; echo bow had no looks | its own shot; echo copied |
| crossbows (dragon, zaryte, armadyl, dragon hunter) | angle 5, heights 155/146; zaryte progress 72, 177; dragon bolts fly `dragon_crossbowbolt_travel` | angle 15, heights 160/144; plain/other dragon bolts flew the generic bolt | angle 5, 39/36, zaryte 44/36 progress 72; every dragon bolt 1468 |
| bows (twisted, bofa, scorching) | 163/146 | 160/144 | 164/144 (content heights are x4) |
| chinchompas | from 21 | from 32 | from 21 |
| zombie axe | smash `battleaxe_crush` 27/27 | axe hack | smash stance only |
| kodai wand, ancient sceptres | block `human_staff_block` 114/115/5 (no off-hand) | unarmed block | staff block |
| off-hands | shields `human_shield_defence` (twisted buckler 262, elysian 52, crystal shield 37, odium 36, elidinis wards 104); defenders and the dragon parrying dagger `warguild_parry_defend` (663) | unarmed block, so the weapon's block won | as recorded |
| scythes, bludgeon, tentacle, inquisitor's mace, macuahuitl, noxious halberd, blowpipes, faerdhinen bows, twisted bow, chinchompa seqs | matched | matched | unchanged |

## Still open

- **Unrecorded looks.** No raider smashed with the obligator (the smash seq is
  by name), fired plain or diamond ascension bolts, or used the uncharged
  glaive or obligator; those take their named or charged counterparts. The
  recorder (Das Idle) never swung the new weapons, so their attack sounds are
  unrecorded and ours play none.
- **Mechanics, not looks:** the obligator thrust's extra targets and the
  venator bow's bounces are not modelled (no rule stated on the wiki pages to
  build them from).
- **Stance coverage.** The recordings show the stance each raider chose. The
  zamorak hasta's recorded swing (`human_axe_chop` 12/12) matches none of our
  stance rows and is left unresolved (the stance is not in the recording).
- **The harness** gives the charged weapons no charges, so the venator bow, the
  glaive and the tentacle revert after their first attack; each first attack
  is the one compared.
