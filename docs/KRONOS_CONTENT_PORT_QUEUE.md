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

## Shared tree — never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. Lane routing (“LC has it → CONTENT_PORT_QUEUE”) is fine; muting
those files is not. See PORTING_GUIDE §7 and
`.cursor/rules/no-park-sibling-content.mdc`.

Loop prompt: read this file + PORTING_GUIDE §4 / §4.4 / §7; port the next pending
unblocked Kronos→OSRS-Content slice (custom skip-list only); NEVER park sibling
lanes; Grep LostCity first — if LC has it, route to CONTENT_PORT_QUEUE (do not
touch its tree); Measure opcode gaps; implement any new Server VM opcode before
inventing C hooks; Resolve names never copy Kronos ids; Verify with
`make -C src mock230-scripts` and `mock230_pack --check-only`; update this file;
re-arm. Stop only when the user stops the loop.

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
6. **Never park sibling lanes** — no `*.skip`, no moving `skill_construction/` /
   `minigame_mta/` aside for compile. Fix your own errors (PORTING_GUIDE §7).

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
| 7a | skill_construction: house enter/leave | done | → SCAPE2009 §4a (live — do not `.rs2.skip`) |
| 7b | skill_construction: build hotspot core | done | → SCAPE2009 §4b (live — do not edit/park from Kronos) |
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
| 20 | bosses: KQ / DKS / Corp | blocked | KQ → CONTENT_PORT_QUEUE (LC has proc); DKS/Corp → SCAPE2009 era |
| 21 | bosses: Zulrah / Vorkath / Hydra / ToB / Inferno | done | stubs: Zulrah/Vorkath/Hydra/ToB (Maiden solo); **Inferno full 1–69** (`minigame_inferno/`: waves, pillars, Jad, Zuk via `map_instance_from_square`); party/ToB rest deferred |
| 22 | skill_agility: rooftops + shortcuts | done | 8 rooftops + MoG roll + Falador wall + GE tunnels; mid-era shortcuts → LC/SCAPE2009; grapples/pet deferred |
| 23 | prayer: Redemption / Retribution | done | `prayer_effects.rs2`: ≤10% HP → drain+heal+gfx; death → gfx+hit `%aggressive_npc` ≤1 tile; policy 2009scape (LC none); multi AoE now unblocked — `map_multiway` hosted with the reference's zone data (see the gap log) |
| 24 | bosses: Cerberus | done | `minigame_cerberus/`: Taverley crawl, Slayer 91 winch, 3 lairs, portcullis exit; souls/lava/loot/DynamicMap deferred |
| 25 | bosses: Kraken / Thermonuclear Smoke Devil | done | `minigame_kraken/` + `minigame_thermy/`: cave enter, Slayer 87/93 boss rooms, whirlpool Disturb→boss; tentacles/face-mask dmg/loot/DynamicMap deferred |
| 26 | bosses: Abyssal Sire | done | `minigame_sire/`: wake sleeping→awake, exit appendage→nexus, Overseer stub; phases/lungs/Unsired font/fairy DIP/DynamicMap deferred |
| 27 | bosses: Skotizo / Demonic Gorilla / Lizardman Shaman | done | `minigame_skotizo/` totem altar; `minigame_gorilla/` crash cavern; `minigame_shaman/` lair; specials/loot/DynamicMap deferred |
| 28 | raid: Chambers of Xeric stub | done | `minigame_cox/`: mountain enter→solo Tekton staging, exit steps, board stub; party/chambers/Olm/DynamicMap deferred |
| 29 | bosses: Obor / Bryophyta | done | `minigame_obor/` giant key chest; `minigame_bryophyta/` mossy key chest; growthlings/loot/DynamicMap deferred |
| 30 | bosses: Callisto / Venenatis / Vetion / Scorpia | done | `minigame_wildy_bosses/`: Scorpia 3-cave enter/exit+spawn; outdoor Callisto/Venenatis/Vet'ion kill stubs; specials/hellhounds/singles/loot deferred |
| 31 | bosses: Chaos Fanatic / Crazy Archaeologist | done | kill stubs + debugprocs in `minigame_wildy_bosses/`; specials/loot deferred; Chaos Elemental → SCAPE2009/classic |
| 32 | bosses: Hespori / Sarachnis | done | `minigame_hespori/` Farming Guild cave; `minigame_sarachnis/` Forthos crypt; healers/spawns/loot/DynamicMap deferred |
| 33 | bosses: Grotesque Guardians | done | `minigame_grotesque/`: brittle key Unlock→roof, Cloister Bell spawns Dawn+Dusk; phases/spheres/loot/DynamicMap deferred |
| 34 | wilderness: Revenant caves | done | `minigame_revcaves/`: south/north enter + `wild_cave_exit_mid` Exit overlay, pillars 65/75/89; mid crevice/combat/ethereum/loot deferred |
| 35 | wilderness: Larran's chest | done | `minigame_larran/`: Unlock big+small with `slayer_wilderness_key`; Check mes; Kronos vesta loot skipped; loot table deferred |
| 36 | bosses: Mage Arena II | done | `minigame_magearena2/`: `ma2_symbol` Activate→spawn choice, kill→hearts + component varbits; hot/cold/imbue/Kolodion hand-in deferred |
| 37 | wilderness: Resource Area gate | blocked | → SCAPE2009 (mid-era wilderness hut) |
| 38 | wilderness: Wilderness Slayer Cave | done | `minigame_wildyslayer/`: north/south Walk-down ↔ Exit; shortcuts deferred |
| 39 | skill: Karuulm dungeon stubs | done | `minigame_karuulm/`: elevator, stepover warn, lava gap, tunnel, stairs; boot burn/slayer-only deferred; Hydra → slice 21 |
| 40 | skill_slayer: superior encounter stub | done | `slayer_superior.rs2`: bit 35 roll 1/200→spawn by category; superior kill credit; loot/full map deferred |
| 41 | skill: Chasm of Fire | done | `minigame_chasm_of_fire/`: surface rope enter/exit + gibbet lifts ±1 plane; combat deferred |
| 42 | skill: Kourend Catacombs entrances | done | `minigame_catacombs/`: statue enter, 4 vines unlock `%cata_hole*`, hole/passage enter, totem combine; Skotizo → slice 27; shortcuts/drops deferred |
| 43 | skill: Brimstone / Konar chest | done | `minigame_brimstone/`: Unlock with `konar_key` + Check; loot deferred (Kronos had ObjectID only) |
| 44 | skill: Lithkren vault | done | `minigame_lithkren/`: vault/lab doors, barrier pass, stairs/trapdoor links; mural kill counters + dragons deferred |
| 45 | skill: Smoke Devil dungeon | done | enter/exit already in `minigame_thermy/` (slice 25); added facemask/slayer-helm worn gate; smoke softtimer deferred |
| 46 | bosses: Mage Arena II follow-ups | done | Kolodion gives symbol + imbues god cape↔`ma2_*_cape` for 3 hearts; hot/cold locate deferred |
| 47 | skill: Brimstone ring combine | done | `minigame_brimstone/brimstone_ring.rs2`: eye+fang+heart → `brimstone_ring` |
| 48 | skill: Zenyte forge (Temple cave) | done | `minigame_zenyte/`: trapdoor/rope + shard+`onyx`→`uncut_zenyte` on wall of flame; furnace alternate deferred |
| 49 | wilderness: Rev caves mid crevice | done | `wild_cave_entrance_mid` Jump-Down + 100k fee (`%revcave_fee_paid`); bank fee / death clear / ethereum deferred; surface coords re-measure when map dump available |
| 50 | skill: Catacombs shortcuts | done | `catacombs_shortcuts.rs2`: stepstones (28) + cracks (17/34 from skill_features); Kronos dest coords |
| 51 | skill: Bracelet of ethereum | done | `ethereum.rs2`: charge/check/toggle/uncharge/dismantle; `%ethereum_charges` stub (inv_setvar gap); combat absorb/auto-collect deferred |
| 52 | skill: Ancient sceptre / wilderness weapons | done | `wildy_weapons.rs2`: Thammaron's / Craw's / Viggora's charge/check/uncharge (1000 activate, max 16000); `%thammaron/craws/viggoras_charges` stubs; combat/dismantle/swap/ancient sceptre deferred |
| 53 | wilderness: Rev caves stairs exits | done | `wild_cave_exit_low/high` Climb-up → surface beside trapdoors (override `climb_up`); `wild_cave_exit_surface` no ops = one-way; crawl `exit_mid` unchanged |
| 54 | wilderness: Escape Caves fee entrances | done | `wild_cave_exit01` is Escape Caves (not rev) — Enter+Check-Fee 50k `%wildy_boss_fee_paid` + internal exits; also gated rev `entrance_low/high` with shared 100k `%revcave_fee_paid`; Peek/prayer-drain/boss dens deferred |
| 55 | skill: Ancient sceptre combine | done | Eblis (`fd_elder_*`) opnpcu: `ancient_icon`+`staff_of_zaros`→`ancient_sceptre` (DT complete); SotN wield gate + elemental variants deferred |
| 56 | skill: Accursed / Webweaver / Ursine | done | `wildy_upgrades.rs2`: Crafting/Fletching/Smithing 85 combines (skull/fang/claws); charge stubs on upgraded weapons; Ferox NPCs/dismantle/swap/specials deferred |
| 57 | wilderness: Callisto / Venenatis / Vet'ion dens | done | `boss_dens.rs2`: Enter+Peek+Check-Fee; shared 50k fee; medium diary gate; den land/exit coords from map; KC peek / slayer alternate / tele delay deferred |
| 58 | skill: Ferox Enclave upgrade NPCs | done | `ferox_upgrades.rs2`: Phabelle/Derse/Andros Talk-to + opnpcu; 500k fee bypasses skill-85; bank fee / full dialogue deferred |
| 59 | wilderness: Hunter's End / Skeletal Tomb | done | singles dens Artio/Spindel/Calvar'ion — Enter+Peek+Check-Fee; hard diary; shared fee; exit branch on template locs; slayer alternate deferred |
| 60 | skill: Ancient sceptre elemental variants | done | `ancient_sceptre_elemental.rs2`: quartz attach/swap/dismantle → `ancient_sceptre_{blood,ice,smoke,shadow}`; DT2 gate + spell passives deferred |
| 61 | skill: SotN / DT2 sceptre gates | blocked | SotN + DT2 quest progress not authored → QUESTHELPER; skill_features cite quest dbrows 2338/2343 |
| 62 | wilderness: Ferox Enclave bank fee | blocked | wiki: no bank fee; real fee is Ferox 5M respawn → slice 65 |
| 63 | skill: Wildy weapon dismantle / swap | done | `wildy_weapons.rs2`: base Dismantle→7500 ether; Accursed/Webweaver/Ursine reverse; sceptre Swap ↔ `*_recol` (a)/(au); charge on recol |
| 64 | wilderness: Boss den Peek KC | done | 20 KC via `%total_{callisto,venenatis,vetion,artio,spindel,calvarion}_kills`; occupancy = `npc_find` at den spawn; kill credit on ai_queue3 (incl. singles) |
| 65 | wilderness: Ferox Enclave respawn | blocked | 5M to Ferox NPC; needs player respawn coord (death.rs2 hardcodes `^respawn_coord`) |
| 66 | skill: Bracelet of ethereum absorb | done | 75% rev dmg reduce (`~ethereum_reduce_damage` in melee + `combat_damage_player`); category `1189=revenant`; kill absorb stub 15 ether; aggression tolerance / full drop tables deferred |
| 67 | wilderness: Rev caves prayer drain | done | withdrawn — wiki + Kronos RevCaves.java: no cave-specific prayer drain / dark drain; normal prayer only |
| 68 | skill: Wildy weapon specials | blocked | Accursed/Webweaver/Ursine specs need special-attack combat model (`orbs.rs2`: arms bar only; LC `player_special_attack` not ported) |
| 69 | wilderness: Cave fee clear on death | done | `~revcave_fees_on_death` clears `%revcave_fee_paid` + `%wildy_boss_fee_paid`; PKer 100k loot drop deferred |
| 70 | skill: Wildy weapon wilderness passives | done | `wildy_passives.rs2`: +50% accuracy/damage in Wildy when charged; 1 ether/attack; Craw's/Webweaver ammo-free; powered-staff built-in spell path deferred |
| 71 | skill: Ethereum aggression tolerance | blocked | charged bracelet → tolerant; needs `.hunt` `check_inv` (LC HuntType) — `nearest_victim` is placeholder |
| 72 | wilderness: Fountain of Rune | done | `minigame_fountain_of_rune/`: oplocu glory/wealth/skills/combat → max (6)/(5); 1/25000 `amulet_of_glory_inf`; desktop op1 overlay |
| 73 | wilderness: Wilderness Sword teleports | done | `minigame_wilderness_sword/`: sword 3 daily FoR + sword 4 unlimited; Kronos coords; worn op / web-slash deferred |
| 74 | skill: Imbued heart | done | `skill_slayer/imbued_heart.rs2`: Invigorate `stat_boost(magic,1,10)` + 700t CD; saturated upgrade 150k essence + `(4,10)`/500t; death clears CD; non-drain / Ferox pool reset deferred |
| 75 | wilderness: Supply / Bloody chest stubs | done | withdrawn — Kronos SupplyChest/BloodyChest are private-server (blood money / bloody keys); Deadman supply chests skip-listed; muddy chest already LC CONTENT_PORT 9f |
| 76 | wilderness: Rogues' Castle chests | done | `areas/wilderness/rogue_chests.rs2`: Search for traps 84 / 701.7 XP / 34t restock; Open trap 16–43; medium diary loot table + hard qty bump; `wilderness_rogue` aggro; clue/RoW(i)/looting-bag deferred; skipped Kronos PVP/CB87 |
| 77 | wilderness: Lava Dragon / Lava Maze shortcuts | done | `agility_shortcuts_osrs.rs2`: Cross stepping stones req 74/82; relative `p_telejump`; diary gate removed (wiki May 2024); 0 XP |
| 78 | bosses: Corporeal Beast cave | blocked | → SCAPE2009 (2008 Corp handlers exist there); Kronos instance dialogue is custom |
| 79 | wilderness: Obelisks | blocked | → SCAPE2009 `WildernessObeliskPlugin`; hard-diary destination picker is OSRS follow-up after mid-era land |
| 80 | wilderness: Axe / Pirate hut lockpick | blocked | → SCAPE2009 / classic thieving doors (pre-2009) |
| 81 | wilderness: Active volcano / Hotspot / Staff bounty | done | withdrawn — Kronos private-server events (blood fragment / hotspot rotation / staff PK) |
| 82 | wilderness: Deep Wilderness crevice | done | `agility_shortcuts_osrs.rs2`: Use `wilderness_deep_crevice` Agility 46 + medium diary; Kronos dests; stairs enter/exit deferred (generic `stairs_cellar`) |
| 83 | skill: Woodcutting Guild | done | `skill_woodcutting/woodcutting_guild.rs2`: WC60 gates + Bone Voyage hand-in; rope ladders; dungeon; shrine eggs → seed nest + 100 Prayer XP; redwood plane / +2 boost deferred |
| 84 | skill: Mining Guild expansion ladders | done | withdrawn — LC `areas/.../mining_guild.rs2` already gates Mining 60 on `mguild_ladder`/`mguild_door`; Kronos coord-specific teleports are the same +6400 cellar offset |
| 85 | skill: WC Guild bird-egg shrine | done | folded into slice 83 (`oplocu,wcguild_shrine`) |

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
| 23 | `map_multiway` (1015) + zone data | Retribution multi AoE | **done** — the earlier "opcode exists" was wrong: 1015 was *declared* and never hosted, so it fell through to the VM's stub and returned 0, which is indistinguishable from an empty zone set. Both halves landed: `maps/multiway.csv` ported from the reference verbatim (4,697 zones — coordinates, not ids, so nothing needs re-resolving) and a host over a sorted zone set in `mock230_content.c`. Measured in the headless client: `::multiway` reads 0 in Lumbridge and 1 at 2984,3912 |
| 22 | (none) | Rooftop locs + existing agility helpers | confirmed — no new opcode |
| 22 | (none) | MoG `obj_add` + GE/Falador shortcut locs | confirmed — no new opcode |
| 21 | instance / dynamic map | Zulrah shrine isolation (shared 35_47/48) | soft — same gap as Fight Caves / NMZ |
| 21 | (none) | Boat/plaque + npc_add expressible | confirmed — no new opcode |
| 21 | instance / dynamic map | Vorkath Ungael isolation (shared 35_63) | soft — same gap |
| 21 | (none) | Ice chunks + poke + npc_changetype | confirmed — no new opcode |
| 21 | instance / dynamic map | Hydra shared maps | soft — Inferno unblocked via `map_instance_from_square` |
| 21 | (none) | Hydra climb/door + Inferno entrance | confirmed — no new opcode |
| 21 | instance / dynamic map | ToB Theatre isolation (shared 49_69) | soft — same gap |
| 21 | (none) | Surface enter + barrier + Maiden kill | confirmed — no new opcode |
| 24 | (none) | Crawl/winch/portcullis + npc_add | confirmed — no new opcode |
| 24 | instance / dynamic map | Cerberus lair isolation | soft — same gap |
| 25 | (none) | Cave/crevice enter + disturb/changetype | confirmed — no new opcode |
| 25 | instance / dynamic map | Kraken / Thermy isolation | soft — same gap |
| 26 | (none) | Wake changetype + lever exit | confirmed — no new opcode |
| 26 | instance / dynamic map | Sire chamber isolation | soft — same gap |
| 27 | (none) | Altar/cavern/lair enter + npc_add | confirmed — no new opcode |
| 27 | instance / dynamic map | Skotizo / gorilla isolation | soft — same gap |
| 28 | (none) | Mountain enter/exit + Tekton npc_add | confirmed — no new opcode |
| 28 | instance / dynamic map | CoX chamber layout | deferred — required for real raid |
| 29 | (none) | Key chest Open + gate/rock exit | confirmed — no new opcode |
| 29 | instance / dynamic map | Obor / Bryophyta isolation | soft — same gap |
| 30 | (none) | Scorpia crawl + outdoor kill hooks | confirmed — no new opcode |
| 31 | (none) | Outdoor kill hooks | confirmed — no new opcode |
| 32 | (none) | Cave/crypt enter + npc_add | confirmed — no new opcode |
| 32 | instance / dynamic map | Hespori / Sarachnis isolation | soft — same gap |
| 33 | (none) | Roof Unlock/bell + dual kill | confirmed — no new opcode |
| 33 | instance / dynamic map | Grotesque Guardians isolation | soft — same gap |
| 34 | (none) | Cave enter/exit + pillar telejump | confirmed — no new opcode |
| 34 | loc.op1..op5 field register | Restate Exit on `wild_cave_exit_mid` | confirmed — client-native keys |
| 35 | (none) | Key Unlock + Check | confirmed — no new opcode |
| 36 | (none) | Symbol Activate + kill hearts | confirmed — no new opcode |
| 38 | (none) | Stairs enter/exit teleports | confirmed — no new opcode |
| 39 | (none) | Elevator + shortcuts expressible | confirmed — no new opcode |
| 39 | softtimer / zone burn | Karuulm hot floor without boots | deferred |
| 40 | (none) | Unlock bit + npc_add + kill credit | confirmed — no new opcode |
| 41 | (none) | Rope teleports + plane movecoord | confirmed — no new opcode |
| 42 | (none) | Entrances + varbit unlock + opheldu | confirmed — no new opcode |
| 43 | (none) | Key Unlock + Check | confirmed — no new opcode |
| 44 | (none) | Doors/stairs/barrier teleports | confirmed — no new opcode |
| 45 | (none) | Face-mask worn check on enter | confirmed — no new opcode |
| 45 | softtimer / zone damage | Smoke without mask | deferred |
| 46 | (none) | Kolodion chat + cape swap | confirmed — no new opcode |
| 46 | hot/cold distance mes | Enchanted symbol locate | deferred |
| 47 | (none) | opheldu combine | confirmed — no new opcode |
| 48 | (none) | Trapdoor loc_change + oplocu forge | confirmed — no new opcode |
| 49 | (none) | Jump-Down + coin fee | confirmed — no new opcode |
| 50 | (none) | Stepstone jump + crack teleports | confirmed — no new opcode |
| 51 | inv_setvar / inv_getvar | Per-item ether charges on bracelet | deferred — player `%ethereum_charges` stub |
| 51 | (none) | Charge/check/toggle/dismantle expressible | confirmed — inventory ops |
| 52 | inv_setvar / inv_getvar | Per-item ether charges on wildy weapons | deferred — player `%thammaron/craws/viggoras_charges` stubs |
| 52 | (none) | Charge/check/uncharge expressible | confirmed — inventory ops |
| 53 | (none) | Stairs Climb-up teleports (named override of climb_up) | confirmed — no new opcode |
| 54 | (none) | Fee enter/exit + Check-Fee mes | confirmed — no new opcode |
| 55 | (none) | Eblis opnpcu combine | confirmed — no new opcode |
| 56 | (none) | Trophy combine + charge stubs | confirmed — no new opcode |
| 56 | inv_setvar / inv_getvar | Per-item charges on upgraded weapons | deferred — player `%accursed/webweaver/ursine_charges` stubs |
| 57 | (none) | Den enter/exit + shared fee + diary gate | confirmed — no new opcode |
| 58 | (none) | Talk-to / opnpcu + coin fee | confirmed — no new opcode |
| 59 | (none) | Singles dens enter/exit + hard diary | confirmed — no new opcode |
| 71 | `.hunt` + `check_inv` (HuntType) | Charged ethereum → skip player in aggro | blocked — `nearest_victim` is nearest-only placeholder; inventing `aggro_immune` C flag would be a content hook |
| 72 | (none) | oplocu recharge + random(25000) eternal | confirmed — inventory ops |
| 73 | (none) | opheld3 + date_minutes daily gate | confirmed — no new opcode |
| 74 | (none) | opheld1 + map_clock CD + death reset | confirmed — inventory ops |
| 76 | (none) | oploc + loc_change + npc_findallany aggro | confirmed — no new opcode |
| 76 | clue / looting bag / RoW(i) | hard clue + auto-store on loot | deferred |
| 77 | (none) | oploc Cross + p_telejump | confirmed — no new opcode |
| 82 | (none) | oploc Use + p_teleport + diary gate | confirmed — no new opcode |
| 83 | (none) | oploc Open + walk-through + teleports | confirmed — no new opcode |

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
- slice 22 (partial): Falador+Seers+Rellekka+Ardougne rooftops — `rooftop_{falador,seers,rellekka,ardougne}.rs2`; all 8 OSRS courses now present; MoG/pet/shortcuts deferred; scripts 4663; pack errors are parallel farming (unrelated)
- slice 22 done: MoG `~agility_mark_of_grace` on all finishers; Falador crumbling wall + GE underwall tunnels in `agility_shortcuts_osrs.rs2`; grapples/pet deferred; mid-era stiles → LC; scripts 4687
- slice 21 (partial): Zulrah shrine stub — `minigame_zulrah/`: boat board, spawn `snakeboss_boss_ranged`, exit plaque; phases/fumes/snakelings/loot/DynamicMap deferred; scripts 4713
- slice 21 (partial): Vorkath stub — `minigame_vorkath/`: Ungael boat, ice-chunk enter/exit, poke `vorkath_sleeping`→`vorkath`; specials/loot/DynamicMap deferred; scripts 4727
- slice 21 (partial): Hydra + Inferno stubs — `minigame_hydra/` climb+door+`hydraboss`; `minigame_inferno/` jump-in wave-1 nibblers; ToB remain; scripts 4776
- slice 21 done: ToB Maiden solo stub — `minigame_tob/`: surface enter, barrier start, `tob_maiden_100` kill→exit; party/Bloat→Verzik/loot/DynamicMap deferred; scripts 4794
- 2026-08-04: Inferno full encounter — `minigame_inferno/`: fire-cape gate, `map_instance_from_square(35_83)`, waves 1–66 budget + pillars/nibblers, 67–68 Jad, 69 Zuk (Content2 seal/`loc_change`/glyph/adds remapped); soft gaps: Zek corpse revive (`inferno_zek.rs2`), logout-pause/resume (perm varps + `~inferno_login`), Jal-nib-rek pet 1/100 (`infernopet`); scripts 9424; pack 0 errors
- 2026-08-04: queue was idle; extended with slices 24–28 (Cerberus / Kraken+Thermy / Sire / Skotizo+Gorilla+Shaman / CoX stub)
- slice 24 done: Cerberus stub — `minigame_cerberus/`: Taverley crawl ↔ lobby, Slayer 91 winch→3 lairs, portcullis exit, `cerberus_attacking` kill; souls/lava/loot deferred; scripts 4932; pack 0 errors
- slice 25 done: Kraken + Thermy stubs — `minigame_kraken/` cove+Disturb whirlpool; `minigame_thermy/` smoky cave+boss crevice; tentacles/face-mask dmg/loot deferred; scripts 4964; pack 0 errors
- slice 26 done: Abyssal Sire stub — `minigame_sire/`: Attack wake 5886→5887 names, exit lever→nexus, Overseer mes; phases/Unsired/fairy DIP deferred; scripts 5000; pack 0 errors
- slice 27 done: Skotizo/Gorilla/Shaman stubs — `minigame_skotizo/` `cata_totem` altar; `minigame_gorilla/` mm2 cavern; `minigame_shaman/` lizardman lair; specials/loot deferred; scripts 5050; pack 0 errors
- slice 28 done: CoX solo Tekton stub — `minigame_cox/`: `raids_entrance_steps` warning→staging+Tekton, exit steps, recruiting board mes; party/chambers/Olm/DynamicMap deferred; scripts 5083; pack 0 errors
- 2026-08-04: extended queue with 29 Obor/Bryophyta + 30 wildy bosses (Callisto/Venenatis/Vetion/Scorpia)
- slice 29 done: Obor + Bryophyta stubs — key chests consume `hillgiant_boss_key` / `mossy_key`; exit gates/rocks; scripts 5126; pack 0 errors
- slice 30 done: Wildy demi-bosses — `minigame_wildy_bosses/`: Scorpia caverns; Callisto/Venenatis/Vet'ion kill stubs; specials/loot deferred; scripts 5166; pack 0 errors
- slice 31 done: Chaos Fanatic + Crazy archaeologist kill stubs in `minigame_wildy_bosses/`; Chaos Elemental left for SCAPE2009/classic; scripts 5170; pack 0 errors
- slice 32 done: Hespori + Sarachnis stubs — `minigame_hespori/` guild cave; `minigame_sarachnis/` Forthos web; healers/spawns/loot deferred; scripts 5227; pack 0 errors
- slice 33 done: Grotesque Guardians stub — `minigame_grotesque/`: `slayer_roof_key` Unlock, Cloister Bell→Dawn+Dusk, both-dead exit; phases deferred; scripts 5278; pack 0 errors (also unblocked parallel RD journal/`%rd_main` compile)
- 2026-08-04: queue was idle after 33; extended with 34 Rev caves / 35 Larran's chest / 36 Mage Arena II; Resource Area → SCAPE2009
- slice 34 done: Revenant caves stub — `minigame_revcaves/`: south/north enter, `wild_cave_exit_mid` Exit (map truth vs Kronos 31557), pillars via skill_features reqs; declared `loc.op1..op5` client-native; mid/combat deferred; scripts 5325; pack 0 errors
- slice 35 done: Larran's chest stub — `minigame_larran/`: Unlock big+small consume `slayer_wilderness_key`; skipped Kronos private-server loot; scripts 5401; pack 0 errors
- slice 36 done: Mage Arena II stub — `minigame_magearena2/`: symbol Activate choice→spawn, kill→hearts + `%ma2_*_component`; hot/cold/imbue deferred; scripts 5461; pack 0 errors
- 2026-08-04: queue idle after 36; extended with 38 Wildy Slayer Cave / 39 Karuulm stubs / 40 superior slayer
- slice 38 done: Wilderness Slayer Cave — `minigame_wildyslayer/` north+south enter/exit from map coords; scripts 5475; pack 0 errors
- slice 39 done: Karuulm dungeon stubs — `minigame_karuulm/`: brimstone elevator, stepover, gap, tunnel, stairs; burn softtimer deferred; scripts 5503; pack 0 errors
- slice 40 done: Superior Slayer stub — bit 35 Bigger and Badder 1/200 spawn by category; superior kill task credit; loot deferred; scripts 5549; pack 0 errors
- 2026-08-04: **queue idle** — no pending unblocked Kronos slices after 40
- 2026-08-04: queue idle after 40; extended with 41 Chasm of Fire / 42 Kourend Catacombs / 43 Brimstone chest
- slice 41 done: Chasm of Fire stub — `minigame_chasm_of_fire/`: `cof_over_falloff2_rope`/`cof_rope_up` + gibbet lifts via `movecoord(…,±1 plane)`; scripts 5581; pack 0 errors
- slice 42 done: Kourend Catacombs entrances — `minigame_catacombs/`: statue + 4 vine unlocks + hole enter + totem combine; main `cata_exit` kept in skotizo; shortcuts deferred; scripts 5624; pack 0 errors
- slice 43 done: Brimstone chest — `minigame_brimstone/`: Unlock with `konar_key` + Check; loot deferred; scripts 5642; pack 0 errors
- 2026-08-04: extended queue with 44 Lithkren / 45 Smoke Devil dungeon / 46 MA2 follow-ups
- slice 44 done: Lithkren vault — `minigame_lithkren/`: doors, barrier, stairs/trapdoor; mural counters deferred; scripts 5677; pack 0 errors
- slice 45 done: Smoke Devil dungeon face-mask gate on `minigame_thermy/` enter (slice 25 already had locs); softtimer damage deferred; scripts 5694; pack 0 errors
- slice 46 done: MA2 Kolodion follow-ups — symbol grant + god-cape imbue hand-in; hot/cold deferred; scripts 5737; pack 0 errors
- 2026-08-04: extended queue with 47 Brimstone ring / 48 Zenyte forge / 49 Rev mid crevice
- slice 47 done: Brimstone ring combine — eye+fang+heart → `brimstone_ring`; scripts 5743; pack 0 errors
- slice 48 done: Zenyte forge — `minigame_zenyte/`: temple trapdoor/rope + wall-of-flame fuse; scripts 5802; pack 0 errors
- slice 49 done: Rev caves mid crevice — Jump-Down + 100k fee; coords approximate; scripts 5815; pack 0 errors
- 2026-08-04: extended queue with 50 Catacombs shortcuts / 51 ethereum bracelet
- slice 50 done: Catacombs shortcuts — stepstones + cracks; skill_features reqs; scripts 5871; pack 0 errors
- slice 51 done: Bracelet of ethereum — charge/check/toggle/uncharge/dismantle; inv_setvar gap noted; scripts 5896; pack 0 errors
- 2026-08-04: extended queue with 52 wildy weapons / 53 rev stairs exits
- slice 52 done: wildy weapons charge stubs — Thammaron's / Craw's / Viggora's; inv_setvar gap noted; combat/dismantle/swap/ancient sceptre deferred; scripts 5981; pack 0 errors
- slice 53 done: rev stairs exits — `wild_cave_exit_low/high` → surface trapdoor tiles; surface trapdoors op-less (one-way); scripts 5997; pack 0 errors
- 2026-08-04: extended queue with 54 fee caves / 55 ancient sceptre / 56 upgraded wildy weapons
- slice 54 done: Escape Caves (`wild_cave_exit01`) 50k fee enter/exit + Check-Fee; rev low/high also fee-gated; scripts 6021; pack 0 errors
- slice 55 done: Ancient sceptre — Eblis combines icon+`staff_of_zaros` (queue note was wrong vs wiki); SotN/elementals deferred; scripts 6035; pack 0 errors
- slice 56 done: Accursed/Webweaver/Ursine — skill-85 trophy combines + ether charge stubs; Ferox/specials deferred; scripts 6198; pack 0 errors
- 2026-08-04: extended queue with 57 boss dens / 58 Ferox upgrade NPCs
- slice 57 done: Callisto/Venenatis/Vet'ion dens — Enter/Peek/Check-Fee + shared fee + medium diary; scripts 6106; pack 0 errors
- slice 58 done: Ferox upgrade NPCs — Phabelle/Derse/Andros 500k fee combines; scripts 6120; pack 0 errors
- 2026-08-04: extended queue with 59 remaining dens / 60 elemental sceptres
- slice 59 done: Hunter's End / Web Chasm / Skeletal Tomb (Artio/Spindel/Calvar'ion); hard diary; scripts 6132; pack 0 errors
- slice 60 done: elemental ancient sceptres — quartz attach/swap/dismantle; DT2/passives deferred; scripts 6175; pack 0 errors
- 2026-08-04: queue idle after 60; extended with 61 SotN/DT2 gates / 62 Ferox bank fee / 63 wildy dismantle / 64 Peek KC
- slice 61 blocked: SotN/DT2 quests not authored
- slice 62 blocked: no Ferox bank fee (wiki); respawn fee → 65
- slice 63 done: wildy dismantle (7500 ether / reverse upgrades) + sceptre Swap (a); scripts 6242; pack 0 errors
- 2026-08-04: added 65 Ferox respawn (blocked on death.rs2 respawn coord)
- 2026-08-04: extended with 66 ethereum absorb / 67 rev prayer drain / 68 wildy specials
- slice 64 done: Peek 20 KC + den occupancy stub + kill credit varps; scripts 6245; pack 0 errors
- slice 66 done: ethereum 75% absorb + charge consume + kill auto-collect stub; `1189=revenant`; scripts 6304; pack 0 errors
- slice 67 done (withdrawn): no rev-cave prayer/dark drain in wiki or Kronos
- slice 68 blocked: special-attack combat model missing
- slice 69 done: clear rev/escape cave fees on death; scripts 6302; pack 0 errors; PKer fee loot deferred
- 2026-08-04: extended with 70 wildy passives / 71 ethereum tolerance
- slice 70 done: wildy weapon +50% acc/dmg in Wildy + ether consume + bow ammo-free; scripts 6340; pack 1 unrelated (`^qot_chest` dup)
- slice 71 blocked: ethereum tolerance needs LC `.hunt` `check_inv`; engine hunt is nearest placeholder — no bracelet ids in C
- 2026-08-04: extended with 72 Fountain of Rune / 73 Wilderness Sword / 74 Imbued heart / 75 supply chests
- slice 72 done: Fountain of Rune recharge + eternal glory 1/25000 (wiki); scripts 6586; pack 0 errors
- slice 73 done: Wilderness sword 3 daily / 4 unlimited FoR tele; scripts 6642; pack 0 errors
- slice 74 done: imbued/saturated heart Invigorate + saturate; death CD clear; scripts 7040; pack 0 errors (no sibling park)
- 2026-08-04: **policy:** never park/silence sibling lanes (`*.skip`,
  `skill_construction`/`minigame_mta` moves) — all queues + PORTING_GUIDE §7 +
  loop prompts updated; Kronos agents must not mute POH/MTA to compile
