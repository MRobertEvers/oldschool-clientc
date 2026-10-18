# Sotetseg

What Jagex's server sends in the Sotetseg room, read from 28 recorded Normal Mode kills (56 mazes), and how our room compares. The sources, caveats and units are in the [README](README.md). Heights are in our units (raw ÷ 4 for the rev-234 v2 projectile packet).

**Recordings:** 3299, 3285, 788, 1833, 1764, 553, 1779, 1837, 582. The full output of `tools/rsprox_sotetseg.py` over them is in [sotetseg.out](sotetseg.out).

**Ours:** `test/raids/tob_sotetseg.lua` (trio), `tob_sotetseg_hard.lua` (five), scriptrun tick logs (seeds listed under Checks).

**Status key:**
- **fixed:** changed to match the recording.
- **match:** ours already sent it.
- **open:** differs, not changed, with the reason.
- **not copied:** deliberately left, with the reason.

## Summary

Our Sotetseg already matched in his attack clock, the bounce rule (one bounce per other raider in reach, at most two, one of each colour), the death ball's schedule, the maze's timing (the teleport three ticks in, his first attack one tick after it) and the realm's drain period.

What did not match:
- **His ball** flew 56 + 8 cycles a tile and landed about 3 ticks after the throw. Jagex's flies **203 cycles at every distance** and lands 6 ticks after it. Its sounds went out as synths; Jagex sends area sounds, and puts an impact graphic on the target.
- **The bounces** had the ball's projectile shape and flight. Jagex's have their own: the grey a fixed 216, the red 36 + 30 per tile. They reached across the whole room; Jagex's reach 9 tiles.
- **The death ball** flew 450 cycles; Jagex's flies 480. Its landing sound and graphic went out a tick before its splat; Jagex sends all three on one tick.
- **The maze's tick** threw a real ball. Jagex plays the cast with no projectile.
- **The maze's messages** were a white game message. Jagex sends the Theatre card's text and fade scripts, and a darkness overlay in the realm.
- **A wrong tile** had no tile change and no zap, the explosion sat at height 0, and the sound was a synth.
- **The tornado** hummed once, when it appeared. Jagex's hums every tick.
- **His death** played a tick late, cleared the room a tick late, and left no book.
- **The supply chest** appeared at the clear and was never opened.

## His attacks (`--section attacks`)

| | Recorded | Ours | Status |
|---|---|---|---|
| Clock | 5 ticks between attacks (339 after a ball, 51 after a melee; the rest straddle a maze); 10 after a death ball (20) | same | match |
| First attack after he turns to combat | +1 (55), the fight's first +6/+7 | same (`^tob_sote_post_maze_ticks`) | match |
| Choice | 393 balls, 61 melee, 27 death balls | same rules | match |
| Melee | the seq alone; the swing's sound is in the seq | same | match |

## The ball (`--section primary`)

| | Recorded | Ours before | Status |
|---|---|---|---|
| Projectile | always the red `tob_sotetseg_maging` from his centre: start 23, angle 16, offset 64, height 26 → 31 (393 of 393) | start 20, height 80 → 31, 36 + 8 a tile | **fixed** |
| Flight | **end 203 at every distance** (3 to 12 tiles; 382 of 393, the rest 233/263) | 56 + 8 a tile: 3–4 ticks | **fixed**: lands **+6**, splats **+7** |
| Throw sound | `sound_area tob_sotetseg_attack_ranged` at his **south-west** tile, delay 10, range 10 (393 of 393) | a synth to the target | **fixed** |
| Impact graphic | `fireblast_impact` on the target, sent with the throw, delay = 203, height 124 | none | **fixed** |
| Landing sound | `sound_area tob_sotetseg_fireball_player_hit` at the target's tile on the landing tick, range 6 | a synth | **fixed** |
| In flight when a maze starts | lands on nobody: no landing sound, no splat (47 of 47 balls, 5 of 5 death balls) | same (`~tob_sote_maze_nulls_hit`) | match. This is what lets a team skip a ball by pushing him into the maze |

The impact graphic was only ever seen on the recorder's own player. That says nothing about privacy: these transcripts show graphics for the recorder alone.

## The bounces (`--section bounce`)

On the ball's landing tick, from its landing tile.

