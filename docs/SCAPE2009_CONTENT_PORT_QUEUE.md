# 2009scape content port queue

Agent-loop state for **2009scape → OSRS-Content** forward port of authentic
**~Jan 2009 (rev 530)** content that LostCity (Sept 2004 / rev 254) never had.

LostCity remains the content *shape* (RuneScript triggers, procs, configs).
2009scape (`/Users/matthewevers/Documents/git_repos/2009scape`) is the
behaviour reference for mid-era skills/quests/minigames (farming, hunter,
construction, slayer masters, Pest Control, Barrows, …). When 2009scape and
the osrs239 cache disagree, **the cache wins** for wire and varp/varbit
layout; 2009scape wins only for *policy* the cache does not state.

Parallel to:

- [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) — LostCity → tree
- [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) — modern /
  post-2009 OSRS only (Wintertodt, Motherlode, Zulrah, …)

**Do not steal LC slices** that still have a LostCity `.rs2`. **Prefer this
queue over Kronos** for anything that existed by Jan 2009 — 2009scape is the
authenticity-first remake; Kronos carries private-server inventiveness.

Each tick ports **one** pending unblocked slice per `docs/PORTING_GUIDE.md` §4
and §4.5. Status: `pending` | `in_progress` | `done` | `blocked`.

Loop prompt: read this file + PORTING_GUIDE §4 / §4.5; port the next pending
unblocked slice; verify (`mock230_pack --check-only`, `make -C src mock230-scripts`);
update this file; re-arm. Stop only when the user stops the loop.

## Methodology (non-negotiable)

1. **Grep LostCity first** (`PORTING_GUIDE` §2.2). If LC has the proc, it belongs
   on `CONTENT_PORT_QUEUE`, not here.
2. **No game-facing strings / ids / config constants in C.** 2009scape Java/Kotlin
   is a *reference*, not something to re-implement in the engine. Express as
   `.rs2` + configs. New Server VM opcodes only when content cannot say it
   (`PORTING_GUIDE` §2.4 / §2.5) — plan + implement in the same slice (log below).
3. **Resolve names through the pack** — never copy 2009scape / rev-530 ids into
   osrs239 content. Classic farming varbits often still share numeric ids, but
   **measure** (`all.varbit.compack`, multilocs, enums 1233–1237) before binding;
   author named aliases when the pack only has `varbit_N`.
4. **Skip custom / non-OSRS** (see skip list). Prefer cache dbtable / CS2
   contracts over 2009scape inventiveness.
5. **Interfaces:** drive the rev-230 panel; do not invent IF1. See
   `UI_ERA_PORTING_GUIDE.md`. Tools IF already landed (`interface_farming/`,
   `farming_server_reqs.md`).

## Skip list (custom / out of scope)

| 2009scape path | Why skip |
|---|---|
| `content/global/bots/**` | server bots, not content |
| `core/game/worldevents/holiday/**`, holiday AME | seasonal custom / holiday randoms |
| `content/global/skill/summoning/**`, Wolf Whistle | Summoning is not in OSRS |
| `content/minigame/fog/**` | Fist of Guthix removed from OSRS |
| `whileguthixsleeps` | RS2-only; never shipped in OSRS |
| Evil Turnip / summoning-linked patches | Summoning ecosystem |
| `content/global/activity/cchallange` | community challenge custom |
| Discord / Grafana / SQLite integrations | infra, not content |
| Donor / loyalty / profit hooks if any appear | against 2009scape's own values and ours |

## Ownership vs Kronos

This queue owns mid-era skills and authentic 2005–2009 activities. Kronos
slices that overlap are **deferred here** (see Kronos queue notes). Kronos keeps
post-2009-only content (rooftops OSRS, Wintertodt, Motherlode, NMZ, Zulrah,
Vorkath, Hydra, ToB, Inferno, …).

## Queue

