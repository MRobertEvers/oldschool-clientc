# Vasa Nistirio: recorded behaviour

Read from submissions 3285, 843 and 3282 (solo, normal) and 820 (a trio and a four-man raid), all on Grid Master
worlds; see [README.md](README.md). Tool: `tools/cox/rsprox_vasa.py` (sections `special`, `boulders`, `crystals`,
`walk`, `stomp`, `death`). The room's spec is `docs/minigames/cox/rooms/vasa_spec.md`. The content cites these rows
as `RV-*` (`cox_vasa.rs2`, `cox.constant`).

    python3 tools/cox/rsprox_vasa.py ~/.rsprox/binary/archive/{3285,843,3282,820}.txt

**Coordinates.** The transcriber maps every instance tile back to the template tile it was copied from. The CoX
templates are 32x32 rooms at x = 3264 + 32 × variant (0 ccw, 1 thru, 2 cw), z = 5280. A player's printed tile (1x1) is
right. A large NPC's printed tile is not: in a turned room it is another corner of the footprint, and a 90-degree room
breaks the mapping at every chunk edge (`tools/cox/rsprox_tekton.py`). So nothing here measures a player against Vasa's
or a crystal's printed tile. The first reading of this data did that, and it produced the teleport's "nearby 3 /
distant 11-15", since corrected (RV-S2).

Ticks are relative to the event that defines each section. **S** is the special's `vasa_stun_spawn`, **B** a
boulder's launch, **P** a crystal NPC's add. Heights are in this tree's units (raw / 4).

## The special (eight fights)

| Row | Recorded |
|---|---|
| RV-S1 | The rocks wake at **S-5**, and the fire is lit at S-1. At **S** he plays `vasa_stun_spawn` and turns into the walking form. Each raider plays `arceuus_necromancy_anim` with `arceuus_teleport_spotanim`. |
| RV-S1 | At **S+3** the teleport lands: the raider plays `stunned_thieving`, and every prayer goes off. |
| RV-S1 | At **S+8** he plays `vasa_attack`. At **S+12** the raider takes one hit. He walks off at **S+14**, and a crystal is added at **S+15**. |
| RV-S2 | Every raider lands on a tile **6-8** from his middle (Chebyshev), in any quadrant; there is no nearby or distant group. Template-local landings: 820 (11,8), (12,22), (21,11), (20,23), (23,10); 3285 (23,20), all against the thru room's middle (15,15); 3282 (8,21), and 843 stayed on (23,11), against the ccw room's middle (16,15). |
| RV-S3 | The S+8 volley is eight `raids_vasanistirio_magic_travel` projectiles, the k-th with start 8k and end 125 (raw heights 420 → 10). Their `raids_vasanistirio_magic_impact` graphics at S+12 play in the same order, the k-th delayed 3k cycles. |
| RV-S4 | Every teleported raider is hit, wherever they landed (3282: 77). |
| RV-S5 | While blocked, a prayer click answers "Vasa Nistirio has blocked your use of prayers." (820, S+5..S+7). |

The explosion's damage, against the hp read at S-1: 118 → 97, 99 → 80, 95 → 77, 135 → 62 / 74 / 41 / 54.

## Boulders

| Row | Recorded |
|---|---|
| RV-B1 | He plays no animation for a throw. Each volley sends one `raids_vasanistirio_range_travel` per raider, flight 90 cycles, staggered 0 / 5 / 10 (49 / 39 / 32 projectiles). |
| RV-B2 | The target is the raider's tile at the end of B-1, and the impact graphic and the hit both come at **B+2**, on a 3x3. A throw every **3 ticks** while he walks (37 of the 45 gaps). |
| RV-B3 | Any raider is a target: under him (footprint distance 0) and 14+ tiles out alike. Sounds: launch 781 (range 15); impact 1021 (range 8). |

## Crystals and the siphon

| Row | Recorded |
|---|---|
| RV-C1 | He turns into the healing form (`vasa_heal_spawn`) two ticks after his last step (3285: arrival P+11, heal P+13). |
| RV-C2 | The heal splats (type 11, 2 each) come every 2 ticks from the tick after the transform; 820's last is on P+82, the tick of `vasa_healed`. |
| RV-C3 | The crystal plays no defend animation. It retypes itself every 4 ticks (P+4, P+8, P+12, ...): **not implemented**. |

He walks 13-15 ticks to a crystal and 6-15 back (`walk`).

## Stomp

In 3285 he walked through the raider standing in his way: hits 3, 3, 6, 5 on t15780-15783, one each tick under his
5x5. The section's 80 and 67 fall on special ticks (S+12), so they are explosions, not stomps.

## Death

`vasa_death` (D+0); at D+3 the loot message, and both the crystal and Vasa are deleted.

## Open

| Item | State |
|---|---|
| RV-G1: geometry | The recorded crystals' printed tiles stand 14 / 15 apart; our template's at 17 / 18. Printed NPC tiles are not template tiles in a turned room (above), so this is most likely the frame, not the room. The explosion's projectiles fall on a grid at offsets {0, 3, 13} from a source 2 tiles in front of him. Our room keeps NR's route table. |
| Damage | Grid Master perks may change both the explosion (hp − ~19 here) and the stomp, so neither is used for a number. |
| Heal amount | 3285 healed a different amount from what its stored hits give (1176 against 34 × 81). |

## Fixed in our content (2026-10-18)

- The special's tick table: as RV-S1. Teleport, stun, bank and prayers off at S+3; projectiles at S+8 (RV-S3: stagger 8,
  flight 125, heights 105 → 2); the explosion at S+12, impacts staggered 3; walk at S+14.
- The teleport sends every raider to one ring, 6-8 from his middle (RV-S2), and the explosion hits each (RV-S4). It also blocks prayers with the
  recorded message (RV-S5).
- Boulders (RV-B1..B3): no animation, flight 90, stagger 5, any raider in the room, the recorded sounds.
- The siphon (RV-C1, C2): the heal transform at arrival + 2; a splat every 2 ticks. The crystal plays no defend
  animation (RV-C3).
- The teleport's tile must have a walk line back to his middle. Before, an unblocked tile in a pocket of the cave wall
  could be picked: scriptrun seed 1 left the raider there for 226 ticks. When 50 random tries miss, the ring is scanned
  in order, and his middle (under his 5x5) is the last resort.
