# Thieving: solver plan (phase 4)

Unit `crabs_thieving`, 2026-10-18. Spec and audit: [`thieving_spec.md`](thieving_spec.md).
Plan for `script/plugins/quest_driver/cox2_thieving.lua` (a `QD.T2` room) and
`test/raids/cox_thieving.lua`, after F3 (`::coxtest 6 <n>`).

## The strategy a strong solo plays

Synq 0:18:13 (30 grubs solo; deposit ~8 at a time so it eats while you thieve; the
stack holds 28; a lockpick speeds it up), the wiki Strategies (deposit at 10-15; the
4-chest circles let you open chest after chest without running), V2 (poison chests
are static; avoid reopening them):

1. **Make one free slot** before entering (drop/bank a potion; grubs stack).
2. **Open chests on the way to the trough**, nearest unopened (not on cooldown, not
   known poison) first; prefer a 4-chest circle (one tile reaches four chests) once
   near the trough. Keep run off when energy is low -- opening is the bottleneck.
3. **Deposit early, deposit often**: at >= 8 grubs, or when passing the trough; the
   scavenger eats 1-5 per 4 ticks, so the last deposit should top the total to the
   requirement while it is still eating.
4. **Hazards**: after the first poison chest is found, the solution set (de0
   ChestData for the variant + turn) narrows to <= 10 candidate quadruples; never
   open a candidate unless nothing else is near. The bats are not worth a detour
   (Synq) unless food is short (RESOURCE layer decides).
5. **Done** when the scavenger turns 7603 (sleeping) / walks to its hay; collect
   the secondaries; walk to the exit.

## Perception (api_drive, client-side only)

| need | verb | status |
|---|---|---|
| chests: tile, state | `api_drive.locs` (29742 closed, 29745 grubs open, 29743 poison/bats open, 29744 after completion) | exists |
| grubs held | `api_drive.inv_count` | exists |
| my deposits / its total | count my own deposits (solo: my grubs are all the grubs) | derived |
| scavenger eating / asleep | `api_drive.npcs`: `seq_id` = eat anim, `npc_id` 7602 -> 7603, `overhead` "Yawwwwn!" | exists |
| poison found | `api_drive.spotanims` 184 on a chest tile; hitsplat on me; message "A cloud of poisonous gas..." (`api_drive.messages`) | exists |
| **hunger bar** | OSRS sends a progressive bar over the scavenger; ours sends none | **missing (engine)**: a script headbar with no hitsplat, then `health_ratio` on the npc row |
| the room's variant and turn | derive from the chest tiles seen (the set of 64/66/74 is distinct per variant; the turn from their orientation) | derived |

## Mode machine

`ENTER -> SURVEY (variant/turn from chests, trough tile) -> LOOP { PICK chest ->
OPEN (hold op until success; re-press after any interruption) -> if grubs >= 8 or
need - deposited <= held: DEPOSIT } -> WAIT_FULL (keep opening if the requirement
is not yet met by what is in the trough) -> DONE`.

Open-attempt model: one attempt per tick (Mod Ash), p = stat_random(thieving,
99/155 or 153/209 with a lockpick); expected 1.7 ticks a chest at 99 without a
lockpick, 1.2 with.

## Kit

Solo kit with one free slot; a lockpick (scavenger drop) when one is found earlier;
Thieving cape optional (boost to 100: max 4 grubs).

## Probes

1. Ticks per open (attempt cadence: one a tick, not one per two) over 30 opens.
2. Grubs per open distribution at 99 (2,2,2,3 per Mod Ash).
3. Eat cadence from a 28 trough (5,4,3,3,2,2,1...); hunger regen after 100 idle ticks.
4. Completion ticks: walk +1, yawn +5, sleep +10; secondaries in the pack.
5. The hazard quadruple against de0's solver for the turn (one live seed).

## PASS

Scavenger asleep and the player through the exit, alive, no poison chest opened
twice, within 90 s of entering (99 Thieving, no lockpick), 0 aborts; 48 seeds.

## Round 2 result (2026-10-18)

`script/plugins/quest_driver/cox2_thieving.lua` + `test/raids/cox_thieving.lua`,
the whole raid's kit (one free slot). Finds its room from wherever it is
called (the whole-raid contract), opens the nearest closed chest (never one
seen open as a hazard), feeds at 10 grubs or what is still needed, drops a bat
or the stamina when the pack is full with no grubs, done when the scavenger is
the sleeping form; the test proves the exit Passage blocked before and taken
after. 48 seeds (`th0..th47`): 48 PASS, ~100-130 ticks in the room.
