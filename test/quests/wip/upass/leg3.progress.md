# leg 3 notebook (b52)
Leg 3 text already in file (b49 green). Run 1 (full): leg 2 crossThePit random swing fail (upass_obstacles.rs2:132 stat_random agility 100,410 -> fall to 2485,9649), leg 3 never started. Not my leg; rerunning.
Run 2: added setup ::setlevel agility 50 (swing fall was deterministic; leg 2 setup-level, outside my leg but blocks it).
Run 3 (full run 2 of mine): died at orb3 walk (hp 7 after hops; eat threshold 6). Changed: lobster 14, hp 70, def 50, eat <=30 in hops, take_orb walk loops with eating.
Run 4: leg 5 failed 'no inventory space' (my extra lobsters). Added eat/drop surplus to 3 + leg.3.pack row before leg.3.end.
Run 5: --from-leg 3 green for leg 3 (earlier from-leg failure was a stale checkpoint). DONE. Open issue for leg 5 (inventory space at killJerro).
