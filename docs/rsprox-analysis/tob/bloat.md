# The Pestilent Bloat

What Jagex's server sends in the Bloat room, read from 30 recorded Normal Mode kills, and how our room compares. The sources, caveats and units are in the [README](README.md). Heights are in our units (raw ÷ 4 for the rev-234 v2 projectile packet).

**Recordings:** 3299, 3285, 788, 1833, 1764, 553, 1779, 1837, 582. The full output of `tools/rsprox_bloat.py` over them is in [bloat.out](bloat.out).

**Ours:** `test/raids/tob_bloat.lua` (trio), `tob_bloat_hard.lua` (five), scriptrun tick logs (seeds listed under Checks).

**Status key:**
- **fixed:** changed to match the recording.
- **match:** ours already sent it.
- **open:** differs, not changed, with the reason.
- **not copied:** deliberately left, with the reason.

## Summary

Our Bloat already matched in her walk-and-sleep cycle and the falling flesh: when the waves come, how many fall, where they land, and when they splat.

What did not match:
- **The stomp** came a tick early (sleep seq + 29; recorded + 30, 37 of 37). It also had the wrong shake, no stomp sound, and released the camera at her rise instead of 2 ticks after the stomp.
- **Sounds.**
  - The splat sound went out once a wave as a synth; Jagex sends it per splat, as an area sound.
  - The fly sound was one of three synths; Jagex sends one of six, as an area sound at the target.
- **The flies** had the wrong projectile parameters, and landed a second graphic on the player.
- **The stun** had the wrong graphic height, no sound and no message.
- **Her death.**
  - The animation started a tick late. Fixing that needed one engine hook (`npc_setdeathanimnow`).
  - The room cleared a tick late.
  - Her book went on the floor for one raider rather than one per raider.
  - The Vampyres' chest points came at the chest's first Open rather than at the room's end.

## Her cycle (`--section cycle`)

