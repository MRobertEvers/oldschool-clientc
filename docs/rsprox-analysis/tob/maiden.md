# The Maiden of Sugadinti

What Jagex's server sends in the Maiden room, read from 31 recorded Normal Mode kills, and how our room compares. The sources, caveats and units are in the [README](README.md). Heights are in our units (raw ÷ 4 for the rev-234 v2 projectile packet).

**Recordings:** 3299 (5 kills), 3285 (1), 788 (1), 1833 (7), 1764 (4), 553 (3), 1779 (5), 1837 (3), 582 (1). The full output of `tools/rsprox_maiden.py` over them is in [maiden.out](maiden.out).

**Ours:** `test/raids/tob_maiden.lua`, trio, scriptrun tick logs (seeds listed under Checks).

**Status key:**
- **fixed:** changed to match the recording.
- **match:** ours already sent it.
- **open:** differs, not changed, with the reason.
- **not copied:** deliberately left, with the reason.

## Summary

Our Maiden already matched in her attack clock, her form changes and crab waves, the blood-throw shape (one throw per raider plus two extras round the furthest), and her death sequence.

What did not match:
- **Both projectiles.**
  - Her magic storm (start 109, 10 cycles a tile) lands on a distance-dependent tick; ours always landed 5 ticks after the throw.
  - Her blood flies 15 cycles a tile **from her centre tile** and is aimed at tiles.
- **Sounds and graphics:** her impact sounds and the pools' graphic are sent at the throw, delayed to the landing.
- **Small differences:**
  - the crab has no spawn animation;
  - the blood-spawn trail has a random rotation and a 29-tick life;
  - a blood spawn rises 10 ticks after its pool lands.
- **Room end:**
  - Her remains are left on the floor.
  - Every raider gets their own book.
  - The room-end music has no ramp, which needed one engine command (`midi_song_envelope`).
- **Her death:** her death runs on Jagex's ticks, which needed one engine hook (`npc_setdeathtype`).

## Her attacks (`--section attacks`)

| | Recorded | Ours | Status |
|---|---|---|---|
| Clock | an attack every 10 ticks (427 of 428 gaps) | same | match |
| First attack | +9 after her add (25 of 32), +5..+7 otherwise | room tick 9 | match |
| Seqs | `maiden_attack_special` (magic), `maiden_attack_blood` (blood), on the attack tick, with the projectiles | same | match |
| Blood share | 98 of 427 attacks | 1 in 3 of the eligible attacks, two-attack cooldown (Blert) | match in shape |

### The magic storm

| | Recorded | Ours before | Status |
|---|---|---|---|
| Projectile | `maiden_shadow_proj` (1577) at the target player, from her centre tile (south-west + 3,3), start **109**, angle 0, offset **11**, height **0 → 0** (362 of 362) | start 120, end height 10, offset 0 | **fixed** |
| Flight | end = 109 + **10 × the Chebyshev distance** from her centre: 3 tiles 139, 5 tiles 159, … 13 tiles 239 (362 of 362) | fixed 170 at every distance | **fixed** |
| Hit | lands floor(end / 30) ticks after the throw: 4 at 3 tiles, 5 at 5–7, 6 at 8–9, 7 at 11 | always 5 | **fixed** (`~tob_flight_ticks` of the new end) |
| Impact sound | `sound_area shadow_blitz_impact` at **her centre**, delay = end, range 10, sent with the throw (362 of 362) | none | **fixed** |
| Swing sound | none sent; the seq carries it in band | none | match |

### The blood throw

| | Recorded | Ours before | Status |
|---|---|---|---|
| Shape | 3–6 projectiles a throw: one start-20 projectile per raider, plus two start-45 extras within 2 tiles of one of them (37 of 37 extras) | one per raider + two extras round the furthest | match |
| Target | a **tile**, every projectile (388 of 388); the player's own throw too | the player's projectile followed the player | **fixed** (`projanim_map` for all) |
| Projectile | `maiden_blood_proj` (1578), start 20 / 45, angle **15**, offset **11**, height **40 → 0** | start 20 / 20, angle 8, offset 0, height 135 → 0 | **fixed** |
| Flight | end = 20 + **15 × the Chebyshev distance from her centre** (3 tiles 65, 4 tiles 80, … 13 tiles 215); extras end exactly **+25** and start at 45 | 50 + 15 × footprint distance, which is 15 cycles late on her west and south sides | **fixed** |
| Graphic and sound | `map_anim maiden_lingering_blood` and `sound_area tob_maiden_blood_impact` (range 10) at the tile, delay = end, **sent with the throw** (388 of 388) | graphic undelayed at the landing; the impact as a synth to everyone; plus a `tob_maiden_blood_attack` synth Jagex never sends | **fixed** |
| Blood spawn | rises on the pool's tile at throw + floor(end / 30) + **10** (50 of 50) | at the pool's expiry, landing + 11 | **fixed**: rolled at landing + 10, the damage window still 11 |

