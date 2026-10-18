# Finishing the Chambers of Xeric (2026-10-18)

Owner, 2026-10-18: "Using what you learned while implementing Theater of Blood,
the content and client issues you encountered, finish implementing chambers of
xeric." Then: **"You are to match OSRS as close as possible."**

The method is `docs/CONTENT_SOLVER_MANUAL.md`: implement each room from the best
source, then prove it with a bot that plays it as a strong human does and sees
only what a client is sent. The Great Olm is already done that way
(`cox2_olm.lua`, `test/raids/cox_olm_solo.lua`, 96/96). This plan covers
everything else, up to a solo raid played from the mountain to the Ancient chest.

## What was found on 2026-10-18

| area | state | OSRS |
|---|---|---|
| layout | a 4x2 serpentine per floor; all 14 rooms over two floors, resource last | one of **26 patterns** (`#SCPFC*#CSPCF*` ...): start room, 3-5 combat rooms dealt from a fixed **rotation** run either way, 2-3 puzzles (a similar rotation), 1-2 scavenger rooms, a farming room and the floor ends ("floors of seven or eight rooms", wiki) |
| room joins | none: the `Passage` loc (`raids_doorway`, op Enter) has no handler | Passage moves you through. Shamans, Muttadiles and Vespula have entrance tendrils (a 2-tile hop that hurts on re-use). Crystals block exits until a room is done |
| floor ends | the resource room holds the hole | floor-end room (`FLOOR_END_DOWNSTAIRS`) with the hole; floor-start room with the rope; energy wells; storage units |
| npc timers | 1 abort in a whole-raid probe (a crab beam's timer with no raider in the room) | none |
| room tests | all broken, moved to `reference_scripts/` | a v2 solver per room |

## The work

### F. Foundation (root session; everything else stands on it)

- **F1 Layout.** Port the 26 patterns (`RaidPattern.layoutCodes`, the same list
  RuneLite's `LayoutSolver` carries), the combat rotation (`BossPattern`: two
  rings run either way, as RuneLite's four rotations and the wiki's "clockwise or
  counterclockwise" say) and the puzzle pairing. Rooms are placed by the palette
  walk (`MapPalette`): N/E/W steps, variant from the turn, rotation from the
  heading. The layout lives in instance registers, not a pure function of a seed.
- **F2 Traversal.** The Passage op, the entrance tendrils, the exit crystals, the
  raid start room (entrance from the lobby), floor start and end rooms (rope and
  hole), energy wells and storage units, at their template positions.
- **F3 Test entry.** `::coxtest <room>`: build a raid whose layout holds that
  room and stand the player outside its entry passage. Room tests start there.
- **F4 Shared CoX solver layer.** The kit, the CoX potion and supply helpers,
  passing between rooms, and a room runner on `QD.T2`.
- **F5 The raid.** `test/raids/cox_raid_solo.lua`: every room in order, then
  Olm, then the chest. Green on 48 seeds, plus one live seed with an identical
  tick log.

### R. Rooms (one owner each, in parallel)

| unit | rooms | content files it owns |
|---|---|---|
| tekton | Tekton | `cox_tekton.rs2` |
| vasa | Vasa Nistirio | `cox_vasa.rs2` |
| vanguards | Vanguards | `cox_vanguards.rs2` |
| vespula | Vespula | `cox_vespula.rs2` |
| muttadiles | Muttadiles | `cox_muttadiles.rs2` |
| guardians_mystics | Guardians, Skeletal mystics | `cox_guardians.rs2`, the mystic procs in `cox_minions.rs2` |
| shamans | Lizardman shamans | the shaman procs in `cox_minions.rs2` |
| icedemon_tightrope | Ice demon, Tightrope | `cox_icedemon.rs2`, the tightrope procs in `cox_puzzles.rs2` |
| crabs_thieving | Jewelled crabs, Thieving | `cox_crabs.rs2`, the thieving procs in `cox_puzzles.rs2` |
| supplies | both scavenger rooms, the resource rooms (farming, fishing, bats, gourd tree, herblore), storage | `cox_scavengers.rs2`, `cox_resource.rs2`, `cox_herblore.rs2`, `cox_bats.rs2`, `cox_potion.rs2` |

Each unit runs the manual's phases for its rooms:

1. **Sources** under `docs/minigames/cox/sources/`: the wiki article and its
   strategies (raw), Synq's chapter (`synq_transcript.md`), the RuneLite /
   plugin constants, Near-Reality's room code
   (`~/Documents/git_repos/RSPS-NEAR-REALITY/.../chambersofxeric/`) for ids.
2. **Tick spec** `docs/minigames/cox/rooms/<room>_spec.md`: every hazard's
   decide tick, read tick, geometry, damage (solo-scaled), prayer read, and tell.
   Cite a source on every row; rule every conflict.
3. **Content audit + fixes**: an audit table (mechanic / ours / OSRS / verdict)
   in the same file, then the content made to match.
4. **Solver plan** `docs/minigames/cox/rooms/<room>_solver.md`: perception
   (which `api_drive` verbs, which are missing), threat field, mode machine,
   kit, PASS criterion.
5. **Solver + test** (after F3 lands): `script/plugins/quest_driver/cox2_<room>.lua`
   (a `QD.T2` room, like `cox2_olm.lua`) and `test/raids/cox_<room>.lua`. Probe 1
   seed, then 8, then 48.

## Rules for everyone working here

- **Match OSRS.** Where sources disagree, the higher one wins: Jagex > recorded
  data > wiki > plugin constants > Near-Reality > our code. Write the ruling down.
- **One owner per file.** Shared configs (`cox.constant`, `cox.npc`, `cox.varp`,
  `cox.loc`) take additive blocks named for your room, via small Edit calls. No
  rewriting of another unit's lines. `cox.rs2`, `cox_layout.rs2`,
  `cox_points.rs2` and `cox_death.rs2` belong to the root session (foundation).
- **Never** `git stash`, `checkout`, `reset` or `clean`, and never mutate a file
  to prove a test fails (CLAUDE.md). Do not commit; the root session commits.
- **Builds.** Private server objdir:
  `make -C src -j4 PLATFORM_OBJ_BASE=build_cox_<unit> torirsserver`
  (binary `src/build_cox_<unit>_opt/torirsserver`). Shared packs only through
  `python3 tools/cox/locked_packs.py`. If the compile fails in a file you do not
  own, wait and retry; do not fix it.
- **Solver Lua** is one chunk with every other part: `luac -p` your file before
  every save (write to a scratch copy, check, then copy in), keep top-level
  `local`s to one or two (state under `QD.*`). A new part file is listed in
  `src/plugin/torirs_plugin_drive_parts.h`; the root session adds the CoX ones.
- **Runs.** Scriptrun only during development (`--scriptrun ... --name <seed>`),
  `TORIRS_STDERR_UNBUFFERED=1`, read `grep -n abort` and `no primary player
  bound` in stdout after every run. NPC timers run with no player.
- **Do not thrash.** Plan, code in full, probe one seed, read everything, fix in
  one batch.

## Foundation, as built (2026-10-18)

- **F1 layout** (`cox_layout.rs2`): the 26 patterns (generated from
  Near-Reality's list), the two combat rings either way (confirmed against the
  wiki's rotation image, `sources/wiki_Chambers_of_Xeric_rotations.png`), the
  puzzle ring Ice demon / Tightrope / Crabs / Thieving (RULING: the wiki only
  says "similar rotation"; this order makes its two named pairs neighbours),
  scavenger small/large and resource A/B dealt without repeat. Every floor of
  every pattern is a 4x2 block, so the instance grid is unchanged. Cells live in
  instance registers 112-119 (pattern 120, generator 121, started 122).
  RULING: the floor-end room is the cache's descent hole on floor 1 and the boss
  hole on floor 2 (Near-Reality draws either; the loc names say which floor).
  `::coxrunlayout 1..4` checks walk adjacency, start/end rooms, coverage of every
  room, no combat repeat, the floor-2 arrival tile.
- **F2 traversal** (`cox.rs2`): Passage (`raids_doorway`), hole down
  (`raids_descentto2`, fade 1512/1513, land on the rope, step off), rope up
  (`raids_ascentto3`), hole to Olm (`raids_bossentrance`). Passages exist only in
  combat/puzzle rooms (exit), the raid start room (exit) and floor end (entry);
  scavenger, resource and floor start rooms are open.
- **F3** `::coxtest <room> <n>`, `::coxlayout`.
- **F6 start** (`cox.rs2`): the raiding party side-panel (`raids_sidepanel`) in
  the chat-channel tab; its own CS2 shows "Start raid" to the leader at
  `raids_client_progress` 0; the button starts the raid (clock, progress 1, the
  lobby bank chest shuts). The start room's Passage holds until then.
  Progress 2 on floor 2, 4 at Olm. Party recruitment is not built (solo only).
- **F4 shared Lua** (`cox2_raid.lua`): `api_drive.instance_zone` (new, both
  lanes: the REBUILD_REGION template grid a client is sent) -> the room I stand
  in, its variant and turn; template<->world tiles; the exit Passage; walking on
  to the next room; the kit; `t.raid.cox_test_enter(room, n)`.

## Cross-cutting items from round 1 (root session)

| item | from | state |
|---|---|---|
| a room's own rule on a player's hit: `cox_combat.rs2` `~cox_prepares_hit` / `~cox_prepare_player_hit`, called from `player_hit_npc_prepare.rs2` | crabs, muttadiles, vanguards | DONE (Muttadiles wired; a room adds one line each) |
| crabs take 0 damage | crabs | DONE (crab branch zeroes after the paint) |
| every hitsplat on an npc row (`splats` = {damage, type, start} x up to 4), both lanes | vanguards | DONE (scriptrun + live) |
| CoX tinderbox op claimed by Corsair Curse | ice demon | DONE (raid first, quest outside) |
| walk joins between rooms | F2 probe | DONE: 46/54 walk in; the 8 others are tendrils (Vespula, Shamans) and the floor end's entry Passage, which take an op |
| multi-tile npc chaser in `aplan` (3x3, 5x5 npcs move like OSRS) | muttadiles | OPEN (engine) |
| death animation before the death script (a "survive at 1 hp" rule) | vanguards | OPEN (engine) |
| a player still swinging at a vanguard shell after the shuffle starts | vanguards | OPEN |
| `npc_freeze` renew / 5-tick immunity | crabs | OPEN (engine) |
| a headbar with no hitsplat (thieving hunger bar, ice demon thaw bar) | crabs/thieving, ice demon | OPEN (check both lanes) |
| heals never reached the client (`npc_statheal` sent no bar) | vespula | DONE: new internal `TORIRSSERVER_NMASK_HEADBAR` -> a rev-239 bar-only update on a hitpoints heal; Olm 2/2, Maiden 2/2 |
| CoX multi-combat | vespula | combat already multi in any instance; `map_multiway` now agrees (instances answer 1). The client's multi icon is never sent (no multiway map data) -- OPEN |
| npc magic defence from Defence vs Magic (ice demon) | ice demon | OPEN (shared `~npc_defence_roll`; check the OSRS rule) |
| thieving scavenger physically blocking the corridor | thieving | OPEN |
| E1: an npc swung before its own timers/queue in the tick (flinch a tick late; npc_queue(N) from a swing fired T+N-1) | guardians, shamans, tekton | DONE: swing moved into the npc's turn after timers+queue (Npc.processNpc order); A/B same seeds: Olm 8/8 -> 8/8, ToB Maiden 8/8 -> 8/8 |
| E2: line-of-sight / line-of-walk verb on scriptrun (`server_los` is live-only) | guardians/mystics | OPEN |
| E3: "where does this npc's naive step land" (corner traps) | mystics | OPEN |
| CoX tinderbox / mystic portals / pickaxe Take unobserved | guardians/mystics | round 2 |
| prayers blocked during Vasa's special: a block-all in `prayer.rs2` (only protection prayers are refused now) | vasa | OPEN |
| a 0 hit on an invulnerable target still grants XP (the funnel grants before `[ai_queue2]`) | vasa | OPEN (check OSRS) |
| storage Shared/Private stores; resource room A's storage hotspot unreachable | supplies | round 2 |
| raid cooking (kindling, fire, raw->cooked) needs firemaking/cooking files | supplies | OPEN |
| spec-weapon hooks (Tekton's guaranteed first DWH/elder maul hit) | tekton | OPEN |
| entrance tendrils (noxious, septic, spirit) ops | muttadiles, vespula, shamans | round 2, each room's unit |
| selftests pinning old behaviour (#106 mutta; ice demon/tightrope rows) | units | round 2, each unit fixes its own |

## Status

Room tests, scriptrun, 48 seeds each (round 2/3 reports; each unit's
`rooms/<room>_solver.md` has its failures):

| room | 48 seeds | notes |
|---|---|---|
| Shamans | 48/48 | live seed identical but for a 1-tick absolute shift |
| Guardians | 48/48 | needs a pickaxe of the raider's own (kit: dragon pickaxe) |
| Mystics | 48/48 | salve (ei) in the kit |
| Vespula | 48/48 | after E2 (declared blockwalk on the server) |
| Tekton | 48/48 room, 40/48 incl. way on | live identical but for a shift |
| Vanguards | 48/48, 0 trample | its live diff was pack drift between the runs, not the lane |
| Vasa | 47/48 | needs stab (dragon shortsword) |
| Muttadiles | 48/48 | |
| Ice demon | 46/48 | |
| Tightrope | 47/48 | |
| Crabs | 45/48 | 3 seeds take one orb hit |
| Thieving | 48/48 | |
| Scavengers, resource prep | 48/48 each | |

Foundation (2026-10-18, round 3):
- **Walker.** Hops toward a target past the scene edge (`cox_walk_toward`) and reads the room ahead at its near tile (`room_ahead`). It stops at the door of a tendril room. The nav probe (whole raid, `::coxclearhere` for every room) gets through 9/10 seeds; nav10 is the ice demon itself standing in its Passage, which `::coxclearhere` does not remove.
- **`::coxtest`.** It searches, then builds in a queued script with its own instruction budget (n >= 44 overran).
- **The whole raid** (`test/raids/cox_raid_solo.lua`):
  - The relay plays each room from where the room before left the player. A solver's room is entered by its solver.
  - raid5 played all 13 rooms and both holes, then died at Olm with no healing left.
  - The other seeds die in late rooms for the same reason: the given doses are gone by floor 2 (round 3: crabs fixed, others in progress).
  - In-raid potion making (supplies unit, `test/raids/cox_raid_solo_prep.lua`): seeds from bosses and floor-end weeds, juice/stinkhorn/cicely from scavengers, brewed in the floor-2 resource room, stored in private storage, the Olm kit restocked at the floor-2 end.
    - With a floor-1 allowance, raida1 and raida4 complete the whole raid: Olm killed, chest looted.
    - Nothing in the raid supplies floor 1: seeds come only from bosses and weeds, and a resource room can be room 1. DECIDED (the OSRS way): the kit brings outside Saradomin brews and super restores for floor 1, and every solver drinks shared supply families (`QD.COX.SUPPLY`) instead of hard-coded raid-potion lists.
  - **Fixed along the way:** antidote++ gave no poison immunity (shared content, now the wiki's 12 minutes). The relay cures poison between rooms and enters a floor end past its own Passage.
  - **Next:** the shared supply families, then fold the prep relay into `cox_raid_solo.lua`. Then a 48-seed whole-raid sweep, then one live seed on a frozen pack.
