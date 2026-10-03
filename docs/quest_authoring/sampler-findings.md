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
tinderbox in that same leg. (The content cannot relight it yet: content-gaps, The Lost Tribe. So stay
on the marked path.)

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

## Sample matthew-mbp-m4-b47 (2026-10-01)

*Origin: the sampler checked theslugmenace, regicide and losttribe, and sent regicide back.*

(a) AN OBSTACLE THE LADDER DOES NOT LIST IS STILL AN OBSTACLE. Regicide's walks through the
Underground Pass ran `goto_tile` from the pit landing (2466,9699) straight to the grid lever at
2466,9673. Shot 085 reads "Teleported to 2466,9673,0". The jump skips `climbOverRockslide4`,
`climbOverRockslide5` and `crossTheGrid`, which the guide shows between the pit and the lever. None
of the three is in `helper_coverage`'s ladder, so it read FULL (coverage-and-gate, "The ladder lists
a route back to front"). The reviewer noted "unlisted pass obstacles between bridge and the tunnel
crossed by one goto_tile hop" as a doc gap. It is a teleport past the route. Walk it, or `walk_to`
in short hops and cross each obstacle with `click_loc` (verbs-pointer, "A walk stops at an
obstacle").

(b) A SUCCESS LINE FROM THE LAST TRY IS NOT THIS TRY'S. The spear-trap loop passed when the last
four chat lines held "...and succeed". After trap 4 succeeded and trap 5 failed, that line was
still in view. `passTrap5-tile` passed on it while shot 117 shows "...and fail, activating the
trap!", and the next `goto_tile` hopped past the trap. `passTrap2-again` and `passTrap4-again` did
the same. Grade a retry on the tile it moved you to, or count the success lines before and after
the press, as `climb()` in the same file already does.
FIXED (b49-seam2): `helper_coverage` reads (a)'s hop and (b)'s stale trap rows as CHEAT
(coverage-and-gate, "lands at ... without pressing the rockslide").

(c) The Lost Tribe still steps onto the maze's floor trap on purpose and then `goto_tile`s from the
Lumbridge Swamp Caves back to the cellar. It no longer `::give`s a lit lantern back, but the content
has no relight (content-gaps, The Lost Tribe), so the tunnels are walked with the lantern out. The
detour is in no guide step and skips none, so the quest stayed. Leave it out of new tests.

## Sample matthew-mbp-m4-b48 (2026-10-01)

*Origin: the sampler checked regicide and sent it back again.*

(a) A FIX MADE IN ONE LEG IS NOT MADE IN ITS COPY. Round 2 was opened to replace the `goto_tile`
over the Underground Pass grid (Sample matthew-mbp-m4-b47 (a)). Leg 3 got the real crossing, but
leg 6's second walk still ran `goto-pullLeverAfterGrid-again` from the pit landing to the lever.
Shot 382 reads "Teleported to 2466,9673,0". A relay leg that walks a route again is a copy of the
first walk. After fixing a hop, `grep` the file for the same coordinates and fix every copy.
FIXED (b49-seam2): `helper_coverage` reads the copy's hop as CHEAT on its own (coverage-and-gate,
"lands at ... without pressing the rockslide").

(b) A ROW THAT SUMS UP A ROUTE IS NOT EVIDENCE THAT IT WAS WALKED. `navigateMaze` and
`goThroughUndergroundPassAgain` were `t.check(name, true, "<list of what ran>")`. Between those
rows, a `goto_tile` jumped from the ledge's far side (2374,9638) to the pipe (2420,9605), across
the maze's narrow rock bridges (gaps-world, Underground Pass). Grade a summary row on a tile or a
server line that only the real route produces, or leave it out.

(c) A DOOR THE CONTENT REFUSES IS A CONTENT_BUG, NOT A REASON TO TELEPORT. After `openIbansDoor`
the player lands at 2173,4725 on level 1. The guide's `enterTemple` (a ConditionalStep branch,
missing from `helper_coverage`'s ladder) is the way to the well room. After Underground Pass,
`upass_tomb.rs2:141-152` answers "The temple is in ruins..." and notes that the Regicide shortcut
is deferred. The test then used `goto_tile` to reach 2010,4709. End the leg in
`t.blocked("content_bug: ...")` naming that file and line.
FIXED (b48-seam1, OSRS-Content 2dd52a46a5): the doors take a Regicide player through; click them
(gaps-world, "Underground Pass: Iban's temple door").

