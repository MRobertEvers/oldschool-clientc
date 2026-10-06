# LEDGER: Blert part of the Colosseum corpus

Pass `matthew-mbp-m4-waves-b1-spec-colosseum`, part `blert`, 2026-10-04 (UTC). Documents and a tool only. Read
`blert/PROVENANCE.md` before citing any row below: OBSERVED rows can carry grade B, ASSERTED rows are `[plugin]`.
Paths are under `docs/minigames/colosseum/sources/` unless they start with `tools/` or `build/`.

## 1. Fetches and copies

| When (UTC) | What | Url / source | Revision | File | For |
|---|---|---|---|---|---|
| 2026-10-04 | shallow clone | https://github.com/blert-io/plugin | `0efb39800b9e1f73e01c0116b962ccc93f0f8f6f` (2026-10-01) | `build/corpus_tmp/blert_plugin/` (not in git) | plugin source |
| 2026-10-03 (Inferno pass), re-read 2026-10-04 | shallow clone, already on disk | https://github.com/blert-io/blert | `7c7750cf01b23e5d223623d7c34c5ef10ca327fd` (2026-10-01) | `build/corpus_tmp/blert/` (not in git); `proto/` is an empty submodule dir | server, web, processing |
| 2026-10-03 (Inferno pass) | shallow clone | https://github.com/blert-io/protos | `450112dffc33e1e51b7ec201e1c8a45bf2f903c9` (2026-09-30) | `build/corpus_tmp/protos/` | event and storage schema |
| 2026-10-04 | copy, 45 files, licence headers intact | the three clones | as above | `blert/` (see `blert/README.md`) | the minigame's code only |
| 2026-10-04T00:17:12Z | probe, 1 request by curl (the tool did not exist yet; not in FETCH_LOG) | `GET https://blert.io/api/v1/challenges?type=4&limit=1` | HTTP 200, 422 bytes | `build/corpus_tmp/blert_probe_type4.json` | shape of the listing |
| 2026-10-04T00:18Z - 00:30Z | 230 requests by `tools/waves_gate/verify_blert.py --game colosseum`, every one HTTP 200: 4 listings (`type=4&limit=12&status=1`, `type=4&limit=12&status=3&stage=ge108` by hand, then inside `sample`: `limit=12&status=1` again and `limit=6&status=3&stage=ge109`), 18 `GET /challenges/colosseum/<uuid>` overviews, 208 `GET /challenges/colosseum/<uuid>/events?stage=<100..111>` | https://blert.io/api/v1 | blert server at the time (no version header) | `blert_api/FETCH_LOG.tsv` (date, status, bytes, url, file of every request), 18 files `blert_api/<uuid>.json` (1.3-1.7 MB each, 24 MB in all; none over 2 MB so none moved to `build/corpus_tmp/blert_api/colosseum/`) | the sample |
| 2026-10-04T00:32:32Z | ONE STRAY request: `GET /challenges?type=5&limit=0&status=1` -> 400, made by my own regression check of the Inferno default; its row was removed from `docs/minigames/inferno/sources/blert_api/FETCH_LOG.tsv` (file is unchanged against git) and is recorded here instead | https://blert.io/api/v1 | n/a | n/a | none |
| 2026-10-04 | derived, offline | the 18 cached files | n/a | `blert_api/observed_npc_events.tsv` (11,268 npc rows), `blert_api/wave_records.tsv` (208 rows), `blert/SAMPLE_SUMMARY.md`, `blert/ID_TABLE.md` | spec workers query these, not the 24 MB |

Sample: 12 completed runs (stage 111 = wave 12, status 1) and 6 wiped runs that reached wave 10 or 11 (stage >= 109; the
listing filters `stage=geN`), the most recent at 2026-10-04T00:1x. The throttle is `MIN_INTERVAL_SECONDS = 3.0` plus
0.05 s slack, a constant in the tool, shared by a lock file (`build/corpus_tmp/blert_api/.last_request`) with the Inferno
mode. The API serves a wave at a time, so a completed run is 13 requests.

## 2. API shape (probed 2026-10-04T00:17:12Z)

