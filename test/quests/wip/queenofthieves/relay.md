## talkToQueenOfThieves / talkToShauna -- the tent doorway (seam matthew-mbp-m4-b53-seam1 queenofthieves_tent_doorway)
- Content fix (uncommitted at the time of writing): `[oploc1,piscquest_tentdoor]` in
  `OSRS-Content/osrs239-content/server/scripts/quests/quest_queenofthieves/scripts/queenofthieves_locs.rs2`.
  Before Devan's go-ahead (stage < `^qot_queen` = 8) a click from outside answers
  "You should speak to Devan Rutter before going in there." and leaves you outside. From stage 8 on,
  the click walks you through by the shared `@door_walkthrough_try`
  (`general_use/scripts/door_walkthrough_fallback.rs2`). Walking out is never refused.
- Geometry (`maps/m27_158.jl2`): the doorway `0 37 37: 31709 0 1` is a wall on the north edge of 1765,10149.
  Outside is 1765,10149 and inside is 1765,10150. The `qip_digsite_tent_wall` ring (local x 33-41,
  z 37-48) closes every other side. Devan stands just south, at 1766,10147-8.
- Sources for the gate:
  - Wiki Transcript:The_Queen_of_Thieves, oldid 14962997, "Returning to Devan": "The queen is impressed. You
    should head on in and speak to her." Then "Talking to Devan again": "So can I go in now?" / "Yes! Now get
    out of my sight!"
  - Transcript:Devan_Rutter, oldid 14785766: the same exchange after the quest.
  - The_Queen_of_Thieves, oldid 15352295: "Return to Devan Rutter. Then, enter the tent north of him and speak
    to the Queen of Thieves."
  - Quest Helper TheQueenOfThieves.java:104-107 and :186: talkToQueenOfThieves at stages 8-9, after
    tellDevanAboutConrad.
  - No source records what the doorway says before stage 8. The refusal line is this port's own wording.
- Rows to write (all measured in build/quest_gate/queenofthieves_seamcopy, pass=78 fail=0, gate green):
  1. After `tellDevanAboutConrad` (you are at 1766,10147, outside), run
     `t.exec("enterTent", t.player.click_loc, "piscquest_tentdoor", 1)`, then `t.ticks(2)`. Then check
     `t.world.tile()` and look for "player at 1765,10150".
  2. `talkToQueenOfThieves` from inside. No goto_tile.
  3. Run `t.exec("exitTent", t.player.click_loc, "piscquest_tentdoor", 1)`, then `t.ticks(2)`. Look for
     "player at 1765,10149". Only after that, `goto-exitWarrens2` to the ladder.
  4. For talkToShauna, run `goto-talkToShauna` to 1765,10147 (outside the tent, plain travel from the
     ladder). Then click `piscquest_tentdoor` in, check for "player at 1765,10150", and talk.
- Scratch proof (`build/seam_state/matthew-mbp-m4-b53-seam1/qot_tentdoor_scratch.lua`):
  - Pre-fix pack (`qot_tentdoor_pre`): `enterTent.inside` FAIL "player at 1765,10149,0 (want 1765,10150)".
    `talkToQueenOfThieves` FAIL with "I can't reach that!".
  - Post-fix (`qot_tentdoor_post`): pass=14 fail=0. That includes the early refusal (still at 1765,10149, with
    the line in the chatbox, shot 003).
