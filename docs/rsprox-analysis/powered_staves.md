# Powered staffs: recorded casts

Read from every RSProx transcript in `~/.rsprox/binary/archive/` (78 files):
every `map_projanim` carrying a powered staff's projectile, with the caster's
cast animation and graphic, the target's impact graphic and the sounds on the
cast tick. Heights are in our units: these are all `map_projanim_v1`, whose
heights the same transcripts record for the wave spells as 43 -> 31, our own
rows. Flight is `endtime - starttime` against the distance to the target's
footprint (the off-diagonal rows in a raw table are large npcs measured from
their south-west tile).

Other players' animations and cast graphics are usually not in these
transcripts, only the recording player's, so a staff only other raiders threw
has its projectile, impact and impact sound recorded and not its cast.

| Staff | Throws (files) |
|---|---|
| Tumeken's shadow (proj 2126) | 90 (1875, 1858), all by other raiders |
| Sanguinesti staff (1539) | 583 (1837, 553, 1696, 1703, 582, 1852), all the recording player's |
| Sanguinesti staff, Justiciar kit (1899) | 330 (1837, 1852), all by other raiders |
| Warped sceptre (2569) | 119 (14), the recording player's |
| Trident of the seas (1252) | 1,267 (1833, 1764, 1779, 553, 1837, 1694), by other raiders |
| Trident of the swamp (1040) | 3 (1754) |
| Dawnbringer (1544) | 560, 489 the recording player's |
| Dawnbringer special (1547) | 240, 106 the recording player's |

Not in any transcript: the Eye of Ayak, Thammaron's and the accursed sceptre,
the bone staff, the ornament-kit tridents.

## Projectiles

| Staff | start (delay) | angle | slope | heights | flight | Seen |
|---|---|---|---|---|---|---|
| Tridents, Sanguinesti (both), Dawnbringer (auto and special) | 51 | 16 | 64 | 23 -> 15 | 10 x tiles - 5 | 2,983 of 2,983 |
| Tumeken's shadow | 56 | 32 | 40 | 62 -> 31 | 10 x tiles + 16 | 90 of 90 |
| Warped sceptre | 46 | 2 | 50 | 50 -> 31 | 10 x tiles | 119 of 119 |

## Cast and impact

| Staff | Cast anim | Cast graphic (height) | Impact graphic (height), delayed to the projectile's end |
|---|---|---|---|
| Sanguinesti staff | `human_castwave_staff` 1167 | 1540 (92), 583 of 583 | 1541 (60), 454 of 583 (the rest: the heal 1542 or a splash) |
| Sanguinesti, Justiciar | not recorded | not recorded | 1901 (60), 255 of 330 |
| Dawnbringer | 1167 | 1543 (92), 489 of 489 | 1545 (60), 518 of 560 |
| Dawnbringer special | 1167 | 1546 (92), 106 of 106 | 1548 (60), 224 of 240 |
| Warped sceptre | `pog_warped_sceptre_attack` 10501 | 2567 (no height), 119 of 119 | 2568 (124), 100 of 119 (15 splashes) |
| Trident of the seas | not recorded | not recorded | 1253 (92), 1,129 of 1,267 |
| Tumeken's shadow | not recorded | not recorded | 2127 (124), 57 of 90 (17 splashes) |

A splash puts `failedspell_impact` (85) at height 124 on the target instead.

## Sounds

- Cast: synth `shadow_cast` (178) on the cast tick, for every staff the
  recording player cast (Sanguinesti 583 of 583, warped sceptre 119 of 119,
  Dawnbringer 489 of 489).
- Landing: area `contact_darkness_impact` (1460), range 10, on the target's
  tile, delayed to the projectile's end, on a hit; `spellfail` (227) on a
  splash. Every staff, other raiders' included, and the Dawnbringer special.

## Ours vs recorded

Rebuilt 2026-10-18 (`gear/powered_staff.rs2`, `gear/powered_staff.constant`,
`specs/pvm_verzik_special_weapon.rs2`). Checked with
`test/raids/powered_staves.lua` (scriptrun: one cast with each recorded staff
at a Lumbridge man, the projectile's id, start cycle and flight formula
asserted) and the tick log's cast and impact rows.

| | Status |
|---|---|
| Projectiles | **fixed**: every staff flew the Fire Wave's 43 -> 31. Now the three recorded sets above; the test's six casts fly them |
| Impact height | **fixed**: was 124 for every staff; now 60 for the Sanguinesti staffs and the Dawnbringer (auto and special), 92 for the trident of the seas, 124 for the shadow and the sceptre |
| Cast height | **fixed** for the warped sceptre (0; was 92); 92 for the others |
| Cast sound | **fixed** for the warped sceptre: its weapon param resolved to `unarmed_punch`; now `shadow_cast`. The others already sent `shadow_cast`, except the shadow (below) |
| Landing sound | **added**: `contact_darkness_impact` / `spellfail`, range 10, delayed to the landing; no spell in our tree sent one |
| Open: unrecorded staffs | the Eye of Ayak, Thammaron's and the accursed sceptre and the bone staff keep the wave spells' geometry (43 -> 31, 51, 16, 64, 10 x tiles - 5) and impact height 124; the ornament-kit tridents fly with the plain ones |
| Open: unrecorded casts | Tumeken's shadow's cast (ours: `toa_sot_cast_b`, graphic 2125 at 92, synth `toa_shadow_weapon_cast_fire_01`), the tridents' casts and the Justiciar kit's cast graphic (ours: 1900) were only thrown by other raiders, whose casts these transcripts do not carry |
| Open: trident of the swamp impact | 3 throws, none with its impact in view; ours 124 |
