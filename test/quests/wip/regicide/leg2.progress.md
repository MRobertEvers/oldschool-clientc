run 1: leg 2 from checkpoint 33 rows green; tightened where() tiles, renamed last row leg.2.end; run 2 green. Done.

round 4 (b48): ~9 runs. Replaced maze goto with walked rows through ledge; cell -> mud -> ledge pocket are each walk-sealed; ledge landing pocket (2374,9638..2376,9616) is walled off on z 9615 (m37_150.jl2 1459 walls) from doorl 2375,9611 and the pipe corridor (2376,9610 -> west pipe -> 2419,9605 east). walk_to from well landing / cell makes no progress. File stops at t.blocked("seam: ...") after crossLedge. enterWell temple marker and later rows not reached.
run 3 (b48 r5): maze+pipe+leaveUnicornArea green via proven copy; walkToIbansDoor walk_to 2369,9718 from 2371,9666 makes no progress (first fail)
run 5 (b48 r5): DONE. walkToIbansDoor by 18 local-tile hops (m37_151 collision); run --from-leg 2 276 PASS, only leg 6's marker remains. leg.2.end PASS.
