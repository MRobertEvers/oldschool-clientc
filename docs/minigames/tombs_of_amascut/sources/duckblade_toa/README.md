# duckblade_toa

Repository: https://github.com/LlemonDuck/tombs-of-amascut
Commit: `831c39b9d60f40965d221fe5ac6b4d19d61c97f4` (committed 2026-09-10)
RuneLite plugin hub entry: `plugins/tombs-of-amascut` (read 2026-10-02 from github.com/runelite/plugin-hub master, pinned to the commit above); shallow fetch of that commit 2026-10-02.

Files copied (mechanics only; UI, config, overlay painting and test files dropped):

`util/` (RaidRoom region ids, RaidMode, Invocation values, RaidState/RaidStateTracker, RaidCompletionTracker, NpcUtil); `features/pointstracker/` (point multipliers, UniqueChanceCalculator, Purple weights); `features/het/beamtimer/BeamTimerTracker.java` (BEAM_FIRE_RATE_TICKS = 9, beam graphics-object ids) and `het/solver/`; `features/scabaras/overlay/*Solver.java` and MatchingTile (puzzle rules); `features/apmeken/` (Baboon types, wave table installer); `features/boss/akkha/AkkhaShadowHealth.java`, `boss/baba/BabaSarcophagusWarning.java`, `boss/kephri/swarmer/` (SwarmNpc, data manager, room data); `features/timetracking/` (Split, SplitsTracker, TargetTimeManager); `features/tomb/CursedPhalanxDetector.java`; `features/PathLevelTracker.java`, `AdrenalineCooldown.java`, `SmellingSaltsCooldown.java`. 48 files. The same plugin is also held as a 2022-era fork in the reference tree (`~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-client-main`, SOURCES.md section 3).

A client-side observer's constant is something the real server does (docs/RAID_ORCHESTRATOR.md section 3 step 4): rank above a wiki sentence, below a recording. Quote the constant and the file.
