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

### `vtob/` — [Vainiven/V-TOB](https://github.com/Vainiven/V-TOB)

A ToB bot script. Its value here is purely geometric: `PestilentBloat.java` names
the Bloat room's four quadrant areas, the four pillar-hug tiles and the barrier
tile as world coordinates.

## 4. Video guides (machine transcripts)

`transcripts/` holds four YouTube guides downloaded with
`yt-dlp --write-auto-subs`, deduped and chaptered by
`vtt_to_md.py`. These are the least reliable sources in the folder — auto-captions
mangle numbers ("sod egg" is Sotetseg, "zarus" is Xarpus) — and are cited only where
they corroborate something already established.

| File | Video | Length |
|---|---|---|
| `yt_yNZZQNAdQAM.md` | S2L OSRS, *Max-Efficiency Theatre of Blood 4s Guide OSRS 2025 updated* — <https://www.youtube.com/watch?v=yNZZQNAdQAM> | 57:34 |
| `yt_KF9y2GYTJ-A.md` | Patyfatycake, *The Ultimate Beginners Guide To The Theatre Of Blood [OSRS]* — <https://www.youtube.com/watch?v=KF9y2GYTJ-A> | 31:09 |
| `yt_VU4WQ1ghn4E.md` | Chriskies, *OSRS - Solo ToB guide (Theatre of Blood)* — <https://www.youtube.com/watch?v=VU4WQ1ghn4E> | 27:09 |
| `yt_G9jx6OUnaws.md` | Beleti, *Hard Mode Theatre of Blood (Mage POV)* — <https://www.youtube.com/watch?v=G9jx6OUnaws> | 26:23 |

## 5. Sources deliberately not used

- **Fandom mirrors** of the OSRS Wiki (`oldschoolrunescape.fandom.com`) — stale
  forks of the pages already pinned above.
- **RSPS wikis** (Alora, RuneRealm, Roat Pkz, Ascension, Simplicity, PkHonor) —
  they document *their own* re-implementations, which is exactly the failure mode
  this plan is trying to avoid.
- **Aggregator blogs** (VirtGold, osrsguru, tonsofxp, osrsbestinslot) — all of them
  paraphrase the Wiki, so they add citation noise and no information.
