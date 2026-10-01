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

(d) FIXED (seam35): the gate's `pre_login` fingerprint could match a real in-game frame (Recruitment
Drive's Spishyus bridge room at the default camera pose). It now needs both canvas probes, and an
in-game frame's bottom-left is the chat stone (coverage-and-gate, "Fingerprints"); no camera turn is
needed.

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

## Sample sonnet-b36 (2026-09-30)

*Origin: section 8 ("Gaps reported by authors"); the sampler checked dragon, mcannon and troll.*

(a) READ A BOSS'S FIRST BAR AGAINST ITS HITPOINTS. Dragon Slayer (`dragon`, sent back) killed Elvarg
in 4 ticks: the attack's own reading was `0/60` after a 10 hitsplat. The spawn was the 10 HP default
form (`gaps-combat.md`, Elvarg). A PASS on `killElvarg.dead` is not a fight when the bar is empty
after the first hit.

(b) `t.check(name, true, ...)` IS A HOLLOW ROW. Dwarf Cannon writes one after every toolkit
`t.ui.invoke` and after the remains pickup. mcannon passed only because a later row proves the
outcome (`repair.message` plus stage 8, and Lawgof's hand-in at stage 3). Grade each press on the
varp or the chat line it moves (`%mcannon_spring_set`, "You hook the spring back into place.").

## Sample sonnet-b43 (2026-10-01)

*Origin: the sampler checked deserttreasure and horror, and sent both back.*

(a) `::god 1` IS NOT A SETUP CONVENTION. Horror from the Deep (`horror`) put `::god 1` in `setup`.
That made the 10-hitpoint account invulnerable, and it beat the Dagannoth and the mother without
food. The reviewer called this "a documented convention", but it is only a cheat listed in
`QUEST_SERVER_CHEATS.md`, and no other quest file uses it. The boss is fought for real: set combat
levels, carry food and eat it (`opts.eat`), and use the prayer the guide names.

(b) THE SAME 10 HP BOSSES, FOUR AT ONCE (b36 (a) again). Desert Treasure's Fareed, both forms of
Damis, Dessous and Kamil have no `.npc` block, so each spawns at `npc_default.npc`'s 10 hitpoints.
Damis read `0/30` after one 10 hitsplat, and Kamil left the pool inside the settle of the first fire
blast. The gate and `helper_coverage` were both green. Before you author a quest with a boss, grep
`--include='*.npc'` for every boss symbol.

(c) A DROP LEFT ON THE FLOOR IS NOT A REASON TO TELEPORT BACK. Desert Treasure never picked up
Fareed's diamond after the kill. Leg 6 then used `goto_tile` to get back into the Smoke Dungeon,
past `sword_haunted_well` (its trigger writes `fd_torch_count1-4`), to take the copy that "found its
way back". Pick up a kill's drop in the leg that made the kill.

## Sample sonnet-b44 (2026-10-01)

*Origin: the sampler checked losttribe, redreef and routequest, and sent losttribe back.*

(a) A PICK-LOCK IS NOT A DESCENT (b35 (a) again). The Lost Tribe (`losttribe`) clicked the H.A.M.
trapdoor's Pick-Lock (`[oploc5,osf_trapdoor_closed]`). That only sets `%ham_thief = 1`, which turns
the `ham_multi_trapdoor` multiloc into `osf_trapdoor_open` (op1 Climb-down). The test then used
`goto_tile` to reach 3152,9644 in the lair, and shot 162 reads "Teleported to 3152,9644,0". It left
the same way, without clicking `osf_ham_ladder`. The b44 reviewer called this "acceptable travel",
and `helper_coverage` credited `enterHamLair` to the pick-lock row because it names the multiloc.
A step whose guide object is an entrance is driven only when you go through that entrance: click
Climb-down and read the tile after it. (Until seam37 that Climb-down answered "You can't go any
further." and the lair had no way in but a teleport; it lands on 3149,9652,0 now: seam-facts, Seam
pass 37 (b).)

(b) A `::give` THAT UNDOES A DETOUR IS STILL A `::give`. The same test walked onto the maze's floor
trap on purpose. The trap put its candle lantern out, and the test then ran `::give
candle_lantern_lit 1` instead of relighting the lantern. Stay on the marked path, which is what the
guide's `walkToMistag` says. If you trigger the trap to show it works, relight the lantern with a
tinderbox in that same leg.

## Sample sonnet-b45 (2026-10-01)

*Origin: the sampler checked mm and hauntedmine, and sent mm back.*

(a) A GATE ONLY THE GUIDE'S BRANCHES NAME IS STILL A GATE. Monkey Madness I (`mm`) leg 8 ran
`goto_tile` from the Ape Atoll dock (checkpoint 7, 2802,2707) straight to Garkor at 2807,2760, and
shot 720 reads "Teleported to 2807,2760,0". That jump crosses the Bamboo Gate into Marim
(`mm_bamboo_largedoor_left`, 2721,2766). `open_mm_largedoor` in `mm_bamboo_doors.rs2` opens it only
for a worn greegree. The guide names the gate: `enterGate` is the `bringMonkey` branch for
`onApeAtollSouth`. But it is in no `getPanels()` list, so `helper_coverage` never grades it and read
FULL anyway. Before any `goto_tile`, check the guide's ConditionalStep zones for both ends of the
jump. When they differ, the step the guide shows for the starting zone is a row you drive.
Machine check since seam36: `helper_coverage` grades such a branch-only step and reads this
jump CHEAT (coverage-and-gate, "A branch-only step"; seam-facts, Seam pass 36 (b)).

(b) A DETAIL OF `table: 0x...` NAMES NOTHING. Haunted Mine's `endcart` and `valve-open` rows passed
`tostring(m)` where `m` came from `t.msg.last(4)`. That is a table, so the detail read `table:
0xca17fc810`. The gate's empty-detail check passes it. The rows were true (shots 045 and 072 show
the lines), so the quest stayed. Pass the matched line instead: `t.msg.expect` returns `(r, d)`, so
`local r, d = t.msg.expect(...)` and then `t.check(name, r, d)`.