| | Recorded | Ours before | Status |
|---|---|---|---|
| First sleep | 38–47 ticks after the fight track (571) starts (28 of 30; one 53) | 41 in seed 1 | match |
| Down | `tob_bloat_sleep` seq | same | match |
| Stomp | **sleep seq + 30**, 37 of 37 | + 29 (blert's `BLOAT_STOMP_TICK`, counted differently) | **fixed** (`^tob_bloat_stomp_offset` 30) |
| Rise | sleep seq + 33 | same | match |
| Awake | 38–47 ticks from the stomp to the next sleep seq (35 of 37) | 38–42 in seed 1 | match |

### The stomp

| | Recorded | Ours before | Status |
|---|---|---|---|
| Sound | `sound_area dragonslayer_dragonstomp3` (3752) at her centre, range 16 | none (the seq's own shout) | **fixed** |
| Synth | `rumbling` to every raider in the room | none | **fixed** |
| Shake | **three** `cam_shake` packets per raider: axis 0, 1, 2, random 5..8 each, amplitude 0, rate 0 (37 of 37) | one Near-Reality shake (type 1, jitter 20, amplitude 5), only to raiders she could see | **fixed** |
| Reset | `cam_reset` 2 ticks after the stomp (37 of 37) | at her rise, 3 ticks after | **fixed** (`[queue,tob_bloat_cam_settle]`) |

## The falling flesh (`--section flesh`)

| | Recorded | Ours | Status |
|---|---|---|---|
| Waves | every 4 ticks (55) or 6 ticks (28) | same | match |
| Per wave | 16 (88 waves) or 15 (7) | same | match |
| Target | not aimed at players: 2 of 1,513 landed on a player's tile | random tiles | match |
| Splat | `tob_bloat_blood_splat` (1576) on its tile 3 ticks after the flesh (1,513 of 1,513) | same | match |
| Splat sound | `sound_area barbassault_splat` (3308) **per splat**, at its tile, range 6 (4,264 of 4,264) | one `barbassault_splat` synth per wave, to raiders she could see | **fixed** |

## The flies (`--section flies`)

| | Recorded | Ours before | Status |
|---|---|---|---|
| Swarm | `tob_bloat_flies_large` (1568) 534 times, `tob_bloat_flies_small` (1569) 322 times, at random | small, always | **fixed** (large 5 in 8) |
| Projectile | start 0, end **30 at every distance**, angle 0, offset **12**, height **0 → 0** (856 of 856) | height 60 → 10, offset 0, 6 cycles a tile | **fixed** |
| On the player | nothing | `spotanim_pl tob_bloat_flies_small` at the landing | **fixed** (removed) |
| Sound | one of **six** `tob_bloat_flies_attack_1..6` as `sound_area` at the target, range 8. Several sounds share one projectile's tick where several swarms fly | one of three (2, 3, 5) as a synth, delayed to the landing | **fixed** |
| When | while she walks, and on her sleep tick (21) | the same: the flies run before the walk's end gates | match |
| A tick into the down | 4 of 856 | never | not copied (4 of 856) |

## The stun (`--section stun`)

All 15 recorded stuns came on a splat's tick (15 of 15).

| | Recorded | Ours before | Status |
|---|---|---|---|
| Graphic | `tob_bloat_stunned` (1575) on the player, height **124** | height 92 | **fixed** |
| Sound | `synth stunned` | none | **fixed** |
| Message | spam: "You've been stunned!" | none | **fixed** (`mes_spam`) |

## Damage, timing and the spread (2026-10-19)

The rows above came from the recorder's own view. These are read from **14
sessions re-transcribed with every raider shown**: the 9 above plus 845, 554,
1758 and 1163 (Normal), with Hard in 1852 and 1837. Every one is from a
Leagues V, Leagues VI or Grid Master world. Their perks change the numbers:
Leagues V's Last Stand relic holds a raider on 1 hitpoint, so every 1 splat
is dropped below, and the same flies hit 6–13 on Grid Master against 9–18 on
Leagues V. **No damage range here is an unmodified one.**

`test/raids/_bloat_tells.lua` stands one raider in her south corridor
through her cycles, with `::tobbosswound` so the flesh falls. That puts every
row below in our tick log.

### The flies

| | Recorded | Ours before | Status |
|---|---|---|---|
| Rate | a raider she sees takes a splat **every tick**, landing the tick after she looked; standing under her counts as seen | same | match |
| Source | a tile on her **near edge** facing the raider (north row for a raider to the north, ...), not her middle; the exact tile also moves with her step that tick | her middle (`~npc_projectile_source`) | **fixed**: the footprint tile nearest the raider, within a tile of the recorded one 74% of the time (her middle: 31%). No rule fitted exactly |
| Projectile to a raider under her | sent more often than not (17 against 8 when under her both before and after her step); the attack sound always | sent | match in kind; no clean rule |
| Damage | Normal 6–18 (Grid Master 6–17, mean 10.2; Leagues V 7–18, mean 13.3); Hard 8–20 | 10–20 | **open**: seasonal. Protect from Missiles never seen (the recorder never prayed it) |
| The spread | a swarm thrown **from the tile of a raider she flew the tick before** (250 of 282) at a raider near them, seen or not (about half were also flown by her that tick). Its hit lands 1 or 2 ticks later (12 / 20 of 33). A spread does not spread (7 of 282 sources were spread-only) | on her own tick, thrown from her middle | **fixed**: a tick behind, from the burning raider's tile |
| Spread reach | 1–9 tiles, about a third of the raiders at each distance up to 6 | everyone within 3 | **open**: no rule for which third |

### The stomp

| | Recorded | Ours before | Status |
|---|---|---|---|
| Hit | on **sleep seq + 31**, the tick after the stomp (60 of 60) | + 30 | **fixed** (`^tob_bloat_stomp_hit_delay`) |
| Who | raiders up to 6+ tiles off her 5x5, as sight allows; **none of the 3 under her** was hit | whoever she sees, under her too | **open**: 3 samples |
| Damage | Normal 35–69 (37), Hard 37–83 (23) | 40–80 | **open**: seasonal |
| On the raider | nothing of the stomp's own | nothing | match |

### The falling flesh

| | Recorded | Ours before | Status |
|---|---|---|---|
| Hit | on the splat's tick, to a raider on its tile (44 of 44) | same | match |
| Stun graphic | `tob_bloat_stunned` at height 124 (54 of 55) | same | match |
| Stun | the raider's first own step on **splat + 4** (36 of 52; the rest stood still) | splat + 6 | **fixed** (`^tob_bloat_hand_stun_ticks` 5 → 3) |
| Damage | Normal 23–42 (25), Hard 27–53 (19) | 30–50 | **open**: seasonal |

Ours now, from `_bloat_tells` (solo, Normal):
- 366 swarms and 384 hits, the hits on the tick after.
- Every stomp hit on sleep + 31.
- Stun steps on splat + 4. When the driver's step reached the server late,
  it came later; never earlier.

## Her death (`--section death`)

T is her killing hit (the first hitsplat after which her headbar reads empty).

| Tick | Recorded | Ours before | Status |
|---|---|---|---|
| T | `enabled_ops 0b00000`, the `tob_bloat_death` seq, and the `tob_bloat_death` synth with it (22 of 30; in the other 8 the bar read empty a tick before the death started, so all three are at T+1 there) | ops hidden at T; seq and sound at T+1 | **fixed** (engine `npc_setdeathanimnow`, below) |
| Seq + 3 | she is deleted (30 of 30) | same | match |
| Seq + 3 | `Wave 'The Pestilent Bloat' … complete!` on the delete tick (30 of 30) | delete + 1: the watchdog cleared the room after two missed looks | **fixed** (`~tob_bloat_killed` from her `[ai_queue3]`) |
| Seq + 3 | `tob_book_bloat` (22506) per raider at her centre (south-west + 2, + 2), theirs alone, 200 ticks | one book at her south-west tile, public | **fixed** |
| Seq + 3 | "The Vampyres are impressed by your prowess. <col=ff0000>Check the chest</col> to see how many points they've granted you." with the chest-points varbits (30 of 30) | points awarded at the chest's first Open | **fixed** (`~tob_chest_award_on_clear`, also for Sotetseg's chest) |
| Seq + 3 | song 147 and jingle 250, envelope 0/0/0/0 | same (the Maiden's room-end fix) | match |
| Seq + 3 | the supply chest open (`tob_midway_chest_open`, op "Search"); never seen before the clear, it is out of view | a closed chest added at the clear, opened with "Open" | **fixed** with Sotetseg's ([sotetseg.md](sotetseg.md#the-supply-chest)): closed from the build, open at the clear |
| Seq + 3..5 | song 578, the room track again | + 4 | match |

Rev 234 prints the book's despawn as 20, and rev 227 prints it as 200 with `ownership=self`. We use 200 ticks.

## Engine changes

**`npc_setdeathanimnow(boolean)`** (opcode 11144): the death animation on the killing hit's tick.
- **The problem:** the death's animation and sound are played by the death's next step, a tick after the killing hit. Jagex plays Bloat's on the hit's own tick.
- **The change:** when the active npc has set the flag, `npc_death_begin` (`torirs_server_combat.c`), which every killing blow runs on its own tick, plays the death sound and the death seq there and marks the seq sent, so the next step does not play it again. A respawn clears the flag.
- **Content:** her tick sets it (`~tob_bloat_tick`).

## Content changes

| File | What |
|---|---|
| `tob_bloat.rs2` | the stomp sound, shakes, synth and reset; the splat area sound per splat; the flies' swarm, projectile and sound; the stun graphic, synth and message; `npc_setdeathanimnow`; `~tob_bloat_killed` (the books and the room's clear) |
| `wiki_pestilent_bloat.rs2` | the drop's book replaced by `~tob_bloat_killed` |
| `tob_chest.rs2`, `tob_raid.rs2` | `~tob_chest_award_on_clear`: Bloat's and Sotetseg's chest points at the room's clear, with the Vampyres' message |
| `tob.constant` | the recorded figures; `^tob_bloat_stomp_offset` 29 → 30 |
| `tob_selftest.rs2`, `torirs_server_world_selftest.c` | the stomp pins restated: stomp at sleep + 30, rise at + 33 |
| `tob_bloat.lua` | the solver's `STOMP` 29 → 30 (`tob2_bloat.lua` reads it) |

## Checks

- **Scriptrun runs:** `test/raids/tob_bloat.lua` seeds 1–3 and `tob_bloat_hard.lua` seeds 1–2 all PASS. Seed 1 used to fail at `chest_found`, before the clear-time points.
- **Our tick logs vs the recording** (seed 1):
  - **Death:** the killing hit, the `tob_bloat_death` seq and the death synth all on 479. On 482: the delete, three books (one per raider), song 147 and jingle 250. 578 on 483.
  - **Cycle:** sleep seqs at 186, 254, 326, 394 and 463 (the first 41 after 571). Every stomp at sleep + 30 with `dragonstomp3` (range 16) and `rumbling`: 15 of 15 across the five runs.
  - **Splats:** 5,279 `barbassault_splat` area sounds at range 6 across the five runs; no splat synth.
  - **Flies:** the solvers keep out of her sight, so their runs carry none. `test/raids/_party_smoke.lua`, whose leader stands in the open, shows `tob_bloat_flies_attack_1..6` as area sounds at range 8.
  - **Stun:** no run was stunned. The code path compiles, but no run has exercised it.
- **`TORIRSSERVER_REV=osrs239 --selftest`:**
  - `::tobrun` passes 56 of 56, and the opcode coverage check passes.
  - The Bloat stomp pins pass at + 30.
  - Five checks fail that passed before. None are in the Bloat fight; they belong to the parallel session's in-progress door and entrance work (OSRS-Content 822fa6d98a and its uncommitted `tob_party.rs2`):
    - `tobexit`'s Bloat passage: the arrival tile moved from (52, 31) to (58, 32), so the scene built around it no longer reaches the passage at x 6405.
    - `tobresume` (the Maiden room).
    - Sotetseg's scan-lead and 5-tick-boss checks (the boss makes no attacks).

## Reproducing

```sh
A=~/.rsprox/binary/archive
F="$A/3299.txt $A/3285.txt $A/788.txt $A/1833.txt $A/1764.txt $A/553.txt $A/1779.txt $A/1837.txt $A/582.txt"
python3 tools/rsprox_bloat.py $F > docs/rsprox-analysis/tob/bloat.out
python3 tools/rsprox_bloat.py --section cycle $F                           # or flesh, flies, stun, death
./src/build_opt/torirsserver --scriptrun test/raids/tob_bloat.lua --bots 3 --name b --seed 1 --ticks 9000 \
    --session $S/b --fixture test/raids/fixtures/fresh_lumbridge.ini
```
