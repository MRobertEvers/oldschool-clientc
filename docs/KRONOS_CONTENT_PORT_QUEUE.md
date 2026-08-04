# Kronos content port queue

Agent-loop state for **Kronos184 → OSRS-Content** forward port of **post-254 /
modern OSRS** content that LostCity never had.

LostCity remains the content *shape* (RuneScript triggers, procs, configs).
Kronos (`/Users/matthewevers/Documents/git_repos/Kronos184-Fixed_2`) is the
behaviour / id reference for skills and activities that post-date rev 254.
When Kronos and the osrs239 cache disagree, **the cache wins** for wire and
varp layout; Kronos wins only for *policy* the cache does not state.

Parallel to [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) (LostCity → tree).
Do not steal LC slices that still have a LostCity `.rs2` — finish those on the
LC queue. This queue is for gaps LC cannot fill.

Each tick ports **one** pending unblocked slice per `docs/PORTING_GUIDE.md` §4
and §4.4. Status: `pending` | `in_progress` | `done` | `blocked`.

Loop prompt: read this file + PORTING_GUIDE §4 / §4.4; port the next pending
unblocked slice; verify (`mock230_pack --check-only`, `make -C src mock230-scripts`);
update this file; re-arm. Stop only when the user stops the loop.

## Methodology (non-negotiable)

1. **Grep LostCity first** (`PORTING_GUIDE` §2.2). If LC has the proc, it belongs
   on `CONTENT_PORT_QUEUE`, not here.
2. **No game-facing strings / ids / config constants in C.** Kronos Java is a
   *reference*, not something to re-implement in the engine. Express as
   `.rs2` + configs. New Server VM opcodes only when content cannot say it
   (`PORTING_GUIDE` §2.4 / §2.5) — plan + implement in the same slice.
3. **Resolve names through the pack** — never copy Kronos rev-184 ids into
   osrs239 content.
4. **Skip custom private-server content** (see skip list below). Prefer cache
   dbtable / CS2 contracts over Kronos inventiveness (e.g. do **not** port the
   Edgeville Easy/Medium/Hard/Boss difficulty picker — use per-master task
   tables from the cache).
5. **Interfaces:** drive the rev-230 panel; do not invent IF1. See
   `UI_ERA_PORTING_GUIDE.md` (Kronos answers *wire minimum*, not content shape).

## Skip list (custom / out of scope)

| Kronos path | Why skip |
|---|---|
| `activities/donatorzone`, `loyaltychest`, `appreciationpoints` | donor economy |
| `activities/bossrush`, `summerevent`, `legacytournament`, `partyroom` | custom events |
| `activities/pvp`, `content/activities/tournament`, `content/activities/lms` | custom PvP / LMS |
| `content/areas/wilderness/DeadmanChest*` | Deadman custom |
| `content/items/SkinScrolls`, `model/content/UpgradeMachine`, `PvmPoints`, `CapePerks` | custom cosmetics / meta |
| `skills/BotPrevention` | anti-bot, not content |
| Edgeville `SlayerMaster` Easy/Med/Hard/Boss chooser | custom; use cache `slayer_master_task` |
| `bosses/BrutalLavaDragon`, `Nechryarch`, `eventboss`, `KaalKetJor` | custom bosses |
| `shops` UUID yaml / donation shops | custom shop backend |

## Queue

