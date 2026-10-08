# CoX room tile / stamp audit (Olm-class void risk)

Date: 2026-10-07  
Trigger: Olm walkthroughs landed in void — not the chamber. Question: do any other rooms have the same failure mode?

## What “the Olm mistake” was

1. Olm is **not** a 32-tile grid room; it is a full `m50_89` stamp on `^cox_level_olm`.
2. Entry used square-local `(32,12)` — south **resource** cell of that square — so the avatar stood off the arena floor.
3. Head/hands were placed near the barrier `(29,33)` instead of the surveyed west-wall caves `(20,42)` / `(20,47)` / `(20,37)` (Near-Reality left-side objects).
4. Facing zones used **x** while Olm rises from the **west** wall (NR south/north middle lines are on **z**).

Fix (OSRS-Content `5929b6d828`, parent PR chamber-tiles): entry `(32,24)`, caves aligned, zones on z.

## Audit method

| Check | Source |
| --- | --- |
| Layout plane + cell-z | `cox_layout.rs2` `~cox_room_src_plane` / `~cox_room_src_cz` vs NR `RaidRoom` `height` / `staticChunkY` |
| Spawn / prop tiles | `~cox_pack_local` tables in each room script vs `docs/minigames/cox/sources/nr/room_coords.txt` |
| Loc markers | `tools/cox_template_survey.py --find` vs `cox.constant` |
| Absolute-instance teleports | grep `~cox_coord(` under `minigame_cox/scripts` |
| Test routing | `script/plugins/quest_driver/raid.lua` room id / floor |

## Layout stamp matrix — all grid rooms OK

Every content room’s `(src_plane, src_cz)` matches Near-Reality’s `(height, cellZ from chunkY)`:

| Room | plane | cz | Template |
| --- | ---: | ---: | --- |
| shamans | 0 | 0 | combat `m51_82` |
| mystics | 1 | 0 | combat `m51_82` |
| guardians | 2 | 0 | combat `m51_82` |
| vasa | 0 | 1 | combat `m51_82` |
| tekton | 1 | 1 | combat `m51_82` |
| vespula | 2 | 1 | combat `m51_82` |
| vanguards | 0 | 0 | puzzle `m51_83` |
| muttadiles | 1 | 0 | puzzle `m51_83` |
| icedemon | 0 | 1 | puzzle `m51_83` |
| tightrope | 1 | 1 | puzzle `m51_83` |
| crabs | 2 | 1 | puzzle `m51_83` |
| thieving | 0 | 0 | `m51_84` |
| resource | 0 | 0 | `m51_85` |
| scavenger_small | 0 | 1 | `m51_81` |
| scavenger_large | 1 | 1 | `m51_81` |
| **olm** | level 2 | n/a (full square) | `m50_89` |

Note already in tree: Guardians used to stamp **cz=1** (Vespula’s cell) on plane 2; that is fixed. Same *class* of bug as Olm (wrong geometry, fight still “runs”), but not the current state.

## Spawn tiles vs Near-Reality — no void-class misses

Room scripts store NR-derived tiles as `~cox_pack_local(lx, lz)` under `~cox_*_tile` procs. Spot-checked against `room_coords.txt`:

- shamans, mystics, guardians, vasa crystals, vanguards, muttadile tree/large/small, icedemon braziers, tightrope mages, crabs — **NR tiles present** (per-variant sets contain the NR lists).
- Marker-backed constants match survey: Tekton anvil `(14,22)/(7,13)/(12,22)`, ice brazier `(8,18)`, thieving trough `(7,13)`, Olm caves, scavenger shortcuts `(8,6)/(18,22)/(25,8)`.

No other room uses gameplay `~cox_coord(...)` teleports. Only `cox_olm.rs2` (chamber) and two selftest scratch coords do.

## `coxgoto` / `raid.enter` landing

| Path | Behaviour | Void risk |
| --- | --- | --- |
| Grid rooms | `::coxgoto id floor` → room **centre (16,16)** on stamped cell | Low, if layout plane/cz correct (they are) |
| Olm | `id=7 floor=1` (floor-2 resource) → click `raids_bossentrance` → `~cox_olm_enter` | **Was high** (bad entry tile); **fixed** |
| Resource terminus | `raids_bossentrance` is **loc_add’d at centre** on the resource stamp (not copied from `m51_80`); intentional | N/A |

## Verdict

| Severity | Room | Notes |
| --- | --- | --- |
| Fixed | **olm** | Only room with confirmed void / wrong-chamber landing |
| Clear | all other 15 grid rooms | Stamp matrix + NR spawn tables + no absolute teleports |
| Watch (not the same bug) | resource→olm handoff | Depends on synthetic bossentrance + dialogue; geometry is fine |
| Historical | guardians cz | Already corrected in `cox_layout.rs2` comments |

**No other room needs the Olm-style coordinate rescue.** Remaining visual issues (black scavenger shots, camera, etc.) are camera/lighting or encounter logic — not “player standing in the wrong template cell.”

## Re-run

```sh
python3 tools/cox_template_survey.py --find raids_tekton_anvil
# layout: read ~cox_room_src_plane / ~cox_room_src_cz in cox_layout.rs2
# spawns: ~cox_pack_local tables vs docs/minigames/cox/sources/nr/room_coords.txt
rg -n '~cox_coord\(' OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/
```