| | Recorded | Ours before | Status |
|---|---|---|---|
| How many | none, one or two; one of each colour when two (41 of 41) | same | match |
| Reach | other raiders within **9** tiles: "a bounce per raider in reach, at most two" fits 332 of 393 landings at 9 (300 at 24); a lone raider 10 tiles off took none (2 of 2); none flew further than 9 | 24 (the whole room) | **fixed** (`^tob_sote_split_range`) |
| Projectile | start 15, angle 8, offset 0, height 31 → 24 | start 5, angle 16, offset 64, 31 → 31 | **fixed** |
| Grey flight | **216** wherever it goes (205 of 236; otherwise 246) | 41 + 8 a tile | **fixed** |
| Red flight | **36 + 30 × max(tiles, 3)**: 126 up to three tiles, then 156, 186 … 306 (152 of 183; otherwise 216/246) | 41 + 8 a tile | **fixed** |
| Sound | sent **with** the bounce at its target's tile, delay = its end (capped at 255), range 4: `ricochet_player_hit` for grey, `fireball_player_hit` for red | a synth at the landing | **fixed** |
| Impact graphic | red: `fireblast_impact` on its target, delay = end, height 96; grey: none | none | **fixed** |
| Splat | on the bounce's landing tick or the tick after (27 and 44), as the two raiders' turns fall | its impact queue ran on the full flight: the landing tick +1 or +2 | **fixed**: queued a tick short, from the victim's queue |
| Who | the nearest other raider in 90 of the single bounces | random among those in reach (Near-Reality) | open: the other ~20% were not the nearest, so it is not a strict nearest rule |
| Red 216/246 | 31 red bounces fly 216 or 246 off the law | | open: no distance or pairing explains them |

## The death ball (`--section deathball`)

| | Recorded | Ours before | Status |
|---|---|---|---|
| Throw | his ranged seq; `tob_sotetseg_sharedattack` start 23, **end 480 at any distance**, angle 40, offset 64, height 26 → 31 | start 30, 450, angle 16, 80 → 31 | **fixed** |
| Sound | `sound_area tob_sotetseg_large_attack_ranged` at his south-west tile, delay 10, range 10 | at his centre, delay 0 | **fixed** |
| Message | "<col=bf0000>A large ball of energy is shot your way...</col>" to the target | same | match |
| Landing (+16) | the splat, `tob_sotetseg_sharedattack_impact` on each raider it hits, and one `sound_area tob_sotetseg_large_fireball_player_hit` at the target's tile, range 6, **all on one tick** | sound and graphic at +15 as synths, splat at +16 | **fixed** (`[queue,tob_sote_ball_landed]`) |
| Bounces | none | none | match |

## The maze (`--section start`, `--section maze`)

