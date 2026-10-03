# Bone Voyage - notes from driving it

- Hosidius sawmill operator is poh_sawmill_opp at 1623,3500 (not prif_sawmill_operator, which stands in Prifddinas). One npc type, two towns: the Talk-to branches on x < 2000.
- Varrock operator 3302,3492. Lost proposal (state 11) is replaced at Varrock; a lost agreement (state 15) at Hosidius or Berry's gate.
- Woodcutting Guild east gate (wcguild_gater) is the Berry fallback ONLY below Woodcutting 60; it signs the proposal or replaces a lost agreement (woodcutting_guild.rs2).
- Digsite barge: guard 3362,3446 (ground), deck is plane 1 around 3363,3452-3454. Embark (op3) boards from state 21; aboard Disembark (op3) lands 3361,3446.
- Post-quest: guard Quick-Travel (op4) lands on the island barge 3722,3785 plane 1; Junior/Lead Travel (op3) on the island lands in Museum Camp 3730,3821; on the Digsite deck it goes to the island barge.
- Dialogue options that gate progress: Lead "I'm ready, let's go." (states 30/35). Odd Old Man and Apothecary only act at state 25.
- Voyage (bv_voyage.rs2) is the wiki minigame: tilt arrow left -> click Steer right. Bearing within one notch makes headway; four notches off runs aground (state 35, back on the dock deck, retry free). Abort button does the same. Sail +/- sets speed 1-3 per cycle and swing 2 (small) or 1.
- The voyage map is region 28,74 (1820,4762 plane 1); the player stands at 1819,4762. A relog there is rescued by talking to its Junior Navigator (sets 35, returns to dock).
- Server keeps attempt state in varp7220-7222 because the client cs2 overwrites varbit 5799 (arrowspeed) with 45.
- Differs from the guide: the arrow target angle is sent with runclientscript 1993 each cycle (bv_voyage_show); guide has no equivalent step.
- Ahab's pub interruption, the nine-barge history, and Haig's Dig Site/Kudos transcript branches stay the pack's shorter dialogue.