| # | Slice | Status | Notes |
|---|---|---|---|
| 0 | Queue tracker | done | This file + PORTING_GUIDE §4.5 |
| 1a | skill_farming: patch registry + state | done | `skill_farming/` dbtable+helpers; Falador herb=%varbit_780 measured; carriers perm+transmit; no new opcodes; scripts 3855; pack 0 errors |
| 1b | skill_farming: Falador herb (rake/plant/harvest) | done | Rake/plant/harvest guam..harralander; `%farming_transmit_d` sync; softtimer+`date_minutes` catchup; disease stub; `SS_OP_DATE_MINUTES` host landed; scripts 3974; pack 0 errors |
| 1c | skill_farming: allotment + flower (Falador) | done | Potato/onion/cabbage + marigold; protect via grown marigold; transmit a/b/c; scripts 4065; pack 0 errors |
| 1d | skill_farming: other herb patches | done | Catherby/Ardougne/Phas + My Arm; mapzones from jl2; transmit gated by inzone; farming_herb_patch_5 skipped; scripts 4195; pack 0 errors |
| 1e | skill_farming: compost bins | done | Classic bins 1–4 (740–743→transmit_e); fill/close/open/scoop; weeds+crops+supers; rot via date_minutes; scripts 4336; pack 0 errors |
| 1f | skill_farming: trees / fruit / hops / bushes | done | Split complete: 1f1–1f4 |
| 1f1 | skill_farming: tree patches (oak+) | done | Classic tree_1..4 oak rake/plant/grow/check/chop/clear; transmit_a mapzones; scripts 4418; pack 0 errors |
| 1f2 | skill_farming: fruit trees | done | Classic fruit_1..4 apple rake/plant/grow/check/pick/chop/clear; restock 40m; scripts 4467; pack 0 errors |
| 1f3 | skill_farming: hops | done | Classic hops_1..4 barley rake/plant/grow/harvest; plant_state 74 from multilocs; scripts 4497; pack 0 errors |
| 1f4 | skill_farming: bushes | done | Classic bush_1..4 redberry rake/plant/grow/check/pick/clear; berry restock 20m; scripts 4571; pack 0 errors |
| 1g | skill_farming: farming_view (179) | done | Populate from mid-era sim; `::farmingview` open+refresh; Geomancy opener deferred; scripts 4607; pack 0 errors |
| 2a | skill_hunter: bird snare | done | Crimson lay/catch/check; softtimer poll+expire; `::huntersnarebird`; scripts 4680; pack 0 errors |
| 2b | skill_hunter: box trap (chins) | done | Grey chin lay/catch/check; shared trap cap; scripts 4703; pack 0 errors |
| 2c | skill_hunter: net trap + implings | done | Baby impling catch (net+jar, BNetPulse success); jar loot + salamander net deferred; `::hunterbabyimp`; scripts 4720; pack 0 errors |
| 2d | skill_hunter: falconry / kebbit tracking | done | Falconry spotted/dark/dashing catch+retrieve; Matthias Quick-falcon; polar trails deferred; `::hunterfalcon`; scripts 4782; pack 0 errors |
| 3a | skill_slayer: Turael Assignment | done | Landed on Kronos lane (cache `slayer_master_task` + `%if1..if6`); cross-check 2009 `SlayerManager` only if policy drifts |
| 3b | skill_slayer: kill credit + points | done | Landed on Kronos lane (`slayer_kill.rs2` + death hook) |
| 3c | skill_slayer: remaining masters | done | Landed on Kronos lane (`slayer_masters.rs2`); combat gates from cache skill_features |
| 3d | skill_slayer: Cancel/Block/Store arms | done | Landed on Kronos lane (`slayer_task_ops.rs2`) |
| 3e | skill_slayer: monster specials | done | Rockslug salt + desert lizard icy water + lethal-hit cap; mirror/gargoyle/banshee deferred; `::slayerslug`/`::slayerlizard`; scripts 4847; pack 0 errors |
| 4a | skill_construction: house enter/leave | blocked | Needs DynamicRegion / private map instance — not in LC engine.rs2 nor ss_opcode.h; see opcode gap log |
| 4b | skill_construction: build hotspot core | blocked | Depends on 4a instance + hotspot build surface |
| 5 | minigame: Barrows | done | Crypt dig/stairs/sarcophagus spawn+kill flags; tunnel/chest/drain/puzzle deferred; `::barrowscrypt`; scripts 4936; pack 0 errors |
| 6 | minigame: Pest Control | done | Lander board/leave (nov/int/vet combat gates); island `DynamicRegion.create(10536)` blocked; `::pestlander`; scripts 4976; pack 0 errors |
| 7 | minigame: Pyramid Plunder | done | Entrance + mummy join + 5-min timer + leave + room1 urns; spears/doors 2–8/chest/overlay IF deferred; `::ntkplunder`; scripts 5028; pack 0 errors |
| 8 | minigame: Puro-Puro | done | Zanaris enter/leave + wheat push (Hunter 17); crop-circle rotate / wilt / Elnock shop deferred; `::puropuro`/`::puromaze`; scripts 5075; pack 0 errors |
| 9 | minigame: Mage Training Arena | done | Lobby door + hat + Enchant/Alchem/Grave enter/leave; Telekinetic DynamicRegion blocked; room scoring/shop deferred; `::mta`; scripts 5108; pack 0 errors |
| 10 | minigame: Fishing Trawler | lc | LostCity has full `minigames/game_trawler` — port via CONTENT_PORT_QUEUE |
| 11 | minigame: Castle Wars | lc | LostCity has full `minigames/game_castlewars` — port via CONTENT_PORT_QUEUE |
| 12 | minigame: Barbarian Assault | skip | 2009scape only has custom torso-seller stub (not authentic BA) |
| 13 | minigame: Blast Furnace | done | Stairs enter/leave + smith fee/Charos/60 free + fee softtimer kick; pump/ore/bars deferred; `::blastfurnace`; scripts 5140; pack 0 errors |
| 14 | minigame: Trouble Brewing | skip | 2009scape only empty MapArea zone stub |
| 15 | minigame: All Fired Up | blocked | Cache: Blaze/Fyre NPCs unnamed/absent; beacon Add-logs locs not found; `varbit_5146` is 1-bit vs 2009 multi-state — re-measure wire |
| 16 | activity: Shooting Stars | blocked | Needs world `loc_add` rotation; pack only has `osb10_clickzone_shootingstars` |
| 17 | activity: Penguin HS | blocked | Spy notebook + disguise NPCs (bush/cactus/etc) not resolvable in osrs239; Larry=`peng_larry_*` exists; 2009 ids remapped |
| 18 | activity: Treasure Trails (clues) | lc | LostCity has full `minigames/game_trail` — port via CONTENT_PORT_QUEUE **18c** |
| 19 | region: God Wars dungeon | done | Entrance: Tie-rope / Climb-down / dying knight / boulder Move / crack Crawl; KC doors/bosses deferred; `::gwd`; scripts 5227; pack 0 errors |
| 20 | bosses: Giant Mole | done | Park dig (6 hills) + light gate + rope exit; mole AI/drops/Wyson deferred; `::giantmole`; scripts 5176; pack 0 errors |
| 21 | quest: Priest in Peril / Nature Spirit | lc | LC has `quest_priestperil` + `quest_druidspirit` — port via CONTENT_PORT_QUEUE |
| 22 | quest: Recruitment Drive | done | Start: Amik→Tiffy (`rd_teleporter_guy`)→grounds (`m38_77`); quit portals; `%rd_main`; puzzles/shuffle deferred; `::rd`; scripts 5278; pack 0 errors |
| 23 | quest: Lost Tribe | done | Sigmund→cook→Duke permit; `%lost_tribe_quest`+`%lost_tribe_contact`; dig/brooch/Mistag/HAM deferred; `::losttribe`; scripts 5299; pack 0 errors |
| 24 | quest: Dig Site / The Golem | done | Dig Site → LC `quest_itexam` (CONTENT_PORT_QUEUE **18d**); Golem: talk+softclay×4 repair→task; portal/museum deferred; `::golem`; scripts 5326; pack 0 errors |
| 25 | quest: Animal Magnetism | done | Ava start (prereqs+skills) → fetch chickens; `%anma_main`; farm/witch/device deferred; `::anma`; scripts 5359; pack 0 errors |
| 26 | quest: A Soul's Bane | pending | |
| 27 | quest: Creature of Fenkenstrain | pending | |
| 28 | quest: Icthlarin's Little Helper | pending | |
| 29 | quest: Shadow of the Storm | pending | |
| 30 | quest: What Lies Below | pending | |
| 31 | quest: Tears of Guthix | pending | |
| 32 | quest: Rag and Bone Man | pending | |
| 33 | quest: Desert Treasure | pending | Large; split when reached |
| 34 | quest: Zogre Flesh Eaters | pending | |
| 35 | skill_agility: Barbarian / Wilderness courses | pending | LC gnome already on LC queue; 2009 for remaining mid-era |

