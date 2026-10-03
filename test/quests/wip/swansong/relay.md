## kill79Trolls: all three ambush trolls now spawn (seam matthew-mbp-m4-b54-seam1 swansong_entrance_ambush_three_trolls)

- **The cause was the content's `movecoord` argument order, not the map or `npc_add`.**
  `movecoord(coord, dx, dLEVEL, dz)`: the middle argument is the level (LostCity
  `engine/src/engine/script/handlers/ServerOps.ts:103-107`,
  `CoordGrid.packCoord(position.level + y, position.x + x, position.z + z)`; ours,
  `src/torirsserver/torirs_server_scripts.c` `SS_OP_MOVECOORD`, does the same). `[proc,ssq_spawn_entrance_ambush]`
  wrote `(2, 1, 0)` and `(-1, 2, 0)`, so trolls 2 and 3 spawned on levels 1 and 2 above the entrance.
  Neither our `npc_add` (`npc_spawn`, world.c) nor LostCity's (`NpcOps.ts:57-68`) checks collision. The map
  tiles are open anyway: `m36_57` local 39,9 / 41,10 / 38,11 on level 0 carry no blocking flag, and the only
  loc there is ground decor (4343 shape 22) at 41,10.
- **Content fix** (OSRS-Content f2902a94dd, `quest_swansong/scripts/swansong_colony.rs2`
  `[proc,ssq_spawn_entrance_ambush]`): `movecoord(^ssq_entrance_ambush_coord, 2, 0, 1)` and `(-1, 0, 2)`.
- **Rows** were measured in `build/quest_gate/b54s1_swan_ambush_after3` (script
  `build/seam_state/matthew-mbp-m4-b54-seam1/scratch/swansong_ambush_copy.lua`). They replace the committed
  `kill79Trolls-attack-1` .. `t.blocked`:
  1. `t.npc.tiles("swan_troll_ambush", 25)` -> `3 copy(s) -- 2343,3657 / 2342,3659 / 2345,3658 L0`.
  2. Three times: `t.player.attack "swan_troll_ambush" 2 20`, then
     `t.npc.await_dead_engaged 120 20 {eat=lobster<45}`. Each kill took 8-12 ticks at 80/80/70 with a rune
     scimitar, and no food was eaten. `varb2107_swansong_trolls` reads 1, 2, 3.
  3. `quest.stage.trolls_beaten` (50). The message `With the last of the ambush trolls down, the way
     further into the colony is clear.` appears, and three Bones are on the ground (keep them; see below).
- **Mind the clock.** `npc_add(..., 50)` despawns each troll after 50 ticks, and `varb2111_swansong_ambush=1`
  stops a respawn, so a fight slower than about 50 ticks from entry softlocks stage 40. The three kills
  took about 34 ticks. Do not pause between kills.
- **Next leg (talkToHermanInBuilding, QH SwanSong.java:248, 2354,3683): what the author needs.**
  - The Colony gate is shut. Click `swan_door_l` op1 first (`t.player.click_loc("swan_door_l", 1)`): it is
    a category-228 double door (`doors/configs/doubledoors.loc:814-828`) and it opened (`map_flag`). The
    wiki says "enter the gates of the Colony" (Swan_Song revid 15359363, "Battle at the Colony").
  - `walk_to 2352,3683` then stops at 2351,3683, beside `swan_desk` (2353,3682). `talk_to swan_herman`
    answered `I can't reach that!` (shot 046). Herman's spawn (`m36_57.spawn:31`, 2354,3683) is behind the
    desk. Try `{at=}` or another approach tile; if none reaches him, it is a content seam (an
    `[apnpc1,swan_herman]` across the desk, like Hudon/Maisa).
- **Parity gap, not fixed here:** the real game has EIGHT level-79 sea trolls at the entrance (wiki
  Swan_Song revid 15359363: "attacked by eight (8) level 79 Sea trolls"). The port keeps 3
  (`^ssq_trolls_needed`; `varb2107` is 2 bits). The 7 bones Malignius wants come partly from later trolls.