## Sample matthew-mbp-m4-b48, second check (2026-10-01)

*Origin: the sampler checked regicide's round-6 green (d3505f4ee) and sent it back a third time.*

(a) ONE ANSWER FROM A MENU OF FIVE IS NOT THE STEP. The guide's `goLearnAboutBomb`
(Regicide.java:817-844) asks Iorwerth about quicklime, naphtha, sulphur, a barrel and a fuse, and
`knowHowToMakeBomb` needs all five `*_chat` varbits. `helper_coverage`'s ladder listed only
`askAboutFuse`, so the run read FULL after the one question. Shot 322 shows the menu offering the
other four. When the guide declares sibling `NpcStep`s on one npc, drive each of them by name, and
check the content sets every varbit (`lord_iorwerth.rs2:143-192`).

(b) A DETOUR THAT TELEPORTS INTO A SEALED AREA TO PRESS A BRANCH STEP IS A CHEAT. At stage 3 the
test used `goto_tile` to reach the pocket north of the tripwire and pressed `goFromTyrasToTrap` there.
It then jumped from 2220,3152 to 2240,3149, past the three dense forests west of the tracker. The
next row's press of that forest read "You can see no way to get past this." (shot 148), because
`regicide_route.rs2` refuses it below `spoken_tracker2`. If a press would only answer in an area the
quest has not opened yet, do not teleport into that area to make it. Drive the step when the route
reaches it, or leave it ALTERNATIVE.

(c) A STEP PRESSED IN A DETOUR IS NOT WALKED WHEN THE ROUTE NEEDS IT. Leg 5 went by `goto_tile`
from inside Tyras's camp (2190,3146) to the sulphur (2261,3132). Leg 6 went by `goto_tile` from
2238,3181 into the tripwire pocket and from that pocket to the ring of leaves. Those jumps skip the
camp passage, the middle forests, the tripwire and `climbThroughForest`, which the guide's
`goToIorwerthAfterCamp`, `goGiveRabbitToGuard` and `goTalkToIorwerthAfterRegicide` route through.
The same file's first Underground Pass walk went from the plank room to the well (past the pit,
the grid and the spear traps). It then drove those obstacles in a later leg, entering from the
voyage cave by `goto_tile` and leaving the same way. Every guide step had a row, so FULL could not
see this. Walk each route once, in guide order. Use `goto_tile` only between two tiles that a walk
connects with no guide loc in the way.
FIXED (b49-seam2): `helper_coverage` now reads (b)'s and (c)'s hops as CHEAT (coverage-and-gate,
"lands at ... without pressing the rockslide"; the round-6 file d3505f4ee reads TEST_GAP).

## Sample matthew-mbp-m4-b48, fourth check (2026-10-02)

*Origin: the sampler checked regicide's round-9 green (d06b64289, 473/0, 626 shots) and accepted
it.*

The run drove all 63 ladder steps by click, walk or dialogue, including the five Iorwerth questions,
both full walks of the Underground Pass, and a real bridge fall with `goBackUpToIbansCavern`. Every
`goto_tile` joins two tiles that a walk in the same file connects, or leaves Tirannwn the way a
player would teleport out (to the furnace, the chemist and Arianwyn). All 626 shots match their
names. The scroll reads 3 quest points, 13,750 Agility XP and 15,000 coins, and the reward rows
assert those literal numbers.

