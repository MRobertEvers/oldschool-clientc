## leg 1
- BLOCKED (content_bug): leg 1 stops at guide step 1 climbOverBrokenWall. Loc burgh_inn_climb_over (3491,3230,0; maps/m54_50.jl2:1516) has no [oploc1,...] handler, so the pub is unreachable and the trapdoor step cannot run.
- Fix content (a handler that carries the player over the wall to the north side, e.g. 3491,3232), then re-run leg 1 and continue from enterBurghPubBasement (steps 2-11 unwritten).
- Setup in file: clearinv, complete quest_inaidofthemyreque, ::darknessofhallowvale, hammer, woodplank 2, nails 8, requirement levels. Bind is varb2573_myq3_main_quest, complete = 320.
- new_quest.py needs --qh-root /home/user/quest-helper/src/main/java/com/questhelper/helpers/quests.
