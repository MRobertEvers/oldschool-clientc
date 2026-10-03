# Theatre of Blood — downloaded sources

Everything the plan cites, pulled down on **17 August 2026** so that a later edit
upstream cannot silently move the acceptance target. Each wiki page is stored as
raw wikitext and pinned to the revision listed below; the `?oldid=` link renders
exactly the text in this folder.

Nothing in here is authored by this project. Treat it as evidence, not as
specification: where two sources disagree the plan says so and picks one, and
where no source settles a number the plan raises a `[MEASURE]` task instead of
inventing one.

---

## 1. OSRS Wiki (primary; pinned revisions)

| File | Page | Revision | As of |
|---|---|---:|---|
| `wiki_Theatre_of_Blood.wikitext` | [Theatre of Blood](https://oldschool.runescape.wiki/w/Theatre_of_Blood?oldid=15303361) | 15303361 | 2026-08-16 |
| `wiki_Theatre_of_Blood_Strategies.wikitext` | [Theatre of Blood/Strategies](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies?oldid=15301586) | 15301586 | 2026-08-14 |
| `wiki_Theatre_of_Blood_Strategies_Nylocas.wikitext` | [.../Strategies/Nylocas](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies/Nylocas?oldid=15017125) | 15017125 | 2025-11-06 |
| `wiki_Theatre_of_Blood_Hard_Mode.wikitext` | [Theatre of Blood/Hard Mode](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Hard_Mode?oldid=15300156) | 15300156 | 2026-08-14 |
| `wiki_Theatre_of_Blood_Entry_Mode.wikitext` | [Theatre of Blood/Entry Mode](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Entry_Mode?oldid=15301035) | 15301035 | 2026-08-14 |
| `wiki_Guide_Advanced_Theatre_of_Blood.wikitext` | [Guide:Advanced Theatre of Blood](https://oldschool.runescape.wiki/w/Guide:Advanced_Theatre_of_Blood?oldid=15222073) | 15222073 | 2026-05-31 |
| `wiki_The_Maiden_of_Sugadinti.wikitext` | [The Maiden of Sugadinti](https://oldschool.runescape.wiki/w/The_Maiden_of_Sugadinti?oldid=15292433) | 15292433 | 2026-08-11 |
| `wiki_Pestilent_Bloat.wikitext` | [Pestilent Bloat](https://oldschool.runescape.wiki/w/Pestilent_Bloat?oldid=15273178) | 15273178 | 2026-07-23 |
| `wiki_Nylocas_Vasilias.wikitext` | [Nylocas Vasilias](https://oldschool.runescape.wiki/w/Nylocas_Vasilias?oldid=15218278) | 15218278 | 2026-05-27 |
| `wiki_Nylocas_Matomenos.wikitext` | [Nylocas Matomenos](https://oldschool.runescape.wiki/w/Nylocas_Matomenos?oldid=15218276) | 15218276 | 2026-05-27 |
| `wiki_Nylocas_Prinkipas.wikitext` | [Nylocas Prinkipas](https://oldschool.runescape.wiki/w/Nylocas_Prinkipas?oldid=15218279) | 15218279 | 2026-05-27 |
| `wiki_Sotetseg.wikitext` | [Sotetseg](https://oldschool.runescape.wiki/w/Sotetseg?oldid=15234530) | 15234530 | 2026-06-17 |
| `wiki_Xarpus.wikitext` | [Xarpus](https://oldschool.runescape.wiki/w/Xarpus?oldid=15267723) | 15267723 | 2026-07-19 |
| `wiki_Verzik_Vitur.wikitext` | [Verzik Vitur](https://oldschool.runescape.wiki/w/Verzik_Vitur?oldid=15279260) | 15279260 | 2026-07-29 |
| `wiki_Support__Theatre_of_Blood_.wikitext` | [Support (Theatre of Blood)](https://oldschool.runescape.wiki/w/Support_(Theatre_of_Blood)?oldid=15201834) | 15201834 | 2026-04-29 |
| `wiki_Web__Verzik_Vitur_.wikitext` | [Web (Verzik Vitur)](https://oldschool.runescape.wiki/w/Web_(Verzik_Vitur)?oldid=15022612) | 15022612 | 2025-11-11 |
| `wiki_Exhumed.wikitext` | [Exhumed](https://oldschool.runescape.wiki/w/Exhumed?oldid=14203521) | 14203521 | 2021-11-11 |
| `wiki_Blood_spawn.wikitext` | [Blood spawn](https://oldschool.runescape.wiki/w/Blood_spawn?oldid=15215871) | 15215871 | 2026-05-23 |
| `wiki_Dawnbringer.wikitext` | [Dawnbringer](https://oldschool.runescape.wiki/w/Dawnbringer?oldid=15235020) | 15235020 | 2026-06-18 |
| `wiki_Chest__Theatre_of_Blood_.wikitext` | [Chest (Theatre of Blood)](https://oldschool.runescape.wiki/w/Chest_(Theatre_of_Blood)?oldid=15101638) | 15101638 | 2026-01-08 |

Also present, unpinned because nothing load-bearing depends on them:
`wiki_Nylocas.wikitext`, `wiki_Nylocas_{Ischyros,Toxobolos,Hagios,Athanatos,Queen}.wikitext`,
`wiki_Perfect_{Maiden,Bloat,Nylocas,Sotetseg,Xarpus,Verzik}.wikitext`,
`wiki_{Scythe_of_vitur,Ghrazi_rapier,Sanguinesti_staff,Justiciar_armour,Avernic_defender}.wikitext`,
`wiki_{Lil__Zik,Sinhaza_shroud,Mysterious_Stranger,Vyre_Orator,Ver_Sinhaza,A_Night_at_the_Theatre}.wikitext`,
`wiki_{Message,Broken_support,Theatre_of_Blood_Records}...`.

Two page titles were probed and do not exist on the Wiki, so there is no file for
them: `Sotetseg's maze` (the maze is documented inside the Sotetseg page) and
`Theatre of Blood/Rewards` (rewards live on `Monumental chest` and on the Hard Mode
page). Don't re-probe them.

## 2. rev-239 cache extracts (authoritative for ids)

Produced from `OSRS-Content/osrs239-content/` — the unpacked osrs239 cache that
both the client and the server in this tree read.

| File | Contents |
|---|---|
| `cache_npc_maiden.txt` | every `tob_maiden_*`, `maiden_elemental`, `maiden_blood_slug`, `maiden_transmog` npc record, all three modes, with its numeric id |
| `cache_npc_bloat.txt` | `tob_bloat`, `_story`, `_hard` |
| `cache_npc_nylocas.txt` | all `tob_nylocas_*` and `nylocas_boss_*` records (incoming/fighting × small/big × 3 styles × 3 modes, plus the four boss forms and the pillar npc) |
| `cache_npc_sotetseg.txt` | `tob_sotetseg_{noncombat,combat,creeper}` × 3 modes |
| `cache_npc_xarpus.txt` | `tob_xarpus_{static,feeding,combat}` and `xarpus_death` × 3 modes |
| `cache_npc_verzik.txt` | every `verzik_*` / `tob_verzik_*` record: the five phase npcs, the transitions, the death bat, webs, pillars, rubble, throne, the three combat nylocas, the armoured and blood nylocas, the tornado, and the five pets |
| `cache_npc_attackrates.txt` | just the `attackrate`, `param_26` (attack type), `size`, `stat*` and `vislevel` rows of all of the above, side by side |
| `cache_seq.txt` | 131 ToB sequence ids (`maiden_*`, `tob_bloat_*`, `top_spider_*`, `tob_sotetseg_*`, `tob_xarpus_*`, `verzik_*`, `nylocas_*`) |
| `cache_spotanim.txt` | 64 ToB spotanim/graphic ids |
| `cache_sounds.txt` | 113 ToB sound-effect ids from `pack/4_soundeffects.pack` |
| `cache_locs.txt` | 456 `tob_*` loc ids — the whole surface area, both castles, every dungeon room, the throne, pillars, cages and floor tiles |
| `cache_vars.txt` | the ToB varbits (`tob_client_*`, `tob_treasureroom_*`, `tob_progress`, `tobquest*`, …) |

Regenerate any of them with `tools/…`-style one-liners over
`OSRS-Content/osrs239-content/configs/all.<type>` plus the matching
`.compack` id map; the exact commands are in the plan's §3.

## 3. Crowdsourced tick data (code, not prose)

### `blert_plugin/` — [blert-io/plugin](https://github.com/blert-io/plugin) (BSD/MIT, `main`)

25 files from `src/main/java/io/blert/challenges/tob/`. Blert is a tick-accurate
raid recorder; its trackers are the single best source in this folder because
every number in them is a constant the recorder must match to parse a real raid.

Load-bearing files:

- `TobNpc.java` — every ToB npc id in all three modes with its **hitpoints by scale**
  as `{trio-and-below, 4-man, 5-man}`.
- `Location.java` — **region ids and room world-areas** for all six rooms plus the
  lobby, corridor, Sotetseg underworld and loot room.
- `BloatDataTracker.java` — `BLOAT_DOWN_CYCLE_TICKS = 32`, `BLOAT_STOMP_TICK = 3`.
- `MaidenCrab.java` — the **20 crab spawn tiles** (10 positions × normal/"scuffed").
- `NylocasDataTracker.java` — `WAVE_TICK_CYCLE = 4`, `NATURAL_STALLS[31]`, room caps.
- `NyloWave.java` — the 31 wave compositions as six-slot lane keys.
- `SpawnType.java` — the seven nylocas lane spawn tiles.
- `SotetsegDataTracker.java` — `SOTE_ATTACK_SPEED = 5`; `Maze.java` — 14×15 grid, both maze origins.
- `XarpusDataTracker.java` — `FIRST_P2_TURN_TICK = 7`, `TICKS_PER_TURN_P2 = 4`, `TICKS_PER_TURN_P3 = 8`.
- `VerzikDataTracker.java` — the whole Verzik clock: P1 14, P2 4, P3 7/5, first-attack
  offsets, `P3_ATTACKS_BEFORE_SPECIAL = 4`, `P2_ATTACKS_PER_REDS = 7`, green-ball delay 12.
- `VerzikSpecial.java` — the P3 special rotation `CRABS → WEBS → YELLOWS → BALL`.

### `blert_nylocas-waves.json` — [blert-io/blert](https://github.com/blert-io/blert) `common/data/nylocas-waves.json`

The complete 31-wave dataset: per wave, the natural stall in ticks and, for each of
the three lanes' two slots, the nylo's size, aggro flag and full flicker rotation.
Rendered into [`../nylocas_waves.md`](../nylocas_waves.md).

### `blert_nylo_pillar_assignment.json` — **derived here, not downloaded**

The one file in this folder that this project authored, and the exception to the
"evidence, not specification" rule above — because for the spawn→pillar assignment
there was no evidence to download. It is the output of
[`../../../../tools/derive_tob_nylo_pillars.py`](../../../../tools/derive_tob_nylo_pillars.py)
over 199 raid recordings pulled from blert's public event API
(`/api/v1/raids/tob/<uuid>/events?stage=12`), each nylo followed from its spawn
tile to the tile it stops on.

One record per spawn slot: wave, lane, slot, spawn tile, size, style, the pillar it
attacks, and the raid counts behind that call — including `disagreeing`, which is
empty on every row. The 199 uuids are listed in `raidUuids` so the measurement can
be re-fetched; the raw dumps themselves are ~300 kB each and are not committed.
Rendered into [`../nylocas_waves.md`](../nylocas_waves.md#which-pillar-each-spawn-attacks).

### `blert_guides/` — [blert-io/blert](https://github.com/blert-io/blert) `web/app/guides/tob/`

`tob_nylocas_mechanics_page.tsx` is the prose behind
<https://blert.io/guides/tob/nylocas/mechanics>: the 4-tick cycle, "first wave
spawns on the fourth tick of the room", the 52-tick self-destruct, the 2-tick
flicker hold, room caps, and demi-bosses counting as 3.

### `openosrs_theatre/` — [JourneyDeprecated/OpenOSRS](https://github.com/JourneyDeprecated/OpenOSRS) `net.runelite.client.plugins.theatre`

The older, widely-forked ToB plugin. 19 files. Independent confirmation of the
wave table (`NyloPredictor.java`, including a hand-written **aggro table**),
the Maiden freeze spawn groups (`MaidenHandler.java`), Xarpus exhumed counts by
party size (`XarpusHandler.java`), and Verzik's per-phase tick counters
(`VerzikHandler.java`). Where it disagrees with blert, blert wins — this plugin
is years older and its Verzik counters are visibly heuristic.

### `tobqol/` — [damencs/tob-qol](https://github.com/damencs/tob-qol)

The plugin-hub "ToB QoL". Used for the Bloat room's game-object ids
(`BloatConstants.java`: tank, top-of-tank, ceiling chains, floor) and the Maiden
phase/crab model.

### `tobutilities/` — [NCG-RS/TobUtilities](https://github.com/NCG-RS/TobUtilities)

Bloat floor object ids and the "Maiden scuff warning" model.

### `tobmistaketracker/` — [QuestingPet/TobMistakeTracker](https://github.com/QuestingPet/TobMistakeTracker)

A plugin that detects, per tick, which raider made which mistake. Because it has to
attribute damage to the right player on the right tick, it encodes the raid's
timing rules more explicitly than anything else in this folder.

- `VerzikMeleeChancedTracker.java` — stores `lastTickTargetArea` / `lastTickVerzikArea`
  every tick and judges the tank from **last tick's** positions when the attack
  animation fires. Its predicate is the actual melee-chance condition:
  `!verzikArea.intersectsWith(tankArea) && verzikArea.distanceTo(tankArea) == 1`
  — adjacent draws a melee, *underneath* does not.
- `MaidenMistakeDetector.java` — `MAIDEN_BLOOD_GAME_TICK_LENGTH = 11`
  (*"each blood tile from maiden lasts exactly 11 ticks"*), and the splat's
  activation tick computed from the projectile's remaining cycles ÷ 30.
- `BloatMistakeDetector.java` — hand tiles keyed on graphic **1576** and cleared
  every tick: a hand is lethal for exactly one tick.
- `VerzikP2MistakeDetector.java` — urnbomb tiles keyed on graphic **1584**, also
  cleared every tick; Hard Mode acid pool is game object **41747**; the player
  knockback animation is **1157**.
- Every detector tests `raider.getPreviousWorldLocation()`, never the current tile.

### `community_tools/` — practice simulators

`devqhp_sotetseg.js` — the full source of the
[Sotetseg maze trainer](https://devqhp.github.io/osrs/sotetseg/). Contains the maze
generator (`maze_width 14`, `maze_height 15`, `path_turns 8`, `max_x_change 5`,
`tornado_row 4`) and credits the high-level players who supplied the maze corpus it
was derived from. See [`../COMMUNITY_SOURCES.md`](../COMMUNITY_SOURCES.md) §3.1.

The [Xarpus melee trainer](https://spacescape20xx.itch.io/xarpus-melee-trainer) by
SpaceScape has no downloadable source, but its instructions are quoted in
[`../ENCOUNTER_TIMING.md`](../ENCOUNTER_TIMING.md) §6.1 — it is the source that
names the scan tick in plain words.

### `vtob/` — [Vainiven/V-TOB](https://github.com/Vainiven/V-TOB)

A ToB bot script. Its value here is purely geometric: `PestilentBloat.java` names
the Bloat room's four quadrant areas, the four pillar-hug tiles and the barrier
tile as world coordinates.

## 4. Video guides (machine transcripts)

`transcripts/` holds **19** YouTube guides downloaded with
`yt-dlp --write-auto-subs`, deduped and chaptered by `vtt_to_md.py`. These are the
least reliable sources in the folder — auto-captions mangle numbers ("sod egg" is
Sotetseg, "zarus"/"sarpus" is Xarpus, "versic" is Verzik) — and are cited only where
they corroborate something already established, **except** where a guide states a
movement instruction in ticks, which no plugin can express.

**Whole-raid**

| File | Video | Length |
|---|---|---|
| `yt_yNZZQNAdQAM.md` | S2L OSRS, *Max-Efficiency Theatre of Blood 4s Guide 2025* — <https://www.youtube.com/watch?v=yNZZQNAdQAM> | 57:34 |
| `yt_KF9y2GYTJ-A.md` | Patyfatycake, *The Ultimate Beginners Guide To The Theatre Of Blood* — <https://www.youtube.com/watch?v=KF9y2GYTJ-A> | 31:09 |
| `yt_VU4WQ1ghn4E.md` | Chriskies, *Solo ToB guide* — <https://www.youtube.com/watch?v=VU4WQ1ghn4E> | 27:09 |
| `yt_G9jx6OUnaws.md` | Beleti, *Hard Mode ToB (Mage POV)* — <https://www.youtube.com/watch?v=G9jx6OUnaws> | 26:23 |

**Per boss** — one dedicated guide per room, minimum

| Room | File | Video |
|---|---|---|
| Maiden | `yt_VEqiIF9EbcM.md` | Indarkment, *Grandmaster Explains Maiden in 3 Levels of Difficulty* |
| Maiden | `yt_1ldGvUsOx2M.md` | Indarkment, *Maiden Freezing 101* |
| Bloat | `yt_oKXoj9Yxy7Q.md` | BillNylo, *Bloat Flinching and Tick Fixing Guide* — the rise window |
| Bloat | `yt_3yAP8lsyBcE.md` | Horselord, *Duo Bloat Guide* (22:29) |
| Nylocas | `yt__QXdNAZh7Yo.md` | Deflne Alive, *Optimal Nylocas Strategy: How to Achieve Sub 4* |
| Nylocas | `yt_yAi5A52J32E.md` | The Academy, *Nylocas Vasilias — Team Guide* |
| Sotetseg | `yt_JdtL9UI5uy0.md` | cBold, *Sotetseg Maze Guide — Diagonals and L-Shapes* |
| Xarpus | `yt_fPpIRjQWtlE.md` | Crusher, *5 Tick Xarpus Guide — Master the Scythe Walk* (16:02) |
| Xarpus | `yt_Lt-iZwJUKmc.md` | Plank2g, *ToB Made Easy: Melee Xarpus* |
| Xarpus | `yt_uQzR4iIuv6s.md` | Lone Gym Rat, *Melee Xarpus 5 Tick Scythe Example* |
| Verzik P2 | `yt_KiaFwopnnEI.md` | Rob, *Learning ToB: All you need to know about Verzik P2* |
| Verzik P2 | `yt_eswoo8D364c.md` | Plank2g, *Verzik P2 Explained for Whip and Scythe Walk* |
| Verzik P3 | `yt_D1b4eWwnOHU.md` | S2L OSRS, *Complete Verzik P3 Guide* (15:21) |
| Verzik P3 | `yt_oGPT3sZMnd8.md` | 07samsquanches, *P3 Verzik Guide — Learner, Tanking, Pogtank* (18:37) |
| Verzik P3 | `yt_3lQjrLeuvHo.md` | Granddad Jad, *Phase 3 Verzik Tanking Guide (1:1 and 2:1:1)* |

## 4a. Added 2026-10-02 (spec-pass corpus) — more guide videos, per room

Downloaded 2026-10-02 with `yt-dlp --skip-download --write-auto-subs --sub-langs en-orig --sub-format vtt --write-info-json` (3 s apart; a 429 burst on four ids was retried after a minute-scale wait) and converted by `tools/raid_gate/vtt_to_md.py`. Transcripts are `transcripts/yt_<id>.md`.

**Overlay column.** *Not watched*: this worker read titles, descriptions, chapters and captions only. `stated` means the title, description or the spoken captions name a tick counter / visual metronome / attack timer plugin; blank means nothing in the text says so and the spec worker must open the video (frame_count.py) to know. A named overlay is a lead, not a measurement.
**Tick-talk timestamps** are paragraphs where the speaker says an N-tick figure or cycle (`mm:ss`, from the captions; useful anchors for the room's attacks); chapters are the uploader's own.

| Room | Channel | Title / url | Length | fps | Overlay | Chapters (uploader) | Tick-talk timestamps |
|---|---|---|---|---|---|---|---|
| Bloat | The Academy | Premier OSRS Guides | [PESTILENT BLOAT - Team Guide](https://www.youtube.com/watch?v=l57Jlt1wbnA) `yt_l57Jlt1wbnA.md` | 3:29 | 60 |  | 0:00 Intro; 0:06 Bloat Mechanics; 0:09 Line of Sight; 1:41 Stomp; 2:13 Mutilated Flesh; 2:33 Player Movement; 2:59 Reward Chest; 3:21 Outro | none |
| Bloat | Plank2g | [TOB Made Easy: The Pestilent Bloat / Bloat Guide 2020/2021](https://www.youtube.com/watch?v=actUD0l9LSU) `yt_actUD0l9LSU.md` | 3:37 | 60 |  | 0:00 Intro; 0:05 Mechanics; 1:09 Walkthrough | 1:38, 2:12 |
| Bloat | SuppCarriesU | [Pestilent Bloat](https://www.youtube.com/watch?v=3IGhBM2vsQE) `yt_3IGhBM2vsQE.md` | 7:29 | 60 |  | none | none |
| Entry mode | RuneWraith | [Entry Mode Theatre of Blood Guide / Fast / Easy](https://www.youtube.com/watch?v=B_gjVdmfOrY) `yt_B_gjVdmfOrY.md` | 32:28 | 60 |  | 0:00 Intro; 1:04 Skills Stats; 1:47 Gear; 3:22 Inventory; 7:18 Disclaimer; 7:52 Maiden; 9:45 Pestilent Bloat; 12:39 Night Locust; 16:29 Sodaseg; 19:21 zarpus; 22:28 verzik; 24:52 verzik phase 2 | 6:36, 8:47, 24:46, 27:00, 28:42 |
| Entry mode (budget) | Help Me RNG | [OSRS budget & low skill Night at the Theatre Guide (TOB entry mode)](https://www.youtube.com/watch?v=9MPHZy4sjmM) `yt_9MPHZy4sjmM.md` | 35:00 | 30 |  | 0:00 <Untitled Chapter 1>; 0:26 Gear; 1:10 Maiden; 2:56 Bloat; 6:20 Nylocas room; 12:55 Nylocas boss; 14:35 Soteseg; 18:15 Xarpus; 23:00 P1 verzik; 25:15 P2 verzik; 29:35 P3 verzik | none |
| Entry mode (solo ranged) | Cudabear | [Night at the Theatre Low Level Ranged Guide / How to Easily Solo Theatre of Bloo](https://www.youtube.com/watch?v=PjoiEMcrREc) `yt_PjoiEMcrREc.md` | 16:51 | 30 |  | 0:00 Intro; 0:38 Equipment Inventory; 2:35 Maiden; 3:31 Bloat; 4:44 nilo; 7:30 Sodag; 8:38 The Maze; 11:35 Verzik; 12:10 Phase 1 2; 13:14 Phase 2 3 | none |
| Entry mode / first KC, whole raid | 10Boot OSRS | [The ONLY ToB Guide You NEED (2026) - Simple first KC](https://www.youtube.com/watch?v=4i4lv-srJkw) `yt_4i4lv-srJkw.md` | 35:58 | 30.0 |  | 0:00 <Untitled Chapter 1>; 1:45 What you ACTUALLY need!; 2:51 Stats, Gear, Inventory, Information; 6:21 Maiden; 9:35 Bloat; 12:27 First Chest; 12:55 Like / Subscribe / Creator Crafted; 13:25 Nylo Room; 15:05 Nylo Boss; 16:10 Sotetseg; 19:05 Second Chest; 19:52 Xarpus | none |
| Entry mode, whole raid | NoBS OSRS | [Theatre of Blood Entry Mode Guide  /  July 2026](https://www.youtube.com/watch?v=M1t2qWMbzEs) `yt_M1t2qWMbzEs.md` | 23:03 | 60 |  | 0:00 Introduction; 1:50 Gear & Inventory; 2:36 Maiden; 4:39 Bloat; 7:14 Nylocas; 11:50 Sotetseg; 14:03 Xarpus; 16:39 Verzik | none |
| Entry mode, whole raid | 10Boot OSRS | [OSRS The only TOB entry mode guide you need (FOR NOOOBS)](https://www.youtube.com/watch?v=bCkpMm0ZDHE) `yt_bCkpMm0ZDHE.md` | 10:43 | 24.0 |  | 0:00 Intro; 0:31 Gear; 1:24 Maiden; 2:35 Bloat; 3:23 Nyo; 5:10 Maze; 5:37 Zarus; 7:12 Phase II; 8:21 Phase III; 9:44 TLDR | 0:34 |
| Maiden | The Academy | Premier OSRS Guides | [MAIDEN OF SUGADINTI - Team Guide](https://www.youtube.com/watch?v=vnf1QWKReLY) `yt_vnf1QWKReLY.md` | 10:12 | 60 |  | 0:00 Intro; 0:06 Maiden Mechanics; 0:13 Auto Attacks; 0:57 Blood Spawn; 1:15 Blood Spiders; 1:39 Roles; 1:50 Freezer; 3:50 DPS; 4:03 Role Perspective; 4:55 N123; 6:53 S124; 9:02 DPS | none |
| Maiden (perfect, solo) | QCS OSRS | [Guide to Perfect Maiden Solo / Master Achievement / OSRS / QCS](https://www.youtube.com/watch?v=dLT7jalJLdw) `yt_dLT7jalJLdw.md` | 6:02 | 30 |  | 0:00 Preparation and gear; 0:32 Managing blood spawns; 1:56 Combat mechanics and flow; 3:24 Final phase and conclusion | none |
| Nylocas | Indarkment | [Grandmaster Explains Nylos in 3 Levels of Difficulty](https://www.youtube.com/watch?v=6soXuRA77JU) `yt_6soXuRA77JU.md` | 22:20 | 60 |  | 0:00 Intro; 0:30 Beginner; 3:15 Intermediate; 7:25 Advanced | 4:57, 6:02, 7:40, 9:19, 11:34, 12:07, 14:24, 14:57, 17:12, 17:45 |
| Nylocas (HM) | Plank2g | [Hard Mode ToB: Nylocas Guide / HMT Nylo Guide](https://www.youtube.com/watch?v=eAJ0xnLpxok) `yt_eAJ0xnLpxok.md` | 6:49 | 25 | stated: tick counter | 0:00 Hard mode mechanics; 0:32 Wave strategy and princes; 2:38 Common failure points; 4:49 Nilo king strategy | 0:00, 3:20 |
| Nylocas (spec weapon) | S2L OSRS | [Make nylos EASY with this spec weapon](https://www.youtube.com/watch?v=2MnpEu6cbG0) `yt_2MnpEu6cbG0.md` | 5:01 | 60 |  | 0:00 Setting up the plugin; 0:55 First dins usage; 2:22 Second dins usage; 3:10 Third dins usage; 3:51 Final boss and wrap up | none |
| Pathfinding / scan rule (all rooms) | PurpleGod | [Pathfinding in OSRS applied to ToB](https://www.youtube.com/watch?v=DX8lN3r3ALw) `yt_DX8lN3r3ALw.md` | 13:05 | 30 |  | 0:00 Introduction; 0:10 Understanding server ticks; 1:18 Movement mechanics; 2:11 Pathing shapes; 3:57 Advanced tick packing; 4:46 Weapon range and animations; 5:41 Blood maiden tactics; 6:49 Bloat fight strategies; 8:16 Soldiers leg maze; 9:14 Sotetseg mechanics; 10:42 Verzik and conclusion | 0:32, 1:05, 1:37, 2:13, 3:55, 4:31, 5:42, 6:15, 6:48, 7:22 |
| Sotetseg | Evse | [Sotetseg Maze - Diagonals and L moves Guide - Theatre of blood](https://www.youtube.com/watch?v=KDTlRVi6YTY) `yt_KDTlRVi6YTY.md` | 7:56 | 30 |  | 0:00 Introduction to movement; 0:19 Mastering diagonal moves; 3:05 Understanding L-moves; 6:14 In-game application; 7:11 Summary and tips | none |
| Sotetseg | Plank2g | [ToB Made Easy: Sotetseg / How to Sotetseg 2022](https://www.youtube.com/watch?v=90957FaXfjM) `yt_90957FaXfjM.md` | 4:00 | 25 |  | none | 2:19 |
| Sotetseg (1-tick maze, short) | Taran | [1 tick Sote Maze W/ Learners](https://www.youtube.com/watch?v=apFSOD8yZNw) `yt_apFSOD8yZNw.md` | 30 | 60 |  | none | none |
| Sotetseg (maze skip) | Horselord | [Sotetseg Maze Skip Guide](https://www.youtube.com/watch?v=EntowMeBPNg) `yt_EntowMeBPNg.md` | 9:34 | 30 |  | 0:00 1. Intro; 0:18 2. Maze Mechanics; 2:15 3. Damage Control; 3:03 4. Skip Procedure; 7:55 5. Not a Perfect Method; 8:45 6. Not a Bug; 8:57 7. Credits | 0:33, 1:08, 1:43, 3:58, 5:37 |
| Sotetseg (tick-eat, short) | Taran | [How not to tickeat at Sotetseg](https://www.youtube.com/watch?v=o-zdMHT_rsc) `yt_o-zdMHT_rsc.md` | 23 | 60 |  | none | none |
| Verzik P2 | RS Mina | [Tob Verzik P2 Advanced Scythe Walk Guide - OSRS](https://www.youtube.com/watch?v=Blx1bKQbed8) `yt_Blx1bKQbed8.md` | 1:43 | 24 |  | none | 0:00, 0:32 |
| Verzik P3 (tank) | Youthful | [Learn to Tank Verzik in 1 Minute [OSRS]](https://www.youtube.com/watch?v=9KyriOtJJEk) `yt_9KyriOtJJEk.md` | 1:12 | 60 | stated: metronome, plugin | none | 0:00, 0:32 |
| Verzik P3 (tank, pogtank) | Fill2 | [Quick guide how to tank verzik and easy pogtank setup](https://www.youtube.com/watch?v=BVfwJSSSemo) `yt_BVfwJSSSemo.md` | 2:26 | 30 | stated: metronome | none | 0:02, 1:06, 1:41 |
| Verzik P3 (tornado) | Plank2g | [ToB Made Easy: Tornado DPS Guide for P3 Verzik](https://www.youtube.com/watch?v=sDaQ2qsU8AQ) `yt_sDaQ2qsU8AQ.md` | 2:45 | 60 |  | 0:00 <Untitled Chapter 1>; 0:07 Don't heal Verzik, ever.; 0:39 ID Your Tornado.; 1:23 Don't Stop Moving.; 1:34 Dump Special Attacks.; 1:46 Rectangles.; 1:59 Run Webs, Always.; 2:13 Other DPS Guide applies!; 2:21 Get 10 Gauntlet KC. | none |
| Whole HM raid | Indarkment | [Hard Mode ToB for Impatient People (OSRS)](https://www.youtube.com/watch?v=9coCVByPHCw) `yt_9coCVByPHCw.md` | 10:31 | 60 |  | 0:00 Intro; 0:15 Inventories; 0:26 Maiden; 1:38 Bloat; 2:28 Nylos; 4:38 Sotetseg; 5:49 Xarpus; 6:44 Verzik | 0:33, 7:12 |
| Whole raid, learner POV | aatykon | [Learn ToB - Range Learner Perspective Walkthrough 1/3](https://www.youtube.com/watch?v=D2DRddtC5H8) `yt_D2DRddtC5H8.md` | 26:48 | 60 | stated: plugin | 0:00 Intro; 1:55 Setup; 7:30 Nylo; 12:30 Hammer; 16:37 Office | none |
| Whole raid, perfect theatre | S2L OSRS | [Perfect Theatre is EASY / Full guide 2025 updated / all roles](https://www.youtube.com/watch?v=H-e-xe1yaAw) `yt_H-e-xe1yaAw.md` | 26:39 | 60 | stated: metronome | 0:00 intro; 0:30 Gear and Inventories; 0:44 plug-ins, custom menu swaps; 1:11 The Maiden of Sugadinti; 5:39 The Pestilent Bloat; 7:09 The Nylocas; 10:09 Sotetseg; 14:49 Xarpus; 18:54 The Final Challenge, Verzik Vitur; 25:29 Outro | 7:15, 10:16, 12:34, 21:12, 21:48, 22:53 |
| Xarpus | Okirra | [Scythe Walking Xarpus guide TOB (Quick Easy)](https://www.youtube.com/watch?v=i_XMP9pV7YE) `yt_i_XMP9pV7YE.md` | 1:29 | 60 |  | 0:00 Introduction to guide; 0:14 Basic attack steps; 0:27 Rhythmic scythe technique; 1:15 Alternative safe spot | 0:34, 1:06 |
| Xarpus | Kuji OSRS | [OSRS Quick Guide - How to Xarpus Scythe Walk THE EASY WAY](https://www.youtube.com/watch?v=NoOtA-adhEg) `yt_NoOtA-adhEg.md` | 1:32 | 60 |  | 0:00 Intro; 0:14 Guide; 1:19 Outro | none |
| Xarpus (solo) | myaahhh_osrs | [SOLO XARPUS FOR NOOBS (explained by a noob)](https://www.youtube.com/watch?v=PWfCL1ECiWM) `yt_PWfCL1ECiWM.md` | 5:49 | 60 | stated: metronome | 0:00 Introduction and goals; 0:54 Preparation and plugins; 2:30 Executing the rhythm; 3:50 Troubleshooting and recovery; 5:08 Conclusion and final tips | 5:09 |

### 4a.1 Overlay and tick-talk notes for the 19 videos already held (§4)

From the already-converted transcripts only (nothing re-downloaded); same caveat.

| File | Overlay named in captions | Tick-talk timestamps |
|---|---|---|
| `yt_1ldGvUsOx2M.md` | (none named) | 0:00, 0:48, 1:44 |
| `yt_3lQjrLeuvHo.md` | (none named) | 0:00, 2:38 |
| `yt_3yAP8lsyBcE.md` | (none named) | 3:42, 4:16, 7:35, 11:52, 15:57, 19:23, 20:02, 21:57 |
| `yt_D1b4eWwnOHU.md` | metronome | 0:51, 2:24, 2:59, 3:30, 3:59, 4:30, 5:50, 6:21, 7:08, 7:36 |
| `yt_G9jx6OUnaws.md` | (none named) | none |
| `yt_JdtL9UI5uy0.md` | (none named) | none |
| `yt_KF9y2GYTJ-A.md` | (none named) | 11:47, 12:17, 15:45, 26:40, 27:12, 28:14 |
| `yt_KiaFwopnnEI.md` | (none named) | 0:34 |
| `yt_Lt-iZwJUKmc.md` | (none named) | none |
| `yt_VEqiIF9EbcM.md` | (none named) | 6:42 |
| `yt_VU4WQ1ghn4E.md` | (none named) | 3:59, 12:42, 19:49 |
| `yt__QXdNAZh7Yo.md` | (none named) | 14:07 |
| `yt_eswoo8D364c.md` | (none named) | 0:00 |
| `yt_fPpIRjQWtlE.md` | attack timer, metronome | 0:06, 1:42, 2:47, 4:51, 5:01, 8:36, 9:48, 12:55 |
| `yt_oGPT3sZMnd8.md` | tick timer | 4:39, 6:18, 6:48, 9:42, 10:45, 11:28, 12:00, 12:31, 13:25, 15:06 |
| `yt_oKXoj9Yxy7Q.md` | (none named) | 3:34, 6:19 |
| `yt_uQzR4iIuv6s.md` | (none named) | 0:01, 0:42, 2:25, 3:02, 3:48 |
| `yt_yAi5A52J32E.md` | (none named) | 4:09 |
| `yt_yNZZQNAdQAM.md` | (none named) | 1:12, 5:36, 10:00, 13:15, 14:52, 17:46, 20:04, 23:22, 29:12, 32:35 |

### 4a.2 Per-room coverage after this pass (two guide videos by different players, minimum)

| Room | Players |
|---|---|
| Maiden | Indarkment, The Academy, QCS OSRS (perfect solo) |
| Bloat | BillNylo, Horselord, The Academy, Plank2g, SuppCarriesU |
| Nylocas | Deflne Alive, The Academy, Indarkment, Plank2g (HM), S2L OSRS |
| Sotetseg | cBold, Evse, Plank2g, Horselord, Taran |
| Xarpus | Crusher, Plank2g, Lone Gym Rat, Okirra, myaahhh_osrs, Kuji OSRS |
| Verzik P2 | Rob, Plank2g, RS Mina |
| Verzik P3 | S2L OSRS, 07samsquanches, Granddad Jad, Plank2g, Youthful, Fill2 |
| Entry mode (whole raid) | NoBS OSRS (July 2026), 10Boot OSRS (x2), Cudabear, Help Me RNG, RuneWraith |
| Pathfinding / scan rule | PurpleGod (`yt_DX8lN3r3ALw.md`) — bears on the T-1 scan in ENCOUNTER_TIMING.md section 1 |

Could not fetch: none after retries. Not downloaded on purpose: 15-60 s clips of unknown provenance (Killegend, maybe jessi, Tiger with the Bars) and the xzact one-minute guides.

## 4b. Added 2026-10-02 (spec-pass corpus) — other folders

Every fetch is recorded, with date, url and revision or commit, in [`../SOURCES.md`](../SOURCES.md).

| Folder / file | What it is |
|---|---|
| `newsposts/` | 77 raw `Update:` pages (Jagex newsposts as the wiki keeps them), pinned in `newsposts/manifest.tsv` (title, revid, date, file). The figures are indexed in `build/spec_state/<pass>/corpus.newsposts.md`. |
| `manifest.tsv` | revids of the wiki pages added on 2026-10-02 (the 48 Combat Achievement pages, `Theatre of Blood/Story Mode`, `Verzik Vitur - Patient Record`, `Verzik's Defeat`, `Monumental chest`, `Tobias`, ...). Pages held since 17 Aug 2026 keep their pins in §1 and were not re-fetched. |
| `wiki_combat_achievements_tob.tsv` | id, name, tier, monster, type, description of every ToB Combat Achievement (46), built from the pinned pages. |
| `blert_repo/`, `blert_guides/tob_*` | blert-io/blert at commit 7c7750cf, rules and constants plus every ToB guide page (see `blert_repo/README.md`). |
| `advancedraidtracker/`, `party_hits/`, `nylo_death_indicators/`, `nylo_stats/`, `xarpus_exhumed_counter/`, `theatreofbloodstats/` | RuneLite hub plugin mechanics files (see `PLUGIN_HUB_README.md` for repo, commits and what each encodes). |
| `wdr/` | We Do Raids public pages; they contain no mechanics (guides are Discord-only). |

## 5. Sources deliberately not used

- **Fandom mirrors** of the OSRS Wiki (`oldschoolrunescape.fandom.com`) — stale
  forks of the pages already pinned above.
- **RSPS wikis** (Alora, RuneRealm, Roat Pkz, Ascension, Simplicity, PkHonor) —
  they document *their own* re-implementations, which is exactly the failure mode
  this plan is trying to avoid.
- **Aggregator blogs** (VirtGold, osrsguru, tonsofxp, osrsbestinslot) — all of them
  paraphrase the Wiki, so they add citation noise and no information.
