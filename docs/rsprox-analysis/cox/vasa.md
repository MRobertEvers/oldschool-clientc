# Vasa Nistirio: recorded behaviour

Read from submissions 3285, 843 and 3282 (solo, normal) and 820 (a trio and a four-man raid), all on Grid Master
worlds; see [README.md](README.md). Tool: `tools/cox/rsprox_vasa.py` (sections `special`, `boulders`, `crystals`,
`walk`, `stomp`, `death`). The room's spec is `docs/minigames/cox/rooms/vasa_spec.md`. The content cites these rows
as `RV-*` (`cox_vasa.rs2`, `cox.constant`).

    python3 tools/cox/rsprox_vasa.py ~/.rsprox/binary/archive/{3285,843,3282,820}.txt
    python3 tools/cox/rsprox_vasa_teleport.py <raw>/archive/{3285,843,3282,820}.txt   # the special, instance tiles

**Coordinates.** The transcriber maps every instance tile back to the template tile it was copied from. The CoX
templates are 32x32 rooms at x = 3264 + 32 × variant (0 ccw, 1 thru, 2 cw), z = 5280. Every recorded Vasa room (3285, 843,
3282, 820) is turned a quarter, and in a quarter-turned room the translation mirrors each tile's offset inside its 8x8
chunk -- the player's tile included -- while a large NPC's printed tile is also another corner of its footprint. So nothing measures a player against Vasa's
or a crystal's printed tile in a translated transcript. **The teleport (RV-S2..S4) is read from untranslated
transcripts** (`TRANSLATE_INSTANCED_COORDS=false`, every player shown; recipe in [README.md](README.md)): instance
tiles, where the raider and Vasa's south-west tile share one frame. Two earlier readings measured in template tiles
and were both wrong ("nearby 3 / distant 11-15", then "one ring 6-8, 10-15 from where they stood").

Ticks are relative to the event that defines each section. **S** is the special's `vasa_stun_spawn`, **B** a
boulder's launch, **P** a crystal NPC's add. Heights are in this tree's units (raw / 4).

## The special (eight fights)

| Row | Recorded |
|---|---|
| RV-S1 | The rocks wake at **S-5**, and the fire is lit at S-1. At **S** he plays `vasa_stun_spawn` and turns into the walking form. Each raider plays `arceuus_necromancy_anim` with `arceuus_teleport_spotanim`. |
| RV-S1 | At **S+3** the teleport lands: the raider's seq is stopped (`sequence id=-1`, every raider, both groups), the near ones play `stunned_thieving`, and every prayer goes off. The cast ends on the caster vanished and holds that frame (framestep 1), so the stop is what makes them visible again. |
| RV-S1 | At **S+8** he plays `vasa_attack`. At **S+12** the raider takes one hit. He walks off at **S+14**, and a crystal is added at **S+15**. |
| RV-S2 | **Next to him.** The targets are split: half, rounded up, land on a tile **3-4 from his middle** (SW + 2, Chebyshev) -- 1 or 2 off his 5x5 -- and are stunned; the rest land **8-11** out, by the walls, with no stun graphic, and are free to move (820: running at S+4). Solo, the raider always lands next to him. 13 of 13 near landings at 3-4: 3285 rel-to-SW (5,3) -> (-1,3), east face to west face; 3282 (-1,2) -> (5,3); 843 already stood at (-1,2) beside his west face and stayed there (no teleport packet); 820, all five trio specials, two near and one far each (far at 10, 8, 9, 11, 10). There is no rule about where the raider stood. |
| RV-S3 | The S+8 volley is eight `raids_vasanistirio_magic_travel` projectiles from his middle, the k-th with start 8k and end 125 (raw heights 420 -> 10), to the tiles (-1, 2, 5) x (-1, 2, 5) from his SW, less the middle. Their `raids_vasanistirio_magic_impact` graphics at S+12 play in the same order, the k-th delayed 3k cycles. |
| RV-S4 | **The explosion hits everyone within 4 of his middle at S+12** -- the area the eight 3x3 impacts cover -- with the same amount each. The near group is always in it (stunned where they landed). In 820 the far raider ran back in three times (under him, m1-m3) and took the same splat as the two stunned raiders (62/62/62, 41/41/41, 54/54/54); left at 7-8 out twice, they took nothing (74/74, 66/67 on the other two). Wiki: "players who are teleported away from Vasa can further divide the damage up by running next to him". |
| RV-S5 | While blocked, a prayer click answers "Vasa Nistirio has blocked your use of prayers." (820, S+5..S+7). |

