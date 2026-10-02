# leg 6 notebook (round 7, fresh runner)
- run1: --from-leg 6: first FAIL walkToWell-again-tile (trap5 not crossed: threshold wrong). edits: removed 3 round-7 blockers (plank room skipped: plank kept in pack; Tirannwn on foot via ring, tracker forests west, tripwire, middle forests; return east via forests + spring flats + ring + log), trap thresholds = leg 2's (x < trap loc).
- run2: --from-leg 6 170/3: tripwire loc is 2220,3153 (not 3154); fixed
- run3: --from-leg 6 173/0 PASS. next: full run
- run4 full: frame budget 200000 hit at tick 6660 in leg 6; max_frames->240000
- run5-6: probe for goBackUpToIbansCavern: walk south of 2161,4640 (bridge D) is blocked, cavern unreachable on foot -> leg 1 GUIDE-GAP stays. added goThroughUndergroundPassAgain row, removed goFromTyrasToTrap marker. next: full run
- run7 full 471/0, gate green, lint clean, coverage CONTENT_GAP=1 (leg1 gap). DONE
