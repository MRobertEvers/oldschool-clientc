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

## enterWizardsBasement: the Wizards' Guild cellar ladder now goes down (seam matthew-mbp-m4-b54-seam2 swansong_wizards_guild_cellar_ladder)

- **The cause was content: the climb binding, not the map or the press.** `ladder_cellar` (17384, op1 "Climb-down" only) at
  2594,3085 (`maps/m40_48.jl2` `0 34 13: 17384 10`) was bound only through its category (`ladders.loc` `climb_down_ladder` ->
  `~climb_ladder(-1)` -> `~climb`). `~maplink_try` is keyed on the PLAYER's tile, and the guild's only row,
  `[maplink_0_40_48_34_13_down]`, is keyed on the ladder's own tile (2594,3085). A shape-10 ladder blocks that tile, so the player
  clicks from 2594,3086 and the row never matches. The miss fell through to the plane default (level 0 - 1), which printed
  "You can't go any further." (shot `b54s2_cellar_before2/003`).
- **Spec:** LostCity places `loc_1754` on the same tile (`LostCity_Content2/maps/m40_48.jm2:6369` `0 34 13: 1754 10`) and climbs
  every copy from the player's own tile, one dungeon mapsquare down:
  `[oploc1,loc_1754] p_arrivedelay; ~climb_ladder(movecoord(coord(), 0, 0, 6400), false);`
  (`scripts/ladders+stairs/scripts/ladders.rs2:83-85`). The basement layout (`m40_148`) matches LostCity row for row.
- **Fix** (OSRS-Content 662de599a5, `ladders_stairs/scripts/ladders.rs2`): a name binding `[oploc1,ladder_cellar]`. It runs
  `p_arrivedelay`, then the crouch anim. A verified maplink row still answers first. On a miss it calls
  `p_telejump(movecoord(coord, 0, 0, 6400))`. All 33 harvested `ladder_cellar` rows are exactly +6400 z already, so the
  ladders that worked before behave the same: `ikov` was 96/0 green with `enterDungeonForBoots` PASS.
- **Rows** (scratch `build/seam_state/matthew-mbp-m4-b54-seam2/scratch/swan_cellar_ladder.lua`, run `b54s2_cellar_after`, 10/0):
  `click_loc("ladder_cellar", 1, {at={2594,3085}})` -> `chat_message`; tile `2594,9486,0`;
  `t.npc.await_present("wizard_frumscone", 15, 5)` -> `slot 60 at 2588,9489`; `talk_to wizard_frumscone` +
  `chat.play {"player:I'm looking for a way to raise a defence", "npc:My magic zombies"}` -> `quest.stage.frumscone_done` 95.
- **Put an `await_present` between the ladder and the talk.** The round-2 file unchanged (`b54s2_round2_copy`) now lands in the
  basement (row 121 `{x=2594 level=0 z=9486}`, was 3086). Its row 122 `talkToFruscone` still read
  `no npc 3246 ... in the client's entity pool`, because the click returns on `map_flag` while the player is still walking
  from the door. The teleport therefore lands at the end of `t.ticks(3)`, and the npc pool arrives a tick after the tile does.
  Adding one row after `enterWizardsBasement-tile`,
  `t.exec("frumscone-present", t.npc.await_present, "wizard_frumscone", 15, 10)`, gives 152/12 (`b54s2_round2_await`):
  every row through `quest.stage.queen_fight` 170 passes (Frumscone, Malignius, apron, Crafter, pot, army, Herman).
- **Next stop: row 151 `killQueen-attack`.** `swan_seatroll_queen` at 2347,3704 answers `'I can't reach that!'`: there is no
  route to the copy that was pressed. Then `killQueen-dead` hits `await_dead_engaged ... already been waited out once`. It
  re-used the ambush fight, so press Attack again. The rewards rows cascade from that, and `skill.snapshot` is not a table
  in that file.
- **Not fixed, outside this seam:** `ladder_from_cellar` in the basement (`m40_148.jl2` `0 34 13: 17385 10 2`) climbs to
  2594,9486 **level 1** (plane default). LostCity `loc_1755` uses `movecoord(coord(), 0, 0, -6400)` (`ladders.rs2:87-94`).
  Leave the basement with `goto_tile`.
