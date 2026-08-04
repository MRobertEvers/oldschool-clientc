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

## Shared tree — never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. `skill_construction/` is live (4a+4b); `minigame_mta/` is live.
Fix your slice. See PORTING_GUIDE §7 and
`.cursor/rules/no-park-sibling-content.mdc`.

Loop prompt: read this file + PORTING_GUIDE §4 / §4.5 / §7; port the next pending
unblocked slice; NEVER park sibling lanes; verify (`mock230_pack --check-only`,
`make -C src mock230-scripts`); update this file; re-arm. Stop only when the
user stops the loop.

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
6. **Never park sibling lanes** — no `*.skip`, no moving live trees aside for
   compile. Fix your own errors (PORTING_GUIDE §7).

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
| 2d | skill_hunter: falconry / kebbit tracking | done | Falconry spotted/dark/dashing catch+retrieve; Matthias Quick-falcon; polar trails →2f; `::hunterfalcon`; scripts 4782; pack 0 errors |
| 3a | skill_slayer: Turael Assignment | done | Landed on Kronos lane (cache `slayer_master_task` + `%if1..if6`); cross-check 2009 `SlayerManager` only if policy drifts |
| 3b | skill_slayer: kill credit + points | done | Landed on Kronos lane (`slayer_kill.rs2` + death hook) |
| 3c | skill_slayer: remaining masters | done | Landed on Kronos lane (`slayer_masters.rs2`); combat gates from cache skill_features |
| 3d | skill_slayer: Cancel/Block/Store arms | done | Landed on Kronos lane (`slayer_task_ops.rs2`) |
| 3e | skill_slayer: monster specials | done | Rockslug salt + desert lizard icy water + lethal-hit cap; mirror/gargoyle/banshee→3f; `::slayerslug`/`::slayerlizard`; scripts 4847; pack 0 errors |
| 3f | skill_slayer: mirror / gargoyle / banshee | done | Mirror shield basilisk/cockatrice; rock hammer gargoyle HP≤10; earmuffs banshee drain/stun; hooks in melee+queue damage; `::slayergarg`/`::slayermirror`/`::slayerbanshee`; scripts 9636; pack 0 errors |
| 4a | skill_construction: house enter/leave | done | Default garden+exit portal via map_instance collage from m29_110; buy/enter/leave; `::poh`/`::pohleave` — **do not park `skill_construction/` / `*.rs2.skip` / `skill_construction.skip`** |
| 4b | skill_construction: build hotspot core | done | Building-mode Build on `poh_crude_garden_6/7` → plant choice → sapling+watering can → `loc_change` + Construction XP; Remove restores hotspot; `::pohbuild`; scripts 6543; pack 0 errors — **do not edit** |
| 4c | skill_construction: build IF + rooms | done | Owned elsewhere / out of this lane — do not extend `skill_construction/` from SCAPE2009 ticks |
| 5 | minigame: Barrows | done | Crypt dig/stairs/sarcophagus spawn+kill flags; tunnel/chest/drain/puzzle deferred; `::barrowscrypt`; scripts 4936; pack 0 errors |
| 5b | minigame: Barrows tunnel | done | Tunnel sarcophagus dialogue→catacombs; chest Open/Search rewards; ladder exit; prayer drain softtimer stub; puzzle/overlay deferred; `::barrowstunnel`; scripts 7266; pack 0 errors |
| 6 | minigame: Pest Control | done | Lander board/leave (nov/int/vet combat gates); island→6b; `::pestlander`; scripts 4976; pack 0 errors |
| 6b | minigame: Pest Control island | done | `map_instance_from_square(10536)` voyage from lander wait; spawn shielded portals + void knight + leave squire; `::pestisland`; shield drops/zeal/overlay/pests/rewards deferred; scripts 6678; pack 3 errors (preexisting `ascentofarceuus.constant` dupes, not this slice) |
| 6c | minigame: Pest Control shield/zeal | done | Overlay IF 408; shuffled shield drops @50t; zeal text + `~pest_add_zeal`; waves stub; `::pestisland`/`::pestzeal`; scripts 7365; pack 0 errors |
| 6d | minigame: Pest Control waves/rewards | done | Wave softtimer spawns pests; portal death→win; knight death→lose; zeal≥50 → `%pest_points`+coins; timeout win; `::pestwin`/`::pestlose`; scripts 7489; pack 0 errors |
| 6e | minigame: Pest Control reward shop | done | `pest_rewardshop` Exchange on void knights; XP/packs/void gear via `last_slot`+enum_2286; `%if1` points; `::pestshop`; scripts 7532; pack 0 errors |
| 6f | minigame: Pest Control Port Sarim sail | done | `pest_squire_ship_*` Travel/Talk; `ship_journey`+`%journey_number` 14/15; docks `1_41_41_39_52`/`1_47_49_33_62`; `::pestsarim`/`::pestoutpost`; scripts 7584; pack 0 errors |
| 5c | minigame: Barrows puzzle/overlay | done | Overlay IF 24 on tunnel enter; puzzle IF 25 on chest-ring doors; wrong→shuffle `%barrows`; `::barrowspuzzle`; scripts 7789; pack 0 errors |
| 6g | minigame: Pest Control AI | done | Idle pests path+hit Void Knight; spinners heal nearest portal; zeal on any pest/portal hit; cats `pest_*`; barricade/splatter/shifter→6h; scripts 7914; pack 0 errors |
| 6h | minigame: Pest Control pest specials | done | Ravager smash via OBJECT_OFFSETS+`loc_change`; splatter explode on barricade/`ai_queue3`; shifter tele when >5 from target; `pest_specials.rs2`; scripts 9686; pack 0 errors |
| 7b | minigame: Pyramid Plunder rooms | done | Overlay IF 428; spears Pass; doors Pick-lock→rooms 2–8; sarcophagus Open; grand gold chest Search; urn artifacts by room; snake charm→7c; `::ntkplunder`/`::ntkroom`; scripts 8056; pack 0 errors |
| 7 | minigame: Pyramid Plunder | done | Entrance + mummy join + 5-min timer + leave + room1 urns; spears/doors 2–8/chest/overlay IF →7b; `::ntkplunder`; scripts 5028; pack 0 errors |
| 8 | minigame: Puro-Puro | done | Zanaris enter/leave + wheat push (Hunter 17); crop-circle/wilt/Elnock →8b; `::puropuro`/`::puromaze`; scripts 5075; pack 0 errors |
| 8b | minigame: Puro-Puro crop-circle/Elnock | done | Rotating Gielinor circles (`loc_add`); maze wilt pulse; Elnock dialogue+exchange+jar gen; storage/scroll overlay deferred; `::purocircle`; scripts 8203; pack 0 errors |
| 9 | minigame: Mage Training Arena | done | Wiki+cache IFs/dialogs/rooms (May 2024) (Telekinetic via `map_instance_from_square`); pizazz scoring; alchemy/enchant/bones hooks; Rewards Guardian shop; `::mta`/`::mta_tele`/`::mta_points`; scripts 6273; pack 0 errors |
| 10 | minigame: Fishing Trawler | lc | LostCity has full `minigames/game_trawler` — port via CONTENT_PORT_QUEUE |
| 11 | minigame: Castle Wars | lc | LostCity has full `minigames/game_castlewars` — port via CONTENT_PORT_QUEUE |
| 12 | minigame: Barbarian Assault | skip | 2009scape only has custom torso-seller stub (not authentic BA) |
| 13 | minigame: Blast Furnace | done | Stairs enter/leave + smith fee/Charos/60 free + fee softtimer kick; pump/ore →13b; `::blastfurnace`; scripts 5140; pack 0 errors |
| 13b | minigame: Blast Furnace pump/ore | done | Pump/pedals/coke/stove/belt/dispenser cool+take; temp gauge IF; breakage/belt NPCs →13c; `::bfmachine`; scripts 8400; pack 0 errors |
| 14 | minigame: Trouble Brewing | skip | 2009scape only empty MapArea zone stub |
| 15 | minigame: All Fired Up | blocked | Cache: Blaze/Fyre NPCs unnamed/absent; beacon Add-logs locs not found; `varbit_5146` is 1-bit vs 2009 multi-state — re-measure wire |
| 16 | activity: Shooting Stars | blocked | Needs world `loc_add` rotation; pack only has `osb10_clickzone_shootingstars` |
| 17 | activity: Penguin HS | blocked | Spy notebook + disguise NPCs (bush/cactus/etc) not resolvable in osrs239; Larry=`peng_larry_*` exists; 2009 ids remapped |
| 18 | activity: Treasure Trails (clues) | lc | LostCity has full `minigames/game_trail` — port via CONTENT_PORT_QUEUE **18c** |
| 19 | region: God Wars dungeon | done | Entrance: Tie-rope / Climb-down / dying knight / boulder Move / crack Crawl; KC doors/bosses deferred; `::gwd`; scripts 5227; pack 0 errors |
| 19b | region: God Wars KC/bosses | done | Overlay IF 406; kill credit via `npc_default_death`; 40 KC Big doors (map door tiles); altars Pray+Teleport; boss AI/ice bridge/grapple/Bandos Str70 deferred→19c; `::gwd`/`::gwdkc`/`::gwdboss`; scripts 8562; pack 0 errors |
| 19c | region: God Wars boss AI | done | Style-mix AI (Graardor/Kree/Zilyana/K'ril) + cries; aviansie melee gate; ice bridge HP70; Bandos bang-door Str70+hammer; grapple Ranged70; multi-AOE/minion sync/drops deferred→19d; scripts 9032; pack 0 errors |
| 19d | region: God Wars drops/minions | done | Boss+minion `ai_queue3` uniques (~7/508) + main; shard combine + hilt→ags/bgs/sgs/zgs (`opheldu`); minion sync/AOE/RDT→19e; `::gwdsword`; scripts 9195; pack 0 errors |
| 19e | region: God Wars minion sync/AOE | done | Chamber `inzone` gates + Zilyana x<2908; minion `ai_timer` assist (pull to boss target); true multi-player AOE + `npc_setrespawn` tick align →19f/gap; scripts 9424; pack 0 errors |
| 19f | region: God Wars respawn sync / multi-AOE | done | `npc_setrespawn` EXTRA 11015 + minion death sync; chamber AOE via LC `huntall`/`huntnext`; RDT already in 19d main; scripts 9545; pack 0 errors |
| 20 | bosses: Giant Mole | done | Park dig (6 hills) + light gate + rope exit; mole AI/drops/Wyson deferred; `::giantmole`; scripts 5176; pack 0 errors |
| 20b | bosses: Giant Mole AI/drops | done | Burrow dig on hit (24% HP 6–99); always claw/skin/big bones + main table; Wyson nest trade; mud extinguish deferred→20c; `::moleboss`; scripts 8728; pack 0 errors |
| 20c | bosses: Giant Mole mud extinguish | done | Burrow `huntall` mud: extinguish open lights (candle/torch/oil lamp); closed lanterns OK; `darkness_medium` (not IF 226/`deadmanprotect`); `::molemud`; scripts 9576; pack 0 errors |
| 7c | minigame: Pyramid Plunder snake charm | done | Check for Snakes→state2; Charm Snake (`snake_flute`)+state3; charmed Search easier roll+66% XP; bite dmg+poison; `::ntkcharm`; scripts 9739; pack 0 errors |
| 8c | minigame: Puro-Puro storage/scroll | done | Scroll Toggle-view → `ii_tracker` overlay (maze-gated); Elnock Quick-withdraw → storage IF + `%ii_stored_*`; `::puroscroll`/`::purostore`; scripts 9867; pack 0 errors |
| 8d | minigame: Puro-Puro imp steal | done | `imp_defender_no_patrol` AI: 1/10 near heroes; Thieving avoid + repellent; steals lowest jar → empty jar ground; `::puroimp`; scripts 9928; pack 0 errors |
| 13c | minigame: Blast Furnace breakage/belt | done | 1/50 pipe/belt/cog break while pump/pedal; Repair Crafting30+hammer; stove multilocs; belt FIFO→`blast_furnace_*_ore` NPCs while pedaling; `::bfbreak`/`::bfbelt`; scripts 10177; pack 0 errors |
| 2e | skill_hunter: salamander net + impling loot | done | Sapling net-trap swamp/orange/red/black; baby jar Loot weighted table; `::hunternet`/`::hunterbabyimp`; scripts 10296; pack 0 errors |
| 2f | skill_hunter: polar kebbit trails | pending | Polar kebbit tracking trails (deferred from 2d) |
| 21 | quest: Priest in Peril / Nature Spirit | lc | LC has `quest_priestperil` + `quest_druidspirit` — port via CONTENT_PORT_QUEUE |
| 22 | quest: Recruitment Drive | done | Start: Amik→Tiffy (`rd_teleporter_guy`)→grounds (`m38_77`); quit portals; `%rd_main`; puzzles/shuffle deferred; `::rd`; scripts 5278; pack 0 errors |
| 23 | quest: Lost Tribe | done | Sigmund→cook→Duke permit; dig/brooch→23b; Mistag/HAM deferred; `::losttribe`; scripts 5299; pack 0 errors |
| 23b | quest: Lost Tribe dig/brooch | done | Pickaxe on `lost_tribe_cellar_hole_blocking`→`%lost_tribe_quest`=4 + floor brooch; Squeeze-through; Duke brooch→`%lost_tribe_contact`=3 librarian; Reldo/Mistag deferred; `::losttribedig`; scripts 6822; pack preexisting ascentofarceuus dupes |
| 23c | quest: Lost Tribe Reldo/book | done | Reldo brooch→`%lost_tribe_bookmark`=1; bookcase→book; Read→Dorgeshuun ID (`bookmark`=2); Duke/Sigmund symbol talk; generals/Mistag deferred; book IF page-turn deferred; `::losttribebook`; scripts 6915; pack 0 errors |
| 23d | quest: Lost Tribe goblin generals | done | Wartface/Bentnoze Dorgeshuun briefing → emotes + `%lost_tribe_quest`=7; Duke/Sigmund war prep → `%lost_tribe_bookmark`=3; Mistag/HAM→23e; `::losttribegenerals`; scripts 7047; pack 0 errors |
| 23e | quest: Lost Tribe Mistag/HAM | done | Goblin bow→Mistag peace (`%lost_tribe_quest`=8); Duke silverware→9; pickpocket key→HAM robes/`%lost_tribe_ham`=1; crate silverware=3; Duke expose→treaty/`quest`=10; Mistag delivery/cutscene→23f; `::losttribemistag`/`::losttribeham`; scripts 7148; pack 0 errors |
| 23f | quest: Lost Tribe treaty/finish | done | Mistag treaty → soft-skip Ur-tag signing (dialogue); Mining 3000 XP + `ring_of_life` + quest=11; Kazgar/Mistag Follow shortcuts; camera cutscene deferred; `::losttribefinish`; scripts 7208; pack 0 errors |
| 24 | quest: Dig Site / The Golem | done | Dig Site → LC `quest_itexam` (CONTENT_PORT_QUEUE **18d**); Golem: talk+softclay×4 repair→task; portal/museum deferred; `::golem`; scripts 5326; pack 0 errors |
| 25 | quest: Animal Magnetism | done | Ava start (prereqs+skills) → fetch chickens; `%anma_main`; farm/witch/device deferred; `::anma`; scripts 5359; pack 0 errors |
| 26 | quest: A Soul's Bane | done | Launa start → rope on rift → enter anger; `%soulbane_prog`/`%soulbane_riftrope_pres`; rooms deferred; `::soulsbane`; scripts 5437; pack 0 errors |
| 27 | quest: Creature of Fenkenstrain | done | Signpost+interview (Braindead/Grave-digging) → hire/`%creatureoffenkenstrain`=2; graves/parts deferred; `::fenkenstrain`; scripts 5461; pack 0 errors |
| 28 | quest: Icthlarin's Little Helper | done | Wanderer+cat+tinderbox+waterskin(4) → hypnosis/`%ics_little_var`=2 Sophanem; pyramid deferred; `::icthlarin`; scripts 5487; pack 0 errors |
| 29 | quest: Shadow of the Storm | done | Reen→Badden infiltrate brief; `%agrith_quest`; dye/ritual/Agrith-Naar deferred; `::shadowstorm`; scripts 5515; pack 0 errors |
| 30 | quest: What Lies Below | done | Rat Burgiss start → empty folder/`%surok_quest`=10; outlaw papers/Surok deferred; `::whatliesbelow`; scripts 5564; pack 0 errors |
| 31 | quest: Tears of Guthix | done | Juna story → chisel `tog_stone` → bowl turn-in complete; `%tog_juna_bowl`; lantern/minigame deferred; `::tearsofguthix`; scripts 5590; pack 0 errors |
| 32 | quest: Rag and Bone Man | done | Odd Old Man start → `%rag_quest`=1; vinegar/boiler/bones deferred; `::ragandboneman`; scripts 5633; pack 0 errors |
| 33 | quest: Desert Treasure | done | Split: 33a Asgarnia+Terry→Bandit (`%deserttreasure`≤5); diamonds/Eblis deferred; Dig Site `%itexam` gate deferred (LC); `::deserttreasure`; scripts 5682; pack 0 errors |
| 33b | quest: Desert Treasure Bandit/Eblis | done | Bartender brew+diamonds rumour→6; Eblis ask→gather mirrors=7; materials/mirrors deferred; `::deserttreasure`→bandit; scripts 5692; pack 0 errors |
| 33c | quest: Desert Treasure mirrors | done | Use-with materials → `%fd_*` counts; talk → `%deserttreasure`=10 + `%fd_mirror_present`; mirrors Eblis brief; diamonds deferred; scripts 5713; pack 0 errors |
| 33d | quest: Desert Treasure Blood start | done | Malak bargain → `%dt_blood_stage`=2 Dessous plan; Ruantun/pot/Dessous fight deferred; `::deserttreasure`→Canifis; scripts 5738; pack 0 errors |
| 33e | quest: Desert Treasure Blood pot | done | Ruantun `silver_bar`→`fd_silver_pot`; Entrana bless; Malak fills blood (−5hp); garlic/Dessous deferred; scripts 5752; pack 0 errors |
| 33f | quest: Desert Treasure Blood season | done | Pestle garlic→`fd_crushed_garlic`; blood pot + garlic/spice→`fd_silver_pot_blood_garlic_spiced_blessed`; Dessous deferred; scripts 5813; pack 0 errors |
| 33g | quest: Desert Treasure Blood finish | done | Pour pot on `vampire_big_grave_noblood` → spawn Dessous; kill→Malak `fd_blood_diamond`; smoke/ice/shadow deferred; scripts 5817; pack 0 errors |
| 33h | quest: Desert Treasure Smoke diamond | done | Torches→`fd_firekey`→Fareed→`fd_diamond_fire`/`%dt_smoke_stage`=100; ice gloves/heat deferred; ice/shadow→33i; `::deserttreasure`→smoke; scripts 5935; pack 0 errors |
| 33i | quest: Desert Treasure Ice diamond | done | Cake→child→ice gate→Kamil→smash parents→`fd_icediamond`/`%dt_ice_stage`=100; spiked boots/path trip deferred; shadow/pyramid→33j; scripts 5995; pack 0 errors |
| 33j | quest: Desert Treasure Shadow diamond | done | Rasolo→chest pick→`fd_sword_cross`→`fd_ring_visibility`→Damis×2→`fd_dark_diamond`; poison-on-fail/unequip ladder deferred; pyramid→33k; scripts 6041; pack 0 errors |
| 33k | quest: Desert Treasure pyramid finish | done | Obelisks→`%fd_column_*`→`^dt_pyramid`; doors; `azzanadra_real`→complete+Magic XP; ancient book/maze deferred; `::deserttreasure`→Azzanadra; scripts 6058; pack 0 errors |
| 34 | quest: Zogre Flesh Eaters | done | Grish start → `%zogre`=1 + chompy/restore gifts; barricade/crypt/Sithik deferred; `::zogre`; scripts 5882; pack 0 errors |
| 34b | quest: Zogre barricade/crypt | done | Guard→`%thzfe_blocking_barricade`/`%zogre`=2; climb; stairs; coffin/knife→prism; lectern page; Brentle→backpack/tankard; Sithik/Slash Bash→34c; `::zogre`; scripts 6416; pack 0 errors |
| 34c | quest: Zogre Sithik / Slash Bash | done | Zavistic prism+page→3; Sithik evidence+bartender→potion→tea→transform; Grish key; Slash Bash→artifact→complete+XP; Relicym/brutal deferred; `::zogre`→bell; scripts 6477; pack 0 errors |
| 35 | skill_agility: Barbarian / Wilderness courses | → lc | LC has `barbarian_course.rs2` + `wilderness_course.rs2`; deferred from CONTENT_PORT_QUEUE 8s — leave on LC lane |

## Opcode gap log

Record new Server VM opcodes **before** inventing C content hooks. Format:
`slice | opcode | why content needs it | status`.

| Slice | Opcode / surface | Why | Status |
|---|---|---|---|
| 1a | softtimer (2109) | Patch growth + offline catchup | used in 1b |
| 1a | `%varbit` / perm scope | Per-patch state | done — carriers varp_501..516/830/… scope=perm+transmit; helpers switch on loc |
| 1b | `date_minutes` (4629) | Offline growth deadlines | done — host op in `mock230_scripts.c` (LC NumberOps wall-clock minutes) |
| 1g | `runclientscript*` / farming_view_setpanel | Patch grid populate | done — `~farming_view_refresh` drives 1119 from sim; Geomancy access still deferred |
| 4a | instance / dynamic map | POH | done — see the row below; measured, not blocking |
| 4a | `map_instance_alloc/setchunk/build/coord/free/find` (11009..11014) | HouseManager.enter/construct (2009scape) | **done** — full slice: measured free pool (2,934 squares all in map x 15..98, so x ≥ 100 is the pool), server registry + instanced collision build, `REBUILD_REGION` (wire 59) encode **and** client decode + per-zone terrain/scenery rebuild with rotation, six EXTRA-band ops, content helpers. LC still has none of it (`engine.rs2` declares no map-alloc command), so these are engine-only rather than ports — [`map_instances.md`](map_instances.md). Proved by `::mapinstance` / `::mapinstance_turn` in the headless client |
| 4b | (same) + hotspot build IF | BuildHotspot / BuildOptionPlugin | **done** for this lane (garden Build/Remove + XP); further IF/rooms owned elsewhere — hands off |
| 6 | `DynamicRegion.create(10536)` | PestControlActivityPlugin.start / PestControlSession | **done** — `~map_instance_from_square(^pest_island_template)` voyage + portal/knight/squire spawn; lander softtimer launches; Leave on `pest_squire_instance` |
| 9 | `DynamicRegion` (Telekinetic maze) | TelekineticZone.start / private maze instance | unblocked by 4a — a maze is per-zone `map_instance_setchunk`; Enchant/Alchem/Grave static rooms already entered without it |
| — | the eleven declared-but-unhosted ops named across all four queues | Clearing every "deferred: *opcode*" in the queues at once rather than one slice at a time | **done** — `busy` (2005), `p_opnpct` (2081), `projanim_pl` (2095), `set_player_op` (2103), `stat_add` (2113), `npc_sethuntmode` (2535), `npc_statsub` (2541), `projanim_npc` (2547), `obj_find` (3505), `inv_dropitem_delayed` (4310), `map_multiway` (1015). All eleven are LostCity `engine.rs2` commands, so each is a port and not a design. Coverage 297 → 308 of 419 declared. See the log entry below for what each needed and how it was measured |
| 19e | `npc_setrespawn` / multi-AOE | Align dead minion respawn; chamber multi-hit | **done** in 19f — `npc_setrespawn` (11015); multi-hit via existing `huntall`/`huntnext` (not a new op) |

## Log

- slice 2e done (salamander net + baby jar loot): LC none; 2009scape NetTrapSetting/NetTrapNode + ImplingLoot.BABY; Set-trap on `hunting_sapling_up_*` needs `net`+`rope`; catch swamp/orange/red/black → salamander item; baby jar `opheld3` weighted loot + empty jar return; secondary net scenery + clues deferred; `::hunternet`/`::hunterbabyimp`; scripts 10296; pack 0 errors; next=2f polar kebbit trails
- slice 13c done (BF breakage/belt): LC none; 2009scape BlastState/BFSceneryController/BFBeltOre; 1/50 pot|pump while pumping, belt then cog while pedaling; furnace heat gated on no breakage; Repair Crafting30+hammer 50xp; stove low/med/full loc_change; ore place→4-slot FIFO + `blast_furnace_*_ore` visuals; pedals advance/deposit; `::bfbreak`/`::bfbelt`; scripts 10177; pack 0 errors; next=2e salamander net + impling loot
- slice 8d done (Puro imp steal): LC none; 2009scape ImpDefenderBehavior; `imp_defender_no_patrol` ai_timer 1/10 within 2 tiles; Thieving avoid (low35/high280 +20 repellent); steals lowest `ii_captured_impling_*` → `npc_say("Be free!")` + empty `ii_impling_jar` ground; cooldown 25–100t; `::puroimp`; scripts 9928; pack 0 errors; next=13c BF breakage/belt
- slice 8c done (Puro storage/scroll): LC none; scroll Toggle-view maze-only → `ii_tracker` + CS2 1327 counts from jarred inv; Elnock Quick-withdraw opens `ii_elnock_storage`/`_side` with `%ii_stored_net`/`repellent`/`impling_jars` (enum_2850 caps); storage is post-2009 cache IF wired here; `::puroscroll`/`::purostore`; scripts 9867; pack 0 errors; next=8d Puro imp steal
- slice 7c done (PP snake charm): LC none; Check for Snakes → `%ntk_urn*_state`=2; Charm Snake needs `snake_flute` →=3; Search closed/snake hard roll (room×4), charmed easier (room×2) + 66% XP; bite 1–4 + poison; slot via multi parent `loc_type`; `::ntkcharm`; scripts 9739; pack 0 errors; next=8c Puro-Puro storage/scroll
- slice 6h done (pest barricade/splatter/shifter): LC none; ravager seeks 2009scape OBJECT_OFFSETS smashables (`loc_change` damage chain); splatter detonates beside barricade + on `ai_queue3` (GFX `splatter_exploding_spotanim*`, blast `huntall`/`npc_huntall`); shifter teleports beside hero when >5 tiles (`shifter_teleport_spotanim` + `pest_shifter_teleportattack`); scripts 9686; pack 0 errors; next=7c Pyramid Plunder snake charm
- slice 3f done (slayer mirror/gargoyle/banshee): LC none; basilisk/cockatrice need `slayer_mirror_shield` else 0 player dmg + NPC force-11 + 25% combat drains; banshee need earmuffs/helm else 0 player dmg + force-8 + 50% drains + 40% stun; gargoyle finisher `slayer_rock_hammer` HP≤10; hooks in `slayer_cap_finish_damage` / `slayer_on_npc_hit_player` / `slayer_after_player_hit`; `::slayergarg`/`::slayermirror`/`::slayerbanshee`; scripts 9636; pack 0 errors; next=6h pest AI specials
- slice 20c done (mole mud extinguish): LC none; 2009scape `GiantMoleNPC.splatterMud` + `LightSource.open`; burrow `huntall` → extinguish open candle/torch/oil lamp (`inv_del`+`inv_add`); closed lanterns skip; no light → `if_openoverlay(darkness_medium)` (cache IF 226 is `deadmanprotect`, not mud splash); `::molemud`; scripts 9576; pack 0 errors; next=3f slayer mirror/gargoyle/banshee
- slice 19f done (GWD multi-AOE + minion respawn sync): LC none for setRespawnTick; EXTRA `npc_setrespawn` (11015) + death-path keeps pre-armed clock; minion `ai_queue3` sync when boss absent; Graardor/Kree/Zilyana specials `huntall` chamber AOE; scripts 9545; pack 0 errors
- slice 19e done (GWD chamber AOE gates + minion assist): LC none; chamber SW/NE from 2009scape ZoneBorders; specials cancel if player left; Zilyana magic x<2908; minion `ai_spawn`/`ai_timer` assist via boss `npc_find`+mode; `npc_setrespawn` + `player_findallzone` →19f/gap; scripts 9424; pack 0 errors
- slice 19d done (GWD drops+godsword): LC none; `godwars_drops.rs2` boss `ai_queue3` (~7/508 uniques + shared main) + 12 minion drops; `godwars_godsword.rs2` shard combine + hilt attach (bind single-shard/hilt subjects — `blade1+2` style names need tight `+` lex, no `opheldu` on `+digit` headers); `::gwdsword`; minion sync/AOE/RDT→19e; scripts 9195; pack 0 errors
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
- slice 4a/4b were blocked on map-instance opcodes (superseded — surface landed; content 4a done below). Historical note kept for chronology. Next at the time = 5 Barrows
- slice 5 done (crypt core): dig on mounds → crypt teleport; stairs → mound; sarcophagus Search spawns brother + kill varbits on `barrows_kills`; tunnel entrance / reward chest / prayer drain / overlay IF / puzzle deferred; `::barrowscrypt`; scripts 4936; pack 0 errors; next = 6 Pest Control
- slice 5b done (tunnel+chest): designated crypt sarcophagus → tunnel dialogue; catacombs tele; `barrows_stone_chest` Open (spawn tunnel brother) / Search (weighted rewards); ladders→crypt; prayer drain softtimer stub; puzzle doors / overlay IF deferred; `::barrowstunnel`; scripts 7266; pack 0 errors
- slice 6 done (lander only): pest lander Cross/Climb for novice/intermediate/veteran (combat 40/70/100); wait softtimer; island voyage blocked on `DynamicRegion.create(10536)` (opcode gap, same as POH); bots skipped; reward shop / Port Sarim sail deferred; `::pestlander`; scripts 4976; pack 0 errors; next = 7 Pyramid Plunder
- slice 6b done (island voyage): lander wait → `~map_instance_from_square(^pest_island_template)` (region 10536); spawn 4 shielded portals + void knight + `pest_squire_instance`; Leave returns to boat pier; `::pestisland`; shield-drop timer / zeal overlay / pest waves / rewards deferred; scripts 6678; pack 3 errors (preexisting ascentofarceuus.constant dups elsewhere); next = deferred arms or blocked 15–17
- slice 6c done (shield/zeal): `pest_status_overlay` via `if_opensub`; shuffled drop order; softtimers 50/100; shield→open portal `npc_changetype`; `%pest_zeal`/`~pest_add_zeal`; `~pest_waves_stub`; leave clears; `::pestzeal`; scripts 7365; pack 0 errors; next = 6d waves/rewards
- slice 6d done (waves/win-lose/rewards): softtimer waves from portals; portal `ai_queue3`→win; void knight death→lose; clock 0→win; zeal≥50 awards `%pest_points` (cap 500) + coins×cmb; combat `~pest_on_damage` zeal; `::pestwin`/`::pestlose`; shop/AI deferred→6e; scripts 7489; pack 0 errors
- slice 6e done (reward shop): Exchange/`opnpc3` on `pest_voidknight_1..4` opens `pest_rewardshop`; `%if1`↔`%pest_points`; confirm `last_slot`→XP (enum_2286)/packs/void gear; Talk-to exchange option; `::pestshop`/`::pestpoints`; Port Sarim/AI→6f; scripts 7532; pack 0 errors
- slice 6f done (Port Sarim sail): `pest_squire_ship_portsarim`↔`pest_squire_ship_island` Talk/Travel; `ship_journey` + `%journey_number` 14/15 + 12t delay; docks measured from 2009scape Ships.java; jingle deferred; `::pestsarim`/`::pestoutpost`; pest AI deferred; next=5c barrows puzzle; scripts 7584; pack 0 errors
- slice 5c done (puzzle/overlay): LC none; `barrows_overlay` on tunnel enter/leave; unlocked doors near chest → `barrows_puzzle` (4 shape sets, pack `model_6713`..`6736`); wrong answer shuffles `%barrows` (TUNNEL_CONFIGS + wholewrite); solved gates walk-through; IF comps renamed `puzzle_q0..2` (digit names unlexable); `::barrowspuzzle`; scripts 7789; pack 0 errors; next=6g pest AI
- slice 6g done (pest AI): LC none; spawn `~pest_ai_arm` + category timers; idle pests `npc_walk`+damage Void Knight; spinners heal nearest open portal (~10% max); zeal on any in-game pest/portal hit; barricade smash / splatter explode / shifter tele deferred; scripts 7914; pack 0 errors; next=7b Pyramid Plunder rooms
- slice 7b done (PP rooms): LC none; `ntk_overlay` on join; spear Pass (room thieving gate); tomb Pick-lock→rooms 2–8 + dead-end doors; sarcophagus Strength push + mummy/sceptre/artifact; grand gold chest Search + swarm; room urn XP/artifacts; snake charm/check deferred; `::ntkplunder`/`::ntkroom`; scripts 8056; pack 0 errors; next=8b Puro-Puro crop-circle/Elnock
- slice 8b done (circles/wilt/Elnock): LC Imp Catcher only; rotating `ii_magic_wheat_m`+ring via map_clock/1500; maze wilt softtimer on moveable wheat; Elnock Talk/Trade/Exchange + free supplies (`%ii_elnock_given_freestuff`) + jar sale + jar generator (`%puro_jar_charges`); storage IF/scroll overlay deferred; `::purocircle`; scripts 8203; pack 0 errors; next=13b Blast Furnace pump/ore
- slice 13b done (BF machine): LC none; pump Str30 / pedals Agi30 / coke Collect+stove Refuel FM30 / belt Put-ore+use-with / gauge IF / sink / dispenser cool+Take; furnace softtimer smelts @51–66°; breakage+belt NPCs deferred; `::bfmachine`; scripts 8400; pack 0 errors; next=19b GWD KC/bosses
- slice 19b done (KC/doors/altars): LC none; `godwars_overlay` KC text; `~gwd_kill_credit` from `npc_default_death`; Open on `godwars_dungeon_door_normal`/`_private` (faction via map door coords); deduct 40 KC; altar Pray CD + Teleport outs; boss AI→19c; `::gwdkc`/`::gwdboss`; scripts 8562; pack 0 errors; next=20b Giant Mole AI/drops
- slice 20b done (mole AI/drops/Wyson): LC none; `~mole_on_damage` dig 24% when HP 6–99 → burrow anim+`npc_tele` 12 dig tiles; `ai_queue3` always claw/skin/big bones + main; Wyson nest trade (`bird_nest_cheapseeds`/ring/eggs); mud extinguish deferred; `::moleboss`; scripts 8728; pack 0 errors; next=19c GWD boss AI
- slice 19c done (boss AI + shortcuts): LC none; `ai_opplayer2` style mix for 4 avatars + battle cries; aviansie melee gate; ice bridge Cross HP70 (+prayer drain north); Bandos `godwars_icecave_bandos_door` bang Str70+hammer; grapple pillar Ranged70+mithril grapple+crossbow; multi-AOE/minion sync/drops→19d; scripts 9032; pack 0 errors; next=19d GWD drops/minions
- slice 23b done (dig/brooch): pickaxe on cellar rubble (Mining 13) → `%lost_tribe_quest`=4 + `obj_add` brooch; Squeeze-through hole; Duke+Sigmund brooch dialogue → `%lost_tribe_contact`=3 (librarian); Look-at → `brooch_closeup`; Reldo/book/goblins/Mistag/HAM deferred; `::losttribedig`; scripts 6822; transcript https://oldschool.runescape.wiki/w/Transcript:The_Lost_Tribe
- slice 23c done (Reldo/book): Reldo brooch option → `%lost_tribe_bookmark`=1; Search `lost_tribe_bookcase` → book; Read opens `lost_tribe_symbol_book` + Dorgeshuun recognition (`bookmark`=2); Duke/Sigmund "I found out about the symbol..."; page-turn IF / generals / Mistag deferred; `::losttribebook`; scripts 6915; pack 0 errors
- slice 23d done (goblin generals): Wartface/Bentnoze Dorgeshuun briefing → goblin bow/salute via `setbit(%emote_access,…)` + `%lost_tribe_quest`=7; Duke/Sigmund war-prep → `%lost_tribe_bookmark`=3; journal; `::losttribegenerals` → Goblin Village WP; Mistag/HAM→23e; scripts 7047; pack 0 errors
- slice 23e done (Mistag/HAM): Goblin bow near `lost_tribe_mistag_1op` → peace talk → quest=8; Duke silverware demand→9; Pickpocket Sigmund→`lost_tribe_chest_key`; Open `lost_tribe_chest`→HAM set/`%lost_tribe_ham`=1; Pick-lock `osf_trapdoor_closed` + Search `lost_tribe_crate`→silverware=3; Duke expose Sigmund→`lost_tribe_treaty`/quest=10; Mistag delivery/cutscene→23f; `::losttribemistag`/`::losttribeham`; scripts 7148; pack 0 errors
- slice 23f done (treaty/finish): Mistag + treaty → soft-skip Ur-tag signing dialogue at Lumbridge courtyard; `%lost_tribe_quest`=11 + Mining 3000 XP + `ring_of_life`; Mistag/Kazgar Follow shortcuts; full cam/door cutscene deferred; `::losttribefinish`; scripts 7208; pack 0 errors
- slice 7 done (entrance + room1): Tarik stub unlocks doors; Search N/E/S/W → guardian vs empty; mummy Talk/Start → room1 + softtimer (500 ticks); leave tomb / timer expel; room1 closed urns → ivory comb or poison bite; spears / room doors 2–8 / sarcophagus / chest / `ntk_overlay` IF deferred; `::ntkplunder`; scripts 5028; pack 0 errors; next = 8 Puro-Puro
- slice 8 done (enter/leave + wheat): Zanaris `ii_magic_wheat_m_zanaris` Enter → maze; exit portal returns to saved tile; Push-through wheat (Hunter 17, imp-box gate); Fairy Aeryka + Elnock talk stubs; rotating Gielinor crop circles / wilt pulse / Elnock shop / jar gen deferred; `::puropuro`/`::puromaze`; scripts 5075; pack 0 errors; next = 9 Mage Training Arena
- slice 9 done (lobby + static rooms): temple door Magic 7; Entrance Guardian → `%magictraining_entra_noob` + Progress hat; Enchant/Alchem/Grave portals (magic + item gates) enter/leave via `magictraining_returndoor`; Telekinetic blocked on DynamicRegion; shop/scoring/room gameplay deferred; `::mta`; scripts 5108; pack 0 errors; next = 10 Fishing Trawler
- slice 9 unblocked (full rooms + shop): LC none (2009scape `content/minigame/mta`); Telekinetic mazes via `map_instance_from_square(52_135)` + telegrab statue slide; Alchem cupboard/cost pulse + alchemy hook; Enchant shapes→orb + bonus pulse + enchant hook (75% XP); Grave bones + convert_bones; Rewards Guardian dialogue shop (wand upgrades / bones-to-peaches); `.rs2.skip` / `_mta_scripts_wip_skip` removed; `::mta`/`::mta_tele`/`::mta_points`; scripts 6273; pack 0 errors; later: Observe/bone-cycle/healenergy/GE Collect (see "slice 9 MTA deferred")
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
- slice 26 done (Launa+rope+enter): `soulbane_launa` → `%soulbane_prog`=1; Use `rope` on falloffs → `%soulbane_riftrope_pres`; Enter → anger `m47_81`; climb-up/`void_exit` → surface; rooms/Tolna deferred; `::soulsbane`; scripts 5437; pack 0 errors; next = 27 Creature of Fenkenstrain
- slice 27 done (sign+hire): `fenk_signpost` Read (Craft 20/Thieve 25/PiP gate/Restless) → `%creatureoffenkenstrain`=1; interview Braindead+Grave-digging →=2 fetch parts; graves/sewing/lightning/creature/Charos deferred; `::fenkenstrain`; scripts 5461; pack 0 errors; next = 28 Icthlarin's Little Helper
- slice 28 done (Wanderer start): cat (inv proxy) → supplies → hypnosis takes tinderbox+`water_skin4`, gives canopic jar, `%ics_little_var`=2 + Sophanem tele; open rock Enter; pyramid/embalm/sphinx deferred; 2009scape stub so wiki/cache; `::icthlarin`; scripts 5487; pack 0 errors; next = 29 Shadow of the Storm
- slice 29 done (Reen+Badden): Demon Slayer+Golem → `agrith_reen` gives Silverlight if needed → `%agrith_quest`=1 Uzer; Badden infiltrate brief →=2; dye/dungeon/ritual deferred; 2009scape dye-only stub so wiki/cache; `::shadowstorm`; scripts 5515; pack 0 errors; next = 30 What Lies Below
- slice 30 done (Rat start): RC 35+Rune Mysteries → `surok_rat` gives `surok_rat_emptyfolder`, `%surok_quest`=10 collect papers; outlaw drops/Surok/Zaff/Roald deferred; `::whatliesbelow`; scripts 5564; pack 0 errors; next = 31 Tears of Guthix
- slice 31 done (Juna+bowl): QP43/FM49/Craft20/Mine20 → story → `%tog_juna_bowl`=1; chisel `tog_stone`→`tog_bowl` → turn-in complete=2 + Craft XP; sapphire lantern/light-creature/minigame deferred; `::tearsofguthix`; scripts 5590; pack 0 errors; next = 32 Rag and Bone Man
- slice 32 done (Odd Old Man start): LC none; `rag_odd_old_man` → `%rag_quest`=1 collect/polish list; journal `quest_ragandboneman1`; vinegar/boiler/bone drops/wishlist (II) deferred; `::ragandboneman`; scripts 5633; pack 0 errors; next = 33 Desert Treasure (split)
- slice 33a done (Asgarnia+Terry): LC none (Tourist Trap=`quest_desertrescue`); `fourdiamonds_indiana_vis` start (skills+Tourist/Ikov/PiP/Waterfall/Troll) → etchings/`%deserttreasure`=1; `archaeological_expert` → translation → return → bandit=5; Dig Site `%itexam` gate deferred (LC CONTENT_PORT_QUEUE); Eblis/diamonds→33b; `::deserttreasure`; scripts 5682; pack 0 errors; next = 33b or 34 Zogre
- slice 33b done (Bandit+Eblis ask): `fourdiamonds_bartender` 650gp→`bandit_brew`/`%dt_bought_beer` → four diamonds rumour=`%deserttreasure`=6; `fd_elder_village` → gather mirrors=7; material hand-in/mirrors/diamonds→33c; `::deserttreasure`→bandit; scripts 5692; pack 0 errors; next = 33c or 34 Zogre
- slice 33c done (materials+mirrors): use-with on Eblis fills `%fd_magiclog`/`%fd_steelbar`/`%fd_glass`/`%fd_bones`/`%fd_ash`/`%fd_charcoal`/`%fd_bloodrune`; talk→`%deserttreasure`=10 + `%fd_mirror_present`; `fd_elder_by_mirrors` brief; diamond bosses→33d; scripts 5713; pack 0 errors; next = 33d or 34 Zogre
- slice 33d done (Malak Blood start): `fourdiamonds_vampire_lord` bargain→`%dt_blood_stage`=2; Ruantun pot/Entrana/garlic/Dessous/`fd_blood_diamond` deferred; smoke/ice/shadow→33e; `::deserttreasure`→Canifis; scripts 5738; pack 0 errors; next = 33e or 34 Zogre
- slice 33e done (Blood pot chain): `malak` (Ruantun) silver bar→`fd_silver_pot`; `high_priest_of_entrana` bless→`fd_silver_pot_blessed`; Malak fill→`fd_silver_pot_blood(_blessed)` + 5 dmg; garlic/spice/Dessous→33f; `::deserttreasure`→sewers; scripts 5752; pack 0 errors; next = 33f or 34 Zogre
- slice 33f done (Blood season): grind garlic→`fd_crushed_garlic`; combine blood pots with garlic/`spicespot` to `fd_silver_pot_blood_garlic_spiced(_blessed)`; Dessous tomb→33g; scripts 5813; pack 0 errors; next = 33g or 34 Zogre
- slice 33g done (Dessous+Blood diamond): oplocu `vampire_big_grave_noblood` with seasoned blessed pot → `blooddiamond_vampirewarrior`; death→`%dt_blood_stage`=3; Malak→`fd_blood_diamond`/100; smoke/ice/shadow→33h; scripts 5817; pack 0 errors; next = 33h or 34 Zogre
- slice 34 done (Grish start): LC none; `zogre_ogre_shaman` (Jungle Potion+Chompy+Ranged30/Smith4/Herb8) → sickies talk → `%zogre`=1 + 3×`cooked_chompy` + 2×`3dose2restore`; journal `quest_zogreflesheaters`; barricade/crypt/Sithik→34b; `::zogre`; scripts 5882; pack 0 errors; next = 34b or 33h DT diamonds
- slice 33h done (Smoke diamond): LC none; tinderbox×4 `4d_standing_torch*` (FM50/`%fd_torch_count*`) → `fd_firedungeon_shutchest`/`fd_firekey` → `fd_fw_metalgateclosed_*` → `firediamond_firewarrior` → `fd_diamond_fire`/`%dt_smoke_stage`=100; ice gloves/weapon heat deferred; ice/shadow/pyramid→33i; `::deserttreasure`→smoke chest; scripts 5935; pack 0 errors; next = 33i or 34b Zogre
- slice 33i done (Ice diamond): LC none; `chocolate_cake`→`fourdiamonds_troll_child_*` → `%dt_ice_stage`=2; `icegate_*`→`icediamond_icewarrior`→3; smash `fd_trollblock*`→`fd_icediamond`/100; spiked boots/ledge/path trip deferred; shadow/pyramid→33j; `::deserttreasure`→troll child; scripts 5995; pack 0 errors; next = 33j or 34b Zogre
- slice 33j done (Shadow diamond): LC none; `shadow_warrior_rasool`→`%dt_shadow_stage`=1; `fd_bandit_shutchest`+`lockpick`→`fd_sword_cross`/2; trade→`fd_ring_visibility`/3 + `%fd_ladder_present`; `fd_damis_normal`→`fd_damis_tougher`→`fd_dark_diamond`/100; chest poison/ring unequip deferred; pyramid→33k; `::deserttreasure`→Rasolo; scripts 6041; pack 0 errors; next = 33k or 34b Zogre
- slice 33k done (pyramid finish): LC none; `desert_treasure_oblix_*` + matching diamond → `%fd_column_blood/fire/ice/shadow`; all four → `%deserttreasure`=`^dt_pyramid`; `four_diamonds_door_*` enter; `azzanadra_real` →=15 + Magic 20000 XP + `~quest_complete`; ancient spellbook unlock / pyramid maze deferred; `::deserttreasure`→Azzanadra; scripts 6058; pack 0 errors; next = 34b Zogre or 35 Agility
- slice 34b done (barricade+crypt clues): LC none; `zogre_ogre_guard`→`%thzfe_blocking_barricade`/`%zogre`=`^zfe_crypt`; climb `ogre_barricade_collapsed*`; stairs; coffin/knife→prism; lectern→page; Brentle→backpack/tankard; Sithik/Slash Bash→34c; `::zogre`; scripts 6416; pack 0 errors; next = 34c or 4b or 35
- slice 34c done (Sithik/Slash Bash finish): LC none; bell/`zogre_human_zavistic_rarve` prism+page→3; `ogre_bedman_loc`→4; drawers/cupboard/wardrobe + portrait + `dragon_bartender` → potion; tea pour→6; `yanillestairsup`→`%thzfe_sithik_transformed`/7; Grish key→8; `zogre_stand`/`zogre_slash_bash`→artifact/9; return→14 + Ranged/Fletching/Herblore 2000; Relicym/brutal arrows deferred; `::zogre`→bell; scripts 6477; pack 0 errors; next = 4b POH hotspot or 35 Agility
- slice 35 → lc: LostCity already has `skill_agility` barbarian + wilderness courses (deferred from CONTENT_PORT_QUEUE 8s); next SCAPE2009 pending = 4b POH hotspot build IF
- slice 9 wiki interfaces+rooms: cache IFs `magictraining_{alchem,encha,grave,tele,shop,main}` (194–198,553) + CS2 `magic_training_*`; lobby HUD + room overlays; shop IF via `%if1..4`+`enum_2753`; full guardian dialogues; May 2024 caps/hat-optional/enchant+grave+alch+tele scoring; scripts 6794; pack 0 errors; deferred pieces landed below
- slice 9 MTA deferred: Observe (`cam_moveto`/`lookat`/`reset`/`shake` hosted — RSProt 30/67/65/107); bone-pile cycle (`loc_change` after 4 grabs); `healenergy(10000)` on 5-maze streak; bankchest Collect→`if_openmain(ge_collect)`; scripts 6992; coverage 312/419; pack 0 errors
- slice 9 MTA graveyard bones: hosted `spotanim_map` → MAP_ANIM zone sub (wire 124; client already spawned MapSpotAnim); hazard sprays 2009scape `GFX_POS` as `1_52_150_*` with bone_drop1–4; NPC pool init no longer ghost-fills on tick 0
- slice 9 MTA item escape: strip all room items (inv+worn) on portal leave / login / death; block spell teleports while `%mta_room`; login ejects from mapsquare (52,150); graveyard death −10 points
- slice 9 lobby upstairs: OSRS wiki coords — spawn `magictraining_guard_entrance` 3363,3305,0 + `magictraining_guard_rewards` 3363,3318,1; bind `magictraining_bankchest` Use→`~openbank` (May 2024 chest in cache); Trade-with=`opnpc4`; `::mta_shop`
- 2026-08-04: **policy restated everywhere:** never park/silence sibling lanes
  (`*.skip`, `skill_construction`/`minigame_mta` moves); all port queues +
  PORTING_GUIDE §4.4–§4.6 / §7 + loop prompts + `.cursor/rules/no-park-sibling-content.mdc`
- **MTA is live — do not `.rs2.skip` / park `minigame_mta/` or strip MTA branches from alchemy/telegrab/enchant/convert_bones for other-lane compiles** (2026-08-04)
- slice 9 MTA finished (was parallel WIP): unskipped `minigame_mta/*.rs2`; removed `_mta_scripts_wip_skip/`; enchant/alchemy/telegrab/convert_bones hooks live; see log "slice 9 unblocked"
- slice 9 MTA Rewards Guardian dialogue: wiki [Transcript](https://oldschool.runescape.wiki/w/Transcript:Rewards_Guardian_(Mage_Training_Arena)) — `p_choice4` includes "Got anything else I can buy?" → Arena book (`magictraining_arenalorebook`, 200 coins); Who-are-you loops to menu; bye line restored; LC none
- **POH / `skill_construction/` is live (4a+4b) — do not `.rs2.skip`, rename the directory to `skill_construction.skip`, delete, or park `poh_*.rs2` / `construction.{constant,varp}` for other-lane compiles** (2026-08-04). Parallel agents repeatedly: (1) renamed scripts to `.rs2.skip`, (2) wiped the tree, (3) renamed the **whole directory** to `skill_construction.skip` mid-compile. That drops `::poh` / `::pohbuild` / portal / estate-agent bindings from `script.dat`. If your lane fails to compile, fix your lane — do not silence construction. Style origin is **m29_110** (m29_89 absent). Also in `CLAUDE.md`, `PORTING_GUIDE` §7, `.cursor/rules/no-park-sibling-content.mdc`.
- **map-instance surface done — 4a/4b/6/9 unblocked** (2026-08-04): six EXTRA-band ops 11009..11014 (`alloc`/`setchunk`/`build`/`coord`/`free`/`find`), 1-based handles with 0 = none so a handle survives in a varp. Pool **measured** off the cache's maps table (2,934 squares, all map x 15..98 → x ≥ 100 free, same band Kronos gates on) rather than hardcoded. Server: `mock230_mapinstance.{c,h}` registry + `mock230_scene_build_instance` per-zone collision/loc copy with rotation; `REBUILD_REGION` wire 59 encode. Client was missing the whole decode path — added `PKT_NAME_REBUILD_REGION` + parser, `App_WorldRebuildBegin(force)` (an instance rebuild is same-zone but not same-scene), source-square prefetch, `WorldBuilder_RebuildInstance` per-zone terrain + scenery with rotation. Content: `~map_instance_copy_area/copy_square/from_square`, `%map_instance_handle`, `::mapinstance` / `::mapinstance_turn` / `::mapinstance_leave`. Proved headless: whole-square copy is **1.08% of viewport pixels** off a control at the same tile (NPCs, animated locs, player anim frame — no terrain or static scenery), four turns render as four quarter-turns. Doc [`map_instances.md`](map_instances.md); scripts 5638; pack 0 errors. Known limit: one collision scene per world, so one instance at a time can be walked in. Next was 4a POH enter/leave
- slice 4a done (house enter/leave): LC none; `skill_construction/` — estate agent buy (1k → `%poh_house_location` Rimmington), `~poh_construct` 8×8 collage from style `0_29_110_0_0` (m29_89 absent in this cache), default garden at grid (4,3) level 1, `loc_add(poh_exit_portal)` after tele+`p_delay`, leave frees instance → `0_46_50_9_24`; `::poh`/`::pohleave`/`::pohbuy`; headless enter 4112 locs at 6435,29,1; leave rebuilds Rimmington (client may SIGSEGV after leave — server path ok); scripts 6416; pack 0 errors; next = 4b hotspot build IF. **Do not `.rs2.skip` this tree** (see log line above)
- slice 4b done (garden hotspot build): LC none; `%poh_building_mode` gate; `[oploc5,poh_crude_garden_6/7]` choice→`poh_sapling_plant_*`+watering can→`loc_change(poh_plantbsmall*)` + Construction XP (31/70/100); Remove restores hotspot; `::pohbuild`; scripts 6543; pack 0 errors
- **POH / `skill_construction/` is done for this lane — do not edit, `.rs2.skip`, or extend it from SCAPE2009 ticks** (2026-08-04). Includes 4a enter/leave and 4b garden build. Further furniture-menu / room-editor work is owned elsewhere.
- **opcode gap sweep done — all eleven queue-named unhosted ops hosted** (2026-08-04). Found by cross-referencing the generated coverage header against every queue doc, which is the only honest way to ask the question: a declared-but-unhosted op falls through to the VM's stub, so from content it looks exactly like a hosted op that answers zero. Coverage **297 → 308** of 419 declared. All eleven are LostCity `engine.rs2` commands, so every one is a port:
  - `stat_add` (2113) — the potion. Boost is written to `stat_boosted` only; the level underneath is untouched, so it decays like a boost instead of becoming a permanent drain. Hitpoints also move `hitpoints`, as the reference does.
  - `npc_statsub` (2541) — the drain, as a **delta** (`stat_drain[]`) rather than a rewrite of the authored level, because the authored block is already the base and copying it would give an npc two answers to "what is your defence". Hitpoints route to the existing `hitpoints`/`base_hitpoints` pair instead of a second bookkeeping site.
  - `npc_sethuntmode` (2535) — moved huntmode from the def to the npc. Content turns aggression on and off for one npc at a time (a chompy spawned docile, a gnome baller called off mid-match) and both are the same npc *type* as their calm counterparts, so a def field cannot express it. Seeded explicitly at spawn because 0 is `HUNT_NONE` — taking the memset default would have quietly pacified every aggressive npc in the world.
  - `projanim_pl` (2095) / `projanim_npc` (2547) — every arrow, spell and dragonbreath in the game; `skill_combat/scripts/projectile.rs2` wraps both, so hosting one would have left half the combat scripts casting invisible spells. New `PROJANIM` zone-sub kind; the client already decoded wire 125.
  - `set_player_op` (2103) — server encode plus the one-line client fix that mattered: wire 75 was mapped to `PKT_NAME_NONE`, so the client had been *dropping* these frames.
  - `busy` (2005) — the inverse of the existing `player_can_access`, which already combines delay, modal interface and logout the way the reference's `isDelayed || containsModalInterface()` does.
  - `obj_find` (3505) / `inv_dropitem_delayed` (4310) — against the ground-obj list, with a delayed-drop queue shaped like the existing loc-revert queue. Unblocks the ranged ammo-recovery deferral (slice 8u) and the pickup clears (14m, 16y).
  - `p_opnpct` (2081) — the `t` (cast-at) interaction. The spell rides on the interaction rather than in a `last_*` latch, because a cast is keyed by the **spell** (`[apnpct,magic:wind_strike]`, one script per spell) and the walk can outlive a latch.
  - `map_multiway` (1015) — content-authored data (`maps/multiway.csv`, ported verbatim from the reference: 4,697 zones, coordinates not ids) plus a sorted-zone-set host. Clears the Kronos row that read "opcode exists, no multi map" — the opcode did *not* exist in any useful sense.
  - Verified headless with three new debugprocs in `engine_op_debug.rs2`: `::projanim` renders a bronze arrow in flight (**646 viewport pixels** differ from a control run; the diff mask isolates an arrow shaft on the path south of the player, alongside the expected fountain/NPC/anim noise), `::multiway` reads `here 0, wilderness 2984,3912 1`, `::statadd` reads `after +3+10%: 4 / base 1` on a fresh account — boost moved, base did not. Tests green; `mock230_pack --check-only` 0 errors (15 pre-existing warnings). Also fixed `::mapinstance_leave` to send you home even when the registry has forgotten the reservation, which is the state that most needs a way out, and corrected [`map_instances.md`](map_instances.md)'s reference to a `::tele` debugproc that does not exist. Still open: `cam_shake` — see the gap-log row; it needs a rev-230 wire opcode measured, not a decision
