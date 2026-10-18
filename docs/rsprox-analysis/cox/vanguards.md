# Vanguards: recorded behaviour

Read from submissions 843 and 3285 (solo, normal) and 3389 (solo, Challenge Mode), all on Grid Master worlds; see
[README.md](README.md). Tool: `tools/cox/rsprox_vanguards.py` (sections `timeline`, `attacks`, `cycles`, `shuffle`,
`wake`, `death`). The room's spec is `docs/minigames/cox/rooms/vanguards_spec.md`; the content cites these rows as
`RV-*` (`cox_vanguards.rs2`, `cox.constant`, `cox.npc`).

    python3 tools/cox/rsprox_vanguards.py --section cycles ~/.rsprox/binary/archive/{843,3285,3389}.txt

**Coordinates** are template room-local tiles (`tools/cox/rsprox_room.py` undoes the transcriber's turned-room errors).
A vanguard is 3x3, and its printed tile is cross-checked against the projectile source and attack sound at its middle
on every swing. **W** is the tick the dormant shells play `vanguard_spawn`; **S** a shuffle's first tick.

**Damage is not read here.** Every hit on the raider in these recordings is a 0 (hitsplat 12), whatever the style.
Timing, tiles and hit counts are reliable; max hits and the prayer factor are not settled by these recordings.

## Rooms

| Row | Recorded |
|---|---|
| RV-R1 | The holes (south-west tiles). **thru** (3389): (12,12), (20,17), (13,21). **cw** (843, 3285): (19,12), (13,19), (9,12). Each raid uses the same three holes for its whole fight. ccw is not recorded. |

## The rise and the attacks

| Row | Recorded |
|---|---|
| RV-W1 | At W all three play `vanguard_spawn`, still the dormant form. The raider was 2-5 tiles from the nearest hole. At **W+5**, on the same tick, each takes its style form with its opening transition (`vanguard_idle_basic_idle_<style>_transition`). |
| RV-A1 | Each takes the raider once they are within **5** of its 3x3, never at 6 (3285: the melee one held 7 ticks at 6 and set off at 5; the ranged one held 8 at 6). Each then swings 5 ticks after its shell opens (or as soon as it has the raider after that), and every 4 after that. They swing together because they open together. Ranged and magic never move while fighting. Melee: 3 hits on the swing tick, sound 3308 at its middle. |
| RV-A2 | **Ranged volley:** 3 rocks from its middle, with sound 239 (range 10). Rock 1334 is **locked** on the target (start 23, angle 10). 1333 goes to a tile exactly **2** from the target (start 25, angle 16), and 1332 to a tile exactly **4** away (start 26, angle 19). Progress 96, heights 100 → 5. All end at **46 + 6 × gap**, where gap is the target's distance from the 3x3. The locked rock's splash (305) and sound 360 go out with the swing, delayed to the end. The tile rocks' splash and sound are sent when they land, end / 30 ticks on (T+2): splash with no delay, sounds 3 and 6 cycles in. |
| RV-A3 | **Magic volley:** 3 orbs (1331) from its middle, with sound 3530 (range 10). One is locked on the target (start 45, angle 60). The others go to tiles exactly 2 away (start 30, angle 61) and exactly 4 away (start 30, angle 62). Progress 64, heights 150 → 5. All end at **46 + 25 × gap**. A tile orb's 659 is sent on landing (floor(end / 30)) with delay 10, its sound 156 3 or 6 cycles in. The locked orb's sound 156 goes out with the swing, delayed to the end. |

## The shuffle

| Row | Recorded |
|---|---|
| RV-S1 | At S all the living play their closing transition and take the walking form on the same tick. Each keeps its style for life. They walk all the same way round the three holes, a coin per shuffle (both ways recorded). Damage lands on the walking form. Each gains 1 hp at every walking form: 843's 81 headbar readings fit exactly with base 186, +1 at each, rounded down. |
| RV-S2 | **A reset** (the gap reached): the shells close on the tick of the hit that crossed it. They walk from S at one tile a tick. **Each heals to full on its own S+4** (`vanguard_heal` and a heal splat for the missing hp). They re-emerge at S+12 (+1 on one raid). The gap: 843 reset at a gap of 101 of base 186 and held at 64 (34%); 3285 reset at 69 (40% of its base, about 172). The wiki's 40% stands. |
| RV-S3 | **On the timer:** they stand 3 ticks, then **run** 2 tiles a tick to their holes, arriving S+5 to S+8. They re-emerge 2 ticks after the last arrives, at S+9 at the earliest (S+9, S+9, S+9, S+10). |
| RV-S4 | The timer runs from the wake and from each shuffle's start (a reset restarts it), not from the emergence. Shells to timed shells: 20 (from the wake), 31, 35, 34, 34 ticks. That is Mod Ash's "20 - 36". |

## The clear

| Row | Recorded |
|---|---|
| RV-D1 | `vanguard_death` at D, removed at D+3. Its drops and the line "As the Vanguard is slain by <name>, supplies are dropped for all." come at D+3. The exit crystal shatters the tick after the last is removed (loc anim, sound 1021, then 3821 5 cycles in), and is replaced 4 ticks later. No room-wide message is sent. |

## Open

| Item | State |
|---|---|
| Damage | Every recorded hit on the raider is 0; the max hits and the protection factor are from the wiki and Near-Reality. |
| The swirls | `raids_vanguard_swirl_high/low_health_spotanim` come every 2 ticks on the healthiest and the weakest. The same gap (63) shows with and without them, so what turns them on is not a plain gap. Ours: 30% of base, a guess. |
| The trample | 2 hits of 5 in 6 checks under a walking shell; too few to settle the cadence. |
| A reset's split shell | The vanguard being hit on the reset tick closes a tick after the other two. Ours closes all three together. |
| The book | `raids_vanguard_book` is placed 4 ticks after the shatter, but on a different template tile in each recording (843 (23,16), 3285 (22,17)). Not placed. |
| ccw holes | Not recorded; ours are Near-Reality's, unverified. |
| Hole styles | Both cw raids put ranged, melee and magic on the same holes. We keep the shuffle random ("completely random", Synq). |

## Fixed in our content (2026-10-18)

- The holes for thru and cw (RV-R1).
- The rise: spawn on the dormant form, the style forms 5 ticks later (RV-W1).
- Aggression: huntrange 3 → 5 (RV-A1; the engine measures from the south-west tile, Jagex's server from the 3x3).
- The volleys: the locked shot, tiles exactly 2 and 4 out, the recorded projectiles, timings and sounds, and the ranged launch sound (RV-A2, A3).
- The shuffle: shells and walking form on one tick; timed shuffles stand 3 ticks then run, resets walk and heal on
  S+4; re-emergence after arrival at S+9 or S+12; the timer from each shuffle's start (RV-S1..S4).
- The clear: each vanguard removed with its drops and the party line 3 ticks after its death animation
  (`death_delay=3`), the crystal shattering a tick after the last is removed and gone 4 ticks later; the unrecorded
  "defeated" and points lines removed (RV-D1).
- Checked against our own tick log after the change (scriptrun, 16 seeds): the rise at W+5; the first swing 5 after
  each opening, then every 4; the volleys' tiles (0, 2, 4 from the target) and cycles; timed shells moving from S+3 at
  2 tiles a tick and opening at S+9/S+10; a reset's heals on S+4 and opening at S+12; removal at D+3; the crystal at
  +1 and +4.

Not from the recordings, and marked so in the content:
- The ranged vanguard has no shot at a raider under its 3x3 (Synq 1:39:24, 1:41:35).
- The trample hits only while the shells move (every recorded trample was while they moved; none while they stood).
  It is dealt as the walking form, so the tick log attributes it (type 7526).
