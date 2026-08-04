# Kronos content port queue

Agent-loop state for **Kronos184 → OSRS-Content** forward port of **post-254 /
modern OSRS** content that LostCity never had.

LostCity remains the content *shape* (RuneScript triggers, procs, configs).
Kronos (`/Users/matthewevers/Documents/git_repos/Kronos184-Fixed_2`) is the
behaviour / id reference for skills and activities that post-date rev 254.
When Kronos and the osrs239 cache disagree, **the cache wins** for wire and
varp layout; Kronos wins only for *policy* the cache does not state.

Parallel to [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) (LostCity → tree)
and [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md)
(authentic mid-era). Do not steal LC slices. Prefer 2009scape for anything that
existed by Jan 2009; this queue keeps **post-2009** gaps (and slayer follow-ups
already started here).

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
| 2 | skill_slayer: kill credit + points | done | Engine calls `[proc,slayer_on_npc_kill]` after every `[ai_queue3]` (drop binds suppress `_`); `slayer_task_member` category seed for Turael; XP=`npc_basestat(hitpoints)`; Kronos streak points into `%slayer_points` / `%slayer_tasks_completed`; skipped wilderness emblem custom drops; cave bug/slime lack category — deferred |
| 3 | skill_slayer: remaining masters | done | Mazchna→Duradel + Nieve/Steve + Krystilia + Konar + Spria; combat floors from skill_features; Duradel Slayer 50; free reset only Turael/Spria; task pick filters `min_comlevel`; leagues master_id 10 skipped; Konar area wire deferred |
| 4 | skill_slayer: Cancel/Block/Store arms | done | `slayer_task_ops.rs2` + per-master blocked varbits; cancel 30 / block CS2 823 prices; Store needs unlock bit 51; assign skips blocked task ids; CS2 greys Block in category mode (98) |
| 5a | skill_farming: patch registry + state | blocked | → [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) §1a |
| 5b | skill_farming: Falador herb (rake/plant/harvest) | blocked | → SCAPE2009 §1b |
| 5c | skill_farming: allotment + flower (Falador) | blocked | → SCAPE2009 §1c |
| 5d | skill_farming: other herb patches | blocked | → SCAPE2009 §1d |
| 5e | skill_farming: compost bins | blocked | → SCAPE2009 §1e |
| 5f | skill_farming: trees / fruit / hops / bushes | blocked | → SCAPE2009 §1f |
| 5g | skill_farming: farming_view (179) | blocked | → SCAPE2009 §1g |
| 6a | skill_hunter: bird snare | blocked | → SCAPE2009 §2a |
| 6b | skill_hunter: box trap (chins) | blocked | → SCAPE2009 §2b |
| 6c | skill_hunter: net trap + implings | blocked | → SCAPE2009 §2c |
| 7a | skill_construction: house enter/leave | blocked | → SCAPE2009 §4a |
| 7b | skill_construction: build hotspot core | blocked | → SCAPE2009 §4b |
| 8 | minigame: Barrows | blocked | → SCAPE2009 §5 |
| 9 | minigame: Fight Caves | done | Wave table + shared-map enter/exit (`minigame_fightcave/`); Kronos/2009 remainder algorithm; Tz-Kek split; Jad reward cape+tokkul; Jad healers deferred; **instance/DynamicMap still needed for concurrent players** |
| 10 | minigame: Pest Control | blocked | → SCAPE2009 §6 |
| 11 | minigame: Warriors' Guild | done | `minigame_warriorsguild/`: entrance Att+Str≥130 or 99; cyclops door 100 tokens + Kamfreena entry dial; 10/min drain; 1/50 bronze→rune drop; stairs; policy 2009scape (LC none); animator/dummy/catapult/token earn + basement dragon deferred |
| 12 | minigame: Wintertodt | done | `minigame_wintertodt/`: Ignisia unlock, Doors of Dinh (FM50), tool chests, chop/fletch/light/feed/fix + herb pot; points + debug crate; storm HP/cold/pyro heal/HUD/crate loot deferred; no new opcodes |
| 13 | minigame: Motherlode Mine | done | `minigame_motherlode/`: enter/exit, mine→paydirt, hopper→`%motherlode_sack_transmit`, strut fix, sack ore table, rockfall; water NPC/upper floor/dark tunnel/HUD deferred; no new opcodes |
| 14 | minigame: Pyramid Plunder | blocked | → SCAPE2009 §7 |
| 15 | minigame: Puro-Puro | blocked | → SCAPE2009 §8 |
| 16 | minigame: God Wars dungeon | blocked | → SCAPE2009 §19 |
| 17 | minigame: Nightmare Zone | done | `minigame_nightmarezone/`: Dominic dream buy, lobby/arena vials, 2-boss stub endurance, barrels, herb-box chest; Kronos was stub-only; full boss list/powerups/absorption/HUD/DynamicMap deferred |
| 18 | clues: easy cryptic stubs | blocked | → SCAPE2009 §18 (still pending there) |
| 19 | bosses: Giant Mole | blocked | → SCAPE2009 §20 |
| 20 | bosses: KQ / DKS / Corp | pending | KQ has LC proc → CONTENT_PORT_QUEUE; DKS/Corp prefer SCAPE2009 era — split or skip |
| 21 | bosses: Zulrah / Vorkath / Hydra / ToB / Inferno | pending | Post-2009; Kronos owns |
| 22 | skill_agility: rooftops + shortcuts | in_progress | Draynor→Canifis done; Falador/Seers/Rellekka/Ardougne + shortcuts remain |
| 23 | prayer: Redemption / Retribution | done | `prayer_effects.rs2`: ≤10% HP → drain+heal+gfx; death → gfx+hit `%aggressive_npc` ≤1 tile; policy 2009scape (LC none); multi AoE deferred (no map_multiway data) |

