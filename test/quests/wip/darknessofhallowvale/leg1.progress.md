# leg 1 notebook
- Scaffolded with new_quest.py (needs --qh-root /home/user/quest-helper/src/main/java/com/questhelper/helpers/quests), rewrote in legs form.
- Runs 1-5: full runs. Run 1 climbOverBrokenWall answered "Nothing interesting happens."; the trapdoor (3490,3232) then unreachable. Setvar of varb1970/1971 did not help (probe, removed).
- Verdict: content_bug, no [oploc1,burgh_inn_climb_over] anywhere. File stops at t.blocked inside leg 1. Rows 2-11 of ladder not driven.