## Opcode gap log

Record new Server VM opcodes **before** inventing C content hooks. Format:
`slice | opcode | why content needs it | status`.

| Slice | Opcode / surface | Why | Status |
|---|---|---|---|
| 1a | softtimer (2109) | Patch growth + offline catchup | used in 1b |
| 1a | `%varbit` / perm scope | Per-patch state | done — carriers varp_501..516/830/… scope=perm+transmit; helpers switch on loc |
| 1b | `date_minutes` (4629) | Offline growth deadlines | done — host op in `mock230_scripts.c` (LC NumberOps wall-clock minutes) |
| 1g | `runclientscript*` / farming_view_setpanel | Patch grid populate | done — `~farming_view_refresh` drives 1119 from sim; Geomancy access still deferred |
| 4a | instance / dynamic map | POH | measure — may block |
| 4a | `DynamicRegion.reserveArea` / chunk fill | HouseManager.enter/construct (2009scape) | **blocked** — LC `engine.rs2` has no instance/map-alloc opcodes; this engine `ss_opcode.h` only has map_clock/blocked/findsquare/live/…; inventing C house hooks violates PORTING_GUIDE §2.4. Unblock when a map-instance opcode surface is designed + hosted |
| 4b | (same) + hotspot build IF | BuildHotspot / BuildOptionPlugin | **blocked** on 4a |
| 6 | `DynamicRegion.create(10536)` | PestControlActivityPlugin.start / PestControlSession | **blocked** — same map-instance gap as 4a; lander board/leave landed without island voyage |
| 9 | `DynamicRegion` (Telekinetic maze) | TelekineticZone.start / private maze instance | **blocked** — same map-instance gap as 4a; Enchant/Alchem/Grave static rooms entered without it |

