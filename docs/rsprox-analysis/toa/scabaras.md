# Path of Scabaras

Read from 8 visits: 14 (solo, Entry) and 1852 (7 Expert raids). Ticks are
relative to entry (E). Full inventory: [scabaras.out](scabaras.out). Common
entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_scabaras.rs2`, rebuilt to these recordings 2026-10-18; see [Ours vs recorded](#ours-vs-recorded) at the end.

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

## Measured from the packets (2026-10-18)

Read with a scanner over the raw transcripts that keeps the recording
player's tile before and after every tick (8 visits). Other raiders' tiles are
not in 1852's player info; where a rule needed them it says so.

**Entering and the start.** The way in is the path barrier (45135) at
square-local (12,26) or (12,38), Quick-Pass. A raider who walks to it arrives
on x 11, faces it for a tick and is moved two tiles to x 13 on the next (8 of
8). The room starts on that crossing in the solo visit (1 of 1); in the party
visits it started earlier, by another raider's crossing. Our start is the
crossing, not a radius.

**Which puzzle is "Puzzle N".** A pad's puzzle opens the barrier on the other
side of its own section, and N names that barrier: the west north pad opens
(29,26) and is "Puzzle 1", west south opens (29,38) "Puzzle 2", east north
opens (44,26) "Puzzle 3", east south opens (44,38) "Puzzle 4" (8 of 8 visits,
every completion). A party splits and each half lets the other through. The
solo raider solves one pad a section and crosses: the crawl hole
(`toa_scabaras_spawn_hole` 45343 at (28,35)/(28,29), `human_longcrawl`,
`climbing_loop` loops 2 delay 4, out on (28,28) two ticks later) and the
stepping stone (45396 at (40,32), Jump-to from three tiles away,
`human_spot_jump` delay 15 then 0, synth `jump` delay 20 then 5, two ticks
apart). The memory game opened in the solo visit with two pads done, so it is
not gated on all four.

**A pad that does not work yet.** The solo raider solved the west north pad
(Puzzle 1, the south lane's way east), crawled south, solved the south-east
addition (Puzzle 4) and jumped the stone into the north-east section, whose
own way in, (29,38), was still shut. Running from the stone (40,35) to (42,37)
crossed the north-east pad's lights-out plate (41,36); on the next tick "This
device won't function yet." and no plate flipped (1 of 1). So an east pad
works once the barrier into its section is open (the south-east behind
Puzzle 1, the north-east behind Puzzle 2); "yet" is that barrier. A party
reaches the east pads through those barriers and never sees it. The one
recorded case was a run's middle tile.

**The party statue.** With a party the server replaces the map's statue at
(49,31) by `invisible_type8_nonblocking` and adds `toa_scabaras_statue01_3x3`
one tile west, closing the way between the memory boards (7 of 7 party visits;
nothing in the solo visit).

**The build.** Each pad's 5x7 (board x..x+4, z-1..z+5) gets
`floorkit_tomb_basic01` rot 0, which the puzzle's own tiles then replace.
Boards (south-west tile): west (20,24)/(20,36), east (37,24)/(37,36).

**Lights-out.** A raider arriving on ring plate k (clockwise from the
north-west) flips plates k-1, k and k+1, every recorded transition (7
visits). It counts on the tick after the raider ends a tick on the plate (12
of 12 recorder steps; run-through tiles were never ring plates in the data).
The board starts at a random state (2-6 of 8 lit) and the whole board is
re-sent on every change.

**Addition.** The 25 numbers are the same in every raid (8 of 8), north row
first `9 3 7 2 9 / 7 6 4 1 1 / 8 6 3 8 5 / 6 2 7 2 4 / 1 9 5 4 3`, only the pad
moves. Every plate walked over counts, including the middle tile of a run (8
of 8 solo steps), the tick after; its overlay is fixed by its number (1-6 ->
fx05-fx10, 7 -> fx03, 8 -> fx11, 9 -> fx12; 25 of 25). The tablet sits at
(board x-2, z+4). Targets recorded: 22, 31, 32, 33, 39. No recording goes over.

**Simon-says.** The diamond's nine plates are (0,2) (1,1) (1,3) (2,0) (2,2)
(2,4) (3,1) (3,3) (4,2) of the board; the button at (-2,4). A push shows five
different plates (10 of 10 shows), one a tick from the next tick, each
`tile_down` added as the previous one goes. A step counts on the plate the
raider ended the tick on (a run across a wrong plate did not fail it); a
stepped plate is `tile_down_player` for 2 ticks. A wrong plate: on that tick
`tile_down_player`, map_anim `mm_roofcollapse` delay 30 on it, synths
`fail_puzzle_notification`, `fail_puzzle_rumble` delay 10 and `rumbling`, a
camera shake; the next tick a hit and "You have failed Perfect Scabaras: <name>
was hit by a puzzle." (3 of 3); the sequence is gone until the button is pushed
again (a new one). Solved: all nine `tile_down`, `stand_on_plate_red_colour`,
`found_gem`.

**Puzzle hits.** 4 at Entry, 12 and 12 at Expert: 4-5 times the damage
factor (Expert at the 250% cap).

**Obelisks.** Six on (board x+0/2/4, z-1/z+5); the one at (x+4, z-1) is lit
from the start and never part of it (8 of 8). The other five have a random
order. A hit on the next one lights it and it stays lit; a hit on any other
sends it and every lit one back (`_hit` -> plain), every recorded solve (5 of
5). `light_activate` (area, range 5) on the struck one. The retype comes on
the swing's own tick. Wrong hits cost nothing else that the packets show.

**Rock falls are the obelisk pad's.** They start the tick after its first hit
(7 of 7 party visits; none in the solo visit, which never touched its
obelisks) and come every 6-8 ticks until it is solved, on 5-13 random tiles of
the pad (7-10 in 21 of 28) with the three rock sounds near the obelisks. No
recording has a raider under one, so their damage is not known.

**Scarabs.** A pair, one on each lane, on S + 6 + kP, k >= 1: P = 27 for one
and two raiders (S+33, S+60), 15 for three (S+21, 36, 51 ...), 13 for six
(S+19, 32, 45 ...). At most two a raider alive: the solo raid sat at 2 for
140 ticks, the trio at 6 spawned single scarabs (both north). They come from
the west edge boxes until all four pad puzzles are done and from the east ones
after (7 of 7 party visits). 12 hitpoints (every kill), removed 4 ticks after
`npc_scarab_death` (19 of 19), all removed on the completion tick. First attack
the tick after spawning (15 of 34), from up to 8 tiles (65 of 126 at 8);
the hit lands floor(end / 30) ticks after the shot. Every recorded hit on the
praying recorder was a 0, so their damage is not known.

**Memory game.** Each board is a shuffle of 1-9 (the north board x 49/51/53 z
36/38/40, the south z 24/26/28). A board shows one open plate; opening a
second on the same board closes the first silently. With one open on each
board they are answered the next tick: a match keeps both faces up with the
number's overlay (1 -> fx05, 2 -> fx06, 3 -> fx03, 4 -> fx08, 5 -> fx09, 6 ->
fx04, 7 -> fx10, 8 -> fx11, 9 -> fx12) and `light_activate` range 3 on each;
a mismatch turns both back with `plate_close` range 3 on each. Solo starts
with four pairs matched (1, 3, 5 and 7 in the one solo visit). The ninth pair
opens the wall, "Puzzle 5 has been completed!", and "Challenge complete" comes
the next tick (4 of 4).

## Ours vs recorded

Rebuilt 2026-10-18: `toa_scabaras.rs2` (the room is now a ticking room,
`~toa_scabaras_tick`), the Scabaras blocks of `toa.constant`, `toa.npc` and
`toa.varp` (`varp7523_toa_scabaras_last_tile`), and the barrier crossing in
`toa_barrier.rs2` (LostCity's `p_arrivedelay`; the invented "You pass through
the barrier." message is gone). Checked with `test/raids/toa_scabaras_tells.lua`
(scriptrun, `--fixture test/raids/fixtures/fresh_lumbridge.ini`, solo, raid
level 0: the raider crosses by Quick-Pass, solves all four pads with one wrong
simon-says plate and one wrong obelisk, uses the crawl hole and the stepping
stone, then the memory game with one mismatch; `::toascab` prints the room's
hidden state for the test; first, before either east pad works, it steps onto
one's plate) against the rules above with a replay checker: 392 of 393.

| | Status |
|---|---|
| Room | **rebuilt** (was five rule procs with no board on the floor, no barriers, no scarabs, no rocks, a 4x4 lights-out, a 9-step sequence and an invented solo "skip" of the obelisks): all of the above |
| Start | **fixed**: on the crossing's tick, 2 ticks after arriving beside the barrier (1 of 1) |
| Build | **fixed**: 4 pad barriers + 17-tile wall (rot 3), pad floors, the four puzzles shuffled, 18 memory plates, the party statue |
| Puzzle numbers | **fixed**: the barrier across the section, "Puzzle N has been completed!", `found_gem` (4 of 4 pads) |
| Lights-out | **fixed**: ring of 8, k-1/k/k+1, the tick after (1 of 1 transition in the test; solved) |
| Addition | **fixed**: the recorded board, every walked plate incl. a run's middle (9 of 9), overlays by number (34 of 34), all 25 on completion |
| Simon-says | **fixed**: 5 different plates a tick apart from press + 1 (2 of 2 shows, 10 of 10 removals), steps on the ended tile (6 of 6), the wrong plate's roof, synths and next-tick hit (1 of 1), solved |
| Obelisks | **fixed**: the order, lit/reset rule, solved. Hits land through the obelisk's hit queue, so a ranged hit counts when it lands, not on the swing |
| Rocks | **fixed**: from the first hit + 1 (1 of 1), every 6-8 (18 of 18), 7-10 tiles on the pad (19 of 19) |
| Scarabs | **fixed**: first pair S+33 (1 of 1), cap 2 a raider, west boxes (2 of 2), every 4 (78 of 78), projectile start 51 / end 50 + 5d, 59 at 1 (80 of 80), hit floor(end/30) later (79 of 80: the one held while the raider was crawling) |
| Memory | **fixed**: one open a board, answered the next tick (5 of 5), overlays by number (10 of 10), mismatch closes both (1 of 1), wall + completion next tick |
| Crossings | **fixed**: hole (1 of 1) and stone (an approach op, range 3; 1 of 1) |
| Open: party numbers | the party rules (scarab periods 15/13, the 2-a-raider cap, the statue, the east boxes) are only exercised by the recordings, not by a party test; four and five raiders' scarab periods (14, 13 here) are not recorded |
| Open: damages | scarab shots (`^toa_scab_scarab_max` 8) and rock falls (answered as a puzzle hit) are not recorded; a raider under a rock and the addition going over are not recorded (both answered as a puzzle hit) |
| Open: Flip | lights-out's op5 is not recorded (here: one plate and a puzzle hit) |
| Open: tablet range | the number is 20-45 here; recorded 22-39 |
| A pad that does not work yet | **fixed**: an east pad before the barrier into its section opens answers a plate (a run's middle included) with "This device won't function yet." and does nothing (1 of 1 in the test). Its tablet, button and Flip say the same and its obelisks take no hits: those are not recorded |
| Open: solo crossings in a party | the hole and stone were only used solo; here they refuse a party |
| **Open, every room** | `~toa_challenge_complete` moves the party on at once; the "Challenge complete" line lacks "Duration … Total …"; the room song is not re-sent every 3 ticks |

