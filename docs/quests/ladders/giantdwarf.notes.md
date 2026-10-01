# giantdwarf -- notes from driving (parity3e, wiki source)

Stage varbit giantdwarf_quest. Driver: `::setvar giantdwarf_quest N` skips ahead (11 saro, 21 after Blasidar).
Levels: magic 33, thieving 14, firemaking 16, crafting 12. Server vars gdwarf_points/gdwarf_task/gdwarf_task_count
are server-only: read with t.var.server, not t.var.varp.

Boots (gdwarf_boots.rs2)
- Dromund's house has a door: dwarf_keldagrim_door_ornate at 2837,10219 (south wall). Click it, then walk in.
- Dromund stands 2835,10225. His gaze flips every 4 ticks (watching) / 8 ticks (away), npc var slot 40.
  While he watches, taking a boot makes him shout and nothing happens: retry. Ledger: 3 refusals then a take.
- Left boot = ground obj at 2838,10220 inside; take is op 3 (click_obj). Needs Thieving 14.
- Right boot = ground obj 2836,10226; Take says "out of your reach". Stand OUTSIDE at 2836,10229 (north of the
  window dwarf_keldagrim_house_window_open 2836,10227) and cast telegrab (t.player.cast("telegrab", obj)). Law+air rune.
  The telegrab hook is skill_magic/scripts/spells/telegrab.rs2 (gdwarf_right_boot_ok, gdwarf_boots_check).
- Holding both boots they combine at once into the pair; stage 14. Right boot first is allowed.

Consortium (gdwarf_consortium.rs2)
- Stairs dwarf_keldagrim_wide_stairs_lower 2895,10210 need stage >= 21 (talk to Blasidar after Riki first).
- Wired companies: Blue Opal (secretary 2869,10205, director 2867,10203), Purple Pewter, Yellow Fortune.
  The first task you accept fixes original_company; another company's staff refuse you.
- Secretary: ore task (clay copper tin iron silver gold mithril coal, 3-5, UNNOTED) 20 points; refuse -2.
  75 points -> "no more work" (stage 23). Director: bar task (bronze iron steel silver gold mithril, 2-4) 12 points
  until 100 (stage 24). Then "I'd like to officially join your company" (25), talk again (26), go down stairs (28).
- Each talk is one step: talk to get the task, bring the items, talk again to hand in. No 10-minute timer.

Different from the guide
- Clothes, boots, axe run in that order (stage ladder), the real game allows any order (gdwarf_start/constant header).
- Riki takes all three items in one talk (gdwarf_consortium.rs2 model handler).
- The boat ride is a teleport to Veldaban on accepting (gdwarf_start.rs2:38).
- Cutscenes (intro boat crash, statue fade, consortium meeting) are not built: spec pending in docs/quests/cutscenes/.
