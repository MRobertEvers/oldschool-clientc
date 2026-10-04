# legends -- released from matthew-mbp-m4-b58 (2026-10-04)

`parked.lua` is the b58 round-3 file: 628 rows / 0 FAIL, coverage FULL 99. The b58
sampler passed its walls, its three real teleport casts (Varrock from the gem cavern,
Camelot from the Viyeldi cave and from the Source) and its jungle cuts, and opened all
827 shots. Start from it, not from the committed test/quests/legends.lua.

## What is left

1. Leg 6, the three hero fights in the Viyeldi caves (San Tojalon, Irvig Senay, Ranalph
   Devere; the local `kill()` helper about lua 1262-1265, called about 1498/1500/1502)
   write no margin row, and `killRanalph-dead` ended "OUT OF shark" (6 sharks staged for
   the leg; lowest hp 39/99). Stage more food for leg 6 and give each fight a margin
   row: lowest hp at least a quarter of max AND food left. The two deathwing fights
   (about lua 1406-1410) need margin rows too.
2. plantSeed kills any jungle wolf in reach before planting. That was a workaround for
   a seed that vanished when the player was attacked mid-planting. The owner questioned
   it (2026-10-04) and it is NOT real behaviour: the planting script is LostCity's line
   for line and nothing in it reacts to damage. See build/seam_state/next-seam-carry.md
   item 28 and build/orchestrator/probe_delay/REPORT.md. Once the engine fix lands,
   remove the wolf workaround. The retry with the next seed may stay: the Herblore roll
   is about 52% per seed.
3. `talkToUngaduluForForce` carries a verified ANY-OF marker (the guide's kill branch
   uses the glowing dagger; ungadulu.rs2:538). Keep it.
4. Nine older mid-run `::give` rows are baselined
   (tools/quest_gate/mid_run_gives_baseline.tsv): move them to setup or the bank verbs.

Runs need a client built from committed source (the pick change of 6ee5baf0a is not
needed by this quest, but the shared binary in the b58 checkout was stale).
