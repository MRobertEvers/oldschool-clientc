# Akkha

Read from 8 kills: 14 (solo, Entry) and 1852 (7 Expert raids). E is the
room's entry; S the tick it starts (the first crystal teleport, "Challenge
started"); C a special's cast. Full inventory: [akkha.out](akkha.out). Common
entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_akkha.rs2`, rebuilt to these recordings
2026-10-19; see [Ours vs recorded](#ours-vs-recorded) at the end.

## Npcs and places

The arena is world (3671..3690, 5398..5417), local (23..42, 22..41) in the
room's square. It is split into quadrants between x 3680|3681 and z
5407|5408: **north-west burn, north-east darkness, south-east lightning,
south-west freeze**. Every flood stops on those lines (15 of 15 memory
blasts, 5 of 5 slams).

| Npc | Id | Where |
|---|---|---|
| Akkha, waiting | `akkha_spawn` (11789) | added on E+0/1 at (3673,5407) (6 of 8; (3673,5406) and (3675,5405) once each); retypes to `akkha_melee` on S |
| His styles | `akkha_melee` (11790), `akkha_range` (11791), `akkha_mage` (11792) | |
| Final stand | `akkha_enrage_spawn` (11793) → `akkha_enrage_initial` (11794) → `akkha_enrage` (11795) | |
| Shadows | `akkha_shadow` (11797); in the final stand `akkha_shadow_enrage` (11798) | south-west tiles (3674,5401), (3674,5412), (3685,5401), (3685,5412) |
| Hourglass bars | `akkha_headbar_npc` (11805), headbar 36 (100 wide) | the corners (3671,5398), (3671,5416), (3689,5398), (3689,5416) |
| Trailing orbs | `akkha_trail_orb_lightning` (11800), `_darkness` (11801), `_burn` (11802), `_freeze` (11803) | |
| Unstable orbs | `akkha_enrage_orb` (11804) | |

He is 3x3; everything below that says "his middle" is his south-west tile
+ (1,1).

## The basic attacks

A swing every **6 ticks** from S+6. Six swings in a style, then a special in
the seventh slot, cast on the tick he takes the next style: melee → ranged
→ magic → melee (solo: every special, 8 of 8). In the Expert raids (Stay
Vigilant) the style also changes between specials. Every change sounds
`bcs_akh_spawn_02` (5585) range 14.

| Style | Swing | Then |
|---|---|---|
| Melee | `npc_akkha_attack_melee_spear` (9770) | the hit the next tick (18 of 18); the raider's own defend animation delay 30 on the swing tick |
| Ranged | `npc_akkha_attack_ranged_spear` (9772) | on swing+2, `akkha_ranged_travel` (2255) from **his middle** to every raider: start 0, angle 12, slope 32, height 101 → 34, **end max(30, 6 × distance)**; synth `toa_akka_non_magic_knife_projectile_01` (5640) delay = the flight; defend animation delay = the flight; the hit on swing+3 (18 of 18) |
| Magic | `npc_akkha_attack_magic_spear` (9774) | the same, `akkha_magic_travel` (2253), **end max(30, 10 × distance)**, spotanim `akkha_magic_impact` (2254) height 124 delay = the flight, synth `toa_akka_projectle_impact_01` (5774) (18 of 18) |

He walks until he is beside his target in every style (14: the recorder at
his edge through ranged and magic). Every hit on him sounds
`toa_akkha_defend_01` (5688) range 9 from his middle, blocks included. He has
no defend animation.

## Specials

Solo: trailing orbs, Memory Blast, trailing orbs, ... from the first (8 of 8
specials). A party: any of the three but the last one (33 of 33, never a
repeat).

### Memory Blast (`npc_akkha_attack_magic_02_spear`, 9777)

| Tick | Recorded (15 of 15 casts) |
|---|---|
| C+2 | "<col=a53fff>Sections of the room start to glow...</col>" |
| C+3 | `npc_akkha_teleport_disappear_spear` (9784) |
| C+4 | he is removed from every client's view (the same npc index is added again when he is back) |
| C+4, +6, ... | a quadrant's symbol glows (`akkha_symbol_glow_burn` 45868 at (3679,5409), `_lightning` 45869 at (3682,5406), `_freeze` 45870 at (3679,5406), `_darkness` 45871 at (3682,5409), shape 10 rot 0), the last one `loc_del`ed the same tick; synth `toa_akka_symbol_glow_04` (5667). Each glow is a quadrant **beside** the last (never the same, never the opposite) |
| 2 after the last glow, then every 4 | a wave: every quadrant but the glowed one explodes; synth `toa_akkha_scatter_across_area_05` (5727). A raider in one is hit the next tick |
| 2 after the last wave | back: `npc_akkha_teleport_appear_spear` (9785) with his south-west tile on (3679\|3680, 5406\|5407) |

4 glows solo at Entry; 5 or 6 in the Expert raids. The next swing is 6 ticks
after he is back.

### Trailing orbs (`npc_akkha_attack_magic_03_spear`, 9778)

C+2 "<col=a53fff>Magical orbs begin to materialise behind you!</col>" and the
raider tinted (0, 605, 0, 0, 0, 112), then (0, 60, 0, 0, 0, 112) every tick;
C+22 "<col=229628>The magical orbs stop materialising.</col>" and the tint
cleared. In between, each tick a raider has moved, an orb of the quadrant it
is in on the tile they stood on at the tick's start, with area
`toa_akkha_black_orb_spawn_02` (5722) range 4. An orb lasts 7 ticks (27 of
28; one went after 4). Standing still makes none (solo 14: none at all).

### Detonate (`npc_akkha_attack_magic_01_spear`, 9776; parties only)

C+2 "<col=a53fff>You have been marked for detonation!</col>" and every raider
tinted (0, T, 8, 1, 100, 112). The blast comes at **C + 10 − 2 × the path's
mechanic tier**: C+6 at Het path level 4-5 (12 of 12), C+8 at level 3 (4 of
4); C+10 below level 2 is the reference's (its tint ends at 245). The tint
lasts until then: T = (delay − 2) × 30 + 5 (125, 185). At the blast every
raider explodes: the element of their own quadrant on the arms of a plus round
them, **3 tiles each way, their own tile not**, tiles off the floor dropped
(12 a plus in open floor), map anims delay 0, and the **element's sound**
range 9 on them (16 of 16):

| Element | Sound |
|---|---|
| Burn | `fireblast_hit` (156) |
| Darkness | `shadow_barrage_impact_short` (5591) |
| Lightning | `godwars_saradomin_magic_impact` (3887) |
| Freeze | `ice_rush_impact` (173) |

### A quadrant's flood

| Element | Map anim |
|---|---|
| Burn (north-west) | `akkha_burn` (2258) height 124 |
| Darkness (north-east) | `akkha_darkness` (2257) |
| Lightning (south-east) | `akkha_lightning` (2256) |
| Freeze (south-west) | `akkha_freeze` (2259) |

75 tiles a quadrant: the octagon's rows run 9, 9, 8, 8, 8, 7, 6, 5, 4, 1 tiles
out from the split. Measured from the quadrant's inner corner (its tile at
the middle: (3680,5407) south-west, (3680,5408) north-west, (3681,5407)
south-east, (3681,5408) north-east), with m the Manhattan distance:

- a memory wave floods outward, delay **30 + 3m** (30 to 66);
- a shadow's slam floods inward, delay **66 − 3m**.

## Shadows

At **80, 60, 40 and 20 %** his hits stop exactly on the threshold (80 / 160 /
240 / 320 / 400 of 400 dealt in 14: the hit that crosses it shows only what
it took) and the next tick:

- the first time "<col=ff289d>Shadows appear throughout the room!</col>", then
  "<col=ff289d>The shadows in the room are restored!</col>";
- a dead shadow is added again with `npc_akkha_spawn_spear` (9790), a living
  one healed; the four headbar npcs are added the first time;
- every hourglass empties.

**Immunity.** He takes nothing (a block, sound included) while his middle is
not in the quadrant of a shadow killed **this time**. After a restore he is
guarded again even where a shadow fell before. The first swing at him while
guarded: "<col=ff289d>Akkha is currently immune to your attacks, but his
shadows are still disrupted.</col>" (once each time they stand).

**Which shadow can die.** Only one a threshold, and only in a quadrant no
shadow has died in before. The rest wear his immunity tint (below) and take
nothing (14 and 1852: the tint per shadow, every tick).

**Tints, every tick.** Akkha: (0, 600, 0, 0, 106, 40) while guarded, else the
clear (0, 0, 255, 255, 255, 0), from E+2 to E+4 of the final stand; not while
he is gone. A shadow: the same tint while it cannot be killed, else the clear.

**The hourglasses.** One charge for all four, a step a tick to 50 (the bar's
fill 2 a step), held from C+4 of a Memory Blast until he is back. **Every
attack on him takes it a step back**, on the swing's tick (14: one a crossbow
swing, every 5 ticks). Corners re-sent every tick: `_empty02` under half,
the plain loc from 25, `_full01` on the slam, `_empty01` for a dead shadow's
corner (`akkha_arena_outer_lvl2_22` north-west, `_23` north-east, `_58`
south-east, `_57` south-west; 45981/45982/46017/46016 and their 46056-46067
variants). A dead shadow's bar is no longer sent.

**The slam.** At 50, every living shadow `npc_akkha_attack_magic_02_spear`
(9777) and `_full01` on the corners; the next tick each living shadow's
quadrant floods inward with `toa_akkha_scatter_across_area_05`. Full for 4
ticks, then from 1 (5 of 5).

A shadow dies with `npc_akkha_death_spear` (9792) and a null SOUND_AREA range
8, ops none, removed 2 ticks later; its corner `_empty01` that tick.

## The final stand

| Tick | Recorded (8 of 8) |
|---|---|
| E (the tick after his last hit) | "<col=ff289d>Akkha makes his final stand!</col>"; every living shadow dies (9792, null sound range 8), every corner `_empty01`; `npc_akkha_attack_magic_04_spear` (9779) and `akkha_enrage_spawn` |
| E+2 | the shadows removed; healed **20 %** (a heal splat, 80 of 400 solo); `akkha_enrage_initial` |
| E+4 | `npc_akkha_teleport_disappear_spear` |
| E+5 | `npc_akkha_teleport_appear_spear` and `akkha_enrage` on a corner's shadow tile, `akkha_shadow_enrage` with 9785 on the other three. The corner is random |
| E+7, every 3 | unstable orbs (below) |

He prays magic and missiles. After **3 successful hits solo**, one more a
raider, he and one shadow `npc_akkha_teleport_disappear_spear` on the hit's
tick and appear in each other's corner the next, both retyped to themselves
(41 swaps). The shadows wear his immunity tint every tick.

**Unstable orbs.** 5, 6, 7 or 8 a wave (8, 27, 56, 38 of 129 waves), at least
one in every quadrant, from **56 fixed tiles** one ring outside the floor,
each leaving by one of its two or three ways in (898 orbs; the table is
`toa_akkha_orb_tiles` in toa.enum). One tile a tick until it leaves the ring.
An orb that reaches a raider strikes on its next step: spotanim
`akkha_enrage_orb_impact` (2260) height 158 delay 10 on the orb, area
`toa_akkha_unstable_orb_impact_01` (5735) range 4 delay 10 on the raider, the
hit that tick, the orb removed the tick after. Hits: 9 at Entry through
Protect from Magic, 16-25 in the Expert raids.

**Death.** The killing hit at D−1; on D his `npc_akkha_death_spear`, every orb
removed, every shadow 9792 with the null sound; on D+2 everything removed,
"Challenge complete", his ashes (`toa_akkha_ashes`) and `toa_book_akkha` on
his middle, and Osmumten (`toa_osmumten_akkha`, `ghost_summon2_priority`).

## The quadrant riders

Recorded only for ice: "<col=ff3045>Your attacks have been slowed by some
ice!</col>" with the hit, "<col=229628>The ice around you melts away.</col>"
25 ticks later. The others are the wiki's: burn (5 a hit, several hits),
darkness (combat stats −3, low rapid damage), lightning (protection prayers
off for a while).

## Ours vs recorded

Rebuilt 2026-10-19: `toa_akkha.rs2` (built on entry like Kephri; the room
tick behind `^toa_var_ak_tick`; the chase in `[ai_timer]`), the Akkha blocks
of `toa.constant`, `toa.npc`, `toa.enum` (`toa_akkha_orb_tiles`), the Akkha
branch of `~player_hit_npc_prepare`, and a new engine command `npc_tint`
(opcode 11162: NPC_INFO's TINTING block, flag 0x100, RSProt's
`NpcTintingEncoder`), drawn by the client as the player's tint is.
Checked with `test/raids/toa_akkha_tells.lua` (scriptrun, solo, raid level 0:
the crystal, his swings, the trailing orbs walked through, a forced
detonation, `::toaakkhanext` to each threshold and the guarding shadow,
the final stand, a swap, the kill: 15 of 15) and
`tools/rsprox_tells.py --anchor 11789-11805` against 14.

| | Status |
|---|---|
| Room | **rebuilt**: the old script spawned `akkha_melee` on start at the wrong centre ((41,32) for (32,31)), had no shadows, no floods, no orbs and a final stand nothing ever entered |
| Clock and styles | **fixed**: S+6, six swings and a special, retype and `bcs_akh_spawn_02` on the special; melee lands +1, ranged and magic thrown +2 from his middle with the recorded flights, synths, impact and defend delays, landing +3 |
| Specials | **fixed**: solo trail/memory alternating from a trail; a party never repeats; Memory Blast (glows, waves, the removal from view by a storey down and back, the middle tile; 4 + min(2, path / 2) glows, 4 / 5 / 6 at path levels 0 / 3 / 4-5), trailing orbs (tint, an orb per step, 7 ticks), detonation (the plus, arms 3, the delay by path tier and the tint to it, the element's sound) |
| Floods | **fixed**: the octagon (75 tiles), 30 + 3m outward, 66 − 3m inward |
| Shadows | **fixed**: hits stop on each threshold; appear/restore; immunity by the dead quadrant this time; one killable shadow a threshold, never a spent quadrant; the tints both ways every tick; the charge (held through a Memory Blast, a step back a swing at him), corners, bars and the slam |
| Final stand | **fixed**: E to E+7 as recorded, the heal splat, corners, the swaps, the 56-tile orbs and their strike; the death on D with orbs and shadows, drops and Osmumten on D+2 |
| Not recorded, from the wiki | the heavy hits' damage (30 base; recorded 15 at Entry, 33 Expert), the burn, darkness and lightning riders, Feeling Special's orb in front |
| Not compared | hitsplats, messages, songs and interfaces are not in our tick log |

### The full inventory, 2026-10-19

`rsprox_tells.py --anchor 11789-11805` over all 8 kills, every section walked.
Everything not in the table above is the recorder's kit (weapons, prayers,
potions, levels, League relic graphics), engine-wide (the per-viewer hitsplat
variants 12/13/16/17/43), or raid-wide. The raid-wide ones are now built for
every room, in `toa_hud.rs2` and `toa_supply_bag.rs2`:

| Raid-wide tell | Recorded | Ours |
|---|---|---|
| `toa_hud` (481) in `overlay_atmosphere`; names (clientscript 6585); timer (clientscript 6580) | the HUD for the whole raid; the timer every tick: (0, 1) before the first path, (elapsed, 0) while running, (final, 1) frozen after the Wardens (14: 4377 of 4377 ticks) | mounted on enter, join and re-entry, and again after each fade (which borrows the slot); the timer from the watchdog |
| `toa_client_p0`..`p7` (14346-14353), `toa_client_partyslot` (14354) | each slot's health: 0 empty, 1 + hp × 26 / max (27 full), 30 dead, 31 in a fade | Nexus registers 60-67 hold each slot's uid and 68-75 its code; each raider's watchdog publishes their own code and writes all eight |
| `toa_damage_done` / `_current` (14325 / 14374), `toa_damage_taken` / `_current` (14323 / 14375) | dealt and taken, the raid's total (16 bits) and this room's (15 bits); the room's reset at the challenge start | `~npc_default_damage`, Akkha's last hit, Het's seal, and `combat_player_hit_received`; reset in `~toa_challenge_started` |
| Helpful Spirit, `toa_midraid_loot` (777), the Supplies bag (item 27314, inv 810, interface 778 at 164:71) | NOT opened on room entry (an earlier version of this table was wrong): the spirit arrives after the 2nd and 4th path; talking rolls three bundles; the bag is opened by its own Open op | the spirit, the roll (22 / 13 / 5 scaled, rounded), the choice, the bag's five ops, its slot ops 1-4 and 9, use-on to store, and "The bag disintegrates." (`test/raids/toa_supply_hud.lua`) |
| `toa_midraidloot_stats_timer` (14344) | the smelling salts' 25-tick steps left (32 on a dose) | set on a dose and each 25-tick step |
| Music | the room's song from the challenge start, the Nexus song (730) otherwise, 147 before the Wardens, 731 after; re-sent every 3 ticks | `~toa_current_song`, re-sent from the watchdog every 3 ticks; ours has no fade fields on the first send |

The base varps of 14344, 14354 and the four damage varbits (3586, 3587, 3589,
3591, 3652) were undeclared, so they were never sent to the client. They are
declared `transmit=yes` in `toa.varp` now.

Two `player_tinting` rows stay unexplained: (0, 60, 0, 0, 15, 112), once each
in two trailing-orb windows (1852, +158 and +142), the tick's other tint being
the trail's own (lightness 0).

