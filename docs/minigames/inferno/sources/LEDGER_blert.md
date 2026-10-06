# LEDGER: Blert part (Inferno, challenge type 5)

Written 2026-10-03 by the blert corpus worker. Read `blert/PROVENANCE.md` before citing any number
here: only OBSERVED events carry grade B; plugin constants are "what a load-bearing plugin uses".

## Fetches and copies

| Date (UTC) | URL / source | Revision | File | For |
|---|---|---|---|---|
| 2026-10-03 | https://github.com/blert-io/plugin (shallow clone) | 0efb39800b9e1f73e01c0116b962ccc93f0f8f6f | blert/plugin/ (MIT, LICENSE kept; 32 code files copied across the three repos) | the recorder: event emit sites, npc table, pillars, wave tracker |
| 2026-10-03 | https://github.com/blert-io/blert (shallow clone) | 7c7750cf01b23e5d223623d7c34c5ef10ca327fd | blert/blert/ (9 files; no LICENSE file at this commit, README says MIT) | server processing (inferno.rs, spawn_index.rs), API routes |
| 2026-10-03 | https://github.com/blert-io/protos (shallow clone) | 450112dffc33e1e51b7ec201e1c8a45bf2f903c9 | blert/protos/ (event.proto, challenge_storage.proto, npc and attack definitions) | event schema |
| 2026-10-03 15:57Z | GET https://blert.io/api/v1/challenges?type=5&limit=1 | live API | build/corpus_tmp/blert_api/probe.json (+ probe.headers, not in git) | API shape: HTTP 200, JSON array of {uuid, sessionUuid, type, startTime, finishTime, status, stage, mode, scale, challengeTicks, overallTicks, totalDeaths, party[]}; status 3 = WIPED, stage 214 = wave 15; ratelimit header 100 |
| 2026-10-03 16:32Z to 17:33Z, 19:36Z to 19:40Z (FETCH_LOG first and last rows after 18:00Z: 19:36:24Z, 19:39:32Z) | 1,262 requests (closer recount of FETCH_LOG.tsv rows, all HTTP 200), one per 3 s, all in blert_api/FETCH_LOG.tsv (date, status, bytes, url, file) | live API | blert_api/{wave_records.tsv, FETCH_LOG.tsv, c8d56ab9-....json} in git; observed_npc_events.tsv (2.9 MB) and 7dca5546-....json (2,086,242 bytes) on disk only, over the loop's 2 MB rule (closer 2026-10-03, `blert_api/.gitignore`); the other 19 challenge caches (each 5 to 9 MB, over the 2 MB rule) stay in build/corpus_tmp/blert_api/ (106 MB, not in git) | per-wave event streams (GET /challenges/inferno/<uuid>/events?stage=<199+wave>) and per-wave records (GET /challenges/inferno/<uuid>) |
| 2026-10-03 | derived by tools/waves_gate/verify_blert.py summary | n/a | blert/SAMPLE_SUMMARY.md | first distributions, from OBSERVED events only |

Sample: the plan was 12 completed + 6 failed late (stage >= wave 31). That set of 18 is cached in full. A rerun
on 19:38Z found the live listing had moved on (the "most recent" listing is live) and fetched two newer
challenges: c8d56ab9 (waves 1 to 32 cached) and dd5b1591 (waves 1 to 28 cached); they were stopped at about 60 requests
and left PARTIAL on purpose (finishing would exceed the 150-request budget). The summary therefore reads 20 challenges,
1,235 wave streams; waves past 28 to 32 have n=18/19. Cache files over 2 MiB (all but two) are kept under build/corpus_tmp/; of the two in sources/blert_api/, only c8d56ab9 is in git (see the fetch row above). The "Wave end per run" table (SAMPLE_SUMMARY.md line 2812) is the lower bound from the last observed event; the "Wave length" table (line 2916) is the end message.
Per-challenge overviews are embedded in each cache file (the `overviews` command fetched 0 more: all present).
Not fetched: the wiki, Blert's website pages, any other host.

## What this part states (numbers), with file:line

Plugin constants (ASSERTED; "what a load-bearing plugin uses", paths under blert/plugin/src/main/java/io/blert/):

* Npc table, hitpoints and attack animation ids, `challenges/inferno/InfernoNpc.java:33-76`:
  `NIBBLER(7691, 10)`, `BAT(7692, 25, Pair.of(7578, ...BAT_AUTO))`, `BLOB(7693, 40, 7581 mage, 7582 melee, 7583 ranged)`,
  `BLOB_MAGER(7694, 15, 7581)`, `BLOB_RANGER(7695, 15, 7583)`, `BLOB_MELEER(7696, 15, 7582)`,
  `MELEER(7697, 75, 7597 auto, 7600 dig)`, `RANGER(7698, 125, 7604 melee, 7605 auto)`,
  `MAGER(7699, 220, 7610 auto, 7611 resurrect, 7612 melee)`, `JAD(7700, 350, 7590 melee, 7592 mage, 7593 ranged)`,
  `JAD_HEALER(7701, 90, 2637)`, ids 7702-7705 repeat ranger/mager/jad/healer for the Zuk set, `ZUK(7706, 1200, 7566)` (`:73`),
  `ZUK_MOVING_SAFESPOT(7707, 600)`, `ZUK_HEALER(7708, 75)` (`:75`), `ROCKY_SUPPORT(7709, 255)` (`:76`).
  The numeric hitpoints are the plugin's own table, not a measurement.
