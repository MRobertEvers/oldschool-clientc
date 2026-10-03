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

## killQueen / kill79Trolls: the Sea Troll Queen and the sea trolls are real fights now (seam matthew-mbp-m4-b54-seam3 swansong_queen_and_trolls_combat_blocks)

- **Why round 4 was sent back.** Neither npc had a server `.npc` combat block, so both fought on the engine default
  (10 hp, attack/strength/defence 1): the Queen died to one Fire Blast. They now have their wiki blocks in
  `quest_swansong/configs/swansong.npc`, cross-checked with the cache's `stat1..6` in `configs/all.npc`:
  - `swan_seatroll_queen` (wiki Sea_Troll_Queen oldid 15215925, id 4315): level 170, **200 hp**, attack 100, strength 70,
    defence 100, **magic 150**, crush + **Water Wave (max 37)**, melee max 16, speed 4, aggressive, no drops. She holds her
    spot in the sea (`wanderrange=0`).
  - `swan_troll_ambush` (wiki Sea_troll oldid 15329222, "Level 79", id 4308): **100 hp**, 60/60/60, crush, speed 3, max 7,
    aggressive, drops Bones. This npc is both the entrance ambush and the fishing-spot troll.
  - Both are `forcemulti=yes`: the colony is a multicombat area (wiki Multicombat_area oldid 15307059). Without it the
    three aggressive trolls attack at once and every Attack on a second one answers "I'm already under attack."
- **No more despawn clocks.** The ambush, the fishing troll and the Queen used to be `npc_add`ed for 50/50/100 ticks with a
  one-shot flag, so a real fight soft-locked stage 40 or 170. They now stay until killed (wiki Swan_Song oldid 15359363).
  Re-entering `swan_hole` at stage 40 puts back the trolls still owed. Talking to Herman at stage 170 puts the Queen back.
- **Server reads** (the 30-unit bar cannot tell 200 hp from 10): `t.cheat("::swansong_queen_hp")` answers
  `Sea Troll Queen hitpoints 200/200 attack 100 strength 70 defence 100 magic 150`. `t.cheat("::swansong_troll_hp")`
  answers `Sea trolls 3: 100/100 100/100 100/100` (count, then each troll within 25 tiles). Read them with `t.msg.expect`.
- **Her swing** (`swansong_finale.rs2` `[ai_opplayer2,swan_seatroll_queen]`): Water Wave at range. In melee distance it is
  melee 1 swing in 3, otherwise Water Wave. **With any overhead protection prayer on and out of melee distance, every swing
  drains 21 Prayer and does no damage** (page: "drains over 20 Prayer points at once"). Measured in `b54s3_queen_rcb_a`:
  Protect from Magic on, prayer 99 -> 34 in 9 ticks, hitpoints 99 unchanged.
- **You cannot melee her.** No route reaches a tile beside her: `b54s3_queen_melee_a` answered `I can't reach that!`, and
  `m36_57` blocks the shore at z 3702/3703. Fight from the beach at `goto_tile(2347, 3701, 0)`, an open tile. 3702 is a
  blocked tile, and only `::goto` can stand you on it. The wiki's advice (melee + Protect from Magic) is therefore not
  available, and at range she hits hard (Water Wave hits of 30+ every 4 ticks).
