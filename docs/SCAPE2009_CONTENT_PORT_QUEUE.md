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
| 1d | skill_farming: other herb patches | in_progress | Catherby/Ardougne/Phas + My Arm; mapzones measured from jl2 |
| 1e | skill_farming: compost bins | pending | 2009 `CompostBin` / `CompostBins` |
| 1f | skill_farming: trees / fruit / hops / bushes | pending | Split further if needed |
| 1g | skill_farming: farming_view (179) | pending | Needs patch sim from 1a+; `farming_server_reqs.md` §2 |
| 2a | skill_hunter: bird snare | pending | 2009 `HunterPlugin` / trap lifecycle |
| 2b | skill_hunter: box trap (chins) | pending | |
| 2c | skill_hunter: net trap + implings | pending | Imp net / jar; skip custom rates |
| 2d | skill_hunter: falconry / kebbit tracking | pending | |
| 3a | skill_slayer: Turael Assignment | done | Landed on Kronos lane (cache `slayer_master_task` + `%if1..if6`); cross-check 2009 `SlayerManager` only if policy drifts |
| 3b | skill_slayer: kill credit + points | done | Landed on Kronos lane (`slayer_kill.rs2` + death hook) |
| 3c | skill_slayer: remaining masters | done | Landed on Kronos lane (`slayer_masters.rs2`); combat gates from cache skill_features |
| 3d | skill_slayer: Cancel/Block/Store arms | done | Landed on Kronos lane (`slayer_task_ops.rs2`) |
| 3e | skill_slayer: monster specials | pending | Rock slug / dusty / mirror shield / etc. from 2009 `slayer/` NPCs |
| 4a | skill_construction: house enter/leave | pending | 2009 `House` enter; instance surface may need engine — measure first |
| 4b | skill_construction: build hotspot core | pending | Likely opcode / instance gaps — mark blocked with citations |
| 5 | minigame: Barrows | pending | Brothers + tunnel + reward chest; vanilla loot |
| 6 | minigame: Pest Control | pending | 2009 `pestcontrol/` |
| 7 | minigame: Pyramid Plunder | pending | |
| 8 | minigame: Puro-Puro | pending | |
| 9 | minigame: Mage Training Arena | pending | |
| 10 | minigame: Fishing Trawler | pending | |
| 11 | minigame: Castle Wars | pending | |
| 12 | minigame: Barbarian Assault | pending | |
| 13 | minigame: Blast Furnace | pending | |
| 14 | minigame: Trouble Brewing | pending | |
| 15 | minigame: All Fired Up | pending | Also quest `allfiredup` |
| 16 | activity: Shooting Stars | pending | `global/activity/shootingstar` |
| 17 | activity: Penguin HS | pending | |
| 18 | activity: Treasure Trails (clues) | pending | `ttrail`; F2P-first |
| 19 | region: God Wars dungeon | pending | `trollheim/handlers/gwd`; KC + doors |
| 20 | bosses: Giant Mole | pending | Vanilla |
| 21 | quest: Priest in Peril / Nature Spirit | pending | Only if LC procs absent — grep first; else LC queue |
| 22 | quest: Recruitment Drive | pending | Post-254; OSRS has it |
| 23 | quest: Lost Tribe | pending | |
| 24 | quest: Dig Site / The Golem | pending | Dig Site may be LC `quest_itexam` — grep first |
| 25 | quest: Animal Magnetism | pending | |
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
| 1g | `runclientscript*` / farming_view_setpanel | Patch grid populate | packet exists; caller corpus gap per `farming_server_reqs.md` |
| 4a | instance / dynamic map | POH | measure — may block |

## Log

- queue created (2026-08-04): 2009scape → OSRS-Content lane; custom/non-OSRS skip list; mid-era ownership vs Kronos; first slice = farming patch registry (1a)
- slice 1a done: `skill_farming/` — `farming_patches` dbtable (43 classic rows), `%varbit_*` get/set helpers, carrier varps perm+transmit; Falador herb measured as `varbit_780`; transmit sync + rake/plant → 1b; no new Server VM opcodes; `make mock230-scripts` ok (3855); `mock230_pack --check-only` 0 errors
- slice 1b done: Falador herb rake/plant/harvest (guam..harralander); `%varbit_780`↔`%farming_transmit_d`; growth softtimer + login catchup via new `date_minutes` host op; herb-save roll; disease stub; `make mock230-scripts` ok (3974); pack 0 errors; next = 1c Falador allotment+flower