* `challenges/inferno/InfernoChallenge.java:60-61`: "The inferno timer begins 6 seconds (10 ticks) before the first wave." `WAVE_1_TIME_OFFSET_TICKS = 10`.
* `challenges/inferno/InfernoChallenge.java:143`: wave advance while `wave < 69` (69 waves).
* `challenges/inferno/Pillar.java:43-45`: pillar tiles west (2257, 5349, 0), east (2274, 5351, 0), south (2267, 5335, 0).
* `core/Stage.java:86-`: `INFERNO_WAVE_1(200)` ... one stage per wave, so wave N = stage 199+N.
* Server processing (blert/blert/challenge-harder/src/processing/inferno.rs): `const WAVE_INTERVAL_TICKS: Ticks = Ticks(6);` (`:26`) and the table `const WAVES: [&[u32]; 69]` (`:47-`: wave 1 `[JAL_MEJRAH]`, wave 2 two, wave 3 `[]`, wave 4 `[JAL_AK]` ...) and the arena origin `Coords { x: 2257, y: 5358 }` with spawn-index tiles (2258,5330), (2262,5335), (2272,5330), (2258,5353), (2273,5341), (2279,5353), (2260,5347), (2280,5333), (2280,5346) (`:28-40`). These are Blert's own assumed spawn set (ASSERTED).

Observed in the sample (event kind, n; all from blert/SAMPLE_SUMMARY.md, tick = recorder tick, Blert clamps spawn time at tick 0):

* Tick-0 set per wave (NPC_SPAWN, n=20 runs each for waves 1-27, 19 for 28+; section "Tick-0 set per wave", line 3): wave 1 bat x1 nibbler x3; wave 2 bat x2 nibbler x3; wave 3 nibbler x6; wave 4 blob x1 nibbler x3; wave 8 nibbler x6; wave 9 meleer x1 nibbler x3; wave 17 nibbler x6; wave 18 nibbler x3 ranger x1; the set is the same in every run. Nibbler counts here are what the recorder saw spawn on tick 0; settle against the wiki part.
* Spawn tiles per wave (NPC_SPAWN; line 77 onward) and spawn-index sets (line 2993 onward); read the tables there, with their counts.
* Attack gaps, ticks between consecutive NPC_ATTACK of the same npc (line 2744 onward): bat auto n=1872, gap 3 in 1451; blob ranged n=1263 gap 6 in 1039; blob mage n=1115 gap 6 in 802; bloblet mage n=527 gap 4 in 454; bloblet melee n=285 gap 4 in 233; bloblet ranged n=473 gap 4 in 419; jad ranged n=426 gap 9 in 356 (gap 8 in 58); jad mage n=438 gap 9 in 371; jad healer n=705 gap 4 in 690; mager auto n=5274 gap 4 in 4786; mager resurrect n=184 gap 4 in 163; meleer auto n=1430 gap 4 in 1196; ranger auto n=5179 gap 4 in 4671; zuk auto n=666 gap 10 in 381 and 7 in 283; zuk_jad gap 8 (n=75 ranged, 66 mage); zuk_mager auto n=358 gap 4 in 356; zuk_ranger auto n=64 gap 4 in 63. A gap that is a multiple of the cadence can be an unseen attack, a freeze or a retarget; the modal gap is the cadence evidence, the tail is not.
* First attack after a real spawn (NPC_SPAWN then NPC_ATTACK; line 2775): bloblet mage n=615 first attack 3 ticks after spawn in 519; bloblet melee n=478, 3 in 388; bloblet ranged n=677, 3 in 605; zuk_mager n=12, 6 in 11; zuk_ranger n=12, 8 in all; zuk_jad ranged n=7, 6 to 7.
* Wave length, start message to end message (line 2916; the per-wave record's `ticks`. Closer 2026-10-03: this line had said "start message to last observed event; lower bound, the stage end event is not served", which the tool contradicts at `tools/waves_gate/verify_blert.py:455-457` and the pinned `blert/blert/challenge-harder/src/processing/inferno.rs:346` (`ticks: events.duration()`) does not support; see SOURCES.md section 3): wave 1 min 8 median 15 max 30; wave 4 min 20 median 31 max 60; wave 7 min 32 median 52 max 93; wave 14 min 28 median 60 max 148 (n=20 each).
* Despawn lifetimes (NPC_DEATH is a DESPAWN, not the hitpoint-zero tick; line 2795): bloblet mage n=741, modal lifetime 7 ticks.
* Per-wave record (`startTick`, `ticksLost`, challengeTicks): blert_api/wave_records.tsv (1,269 rows).

Not available from this source (no event exists for it): the Zuk shield, the Zuk healers' timing, pillar damage, the mager's revive target, projectiles (see PROVENANCE.md section 1).