## Opcode gap log

Record new Server VM opcodes **before** inventing C content hooks. Format:
`slice | opcode | why content needs it | status`.

| Slice | Opcode / surface | Why | Status |
|---|---|---|---|
| 1 | (none) | Assignment is varp writes + `db_find` + chat | confirmed — no new opcode |
| 2 | (none) | Kill credit via named proc after `ai_queue3` (existing `run_proc_on_npc`) | confirmed — no new opcode |
| 3 | (none) | Masters = gates + `db_find` + existing chat/rewards | confirmed — no new opcode |
| 4 | (none) | Confirm arms = varbit writes + points | confirmed — no new opcode |
| 5a | softtimer / growth clock | Patch growth between logins | softtimer exists — confirm wall-clock |
| 7a | instance / dynamic map | POH | measure — may block |
| 9 | instance / dynamic map | Fight Caves multiplayer isolation (shared map used for single-player) | soft — content landed on shared region 9551 |
| 11 | (none) | Doors/tokens/drops/softtimer already expressible | confirmed — no new opcode |
| 12 | (none) | Enter/actions expressible with loc_change + softtimer later | confirmed — no new opcode |
| 13 | (none) | Hopper/sack/veins expressible via cache varbits + loc_change | confirmed — no new opcode |
| 17 | instance / dynamic map | NMZ dream isolation (shared 35_73 used for single-player) | soft — same gap as Fight Caves |
| 23 | (none) | Existing damage/death hooks + spotanim_pl | confirmed — no new opcode |
| 23 | map_multiway zone data | Retribution multi AoE | deferred — opcode exists, no multi map |
| 22 | (none) | Rooftop locs + existing agility helpers | confirmed — no new opcode |

## Log

- queue created (2026-08-04): Kronos → OSRS-Content lane; custom skip list; first slice = Turael Assignment
- 2026-08-04: mid-era overlaps (farming/hunter/construction/slayer/Barrows/PC/…) deferred to [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md); this queue keeps post-2009-only
- slice 1 done: Turael Assignment — `skill_slayer/` schema overlays + `%if1..if6` + `~slayer_assign` weighted pick from cache `slayer_master_task` master_id=1; Talk-to/Assignment binds; skipped Kronos Easy/Med/Hard chooser; no new Server VM opcodes; `make mock230-scripts` ok (3738); kill-credit → slice 2
- slice 2 done: kill credit + points — `mock230_world_npc_died` → `[proc,slayer_on_npc_kill]`; membership `slayer_task_member` (category seed); finish awards Kronos streak points; `%slayer_tasks_completed_1` perm+transmit; no new opcodes
- slice 3 done: remaining masters — `slayer_masters.rs2` Talk/Assignment/Rewards for all cache masters; combat/slayer gates from skill_features; weighted assign filters `min_comlevel`; Konar location deferred; no new opcodes
- slice 4 done: Cancel/Block/Store/Unblock — `~slayer_confirm` arms; blocked slots per master (CS2 8025); cancel 30pts; block prices from CS2 823; Store/Swap behind unlock 51; no new opcodes
- slice 9 done: Fight Caves wave table + shared-map loop — enter/exit locs, remainder spawn algorithm, Tz-Kek split, Jad cape; healers + true instance deferred
- slice 11 done: Warriors' Guild cyclops core — entrance gate, Kamfreena entry dial, token take/drain, defender progression drops, WG stairs; activities rooms + basement dragon deferred; `make mock230-scripts` 4132; pack 0 errors; no new opcodes
- slice 12 done: Wintertodt core loop — Ignisia, doors, chests, bruma chop/fletch, brazier light/feed/fix, herb→potion; storm/cold/HUD deferred; scripts 4195; pack 0 errors; no new opcodes
- slice 13 done: Motherlode Mine core — enter/exit, paydirt veins, hopper→sack varbit, strut repair, sack loot table, rockfall; water NPC path / upper floor deferred; scripts 4241; pack 0 errors; no new opcodes
- slice 17 done: Nightmare Zone lobby+stub dream — Dominic purchase, vials, Count Draynor→Elvarg, barrels, herb box; full rumble/powerups/DynamicMap deferred; scripts 4378; pack 0 errors
- slice 23 done: Redemption/Retribution procs — `~prayer_redemption_check` on melee/magic/poison damage; `~prayer_retribution_on_death` hits aggressive npc within 1; multi AoE deferred; scripts 4467; no new opcodes
- slice 22 (partial): Draynor rooftop — `rooftop_draynor.rs2` all 7 obstacles via cache loc names; Kronos XP/policy; Mark of Grace/pet deferred; other rooftops remain; scripts 4504; pack 0 errors
- slice 22 (partial): Al Kharid rooftop — `rooftop_alkharid.rs2` req 20, 8 obstacles; zip/bamboo simplified vs Kronos forces; scripts 4513; pack 0 errors
- slice 22 (partial): Varrock rooftop — `rooftop_varrock.rs2` req 30, 9 obstacles; wall/balcony forces simplified; scripts 4528; pack 1 error is parallel farming bushes.dbrow (unrelated)
- slice 22 (partial): Canifis rooftop — `rooftop_canifis.rs2` req 40, 8 obstacles; scripts 4592; unblocked parallel grandtree `gosub(npc_death)` compile break