(a) A CROSSING ROW CAN PASS FROM THE SIDE IT STARTED ON. The Sticks in leg 3 were crossed east, not
west, and the 6-tile `-tile` check passed (start-and-travel: "A trap you can walk over gets crossed
the wrong way"). This did not send the quest back. The trap is not a gate, the press and the roll
were real, and leg 4 crossed it from its west `src` east toward the tracker, the way that walk goes.

(b) A MID-LEG FOOD REFILL IS NOT A STEP. `t.cheat("::give lobster 6")` fired twice, in legs 4 and 6,
when the traps had eaten the setup's food. It does none of the quest's work, so it stayed. A
reviewer reading `Gave 6 x Lobster` in a shot should check that the item is food the guide
recommends, not a quest item.

## Sample matthew-mbp-m4-b50 (2026-10-02)

*Origin: the sampler checked bearyoursoul (a5f4d7fa2) and eaglepeak (6d75971d2) and sent both
back. All 34 and all 222 shots matched their names.*

(a) A `goto_tile` INSIDE A DUNGEON CAN JUMP A GATE THAT THE GUIDE NAMES ONLY AS AN ITEM. Bear Your
Soul went down the Taverley ladder (2884,9796) and then `goto_tile`d to the Cerberus cave mouth at
2874,9846. That hop skips the dusty-key gate, `deepdungeondoor` at 2924,9803
(`areas/taverly/dungeon/scripts/jail_doors.rs2`). QH lists the gate only as the required item
"Dusty key, or another way to get into the deep Taverley Dungeon", and helper_coverage read FULL.
The fix is cheap: put `dusty_key` in the setup (it is a required item, not one a guide step has you
get), use it on the gate, and walk to `hellhound_cave_entrance_a_01`. Before each `goto_tile`, read
the step's `items:` line in `ladder.py`. A key or "another way into" there names an obstacle.
The proved route (key, pipe or spikes) is gaps-world "Taverley Dungeon deep area"
(matthew-mbp-m4-b50-seam1).

(b) A RECOVERY STEP TAKEN EARLY TURNS A MAIN-PATH STEP INTO A REFUSED PRESS. In the gold room,
Eagles' Peak fed `eaglepeak_bird_feeder1a` (QH `fillFeeder7`, commented "If you've blocked lever
1") while lever 1 was still down. QH shows `pushLever1Up` in that state. Feeding feeder1a moved
mechbird 1, so the main path's `fillFeeder5` (feeder1, the same bird) answered "This feeder already
has seed" and the file passed it as `fillFeeder5.kept`. A row that asserts a refusal is not a
driven step. Follow `ladder.py`'s state order (`createDisguises.addStep` read bottom-up). The
content accepts feeder1 on that path (`gold_room.rs2` `eaglepeak_gold_feeder_ready` 1: gate3 up,
gates 4 and 1 down). Press a recovery step only in the state its condition names.


## Sample matthew-mbp-m4-b52 (2026-10-02)

*Origin: the sampler checked xmarksthespot (a19c9e7e5), childrenofthesun (0a5e7189d) and
insearchofknowledge (388dc78a1) and opened all 96, 173 and 50 shots. It sent
insearchofknowledge back, because eleven of its tattered pages were `::give`n (coverage-and-gate:
"A `-- GUIDE-GAP:` over a `::give` of a quest drop").*

(a) AN NPC LINE TITLED "Someone" WITH NO CHATHEAD comes from a var write that hides the speaker
before its next `~chatnpc`. X Marks the Spot sets `%varb12151_veos_lumbridge_vis = 3` and then
gives Veos three more lines (`xmarksthespot.rs2:114-117`; shots 022-025). Children of the Sun
writes `^cots_finish` and then gives Tobyn four more lines (`childrenofthesun.rs2:483-485`; shots
105, 107, 108 and 110). `t.chat.play` still PASSes, because it matches the text and not the
title, so only a shot shows the problem. The content fix is to write the var after the speaker's
last line. This did not send either quest back.

(b) "JUST INSIDE THE PIG PEN" SITS BEHIND A GATE. X Marks the Spot's `goto-digMartin` lands on
3078,3259, inside the Draynor pig pen, and passes `farming_fencegate_l`/`_r` at 3077-3078,3258.
The shots' hover text reads "Open Gate". The guide names the pen but not the gate, so this did not
send the quest back. An author can avoid the question by walking to 3078,3257 and pressing the
gate.

## Sample matthew-mbp-m4-b52, round 2 (2026-10-02)

*Origin: the sampler checked upass's full-run green (91d1da7a9, 255/0, 367 shots) and sent it back
(revert 42ecba163). The green file was kept at `test/quests/wip/upass/sent_back_b52.lua` (removed once green; read it at 2a404b410).*

(a) A FALLBACK `goto_tile` INSIDE A RETRY LOOP FIRES IN A CASE ITS COMMENT DOES NOT NAME. The
`navigateMaze` loop (upass.lua:689-694) had a bare `t.player.goto_tile` to the near side of the
last rock bridge, "for a failed roll there". Its condition was "not standing on the near side,
attempt > 1, last bridge". In the full run the player fell off bridge 2387,9631 and walked
into the pit as far as 2392,9625 (bridge 2 then answered `refused` seven times, bridge 3 eight
times, and bridge 4 answered `covered`). The goto then jumped to 2405,9637. Shot 188 reads "I can't reach that!"
three times, then "Teleported to 2405,9637,0". The row passed because it graded only the final
tile, which is (b) of Sample matthew-mbp-m4-b48 again. Because the goto was not a `t.exec` row, it
had no ledger row and no departure, so `helper_coverage` could not judge it and read FULL. A
fall drops you one tile off the bridge (`upass_obstacles.rs2` `[oploc1,walkway_upass_narrow_mid_top]`,
`p_teleport` to z-1 or z+1). Drive a way out of the pit on foot and cross each bridge again in
order. Every `goto_tile` in a file goes through `t.exec("goto-...")` so that it has a row.

## Sample vm-b1, round 3 (2026-10-02)

*Origin: the sampler checked royaltrouble (020cb78fe) and darknessofhallowvale (247253c72). It sent
Royal Trouble back and passed Darkness of Hallowvale (all 421 shots matched their names).*

(a) FULL WHILE A `goto_tile` LANDS ON THE FIRST ISLAND OF A CHAIN OF TRAPS. Royal Trouble's
slippery rocks are four `royal_invisible_puddletrap` tiles (2548, 2545, 2542, 2539), and the plank
crosses one at a time. QH picks the step from the zone the player stands in: `inPath2` asks for
plankRock1 (2548), `jumpIsland1` for plankRock2, and so on. Leg 6 `goto_tile`d from the first fire
(2553,10295) to 2546,10287, which is on jumpIsland1, so rock 2548 was never planked. The grader
only flagged the later goto that walked back east over 2545. The round-3 "fix" deleted that goto
and the only plankRock1 press with it. helper_coverage still read FULL, because plankRock1 matched
an unrelated line that names the same trap symbol. A shared loc symbol is not a press of the step's
tile. When a step's sub-steps are zones (`jumpIsland1..3`), stand in the zone that asks for the
first step (path2p2, 2549,10288) and plank each rock in order. Do not `goto_tile` into a later
zone, and check that every rock tile appears in some `from x .. to ..` detail.

## Sample matthew-mbp-m4-b53 (2026-10-02)

*Origin: the sampler checked forsakentower (526039ca5) and queenofthieves (1420d9ba2). It passed The
Forsaken Tower (all 160 shots matched their names) and sent The Queen of Thieves back.*

(a) A DOORWAY NO GUIDE STEP NAMES IS STILL THE ONLY WAY INTO THE ROOM. (Content half FIXED in seam
pass matthew-mbp-m4-b53-seam1 (d): the doorway is scripted; click it both ways.) The Queen of Thieves sits in
a tent at the end of the Warrens. The tent walls (`qip_digsite_tent_wall`, x 1761-1769, z
10149-10160) close it on every side except `piscquest_tentdoor` ("Doorway", op1 Go-through) at
1765,10149. Quest Helper's `talkToQueenOfThieves` and `talkToShauna` give only the Queen's
WorldPoint (1764,10158), and Devan says "The Queen will see you now in the tent at the end of the
tunnels". The test ran `goto_tile` from Devan (1766,10147) to 1764,10157 in 0 ticks, twice. It
also ran `goto_tile` out of the tent to the ladder. helper_coverage read FULL 18/18 because the
grader cannot read the map's walls (seam-facts: Seam pass matthew-mbp-m4-b51-seam1 (a)). The
doorway has no `[oploc1,piscquest_tentdoor]` script, so clicking it does nothing. That is a
content gap. Content must script the door (gated on Devan's go-ahead), and then the test must
click through it both ways. To find such a wall before you goto: the map square is
`maps/m<x>>6>_<z>>6>.jl2`. Each line is `level lx lz: id shape rot`, and
`configs/all.loc.compack` maps the id to a name. Shapes 0-3 and 9 are walls. A shape-0 loc with
an op (a door, a doorway, a gate) on the line between you and the step's tile is a step, so click
it.

(b) A PORT PUZZLE CAN HAVE TWO ANSWERS. The Forsaken Tower's refinery note (`ft_puzzles.rs2`
`[opheld1,lovaquest_fluid_note]`) prints three clues, and each set of clues fits two vial
positions. For example, "Acidic is directly left of Cleansing; Cleansing is next to Caustic; Inert
is directly right of Volatile" fits both vial 2 and vial 4. The test picks the vial by matching the
note's line template, which is the generator's own order, so it always picks right. That is not a
cheat, because the table and the refinery are still clicked. But a player has a 50% chance, so
name it if a reviewer asks for content parity with the real note.

## Sample matthew-mbp-m4-b53, round 2 (2026-10-02)

*Origin: the sampler checked whatliesbelow (38ed22f0e, 89/0, 169 shots) and queenofthieves
(e09df54ad, 78/0, 88 shots) and opened every shot. It passed both quests. None of the findings
below sent a quest back.*

(a) AN NPC'S LINES ARE TITLED WITH ANOTHER NPC'S NAME, AND THE CHATHEAD IS MISSING. (FIXED
b53-seam3, OSRS-Content fd1bf2f29f: the spawn runs after the last line; `.npc_add` is no fix in
this engine, see seam-facts: Seam pass matthew-mbp-m4-b53-seam3 (b).) In What
Lies Below, Rat Burgiss's `bringFolderToRat` lines (shots 052-062) are headed "Outlaw".
`whatliesbelow.rs2:83` calls `~wlb_spawn_outlaws` before `~chatnpc("Hello again! ...")` at :84.
That proc (`whatliesbelow_papers.rs2:11-20`) runs `npc_find`/`npc_add` on `surok_outlaw1..3`.
Both commands rebind the ACTIVE npc, so every later `~chatnpc` in the branch speaks as the last
outlaw found or added. `t.chat.play` matches the text and not the title, so the row PASSes, and
only a shot shows the problem. This is a different cause from the "Someone" header (Sample
matthew-mbp-m4-b52 (a)), where a var write hides the speaker. The content fix is to call the spawn
after the branch's last `~chatnpc`, or to look the npcs up on the secondary pointer (`.npc_find`,
as `death_thin_npcs.rs2:57` does). The accept branch at :38 already spawns after its last line.

