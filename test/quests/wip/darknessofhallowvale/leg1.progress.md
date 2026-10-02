# leg 1 notebook
- Scaffolded with new_quest.py (needs --qh-root /home/user/quest-helper/src/main/java/com/questhelper/helpers/quests), rewrote in legs form.
- Runs 1-5: full runs. Run 1 climbOverBrokenWall answered "Nothing interesting happens."; the trapdoor (3490,3232) then unreachable. Setvar of varb1970/1971 did not help (probe, removed).
- Verdict: content_bug, no [oploc1,burgh_inn_climb_over] anywhere. File stops at t.blocked inside leg 1. Rows 2-11 of ladder not driven.
- Round 2 (seam fixes landed): rewrote leg 1 rows 1-4 (wall, trapdoor, Veliaf, ladder) with a t.blocked placeholder after; run 6 next
- Runs 6-8: rows 1-6 (wall, trapdoor, Veliaf, ladder, boat, chute) PASS; fixing pushBoat (stand 3524,3177) / boardBoat (sang_boat_water_multiloc)
- Runs 9-13ish: all rows through climbRubble PASS; citizen wanders to 3597,3214, reachable only from 3596,3214
- DONE: run 16 total, 50/50 PASS, checkpoint 1 written, leg1.json written.
