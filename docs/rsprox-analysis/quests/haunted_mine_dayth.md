# Haunted Mine: Treus Dayth (the fight)

Rebuilt 2026-11-22 from RSProx index 4647 (rev 242.1, the owner's own and the only playthrough):
fight 1 [5543..6060] (the recorder Varrock-teleported out at [6053]) and fight 2 [6721..6948]
(the kill at [6944]). The rest of the quest (the key's op up to the mesbox continue, the take
after the kill, the crystal doors) is `docs/rsprox-analysis/quests/haunted_mine.md`.

Files (all new or rewritten for this section):
- `OSRS-Content/osrs-content/server/scripts/quests/quest_hauntedmine/scripts/hauntedmine_dayth_fight.rs2`
- `.../quest_hauntedmine/configs/hauntedmine_dayth.constant`, `hauntedmine_dayth.varn` (13 varns,
  `pack/varn.alloc` 71..83)
- `.../quest_hauntedmine/configs/quest_hauntedmine.npc` (both Dayth forms; the invented
  aggressive/huntrange/rangedefence/elemental contract removed)
- `.../_explicit/configs/server_fields.generated.npc`: `defaultmode`, `wanderrange`, `maxrange`,
  `death_delay`, `retaliate` deleted from `[hauntedmine_boss_ghost]` and
  `[hauntedmine_boss_ghost_faded]` (now stated in the quest's .npc; a field stated twice fails
  the server pack)
- `test/quests/_hauntedmine_dayth.lua` (scriptrun, 17 rows)
- `quest_hauntedmine/scripts/hauntedmine_dayth_stub.rs2` deleted (the parent's stub)

## The seam

| proc | contract |
|---|---|
| `~hmq_dayth_ambush` | Called by the key op (hauntedmine_dayth.rs2) LAST, on the voice mesbox's continue (T), after stage 8 is written. Suspends the calling player script T..T+7 (`p_delay`s: the recorded hold, busy 1). Adds Dayth, owns everything to the kill. |
| `~hmq_dayth_live()(boolean)` | True while this player's Dayth (either form, not dying) is up. Puts the CALLER's active npc back before it returns (it needs one: the key). |
| `~hmq_dayth_on_logout`, `~hmq_dayth_on_death` | player/logout.rs2:45, player/death.rs2:50. Mark the player's Dayth gone (deleted on his next tick); safe with none up; may leave the active npc changed. |
| stage | Mine: 9 (`^hmq_dayth_killed`) on the killing tick, with the mesbox. Never writes 7 or 8. |
| player varps | None. State is on the npcs (varn). `current_hint_arrow` (14192) is written at T+7, never cleared (as recorded). |
| softtimer | `hmq_dt_watch` on the player from T: deletes a Dayth left behind 20 ticks after the player left the room (scriptrun does not process the room's npcs while nobody is there). |

## The rise (T = the continue: [5543], [6721])

| tick | recorded (both rises identical) | ours |
|---|---|---|
| T | Dayth `[add]` 2788,4458 spawnangle south, `bossghost_summon`; `cam_moveto` 2788,4454 h400 0/100, `cam_lookat` 2788,4458 h80 0/100; synth `bigghost_appear` (1595); `if_closesub` chat | same (`if_close`) |
| T+2 | `cam_lookat` 2788,4455 h80 1/50, `cam_moveto` 2782,4455 h800 1/50 | same |
| T+4 | crane 19469 (2787,4451) `mining_crane_attack` delay 50 + `[facing] coord=` the player's tile; synth `crane_move_and_snap` (1594); Dayth `ghost_update_tendrill_summoning` | same (the crane edge-on to the player) |
| T+7 | busy 1 -> 0; the snap's `[hit] 12, 0` + `human_block_1` d20 (2/2 zero); Dayth `bossghost_fade_out`; `cam_reset`; `current_hint_arrow` (0 ->) 7, `hint_arrow reset`, `hint_arrow npc` | same (3/3 runs) |
| T+8 | ghost form (`transformation` 3617, `level_change` 95, `enabled_ops` 0b11111), `[facing] type=reset`, glide (-2,+2 from the hub) | same |
| T+8..12 | 2788,4458 -> (-2,2) (-4,2) (-6,2) (-7,3) -> end (-9,5) / (-8,3) | same nodes; end one of the NW variants |
| T+12 | `bossghost_fade_in`, `[facing] player=`, synth `bigghost_reappear` (1601); no summon | same |
| T+13 | Treus Dayth again (`transformation` 3616) | same |
| T+17 | first summon (2/2) | same (6/6) |

## Per-type records

**Treus Dayth / ghost form (3616 / 3617), dynamic npc (index 60691, 54897).** Spawn 2788,4458
south. Never walks, hunts, swings or retaliates. Moves only by `teleport` blocks of 1..2 tiles
(glides). Seqs: `bossghost_summon` (rise), `ghost_update_tendrill_summoning` (every summon),
`bossghost_fade_out` / `_fade_in`, `ghost_update_tendrill_defend` (on every player shot, delay =
the arrow's endtime: engine), `ghost_update_tendrill_death` on the killing splat. Facing: the
player from each fade-in to the next fade-out, `type=reset` with each ghost form. Headbar id 0
(30 wide). Levels: cache stat1..4 70/100/70/100 (ghost 1/195/1/200); damage carries across forms
(npc_changetype's newBase - (oldBase - current)). Sounds: defend `bigghost_hit` (36/36 hits,
zeros too), `bigghost_disappear` with each hit-fade, `bigghost_reappear` each fade-in,
`bigghost_death` + death seq + ops 0 + facing reset on the killing tick; `[del]` 4 ticks on.

**Cranes `crane_posessed_mine` (3618)** at 2782,4458 / 2787,4446 / 2787,4451 / 2793,4458 (3x3,
world spawns): only `mining_crane_attack` + `[facing] coord=` the player's tile (the facing
latches; re-adds carry it).

**Carts `hauntedmine_boss_cart` (3620)**, world npcs on seven stretches (below); move only by
1-tile `teleport` blocks (145/145), one a tick, end to end; facing follows the roll (turnspeed
1024). Positions persist between fights. The `[del]/[add]` pairs in the transcript are the
15-tile view edge, not content.

**Key `hauntedmine_boss_key` (3619)**: the parent's.

## Rules (fitted on both fights)

| rule | evidence |
|---|---|
| Summons while seen: every 5 ticks; the first 0..4 after he reappears (D+0 3, +1 5, +2 6, +3 2, +4 7), D+4 after the rise | 62/62 gaps of 5 in ours |
| A summon picks by the player's tile (after this tick's cart shoves, before the player steps): edge-on to a crane's 3x3 (corners excluded) -> that crane; on a cart's stretch -> that cart (nothing at all if it is rolling); else a pickaxe | 150/150 attack ticks |
| Crane bite: hit +1 tick, `crane_jaws` (1608) | 39/39 |
| Cart: `cart_away` (1602) on the summon, rolls from +1 toward the player to the end of its stretch, shoving the player standing on its next tile one tile on: `[exactmove]` tile -> next, d1 10, d2 30, angle back at the cart; `human_stumble_back_continuous` d10; `cart_hit` (1603); the splat | 30/30 shoves |
| Pickaxe: `map_anim` 308 h50 on a free tile 1..5 from the player, `map_projanim` 309 homing on the player, start 20, end 28 + 8 x distance, heights 0, angle 0, progress 0; `pickaxe_attack` (1611); hit +2, +3 from distance 4 | 80/80 endtimes, 80/80 landings |
| A hit on him (0s too) fades him two times in three | 23 of 34 (2/3: 22.7) |
| Fade: O `bossghost_fade_out` + `bigghost_disappear`; O+1 ghost form, facing reset, first step; back along his arm to the hub 2788,4456, a tick there, out along the new arm, end tile, 4 ticks, fade-in (+ face, `bigghost_reappear`, a summon), next tick Treus Dayth | O->F 25/25, A->I 16+7/16+7 |
| Destination: one of the three spots not his own or beside it, in the ring N, NE, E, S, W, NW | 22/22 (uniform over 5 would avoid neighbours 22 times with p = 1e-5) |
| Gliding summons: at the hub, at the arm's last node, at the fade-in; the S arm also at (0,-4) out and (0,-7) in | every glide, both fights |
| Player gone from the room: summons stop; a glide still ends in its fade-in sound, then he is gone; stage stays 8; the key raises a new one | [6053..6060], [6713] |

Arms (offsets from 2788,4456; the summon node marked *): N (0,2) (0,4) (0,6)* end (0|1,7);
NE (2,1) (4,2) (6,3) (8,3)* end (9,5)|(8,4)|(8,5); E (2,-1) (4,-3)* end (6,-3);
S (0,-2) (0,-4)* (0,-5) (0,-7) (0,-9) (-1,-11)* end (-1,-12)|(-1,-13); W (-2,0) (-4,0) (-6,0)
(-7,-2)* end (-9,-2)|stay; NW (-2,0) (-4,1) (-6,2) (-7,3)* end (-8,5)|(-9,5)|(-8,3).

Cart stretches: x2791 z4447..4452; x2785 z4447..4452; x2793 z4446..4454; x2783 z4446..4453;
z4462 x2781..2795; z4456 x2783..2784 (called from 2782 too: 6/6 "cart_away, no roll");
z4456 x2792..2794.

## Damage (every splat on the player has a cause)

| cause | recorded n / zeros / max / mean nonzero | ours (3 scriptrun fights) | verdict |
|---|---|---|---|
| pickaxe | 78 / 17 (22%) / 10 / 5.9 (Missiles on or off: same) | 113 / 26 (23%) / 10 / 5.0 | matched; `random(7)` miss + 0..10 |
| crane bite | 37 / 11 (30%) / 10 / 6.0 | 56 / 24 (42%) / 10 / 5.2 | matched within sampling; `random(4)` miss + 0..10 |
| crane snap (rise) | 2 / 2 / 0 | 6 / 6 / 0 | matched |
| cart shove | 30 / 11 (37%) / 7 / 2.3 | 22 / 5 / 7 / 2.5 | matched; 0..2, 1 in 6 0..7 |
| none | 0 | 0 in the fight (3 pickaxes held behind the kill mesbox, see Open) | |

Protect from Missiles changes none of them (pickaxe prayed 58: 24% zeros, max 10; unprayed 20:
15%, max 10). All typeless (`damage`, from the player's own queue).

## Wire (scriptrun, final runs)

| block / event | recorded | ours | verdict |
|---|---|---|---|
| Dayth kinds: sequence, transformation, level_change, enabled_ops, facing, hit, headbar | all | all | every kind sent (the ghost form's 1 hit/headbar is [5976], a hit on the fade-in's transform tick) |
| ghost glide steps | gaps 1:71 2:8, diagonal 34% | 1:86 2:10, diagonal 37% | matched |
| cart steps | 1-tile teleports | 1-tile steps (ours prints walk) | same look (PACKETS.md) |
| O->F, A->I, K->del | 1, 4, 4 | 1 (37/37), 4 (28/30), 4 (3/3) | matched |
| sounds | 1594 1595 1597 1598 1599 1601 1602 1603 1608 1611 | all present | matched |

## Open

- **Multi-combat**: the recording has `multiway_indicator` 1 everywhere in map square 43_69
  (Dayth's room, its stair landing, the crystal hall: [5331..5401], [5502..6053], [6697..6999]);
  `maps/multiway.csv` has no 0_43_69 zone. Not mine (shared data): add the square.
- **Facing re-sends**: Jagex sends `[facing] player=` on most angle changes (203 in both fights);
  ours fewer (the engine's face-follow rule). Not compared tick by tick.
- **Pickaxe source distance** (1..5): recorded 24/21/16/8/11; ours (distance first, then a tile
  on that ring) 25/30/17/29/16. The picker is approximate.
- **Cart positions**: the world spawn (`m43_69.spawn`, generated) puts the two z4456 carts at
  2785 and 2791, outside their recorded stretches (they then roll back into them), and the other
  five at the far ends from where 4647 first saw them. Unverified which is Jagex's spawn.
- **Unverified rules**: the player leaving while Dayth is seen (ours: gone at his next summon);
  logout/death (gone next tick); the player's watch (20 ticks); pickaxes in the air at the kill
  still land (held by the engine until the kill mesbox closes); the ghost form clicked: ours says
  "You can't attack this npc." (the recorder never clicked it).
- **Conformance row** `test/quests/_conformance.lua` 9816-9894 (`seam.npc_cover_settled_recovery`,
  `seam.npc_tele_reslot_followed`) expects the old "Treus Dayth rises" line and a >2-tile npc_tele
  re-slot; neither exists now (the pages, then 2-tile glides). Needs rewriting by its owner.