The plugin's "1 tile away takes 65 cycles" is the same law seen from the other side. It measures from her footprint, and her centre tile is the north-east of her middle four, so her east face is 3 tiles from it and her west face 4.

## Blood-spawn trail (`--section blood`)

| | Recorded | Ours before | Status |
|---|---|---|---|
| What it is | `tob_maiden_blood` (32984) put down on the tile a blood spawn leaves, every step | same | match |
| Rotation | random: 498 / 539 / 484 / 470 at rotations 0–3 | always 0 | **fixed** |
| Life | 29 ticks (1,226 of 1,228) | 30 | **fixed** |
| Expiry | the tile goes back to what the map had there: deleted (1,228), or `invisible_type8_nonblocking` where the map has one (757) | engine loc revert | match |

## Nylocas Matomenos (`--section crabs`)

| | Recorded | Ours before | Status |
|---|---|---|---|
| When | added on the tick she changes form (70, 50, 30) | same | match |
| How many | 2, 4, 6 or 8 a wave by party | by party | match |
| Spawn | **no seq** (430 crabs) | `elemental_spawn` (8098) | **fixed** (it is Verzik's crab's spawn) |
| Killed | `elemental_death` at hp0 + 0..3, deleted 2 ticks later | same | match |
| Reaching her | `elemental_death`, deleted 2 later, and she shows a heal splat (id 6) that tick | heal + death | match |

## Blood spawns (`--section slugs`)

| | Recorded | Ours | Status |
|---|---|---|---|
| Added | 12–18 ticks after the throw, which is landing + 10 (above) | landing + 10 | **fixed** |
| Killed | `tob_blood_spawn_death`, deleted 3 ticks later | | match (not re-measured) |
| At her death | removed with no seq | removed by `~tob_maiden_clear_room` | match |

## Her death (`--section death`)

T is her killing hit (headbar fill 0). 31 of 31 the same.

