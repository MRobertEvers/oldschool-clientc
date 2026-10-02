## seam matthew-mbp-m4-b49-seam1 (troll_love_arrg_and_sleds) -- the sled rows were the caves; Arrg is test-side
- Sleds: the rides play and `t.cutscene.await` sees them: 5/5 keyframes each, landing at 2790,3794 and 2794,3719. They
  failed in b47 because the sled was never worn. Both cave clicks answered "A snowy cave."; the player stayed at
  2772,10232, outside the mountain zone, and Ride answered "You cannot use that here!" (equipSled `-> 1 left`).
  Cause: OSRS-Content quests/quest_curseofarrav/scripts/curseofarrav.rs2:223-230 binds `[oploc1,trollromance_caveentrance]`
  and `[oploc1,trollromance_snow_cavewall_crevis]` by name. That binding shadows `[oploc1,_maplink_transition]`
  (ladders_stairs/scripts/maplink.rs2:77), and outside Curse of Arrav it only prints that message.
- Content fix LANDED by the seam closer (OSRS-Content curseofarrav.rs2, [seam:matthew-mbp-m4-b49-seam1]): in that binding the
  last line `mes("A snowy cave.");` is now `~maplink_transition;`. The cave then lands at 2803,10187 and the crevice at 2778,3869
  (maplink.dbrow rows 0_44_58_6_31..33 and 0_43_159_20_56).
- Proof, with a private pack (the shared scripts tree copied, only this line changed, TORIRSSERVER_SCRIPTS=<private>):
  build/quest_gate/tlseam_caves_after is 18/18. The same pack without the line (tlseam_caves_control) and the shared
  pack (tlseam_caves_before) are both 12/6.
- Arrg: the port matches the OSRS wiki (oldid=15215810: 140 hp, max hit 38 melee / 30 ranged, 4 ticks). LostCity's
  2004 Arrg would hit at most 8 with ranged. b47 died to Arrg with 75/75/75, no armour and 14 sharks, which is
  test-side. parked.lua now has 85/85/85, a dragon scimitar, rune helm/chainbody/legs/kiteshield worn at the start
  (the rune platebody needs Dragon Slayer), 16 sharks eaten below 50, and sled.stowed moved after the stronghold goto.
- With the fix, that parked.lua (copy build/seam_state/matthew-mbp-m4-b49-seam1/tl_scratch/troll_love_copy.lua) ran
  97/97 in build/quest_gate/tlseam_copy_after: Arrg died in 112 ticks after 6 sharks, quest.varp_complete 45, the
  scroll, qp +2, the journal, and all five reward rows. gate green; helper_coverage FULL.
- On the SHARED pack after the line landed, the same copy ran 97/97 (build/quest_gate/closer_tl_shared): cave 2803,10187,
  crevice 2778,3869, both rides 5/5 keyframes, Arrg dead in 108 ticks, the scroll, qp 2->4, the journal.
- Next: copy parked.lua to test/quests/troll_love.lua and run it. Make the cave `.tile` rows ASSERT the tile (today they
  only print it), and replace sledSouth.msgs (it prints a Lua table address).