`GET /api/v1/challenges?type=4&limit=1` returns a JSON ARRAY of challenge rows: `uuid`, `sessionUuid`, `type` (4),
`startTime`, `finishTime`, `status` (0 in progress, 1 completed, 2 reset, 3 wiped, 4 abandoned), `stage` (the last
stage reached, 100..111), `mode` (0), `scale` (1), `challengeTicks`, `overallTicks`, `totalDeaths`, `splits` (map split
id to ticks), `party[]`. Filters in `blert/web/app/api/v1/challenges/query.ts:205-214` (`status`, `stage`, `startTime`,
`challengeTicks`, `scale`, `colo.handicap`, comparator forms like `ge108`); `limit` 1..100 (`route.ts:9-10`).
`GET /challenges/colosseum/<uuid>` returns the challenge plus `colosseum.waves[]` (stage, ticksLost, offset, handicap,
options, npcs{roomId: spawnNpcId, spawnTick, spawnPoint, deathTick, deathPoint}), `spawns{stage: {npcs, player,
modified}}`, `colosseumStats.handicaps`, `splits`. `GET .../events?stage=` returns the stored events of one wave as an
array (`blert/web/app/api/v1/challenges/colosseum/[id]/events/route.ts:12-19`; an optional single `type=` filter exists).

## 3. What this part states (numbers), with file and line

`P` = `blert/plugin/src/main/java/io/blert/`, `B` = `blert/blert/`, `S` = `blert/SAMPLE_SUMMARY.md`.

### Constants the code states (tag `[plugin]`; none is a measurement)

| Number | Quote | File:line |
|---|---|---|
| NPC hitpoints | `JAGUAR_WARRIOR(12810, 125,...)`, `SERPENT_SHAMAN(12811, 125,...)`, `MINOTAUR(new int[] {12812, 12813}, 225,...)`, archer/seer/berserker `12814-12816, 50`, `JAVELIN_COLOSSUS(12817, 220,...)`, `MANTICORE(12818, 250)`, `SHOCKWAVE_COLOSSUS(12819, 125,...)`, `SOL_HEREDIT(12821, 1500,...)`, `HEALING_TOTEM(12825, 1)`; bees 12823, prism 12824, solarflare 12826 = 0 ("not meaningful") | `P challenges/colosseum/ColosseumNpc.java:32-55` |
| animation to attack | 10847 jaguar, 10859 shaman, 10843 minotaur, 10850 archer, 10853 seer, 10856 berserker, 10892 / 10893 javelin auto / toss, 10903 shockwave, 10883 / 10884 / 10885 / 10887 Sol thrust / break / slam / combo | `ColosseumNpc.java:32-51`; `ID_TABLE.md` |
| manticore | `LOADING_ANIMATION = 10868`, `ATTACK_ANIMATION = 10869`, projectiles (spotanims) 2681 mage, 2683 range, 2685 melee; `attacksRemaining = 3` | `P challenges/colosseum/Manticore.java:35-40`, `:85` |
| Sol and effect ids | reentry `50743` / `50744`; totem projectile `2687`; dust `2669,2670,2671`; laser scan `2689-2691`, shot `2693-2695`; pool `2698` | `P challenges/colosseum/WaveDataTracker.java:58-64` |
| grapple hit timeout | "If a grapple was announced and no defend/parry message arrived within 4 ticks, it was a hit." `getTick() - pendingGrappleTick > 4` | `WaveDataTracker.java:437-438` |
| grapple lines | CRUSH YOUR BODY (torso), BREAK YOUR BACK (cape), TWIST YOUR HANDS OFF (gloves), BREAK YOUR LEGS (legs), CUT YOUR FEET OFF (boots) | `WaveDataTracker.java:77-82` |
| arena and regions | `COLOSSEUM_REGION_ID = 7216`, lobby `7316`, `WorldArea(1806, 3088, 38, 38, 0)`, Minimus `12808`, chest `50741`, script `4931`, varbit `9788` | `P challenges/colosseum/ColosseumChallenge.java:52-60` |
| waves counted | `if (currentWave < 13)`; handicap read `new DeferredTask(..., 3)` ticks after Minimus despawns; finish deferred 3 ticks | `ColosseumChallenge.java:258`, `:142-157`, `:239` |
| a tick | `MILLISECONDS_PER_TICK = 600` | `P util/Tick.java:30` |
| wave 12 start offset | `startWave(-1)` on `Sol Heredit jumps down from his seat...` | `WaveDataTracker.java:46`, `:307-310` |
| handicap ids | MANTIMAYHEM 0, REENTRY 1, BEES 2, VOLATILITY 3, BLASPHEMY 4, RELENTLESS 5, QUARTET 6, TOTEMIC 7, DOOM 8, DYNAMIC_DUO 9, SOLARFLARE 10, MYOPIA 11, FRAILTY 12, RED_FLAG 13 | `P challenges/colosseum/Handicap.java:30-43`; `protos/event.proto:716-731` |
| handicap level | `HANDICAP_LEVEL_INCREMENT = 30` (id = handicap + 30 x level); `NUM_HANDICAPS = 14` | `B challenge-harder/src/processing/colosseum.rs:27`, `:29`, `:179` |
| arena base and size | base (1808, 3123), 34 x 34 | `B web/app/utils/spawn-index.ts:30-32`; `colosseum.rs:33` |
| 12 spawn tiles of the indexed types | (1811,3109) (1817,3106) (1825,3114) (1821,3109) (1827,3109) (1836,3109) (1832,3107) (1811,3104) (1836,3104) (1821,3103) (1827,3103) (1824,3099); types shaman, javelin, manticore, shockwave | `spawn-index.ts:39-52`; `colosseum.rs:34-48` |
| the server's wave priors (four types only) | w1 shaman; w2 shaman javelin; w3 shaman javelin javelin; w4 shaman manticore; w5 shaman javelin manticore; w6 shaman javelin javelin manticore; w7 javelin manticore shockwave; w8 javelin javelin manticore shockwave; w9 javelin manticore manticore; w10 javelin javelin manticore manticore; w11 javelin manticore manticore shockwave; w12 none | `colosseum.rs:58-81` |
| Dynamic Duo | an extra shockwave colossus when the pick level > 0 and the wave has one | `colosseum.rs:182-190` |

