# RuneLite plugin-hub plugins for Theatre of Blood (cloned 2026-10-02)

Hub manifest read from https://github.com/runelite/plugin-hub (`plugins/` tree, master, via the GitHub trees API, 2833 entries; the ToB-related names were matched by regex and each descriptor fetched raw). Each repo was shallow-cloned (depth 1) into `build/corpus_tmp/hub/`; ONLY files that encode mechanics were copied (tick constants, npc/attack ids, hp tables, phase rules, hazard timers). Licences stay in the file headers (BSD-2 for the plugins listed). Pre-existing copies (`tobmistaketracker/`, `tobqol/`, `blert_plugin/`, `openosrs_theatre/`, `tobutilities/`, `vtob/`) were not re-fetched. "Hub commit" is what the hub descriptor pins; "cloned" is the HEAD actually copied (equal unless noted).

| Directory | Repo | Hub commit | Cloned commit | Files copied | What it encodes |
|---|---|---|---|---|---|
| `advancedraidtracker/` | capslock13/AdvancedRaidTracker | d66a7075d6d2b7d3d21f82dad6de40f73a76c7fc | 3cbedc997f13db43c886755fb5eeb6306fe9223a (newer than hub) | rooms/tob/{Bloat,Maiden,Nylo,Sotetseg,Verzik,Xarpus}Handler.java, constants/TobIDs.java | per-room tick tracking: attack ticks, phase transitions, npc ids; capslock13 is also Blert's author |
| `party_hits/` | JaccodR/party-hits (hub id `tob-predicted-hit`) | 634af80ed3c3f80cab843619c4f04f5fe1dbb9d3 | same | bosses/{Boss,Maiden,Verzik}Handler.java, npcs/ToBNPC{,s}.java, XpToDamage.java | hit prediction: npc ids, hp, defence per ToB npc |
| `nylo_death_indicators/` | InfernoStats/Nylo-Death-Indicators | 1b29a76e1799cf813299bfdc75e12251028643ae | same | NylocasType, NylocasHealth, NyloDeathIndicatorsPlugin | nylocas hp by id and the hit-lands-next-tick rule |
| `nylo_stats/` | JaccodR/nylo-stats | 8ffba1e26c4e2cf96756c246655f5e03b2f062ef | 5412bad8139777ae17d77f7dc5b3908675197614 (newer than hub) | NyloStatsPlugin, Nylospawns, StallDisplays | wave spawn tiles, stall ticks |
| `xarpus_exhumed_counter/` | Dan-E-Git/xarpus-exhumed-counter | e034f731f143c48d4b090ebe771cd028dc5b68e0 | same | XarpusExhumedCounterPlugin | exhumed counts per party size, heal rule |
| `theatreofbloodstats/` | HarrySJ96/theatreofbloodstats | 22453e7de01ca489a60f0e72d18f6cce2a35d28b | same | rooms/*.java | room and phase start/end detection |

Considered and not copied (no mechanics beyond ids or message strings): Loze-Put/tob-hm-timer, aronson/runelite-external-plugins tob-damage-counter (phase id sets only), winterdaze/tob-time-infoboxes, KingsBo/tob-health-bars-enhanced, jlee513/tob-notification, EIKOOT/nyloer (QoL role swapper), levex/furry-nylos (Blood Moon Rises content, not ToB). Not cloned: tob-gear-checker, tob-light-up, tobdeathsound, sote-wall-remover, verzik-camera-fix, tob-recruitment-helper, tob-notice-board, tob-drop-chance, tob-chest-roulette (cosmetic or loot). `learner-tob` is disabled upstream.

Not applicable to this raid: the ToA (tombs-of-amascut, toa-mistake-tracker, ...) and CoX (cox-additions, cox-assistant, ...) hub plugins; those belong to the other raids' corpus workers. `sources/runelite/CoxPlugin.java` is not cited by the ToB plan.
