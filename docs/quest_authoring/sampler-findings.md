# Sampler findings (section 8)

## What `helper_coverage` and the gate still miss (sampler sonnet-b31)

> CONFLICT (kept both): (c) below lists `t.await`'s `ok` and `t.ui.await_open` as verbs whose detail
> is nil. That was sampler sonnet-b31, before seam27; since seam27 both answer a detail on `ok`
> (section 3, `verbs-root-and-quest.md` and `verbs-ui-and-npc.md`; seam pass 26 (f)). The later date
> wins; `t.ui.await_close` still answers a bare `ok`.

*Origin: section 8 ("Gaps reported by authors").*

WHAT `helper_coverage` AND THE GATE STILL MISS (sampler sonnet-b31).

(a) and (b) were fixed in seam26 (`helper_coverage_credits_by_mention`); the rule is now section 7's
"How a guide step is DRIVEN".

(a) FIXED: a line that names a step's target no longer drives it by mention alone -- The Feud's
`talkToAVillager`/`talkToAVillagerToSpawnMayor` now grade UNMATCHED, because the only villager
presses are `pickpocketVillager1` (op3 Pickpocket, and `pickpocketVillager`'s own row).

(b) FIXED: a custom step class's sub-steps are graded -- Recruitment Drive's
`MissCheeversStep.getPanelSteps()` gather chain is fourteen steps of its own, and a port that hands
the items over in dialogue grades them UNMATCHED.

(c) EVERY PASS ROW NEEDS A DETAIL, and since seam27 `gate.py` fails one without:
`t.check(name, r, d)` over a verb whose detail is nil (`t.await`'s `ok`, `t.ui.await_open`) writes
an empty one, and a sampler sends the quest back for it (The Feud rows `carpet-landed`, `safe-open`)
-- pass your own string naming what landed and where.

(d) The gate's `pre_login` fingerprint can match a real in-game frame (Recruitment Drive's Spishyus
bridge room at the default camera pose); camera yaw decides it and the pose persists across rows
until reset, so turn or reset the camera before that room's shots instead of re-running.

## Sample sonnet-b32 (2026-09-28)

*Origin: section 8 ("Gaps reported by authors").*

SAMPLE sonnet-b32 (2026-09-28).

(a) FIXED (seam27): `t.npc.await_dead_engaged` re-casts under auto-retaliate and eats through
`opts.eat` (section 3) -- no inline cast+eat loop.

(b) SETUP ARMOUR: `rune_platebody` is Dragon Slayer-gated to wear; set up armour this account can
wear.

Other sampler findings live beside the rule they changed: trap 4 (sonnet-b33, repeated pages), trap
12 (sonnet-b27, sonnet-b13), trap 17 (sonnet-b29), trap 21 (sonnet-b12, sonnet-b17), trap 32
(sonnet-b27, sonnet-b28) and the `coordz` side-test gap (sonnet-b16). `INDEX.md` lists them all.

## Sample sonnet-b34 (2026-09-29)

*Origin: section 8 ("Gaps reported by authors"); sampler checked elena, grandtree, itexam.*

(a) A SECOND BRANCH AFTER COMPLETION. Hazeel Cult has two sides. `hazeelcult.lua` completes the
Ceril branch to `expect_complete`, then issues `t.cheat("::hazeelcultreset")` and plays the Hazeel
branch to a second `expect_complete`. The b34 reviewer accepted this. The debugproc
(`hazeelcult_selftest.rs2`) only rewinds the quest and does none of its work, and it runs after the
first branch has already been graded complete. That is the one use of a reset: a debugproc that
ADVANCES a stage, or a reset before the first completion, is still trap 16.

(b) A `::spawn` DROP HUNT IS UNSOLVED (Imp Catcher, `imp`, gave up). `drop_tables/scripts/imp.rs2`
drops one `death_drop` plus one `random(128)` roll per death, with each bead at 5/128 on
`npc_coord` for `^lootdrop_duration`. 250 spawned-imp attempts yielded black 1, red 1, white 1 and
yellow 0 when roughly 10 of each were expected, and the cause was not found. The reviewer's shot
showed several drops stacked on the kill tile, so a `click_obj` by bead name may face the wrong copy.
Count only ZERO-BAR deaths as kills, and log `t.world.obj_near` for every colour right after each
death, before any pickup. Content imps do not teleport when damaged
(`areas/lumbridge/scripts/imp.rs2` has no panic teleport). They only teleport on their idle timer,
and never while in combat.

## Sample sonnet-b35 (2026-09-29)

*Origin: section 8 ("Gaps reported by authors"); sampler checked chompybird, druidspirit, imp.*

(a) A TRAPDOOR THAT ONLY OPENED, OR A REFUSED ONE, IS NOT A DESCENT. Nature Spirit (`druidspirit`,
sent back) clicked the Paterdomus `trapdoor`, read "The trapdoor opens...", and then used `goto_tile`
to reach Drezel underground. Later it clicked `pipeastsidetrapdoor`, read "Lab stairs and trapdoors
sit locked.", and used `goto_tile` past that too. Both rows passed on the chat line alone. Read the
line a travel click produced. An op1 that only opens a trapdoor needs a second click (Climb-down).
A refusal is a seam to report (`gaps-world.md`, Paterdomus). Neither one licenses a `goto_tile`.

(b) A GUIDE SUB-STEP IS PART OF THE LADDER. NatureSpirit.java adds `leaveDrezel` (the holy barrier,
`pip_underground_wall_side_withportal`) with `enterSwamp.addSubSteps(leaveDrezel)`. `helper_coverage`
graded `enterSwamp` FULL on the gate click alone, but the test teleported from Drezel to the swamp
gate and never crossed the barrier the guide names. Read the guide's `addSubSteps` lines as well as
its panel.

(c) REWARD SHOTS SHOW THE QUEST LIST TAB. `t.quest.expect_complete()` ends on `quest.journal`, which
leaves the Quest List tab open. Every reward row after it (`reward.magic`, `reward.ogre_bow`, ...)
therefore photographs that tab, not the skill or item it names. This happened in imp shots 36-38 and
chompybird shots 237-239. The ledger detail still carries the literal amount. Press
`t.ui.tab("inventory")` before an item reward row, so the shot shows what the row name claims.
