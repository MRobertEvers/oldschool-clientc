# LEDGER_code: plugins, simulators and reference servers for the Inferno

Corpus part `code` of the waves loop, pass `matthew-mbp-m4-waves-b1-spec-inferno`. Fetched 2026-10-03 by one worker,
from GitHub only (hosts `api.github.com`, `github.com` clones over https, `raw.githubusercontent.com`), one request at a
time with at least a second between requests; no wiki, no blert.io, no Blert repository. Clones and downloads sit under
`build/corpus_tmp/` (not in git). The pinned copies are under `docs/minigames/inferno/sources/<dir>/` with their
licence files; each directory has a `README.md` (repo, commit, files, what the source credits, constants with file and
line). The one table of every stated constant is [`CODE_CONSTANTS.md`](CODE_CONSTANTS.md); section 5 below repeats each
row with the quoted source lines so a spec worker need not open the files.

Standing reading of a code source: a RuneLite plugin is a **client-side observer** (what a client sees, timed in ticks:
what the real server does, as read by a player); InfernoTrainer, osrs-sdk and AUTOZUK are **simulators** built from
observed data and the wiki, not Jagex code; the reference servers are **ids and shapes only**.

## 1. Fetches

| date | url | revision | what | for |
|---|---|---|---|---|
| 2026-10-03 | https://api.github.com/repos/runelite/plugin-hub/git/trees/master:plugins | tree of master (commit 9f14e7daebee0e8b03c4d522eccba92476bc4766 at the fetch) | the 2,838 plugin names (`build/corpus_tmp/hub_names.txt`) | finding every hub plugin with inferno, zuk, jad, set, tz in its name |
| 2026-10-03 | https://api.github.com/repos/runelite/plugin-hub/contents/plugins/<name> for inferno-2d-map, inferno-autosplitter, inferno-blob-audio, inferno-scouter, inferno-splits-logger, inferno-stats, inferno-tracker, inferno-wave-splits, infernalfc, set-timer, tzhaar-hp-tracker, fight-cave-helper, fight-cave-waves, npc-attack-tick-timer | same | the `repository=` and `commit=` pin of each | which repo and commit the hub ships; copy `sources/plugin_hub_manifest_inferno.txt` |
| 2026-10-03 | https://api.github.com/repos/runelite/runelite/contents/runelite-client/src/main/java/net/runelite/client/plugins | master | listing | RuneLite core has **no** inferno plugin (empty match for infern/fight/tz): the plugin the community uses lives in the hub and in OpenOSRS descendants |
| 2026-10-03 | https://api.github.com/search/code?q=InfernoNPC filename:InfernoNPC.java and InfernoPlugin.java (two requests, 3 s apart) | n/a | 22 repositories carrying copies of the OpenOSRS Inferno plugin (OreoCupcakes/kotori-plugins, lucid-plugins/SideloadPlugins, karankurbur/OpenOSRSPlugins, Dirro/osrs-plugins, jky-dev/b2slite, Dabalon/MeteorLite, several Kronos client trees, ...) | choosing the OpenOSRS-lineage copies to pin |
| 2026-10-03 | https://api.github.com/search/repositories (queries: InfernoTrainer, inferno runelite plugin, zuk runelite, openosrs plugins, zuk osrs, zuksharp, inferno osrs simulator, tzkal-zuk, jal-zek resurrect, osrs inferno solver; 3 s apart) | n/a | candidate repositories | finding the simulators and the set-timer repositories |
| 2026-10-03 | https://github.com/OreoCupcakes/kotori-plugins (clone --depth 1) | 9ea4866e0fe1fb96ab07fcce3211f151441d4053 | `inferno/` | the maintained OpenOSRS-lineage Inferno plugin: attack ticks, wave table, shield, set timer |
| 2026-10-03 | https://github.com/karankurbur/OpenOSRSPlugins (clone --depth 1) | 904639cfdeaef33e724c872118e005e8967eadfd | `inferno/` | the OpenOSRS Inferno plugin (2020); the older lineage member |
| 2026-10-03 | https://github.com/Dirro/osrs-plugins (clone --depth 1) | b13d786e205b02f6e0a1f5eb6069405fa6277e41 | `inferno/` | examined only (older, 2020-05-01; differs from karankurbur in the same files); not copied |
| 2026-10-03 | https://github.com/open-osrs/plugins (clone --depth 1) | 9e680b5e220e33bc74a368d1f735fee49a22ca06 | none: the repository no longer carries an `inferno` plugin | confirms the official OpenOSRS repository dropped it |
| 2026-10-03 | https://github.com/lucid-plugins/SideloadPlugins (clone --depth 1) | 027df8dcc643a5091e772b2de5f0dafd32df2fe8 | `src/main/java/com/lucidplugins/inferno/` | listed, not pinned: same file set and class names as kotori (a copy of the lineage, 2024-10-22); would add no independent row |
| 2026-10-03 | https://github.com/OldSchoolSDK/InfernoTrainer (clone --depth 1) | 06fc103f70f1fa228678ca79910a8d3bb0798a7d | `src/content/inferno/js/**`, `test/simulations/ZukLineOfSight.test.ts` | the Inferno trainer (infernotrainer.com) |
| 2026-10-03 | https://github.com/OldSchoolSDK/osrs-sdk (clone, then `--unshallow` for the file history) | 04fdaee3d155238e54cf16c1ac259f6c2b210078 (version bump to 0.1.4: 9b80ab4) | `src/sdk/**` | the engine under the trainer: line of sight, movement, hit delay, max hit; history used only to show the copied files equal the 0.1.4 the trainer pins |
| 2026-10-03 | https://github.com/jeremiah855/AUTOZUK (clone --depth 1) | 346dece30982a2c5ee60b702721d91ba17b4b8da | `index.html` | the AUTOZUK wave solver (sim core) |
| 2026-10-03 | https://github.com/propagating/ZukPrayer (clone --depth 1) | 1743f1d806f62ade70748549d3299d83096376e9 | README, `LoginTicks.java`, `ZukPrayerPlugin.java` read | the since-login tick counter and the "wave spawns on tick 15" claim; no licence: only the README is copied |
| 2026-10-03 | https://api.github.com/repos/propagating/zuksharp | n/a | HTTP 404 | the repository named in the ZukPrayer README is not public / does not exist under that name: **unavailable** |
| 2026-10-03 | https://github.com/jeremiah855/inferno-scouter (clone --depth 1) | ff025377ea34d01c30d3bace220b9b14e359cfcb (= hub pin) | `InfernoScouterPlugin.java`, README | spawn tiles, ids, pillar ids |
| 2026-10-03 | https://github.com/TheRealGuru/inferno-2d-map (clone, then fetch of the hub pin) | 463b31a2989d49d5b310f39fe7f3d01c70999db4 (= hub pin; repo head 52503f8) | `Inferno2dMapPlugin.java`, `Coordinate.java` | grid, ids, coordinate frames |
| 2026-10-03 | https://github.com/InfernoStats/InfernoStats (clone --depth 1) | 0efd7441d0a33486bbf2f13fd54cea846f426ee8 (= hub pin) | `InfernoStatsPlugin.java`, `model/InfernoNpc.java`, `controller/ChatHandler.java` | region ids, footprint sizes, wave message |
| 2026-10-03 | https://github.com/InfernoStats/SetTimer (clone --depth 1) | 45a47eb32763087006459e43e865975fdace68fe (= hub pin) | `SetTimer.java`, `SetTimerPlugin.java` | the Zuk set timer (the hub's own) |
| 2026-10-03 | https://github.com/maxswa/inferno-timer (clone --depth 1) | e36b1ca99f2522d74c37694ffa9c2ff6e85fc02a | `src/App.tsx`, `src/utils.ts` read | a second set timer |
| 2026-10-03 | https://github.com/bradyp30/Zuk-Timer (clone --depth 1) | 1dec8d9d8f357b7ed0ab6035c2fa3745d6b1eec1 | `timer.js` read, not copied (no licence) | a third set timer, anchored on the cutscene |
| 2026-10-03 | https://github.com/davidsaad-git/triple-jad-sim (clone --depth 1) | b6c8d95386bec11de3f7acddc5cf714b1b4ff904 | `docs/INFERNO_DATA.md`, `docs/RESEARCH.md`, README, NOTICE.md | a compilation of the wiki, the OpenOSRS plugin, InfernoStats, scouter, runemarkers and the trainer, with its own unresolved list; a map of claims, not a source |
| 2026-10-03 | https://github.com/jamiegyoung/runemarkers (clone --depth 1) | e7e618564bfc29a166db78efad56f20ff98ee1ac | `entities/inferno.json` | the WeDoRaids Zuk tile set |
| 2026-10-03 | https://github.com/Deagsly/Blobs, evaan/InfernoTracker, molgoatkirby/InfernoAutoSplitter, usa-usa-usa-usa/inferno-wave-splits (clone --depth 1; commits 257d7026, 85c8f5ca, f7c351b9, 5e026535) | n/a | file lists and sizes only | the hub's other Inferno plugins: audio meme, splits, tracker. They state no mechanic beyond npc ids 7693/7694-7696 (blob audio) and the chat line; not copied |
| 2026-10-03 | https://raw.githubusercontent.com/runelite/runelite/d8e7d1e5f34e2899eda3d7cf4cd9661ae2206f22/runelite-api/src/main/java/net/runelite/api/gameval/NpcID.java (blob sha 05827b4a5e6d8acb7454dfb19d4070c0f298ad57, 1,300,343 bytes) | d8e7d1e5 (master 2026-09-30) | the Inferno rows | numeric ids for the gameval names kotori uses; copy `sources/runelite_gameval_NpcID_inferno_excerpt.txt` (an excerpt with its line numbers) |

