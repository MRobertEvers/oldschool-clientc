# Akkha

Read from 8 kills: 14 (solo, Entry) and 1852 (7 Expert raids). Ticks are
relative to entry (E) or the event named. Full inventory:
[akkha.out](akkha.out). Common entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_akkha.rs2` (not yet compared).

## Npcs

| Npc | Id | Where |
|---|---|---|
| Akkha, waiting | `akkha_spawn` (11789) | (3673,5407), retypes to `akkha_melee` on E+5..10 |
| Akkha's styles | `akkha_melee` (11790), `akkha_range` (11791), `akkha_mage` (11792) | |
| Enrage | `akkha_enrage_spawn` (11793) → `akkha_enrage_initial` (11794) → `akkha_enrage` (11795) | |
| Shadows | `akkha_shadow` (11797); enraged `akkha_shadow_enrage` (11798) | (3674,5401), (3674,5412), (3685,5401), (3685,5412) |
| Shadow health bars | `akkha_headbar_npc` (11805), headbar 36 | the corners (3671,5398), (3671,5416), (3689,5398), (3689,5416) |
| Trailing orbs | `akkha_trail_orb_darkness` (11801), `_burn` (11802), `_freeze` (11803) | |
| Enrage orbs | `akkha_enrage_orb` (11804) | |

Akkha's model carries a tint while in a style: `start 0, end 600, hue 0,
saturation 0, lightness 106, weight 40`, cleared with `0/0/255/255/255/0`.

## The basic attack

Every **6 ticks** (68 of 92 melee, 78 of 95 ranged, 72 of 87 magic gaps).
The style changes melee → ranged → mage → melee by a retype (24/21/16 seen),
and the attack goes on through it.

| Style | Sequence | Projectile (from Akkha's tile to the raider) |
|---|---|---|
| Melee | `npc_akkha_attack_melee_spear` (9770) | — |
| Ranged | `npc_akkha_attack_ranged_spear` (9772) | `akkha_ranged_travel` (2255): start 0, angle 12, slope 32, height 101 → 34, **end = max(30, 6 × distance)** (30 to 5 tiles, 36, 42, 48, 54 … 72 at 12) |
| Magic | `npc_akkha_attack_magic_spear` (9774) | `akkha_magic_travel` (2253): same, **end = max(30, 10 × distance)** (30 to 3, 40, 50, 60, 70, 80) |

## Specials (they replace a basic attack)

| Sequence | What follows |
|---|---|
| `npc_akkha_attack_magic_01_spear` (9776) | **Detonate:** one element's quadrant explodes 6 ticks later (some tiles 8) |
| `npc_akkha_attack_magic_02_spear` (9777) | **Memory:** cast+3 `npc_akkha_teleport_disappear_spear` (9784); from cast+4 the arena symbols glow one at a time **2 ticks apart** (4–6 glows): `akkha_symbol_glow_lightning` (45869) at (3682,5406), `_freeze` (45870) at (3679,5406), `_darkness` (45871) at (3682,5409), `_burn` (45868) at (3679,5409), each removed with a `loc_del` before the next. Then the quadrants explode in that order, **4 ticks apart** (4–6 waves); Akkha reappears (`npc_akkha_teleport_appear_spear`, 9785) after the last |
| `npc_akkha_attack_magic_03_spear` (9778) | **Trailing orbs:** from cast+3, one `akkha_trail_orb_<element>` a tick behind a raider for several ticks; area `toa_akkha_black_orb_spawn_02` with each |
| `npc_akkha_attack_magic_04_spear` (9779) | **Enrage:** on the enrage retype, then disappears 4 ticks later |

### A quadrant explosion

The four elements own the arena's quadrants (about 12 × 12 each, overlapping
on the centre lines):

| Element | Graphic | Quadrant |
|---|---|---|
| Lightning | map_anim `akkha_lightning` (2256) | x 3678–3690, z 5398–5410 (south-east) |
| Freeze | `akkha_freeze` (2259) | x 3671–3682, z 5398–5408 (south-west) |
| Darkness | `akkha_darkness` (2257) | x 3679–3690, z 5405–5417 (north-east) |
| Burn | `akkha_burn` (2258) height 124 | x 3671–3683, z 5405–5417 (north-west) |

Each tile's delay is about **27 + 3 × its Manhattan distance from the arena
centre (3680.5, 5407.5)**, capped at 60 (16,305 of 17,325 tiles); a few
centre tiles have delay 0. So an explosion spreads out from the middle.

## Shadows

At **80%, 60%, 40% and 20%** of Akkha's health (headbar fill 80/60/40/20 of
100, all 8 kills), four `akkha_shadow` npcs appear together, one at each
corner tile above, with `npc_akkha_spawn_spear` (9790). A shadow can cast
`npc_akkha_attack_magic_02_spear` (9777); it dies with `npc_akkha_death_spear`
(9792), `spotanim -1`, ops 0b00000. They live 21–116 ticks.

Each corner's arena locs change state with its shadow:
`akkha_arena_outer_lvl2_22/_23/_57/_58` (45981, 45982, 46016, 46017) at
(3671,5416), (3689,5416), (3671,5398), (3689,5398), with `_empty01`,
`_empty02` and `_full01` variants (46056–46066), re-sent every tick while
the fight runs.

## Enrage

After the fourth shadow phase: `akkha_enrage_spawn` with
`npc_akkha_attack_magic_04_spear`, 2 ticks later `akkha_enrage_initial`,
2–3 later `akkha_enrage`. Then:
- Akkha teleports every **7–10 ticks**: `npc_akkha_teleport_disappear_spear` and
  `_appear_spear` on the same tick, with a retype to itself; the shadows
  become `akkha_shadow_enrage` and do the same.
- **Enrage orbs** every **3 ticks**, 5–8 at a time: `akkha_enrage_orb` (11804),
  each moving one tile a tick in one of the eight directions, living 10–23
  ticks; spotanim `akkha_enrage_orb_impact` (2260) height 158 delay 10 when one
  reaches a raider.
- Death: `npc_akkha_death_spear` (9792).

## Sounds

Area `toa_akkha_defend_01` on Akkha when hit; synth
`toa_akkha_scatter_across_area_05` with a memory cast.