| # | Slice | Status | Notes |
|---|---|---|---|
| 0 | Queue tracker | done | This file + PORTING_GUIDE §4.4 |
| 1 | skill_slayer: Turael Assignment | done | Schema overlays for `slayer_master_task`/`slayer_task`; `%if1..if6` perm; weighted `~slayer_assign`; opnpc1/3 on Turael; no new opcodes; scripts 3738; pack clean for these files (parallel tree errors unrelated); headless `::talk` currently unrouted (packet name 1) — verify next tick via selftest or fix routing |
| 2 | skill_slayer: kill credit + points | pending | On-death decrement + XP + points; Kronos `Slayer.onNPCKill` policy; wire `%slayer_tasks_completed*` |
| 3 | skill_slayer: remaining masters | pending | Mazchna→Duradel/Konar/Krystilia; combat-level gates from cache; Trade stub until shops |
| 4 | skill_slayer: Cancel/Block/Store arms | pending | Fill `~slayer_confirm` branches once tasks exist (`slayer_rewards.md` §4.2) |
| 5a | skill_farming: patch registry + state | pending | Kronos `Patch`/`PatchData` → content dbtable + perm varbits (`FARMING_PATCH_*`); tools IF already landed |
| 5b | skill_farming: Falador herb (rake/plant/harvest) | pending | First playable patch; growth softtimer; disease stub ok |
| 5c | skill_farming: allotment + flower (Falador) | pending | Protection flower interaction |
| 5d | skill_farming: other herb patches | pending | Catherby/Ardougne/Canifis/Trollheim/Zeah |
| 5e | skill_farming: compost bins | pending | Kronos `CompostBin` |
| 5f | skill_farming: trees / fruit / hops / bushes | pending | Split further if needed |
| 5g | skill_farming: farming_view (179) | pending | Needs patch sim from 5a+; `farming_server_reqs.md` |
| 6a | skill_hunter: bird snare | pending | Kronos `BirdSnare` + `Bird`; trap loc lifecycle |
| 6b | skill_hunter: box trap (chins) | pending | |
| 6c | skill_hunter: net trap + implings | pending | Imp net / jar; skip custom rates |
| 7a | skill_construction: house enter/leave | pending | Kronos `House` enter; instance surface may need engine — measure first |
| 7b | skill_construction: build hotspot core | pending | Likely opcode / instance gaps — mark blocked with citations |
| 8 | minigame: Barrows | pending | Brothers + tunnel + reward chest; skip custom loot tables if any |
| 9 | minigame: Fight Caves | pending | Jad wave table |
| 10 | minigame: Pest Control | pending | |
| 11 | minigame: Warriors' Guild | pending | |
| 12 | minigame: Wintertodt | pending | |
| 13 | minigame: Motherlode Mine | pending | |
| 14 | minigame: Pyramid Plunder | pending | |
| 15 | minigame: Puro-Puro | pending | |
| 16 | minigame: God Wars dungeon | pending | KC + doors; skip custom |
| 17 | minigame: Nightmare Zone | pending | |
| 18 | clues: easy cryptic stubs | pending | Kronos cluescrolls; F2P-first |
| 19 | bosses: Giant Mole | pending | Vanilla |
| 20 | bosses: KQ / DKS / Corp | pending | Vanilla only |
| 21 | bosses: Zulrah / Vorkath / Hydra / ToB / Inferno | pending | Large; split when reached |
| 22 | skill_agility: rooftops + shortcuts | pending | Kronos `courses`/`Shortcuts`; LC gnome already on LC queue |
| 23 | prayer: Redemption / Retribution | pending | Kronos small; check LC first |

## Opcode gap log

Record new Server VM opcodes **before** inventing C content hooks. Format:
`slice | opcode | why content needs it | status`.

| Slice | Opcode / surface | Why | Status |
|---|---|---|---|
| 1 | (none) | Assignment is varp writes + `db_find` + chat | confirmed — no new opcode |
| 5a | softtimer / growth clock | Patch growth between logins | softtimer exists — confirm wall-clock |
| 7a | instance / dynamic map | POH | measure — may block |

## Log

- queue created (2026-08-04): Kronos → OSRS-Content lane; custom skip list; first slice = Turael Assignment
- slice 1 done: Turael Assignment — `skill_slayer/` schema overlays + `%if1..if6` + `~slayer_assign` weighted pick from cache `slayer_master_task` master_id=1; Talk-to/Assignment binds; skipped Kronos Easy/Med/Hard chooser; no new Server VM opcodes; `make mock230-scripts` ok (3738); kill-credit → slice 2