| Tick | Recorded | Ours | Status |
|---|---|---|---|
| T | transformation to `tob_maiden_dying_a`, ops `0b11111` | the same | **fixed**: the engine retypes her at the death's start (`npc_setdeathtype`, below); it used to happen in `[ai_queue3]` at T+1 |
| T+1 | `maiden_death_a` | the same | match |
| T+4 | transformation to `tob_maiden_dying_b` and `maiden_death_b` | the same | **fixed**: `^tob_maiden_death_a_ticks` 5 → 4; it was tuned to hold dying_a four ticks from the late retype, and every step after it ran a tick late (T+5, deleted T+9) |
| T+8 | she is deleted; `loc_add tob_maiden_dead_remains` ("Pool of blood") at her south-west tile, shape 10, rotation 0; `Wave 'The Maiden of Sugadinti' … complete!`; `tob_book_maiden` per raider at her south-west + (6, 3), theirs alone, 200 ticks; song 147 and jingle 250 | deleted, completion, song 147 + jingle | **fixed**: the remains loc (`~tob_maiden_faded`) and the book (`~tob_maiden_cleared_book`); there was no Maiden book at all |
| T+8..10 | song 570 (Maiden's Sorrow) | 570 a tick after | match |
| Song envelopes | 147 and 570 both **0/0/0/0**: no ramp | `midi_song`'s default 0/60/60/0 | **fixed** (engine `midi_song_envelope`, `~tob_music_room_cleared`), for every room |

## Music

| | Recorded | Ours | Status |
|---|---|---|---|
| Fight | `midi_song 569` (The Maiden's Anger) with out 0/60, in 60/0, on the tick the fight starts (her HUD opens; she is added 3 ticks later) | 569 at the fight start, `midi_song`'s default envelope 0/60/60/0 | match |
| End | above | | fixed |

## Messages

| | Recorded | Status |
|---|---|---|
| "Your stats are drained." (spam) | also outside the Maiden room, thousands of ticks from any of her attacks | not copied: a player-side or seasonal effect, not hers |
| "You feel refreshed as your health is replenished." | Leagues and Grid Master worlds only | not copied (seasonal, as in the Nylocas) |

## Engine changes

**`midi_song_envelope(int id, int out_delay, int out_speed, int in_delay, int in_speed)`** (opcode 11142).
- **The problem:** `midi_song` always sends script9630's default ramp (0/60/60/0). Jagex sends a ToB room's completion song and the room track after it with no ramp (0/0/0/0) in every recording, and content had no way to say so.
- **The change:** `~tob_music_play` keeps the default through it, and `~tob_music_room_cleared` asks for none.

**`npc_setdeathtype(npc type)`** (opcode 11143): a death transformation on the killing hit's tick.
- **The problem:**
  - Jagex has the Maiden in her first dying form on the tick her hitpoints reach 0, a tick before her death animation.
  - Ours can't do that from content: a death's script, `[ai_queue3]`, runs a tick after the hit (the reference's `npc_queue(3, 0, 0)`).
- **The change:**
  - **The hook:** the active npc names the type it becomes when its death starts. `npc_death_begin` (`torirs_server_combat.c`), which every killing blow runs on its own tick, applies it through `ToriRSServer_NpcChangeType` after hiding the menu. So the transformation's ops restore wins, as recorded: `enabled_ops 0b11111` beside it, and she stays attackable through the fade.
  - **Identity:** the death keeps the type it started as (`died_as` on the npc). `[ai_queue3]` is dispatched on it (`ToriRSServer_WorldNpcDied`), loot is credited to it, and the corpse wait is its record's `death_delay`. So `[ai_queue3,tob_maiden_30]` still runs and her timing is unchanged.
  - **Lifetime:** the hook is used up by the death it applies to; a revive or respawn clears both fields.
- **Content:**
  - Her tick sets it to her mode's `tob_maiden_dying_a`.
  - `~tob_maiden_killed` no longer retypes her again. A second TRANSFORMATION on the death-animation tick would rebuild her model on the client and cancel the animation.

**Argument types for `sound_area`.**
- **The problem:** the compiler resolves a bare name in a command argument by the type the command declares. `sound_area` (11132) declared none, so a sound sharing its name with a spotanim compiled as the spotanim's id: `shadow_blitz_impact` is sound 176 and spotanim 381, and the storm's impact went out as 381 (`creeper_attack`).
- **The change:** `gen_command_kinds.py` now declares `sound_area(synth, int, coord, int)` and `npc_setdeathtype(npc)`.
- **Audit:** of the 48 names `sound_area` is called with in the tree, that was the only one with a collision.

## Content changes

| File | What |
|---|---|
| `tob_maiden.rs2` | storm: start 109, 10 a tile from her centre, 0 → 0, offset 11, impact area sound at her centre. Blood: every projectile at a tile, 20 + 15 a tile from her centre, start 45 for the extras, angle 15, 40 → 0, offset 11, graphic and impact sound sent with the throw; no blood-attack synth; blood spawn at landing + 10. Crab: no spawn seq. Trail: random rotation. Book. |
| `tob.rs2` | the remains loc on her delete |
| `tob_music.rs2`, `tob_raid.rs2` | the room-end songs unramped; the Maiden book |
| `tob.constant`, `tob_maiden.constant` | the recorded figures |
| `tob_selftest.rs2` | the flight and storm pins restated as the recorded table |
| `tob_maiden.lua`, `tob2_maiden.lua` | the solvers' pool and storm landing ticks on the recorded law |

## Checks

- **Scriptrun runs:** all PASS with no script aborts.
  - `test/raids/tob_maiden.lua` seeds 1–3.
  - `tob_maiden_hard.lua` seeds 1–2.
  - `tob_nylocas.lua` seeds 1–2 (the room-end music and the Nylocas changes are shared).
- **Our tick logs vs the recording:**
  - **Death:** dying_a at T, `maiden_death_a` T+1, dying_b and `maiden_death_b` T+4. At T+8: the delete, `tob_maiden_dead_remains`, the books (one per raider), song 147 and jingle 250. 570 at T+9.
  - **Storm:** every storm at start 109, end 109 + 10d exactly. `shadow_blitz_impact` (176) at her centre, delay = end, range 10.
  - **Blood:** every blood primary at start 20, end 20 + 15d exactly; extras at start 45. All aimed at tiles, with `maiden_lingering_blood` and `tob_maiden_blood_impact` (range 10) delayed to the end.
  - **Crabs and trail:** no crab spawn seq. Trail rotations 175 / 172 / 170 / 192 across 0–3.
- **`TORIRSSERVER_REV=osrs239 --selftest`:**
  - The failure list is identical to the previous binary's, and `::tobrun` passes 56 of 56.
  - Three checks were restated as the recording:
    - **Death timeline:** now struck through `ToriRSServer_CombatHitNpc`, so the death's start runs. It reads "dying_a on 4 ticks from K+0, dying_b on 4 from K+4", where it used to arm the death by hand and expect K+1 / K+5.
    - **Trail life:** 29 ticks.
    - **Projectile pins:** the blood flight table and the storm.
  - The opcode coverage table is regenerated.

## Reproducing

```sh
A=~/.rsprox/binary/archive
F="$A/3299.txt $A/3285.txt $A/788.txt $A/1833.txt $A/1764.txt $A/553.txt $A/1779.txt $A/1837.txt $A/582.txt"
python3 tools/rsprox_tells.py --anchor 8360-8367 $F                       # every signature, counted
python3 tools/rsprox_maiden.py $F > docs/rsprox-analysis/tob/maiden.out
python3 tools/rsprox_maiden.py --section attacks $F                       # or blood, crabs, slugs, death
./src/build_opt/torirsserver --scriptrun test/raids/tob_maiden.lua --bots 3 --name m --seed 1 --ticks 8000 \
    --session $S/m --fixture test/raids/fixtures/fresh_lumbridge.ini
python3 tools/rsprox_tells.py --ticklog --anchor 8360-8367 $S/m/ticklog.tsv
```
