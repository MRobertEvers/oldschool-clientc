# toa_mistake_tracker

Repository: https://github.com/QuestingPet/ToaMistakeTracker
Commit: `c73766d005af28741be13dee635723f12a41815b` (committed 2024-02-13)
RuneLite plugin hub entry: `plugins/toa-mistake-tracker` (read 2026-10-02 from github.com/runelite/plugin-hub master, pinned to the commit above); shallow fetch of that commit 2026-10-02.

Files copied (mechanics only; UI, config, overlay painting and test files dropped):

`detector/boss/{Akkha,Baba,Kephri,Zebak,WardensP1P2,WardensP3}Detector.java`, `detector/puzzle/{Apmeken,Crondis,Het,Scabaras}PuzzleDetector.java`, `detector/tracker/*` (delayed hit tiles, hitsplat tracking), `detector/death/DeathDetector.java`, `ToaMistake.java`, `RaidRoom.java`, `RaidState.java`. 26 files. By the author of TobMistakeTracker (already held for ToB): the per-attack animation ids, projectile ids, graphics ids and hit delays in ticks that the wiki never states (e.g. Wardens `DDR_HIT_DELAY_IN_TICKS = 1`, Zebak `CHOMP_HIT_DELAY_IN_TICKS = 2`, Baba `BANANA_SLIP_COOLDOWN_IN_TICKS = 3`).

A client-side observer's constant is something the real server does (docs/RAID_ORCHESTRATOR.md section 3 step 4): rank above a wiki sentence, below a recording. Quote the constant and the file.
