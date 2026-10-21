# Npc info, room by room (rev 241)

Every npc the rooms drive, its npc-info stream against ours: the recording's
per-life record from `tools/rsprox/rsprox_npcinfo.py` (index 4292, and 4288
for Zebak: the rev-241 recordings, named ids) and other revisions where a
block differs; ours from each room test's tick log (`npc_anim`, `npc_spotanim`,
`npc_face`, `npc_headbar`, `npc_retype`, `npc_tile`). Facing is compared with
`face_timeline.py`-style traces (tick, block, repeat or new, against the npc's
tile). Akkha is in [invocations.md](invocations.md) ("Akkha's four").

"Same on screen" marks a block Jagex re-sends that ours sends once, or a tile
face where ours uses an entity face on the same raider: the npc ends up turned
the same way, and the extra blocks are not reproduced.

## Kephri (`toa_kephri_tells`)

| Npc | Block | Recorded | Ours | |
|---|---|---|---|---|
| Kephri | headbar | 13 shielded / weak, 21 enraged | 13 | match |
| | facing | a tile face every tick on her raider's tile at the tick's start (121 of 156), a reset on each egg cast, none while stunned, a reset at death | the entity face (`playerface`) on her attacker, cleared on strikes and breaks | same on screen (rev 227 sent both, which flickered; see `~toa_keph_attacked`) |
| | seqs, spotanims | radial, eggs, stunned, wakeup, death; shield alive / dead / death / reform | the same | match |
| Guardians | headbar | 17 (`_standard_60`) | `_standard_60` | match |
| | death to `[del]` | 4 | 4 | match |
| Swarms | spawn / facing | a tile face on the add, a reset as they walk (38 of 44) | the tile face | same on screen |
| | death to `[del]` | 2 | 2 | match |
| Kites | movement | run, 2 tiles a tick | 2 | match |
| | facing | a tile face every tick on their target | entity face | same on screen |
| Eggs | spawn | south, `nightmare_infection_spotanim_spawn` | the same | match |

Three of `toa_kephri_tells`' own checks (the raider's bow splat, killing an
egg, attacking a swarm) fail on a pack from before this audit too: the
driver's attacks ("not_visible"), not the room.

## Zebak (`toa_zebak_inv_tells`)

| Npc | Block | Recorded | Ours | |
|---|---|---|---|---|
| Zebak | headbar | **20** (`_standard_120`), normal and enraged (135 of 135) | none set: the default | **fixed** (toa.npc) |
| | facing | a tile face on the add | the same | match |
| Waves | spawn / facing | going north: added facing north with `facing angle=1024` (108 of 108); going south: the spawn's south (54); every one a reset at add + 1 (162 of 162) | no facing | **fixed** |
| Jugs | rolling | the rolling form re-sent, teleport + exact_move a step, and `facing angle=` its way (1024 north, 0 south) every step (125 of 125) | no facing | **fixed** |
| Split carrier | spotanim | `zebak_mage_split` / `_ranged_split` at 750 | the same | match |
| Jug break, boulders | spawn, destroy seq | as recorded | the same | match |

## Ba-Ba (`toa_baba_tells`)

| Npc | Block | Recorded | Ours | |
|---|---|---|---|---|
| Ba-Ba | headbar | **21** in every form (rev 223-241) | 18 (the cache's `_standard_80`) | **fixed** (toa.npc) |
| | facing | entity faces (14) and tile faces (8) | 5 and 8 | match in kind |
| | movement | run 2, walk 1, teleports to her throne | the same | match |
| Rolling boulders | facing | `facing angle=512` with every roll, the add's too (896 of 896; cracked 169 of 169) | none | **fixed** |
| Baboons | facing | entity faces on their target (695 in rev 227), a reset at the end | tile faces | same on screen |
| Rubble, sarcophagus npc | headbar, spawn | 0, south | the same | match |

## The Wardens (`toa_wardens_p1_tells`, `toa_wardens_inv_tells`)

| Npc | Block | Recorded | Ours | |
|---|---|---|---|---|
| Phase one Wardens | headbar | 36 (the charge) | 36 | match |
| Obelisk | headbar | 20 | 20 | match |
| Phase two Warden | headbar | 38 (shield), 21 exposed | 38, 21 | match |
| | facing | entity faces on attacks, a tile face about every tick while exposed (64) | tile faces | match in kind |
| Phase three Warden | headbar, facing | 21; a tile face on the add | the same | match |
| Energy orbs | facing | **rev 241**: `facing angle=` its way on the add (66 of 66), then a tile face on the tile it moves to every step (488 of 488), so it turns with its trough; revs 223-227 sent one face_coord nine tiles on | the nine-tiles-on face | **fixed** to rev 241 |
| Siphons | headbar, retype | 0; blue → red with `level_change` and `enabled_ops` | the same | match |
| Amascut, the phantoms | spawn faces, spotanims | as recorded | the same | match |

`toa_wardens_elidinis_tells` stops at phase two on a pack from before these
changes as well (Tumeken's shielded phase two is not fought; the p3/p4 tests
got that fix in 8a797b169, this one did not).
