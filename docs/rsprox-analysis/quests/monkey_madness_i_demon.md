# Monkey Madness I: the 10th squad sigil and the Jungle Demon

Source: **RSProx index 4565** (rev 242.1, world 386, 2026-11-13, normal world, the
owner's "Monkey Madness Boss"). It is the only recording of the fight on the index:
`search npc 1443`, `search npc 1434`, `search npc 1442`, `search npc 5284`,
`search loc 5800`, `search loc 5798` all return 4565 alone; `search mapsquare
10894`/`10895` add 4562 and 4563, which pass through the squares below the arena
and never meet the demon, the squad, Bonzara, a trapdoor or a crack. (The brief's
square 10894 is (42,142); the arena, x 2688..2751 z 9152..9215, is square 10895.)
One recording is one roll: every count below is from it, both visits pooled.

Transcripts: `~/.rsprox-index-cache/home-{default,bas,raw}/.rsprox/binary/archive/idx4565.txt`.
The raw transcript prints the arena's tiles as the default one does: the arena is the
real map, not an instance copy. Our side: `test/quests/_mm_demon.lua`, run through
`tools/quest_gate/run.py --script ... --name mm_demon` (tick log
`build/quest_gate/mm_demon/ticklog.tsv`).

Script: `OSRS-Content/osrs-content/server/scripts/quests/quest_mm/scripts/mm_demon.rs2`
(rewritten; its header listed the battle as "this pack's owner-private design").
Config: `quest_mm.npc` `[mm_demon]`.

## 1. Actions in the window [1741]..[2179] (rsprox_actions.py)

| first tick | n | action | verdict |
|---|---|---|---|
| [1741] | 3 | `if_buttonx inventory mm_sigil op=3` (Wear = opheld2) | **this content**, §3 ([1939]'s menu was shut by the recorder's own walk) |
| [1782] | 3 | `prayerbook:prayer13` | recorder's kit (Protect from Magic) |
| [1787] | 1 | wield `adamant_knife` | recorder's kit (thrown, `human_stake2_pvn`, `adamant_tknife_*`) |
| [1790] | 3 | eat `shark` | generic, not this content |
| [1795] | 21 | `opnpc2 mm_demon` | **this content**, §4-§6 |
| [1801] [1995] [2000] | 3 | prayer restore sips | generic |
| [1837] | 1 | stamina sip | generic |
| [1859] | 1 | `oploc1 mm_trapdoor_ne` | **this content**, §8 |
| [1866] | 1 | `oploc1 mm_trapdoor_ne_open` | **this content**, §8 |
| [1890] | 1 | `oploc1 mm_wallcrack_center` | **this content**, §8 |
| [1914] | 1 | `opnpc1 mm_bonzara` | **this content**, §8 |
| [2069] | 4 | `opnpc1 mm_zooknock_final_battle` | **this content**, §7 |
| [2153] | 1 | `opnpc1 mm_garkor_final_battle` | **this content**, §7 |

| teleport | from -> to | cause | ours |
|---|---|---|---|
| [1755] | (2805,2761,0) -> (2702,9173,1) | the sigil, C+10 | C+10, same tile (test row `sigil1.arena`) |
| [1870] | (2741,9205,1) -> (2739,9205,0) | open trapdoor, O+4 | same tile (`trapdoor.landing`) |
| [1905] | (2715,9184,0) -> (2415,9908,0) | centre crack, O+4 | same tile (`crack.landing`) |
| [1931] | (2414,9911,0) -> (2705,2782,0) | Bonzara, B+4 | same tile (`bonzara.landing`); was a random tile in 7 of (2712,2784) |
| [1963] | (2725,2780,0) -> (2702,9173,1) | the sigil, C+10 | same (`sigil2.arena`) |
| [2179] | (2699,9171,1) -> (2715,2786,0) | Zooknock, A+3 | same tile; was a random tile in 7 of (2712,2784) |

## 2. The arena's npcs and their configs

| type | cache (all.npc) | server block before | now |
|---|---|---|---|
| mm_demon 1443 | size 3, turnspeed 16, op2 Attack, stat1..4,6 = 170, strengthbonus 50, slash/magic defence 50, no attack bonus | quest_mm.npc: attackrange 8, stab/slash attack 50 (invented), generated `defaultmode=wander, wanderrange=5, maxrange=7` | attackrange 10, stab/slash 0, `param=death_script,yes`; the generated movement lines need removing (Open) |
| the nine `mm_*_final_battle` 1434..1442 | no op2; Garkor and Zooknock op1 Talk-to | generated: wander 5, hitpoints 10, null sounds | driven by script in mode `none`; config left (every key is stated by the generated block: Open) |
| mm_bonzara 5284 | op1 Talk-to | generated wander 5 + npc_movement indoors; also spawned by our script in the arena | arena spawn deleted; world spawn tile and wander are not this file's (Open) |

## 3. The sigil and the arrival (C = the "Yes" click: [1745], [1953])

| tick | recorded | ours (run 1) | verdict |
|---|---|---|---|
| click | chatmenu "Let the sigil teleport you when worn?" `Yes|No.`, busy 1 | `~p_choice2_header("Yes", 1, "No.", 2, ...)`; was "Let the sigil teleport you" / "Not yet" | fixed |
| C | "The 10th squad sigil begins to shake violently!", `sound_area rumbling (1678)` r5 at the player, `cam_shake 0,10,0,0`, menu shut | rumbling area r5 at C (tick 40) | fixed (was a synth, then `p_delay(5)` from the shake) |
| C | the sigil is NOT worn (no worn-slot change, 2/2) | not worn | fixed (ours equipped it) |
| C+6 | `teleport_casting` 92, `human_castteleport` (absent while a monkey, [1751]; present [1959]), `sound_area teleport_all (200)` r5, `cam_reset`, the black (948 0,255,0,0,50), `midi showdown (311)` 0/60/60/0 | all at C+6 (tick 46; no seq as a monkey) | fixed (ours: synth, black at C+6 with the cast at C+8) |
| C+10 | the move to (2702,9173,1), `minimap_toggle 0` | tick 50 | fixed (ours C+11) |
| C+11 | the black lifts (948 0,0,0,255,50), `synth rumbling_loop (1686)` x4, `cam_shake`, `cam_moveto (2715,9183) 300 232/232`, `cam_lookat (2715,9186) 150 232/232`, `cam_lookat ... 10/10`, `cam_moveto (2715,9177) 1000 2/0` | tick 51; same camera lines | matched (camera tiles were already right) |
| C+27 | nine squad adds, spawn angle south, `smokepuff` 50 each; `cam_reset` | tick 67 | fixed (ours added them at a fixed set of tiles at C+30) |
| C+27 (visit 1) | greegree to the backpack, `windwave_impact` 124, `monkey_into_human (1681)`, `equip_fun (2238)`, mesbox "The Monkey Greegree wrenches itself from your hand as its power begins to fade..." | run 1: mesbox at 67 but the form's spotanim at 72 and no synths (the greegree timer waits for the scene) | fixed in run 2 (the revert and both synths in the scene) |
| page | overlay shut, Garkor `chatneu2` "Ready yourself, human: the final battle begins!" (visit 2: "... begins... again!") | both pages (test rows `sigil1-pages`, `sigil2-pages`) | matched |
| page click | demon add (spawn angle south) + `demon_appear (1418)`; `hint_arrow reset`, `hint_arrow npc`; `midi showdown` again; busy 0 | add + demon_appear + showdown on the click (tick 72) | matched |
| add+1 | its first wave ([1779], [1984]) | run 1 cast on the add tick | fixed in run 2 |

Squad tiles at the add, relative to the landing (2702,9173): visit 1 dx -5..+4, dz -4..+5,
three on one tile (2700,9178); visit 2 dx -5..+3, dz -5..+5. Ours: `map_findsquare(landing, 0, 5)` each.
Demon SW tile: (2701,9183) and (2704,9180); ours `map_findsquare((2702,9181), 0, 2)` (fitted to 2).

## 4. The demon (npc info, §9; projectiles, §4; sounds, §5/§6)

| item | recorded (4565) | ours (run 2/3) | verdict |
|---|---|---|---|
| adds | 2 spawns with `demon_appear` (spawn angle south), 3 re-adds into view | 2 spawns, `demon_appear` | matched |
| attack | waves only: 35 casts, 0 swings (the recorder stood 2..11 from its 3x3, never adjacent) | waves; the melee at reach 2 is the wiki's (unverified) | matched for waves |
| rate | 6 ticks between casts in reach (29/35 gaps = 6; the rest were the player out of reach) | 6 (58/58) | matched |
| first cast | add+1 ([1779], [1984]) | run 1 on the add tick; run 2 add+1 (73, 282) | fixed |
| reach | casts at footprint distance <= 10 (1779, 2098, 2104, 2117 at 10), a step toward the player at 11+ ([1839]..[1869], [2105]..[2116]) | `attackrange` 10 (was 8) | fixed |
| spell | uniform over four: fire 11, wind 9, water 8, earth 7 | `random(4)` | matched |
| cast | `demon_casting (69)` + `<elem>wave_casting` 92 on the demon | same | matched |
| projectile | `map_projanim_v2 <elem>wave_travel`, source = its SW tile, homing on the player, starttime 51, **endtime = 26 + 20 x Chebyshev(centre, player)** (35/35: d 4..11 -> 106..246), angle 16, progress 64, heights 350 -> 124 raw | `projanim_pl_raw(npc_coord, uid, .., 350, 124, 51, 26+20d, 16, 64)`; run 2: 58/58 on the formula | fixed (was the shared spell table's flight) |
| impact | on the cast tick, on the player: `<elem>wave_impact` 124 delayed to endtime (accurate) or `failedspell_impact` 124 (splash); synth `<elem>_wave_all` / `_fail` with the same delay | same | fixed (the shared proc turned a prayed hit into a splash) |
| block anim on the player | none (26/26 cast ticks without the player's own attack carry no player seq) | none | fixed (the shared proc sends the worn weapon's block delayed to the landing) |
| landing | splat floor(endtime / 30) ticks after the cast on every accurate cast; none on a splash | 46/46 accurate on the tick, 12/12 splashes without a splat | matched |
| facing | `[facing] player=` on the first cast and re-sent on every tick the player moves (74 lines) | one face block per life (4 rows) | Open: engine face-entity re-send |
| block on hits | `demon_block` delay = the hit's flight (47/52 knives, 56..76 squad shots, 0 melee) | same delays | matched |
| hit sounds | the recorder's hits: `synth_sound black_demon_hit` (to the dealer); the squad's: `sound_area black_demon_hit` r15 at its SW tile, one per hit | squad: one area r15 per hit (run 2: 53/53); player's hits: an npc-sourced area sound r12 | squad fixed; the player's is the engine's (Open) |
| kill (K = the killing splat [2133]) | K+1: `demon_death (67)`, `[facing] type=reset`, `enabled_ops 0b00000`, mm_main 5 -> 6, `midi silence (147)` 0/0/0/0, `midi_jingle (134)` (ours `mission_over_monkey_madness`), `synth black_demon_death (398)`; six of the squad deleted with `map_anim smokepuff` 50 on their tiles (Garkor, Zooknock, Lumdo stay and keep walking to [2179]) | all at K+1 (run 2: 546) except the face reset | matched; face reset Open (§9) |
| corpse | del at K+7 (seq+6) with `malicious_ashes` at its CENTRE tile (2714,9172), ownership self, reveal 100, despawn 200 | run 2 seq+5, fixed to seq+6 for run 3; centre tile, `obj_add_private(.., ^lootdrop_duration, 100)` | fixed (was the SW tile, at the kill) |
| restore | the squad's 1 that took it to 0 ([2079], bar 0) left it on exactly 35 by [2082] (the bars 6,6,6,6,6,5,5,5,5,5,4 after it fit 35 only), no heal splat, no death; the squad keeps hitting under 10 HP (5 -> 1 at [2065]..[2074]) | `death_script` + `~mm_demon_gnome_hit`: statsub 1, hitmark (bar 0), statheal to 35; run 3 drives it with `::mm_demon_hp 2` | fixed (ours stopped the squad at 10 HP and healed 21..42 instead) |

## 5. Damage (§13)

| attack | recorded n | accurate | zero % | max | prayed | ours (run 2, no armour) | verdict |
|---|---|---|---|---|---|---|---|
| wave, cast unprayed | 1 | 1 | 0 | 17 ([1779] -> [1787]) | - | (run 1, prayer toggled off by the test: 13 unprayed, max 30) | max unverified (wiki 32) |
| wave, Protect from Magic | 34 | 18 | 18/18 = 100% | 0 | 0 (100% protection) | 46 accurate, 46/46 = 0 | matched |
| splash | 16/35 = 46% | - | no splat | - | - | 12/58 = 21% | consistent: predicted 38% for the recorder's kit (magic def +93: hit roll (170+9) x 64 = 11456 vs (35+13+8) x (93+64) = 8792 -> P(hit) 0.616, 21.6 of 35 expected, 19 recorded) and 16% for ours (no armour, P(hit) 0.844) |
| melee | 0 | | | | | | unverified (never in reach) |

The demon's levels are the cache's (stat1..4,6 = 170, strengthbonus 50, slash/magic defence 50).
Our block's stab/slash attack 50 were invented: now 0.

## 6. The 10th squad (§9, §13b)

Every squad attack is a hit of exactly 1 on the demon (68 of 68 splats `17`, none blocked;
68 attacks = 68 hits). The recorder dealt the rest (35 splats 1..9, 5 at the knives' max 10, 24 blocks).

| gnome | recorded attacks (both visits) | seq | shot | reach (footprint distance at the attack) |
|---|---|---|---|---|
| Garkor, Lumo, Bunkdo | 5, 11, 10 | `gnome_attacksword (12045)` | none: hit on the swing's tick, `demon_block` delay 0 | 1, from a side (0 corners) |
| Karam | 9 | `m_assassin_attack (1413)` | none | 1 |
| Carado, Lumdo | 4, 3 | `gnome_attackbow (12043)` + `iron_arrow_launch (18)` 48 | `iron_arrow_travel (9)`, no impact spotanim | 1..5 |
| Zooknock | 14 | `m_mage_attack (1415)` | `fireblast_travel (130)` 9 / `waterblast_travel (136)` 5, impact `fireblast_impact (131)` / `waterblast_impact (137)` | 1..5 |
| Waymottin, Bunkwicket | 5, 7 | `m_mage_attack` | `vial_travel (49)`, impact `explodingvial (176)` | 2..5 |

- Shots: `map_projanim_v2`, starttime 25, **endtime = 46 + 5 x Chebyshev(gnome, demon centre)** (d 2..6 -> 56..76, all four kinds), angle 16, progress 64, heights 100 -> 200 raw, target the demon's CENTRE TILE (no npc: not homing). On the shot's tick the demon gets `demon_block` delay = endtime and, for a spell, the impact at height 200 with the same delay; the splat lands floor(endtime / 30) ticks on.
- Rate: 9 ticks is the shortest gap between one gnome's attacks (most melee gaps 9..11); between attacks every gnome walks straight legs a tile a tick about x 2691..2715, z 9167..9187. Visit 2 (151 ticks): 42 attacks, one per ~32 ticks per gnome; ours run 2 fight 1: 23 in 74 ticks (one per ~29).
- Facing: melee gnomes `[facing] npc=demon` on every step in and the attack (Garkor 15, Lumo 15, Karam 10, Bunkdo 9 steps in visit 2); bows and mages only on the shot; `type=reset` exactly 2 ticks after every attack (41/41); never the player, bar Garkor and Zooknock when spoken to.
- Ours before: the squad ran on the generated wander default, never attacked; the demon's own `ai_timer` dealt a 3..10 "gnome" hit every 6 ticks with no seq, projectile or splat source, stopped at 10 HP and healed 21..42 on a would-be kill.
- Ours now: `~mm_squad_tick` per gnome (mode `none`, a 1-tick timer): legs, going in, the attack with the recorded seq / launch / projectile / impact, the hit 1 via `[ai_queue4,mm_demon]` on the landing tick, `npc_facenpc` on melee steps and every attack, `npc_facereset` at +2.
- Run 2 found ours turning every gnome to the player (20..64 face blocks each): `npc_setowner` makes the engine re-latch an idle owned npc onto its owner. Run 3 adds them without it.
- Run 3 showed the other half: an unowned gnome's `npc_findall` cannot see an owner-private demon (no squad attack at all). Run 4 adds the demon public too; every fight npc carries `%mm_attacking_uid` = its summoner, and every search and the cleanup filter on it. Run 4: squad faces 58 demon / 57 reset / 2 player (Garkor and Zooknock spoken to; recorded 3).
- Run 4 drove the restore with `::mm_demon_hp 2` (auto retaliate off): at tick 295 a squad shot took it to 0 and it stood on 35 (`npc_heal 35 [ai_queue4,mm_demon]>[proc,mm_demon_gnome_hit]`), no death; it died to the player's knives at 380.

Movement (npc_movement_audit.py paths, run 4): every squad type walks a tile a tick (gap 1 dominant on both sides), diagonal 31..45% ours vs 31..64% recorded, undo 0..5% vs 4..10%; the demon 26 chase steps vs 25. Block kinds (npc_block_inventory.py): **kinds recorded and never sent: 0**.

## 7. The squad's pages (§10)

| tick | recorded | ours | verdict |
|---|---|---|---|
| [2072] | Zooknock, in the fight: "Stop talking and start fighting!" `chatang1`; he keeps fighting (no turn to the player) | `^chat_angry`, same line; our op turns him (engine) | page matched; the turn is the engine's op face |
| [2146] | Zooknock after the kill, mm_garkor 6: "You should speak to the Sergeant." `chatneu1` | same (test `zooknock1-dialog`) | matched |
| [2156] | Garkor `chathap2` "Well done, human! That was a most impressive display of skill." | `^chat_happy` | matched |
| [2158] | player `chatneu1` "Thank you." | same | matched |
| [2159] | Garkor `chatneu3` "You should report to King Narnode immediately. Tell him that the 10th squad still survives and has suffered no casualties." | same | matched |
| [2162] | player `chatneu2` "Rest assured, I will do so. How do I leave this place?" -- ONE page | one page | fixed (ours: two pages, the second `chatquiz`) |
| [2164] | mm_garkor 6 -> 7 with Garkor `chatneu1` "Speak to Zooknock. He will arrange for you to leave." | same | matched (test `garkor.varbit`) |
| [2167] | on its click: `hint_arrow reset`, `hint_arrow npc=Zooknock` | `hint_stop; hint_npc` on Zooknock | matched (the kill's arrow on Garkor was ours alone: deleted) |
| [2174] | Zooknock `chatneu1` "Well done, human. Bear with me now." | same | matched |
| A = [2176] | `human_castteleport`, `teleport_casting` 92, the black, `synth teleport_all`, page shut | same (596) | fixed (ours: no cast, a bare telejump a tick later) |
| A+3 / A+4 / A+6 | the move to (2715,2786,0) + arrow cleared / seq -1, the black lifts, busy 0 / overlay shut | 599 / 600 / 602 | fixed (was a random tile within 7 of (2712,2784)) |

## 8. The way out between visits (§1, §2)

| tick | recorded | ours | verdict |
|---|---|---|---|
| [1863] L+1 | `human_pickupfloor`, face the trapdoor | 175 | matched |
| [1864] L+2 | "The trapdoor opens...", `loc_add_change mm_trapdoor_ne_open` (2741,9205,1) shape 22 rot 2, `synth door_open` | 176 | matched (how long it stays open: unverified, 500) |
| [1866] O | "You climb down the trapdoor.", the black; busy from O+1 | `~mm_fade_move` | fixed (ours moved at O+3, lifted at O+4, shut at O+7) |
| O+4 / O+5 / O+9 | (2739,9205,0) / the black lifts / overlay shut, busy 0 | 186 = O+4 | matched |
| [1901] O | "You enter the crack in the wall.", the black; O+4 (2415,9908,0); O+5 lift; O+9 shut | same shape (221) | fixed |
| [1914] | Bonzara `chatquiz2` "It looks like you're trying to escape. Would you like some help?"; "Select an option" `Yes|No` | same | matched |
| [1925] [1926] | player `chatcon1` "I ... uh ... yes."; Bonzara `chatneu1` "Right you are." | same | matched |
| B = [1927] | `human_castteleport`, `teleport_casting` 92, the black, `synth teleport_all`, page shut | 234 | fixed (ours: none of it) |
| B+4 / B+5 / B+7 | (2705,2782,0), Bonzara gone from view / seq -1, the black lifts, busy 0 / overlay shut | 238 / 239 / 241 | fixed (was random within 7 of (2712,2784) at B+2) |
| Bonzara | one world npc at (2414,9912,0), spawn angle north-west, idle 26 ticks, faces the player on his page | ours: world spawn (2415,9910,0) (m37_154.spawn, not this file) AND a second one our sigil added in the arena | arena copy deleted; spawn tile Open |

The other three trapdoors and five cracks share the recorded shape; their landings are the
earlier port's (LostCity), unverified.

## 9. Runs (`python3 tools/quest_gate/run.py --script test/quests/_mm_demon.lua --name mm_demon --fixture fresh_lumbridge.ini --no-publish`)

| run | result | what it found |
|---|---|---|
| 1 | 20 PASS, 1 FAIL (`player.died`) | the test's prayer helper toggled Protect from Magic off in fight 2; first cast on the add tick; two hit sounds per squad hit; the greegree's revert 5 ticks late |
| 2 | 30/30 PASS | the squad turned to the player on every idle turn (`npc_setowner`); del at seq+5; the restore never reached |
| 3 | 29 PASS, 1 FAIL (`fight2.dead`) | unowned gnomes cannot find an owned demon (no squad attack); auto retaliate killed the 2-HP demon first |
| 4 | **30/30 PASS** (`SUMMARY 30 PASS 427 exit=0 pass=30 fail=0`) | the restore at 295, del seq+6, squad faces as recorded |

## 10. Residual audit (§13b, rsprox_hit_causes.py --by-prayer)

On the player: 19 wave splats (18 prayed 0, one 17 cast before the prayer went up), poison 7 and
the snakes, insects and spider at [1880], [1932]..[1945], [2182]..[2186] outside the arena (Ape
Atoll's own npcs, not this content). On the demon: the squad's 68 1s (content, §6), the
recorder's knives (kit). No heal splat anywhere; the one silent heal is the squad restore (§4).

## 11. Open

1. **The demon's `maxrange=7`** (generated block): our demon gives up a chase 7 tiles from where it
   appeared and walks back; the recorded one followed the player 29 tiles to the NE trapdoor
   ([1839]..[1869]). Needs `maxrange` (64 covers the arena) authored in `quest_mm.npc` and the
   generated line deleted -- see the report's edits. Likewise `defaultmode=wander`/`wanderrange=5`
   for the demon, the nine squad types (the script holds them in mode `none`, so this is the
   config's honesty and the movement audit's flag) and Bonzara (recorded idle; one 26-tick view).
2. **Engine: the npc face-entity re-send.** Jagex re-sends `[facing] player=` each tick the target
   moves (74 lines for the demon); ours sends it once per latch.
3. **Engine: the death tick's face reset.** `[ai_queue3]` sets mode `none` and resets the face, but
   the still-latched `applayer2` re-faces the player in the same npc phase and `npc_face_settle`
   drops the unchanged latch: no `type=reset` with `demon_death` (recorded [2134]).
4. **Engine: the player's hit sound on an npc.** Recorded: `synth_sound black_demon_hit` to the
   dealer; ours: an npc-sourced area sound, range 12.
5. **Music areas** (not this file): Bonzara's room re-sends Showdown (311) at O+9 ([1910]), ours
   plays 296; the landing outside (2705,2782,0) plays `monkey_madness` (303) at [1932], ours 306.
6. **Bonzara's world spawn** (`areas/world/configs/m37_154.spawn`, generated): recorded
   (2414,9912,0) facing north-west; ours (2415,9910,0).
7. **Single rolls:** the restore value 35 (fixed or rolled?), the demon's spawn box (2 samples),
   Lumdo staying with Garkor and Zooknock after the kill, Bonzara's and Zooknock's landing tiles,
   the squad's walk legs and engage odds (a fit to one recording's rates, not a law).
8. **Unverified, no recording:** the demon's melee (reach 2, half the time) and both max hits (32),
   the sigil's "No." and its off-stage Wear (both now wear it), the wilderness line, Bonzara's No
   branch (wiki transcript), Garkor's lines before the kill and after mm_garkor 7, the trapdoor's
   open time, the other landings, the elemental weakness and xp multiplier.
9. **The bar on a lone restoring hit:** recorded fill 0 at [2079]; run 4's restore tick carried two
   squad hits, so ours showed the tick's last bar (6). Not yet seen alone on our wire.
10. **The greegree's own removal** (mm_greegree.rs2, not this file): `~force_unequip_greegree` and
    the timer's revert send no `monkey_into_human`/`equip_fun` and revert a tick or more after the
    unequip; this scene now sends them itself.