| | Recorded | Ours before | Status |
|---|---|---|---|
| The maze's tick | `tob_sotetseg_attack_ranged` **with no projectile**, his turn to non-combat, ops `0b11111`, level 995 (56 of 56) | a real ball thrown, then un-counted toward the death ball | **fixed**: the seq is the maze's (`~tob_sote_start_maze`); no attack that tick |
| Every raider, T | `human_teleport_other_impact`, the `teleport_shadowrealm` synth | same | match |
| Every raider, T | client script **2308** (the card's text): the runner "<col=7f7f7f>Sotetseg chooses you...</col>", everyone else "" | `mes("<col=ffffff>Sotetseg chooses you...")` to the runner | **fixed** (`~tob_sote_maze_card`) |
| Teleport | T+3 | same | match |
| Every raider, T+4 | client script **2306** (the fade): 255, ramp, the same text; the runner also `if_opensub tob_hud:atmospheric darkness_medium` | none | **fixed** |
| Realm drain | `tob_sotetseg_drain` every **7** ticks (83 of 83), the hit (1–3) **2 ticks after** it | the graphic and the hit on one tick | **fixed** (`[queue,tob_sote_realm_hit]`) |
| Leaving | on the exit tile: the `teleport_shadowrealm` synth and 2308 with ""; the teleport the next tick; the tick after, 2306 (255, ramp, "") and the darkness closed | "You step back out of the shadow realm." | **fixed**: no recording has that line |
| Maze end | he turns combat, attacks the next tick | same | match |

### A wrong tile (`--section wrongtile`)

| | Recorded (170 steps) | Ours before | Status |
|---|---|---|---|
| The tile | `tob_sotetseg_triggeredtile` for **one tick**, then back (170 of 170) | unchanged | **fixed** (`loc_change`, 1 tick) |
| Explosion | `devious_explosion` at height **10** | height 0 | **fixed** |
| Sound | `sound_area tob_sotetseg_player_tile_explosion_impact_2`, range 8 | a synth of the `_impact` sound | **fixed** |
| Zap | `tob_sotetseg_zap` from the tile to each raider one tile off it: start 0, end 15, angle 64, offset 64, height 0 → 16 | none | **fixed** |
| Second zap form | start 15, end 25, angle 16, offset 0 (13 cases) | | open: what selects it is not clear from 13 |
| Damage | the hit on the explosion's tick or the next (17 and 24) | on the tick | open |

### The tornado

| | Recorded (77 tornadoes) | Ours before | Status |
|---|---|---|---|
| Hum | `sound_area tob_sotetseg_red_spiral_hum` **every tick** from the one after it appears, at the centre of the 3×3 it steps to, range 6, random delay 0–5. Two a tick while a second tornado is alive (50 lives), one alone (27 of 27) | once, at its spawn, heard across the maze | **fixed** |
| Collision | `sound_area tob_sotetseg_redspiral_grid_playercollision` at the raider's tile, range 6, with the hit | a synth | **fixed** |

## His death (`--section death`)

| Tick | Recorded (28 kills) | Ours before | Status |
|---|---|---|---|
| T | `tob_sotetseg_death` seq, ops `0b00000`, the `tob_sotetseg_death` synth | ops at T; the seq at T+1, the synth at T+3 | **fixed**: `npc_setdeathanimnow` and the records' `death_sound` |
| T+3 | deleted; "complete!"; the Vampyres' points message; `tob_book_sotetseg` per raider at his centre, theirs alone, 200 ticks; `tob_midway_chest_open`; song 147 and jingle 250 | deleted; the room cleared by the watchdog at T+4; no book | **fixed** (`~tob_sote_killed` from `[ai_queue3]`) |
| T+3..5 | song 584 | T+4 | match |
| T+3 | `invisible_type8_nonblocking` at his south-west tile, where `invisible_type8_blocking_size5` stood | none | open: ours blocks his tiles through the npc itself |

## The supply chest

| | Recorded | Ours before | Status |
|---|---|---|---|
| Through the fight | `tob_midway_chest_closed` (op "Open") | none | **fixed**: placed when the room is built |
| On the clear | replaced by `tob_midway_chest_open` (op "Search") on the delete tick | the closed chest added | **fixed** |
| Use | `oploc2` on the open chest | `oploc1` on the closed one | **fixed**: the store opens from Search; the closed chest's Open does nothing |

The Bloat room uses the same chest code, so its chest now opens on its clear too. Its recordings never show the chest before the clear (it is out of view), and always show it open after.

## Engine changes

None new. The death uses `npc_setdeathanimnow` (opcode 11144, added for the Bloat).

## Content changes

| File | What |
|---|---|
| `tob_sotetseg.rs2` | the ball, the bounces and the death ball as recorded; the maze's tick a cast with no ball; the card scripts and the darkness; the drain's hit two ticks on; the exit's scripts, no exit message; the wrong tile; the tornado's hum and collision; the death and the books |
| `tob_sotetseg.constant`, `tob.constant` | the recorded figures; `^tob_sote_split_range` 24 → 9; `^clientscript_torirs_tob_hud_text` (2308) |
| `tob.npc` | `death_sound` on the three combat forms |
| `tob_chest.rs2`, `tob_raid.rs2` | the chest closed at the build, open at the clear, searched |
| `tob_sotetseg.lua`, `tob2_sotetseg.lua`, `action_plan.c` (`sote_guard`) | the solvers' landings on the recorded flights: the ball +6 anywhere, a red bounce max(d, 3) after its victim, the queue a tick short |
| `action_plan_test.c` | the guard's pin restated on the new flights, with ricochets in the scene |
| `tob_relay.lua`, `_conformance.lua` | the chest found open and searched |

## Checks

- **Scriptrun runs:** `test/raids/tob_sotetseg.lua` seeds 1–3, `tob_sotetseg_hard.lua` seeds 1–2 and `tob_bloat.lua` seed 1 all PASS.
- **Our tick logs vs the recording** (`rsprox_tells.py --ours`):
  - **Ball:** every ball start 23, end 203; the throw sound at his south-west tile, delay 10, range 10; the landing sound range 6.
  - **Bounces:** start 15; greys end 216; reds 126, 156, 186 …; their sounds sent with them, delay = end, range 4.
  - **Death ball:** start 23, end 480; its sound delay 10.
  - **Tornado:** a hum every tick with delays 0–5.
  - **Death (seed 1):** the killing hit, the death seq and the death sound all on 572. On 575: three books, song 147, jingle 250, the chest turned open, the delete. 584 on 576.
- **Not exercised by a run:**
  - The wrong tile: the solvers never step off the path. It compiles; no test steps on one.
  - The maze card and darkness: the tick log does not record client scripts or interfaces. The runs pass through both mazes with no script abort.
- **`TORIRSSERVER_REV=osrs239 --selftest`:** no new failures; `::tobrun` passes 56 of 56. The Sotetseg scan-lead and 5-tick-boss checks fail as they did before this work (the boss makes no attacks in that harness).
- **`make -C src test-action-plan`:** passes.

## Reproducing

```sh
A=~/.rsprox/binary/archive
F=($A/3299.txt $A/3285.txt $A/788.txt $A/1833.txt $A/1764.txt $A/553.txt $A/1779.txt $A/1837.txt $A/582.txt)
python3 tools/rsprox_sotetseg.py $F > docs/rsprox-analysis/tob/sotetseg.out
python3 tools/rsprox_sotetseg.py --section bounce $F        # or attacks, primary, deathball, start, maze, realm, wrongtile, death
python3 tools/rsprox_tells.py --anchor 8387-8389 $F --ours $S/s/ticklog.tsv
./src/build_opt/torirsserver --scriptrun test/raids/tob_sotetseg.lua --bots 3 --name s --seed 1 --ticks 9000 \
    --session $S/s --fixture test/raids/fixtures/fresh_lumbridge.ini
```
