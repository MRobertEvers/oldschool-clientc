# Scavenger beasts: recorded behaviour

Read from all five raid recordings (see [README.md](README.md)): 13 visits to scavenger rooms and 17 beasts.

- 843, 3282 and 3285: two solo visits each
- 3389: two visits, one of them with three respawns
- 820: three visits by its four-man party, two beasts each

They gave 15 swings and 2 deaths (both in 3389). Raiders mostly run past the beasts, so the swings are few. Every transcript was read **untranslated** (instance tiles, every raider shown). The beast is 2x2 (`size=2`).

- Tool: `tools/cox/rsprox_scavengers.py`, with sections `room`, `attacks`, `cadence`, `wake`, `movement` and `death`. It refuses translated input.
- `tools/rsprox_tells.py --anchor 7548-7549` lists every tell in the rooms.
- The content cites these rows as `RS-*` in `cox_scavengers.rs2`, `cox.npc` and `cox.constant`.

    python3 tools/cox/rsprox_scavengers.py <raw>/archive/{843,3282,3285,3389,820}.txt

## Opening and chase

| Row | Recorded |
|---|---|
| RS-W1 | **Aggressive, hunt range 5.** A beast nobody had hit faces a raider and swings, after he has come within **5 of its south-west tile** (Chebyshev) at the end of the tick before: 14 of 16 openings to the tick, the other 2 a tick later. The footprint's gap and the centre fit worse (12 of 16 at best). The swing comes 1 tick after the face (7 of 8 solo openings; the 8th, born beside him, swung with it; 3 of 820's came 2 after). |
| RS-W2 | **The chase holds far.** Beasts kept a target 7-11 tiles off where they were first seen (2 ticks at 12-13), and gave up at 10-15. NR's `maxDistance = 4` is not the server's leash. |
| RS-S1 | Idle beasts wander in short walks. |
| RS-N1 | A solo room holds one beast at a time; the four-man 820 party met two. |

## The swing

| Row | Recorded |
|---|---|
| RS-A1 | `skavid_attack` (255), with `goblin_attack` (469) to the target that tick (10 of 10 swings at the local raider). The **hit lands that tick** (15 of 15). At the end of the tick before, the target was adjacent to the 2x2 or one step from it (the beast steps, then swings). |
| RS-A2 | The hits: 0 thirteen times, 5 (normal) and 14 (Challenge Mode), within the wiki's 13 and 27. |
| RS-A3 | Two swings came from one beast in a row: 4 ticks apart, and 11 apart in a chase. |

## Hits, death, drop, respawn

| Row | Recorded |
|---|---|
| RS-B1 | `skavid_block` (256) is delayed to the attacker's projectile. `goblin_hit` (472) plays on each hit (already in `cox.npc`). |
| RS-D1 | `skavid_death` (258) with `goblin_death` (471). The timing is the engine's default death ledger: D = the killing hit + 1, + 1 if the beast stepped the tick before, + 2 if it stepped that tick (2 of 2). The beast is removed on **D+2**. |
| RS-D2 | On D+2, under it: <ul><li>bones (526, despawn 200)</li><li>**mallignum root planks x2** (21036, despawn 3000)</li><li>the two rolls, despawn 200: worms x66 with the secondaries (stinkhorn 8, cicely 3, juice 7), then a fishing rod with the secondaries (9, 6, 6)</li></ul>The plank is a drop of its own: each death had it beside two rolls. |
| RS-D3 | **The next beast** was added D+4 and D+3 after the death seq, 1-3 tiles from where the last one died. |

## Fixed (2026-10-19)

| Was | Now |
|---|---|
| `huntmode=none` (no record line): a beast fought only once hit | `huntmode=aggressive`, `huntrange` 5 (RS-W1) |
| `maxrange=4` (NR) | 10 (RS-W2) |
| no attack sound | `goblin_attack` (RS-A1) |
| every drop at 3000 ticks, and the plank only a 2/18 roll | bones and rolls at 200; mallignum root planks x2 on every death at 3000, as well as the rolls (RS-D2) |

Already as recorded: the seqs (255/256/258), the hit and death sounds, the hit on the swing tick, the death ledger, the removal on D+2 and the respawn on D+4.

`test/raids/_scavengers_tells.lua` checks every swing, every hit sound, each unprovoked opening's range, and every death's seq, sound, removal, bones and planks, plus the next beast's arrival. It plays the room solver on 10 of 10 layouts (small and large). A worktree mutation without `goblin_attack` and the plank drop fails it on both counts. The room test (`cox_scavengers.lua`) passes 10 of 10.

## Open

| Item | State |
|---|---|
| The leash | 10 fits all but a few ticks; recorded holds and drops overlap at 10-13, measured from the first-seen tile, which is not always the spawn. |
| Worms | 66 recorded, outside the wiki's 30-50 (one death; Grid Master?). Ours keeps the wiki. |
| The plank in the rolls | The wiki's 2/18 plank roll is kept; the recordings show only the separate pair. |
| Where the next beast appears | Ours picks a scattered tile in the room; recorded 1-3 from the last death (2 cases). |
| Hunt line of sight, party counts past 820 | Not recorded. |