Not fetched on purpose: the wiki, blert.io and the Blert repositories (other workers), detuks.com (not GitHub), YouTube.
Plugin-hub plugins read by name only and not opened: `infernalfc` (disabled as unmaintained; warns it submits world, region
and IP to a third-party server), `inferno-splits-logger` (disabled: unmaintained), `npc-attack-tick-timer` (disabled),
`tzhaar-hp-tracker` (bopsec/buchus-plugins, 464672cf: a hitpoint tracker, not opened), `fight-cave-helper`,
`fight-cave-waves` (the Fight Caves are out of scope). No hub plugin carries zuk or jad in its name: the Zuk shield and
set timers are inside the Inferno plugin (kotori / OpenOSRS) and the two timer plugins above.

## 2. Copies into `docs/minigames/inferno/sources/`

| directory | from | files | licence | README |
|---|---|---|---|---|
| `kotori_inferno` | OreoCupcakes/kotori-plugins 9ea4866 | InfernoNPC, InfernoPlugin, InfernoWaveMappings, InfernoSpawnTimerInfobox, InfernoBlobDeathSpot, displaymodes/InfernoZukShieldDisplayMode | BSD-2 | `kotori_inferno/README.md` |
| `openosrs_inferno` | karankurbur/OpenOSRSPlugins 904639c | InfernoNPC, InfernoPlugin, InfernoWaveMappings, InfernoSpawnTimerInfobox | GPL-3.0 repo, BSD-2 file headers | `openosrs_inferno/README.md` |
| `infernotrainer` | OldSchoolSDK/InfernoTrainer 06fc103 | InfernoWaves, InfernoRegion, InfernoPillar, ZukShield, InfernoMobDeathStore, InfernoHealerSpark, Wall, 13 mob classes, ZukLineOfSight.test.ts, README, sidebar.html | GPL-3.0 | `infernotrainer/README.md` |
| `osrs_sdk` | OldSchoolSDK/osrs-sdk 04fdaee | LineOfSight, Collision, Pathing, Mob, Unit, MagicWeapon, RangedWeapon, MeleeWeapon, gear/Weapon | GPL-3.0 | `osrs_sdk/README.md` |
| `autozuk` | jeremiah855/AUTOZUK 346dece | index.html | MIT | `autozuk/README.md` |
| `inferno_scouter` | jeremiah855/inferno-scouter ff02537 | InfernoScouterPlugin.java, README | BSD-2 | `inferno_scouter/README.md` |
| `inferno_2d_map` | TheRealGuru/inferno-2d-map 463b31a | Inferno2dMapPlugin.java, Coordinate.java | BSD-2 | `inferno_2d_map/README.md` |
| `infernostats` | InfernoStats/InfernoStats 0efd744 | InfernoStatsPlugin.java, InfernoNpc.java, ChatHandler.java | BSD-2 | `infernostats/README.md` |
| `settimer` | InfernoStats/SetTimer 45a47eb | SetTimer.java, SetTimerPlugin.java, README | BSD-2 | `settimer/README.md` |
| `inferno_timer_maxswa` | maxswa/inferno-timer e36b1ca | App.tsx, README | none stated | `inferno_timer_maxswa/README.md` |
| `triple_jad_sim` | davidsaad-git/triple-jad-sim b6c8d95 | docs/INFERNO_DATA.md, docs/RESEARCH.md, README, NOTICE.md | see NOTICE.md | `triple_jad_sim/README.md` |
| `runemarkers_inferno` | jamiegyoung/runemarkers e7e6185 | entities/inferno.json | MIT | `runemarkers_inferno/README.md` |
| `zukprayer` | propagating/ZukPrayer 1743f1d | README only | none stated: source not copied | `zukprayer/README.md` |
| `(file) runelite_gameval_NpcID_inferno_excerpt.txt` | runelite/runelite d8e7d1e | 37 rows of NpcID.java | BSD-2 |  |
| `(file) plugin_hub_manifest_inferno.txt` | runelite/plugin-hub | the hub entries of the Inferno plugins | n/a |  |

## 3. Reference servers (Kronos, LostCity): what the tree already took, ids and shapes only