## Log

- queue created (2026-08-04): 2009scape → OSRS-Content lane; custom/non-OSRS skip list; mid-era ownership vs Kronos; first slice = farming patch registry (1a)
- slice 1a done: `skill_farming/` — `farming_patches` dbtable (43 classic rows), `%varbit_*` get/set helpers, carrier varps perm+transmit; Falador herb measured as `varbit_780`; transmit sync + rake/plant → 1b; no new Server VM opcodes; `make mock230-scripts` ok (3855); `mock230_pack --check-only` 0 errors
- slice 1b done: Falador herb rake/plant/harvest (guam..harralander); `%varbit_780`↔`%farming_transmit_d`; growth softtimer + login catchup via new `date_minutes` host op; herb-save roll; disease stub; `make mock230-scripts` ok (3974); pack 0 errors; next = 1c Falador allotment+flower
- slice 1c done: Falador allot NW/SE (potato/onion/cabbage, 3 seeds) + centre marigold; flower-protect message; transmit a/b/c/d; scripts 4065; pack 0 errors
- slice 1d done: Catherby/Ardougne/Port Phas herbs (`farming_herb_patch_2..4`, varbits 781..783, transmit_d) + My Arm (`myarm_herbpatch`, varbit_2788, transmit_a at `0_44_57`); mapzones measured from jl2; growth transmit writes gated by `inzone`; unused `farming_herb_patch_5` at m59_44 skipped; scripts 4195; pack 0 errors; next = 1e compost bins
- slice 1e done: classic compost bins 1–4 (Falador/Catherby/Phas/Ardougne, varbits 740–743, `%farming_transmit_e`); fill/close/open/bucket-scoop per 2009scape CompostBin; compostables dbtable (weeds+allot crops+tomato+supers); rot deadline `date_minutes`+35..49; Zeah bins 5–7 deferred; Dump/potion-convert deferred; scripts 4336; pack 0 errors; next = 1f trees/fruit/hops/bushes
- slice 1f1 done: split 1f; classic tree patches 1–4 (Taverley/Falador park/Varrock/Lumbridge, varbits 700–703, transmit_a); oak sapling lifecycle rake→plant→grow(40m)→check-health→chop(1/8 stump)→clear; disease stub; willow+ deferred; scripts 4418; pack 0 errors; next = 1f2 fruit trees
- slice 1f2 done: classic fruit patches 1–4 (Gnome/TGV/Brimhaven/Catherby, varbits 704–707); apple lifecycle check-health(+20 visual)→6 fruit→pick→chop(+19 stump)→clear; fruit restock 40m; scripts 4467; pack 0 errors; next = 1f3 hops
- slice 1f3 done: classic hops patches 1–4 (Yanille/Entrana/Lumbridge/Seers, varbits 716–719, transmit_a); barley lifecycle (4 seeds, plant_state 74 from multilocs not 2009scape 49); allotment-style lives harvest; scripts 4497; pack 0 errors; next = 1f4 bushes
- slice 1f4 done: classic bush patches 1–4 (Champions/Rimmington/Etceteria/Ardougne, varbits 732–735, transmit_a; mapzones 0_49_52/0_45_50/0_40_60/0_40_50); redberry lifecycle check-health(claim 250)→4 berries→pick→clear; berry restock 20m; scripts 4571; pack 0 errors; next = 1g farming_view
- slice 1g done: farming_view (179) populate from mid-era patch sim (herbs/allot/flower/trees/fruit/hops/bushes); herb `view_product` for enum_1238 clean herbs; empty=`bucket_empty`/`weeds`; `::farmingview` open+refresh, `::farmingviewcell` for wire checks; Geomancy lunar opener deferred (corpus gap); scripts 4607; pack 0 errors; next = 2a hunter bird snare
- slice 2a done: skill_hunter bird snare (LostCity has none); crimson swift (`hunting_bird_jungle`) lay→poll catch via npc_huntall→check (feathers+meat+bones) / fail→broken / expire; 2009scape success formula; single trap slot; `::huntersnarebird`; scripts 4680; pack 0 errors; next = 2b box trap
- slice 2b done: box trap grey chin (`hunting_chinchompa`→`chinchompa_captured`); shared `~hunter_trap_max`/`success` with bird snare; lay lvl 27 / catch lvl 53; `::hunterboxchin`; scripts 4703; pack 0 errors; next = 2c net trap + implings
- slice 2c done: baby impling catch (`ii_impling_type_1`/`_maze`); wielded butterfly/magic net + empty `ii_impling_jar` → `ii_captured_impling_1`; lvl 17 / xp 200; 2009scape BNetPulse success (+5 magic net); jar loot stub + salamander tree-net deferred; `::hunterbabyimp`; scripts 4720; pack 0 errors; next = 2d falconry / kebbit tracking
- slice 2d done: falconry (`huntingbeast_speedy/silent/speedy2` → falcon-on-prey retrieve fur+bones); glove swap `falcon_on_gloves`↔`falcon_gloves`; Matthias Quick-falcon rent; expire softtimer; projectile visual + zone leave + polar kebbit trails deferred; `::hunterfalcon`; scripts 4782; pack 0 errors; next = 3e slayer monster specials (or 4a construction)
- slice 3e done: rockslug (`slayer_bag_of_salt` when HP<5) + desert lizards (`slayer_icy_water` when HP≤2, slayer 22); `~slayer_cap_finish_damage` in melee + `npc_default_damage`; mirror shield / gargoyle smash / banshee earmuffs deferred; `::slayerslug`/`::slayerlizard`; scripts 4847; pack 0 errors; next = 4a construction house enter/leave
- slice 4a/4b blocked: POH enter needs `DynamicRegion.reserveArea` / chunk fill (2009scape `HouseManager.getPreparedRegion`); LC `engine.rs2` has no map-instance opcodes; this engine `ss_opcode.h` only map_clock/blocked/findsquare/live/… — no C house hooks (§2.4). Unblock when map-instance surface is designed+hosted. Next = 5 Barrows
- slice 5 done (crypt core): dig on mounds → crypt teleport; stairs → mound; sarcophagus Search spawns brother + kill varbits on `barrows_kills`; tunnel entrance / reward chest / prayer drain / overlay IF / puzzle deferred; `::barrowscrypt`; scripts 4936; pack 0 errors; next = 6 Pest Control
- slice 6 done (lander only): pest lander Cross/Climb for novice/intermediate/veteran (combat 40/70/100); wait softtimer; island voyage blocked on `DynamicRegion.create(10536)` (opcode gap, same as POH); bots skipped; reward shop / Port Sarim sail deferred; `::pestlander`; scripts 4976; pack 0 errors; next = 7 Pyramid Plunder
- slice 7 done (entrance + room1): Tarik stub unlocks doors; Search N/E/S/W → guardian vs empty; mummy Talk/Start → room1 + softtimer (500 ticks); leave tomb / timer expel; room1 closed urns → ivory comb or poison bite; spears / room doors 2–8 / sarcophagus / chest / `ntk_overlay` IF deferred; `::ntkplunder`; scripts 5028; pack 0 errors; next = 8 Puro-Puro
- slice 8 done (enter/leave + wheat): Zanaris `ii_magic_wheat_m_zanaris` Enter → maze; exit portal returns to saved tile; Push-through wheat (Hunter 17, imp-box gate); Fairy Aeryka + Elnock talk stubs; rotating Gielinor crop circles / wilt pulse / Elnock shop / jar gen deferred; `::puropuro`/`::puromaze`; scripts 5075; pack 0 errors; next = 9 Mage Training Arena
- slice 9 done (lobby + static rooms): temple door Magic 7; Entrance Guardian → `%magictraining_entra_noob` + Progress hat; Enchant/Alchem/Grave portals (magic + item gates) enter/leave via `magictraining_returndoor`; Telekinetic blocked on DynamicRegion; shop/scoring/room gameplay deferred; `::mta`; scripts 5108; pack 0 errors; next = 10 Fishing Trawler
- slice 10 → lc: LostCity already has `minigames/game_trawler` (start/flood/net/bail/win); leave on CONTENT_PORT_QUEUE
- slice 11 → lc: LostCity already has `minigames/game_castlewars`; leave on CONTENT_PORT_QUEUE
- slice 12 skip: 2009scape BA is only CaptainCain custom torso shop + empty area zone — not authentic minigame
- slice 13 done (enter/leave): `dwarf_keldagrim_factory_stairs` fee (2500 / Charos 1250 / Smithing 60 free) + `bf_fee_expire` softtimer kick; `blast_furnace_stairs_up` exit; pump/pedals/coke/conveyor/dispenser deferred; `::blastfurnace`; scripts 5140; pack 0 errors; next = 14 Trouble Brewing
- slice 14 skip: Trouble Brewing is empty `TroubleBrewingArea` MapArea only
- slice 15 blocked: All Fired Up — Blaze/Fyre not in npc.compack by name; no Add-logs beacon locs; osrs239 `varbit_5146` is 1-bit (basevar varp_1350) not 2009 multi-state
- slice 16 blocked: Shooting Stars needs global crash rotation (`loc_add`); pack lacks named crash locs beyond OSB clickzone
- slice 20 done (access): Falador Park spade dig on 6 mole-hill tiles (light required) → lair; `mole_rope_02` Climb → park; mole burrow AI / drops / Wyson deferred; `::giantmole`; scripts 5176; pack 0 errors; next = 17 Penguin HS
- slice 17 blocked: Penguin HS — disguise NPCs / spy notebook not in osrs239 under 2009 names; Larry present as `peng_larry_*`; re-measure when penguin content pack lands
- slice 19 done (entrance): Tie-rope (`godwars_rock_no_rope1`) / Climb-down rope (Agility 15 + `%godwars_palladin1`) / dying knight talk / boulder Move (Strength 60) / crack Crawl (Agility 60); carriers overlay on cache `godwars`; KC doors / altar / bosses deferred; `::gwd`; scripts 5227; pack 0 errors; next = 18 Treasure Trails
- slice 18 → lc: LostCity already has `minigames/game_trail` (easy/medium/hard clues); leave on CONTENT_PORT_QUEUE as 18c; next = 21/22 quests (grep LC first)
- slice 21 → lc: LostCity has `quest_priestperil` + `quest_druidspirit`; next = 22 Recruitment Drive
- slice 22 done (start+enter/leave): Amik post-BKF offer (needs `%spy`+`%druidquest` complete) → `%rd_main` referred; Tiffy (`rd_teleporter_guy`) empty inv/worn → tele `m38_77` Spishyus room; quit portals → park; journal stub; puzzles/shuffle/gender/Wanted deferred; `::rd`/`::rdenter`/`::rdpark`; scripts 5278; pack 0 errors; next = 23 Lost Tribe
- slice 23 done (start arc): Sigmund (`lost_tribe_sigmund_there`) start (Rune Mysteries+Goblin Diplomacy) → cook witness → Duke investigation permit; `%lost_tribe_quest` multilocs + `%lost_tribe_contact` early milestones; cellar dig/brooch/librarian/Mistag/HAM/treaty deferred; `::losttribe`; scripts 5299; pack 0 errors; next = 24 Dig Site / The Golem
- slice 24: Dig Site → lc (`quest_itexam` → CONTENT_PORT_QUEUE 18d); Golem done (repair): `golem_broken_golem` Talk (Crafting 20/Thieving 25) → `%golem_a` offered; softclay×4 → `%golem_clay` + repaired → tasked; letter/museum/portal/program deferred; `::golem`; scripts 5326; pack 0 errors; next = 25 Animal Magnetism
- slice 25 done (Ava start): `anma_assistant` Talk if Restless Ghost+Ernest+`%priestperil`≥61+skills → `%anma_main`=10 fetch chickens; Alice/Malcolm/witch/trees/notes/device deferred; PiP body remains LC queue; `::anma`; scripts 5359; pack 0 errors; next = 26 Soul's Bane
