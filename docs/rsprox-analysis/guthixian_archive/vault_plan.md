# The vault: spec from RSProx

Spectated: index 4301 [1969..2045] (three raiders) and 4308 [2406..2436]
(four, B0aty's; `--all-players` for the raiders' walks and seqs). Both
recorders are spectators, so a raider's own loot interface is in neither. The
wiki (`sources/Chest__The_Fractured_Archive_.wiki`) has the loot. Template
square (64,90), plane 2; tiles below are local to it.

## 1. The room (A = the raiders' arrival)

| tick | recorded |
|---|---|
| A | each raider lands at (31,14) (4127,5774,2) facing north, four tiles south of where 4301 [1969] first shows Vampyire (he had run on; `nexus_plan.md` §6: 4 of 5 landings seen standing), and runs north to the chests; the timer HUD frozen at the raid's total with its stopped flag (clientscript 10036 `(2115, 1)` every tick from Ocellus's death, 4301 [1943..2044]); song 147 |
| A | `tfa_vault_scoreboard` "Merit scroll" (33,18) r1 shape 10; `tfa_vault_exit` "Rift" (31,13) r0 shape 10 |
| A | the eight chests `tfa_rewards_chest_vault_N_closed` (multinpc on varbit 16433 `tfa_vault_own_chest`: chest N is `_closed_mine` to the raider whose varbit is N, `_closed_notmine` to everyone else), each `[facing] coord=` (31,40): 1 (28,33), 2 (34,33), 3 (25,36), 4 (37,36), 5 (24,40), 6 (38,40), 7 (25,43), 8 (37,43) (2/2) |
| A | five `tfa_butterfly01` at (35,35), (34,40), (27,34), (31,37), (21,47), wandering a tile a tick (2/2) |
| A+2 | the raid HUD again (`tfa_hud`) |
| A+9 | two `tfa_brazier_guthixloot01_fire01` (shape 22): (35,45) r0 and (27,45) r2 (4301 [1978], 4308 [2415]) |

Each raider's chest is their party slot: three raiders opened 1..3 (4301),
four 1..4 (4308); 5..8 stay shut.

## 2. The chests

| tick | recorded |
|---|---|
| L+1 | the raider faces the chest and plays `human_tfa_small_chest_open` (4308 [2415..2417], 4/4) |
| L+2 | the chest `transformation` to `tfa_rewards_chest_vault_N_open` ("Rift", op Search), `chest_guthixian02_open01`, `enabled_ops` 0b11111, `level_change` 0; the raider faces it (4/4) |
| first open +5 | `tfa_vault_exit_small` "Rift" (Leave, Quick-leave), 3x3 at (30,36), facing north (instant), `transparency` 0..90 cycles 255 -> 0 (4301 [1984] -> [1989]; 4308 [2416] -> [2421]) |

The loot (the wiki): a loot interface (cache: `tfa_chests` 973 over inv
`tfa_chests`, size 6: the item grid, Take-all, Bank-all, Discard-all); three
rolls on the standard table, each quantity in its range (one of the three
five-fold "under unknown conditions"); tertiaries (Archivist's folio (inert),
the four fragments, an elite clue); cabbage for a poor performance; one item
from the unique chest on the platform. 4308 [2424]: Gnomonkey took an
`archivists_folio_empty` into slot 11.

## 3. Leaving

| tick | recorded |
|---|---|
| L+1 | Leave on the small Rift (4308: Gnomonkey beside it at [2428]) |
| L+3 | the lobby: (4129,5561), (4130,5559), (4130,5561) (4308 [2431], three raiders) |

## Audit: our scriptrun against 4301, 4308 (2026-10-21)

Run: `test/guthixian_archive/tfa_vault.lua` (solo; `::tfaroom 7 6`; chest 2
Open does nothing; chest 1 Open, Take-all; Leave). The way in is checked by
`tfa_ocellus.lua`, which now travels with The Blade after the kill.

| tell | recording | ours | verdict |
|---|---|---|---|
| room locs | scoreboard (33,18) r1, Rift (31,13) r0, both shape 10 | same, [78] | matched |
| chests | eight, their tiles, facing (31,40), multinpc on 16433 = party slot | same; 16433 sent (`varp5922` declared transmitted) | matched |
| butterflies | five, their tiles, wandering | same | matched |
| braziers | A+9, (35,45) r0 and (27,45) r2, shape 22 | [87] = A+9 | matched |
| chest open | raider seq at L+1, the chest's open form and `chest_guthixian02_open01` +1 | seq at L, chest +1 | **open (engine: an npc op fires on the arrival tick, the recording a tick later)** |
| small Rift | first open +5, (30,36), north, fade 0..90 | [103] -> [108] | matched (on the chest's queue: the raider's room tick waits while a modal is open) |
| leave | L+1 op, L+3 the lobby | L op, L+2 the lobby (4130,5559) | same engine offset as the chest |
| timer | frozen at the total, flag 1 | frozen at Ocellus's death (`^tfa_var_raid_end`), flag 1 | matched |
| song | 147 | 147 | matched |
| loot interface | private | `tfa_chests` on the chest's open tick | unverified |
| loot table | the wiki: three rolls, ranges, tertiaries | three uniform rolls, the ranges, one tertiary in 20 | unverified rates; the five-fold roll, the cabbage, the elite clue not built |
| unique chest | the wiki (platform, one item) | not built | not built (no recording has one) |
| unclaimed loot | the wiki: the loot wheelbarrow | banked on Leave | stand-in |
| Merit scroll | Read (no recording of it) | no handler | not built |

## NPC info audit (2026-10-21)

Every NPC_INFO block of every npc of the room, recording against our tick log, per life:
`tools/rsprox/npc_stream.py --match <type> --summary <transcript> <ticklog>` (counts per
life), `tools/rsprox/npc_timeline.py` (the recording's blocks) and
`tools/rsprox/ticklog_npc_timeline.py` (ours, with the file-only `npc_info` rows:
spawn angle, ops, level, transparency, exact move, timed bar). Facing is part of npc info
and is audited block by block (the entity face, the coord face and its `instant`, the angle,
the reset, the walk type).

| npc | block | recorded | ours now | verdict |
|---|---|---|---|---|
| chests | add | turned to the vault's middle and facing it (8/8) | ours: angle 0 | fixed |
| exit Rift | add | north, `angle=1024, instant=true`, fade 0..90 | ours: no angle face | fixed |
| butterflies | wander | 0.7 steps a tick, 5..13 tiles out; a `reset` with the first step (4/5) | 0.34 a tick (`tfa_vault.npc`, wanderrange 10); no reset | **open (engine: the wander roll's rate, the first-step reset)** |
