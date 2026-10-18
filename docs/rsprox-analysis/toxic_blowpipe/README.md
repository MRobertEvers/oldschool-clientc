# Toxic blowpipe projectiles from RSProx

Written 2026-10-18. It covers 3,611 recorded blowpipe dart projectiles and
9 special-attack projectiles, from 17 RSProx sessions (revs 224–237). It
also covers the rosewood blowpipe, which shares the dart shot.

## How the shots were found

The archive search on seq 5061 (`snakeboss_blowpipe_attack`) finds 36
sessions. These were transcribed with the upstream RSProx build, which
decodes revs 223–241:

- the toxic blowpipe: 471, 472, 901, 1044, 1441, 1837, 1852, 2758, 3216,
  3277, 3455, 3467, 4029 and 4032;
- the rosewood blowpipe: 3858, 4017 and 4028.

Many sessions record no player info for the shooter. So a shot is picked out
by the **local "dart" synth (2696)**: every projectile launched in a tick
that carries that sound.

    python3 -I tools/rsprox/rsprox_blowpipe.py ~/.rsprox/binary/archive/{471,472,...}.txt > shots.tsv

In the sessions that do record the shooter (471, 472, 2758 and 3858), each
dart projectile starts on the shooter's tile, on the tick the shooter plays
5061 (or rosewood 13144).

## The normal shot

| Field | Recorded | Notes |
|---|---|---|
| graphic | the loaded dart's travel spotanim | iron 227, steel 228, mithril 229, adamant 230, rune 231, dragon 1122 (`ii_dragon_dart_projanim`), amethyst 1936. 3,611 shots. |
| starttime | **32** | every shot |
| endtime | **32 + 5 × distance** | No fixed part. All 3,611 are a multiple of 5 and at most 5 × the distance to the target's south-west tile; the distance used is to the target's nearest tile (`npc_range`). Distance 1 gives 37, which is `ogre_block@37` in ../combat/defend_timing.md. |
| angle | **15** | revs 227–237 (rev 229's 2758 sent 2 on 13 shots) |
| progress | **105** | every shot. This is not scaled on rev 239's wire (arrows record 11 and firewave 64 there too). |
| startheight / endheight | **35 / 36** on revs 227 (MAP_PROJANIM_V1); **142 / 146** on revs 233–237 (V2, which drops the client's ×4) | Rev 229's 2758 sent 40 / 36. |
| graphic on the shooter | **none** | No spotanim on the shooter in 471, 472, 2758 or 3858. The dart's `*_dart_launch` is the thrown dart's. |
| sound | `dart` (2696), delay 0 | already ours (attack_sounds.obj) |

The npc's defend animation is sent with delay = endtime (1441: `delay=39`
and `47` against shots ending at 39 and 47). That agrees with
../combat/defend_timing.md.

## The special attack (Toxic Siphon)

Nine projectiles were recorded:
- graphic 1043 `toxic_blowpipe_specialattack` in 1044, 1441, 3277 and 4029;
- its Leagues twin 2599 in 1044.

| Field | Recorded |
|---|---|
| starttime | 32 |
| endtime | **32 + 7 × distance** (distance 1 → 39: the cave slime in 1441, the soul-wars avatar in 3277; distance 4 → 60; distance 5 → 67: the pest portal in 4029) |
| angle / progress | **15** / 105 |
| heights | 35 / 36 (V1), 142 / 146 (V2) |

No recording carries the `snake_hit` (800) landing sound that ours plays.
None of the spec shots is the recording player's own with sound on, so this
is open, not refuted.

## The rosewood blowpipe

The rosewood blowpipe is in sessions 3858, 4017 and 4028 (revs 235–237).

- **Normal shot:** seq `rosewood_blowpipe_attack` (13144) with the dart's
  travel graphic, and exactly the toxic blowpipe's shape (32, 5 per tile, 15,
  105, 142 / 146).
- **Special (Rapid Burst):** seq `rosewood_blowpipe_special_attack` (13145)
  and **two** `rosewood_blowpipe_special_travel` (3486) projectiles in the
  same tick. The first starts at 32 and ends at 32 + 5 × distance; the second
  starts at **47** and ends at 47 + 5 × distance. Each comes with a `dart`
  synth, the second with delay 15.

## Ours, before and after (2026-10-18)

| | Before | After |
|---|---|---|
| normal shot (`player_ranged.rs2`, every blowpipe) | the bow row: heights 40 / 36, delay 41, length 5 (end = 46 + 5 × distance), angle 15, offset 11 | 35 / 36, delay 32, length 0 (end = 32 + 5 × distance), angle 15, offset 105 |
| graphic on the shooter | the dart's `*_dart_launch` at height 96 | none |
| spec (`pvm_toxic_blowpipe.rs2`) | angle **21** (Near Reality), otherwise the recording's | angle 15 |

**Still not exact.** On rev 239 our encoder sends content heights × 4
(`torirs_server_wire.c`, MAP_PROJANIM_V2). So 35 / 36 goes out as 140 / 144
against the recorded 142 / 146, half an old height unit low.

This affects every rev-239 projectile, not just the blowpipe. Arrows record
163 / 146 against our 160 / 144, and firewave 172 / 124 matches exactly.
Making them exact needs a way for content to state heights in the fine
units.

**Not done.** The rosewood special still sends no animation, no projectiles
and no sounds (`pvm_rosewood_blowpipe.rs2`). Its recorded shape is above.