The explosion's damage, against the hp read at S-1: 118 → 97, 99 → 80, 95 → 77, 135 → 62 / 74 / 41 / 54.

## Boulders

| Row | Recorded |
|---|---|
| RV-B1 | He plays no animation for a throw. Each volley sends one `raids_vasanistirio_range_travel` per raider, flight 90 cycles, staggered 0 / 5 / 10 (49 / 39 / 32 projectiles). |
| RV-B2 | The target is the raider's tile at the end of B-1, and the impact graphic and the hit both come at **B+2**, on a 3x3. A throw every **3 ticks** while he walks (37 of the 45 gaps). |
| RV-B3 | The targets are the raiders **1-8 from his 5x5**, measured from where he stands after that tick's step: 128 of 128 (untranslated; 820: 8, 6, 13, 17, 9, 23, 28, 9 at 1..8), **never one under him**. The wiki's "within 8 tiles of him but not underneath". (Read first from template tiles as "under him and 14+ out alike": the frame error, RV-S2.) Hit: a raider 0-1 from the rock at the end of B+1 (a 3x3); 0 of 40 at 2. Sounds: launch 781 (range 15); impact 1021 (range 8). |

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
| RV-G1: geometry | The recorded crystals' printed tiles stand 14 / 15 apart; our template's at 17 / 18. Printed NPC tiles are not template tiles in a turned room (above), so this is most likely the frame, not the room: re-read it from an untranslated transcript. (The explosion grid, once read as "{0, 3, 13} from a source in front of him", was the same frame: in instance tiles it is (-1, 2, 5) from his SW, RV-S3.) Our room keeps NR's route table. |
| Damage | Grid Master perks may change both the explosion (hp − ~19 here) and the stomp, so neither is used for a number. |
| Heal amount | 3285 healed a different amount from what its stored hits give (1176 against 34 × 81). |

## Fixed in our content (2026-10-18)

- The special's tick table: as RV-S1, including the seq stop at S+3 (without it the raider stayed invisible under the
  stun's birds; `test/raids/_vasa_stun_birds.lua`). Teleport, stun, bank and prayers off at S+3; projectiles at S+8 (RV-S3: stagger 8,
  flight 125, heights 105 → 2); the explosion at S+12, impacts staggered 3; walk at S+14.
- The teleport (RV-S2): half the targets, rounded up, at random, land 3-4 from his middle, stunned, prayers off and
  blocked with the recorded message (RV-S5), their hp banked; the rest land 8-11 out, free. The explosion (RV-S4)
  splits the bank over everyone within 4 of his middle at S+12; a runner under Protect from Magic takes 0 (wiki).
  `test/raids/cox_vasa.lua` checks every landing off the server's tick log (`vasa.teleport_ring`). (2026-10-18,
  replacing the one 6-8 ring.)
- Boulders (RV-B1..B3): no animation, flight 90, stagger 5, the recorded sounds; at raiders 1-8 from his 5x5 after his
  step (`^cox_vasa_boulder_reach`, `~cox_vasa_next_tile`; 2026-10-18, replacing "every raider in the room").
- The siphon (RV-C1, C2): the heal transform at arrival + 2; a splat every 2 ticks. The crystal plays no defend
  animation (RV-C3).
- The teleport's tile must have a walk line back to his middle. Before, an unblocked tile in a pocket of the cave wall
  could be picked: scriptrun seed 1 left the raider there for 226 ticks. When 50 random tries miss, the ring is scanned
  in order, and his middle (under his 5x5) is the last resort.