### What the 18-run sample observed (tag `[blert]`, grade B candidates; sample sizes are the n in each line)

| Statement | Event kind | Where |
|---|---|---|
| Tick-0 set per wave is fixed: w1 archer+berserker+seer+shaman; w2 +javelin; w3 +2 javelin; w4 archer berserker seer manticore shaman; w5 +javelin; w6 +2 javelin; w7 archer berserker seer javelin manticore shockwave; w8 +2 javelin; w9 archer berserker seer javelin 2 manticore; w10 2 javelin 2 manticore; w11 javelin 2 manticore shockwave (18 of 18 runs agree except modifiers: Quartet adds one random Fremennik, Dynamic Duo a 2nd shockwave) | NPC_SPAWN tick 0 | `S:3-26` (one run of 18 per exception) |
| Reinforcements arrive on tick 66 of the wave, always (every post-tick-0 spawn of waves 2-11 is tick 66, n = 5, 18, 9+9, 18+18, 18+18, 18, 18, 18, 17+17, 12+12), on one of three tiles (1823,3120) (1824,3120) (1825,3120): w2-w3 one jaguar warrior; w4-w6 jaguar warrior + serpent shaman; w7-w9 minotaur; w10-w11 minotaur + serpent shaman. A wave that ends before tick 66 has none (w2: 5 of 18 reached it) | NPC_SPAWN after tick 0 | `S:874-898`; `blert_api/observed_npc_events.tsv` |
| Sol Heredit is first seen on tick 6 in 12 of 12 wave-12 streams; a fremennik seer once on tick 9; laser prisms spawn at ticks 27-226 (48 spawns, no fixed tick); one healing totem on wave 12 | NPC_SPAWN | `S:896-897`; see PROVENANCE for tick 0 |
| Healing totems on wave 11 spawn at ticks 7, 13, 31, 87, 132, 178, 216, 273, 303 (one per run, no fixed tick); lifetimes 1-15 ticks (n 10) | NPC_SPAWN / NPC_DEATH | `S:890`, `S:863-872` |
| Attack gaps (gap: count): javelin auto 5:1765 of 2013, javelin toss 5:418 of 450, serpent shaman 5:815 of 891, minotaur 5:332 of 340, jaguar warrior 5:101 of 115, shockwave 5:278 of 337, Fremennik archer 6:80 of 86, seer 6:8 of 11, berserker 6:3 of 4; a gap of 12 is two cadences | NPC_ATTACK (animation) | `S:831-851` |
| Manticore burst starts are 10 ticks apart (731 of 822 gaps); burst orders: mage-range-melee 558, range-mage-melee 406 (melee is always last), 49 short bursts; one burst = 3 events (asserted) | NPC_ATTACK (spotanim read) | `S:843`, `S:899-909` |
| Sol Heredit attack animation gaps are mixed: thrust 5:36 6:41 7:16 12:12 13:24 14:16 (n 166), slam 5:30 6:58 7:24 (n 124), break (11) and combo (10) rare | NPC_ATTACK | `S:848-851` |
| Sol effects: first dust tick 14 (10 of 12), 15 (2); dust gaps 5:64 6:108 7:32 (n 294); laser scan then shot 4 ticks (59) or 3 (11); grapple outcome parry at +4 (10), hit at +5 (1, asserted); no COLOSSEUM_TOTEM_HEAL event in 208 waves; doom hitsplats start at wave 2 (n 2) and reach 37 at wave 11 | events 204-207, 205, 201, 202 | `S:1175-1187` |
| Wave lengths (the game's own timer; median, ticks): 34, 59, 123, 104, 167, 189, 164, 190, 184, 256, 237, 223 for waves 1-12 (n 18, waves 10-11 exclude the wave a failed run died in) | STAGE_UPDATE / splits 152-163 | `S:952-966` |
| A completed run is 1,656-2,266 game ticks (n 12): the sum of the wave timers; the Minimus intermission is not counted | splits 150 | the listing in `build/logs/blert_list_done.log` and `S:928-950` |
| Fremennik trio tick-0 tiles are random (about 17 tiles seen for one archer in 18 runs); the four indexed types stand on the 12 tiles above | NPC_SPAWN tick 0 | `S:28-829` |
| Wave 12 reentry / doom / pool events need the modifiers; the pool events (206) are not a modifier but Sol's | events 201, 203, 206 | `S:1175-1187` |

## 4. Not available or not answered from this source

* The hit, the projectile's flight, damage, prayer checks: no Blert event. Needs our own tick log.
* NPC maximum hitpoints independent of the plugin constants; the cache `stat4` is the check (cache part).
* Whether `Wave: 12` or Sol's jump message starts wave 12's tick 0 (PROVENANCE.md OPEN).
* `COLOSSEUM_TOTEM_HEAL` (projectile 2687): zero events in 18 runs although 10 totems spawned; the heal rule is open. Zero events is not zero heals: the plugin records a heal only for a projectile sighted on cycle 0 leaving the totem's spawn tile (`blert/plugin/.../WaveDataTracker.java:232`, `:243`; closer's note in `blert/PROVENANCE.md`).
* Anything for modifiers' tick-level effects other than the spawn deltas above (one run each for Quartet and Dynamic Duo).
* The Colosseum recorder emits no event for the Minimus walk-in, the reward roll, the pet or cash-out: `PROVENANCE.md`.

## 5. The tool

`tools/waves_gate/verify_blert.py --game colosseum {list,fetch,sample,overviews,export,summary,ticklog}`: written on
`tools/verify_tob_timings.py`'s pattern; the Inferno pass's file generalised (`--game inferno` is the default and its
`summary` output is byte-identical to `docs/minigames/inferno/sources/blert/SAMPLE_SUMMARY.md` below line 1). Caches:
`blert_api/` (one file per challenge, a wave never refetched), checkpoints in `build/corpus_tmp/blert_api/colosseum/`.
`ticklog <path>` reads OUR server's `ticklog.tsv` through `read_ticklog` (TODO: `docs/minigames/waves_loop/DRIVER_NOTES.md`
fixes the columns; only that function changes). Manticore events are grouped into bursts so only observed burst starts
are measured. Checked 2026-10-04 with a synthetic six-row log in the scratchpad (manticore burst gap 20, shaman gap 6).
