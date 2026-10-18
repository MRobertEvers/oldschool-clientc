# Path of Het

Read from 8 visits: 14 (solo, Entry) and 1852 (7 Expert raids). Ticks are
relative to entry (E). Full inventory: [het.out](het.out). Common
entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_het.rs2` (not yet compared).

## Layout

| What | Id | Tile |
|---|---|---|
| Player teleported to | | (3697,5279), the east end (beside the pickaxe table: "You take the Dragon pickaxe.", `human_pickuptable` (832), synth `pick2`) |
| Beam source | loc `toa_het_source` (45486) shape 10 rot 1 | (3676,5279) |
| Statue | loc `toa_het_target` (45485) shape 10 rot 3 | (3682,5279) |
| Seal | the boss bar: `varp1683_hpbar_hud_npc` = `toa_het_goal` (11706) while shielded, `toa_het_goal_vulnerable` (11707) while weakened | dies as loc `toa_het_goal_dead` (45489) at (3679,5279) |
| Mirrors | movable `toa_het_mirror` (45455) shape 11 (diagonal), fixed `toa_het_mirror_fixed` (45456) shape 11 | placed on E+9 with the maze |

"Challenge started: Path of Het." and the room song `song_732` "Test of
Strength" on E+9. The seal's base hp (`varb6100_hpbar_hud_basehp`) is 130 in
14 (solo Entry) and 225 in the 2-raider Expert raids.

## The cycle (every 44 ticks)

| Tick (R = the maze rises) | Recorded |
|---|---|
| R−2 | The maze's walls rise: `toa_het_barrier_internal_rising` (45459), `_end_rising` (45461), `_destructable_internal_rising` (45463), `_destructable_end_rising` (45465), all shape 10, rotations as the wall runs; area `toa_poh_rising_barrier_02` (6535) loops 1 range 15 at (3680,5280) |
| R | They become `toa_het_barrier_internal` (45458), `_end` (45460), `_destructable_internal` (45462), `_destructable_end` (45464). Mirrors are placed. The bar is `toa_het_goal` (shielded) |
| R+10, then every **9 ticks** | The beam (below) |
| B = the beam reaches the statue | "<col=185820>The statue has been struck! The seal weakens!</col>" and "The room begins to shake violently!"; the bar becomes `toa_het_goal_vulnerable`; mirrors a raider holds break ("The shaking breaks the mirrors you're holding.") |
| B+1 | Rock fall: map_anim `rockfall` (60) delay 15 on tiles over the maze; the maze's walls are replaced by `invisible_type8_nonblocking` (29733) |
| B+1..B+24 | The seal can be mined: "You swing your pick at the seal.", synth `mine_ore` (3600); hits take 5–12 a swing |
| B+24 | The maze rises again (the next R−2) with a new layout, and the bar goes back to `toa_het_goal` |

Mining the shielded seal: "A strange force is protecting the seal from damage.
It must be weakened somehow." The seal at 0: `toa_het_goal_dead` on that tick
and the bar closed (`varp1683` −1) 8–14 ticks later.

## The beam

Each shot, from the source along the path the mirrors make:
- area `toa_poh_light_beam_proj_01` (6540) loops 1 **range 30** at (3676,5280);
- on each tile, map_anim `fx_beam_attack01..08` (2064, 2114–2119) height 255
  with **delay = the tile's position along the path** (1, 2, 3, … cycles; up to
  36 seen). The 01..08 variants are the segment's shape (straight / turning);
- at each mirror, area `toa_poh_light_beam_mirror_reflect_01` (6538) loops 1 range 3;
- where it stops: map_anim `fx_beam_impact01` (2120) height 255 with that
  tile's delay, area `windbolt_hit` (219) **loops 6** range 6 with the same
  delay, and area `toa_poh_light_explode_02` (6546) loops 1 range 5.

A raider in the beam: "You have failed <col=0cc919>Perfect Het</col>: Z-Kris
was hit by a light beam."

## Mirrors and barriers

| Action | Recorded |
|---|---|
| Pick up / put down a mirror | synth `put_down` (2739); "There isn't enough room to put that here." when blocked; a placed mirror is a `toa_het_mirror` shape 11 at the chosen rotation |
| Dirty mirror | "You quickly brush away the dust coating the mirror." |
| Destructable wall | "You swing your pick at the barrier.", `mine_ore`; "The barrier crumbles apart." and it becomes `toa_het_barrier_destroyed` (45466) shape 10 |

## Orbs

| | Recorded (344 orbs) |
|---|---|
| Npc | `toa_het_orb` (11708) |
| Spawn | every **7 ticks** from R+11, on an edge tile (west x 3670–3671, east 3689–3690, south z 5271–5272, north 5287–5289); map_anim `shadow_burst_impact` (382) and area `toa_path_of_het_orb_spawn_01` (6545) loops 1 range 4 there |
| Move | one tile a tick in one straight direction (324 of 344), by `teleport` |
| End | after 1–12 ticks, at a wall or a raider: map_anim `shadow_rush_impact` (379) and area `shadow_rush_impact` (179) loops 1 range 4; removed |