- **Measured staging** (full restaged round-4 copy `build/seam_state/matthew-mbp-m4-b54-seam3/scratch/swansong_round4_copy.lua`,
  run `b54s3_round4_copy_a`: **211/0**, reaches the scroll):
  - Setup: round 4's quest items (no lobsters, no fire-blast runes), plus `magic_shortbow` + `rune_arrow 400`, `coif`,
    `dragonhide_chaps`, `dragon_vambraces` and `shark 10`. Levels: ranged 99, magic 94, defence 80, attack and strength 80,
    hitpoints 99. That is exactly 28 slots. Wear the bow/arrows/armour in the first rows (not `dragonhide_body`: "You need
    to complete the Dragon Slayer quest first").
  - Trolls (eat `shark` below 50): 72 / 60 / 51 ticks, 1 shark. Fishing troll: 83 ticks, 1 shark. Keep the bones.
  - Queen: `t.player.attack("swan_seatroll_queen", 2, 20)`, then `t.npc.await_dead_engaged(400, 40, {eat={item="shark", below=60}})`.
    Dead after 128 ticks with 8 re-engagements: **16 sharks eaten**, lowest hp 23/99, stage 190.
  - **The open problem for the author is food.** The setup holds only 10 sharks beside the quest's own items, and 8 were
    left at the Queen with 15 free slots. The proof copy topped the food up with a mid-run `::give shark 16` (row
    `killQueen-food-staged`), which a test may NOT do (trap 16). The queen-only scratch `b54s3_queen_kit_b` (same kit,
    22 sharks) needed all 22, lowest hp 13. With 10 sharks the character dies with a bow, a rune crossbow or a twisted bow:
    eating every few ticks keeps interrupting the attack.
  - Prayer potions cannot replace the food. At range with Protect from Magic she drains 21 every 4 ticks, a dose restores
    31, and `t.player.inv_op` on a potion takes 14-18 ticks to settle (`b54s3_queen_pray_d`: died at Queen 85/200).
  - Toggle a prayer this way: `t.ui.tab("prayer")`, `t.ticks(1)`, `t.ui.widget("prayerbook:prayer13")` (Protect from Magic;
    prayer14 Missiles, prayer15 Melee), `t.ui.invoke(w, 1)`, `t.ticks(2)`, then read `varb4116_prayer_protectfrommagic`.
  - For the trolls, the wiki recommends Protect from Melee: "The trolls only attack with melee".
- **Committed file** `test/quests/swansong.lua` with its `t.blocked` removed (`b54s3_committed_copy_a`, 30/0): the round-1 kit
  (rune scimitar, 80/80/70, 6 lobsters) kills all three trolls (68 / 59 / 74 ticks, all 6 lobsters eaten, lowest 40/99) and
  reaches `quest.stage.trolls_beaten` 50. The file ends there.
- **Parity gaps left open** (also in `docs/bosses/quest_combat_manifest.json` known_gaps):
  - eight entrance trolls in the real game (the port keeps 3);
  - the level 65/87/101 fishing trolls (the port spawns one level-79);
  - the melee/magic ratio in reach;
  - the exact drain;
  - the siege cutscene;
  - the earth weakness;
  - no tile beside the Queen (a map/pathing question, outside this seam).

## talkToFranklin (hammer): Franklin hands over his hammer now (seam matthew-mbp-m4-b54-seam3 swansong_franklin_hammer)

- **The hole no longer asks for a hammer.** `swan_hole` still wants logs, a tinderbox and 5 iron bars, but the
  hammer is "(obtainable during the quest)" (wiki Swan_Song oldid 15359363). Drop round 4's general-store leg
  (`goto-hammerShop` .. `hammerShop-hammer`) and the coins it needed.
- **Talk to Franklin once the firebox is lit, before you fix the wall** (`varb2099_swansong_franklin` = 3, fewer than
  five sections fixed). The dialogue follows wiki Transcript:Swan_Song oldid 15263341 (`swansong_franklin.rs2`). With
  the five sheets pressed:
  `npc:How are you getting on?`, `player:the press is working now`, `npc:At least you've got enough iron sheets`,
  `player:Also I think I need a hammer`, `npc:Hammer? Not a problem`, then `t.inv.await("hammer", 1, 10)`. With a
  hammer already held he skips the last two pages. With a full pack, the player asks and gets
  "You don't have enough inventory space."
- Proved: closer scratch `b54s3c_full_franklin` (the restaged round-4 copy with the store leg removed and this talk
  added after `flattenBar-5`). The player entered the colony with no hammer, `talkToFranklinHammer-hammer` read
  `hammer 1 -> 1`, and the walls went to `quest.stage.tasks_done` 80 and on to `quest.stage.queen_fight` 170.
  At the Queen that run died (23 sharks eaten, her bar 28/80): the food problem above is real and varies run to run.

## killQueen: round 5 was sent back for its food margin (sampler matthew-mbp-m4-b54)

- **What passed.** Every guide step ran with the seam3 staging (`round5_rejected.lua`, 208/0, 756 ticks). Franklin
  handed over the hammer, and the bow killed the trolls in 52, 60 and 52 ticks and the fishing troll in 60 without
  eating. `::swansong_queen_hp` read 200/200, and the Queen died after 164 ticks with 12 re-engagements.
- **Why it went back.** Setup gave 15 sharks and 15 reached the Queen. `killQueen-dead` ate all 15 (lowest reading
  30/99, `OUT OF shark` with her bar at 4/80 at t+147). Shots 228 and 236 show the hitpoints orb at **1**. The
  reviewer's two reruns also ended at 1 hp. The author's first run (11 sharks fitted) ran out with her at 1/80.
  That is under the "a quarter more than the worst run" rule (gaps-combat: Food is not reproducible).
- **Slot peak.** The pack peaks on the fishing leg: 4 bones, 2 soft clay, hammer, net and 5 raw monkfish beside
  15 sharks. With 16 the fifth cast failed. Before the chickens the pack held 4 bones, 2 soft clay, the hammer and
  15 sharks (`pack-before-chickens`).
- **Not tried yet (all honest setup).**
  - `black_dragonhide_body`: magic defence +45, and only green `dragonhide_body` needs Dragon Slayer
    (`levelrequire.rs2:178`). Worn magic defence goes from 35 (coif 4, chaps 23, vambraces 8) to 80.
  - Eat to full before the first Attack: the fight opened at 72/99.
  - Drop the hammer after the fifth wall.
  If she still outlasts the food twice in a row, end at the Queen with `t.blocked`.