(b) A GAP WHERE AN EM DASH SHOULD BE. (FIXED b53-seam3 for this quest, OSRS-Content fd1bf2f29f;
the rest of the tree: gaps-dialogue: A gap where an em dash should be.) The Queen of Thieves writes a real `—` in four `~chatnpc`
lines (`queenofthieves.rs2:60, 81, 145, 218`). The dialogue font has no glyph for it, so Devan's
line draws as "see you now    tent at the end of the tunnels" (shot 048). `chat.play` fragments
that stop short of the dash still match. Content should write `-` or rephrase the line.

(c) A LINE THAT NAMES A DOOR THE MAP DOES NOT HAVE. (FIXED b53-seam3, OSRS-Content fd1bf2f29f:
the line follows Transcript:The_Queen_of_Thieves oldid 14962997, which names no door or lock.) The Queen says "The door will need picking"
(`queenofthieves.rs2:225`), but Councillor Hughes' house has no wall loc with an op. Its doors are
`kr_bankdoor_l/r_inactive`, which have no ops in `configs/all.loc`. The wiki Quick guide (oldid
15013569) says "enter the house ... and climb the stairs" and names no lock. So the
`goto-goToKingstown` hop into the house is plain travel. To check a hop like this, list every
shape 0-3/9 loc that has an `op` between the departure and the landing (`maps/m<x>_<z>.jl2`
joined to `configs/all.loc`). A door with no op is not a step.

