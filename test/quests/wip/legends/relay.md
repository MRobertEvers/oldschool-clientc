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
2. plantSeed kills any jungle wolf in reach before planting. The b58 fixer added that
   believing a wolf's hit cancelled the planting and wasted the seed. That is WRONG
   (probed 2026-10-04 after the owner questioned it): a script paused in p_delay
   survives a hit, auto-retaliate, a walk click and an attack click, in LostCity and in
   ours. 22 plantings with a wolf attacking: 14 saplings, 8 seeds lost, all 8 to the
   Herblore roll (`stat_random(herblore, 40, 243)`, legends_yommi.rs2:30: "You planted
   the seed incorrectly ... withers and dies"), 0 cancels. There is no engine fix to
   wait for. Report: test/quests/orchestrator/matthew-mbp-m4/reports/pdelay_survives_hit_2026-10-04.md.
   Keep the retry with the next seed (the roll fails about half the time at Herblore
   45; carry three seeds). The wolf kill may stay only as hp-margin housekeeping; the
   next author should drop it if the margin holds without it.
3. `talkToUngaduluForForce` carries a verified ANY-OF marker (the guide's kill branch
   uses the glowing dagger; ungadulu.rs2:538). Keep it.
4. Nine older mid-run `::give` rows are baselined
   (tools/quest_gate/mid_run_gives_baseline.tsv): move them to setup or the bank verbs.

Runs need a client built from committed source (the pick change of 6ee5baf0a is not
needed by this quest, but the shared binary in the b58 checkout was stale).