- 2026-08-04: `map_multiway` (1015) hosted, so slice 23's Retribution multi AoE is
  unblocked — the gap-log row above was wrong to say "opcode exists": it was
  declared and never hosted, which from content is indistinguishable from an
  empty zone set, since both answer 0. Data (`maps/multiway.csv`, 4,697 zones)
  ported verbatim from the reference; measured 0 in Lumbridge, 1 at 2984,3912.
  Landed as part of a sweep of all eleven queue-named unhosted opcodes — full
  accounting in [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md),
  same date. `npc_sethuntmode` and `busy` from that sweep also matter here: the
  boss dens and wildy specials both wanted per-npc aggression
- slice 76 done: Rogues' Castle chests — trap Open, Search for traps 84/701.7/34t, medium diary loot + hard qty, rogue aggro; clue/RoW deferred; scripts 7266→7365 (parallel PC); pack 0 errors; no sibling park (fixed PC duplicate constants/varps to compile)
- slice 77 done: Lava Dragon (74) + Lava Maze (82) stepping stones in `agility_shortcuts_osrs.rs2`; pack 0 errors
- slice 82 done: Deep Wilderness crevice Agility 46 + medium diary; stairs enter deferred; scripts 7365; pack 0 errors
- 2026-08-04: queue idle after Misc.java shortcuts; extended with 83 WC Guild / 84 Mining Guild / 85 shrine
- slice 83 done: Woodcutting Guild gates WC60 + BV hand-in moved off bonevoyage oploc; ropes + dungeon; scripts 7395; pack 0 errors
- slice 85 done (in 83): shrine bird eggs → `bird_nest_seeds` + 100 Prayer XP; scripts 7396; pack 0 errors
- slice 84 withdrawn: Mining Guild already LC-ported
