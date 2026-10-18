# Path of Scabaras

Read from 8 visits: 14 (solo, Entry) and 1852 (7 Expert raids). Ticks are
relative to entry (E). Full inventory: [scabaras.out](scabaras.out). Common
entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_scabaras.rs2` (not yet compared).

## Layout

The player is teleported to (3552,5280) on E+0, then to (3522,5279) on E+1
(the room's west entrance). "Challenge started: Path of Scabaras." on E+7.
Room song: `song_738`, from E+7..13.

The room runs west → east in three sections divided by barriers,
`toa_scabaras_barrier` (45335) shape 10 rot 3:

| Section | Holds | Barrier after it |
|---|---|---|
| West, x 3538–3544 | two puzzles, one on the south pad (z 5271–5277) and one on the north pad (z 5283–5289) | x 3549 at z 5274 (south) and z 5286 (north) |
| East, x 3555–3561 | two more puzzles, south and north pads | x 3564 at z 5274 and z 5286 |
| Memory game, x 3569–3573 | always here; statue `toa_scabaras_statue01_3x3` (45205) rot 3 at (3568,5279) | x 3575, z 5272–5288 (17 tiles) |

**Every raid loads all five puzzles.** The four pad puzzles (lights-out,
addition, simon-says, obelisks) are shuffled over the four pads; the memory
game is fixed. A pad puzzle completed opens the barrier tile on its side of
the section (the barrier loc is replaced by `invisible_type8_nonblocking`
(29733) rot 3), with "<col=229628>Puzzle N has been completed!" and synth
`found_gem` (2655). The memory game opens the whole x 3575 wall.

All puzzle locs, barriers and the statue are re-sent (same id) every few
ticks while the room runs.

## Pad puzzles

### Lights-out

| | |
|---|---|
| Locs | floor `toa_scabaras_lightsout_tile_off` (45344) shape 22 rot 0 on 8 tiles of a 3×3 ring (2-tile spacing); a lit tile carries `toa_scabaras_fx01` (45384) shape 10 rot 1 on it |
| Step | the toggled tiles' fx01 is added or replaced by `invisible_type8_nonblocking` (29733) rot 1; synth `toa_pos_p4_light_activate` (6561) |
| Done | every light on: the completion message, light_activate, the barrier tile |

### Addition ("totaltiles")

| | |
|---|---|
| Locs | a 5×5 of number tiles `toa_scabaras_totaltiles_tile1..9` (45345–45353) shape 22 rot 1, and the tablet `toa_scabaras_totaltiles_tablet` (45339) shape 10 rot 0 beside the pad (e.g. (3538,5276)) |
| Target | on reading the tablet: "The number <col=ff3045>N</col> has been hastily chipped into the stone." and synth `toa_pos_p4_number_chipped_stone` (6548) |
| Step | a stepped tile gets an overlay `toa_scabaras_fx03..fx12` (45386–45395) shape 10 rot 1 |
| Done | the sum reached: every tile gets an fx overlay, `found_gem`, light_activate, the barrier tile |

### Simon-says

| | |
|---|---|
| Locs | floor tiles `toa_scabaras_simonsays_tile_up` (45340) shape 22 rot 0 and a button `toa_scabaras_simonsays_button` (45338) shape 10 rot 0 (e.g. (3555,5276)) |
| Show | pressing the button: synth `toa_pos_p2_ancient_button_press` (6559) delay 10; next tick the sequence plays **one tile a tick**: the tile becomes `toa_scabaras_simonsays_tile_down` (45341) shape 10 (and the previous one is deleted back to up) |
| Repeat | each correct step by a raider: that tile becomes `toa_scabaras_simonsays_tile_down_player` (45342) |
| Wrong | synth `toa_pos_p2_stand_on_plate_red_colour` (6549); `toa_pos_p4_fail_puzzle_notification` (6557) and `_fail_puzzle_rumble` (6558) delay 10; "You have failed <col=0cc919>Perfect Scabaras</col>: Z-Kris was hit by a puzzle." |
| Done | the completion message, the barrier tile |

### Obelisks ("guesser")

| | |
|---|---|
| Npcs | six `toa_scabaras_guesser_obelisk` (11698) on a 3×2 grid, 2 tiles apart (e.g. x 3557/3559/3561, z 5283/5289), each on `invisible_type8_blocking_active` (43876) |
| Hit | attacking one retypes it to `toa_scabaras_guesser_obelisk_hit` (11699) for 2 ticks, then back (`level_change` 0, `enabled_ops` 0b11111 each time); area `toa_pos_p4_light_activate` (6561) loops 1 range 5 on its tile. A raid can start with one already `_hit` |
| Done | the completion message, the barrier tile |

## Memory game

| | |
|---|---|
| Locs | 18 plates, `toa_scabaras_memorygame_button1..9` (45356–45364) shape 22 rot 1, two of each, on x 3569/3571/3573 × z 5272/5274/5276 and 5284/5286/5288 |
| Step | the plate turns to its face, `toa_scabaras_memorygame_tile<n>` (button n → tile n); synth `toa_pos_p5_plate_open` (6553) |
| Mismatch | both turn back to `button<n>` **1 tick** later (128 of 150); area `toa_pos_p5_plate_close` (6554) loops 1 range 3 on each |
| Match | they stay as tiles |
| Early | "This device won't function yet." |
| Done | the x 3575 wall opens (17 tiles to `invisible_type8_nonblocking`) |

## Hazards

### Rock falls

Every 6–8 ticks (6 in 10 of 20 gaps): map_anim `ga_beast_rock_fall` (317)
delay 20 on 7–10 tiles near the raiders, with, on one tile of the burst, area
`toa_amp_rocks_fall_03` (6575) loops 1 range 6, `toa_pos_p4_rock_impacts`
(6556) loops 1 range 6 delay 180 and `bear_undead_rock_fall` (4459) loops 1
range 3 delay 155. Also, rarely, map_anim `mm_roofcollapse` (302) delay 30.

### Scarabs

| | Recorded |
|---|---|
| Npc | `toa_scabaras_scarab` (11697) |
| Spawn | from E+20 to the end, at the section edges (e.g. (3533..3535, 5273..5275), (3533..3535, 5285..5287), (3550..3552, 5273..5274), (3550..3551, 5285..5287)); `npc_scarab_spawn` (9589) |
| Attack | `npc_scarab_attack` (9587), every **4 ticks**; projectile `nightmare_ranged_travel` (1766) to the player: start 51, angle 16, slope 64, height 11 → 22; end 59/60/65/70/75/80/85/90 at distance 1–8 (**50 + 5 × distance**, but 59 at 1) |
| Death | `spotanim -1`, `npc_scarab_death` (9590), ops 0b00000 |