No server code was fetched. The Kronos tree the content was ported from is on the owner's disk at
`/Users/matthewevers/Documents/git_repos/Kronos184-Fixed_2` (`docs/KRONOS_CONTENT_PORT_QUEUE.md` line 7 of the main
checkout names it: "behaviour / id reference ... When Kronos and the osrs239 cache disagree, the cache wins ... Kronos wins
only for policy the cache does not state"); the Inferno path quoted by `docs/INFERNO_SOUNDS.md:78` is
`Kronos-master/kronos-server/src/main/java/io/ruin/model/activities/inferno/` (`Inferno.java` and eleven monster classes) and
`docs/BOSS_ASSETS.md:513` names `kronos-server/data/npcs/combat/<Name>.json` for stats. The shape reference for scripts is
`LostCity_Content2` `area_inferno` (named in the content file headers; LostCity itself stops at 2004 and has no Inferno,
`inferno.npc:110-111`). **A server's balance number is not a source**; the tree's own comments already say so
(`inferno.constant` "Where Kronos and the wiki differ, the wiki is what this tree follows").

What the tree took, quoted from the content it ports into
(`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/`, every line that names Kronos):

```
configs/inferno.constant:3:// (Kronos TzKalZuk), remapped to osrs239 pack names. Waves/pillars: Kronos
configs/inferno.constant:37:// Pillars (Kronos west/south/east).
configs/inferno.constant:45:// Combat spawn tiles (Kronos ROTATIONS), local.
configs/inferno.constant:76:// Zuk fight (Kronos size-7 coords; cache Zuk is size 7).
configs/inferno.constant:95:// Kronos spawnZuk teleports here on plane 1 so LOC_* packets (player-plane only)
configs/inferno.constant:119:// Kronos forObj(30356) at (2267, 5368) plane 1 — multiloc roof marker.
configs/inferno.constant:193:// Kronos Inferno.spawnZuk does the same either side of its object work
configs/inferno.constant:238:// Kronos's `max_damage` of 251.
configs/inferno.constant:246:// The other five inferno max hits below were cited as matching Kronos, and they
configs/inferno.constant:249:// Kronos over the wiki on the one value where the two disagree.
configs/inferno.constant:317:// Kronos TzKalZuk.startAddsSpawns:
configs/inferno.constant:338:// Kronos hands each spawned add an attack clock that is already part-spent:
configs/inferno.constant:349:// Kronos's number is left here as the record of what was dropped. The Jad's 9
configs/inferno.constant:424:// Soft gaps — Kronos JalZek revive + wiki Jal-nib-rek 1/100.
configs/inferno.varp:3:// Permanent: pause/resume across logout (Kronos allowLogout / prepareWave(login)).
configs/inferno.varp:15:// (the black flat rectangle above Zuk). Kronos removes obj 30356 instead.
configs/inferno.varp:173:// Survives logout so re-entry can resume (Kronos keeps Inferno on the player).
scripts/inferno_zek.rs2:1:// Jal-Zek corpse revive — Kronos JalZek.java.
configs/inferno.npc:2:// LostCity_Content2 area_inferno/configs/inferno.npc; Kronos TzKalZuk shield.
configs/inferno.npc:44:// Kronos Inferno.java:600-601 sets these two on the glyph's def directly.
configs/inferno.npc:94:// Kronos TzKal-Zuk.json 7566/7565/7562.
configs/inferno.npc:125://   k   A Kronos source line plays it (`kronos-server/.../inferno/monsters/`).
configs/inferno.npc:172:// **The ids are Kronos's, not this cache's seq names.** Kronos states them per
configs/inferno.npc:173:// npc in `kronos-server/data/npcs/combat/<Name>.json`
configs/inferno.npc:182://   Jal-Xil / Jal-Zek `attack_animation` is the MELEE seq — Kronos names the
configs/inferno.npc:187:// docs/BOSS_ASSETS.md agrees with Kronos on all six; the seq names do not.
configs/inferno.npc:193:// — not from the cache record, not from Kronos — so `general/configs/
configs/inferno.npc:198://     further down said the rest "agree to the number" with Kronos, and they
configs/inferno.npc:219:// against Kronos: **Jal-Xil is 125, not 130, and Jal-MejJak is 75, not 80.**
configs/inferno.npc:220:// Where Kronos and the wiki differ, the wiki is what this tree follows.
configs/inferno.npc:286:// Jal-Xil (final-wave ranger) — Kronos Jal-Xil.json 7604/7607/7606.
configs/inferno.npc:313:// Jal-Zek (final-wave mager) — Kronos Jal-Zek.json 7612/-1/7613. Kronos states
configs/inferno.npc:357:// JalTok-Jad — Kronos JalTok-Jad.json 7590/7591/7594.
configs/inferno.npc:388:// `firewave_hit` and is played by inferno_jad.rs2 — Kronos does exactly that
configs/inferno.npc:396:// Yt-HurKot (Jad's healers) — Kronos Yt-HurKot.json 2637/2635/2638.
configs/inferno.npc:434:// Jal-MejJak (Zuk's healers) — Kronos Jal-MejJak.json 2868/2863/2865. The two
configs/inferno.npc:770:// and neither the wiki's Notes column nor Kronos mentions it. Every layer
scripts/inferno_jad.rs2:84:// Kronos runs the SAME `attack()` whether the target is the shield or a person
scripts/inferno_jad.rs2:197:// The ranged attack at an npc. No projectile, and that is the reference: Kronos
scripts/inferno_jad.rs2:212:// Kronos plays 163 `firewave_hit` on the player in the `postDamage` of BOTH
scripts/inferno_jad.rs2:219:// `privateSound` is per-player in Kronos too, which is why this is
scripts/inferno_zuk.rs2:29:// Kronos removes the loc on level 1; the cache multiloc is the portable path.
scripts/inferno_zuk.rs2:169:// so clientscript 735/739 mounts with real HP (Kronos TARGET_OVERLAY_CUR/MAX).
scripts/inferno_zuk.rs2:203:// — Kronos's SECONDARY_OVERLAY ("fading, castle wars game, snow falling"),
scripts/inferno_zuk.rs2:340:// Kronos spawns 30342 at (2267,5366) — map square has no standing loc there, so
scripts/inferno_zuk.rs2:357:// Kronos adds these on plane 1 (which is what the plane-1 teleport in
scripts/inferno_zuk.rs2:459:            // Kronos TzKalZuk.attack(): `animate(getDefendAnimation(), delay - 25)`.
scripts/inferno_zuk.rs2:533:// Kronos's own test, re-read every tick rather than latched once — see the
scripts/inferno_zuk.rs2:601:        // **Jad does not spend an opening wait either.** Kronos states 9 for
scripts/inferno_zuk.rs2:915://   1  flanks on plane 1 instead — the arrangement inherited from Kronos, and
scripts/inferno_waves.rs2:2:// Kronos Inferno.beginWave + wiki nibbler fillers (6 on 3/8/17/34).
scripts/inferno_pillars.rs2:2:// Kronos Pillar + wiki 255 HP. Damage → %inferno_safespotN_health varbits.
scripts/inferno.rs2:2:// Kronos Inferno.join / leave / allowLogout. LostCity: none.
scripts/inferno.rs2:187:// Pause at end of wave (Kronos allowLogout / logoutRequest). Engine has no
scripts/inferno.rs2:384:// Kronos's `Inferno.allowLogout` is a *request*: a player who wants to stop
scripts/inferno_ai.rs2:1:// Inferno monster AI — Kronos Jal-* behaviours, no huntmode / no varn.
scripts/inferno_adds.rs2:22:// letting it hand out the target later. Kronos's `updateLastAttack(10)` is
scripts/inferno_adds.rs2:525:// The TRAJECTORY is Kronos's, because numbers port across revisions where ids
scripts/inferno_adds.rs2:530:// The splash, on the same 75 Kronos's `World.sendGraphics(659, 0, 75, pos)` uses.
```

Read as a list of shapes and ids taken:

* **Shape** of the encounter: `Inferno.java` join / leave / `allowLogout` / `beginWave` (inferno.rs2:2, :187, :384;
  inferno_waves.rs2:2), the pause-between-waves rule, nibbler fillers from the wiki (6 on 3 / 8 / 17 / 34), the
  `TzKalZuk` shield (inferno.npc:2), the `startAddsSpawns` HP band (`inferno.constant` quotes "if (npc.getHp() >= 479 &&
  npc.getHp() <= 599) event.delay(1)"), the opening waits Kronos states (`updateLastAttack(10)` for the mager and ranger,
  `(9)` for Jad: kept in the constants file and **deliberately not spent**, inferno_adds.rs2:22, inferno_zuk.rs2:601),
  `JalZek.java` corpse revive (inferno_zek.rs2:1), the Jad style split (`JalTokJad.java:106`, inferno_jad.rs2:84-90).
* **Ids** taken from Kronos: the per-npc attack / defend / death animation ids from `kronos-server/data/npcs/combat/<Name>.json`
  (Zuk 7566 / 7565 / 7562, Jal-Xil 7604 / 7607 / 7606, Jal-Zek 7612 / -1 / 7613, JalTok-Jad 7590 / 7591 / 7594,
  Yt-HurKot 2637 / 2635 / 2638 and Jal-MejJak's pair, inferno.npc:94, 286, 313, 357, 396), the glyph's pair set in code at
  `Inferno.java:600-601` (inferno.npc:44), `JalXil.java:37` (7605) and `JalZek.java:60` (7611, the resurrect), Jad's
  `privateSound(163)` at `JalTokJad.java:142` and `:161` (INFERNO_SOUNDS.md section 3), `World.sendGraphics(659, 0, 75, pos)`
  and `new Projectile(660, 0, 0, 0, 75, 0, 45, 32)` (flight numbers only, the graphic ids are rev-184: inferno_adds.rs2:525-530),
  the Zuk shot's `getDefendAnimation(), delay - 25` (inferno_zuk.rs2:459), loc 30356 / 30342 at (2267,5366)
  (inferno.varp:15, inferno_zuk.rs2:340).
* **Numbers the tree took and later overrode from the wiki** (so they are NOT Kronos's any more; both the tree and
  `docs/BOSS_ASSETS.md:513-531` say so): Jal-Xil hitpoints 125 (Kronos 130), Jal-MejJak 75 (Kronos 80), Zuk max hit 148
  (Kronos `max_damage` 251; the trainer's `magicMaxHit` is also 251, C072), Yt-HurKot heal 15-24 (the tree quotes a wiki
  note by Mod Ash; the trainer's code heals 0..19, C065).
* **Constants the tree states that differ from the pinned code** (a comparison for the spec workers, not a verdict;
  the tree's file is `configs/inferno.constant`): `^inferno_wave_delay = 8` (line 11) against the trainer's 9 (C018);
  `^inferno_add_wave_first = 60` (line 312) against the trainer's 72 ticks (C075) and the Zuk-Timer's 51 s (C092);
  `^inferno_glyph_pause = 4` (line 257) against kotori's 4 and the trainer's freeze(5) (C082); `^inferno_spawn_0..8_lx/lz`
  (lines 46-63) are **not** the scouter's nine tiles in any simple frame (examples: tree spawn_5 (21,27) against scouter
  (22,23); spawn_0 (19,41) against (18,41); spawn_6 (20,20) against (18,18)): the nine in the tree are unresolved against
  the observation (C013); the pillar tiles (17,37) (27,23) (34,39) and the nibbler block (25,33) **do** equal the
  scouter-derived ones (C021, C015).

## 4. Unavailable or not pinned

* `propagating/zuksharp`: HTTP 404 (named in the ZukPrayer README; not public).
* The detuks.com AUTOZUK write-up and the YouTube AUTOZUK video: not GitHub, left to the guides worker (TJS cites the
  write-up for "monsters spawn on tick 15").
* Plugin-hub plugin sources not opened: `infernalfc`, `inferno-splits-logger`, `npc-attack-tick-timer`,
  `tzhaar-hp-tracker`, `inferno-autosplitter`, `inferno-tracker`, `inferno-wave-splits` (splits and trackers; the two read
  by name only state wave numbers and times, not mechanics).
* Blert, the wiki and the cache are other workers' corpora: the rows that need them say so in `CODE_CONSTANTS.md`.
* The Kronos and LostCity trees were not read again; the list in section 3 is what the tree's own comments record.

## 5. What this part states: every number, with file, line and the quoted line

(The line numbers are those of the pinned copies; see `CODE_CONSTANTS.md` for agree / disagree.)

### C001 arena: region id = 9043 (Inferno interior)

* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:79` : `private static final int INFERNO_REGION = 9043;`
* `openosrs_inferno/inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoPlugin.java:77` : `private static final int INFERNO_REGION = 9043;`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:58` : `private static final int INFERNO_REGION_ID = 9043;`
* `inferno_2d_map/src/main/java/com/github/therealguru/Inferno2dMapPlugin.java:34` : `private static final int INFERNO_REGION = 9043;`
* `infernostats/src/main/java/com/infernostats/InfernoStatsPlugin.java:45` : `private static final int INFERNO_REGION_ID = 9043;`

### C002 arena: playable grid = 29 x 30 tiles

* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:61` : `private static final int GRID_WIDTH = 29;`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:62` : `private static final int GRID_HEIGHT = 30;`
* `inferno_2d_map/src/main/java/com/github/therealguru/Inferno2dMapPlugin.java:50` : `public static final int GRID_WIDTH = 29;`
* `autozuk/index.html:369` : `const ARENA_X_MIN=1,ARENA_X_MAX=29,ARENA_Y_MIN=1,ARENA_Y_MAX=30;`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:370` : `this.addEntity(new InvisibleMovementBlocker(this, { x, y: 13 }));`

### C003 arena: scout-grid transform = x = regionX - 17; y = 46 - regionY (SW tile of a footprint)

* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:59` : `private static final int REGION_X_OFFSET = 17;`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:60` : `private static final int REGION_Y_OFFSET = 46;`

### C004 ids: npc ids Jal-Nib / Jal-MejRah / Jal-Ak / bloblets = 7691 / 7692 / 7693 / 7694 (mage) 7695 (range) 7696 (melee)

* `runelite_gameval_NpcID_inferno_excerpt.txt:10` : `35070:	public static final int INFERNO_NIBBLER = 7691;`
* `runelite_gameval_NpcID_inferno_excerpt.txt:13` : `35085:	public static final int INFERNO_CREATURE_SPLITTER_MAGE = 7694;`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:82` : `7692, // Jal-MejRah (bat)`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:83` : `7693, // Jal-Ak (blob)`

### C005 ids: npc ids Jal-ImKot / Jal-Xil / Jal-Zek (final-wave variant) = 7697 / 7698 (7702) / 7699 (7703)

* `runelite_gameval_NpcID_inferno_excerpt.txt:16` : `35100:	public static final int INFERNO_CREATURE_MELEE = 7697;`
* `runelite_gameval_NpcID_inferno_excerpt.txt:21` : `35125:	public static final int INFERNO_RANGER_FINALWAVE = 7702;`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:84` : `7697, // Jal-ImKot (melee)`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:86` : `7702, // Jal-Xil (alt)`

### C006 ids: npc ids JalTok-Jad / healer (final-wave variant) = 7700 / 7701 (7704 / 7705)

* `runelite_gameval_NpcID_inferno_excerpt.txt:19` : `35115:	public static final int INFERNO_JAD = 7700;`
* `runelite_gameval_NpcID_inferno_excerpt.txt:24` : `35140:	public static final int INFERNO_JAD_HEALER_FINALWAVE = 7705;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:400` : `HEALER_JAD(new int[]{NpcID.TZHAAR_FIGHTCAVE_SWARM_BOSS_CLERIC, NpcID.INFERNO_JAD_HEALER, NpcID.INFERNO_JAD_HEALER_FINALWAVE, NpcID.JAD_CHALLENGE_HEALER}, Attack.MELEE, 4, 1, 6),`

### C007 ids: npc ids TzKal-Zuk / shield (Ancestral Glyph) / Jal-MejJak / pillar placeholder / dying pillar = 7706 / 7707 (INFERNO_MOVING_SAFESPOT) / 7708 / 7709 / 7710

* `runelite_gameval_NpcID_inferno_excerpt.txt:25` : `35145:	public static final int INFERNO_TZKALZUK_PLACEHOLDER = 7706;`
* `runelite_gameval_NpcID_inferno_excerpt.txt:26` : `35150:	public static final int INFERNO_MOVING_SAFESPOT = 7707;`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:74` : `private static final Set<Integer> PILLAR_NPC_IDS = Set.of(7709, 7710);`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:330` : `if (npcId == NpcID.INFERNO_MOVING_SAFESPOT)`

### C008 ids: pillar loc ids = 30353, 30354, 30355

* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:73` : `private static final Set<Integer> PILLAR_OBJECT_IDS = Set.of(30353, 30354, 30355);`
* `inferno_2d_map/src/main/java/com/github/therealguru/Inferno2dMapPlugin.java:49` : `private static final List<Integer> PILLAR_IDS = List.of(30353, 30354, 30355);`

### C009 anim: animation ids the plugins key on = Nib attack 7574 (death 7576); bat attack 7578 (stand 7577); blob range 7581 melee 7582 magic 7583 (death 7584); melee 7597 (burrow 7600); ranger melee 7604 range 7605; mager mage 7610 melee 7612 (revive 7611); Jad mage 7592 range 7593; Zuk 7566

* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:173` : `public static final int JAL_NIB = 7574;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:174` : `public static final int JAL_MEJRAH = 7578;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:176` : `public static final int JAL_AK_RANGE_ATTACK = 7581;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:182` : `public static final int JAL_ZEK_MAGE_ATTACK = 7610;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:184` : `public static final int JALTOK_JAD_MAGE_ATTACK = 7592;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:186` : `public static final int TZKAL_ZUK = 7566;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:441` : `&& animationId == 7576)`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoBlobDeathSpot.java:12` : `static final int BLOB_DEATH_ANIMATION = 7584;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:288` : `else if (npcAnimationId == 7600)`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:293` : `else if (npcAnimationId == 7611)`

### C010 wave table: waves 1-66: monsters per wave (nibbler, bat, blob, melee, ranger, mager) = w1 3,1,0,0,0,0 ... w66 3,0,0,0,0,2; nibblers 3 except 6 on waves 3, 8, 17, 34

* `infernotrainer/src/content/inferno/js/InfernoWaves.ts:154` : `static waves = [`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:51` : `waveMapBuilder.put(1, new int[]{32, 32, 32, 85});`
* `openosrs_inferno/inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoWaveMappings.java:51` : `waveMapBuilder.put(1, new int[]{32, 32, 32, 85});`

### C011 wave table: waves 67 / 68 / 69 = 67: 1 JalTok-Jad; 68: 3 JalTok-Jad; 69: TzKal-Zuk

* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:117` : `waveMapBuilder.put(67, new int[]{900});`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:118` : `waveMapBuilder.put(68, new int[]{900, 900, 900});`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:119` : `waveMapBuilder.put(69, new int[]{1400});`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:442` : `} else if (this.wave === 67) {`

### C012 wave table: wave announcement chat line = game message "Wave: <n>"

* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:488` : `if (event.getMessage().contains("Wave:"))`
* `infernostats/src/main/java/com/infernostats/controller/ChatHandler.java:41` : `private static final Pattern TZHAAR_WAVE_MESSAGE = Pattern.compile("Wave: (\\d+)");`

### C013 spawn: candidate spawn tiles (region-local SW tile of the footprint) = (18,41) (39,41) (20,35) (40,34) (33,29) (22,23) (40,21) (18,18) (32,18); world = region + (2240, 5312) [derived]

* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:1091` : `pts.add(new P(18, 41));`
* `infernotrainer/src/content/inferno/js/InfernoWaves.ts:15` : `static spawns = [`
* `autozuk/index.html:371` : `const SPAWN_LOCATIONS=[{x:2,y:6},{x:23,y:6},{x:4,y:12},{x:24,y:13},{x:17,y:18},{x:6,y:24},{x:24,y:26},{x:2,y:29},{x:16,y:29}];`

### C014 spawn: rule assigning monsters to tiles = Fisher-Yates shuffle of the 9 tiles per wave; assigned in order mager(s), ranger(s), melee(s), blob(s), bat(s); nibblers separately

* `infernotrainer/src/content/inferno/js/InfernoWaves.ts:26` : `static shuffle(array) {`
* `infernotrainer/src/content/inferno/js/InfernoWaves.ts:57` : `mobs.push(new JalZek(region, spawnLocation, { aggro: player }));`

### C015 spawn: nibbler spawn block = 3x3 block region x 25..27, y 33..35; n tiles taken from a shuffle of 9

* `infernotrainer/src/content/inferno/js/InfernoWaves.ts:127` : `static spawnNibblers(n: number, region: Region, pillar: Entity) {`
* `autozuk/index.html:634` : `// Spawn 3 nibblers in the 3x3 box: gameX 19-21, gameY 25-27`

### C016 spawn: monsters spawn on tick 15 of the wave (counted since login) = 15

* `zukprayer/UPSTREAM_README.md:5` : `- the since-login tick counter (the wave spawns on tick 15 — same counter the AUTOZUK video`
* `autozuk/index.html:2799` : `setStatus('Sim started! ${sim.mobs.filter(m=>!m.dead).length} mobs spawned on tick 15.','info');updateUI();return 'created';`

### C017 spawn: spawn stun (ticks before a new monster acts) = 1 for nib, bat, blob, melee, ranger, mager, Yt-HurKot; Zuk 8; Jal-MejJak 1; Jad = constructor option (1, or 1/4/7 on wave 68)

* `infernotrainer/src/content/inferno/js/mobs/JalNib.ts:37` : `this.stunned = 1;`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:231` : `this.stunned = 8;`
* `infernotrainer/src/content/inferno/js/mobs/JalMejJak.ts:91` : `const SPAWN_DELAY = 1;`
* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:130` : `this.stunned = options.stun;`

### C018 spawn: next wave starts N ticks after the last monster dies = 9 ("1 extra tick to allow for bloblets")

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:687` : `this.waveCompleteTimer = 9;`

### C019 player start: player start tile (trainer frame) = waves 1-66 (28,17); wave 67 (18,25); wave 68 (25,27); wave 69 (25,15)

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:413` : `player.location = { x: 28, y: 17 };`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:446` : `player.location = { x: 18, y: 25 };`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:457` : `player.location = { x: 25, y: 27 };`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:485` : `player.location = { x: 25, y: 15 };`

### C020 pillar: hitpoints = 255

* `infernotrainer/src/content/inferno/js/InfernoPillar.ts:32` : `hitpoint: 255,`
* `autozuk/index.html:574` : `const PILLAR_MAX_HP=255;`

### C021 pillar: size and positions = 3x3; south (21,37), west (11,23), north (28,21) in the trainer frame = region SW tiles (27,23) (17,37) (34,39)

* `infernotrainer/src/content/inferno/js/InfernoPillar.ts:209` : `region.addEntity(new InfernoPillar(region, { x: 21, y: 37 }));`
* `autozuk/index.html:372` : `const PILLAR_LOCS={S:{x:11,y:24,size:3},W:{x:1,y:10,size:3},N:{x:18,y:8,size:3}};`
* `inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:70` : `private static final int PILLAR_SIZE = 3;`

### C022 pillar: pillars present = waves 1-66 only (none on 67-69)

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:360` : `if (this.wave < 67 || this.wave >= 70) {`

### C023 pillar: nibbler damage to a pillar = floor(random*5) = 0..4 per hit, one hit per attack speed (4)

* `infernotrainer/src/content/inferno/js/mobs/JalNib.ts:12` : `const damage = Math.floor(Random.get() * 5);`
* `autozuk/index.html:900` : `hlDamagePillar(pillarTarget,Math.floor(S.rng()*5),tick,S);mob.attackDelay=mob.atkSpeed;return;`

### C024 pillar: collapse effects = AUTOZUK: nibblers die, other monsters next to the pillar take floor(hp/2), the player next to it takes floor(current hp/2)

* `autozuk/index.html:770` : `let damage=mob.type==='nibbler'?mob.hp:Math.floor(mob.hp/2);`
* `autozuk/index.html:1174` : `let damage=Math.floor(Math.max(0,hp)/2);`
* `infernotrainer/src/content/inferno/js/InfernoPillar.ts:200` : `// TODO: needs to AOE the nibblers around it`

### C025 pillar: nibbler target rule = all of a wave's nibblers target one randomly chosen surviving pillar and ignore the player until none is left

* `infernotrainer/src/content/inferno/js/InfernoWaves.ts:144` : `const options: UnitOptions = { aggro: unknownPillar as Unit /* TODO: || world.player */ };`
* `autozuk/index.html:629` : `let target=available.length?available[Math.floor(rng()*available.length)]:null;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:303` : `calculateCentralNibbler();`

### C026 Jal-Nib: hitpoints / level / defence = 10 / 32 / 15

* `infernotrainer/src/content/inferno/js/mobs/JalNib.ts:52` : `hitpoint: 10,`
* `infernotrainer/src/content/inferno/js/mobs/JalNib.ts:33` : `return 32;`
* `infernotrainer/src/content/inferno/js/mobs/JalNib.ts:49` : `defence: 15,`
* `autozuk/index.html:1486` : `nibbler:{def:15,magic:15,atk:1,str:1,ranged:1,off:{crush:0},defensive:{stab:-20,slash:-20,crush:-20,magic:-20,light:-20,standard:-20,heavy:-20},max:4,style:'melee',meleeType:'crush'},`

### C027 Jal-Nib: attack speed / range / size / style = 4 / 1 / 1 / crush

* `infernotrainer/src/content/inferno/js/mobs/JalNib.ts:89` : `return 4;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:393` : `NIBBLER(new int[]{NpcID.INFERNO_NIBBLER}, Attack.MELEE, 4, 99, 100),`
* `autozuk/index.html:381` : `nibbler:{letter:'N',size:1,hp:10,atkSpeed:4,range:1,style:'melee',color:'#aaaaaa'},`

### C028 Jal-Nib: max hit vs player = 4 (damage 0..4)

* `infernotrainer/src/content/inferno/js/mobs/JalNib.ts:12` : `const damage = Math.floor(Random.get() * 5);`
* `autozuk/index.html:1486` : `nibbler:{def:15,magic:15,atk:1,str:1,ranged:1,off:{crush:0},defensive:{stab:-20,slash:-20,crush:-20,magic:-20,light:-20,standard:-20,heavy:-20},max:4,style:'melee',meleeType:'crush'},`

### C029 Jal-MejRah: hitpoints / level / size = 25 / 85 / 2

* `infernotrainer/src/content/inferno/js/mobs/JalMejRah.ts:47` : `hitpoint: 25,`
* `infernotrainer/src/content/inferno/js/mobs/JalMejRah.ts:25` : `return 85;`
* `infernotrainer/src/content/inferno/js/mobs/JalMejRah.ts:87` : `return 2;`
* `infernostats/src/main/java/com/infernostats/model/InfernoNpc.java:10` : `BAT("Jal-MejRah", 2, Color.GRAY, "bat"),`

### C030 Jal-MejRah: attack speed / range / style = 3 / 4 / ranged

* `infernotrainer/src/content/inferno/js/mobs/JalMejRah.ts:79` : `return 3;`
* `infernotrainer/src/content/inferno/js/mobs/JalMejRah.ts:83` : `return 4;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:394` : `BAT(new int[]{NpcID.INFERNO_CREATURE_HARPIE}, Attack.RANGED, 3, 4, 7),`

### C031 Jal-MejRah: max hit; run-energy drain per hit = 19; run energy -300 (trainer stat units)

* `infernotrainer/src/content/inferno/js/mobs/JalMejRah.ts:15` : `player.currentStats.run -= 300;`
* `autozuk/index.html:1487` : `bat:{def:55,magic:120,atk:1,str:1,ranged:120,off:{ranged:30},defensive:{stab:30,slash:30,crush:30,magic:-20,light:45,standard:45,heavy:45},max:19,style:'range'},`

### C032 Jal-Ak: hitpoints / level / size / range = 40 / 165 / 3 / 15

* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:50` : `hitpoint: 40,`
* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:22` : `return 165;`
* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:93` : `return 3;`
* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:89` : `return 15;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:395` : `BLOB(new int[]{NpcID.INFERNO_CREATURE_SPLITTER}, Attack.UNKNOWN, 6, 15, 4),`
* `infernostats/src/main/java/com/infernostats/model/InfernoNpc.java:11` : `BLOB("Jal-Ak", 3, Color.YELLOW, "blob"),`

### C033 Jal-Ak: attack cycle = 6 ticks (trainer: attackSpeed 3 doubled by the scan: scan, then attack 3 ticks later)

* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:81` : `// Since blobs attack on a 6 tick cycle, but these mechanics are odd, i set the`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:395` : `BLOB(new int[]{NpcID.INFERNO_CREATURE_SPLITTER}, Attack.UNKNOWN, 6, 15, 4),`
* `autozuk/index.html:379` : `blob:{letter:'B',size:3,hp:40,atkSpeed:3,range:15,style:'blob',color:'#D9C24A',isBlob:true},`

### C034 Jal-Ak: scan rule (when it reads the prayer) = scans when it gains line of sight, or when its cooldown is <= 0 and no scan is held; the attack follows 3 ticks later

* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:139` : `// Scan when appropriate`
* `autozuk/index.html:909` : `if(mob.isBlob){if(!mob.hasLOS&&!mob.blobScanPrayer)return;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:308` : `//Blob prayer detection`

### C035 Jal-Ak: style from the overhead read at the scan = Protect from Magic -> ranged; Protect from Missiles -> magic; no overhead (or melee) -> 50/50 magic or ranged

* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:116` : `return this.playerPrayerScan === "magic" ? "range" : "magic";`
* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:114` : `return Random.get() < 0.5 ? "magic" : "range";`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:313` : `if (PrayerInteractions.isActive(Prayer.PROTECT_FROM_MISSILES))`

### C036 Jal-Ak: max hit (magic/ranged) = 29

* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:124` : `return 29;`
* `autozuk/index.html:1488` : `blob:{def:95,magic:160,atk:160,str:160,ranged:160,off:{crush:0,magic:45,ranged:45},defensive:{stab:25,slash:25,crush:25,magic:25,light:25,standard:25,heavy:25},max:29,style:'blob',meleeType:'crush'},`

### C037 Jal-Ak: melee when the player is adjacent = crush, 50% of attacks while within melee distance (diagonals count)

* `osrs_sdk/src/sdk/Mob.ts:340` : `if (this.isWithinMeleeRange() && Random.get() < 0.5) {`
* `autozuk/index.html:514` : `function canUseSecondaryMelee(mob,player){`

### C038 Jal-Ak: bloblets on death = 3: range (+1,-1), melee (0,0), mage (+2,-2) from the blob SW tile; 15 hp, level 70; first attack after a cooldown of 4

* `infernotrainer/src/content/inferno/js/mobs/JalAk.ts:158` : `removedFromWorld() {`
* `infernotrainer/src/content/inferno/js/mobs/JalAkRekMej.ts:44` : `hitpoint: 15,`
* `autozuk/index.html:936` : `function hlSpawnBlobletsFromBlob(blob,tick,S){`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoBlobDeathSpot.java:11` : `static final int BLOB_DEATH_TICKS = 3;`

### C039 Jal-AkRek (bloblets): hp / max hit / speed / range = 15 / 18 / 4 / melee 1, mage 15, range 15 (level 70)

* `infernotrainer/src/content/inferno/js/mobs/JalAkRekKet.ts:45` : `hitpoint: 15,`
* `autozuk/index.html:1489` : `blobletMage:{def:95,magic:120,atk:1,str:1,ranged:1,off:{magic:25},defensive:{stab:0,slash:0,crush:0,magic:25,light:0,standard:0,heavy:0},max:18,style:'magic'},`
* `infernotrainer/src/content/inferno/js/mobs/JalAkRekMej.ts:81` : `return 15;`

### C040 Jal-ImKot: hitpoints / level / size / speed / range = 75 / 240 / 4 / 4 / 1

* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:46` : `hitpoint: 75,`
* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:22` : `return 240;`
* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:83` : `return 4;`
* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:95` : `return 4;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:396` : `MELEE(new int[]{NpcID.INFERNO_CREATURE_MELEE}, Attack.MELEE, 4, 1, 3),`
* `infernostats/src/main/java/com/infernostats/model/InfernoNpc.java:12` : `MELEE("Jal-ImKot", 4, Color.ORANGE, "melee"),`

### C041 Jal-ImKot: max hit = 49 (slash)

* `autozuk/index.html:1492` : `meleer:{def:120,magic:120,atk:210,str:290,ranged:220,off:{slash:40},defensive:{stab:65,slash:65,crush:65,magic:30,light:50,standard:50,heavy:50},max:49,style:'melee',meleeType:'slash'},`
* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:42` : `strength: 290,`

### C042 Jal-ImKot: dig trigger = no line of sight and attackDelay <= -38 with 10% per tick, or <= -50

* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:113` : `if ((this.attackDelay <= -38 && Random.get() < 0.1) || this.attackDelay <= -50) {`
* `autozuk/index.html:871` : `if(mob.hasDig&&!mob.hasLOS&&!mob.digTimer){if((mob.attackDelay<=-38&&S.rng()<0.1)||mob.attackDelay<=-50){startDig(mob,player,region);return}}`

### C043 Jal-ImKot: dig and resurface timing = 6 ticks frozen digging; on resurfacing attackDelay 6 and frozen 2; observers use 12 ticks from the burrow animation to the next attack

* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:129` : `this.digSequenceTime = 6;`
* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:175` : `this.attackDelay = 6;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:288` : `else if (npcAnimationId == 7600)`
* `openosrs_inferno/inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoNPC.java:261` : `else if (this.getNpc().getAnimation() == 7600)`

### C044 Jal-ImKot: dig landing tile = first free of: (player.x-3, player.y+3), under the player, (x-3, y), (x, y+3), else (x-1, y+1) (trainer frame)

* `infernotrainer/src/content/inferno/js/mobs/JalImKot.ts:114` : `this.startDig();`
* `autozuk/index.html:1267` : `function startDig(mob,player,region){`

### C045 Jal-Xil: hitpoints / level / size / speed = 125 / 370 / 3 / 4

* `infernotrainer/src/content/inferno/js/mobs/JalXil.ts:68` : `hitpoint: 125,`
* `infernotrainer/src/content/inferno/js/mobs/JalXil.ts:33` : `return 370;`
* `infernotrainer/src/content/inferno/js/mobs/JalXil.ts:109` : `return 3;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:397` : `RANGER(new int[]{NpcID.INFERNO_CREATURE_RANGER, NpcID.INFERNO_RANGER_FINALWAVE}, Attack.RANGED, 4, 98, 2),`
* `infernostats/src/main/java/com/infernostats/model/InfernoNpc.java:13` : `RANGER("Jal-Xil", 3, Color.GREEN, "ranger"),`

### C046 Jal-Xil: attack range = trainer/AUTOZUK 15 tiles; kotori Type.RANGER range 98

* `infernotrainer/src/content/inferno/js/mobs/JalXil.ts:105` : `return 15;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:397` : `RANGER(new int[]{NpcID.INFERNO_CREATURE_RANGER, NpcID.INFERNO_RANGER_FINALWAVE}, Attack.RANGED, 4, 98, 2),`
* `autozuk/index.html:377` : `ranger:{letter:'R',size:3,hp:125,atkSpeed:4,range:15,style:'range',color:'#43A85B'},`

### C047 Jal-Xil: max hit (ranged / crush when adjacent) = 46 / 19

* `autozuk/index.html:1493` : `ranger:{def:60,magic:90,atk:140,str:180,ranged:250,off:{ranged:40,crush:0},defensive:{stab:0,slash:0,crush:0,magic:0,light:0,standard:0,heavy:0},max:46,style:'range',meleeMax:19,meleeType:'crush'},`

### C048 Jal-Xil: projectile hit delay = trainer: SDK ranged formula floor((3+d)/6)+1 then +2 (reduceDelay -2); AUTOZUK calibrated table by distance from the 3x3 centre: d1-5 3, d6-9 4, d10-12 5, d13+ 6 (hit tick, attack tick = 1)

* `infernotrainer/src/content/inferno/js/mobs/JalXil.ts:56` : `reduceDelay: -2,`
* `osrs_sdk/src/sdk/weapons/RangedWeapon.ts:25` : `calculateHitDelay(distance: number) {`
* `autozuk/index.html:450` : `ranger:[3,3,3,3,3,4,4,4,4,5,5,5,6,6,6,6],`

### C049 Jal-Zek: hitpoints / level / size / speed = 220 / 490 / 4 / 4

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:86` : `hitpoint: 220,`
* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:50` : `return 490;`
* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:119` : `return 4;`
* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:127` : `return 4;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:398` : `MAGE(new int[]{NpcID.INFERNO_CREATURE_MAGER, NpcID.INFERNO_MAGER_FINALWAVE}, Attack.MAGIC, 4, 98, 1),`
* `infernostats/src/main/java/com/infernostats/model/InfernoNpc.java:14` : `MAGER("Jal-Zek", 4, Color.RED, "mager");`

### C050 Jal-Zek: max hit (magic / stab when adjacent) = 70 / 52

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:147` : `return 70;`
* `autozuk/index.html:1494` : `mager:{def:260,magic:300,atk:370,str:510,ranged:510,off:{magic:80,stab:0},defensive:{stab:0,slash:0,crush:0,magic:0,light:0,standard:0,heavy:0},max:70,style:'magic',meleeMax:52,meleeType:'stab'}`

### C051 Jal-Zek: flicker (visual tell) = 1 tick before the attack

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:37` : `flickerDurationTicks = 1;`
* `autozuk/index.html:903` : `if(mob.hasFlicker){mob.flickering=(mob.attackDelay===1&&mob.hasLOS);if(!mob.hasLOS||mob.attackDelay>0||isUnderMob(mob,player))return;`

### C052 Jal-Zek: resurrection chance per attack opportunity = 10%

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:199` : `if (Random.get() < 0.1 && !this.shouldRespawnMobs) {`
* `autozuk/index.html:904` : `if(S.rng()<0.1&&S.deadMobs.length>0){let toRes=S.deadMobs.shift();toRes.revivedOnce=true;let reviveHp=Math.floor(toRes.maxHp/2);toRes.hp=reviveHp;toRes.dead=false;toRes.dying=-1;toRes.pendingRemovalTi`

### C053 Jal-Zek: revive: hp, once, who, which waves = returns at floor(maxhp/2); each corpse once; nibblers and bloblets excluded; none on wave 69

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:204` : `// Set to 50% health`
* `infernotrainer/src/content/inferno/js/InfernoMobDeathStore.ts:9` : `if (!mob.hasResurrected) {`
* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:64` : `this.shouldRespawnMobs = region.wave >= 69;`
* `autozuk/index.html:962` : `if(!mob.type.startsWith('bloblet')&&mob.type!=='nibbler'&&!mob.revivedOnce)S.deadMobs.push(mob);`

### C054 Jal-Zek: ticks after a revive = mager acts again after 8; the revived monster's first attack after attackSpeed (trainer) or attackSpeed+1 (AUTOZUK)

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:217` : `this.attackDelay = 8;`
* `autozuk/index.html:907` : `mob.attackDelay=mob.atkSpeed*2;return}`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:293` : `else if (npcAnimationId == 7611)`
* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:207` : `mobToResurrect.attackDelay = mobToResurrect.attackSpeed;`

### C055 Jal-Zek: revive tile = first free tile scanning x 26..32, y 24..36 (trainer frame), fallback (21,22)

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:159` : `for (let x = 15 + 11; x < 22 + 11; x++) {`
* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:169` : `return { x: 21, y: 22 };`

### C056 JalTok-Jad: hitpoints / level / size = 350 / 900 / 5

* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:157` : `hitpoint: 350,`
* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:141` : `return 900;`
* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:238` : `return 5;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:131` : `nameMapBuilderSimple.put(900, "Jad");`

### C057 JalTok-Jad: attack speed = 8 on wave 67 and the Zuk-wave Jad; 9 on wave 68; kotori: 8 after the animation (6 with its sixTickJad option)

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:450` : `{ aggro: player, attackSpeed: 8, stun: 1, healers: 5, isZukWave: false },`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:464` : `{ aggro: player, attackSpeed: 9, stun: stunTimers[0], healers: 3, isZukWave: false },`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:154` : `attackSpeed: 8,`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:240` : `if (config.sixTickJad())`
* `openosrs_inferno/inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoNPC.java:221` : `this.updateNextAttack(this.getType().getDefaultAttack(), 8);`

### C058 JalTok-Jad: prayer must be up N ticks after the attack animation starts = 3 (JAD_PROJECTILE_DELAY = 3; kotori ticksAfterAnimation 3)

* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:33` : `const JAD_PROJECTILE_DELAY = 3;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:399` : `JAD(new int[]{NpcID.INFERNO_JAD, NpcID.INFERNO_JAD_FINALWAVE, NpcID.JAD_CHALLENGE_JAD}, Attack.UNKNOWN, 3, 99, 0),`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:205` : `//Jad animation detection`

### C059 JalTok-Jad: style choice = 50% ranged / 50% magic; melee (stab) 50% of attacks while adjacent

* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:262` : `return Random.get() < 0.5 ? "range" : "magic";`
* `osrs_sdk/src/sdk/Mob.ts:340` : `if (this.isWithinMeleeRange() && Random.get() < 0.5) {`

### C060 JalTok-Jad: max hit (magic) = 113

* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:274` : `return 113;`

### C061 JalTok-Jad: animations the plugin uses to tell the style = mage 7592, range 7593 (melee 7590 in TJS)

* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:184` : `public static final int JALTOK_JAD_MAGE_ATTACK = 7592;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:185` : `public static final int JALTOK_JAD_RANGE_ATTACK = 7593;`

### C062 Jad healers: spawn threshold = below 50% of hitpoints (175 of 350), once

* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:170` : `if (this.currentStats.hitpoint < this.stats.hitpoint / 2) {`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:400` : `HEALER_JAD(new int[]{NpcID.TZHAAR_FIGHTCAVE_SWARM_BOSS_CLERIC, NpcID.INFERNO_JAD_HEALER, NpcID.INFERNO_JAD_HEALER_FINALWAVE, NpcID.JAD_CHALLENGE_HEALER}, Attack.MELEE, 4, 1, 6),`

### C063 Jad healers: count = 5 on wave 67; 3 per Jad on wave 68; 3 on the Zuk-wave Jad

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:450` : `{ aggro: player, attackSpeed: 8, stun: 1, healers: 5, isZukWave: false },`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:464` : `{ aggro: player, attackSpeed: 9, stun: stunTimers[0], healers: 3, isZukWave: false },`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:156` : `healers: 3,`

### C064 Yt-HurKot: hp / level / speed / size = 90 / 141 / 4 / 1

* `infernotrainer/src/content/inferno/js/mobs/YtHurKot.ts:64` : `hitpoint: 90,`
* `infernotrainer/src/content/inferno/js/mobs/YtHurKot.ts:46` : `return 141;`
* `infernotrainer/src/content/inferno/js/mobs/YtHurKot.ts:96` : `return 4;`
* `infernotrainer/src/content/inferno/js/mobs/YtHurKot.ts:108` : `return 1;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:400` : `HEALER_JAD(new int[]{NpcID.TZHAAR_FIGHTCAVE_SWARM_BOSS_CLERIC, NpcID.INFERNO_JAD_HEALER, NpcID.INFERNO_JAD_HEALER_FINALWAVE, NpcID.JAD_CHALLENGE_HEALER}, Attack.MELEE, 4, 1, 6),`

### C065 Yt-HurKot: heal per attack on Jad = trainer: random 0..19 (damage = -floor(random*20))

* `infernotrainer/src/content/inferno/js/mobs/YtHurKot.ts:16` : `this.damage = -Math.floor(Random.get() * 20);`

### C066 Yt-HurKot: placement when spawned = random offset around Jad: waves 67/68 x -5..+5, y -5..+9 less size; Zuk wave x 0..5, y -(0..3) less size; retried while the tile holds a monster

* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:185` : `xOff = Math.floor(Random.get() * 11) - 5;`
* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:182` : `xOff = Math.floor(Random.get() * 6);`

### C067 Jad wave 67: positions (trainer frame) = Jad (23,27), player (18,25)

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:449` : `{ x: 23, y: 27 },`

### C068 Jad wave 68: positions and stun offsets = Jads (18,24) (28,24) (23,35); stun shuffle of [1,4,7]; attack speed 9

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:459` : `const stunTimers = [1, 4, 7].sort(() => 0.5 - Math.random());`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:463` : `{ x: 18, y: 24 },`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:477` : `{ x: 23, y: 35 },`

### C069 TzKal-Zuk: hitpoints / level / size = 1200 / 1400 / 7

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:245` : `hitpoint: 1200,`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:215` : `return 1400;`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:292` : `return 7;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoWaveMappings.java:119` : `waveMapBuilder.put(69, new int[]{1400});`

### C070 TzKal-Zuk: attack speed = 10; 7 when enraged (below 240 hp)

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:277` : `if (this.enraged) {`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:233` : `this.updateNextAttack(this.getType().getDefaultAttack(), 10);`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:228` : `this.updateNextAttack(this.getType().getDefaultAttack(), 7);`
* `openosrs_inferno/inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoNPC.java:208` : `this.updateNextAttack(this.getType().getDefaultAttack(), 7);`

### C071 TzKal-Zuk: first attack = trainer: attackDelay 14 and stun 8 at spawn; kotori: 12 ticks once the shield has reached its corner ("TODO: Could be 10 or 11. Test!")

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:72` : `this.attackDelay = 14;`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:231` : `this.stunned = 8;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:513` : `infernoNPC.updateNextAttack(InfernoNPC.Attack.UNKNOWN, 12); // TODO: Could be 10 or 11. Test!`

### C072 TzKal-Zuk: max hit = trainer 251 (magicMaxHit)

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:223` : `return 251;`

### C073 TzKal-Zuk: projectile = typeless; setDelay 4, visualDelayTicks 2; ignores protection prayers (isBlockable false)

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:44` : `setDelay: 4,`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:31` : `isBlockable() {`

### C074 TzKal-Zuk: target choice = hits the shield unless the player is left of the shield's x, 5 or more tiles right of it, or north of y 16 (trainer frame y > 16): then the player

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:201` : `if (this.aggro.location.x < this.shield.location.x || this.aggro.location.x >= this.shield.location.x + 5) {`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:204` : `if (this.aggro.location.y > 16) {`

### C075 Zuk set timer: first set and period = 72 ticks after spawn, then every 350 ticks

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:66` : `setTimer = 72;`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:125` : `this.setTimer = 350;`

### C076 Zuk set timer: pause and resume = pauses once when Zuk is below 600 hp; Jad spawns and the timer resumes below 480 hp with +175 ticks (105 s)

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:140` : `if (this.currentStats.hitpoint < 600 && this.hasPaused === false) {`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:145` : `if (this.currentStats.hitpoint < 480) {`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:146` : `this.setTimer += 175;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:995` : `final int pauseHp = 600;`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoSpawnTimerInfobox.java:38` : `private static final long SPAWN_DURATION_INCREMENT = 105; // 1 minute 45 seconds`
* `settimer/src/main/java/com/settimer/SetTimer.java:29` : `private static final int jadTime = 1 * 60 + 45;`
* `inferno_timer_maxswa/src/App.tsx:46` : `const JAD_SECONDS = 105;`

### C077 Zuk set contents: mager + ranger per set = 1 Jal-Zek at (20,21) after a 7-tick spawn delay and 1 Jal-Xil at (29,21) after 9, both aggro on the shield (trainer frame)

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:127` : `const mager = new JalZek(this.region, { x: 20, y: 21 }, { aggro: this.shield, spawnDelay: 7 });`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:129` : `const ranger = new JalXil(this.region, { x: 29, y: 21 }, { aggro: this.shield, spawnDelay: 9 });`

### C078 Zuk Jad: spawn threshold and kit = below 480 hp: 1 Jad at (24,25), speed 8, stun 1, 3 healers, spawn delay 7, aggro on the shield

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:149` : `const jad = new JalTokJad(`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:157` : `isZukWave: true,`

### C079 Zuk healers: Jal-MejJak threshold, count, tiles = below 240 hp: 4 Jal-MejJak at (16,9) (20,9) (30,9) (34,9), spawn delay 2

* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:165` : `if (this.currentStats.hitpoint < 240 && this.enraged === false) {`
* `infernotrainer/src/content/inferno/js/mobs/TzKalZuk.ts:168` : `const healer1 = new JalMejJak(this.region, { x: 16, y: 9 }, { aggro: this, spawnDelay: 2 });`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:377` : `case HEALER_ZUK:`

### C080 Jal-MejJak: hp / level / speed / heal / spark = 75 / 250 / 3 / heals Zuk random 0..24 / 3 sparks per volley, 5..10 each, landing after 2 ticks

* `infernotrainer/src/content/inferno/js/mobs/JalMejJak.ts:121` : `hitpoint: 75,`
* `infernotrainer/src/content/inferno/js/mobs/JalMejJak.ts:100` : `return 250;`
* `infernotrainer/src/content/inferno/js/mobs/JalMejJak.ts:158` : `return 3;`
* `infernotrainer/src/content/inferno/js/mobs/JalMejJak.ts:22` : `this.damage = -Math.floor(Random.get() * 25);`
* `infernotrainer/src/content/inferno/js/InfernoHealerSpark.ts:20` : `this.damage = 5 + Math.floor(Random.get() * 6);`
* `infernotrainer/src/content/inferno/js/mobs/JalMejJak.ts:73` : `DelayedAction.registerDelayedAction(`

### C081 Ancestral Glyph (shield): hitpoints / size / speed / ends = 600 / 5 / 1 tile per tick / reverses when x < 11 or x > 35 (so it reaches x 10 and x 36 in the trainer frame), pausing at each end

* `infernotrainer/src/content/inferno/js/ZukShield.ts:49` : `hitpoint: 600,`
* `infernotrainer/src/content/inferno/js/ZukShield.ts:168` : `return 5;`
* `infernotrainer/src/content/inferno/js/ZukShield.ts:144` : `this.location.x++;`
* `infernotrainer/src/content/inferno/js/ZukShield.ts:148` : `if (this.location.x < 11) {`
* `infernotrainer/src/content/inferno/js/ZukShield.ts:152` : `if (this.location.x > 35) {`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:845` : `nextShieldXCoord += ticksTilZukAttack;`

### C082 Ancestral Glyph (shield): pause at each end = trainer freeze(5) on reaching an end (x < 11 or x > 35); kotori ticksLeftInCorner 4

* `infernotrainer/src/content/inferno/js/ZukShield.ts:149` : `this.freeze(5);`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoPlugin.java:761` : `zukShieldTicksLeftInCorner = 4;`
* `openosrs_inferno/inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoPlugin.java:759` : `zukShieldTicksLeftInCorner = 4;`

### C083 Ancestral Glyph (shield): start and direction = spawn (23,13), frozen 1 tick, direction random per run

* `infernotrainer/src/content/inferno/js/ZukShield.ts:38` : `this.freeze(1);`
* `infernotrainer/src/content/inferno/js/ZukShield.ts:35` : `this.movementDirection = Random.get() < 0.5 ? true : false;`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:489` : `const shield = new ZukShield(this, { x: 23, y: 13 }, { aggro: player }, shieldDirection);`

### C084 Zuk arena: geometry (trainer frame) = Zuk SW (22,8) size 7; walls at x=21 and x=29 for y 0..8; player start (25,15); tile markers at y=14, safe x 14/20/30/36, unsafe 16-18 and 32-34

* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:492` : `this.addMob(new TzKalZuk(this, { x: 22, y: 8 }, { aggro: player }));`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:494` : `this.addEntity(new Wall(this, { x: 21, y: 8 }));`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:513` : `this.addEntity(new TileMarker(this, { x: 14, y: 14 }, "#00FF00", 1, false));`
* `infernotrainer/test/simulations/ZukLineOfSight.test.ts:55` : `test("player has line of sight at left safespot", () => {`

### C085 Jal-Zek: no revival on wave 69 = revive disabled when wave >= 69

* `infernotrainer/src/content/inferno/js/mobs/JalZek.ts:64` : `this.shouldRespawnMobs = region.wave >= 69;`

### C086 line of sight: algorithm and masks = Bresenham-style, 16.16 fixed point; masks NORTH 0x400, EAST 0x1000, SOUTH 0x4000, WEST 0x10000, FULL 0x20000; an NPC's LoS is tested from the player to the closest footprint tile; range 1 = melee adjacency

* `osrs_sdk/src/sdk/LineOfSight.ts:19` : `FULL_MASK = 0x20000,`
* `osrs_sdk/src/sdk/LineOfSight.ts:68` : `static hasLineOfSight(`
* `autozuk/index.html:499` : `function raycast(region,x1,y1,x2,y2){`
* `kotori_inferno/inferno/src/main/java/com/theplug/kotori/inferno/InfernoNPC.java:107` : `boolean hasLos = new WorldArea(target, 1, 1).hasLineOfSightTo(client.getTopLevelWorldView(), this.getNpc().getWorldArea());`

### C087 movement: NPC step rule = one step a tick toward the player by sign(dx), sign(dy); if the destination footprint overlaps the player keep y (corner safespot); under the player, a random cardinal sidestep; no move while attackDelay > attackSpeed (after a dig); frozen or stunned block movement

* `osrs_sdk/src/sdk/Mob.ts:110` : `let dx = this.location.x + Math.sign(this.aggro.location.x - this.location.x);`
* `osrs_sdk/src/sdk/Mob.ts:144` : `// allows corner safespotting`
* `osrs_sdk/src/sdk/Mob.ts:149` : `// No movement right after melee dig. 8 ticks after the dig it should be able to move again.`
* `autozuk/index.html:874` : `else if(collisionMath(dx,dy,mob.size,player.x,player.y,1)){dy=mob.y}`

### C088 combat: hit delay formulas (engine; NPC projectiles add their own delay) = magic floor((1+d)/3)+1; ranged floor((3+d)/6)+1

* `osrs_sdk/src/sdk/weapons/MagicWeapon.ts:22` : `calculateHitDelay(distance: number) {`
* `osrs_sdk/src/sdk/weapons/RangedWeapon.ts:25` : `calculateHitDelay(distance: number) {`

### C089 combat: retaliation delay = a retaliating NPC waits floor(speed/2)+1 ticks (Jad flinchDelay 2)

* `osrs_sdk/src/sdk/Unit.ts:363` : `return Math.floor(this.attackSpeed / 2);`
* `infernotrainer/src/content/inferno/js/mobs/JalTokJad.ts:229` : `get flinchDelay() {`

### C090 combat: NPC max hit and defence roll = max = floor((effective * (bonus + 64) + 320) / 640), effective = level + 9; defence roll (def + 9) * (bonus + 64)

* `osrs_sdk/src/sdk/gear/Weapon.ts:204` : `return Math.floor(Random.get() * (this._maxHit(from, to, bonuses) + 1));`
* `autozuk/index.html:1618` : `let npc=INFERNO_NPCS[type],defenceRoll=(npc.magic+9)*(((npc.defensive.magic)||0)+64);`

### C091 Zuk safespots: pre-enrage safe and danger tiles on the north row (region 9043 coordinates) = safe (grey) region (20,46) (26,46) (36,46) (42,46); danger (red) (22,46) (23,46) (24,46) (38,46) (39,46) (40,46); labelled UNSAFE (31,45); other grey markers (33,43) (32,29) (26,34)

* `runemarkers_inferno/entities/inferno.json:15` : `"regionX": 36,`
* `runemarkers_inferno/entities/inferno.json:36` : `"regionX": 20,`
* `runemarkers_inferno/entities/inferno.json:64` : `"regionX": 31,`
* `runemarkers_inferno/entities/inferno.json:93` : `"regionX": 33,`
* `infernotrainer/src/content/inferno/js/InfernoRegion.ts:513` : `this.addEntity(new TileMarker(this, { x: 14, y: 14 }, "#00FF00", 1, false));`

### C092 Zuk set timer: first set after the Zuk cutscene (web helper) = 51 s from the cutscene to the first set; then a 210 s cycle (timer wraps at 209)

* `(not copied; clone under build/corpus_tmp) bradyp30__Zuk-Timer/timer.js:2` : `const start_time = 51; // Time from cutscene to first set spawn`
* `(not copied; clone under build/corpus_tmp) bradyp30__Zuk-Timer/timer.js:23` : `if (remaining_time < 0) remaining_time = 209;`
* `(not copied; clone under build/corpus_tmp) bradyp30__Zuk-Timer/timer.js:57` : `// Pause @600 hp`

