## The whole quest, Marcellus to the scroll (seam matthew-mbp-m4-b53-seam2 ribbitingtale_yellow_frogs_stage8_and_shells)

- Content fix (uncommitted at the time of writing), OSRS-Content `server/scripts/quests/quest_ribbitingtale/`:
  - `configs/ribbitingtale.spawn` now places the cache's multinpc SHELLS (all.npc:429457-429520), on the
    same tiles as before: `frog_quest_marcellus` (13401) at 1683,2973, `frog_quest_gary` (13402) at 1695,2996,
    `frog_quest_sue` (13404) at 1695,2995, `frog_quest_dave` (13403) at 1697,2984, `frog_quest_jane` (13405)
    at 1696,2983. The varbits pick the form:
    - Gary and Sue (`varb9844_frog_quest`): a "Frog" with no Talk-to at stage 0, "Frog" at stage 2, then
      Gary/Sue from stage 4.
    - Dave and Jane: a "Frog" with no Talk-to until stage 4, then Dave/Jane.
    - Marcellus (`varb9845_frog_quest_patch_unlocked`): `_normal`, then `_farmer` (Pay/Trade) after the quest.
  - `scripts/ribbitingtale.rs2`: Talk-to is bound on the shell symbols (trap 19). `[label,ribbit_yellow_talk]`
    is rebuilt from the wiki Transcript:The_Ribbiting_Tale_of_a_Lily_Pad_Labour_Dispute (oldid 15005191):
    - stage 8 is the election talk and writes `^ribbit_chop` (10). This is Quest Helper talkToYellowFrogs,
      TheRibbitingTaleOfALilyPadLabourDispute.java:75/:126.
    - stage 10: "Remind me, where do you get your oranges?"
    - stages 12-14: "Can't stop to talk!"
    - stages 16-24: after the hop-off.
    - stage 30: "peace talks are going well".
    - stage 32: post-quest.
    - Dave and Jane alternate lines (`~chatnpc_specific`).
- **Targets are the SHELL symbols now.** Use `talk_to("frog_quest_marcellus")`, `talk_to("frog_quest_gary")` and
  `talk_to("frog_quest_dave")` / `"frog_quest_jane"`. A child symbol (`frog_quest_gary_unnamed`) no longer
  finds Gary once he is named: the live npc_id is whatever child the varbit selects.
- The committed `test/quests/ribbitingtale.lua` repro rows (`marcellus.spawn`, `gary.spawn`) assert absence.
  Delete them, along with the `t.blocked`.
- Setup adds the gear for Cuthbert, Lord of Dread (QH getCombatRequirements :178-181; a fresh 10 hp
  character dies to him at 30 hp, run seam2_ribbit_copy):
  - `::setlevel attack|strength|defence|hitpoints 40`
  - `::give mithril_scimitar 1` + `::wield mithril_scimitar`
  - `::give lobster 5`
  - on the kill wait: `opts.eat = { item = "lobster", below = 20 }`
- Rows in order. All of them were measured in `build/quest_gate/seam2_ribbit_copy3` (pass=97 fail=0,
  `gate.py` green). The script is `build/seam_state/matthew-mbp-m4-b53-seam2/ribbit_seam2/copy.lua`.
  1. `goto-talkToMarcellus` 1683,2973. Then `talkToMarcellus` + chat (9 pages, choose "I see... How
     about I head over there and take a look?"), giving stage 2.
  2. `goto-talkToBlueFrogs` 1694,2996. Then `talkToBlueFrogs` (frog_quest_gary, reads "Frog" npc 12938)
     + chat (17 pages), giving stage 4.
  3. `talkToMarcellus2` + chat (13 pages), giving stage 6.
  4. `talkToGary` (reads "Gary" 12939 / "Sue" 12947) + chat (10 pages), giving stage 8.
  5. `goto-talkToYellowFrogs` 1696,2982. Then `talkToYellowFrogs` (frog_quest_dave, "Dave" 12943 / "Jane"
     12951) + chat.play, giving stage 10. The chat.play entries:
     - `npc:Have you heard about the election?`
     - `player:I need to inspect the lily pads.`
     - `npc:No can do.`
     - `player:don't you think it would be wise for a third party`
     - `npc:I really could do with a bit of a rest`
     - `player:You said you were hungry?`
     - `npc:Oh I'd kill for a nice tasty orange!`
     - `player:An orange?`
     - `npc:most people think we just eat flies.`
     - `player:Where do you get your oranges?`
     - `npc:They grow on trees around here`
     - `npc:Just a shame we can't reach those ones...`
     - `player:Hmm...`
  6. `goto-pickUpAxe` 1684,2976, then `click_loc("log_withaxe", 1)` and `inv.await bronze_axe 1`.
  7. `goto-chopOrangeTree` 1695,2981, then `click_loc("frog_quest_tree_op", 1)`, giving stage 12.
  8. `goto-sabotageLilyPad` 1692,2984, then `click_loc("frog_quest_lily_pad_destroyable_op", 1)`, giving stage 14.
  9. `goto-talkToGary2` 1694,2996, then `talkToGary2` + chat. That is 13 pages, the hop-off narration and the
     peace terms, giving stage 18. Stage 16 passes inside the same conversation.
  10. `talkToMarcellus3` at 1683,2973 (12 pages), giving stage 20. `talkToGaryToBlame` (4 pages) gives stage 22.
  11. `goto-openChest` 1676,2975, then `click_loc("frog_quest_chest", 1)`. Set the dials to N,A,L,I,A and
      Confirm; you get the plushy and stage 24.
  12. `goto-plantPlushy` 1694,2977, then `click_loc("frog_quest_poo_plant_op", 1)`, giving stage 26.
      Cuthbert, Lord of Dread spawns.
  13. `attack("frog_quest_cuthbert_combat", 2)` + `await_dead_engaged`, giving stage 28.
  14. `talkToMarcellusEnd` (5 pages), giving stage 30. Then `talkToGaryEnd` (10 pages) gives stage 32, and
      `quest.expect_complete` writes 4 PASS rows. `reward.woodcutting` is +2000.
- The display-name rows (`names.*`) read `npc.nearest(<shell>, 20)` -> name + npc_id:
  - stage 0: Frog 12937 / 12945 / 12941 / 12949 and Marcellus 12935.
  - stage 2: Frog 12938 / 12946.
  - stage 6: Gary 12939, Sue 12947.
  - stage 8: Dave 12943, Jane 12951.
  - complete: Gary 12939, and Marcellus 12936 (the farmer form).
- Known port gaps the run walks past (no guide step needs them):
  - At stage 12, `[label,ribbit_blue_talk]`'s `= ^ribbit_sabotage` branch shadows the "It's all sorted"
    branch. At 14, Gary goes straight into the hop-off narration.
  - The lily pad refusal ("Hey, don't be going near those lily pads", Jane) at stages 8-10 is not authored.
  - Marcellus's farmer Pay/Trade has no trigger (a named leftover in ribbitingtale.rs2).
