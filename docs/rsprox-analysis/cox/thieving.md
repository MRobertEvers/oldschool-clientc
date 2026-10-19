# Thieving: recorded behaviour

3389 is the only recording that holds the room: a solo raid in **Challenge Mode**, on a Grid Master world, at visible Thieving 99 (see [README.md](README.md)). It covers ticks 4103-4311:

- 28 chest searches and 2 trough deposits
- 7 meals
- the yawn, the sleep and the first "Zzz"

No hazard chest (bats, poison) was opened. It was read **untranslated**.

- Tool: `tools/cox/rsprox_thieving.py`, with sections `searches`, `trough`, `scavenger` and `reward`.
- The content cites these rows as `RT-*` in `cox_puzzles.rs2` and `cox.constant`.

    python3 tools/cox/rsprox_thieving.py <raw>/archive/3389.txt

**A** is the tick the raider stops beside the chest or trough. **R** is a search's result. **E** is the scavenger's last meal.

## The chests

| Row | Recorded |
|---|---|
| RH-C1 | **Timing:** `human_openchest` (536) on **A+2**, the result on **A+3** (28 of 28). There is no other animation or sound first. No search failed: every result came on A+3. |
| RH-C2 | **The result,** all on R: `chest_open` (52) as a `sound_area` round the chest, range 5; "Some cavern grubs have hatched; you take them."; the chest becomes `raids_thievingchest_eggs_hatched` (29745) at its rotation; the grubs go into the pack, **2** (21 times) or **3** (4 times). |
| RH-C3 | **A full stack** (28): `human_openchest` still plays, then a **message box**: "You don't have space to take any more grubs from the chest." The chest stays closed and no sound plays (2 of 2). |
| RH-C4 | A hatched chest closes again **11-15 ticks** later (26 of 26: 11 ×4, 12 ×8, 13 ×4, 14 ×3, 15 ×7). |

## The trough and the scavenger

| Row | Recorded |
|---|---|
| RH-T1 | **Deposit**, on A+1: `human_pickuptable` (832) with `dream_squish_illusive` (3637) round the raider (range 5), and the grubs leave the pack. **No message.** An empty trough shows `raids_thievingchest_foodtrough_full` (29874) the tick after (2 of 2). |
| RH-S1 | **The scavenger eats every 3 ticks** (6 of 6 gaps), `raids_thievingchests_eat` (7496), starting 2 ticks after the first deposit. Its bar went 100, 90, 70, 53, 37, 24, 10, 0 over 7 meals, with 56 grubs in the trough. |
| RH-S3 | **Full**, with E its last meal: <ul><li>E+1: the reward on the floor under it, not in the pack, lying 200 ticks: stinkhorn 5, cicely 3, endarkened juice 5 (the party formula's solo amounts); it then walks 3 tiles to its hay.</li><li>E+7: "Yawwwwwn!", `raids_thievingchests_tired` (7497), and `howl1` (1734, delay 10, range 5).</li><li>E+14: asleep (`raids_thievingchest_beast_sleeping`, 7603).</li><li>E+21: "Zzz" (`troll_zeds`, 277) with `troll_snore` (869, range 8).</li></ul> |

## Fixed (2026-10-19)

| Was | Now |
|---|---|
| a search on arrival: `human_lockedchest` and `lever`, then the result with `human_openchest` and a synth `chest_open` | the recorded A+2 / A+3 timing; `chest_open` round the chest (RH-C1, C2) |
| a full stack: "You already have the maximum amount of cavern grubs." as a game message, no animation | the recorded message box after the opening animation (RH-C3) |
| chests closed 8 ticks after hatching | 11-15 (RH-C4) |
| a deposit on arrival with "You deposit some cavern grubs in the trough.", the trough full at once | A+1, with its sound and no message; the trough full the tick after (RH-T1) |
| meals every 4 ticks | every 3 (RH-S1) |
| the yawn 5 ticks after the last meal ("Yawwwwn!", silent), asleep at 10, "Zzz" every 5 ticks, silent | 7, with `howl1` and "Yawwwwwn!"; 14; the first "Zzz" at 21 with `troll_snore`, then every 7 (RH-S3) |
| the reward into the pack, or at the feet when full | on the floor under the scavenger on E+1 (RH-S3); the room solver now takes what fits |

`test/raids/_thieving_tells.lua` checks every search, deposit and meal, the yawn, the sleep, the "Zzz" and the reward off the tick log. It plays the room solver on 8 of 8 layouts. A worktree mutation (meals every 4, the yawn on 5, a synth `chest_open`) fails it on all three counts. The room test (`cox_thieving.lua`) passes 8 of 8.

## Open

| Item | State |
|---|---|
| Failed picks | None in 28 at visible Thieving 99 (115 with Grid Master's invisible boost). Ours keeps the wiki's chart, about 60% at 99, with a retry. Grid Master may be why no pick failed. |
| Grubs per meal | 30 eaten in 7 meals from 56. NR's max(1, min(5, trough / 6)) does not reproduce the bar exactly; kept. |
| "Zzz" after the first | The view ended 2 ticks after it; ours repeats every 7. |
| Hazard chests | Bats and poison are unrecorded. Their sound is now `chest_open` round the chest, like a grub chest. |
| Thieving xp | Not read (the stat rows here do not give the gain). |
| The reward in a party | One drop scaled by party size (the formula's); recorded solo only. |