## Sample matthew-mbp-m4-b53, round 3 (2026-10-02)

*The sampler checked contact (964621432, 240/0, 108 shots). It SENT the quest BACK. Commit
964621432 is reverted (0af96bd10), and so is the evidence commit (OSRS-Content d24d8d14d2). The
round-3 rows are kept in `test/quests/wip/contact/round3_rejected.lua`.*

(a) THE BOSS FIGHT IS NOT A FIGHT. `contact_scarab_boss` has no `.npc` combat block, so it fights
with the engine's default stats: 10 hitpoints and attack/strength/defence 1. It died 7 ticks after
the first swing and dealt no damage (shots 080-081). The wiki gives it 130 hitpoints. See
gaps-combat: A level-191 boss dies in seven ticks.

(b) The completion scroll lists "1 Quest Point" twice (shot 098). See gaps-dialogue: A brief names
skill XP that the content pays as a lamp.

Not findings: the maze is walked on foot with the trap presses, both times. Maisa is talked to
across the chasm through the `[apnpc1]` trigger. The instance is left by its ladder. The goto from
the chasm to Al Kharid matches the glory teleport that Quest Helper recommends ("Amulet of glory
for getting to Osman"). The seam relay sanctioned it. Nit: `mazeUp` is defined and never used.
Maisa's two questions are split across two talks, which is a port parity leg the relay already
names.

