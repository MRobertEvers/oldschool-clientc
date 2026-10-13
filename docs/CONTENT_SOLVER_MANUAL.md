# Implementing content, and proving it with a solver: the manual

Written 2026-10-13 from the Theatre of Blood work (Normal trio relay 96/96, Hard
Mode five-man relay 128/128 then 254/256), so the next piece of content (Chambers
of Xeric first, then anything with mechanics that can be got wrong) is built the
way that work ended up being built, not the way it started.

The method in one paragraph: **implement the content from the most authoritative
source you can find, then write a bot that plays it the way a strong human does,
perceiving only what a real client is sent. The bot is the test.** A solver that
cannot pass is either a solver bug or a content bug, and in ToB it was a content
bug far more often than anyone expected: almost every room had one (crab waves
firing every tick, blackstorms always hitting one seat, 3 blood pools instead of
5, ranged hits a tick late, a maze that ended on the wrong row, Xarpus with no
ranged defence, a sound call that stopped a phase from ever ending, a missing
`maxrange` that made the Hard Mode tank invisible). None of them showed up in a
selftest. They showed up because a bot that plays to a published strategy could
not make that strategy work.

Sources this was distilled from: `docs/minigames/theater_of_blood/solver_lessons.md`
(lessons 1-72), `TOB2_DESIGN.md`, `tob2_rooms/ORACLE_RULES.md`, `ROOM_SOLVERS.md`,
and the owner's corrections in the two ToB sessions (2026-10-09..13).

---

## Part 1. The rules that are not negotiable

These are owner rulings. Each was given after it was broken; most more than once.

1. **Do not thrash.** Never make a small edit and then run minutes of tests.
   "You are making micro changes and running 5 minutes of tests again. You need to
   not do that. Identify many fixes, implement, then test." Plan, code in full, run
   one probe, read everything, fix in one batch.
2. **Plan before any run.** "Read the lessons learned. Create a plan for your
   implementation for each room first, THEN run the tests." The first run of a
   room is a probe on one seed, never a design tool.
3. **The bot perceives only what a client is sent.** No server varp reads, no
   trace channels, no debug state. Scriptrun decodes the same packets the client
   decodes, through the client's own C code. A bot that reads the server passes
   and then fails in the real client.
4. **Scriptrun and the live client give identical tick logs for the same seed.**
   "IT MUST NOT DIVERGE. YOU MUST ALWAYS ENSURE THEY HAVE IDENTICAL TICK LOGS."
   Develop on scriptrun (100x real time); prove one seed live; diff the logs.
5. **Authoritative sources win, and you fix what they say.** For OSRS content the
   order is: Jagex statements (patch notes, Mod posts quoted on the wiki) >
   recorded game data (Blert for ToB: "Blert is authoritative. The game must match
   that.") > wiki article text > measured plugin constants > a reference server
   (Near-Reality / Zenyte) > our existing code. A reference server is the
   **baseline** to port from (it carries the ids: animations, graphics,
   projectiles, locs), then each fact is refined against the higher sources.
6. **Never hand a found content bug back.** "No you need to fix this." If the
   solver exposes a content mismatch, fix the content at its source, cite the
   source in a comment, and keep going.
7. **Never work round content in the solver.** A failure caused by the content or
   the engine is fixed there. Equally: do not "fix" real behaviour because it is
   inconvenient (a stuck Verzik crab is Blert's too).
8. **The kit is fixed once the owner names it.** "The committed void kit is fine.
   I want that to pass." Solve with what the kit gives. Never drop equipment;
   only supplies change hands. Every raider walks between rooms itself.
9. **Green is a criterion written down before the sweep.** ToB: no raider dead,
   0 wrong-style hits, room-specific zeros (Bloat 0 damage, Verzik P3 0 avoidable
   damage). Judge on 48+ seeds; 8-seed results flipped at 48 twice.
10. **Never idle on a question.** "Did you give up?" If a decision is pending,
    keep working on everything that does not depend on it.
11. **Give the hardest part the full loop first, not last.** "How do you improve
    the other rooms so much and then just bail on p3?"
12. **Isolate.** Other sessions build from the shared checkout. Work in a git
    worktree with a private objdir (`PLATFORM_OBJ_BASE=build_<you>`) and a content
    worktree; never mutate the shared tree to prove a test fails (CLAUDE.md).

---

## Part 2. The pipeline, phase by phase

### Phase 0. Set up an isolated lane (once)

```sh
S=<session scratchpad>
git worktree add $S/wt -b <branch> v3
pin=$(git ls-tree v3 OSRS-Content | awk '{print $3}')
rm -rf $S/wt/OSRS-Content
git -C OSRS-Content worktree add -b <branch> $S/wt/OSRS-Content $pin
for c in cache.osrs184 cache.osrs230 cache.osrs239 cache.osrs239.baked; do ln -s $PWD/$c $S/wt/$c; done
cd $S/wt
make -C src PLATFORM_OBJ_BASE=build_<you> -j8 torirsserver           # scriptrun server
make -C src PLATFORM_OBJ_BASE=build_<you> EMBED_SERVER=1 -j8 torirs   # live client (embed)
make -C src torirsserver-packs                                         # 14-20 s full, ~1 s an edit
```

(`git submodule update --init` fails from a worktree; the content worktree is the
way.) The solver Lua is compiled into the binary through
`src/plugin/torirs_plugin_drive_parts.h`: a NEW part file needs a rebuild; an
edit to an existing part is read per run.

### Phase 1. Gather the sources, before reading our code

For every mechanic, find:

- the **wiki** article and its Strategies page, fetched raw (`?action=raw`), saved
  under `docs/minigames/<content>/sources/`;
- **recorded data** if it exists (Blert for ToB; for others: RuneLite plugin
  constants, community tick sheets, frame-stepped videos);
- **the published human strategy** the bot will play (ToB: the wiki's
  Learner/Void guide; CoX Olm: Synq's guide transcript and the wiki's 4:1);
- **the reference server's** implementation, for structure and ids.

Read the guides fully. "Did you read the strategy guides???" was asked three
times; each time the guide changed the room. Calibrate every mechanic, not just
the boss's attacks ("Did you run the blert statistics on the mazes and the
tornados?").

### Phase 2. Write the tick-level spec from the content

Before any solver code, write down for every hazard (lessons 1, 2, 19):

- **the tick it is decided** and **which tick's positions it reads**. The server
  runs npcs before players: a hazard decided on tick T reads where players stood
  at the END of T-1. A ball landing in a player queue reads the target at end of
  L-1, a lower pid at end of L, a higher pid at end of L-1 — derive every read
  tick from the queue it runs in.
- **its geometry** (tiles, footprint, line of sight, inclusive or exclusive
  zone edges — Xarpus' screech quadrants are inclusive, centre lines are in both);
- **its damage** (expected and max) and the prayer that reduces it, and **when
  the prayer is read** (at the swing, at release, at landing — this differs per
  attack and was the most-disputed fact in ToB; settle it from recorded data);
- **what the client sees** and when: the animation, projectile, graphic, loc or
  message that is the tell. A mechanic with no tell is unplayable for a human and
  unsolvable for an honest bot; that is a content bug.

Put it in a spec table (`encounters/<room>.tsv` for CoX; `solver_specs/<room>.md`
for ToB) with a source per row. Where sources disagree, write both and rule
(wiki outranks cache comments; recorded data outranks the wiki on timing).

### Phase 3. Audit and implement the content

Diff the spec against the content line by line and write the audit as a table
(mechanic / ours / reference / verdict). Then implement. Rules learned the hard
way in the content language:

- **NPC timers run with no player bound** (lesson 42). A `[ai_timer]` that reads
  or writes a player `%varp`, calls `mes`, `sound_synth`, or anything player-scoped
  ABORTS on that line, every tick, and the server log is the only place it says
  so. Raid state lives in **instance registers** (`map_instance_var_get/set`,
  128 per instance) or npc vars (`npc_var_get/set`); per-player effects go
  through a `huntall` loop with the dotted forms (`.mes`, `.%varp`). Grep the
  server log for `abort` after every run: in ToB the Xarpus HM acid ring was
  never laid on any raid for this reason and nobody had looked.
- **There is ONE hunt iterator.** A `huntall` nested inside another (even in a
  debug proc) silently ends the outer loop (Maiden threw 3 pools instead of 5).
- **`npc_findallany` re-points the active npc** — hoist what you need into locals
  before a second lookup.
- **`[ai_queue2,<npc>]` is the engine's damage hook.** Adding a handler there for
  a probe turns every hit into whatever it does.
- **Queue delays**: `queue*(x,0)` runs next tick, `queue*(x,1)` two ticks later;
  a delay-0 re-queue from a queue script into a higher slot runs again in the same
  pass (an acid chain burned 16 ticks at once).
- **Do not put `npc_add` between two `random()` draws that must be independent**
  (Verzik's crab coins came up HHH far too often). Draw every coin first.
- **`npc_setmode(none)` does not cancel a queued walk**; a walk to its own tile is
  the stop. A standing mode (`playerface`) runs before the waypoint drain, so a
  walk queued beside it is never taken.
- **Visuals are part of the mechanic.** Animations, projectiles
  (`projanim_pl/_map/_npc`), graphics (`spotanim_map`, whose last argument is a
  start DELAY in client cycles, not a duration), locs (`loc_add/loc_change/
  loc_anim`). A boss whose visible body is a loc (Olm, Verzik's pillars) must
  animate the loc; the npc carries hitpoints and targeting only.
- **No implicit defaults.** Every server record states every field
  (`cachepack --defaults=error`). Missing `maxrange` silently broke the HM tank.
- **Check the wiki for `{{Gone}}`** before porting a room a 2017-era server
  defines (Zenyte's scavenger runt is deleted content).
- After a content edit: `make -C src torirsserver-packs`, then run. A failed
  compile leaves the OLD pack in place; check the build output.

### Phase 4. Make the tells perceivable on BOTH lanes

The bot reads only the drive API (`api_drive.*`), served by the live client's
plugin layer (`src/plugin/torirs_plugin_drive_*.c`) and by scriptrun
(`src/torirsserver/torirs_server_scriptrun.c` over `src/scriptrun/`). Each verb
must answer identically on both lanes:

- **Perception comes from packets, never from asset-gated state** (lesson 66).
  The live client spawns a projectile, graphic or loc change after its models
  load, a tick late. Record the packet when it is applied, and answer from that.
- **A stamp names the tick that SENT it** (lesson 67); `api_drive.tick()` is the
  server tick of the last fence applied (lesson 63).
- **Npc rows are on the server tile** (`server_x/z`), not the drawn tile.
- **Diff the two verb sets before a live run** (`cast_npc` existed on scriptrun
  only and the live freezer's first cast was a Lua error).
- Verbs the solver needs that do not exist are engine work to do first, not a
  reason to change the strategy ("a missing engine verb is a gap to close").

### Phase 5. Write the room plan, mapped to the lessons

`docs/minigames/<content>/<room>_solver.md`: the threat field (each hazard as a
plan term with its window and damage), the strategy mode machine (states,
transitions, roles), targets and their worth, the probes (facts the spec cannot
settle, each a row the first run prints), and the PASS criterion. Check it
against `solver_lessons.md`. Only then code.

### Phase 6. Code the solver in the layered shape

The architecture that won (TOB2_DESIGN.md), and why: v1 modelled the world well
and the player badly — a click starts a server process (a walk queue, an attack
that follows its npc, a held op that ends a swing, a refused press), none of that
was in the search, so every room grew ORDER special cases found one seed at a time.

```
 observe -> PREDICT -> STRATEGY -> PLAN -> EXECUTE -> server
            (room)     (room)      (shared C) (shared Lua)
   ^                                                  |
   +------------ MONITOR: predicted vs measured ------+
   + RESOURCE layer over the whole run (supplies as one budget)
```

| layer | owns | where |
|---|---|---|
| PREDICT | the threat field: what can hurt which tile at which absolute tick, expected and max | room file `predict(S, F, B)` |
| STRATEGY | a mode state machine: roles, goals, targets and their worth, prayer, gear, specials | room file `strategy(S, F, B) -> intents` |
| PLAN | a beam search over MY ACTIONS (stay / walk / attack) with the actuator state in the node | C `action_plan.c`, `api_drive.aplan` |
| EXECUTE | one state machine per output channel (interact, held, worn, prayer, special, run, loc op) | `tob2_exec.lua` |
| MONITOR | every tick: last plan's step 1 vs what happened, mismatch counts by rule | `tob2_monitor.lua` |
| RESOURCE | stock / need / reserve of supplies across rooms; the rescue on predicted death | `tob2_budget.lua`, `QD.T2.rescue` |

A new room is `QD.T2.register(name, { init, enter, predict, strategy, done,
summary })`; the shared loop (`tob2_run.lua`) runs monitor → resources → builder
→ predict → strategy → schedule → plan → rescue → emit → decision log each tick.

**One currency: expected HP.** A threat on my tile costs its expected damage after
my prayer; its max runs down a worst-case HP (`hp_worst`, lethal at ≤ 0 whatever
the label); an owner rule ("never stand on a pool") is `forbid`; a swing that
lands is worth `value = E[damage] × HP_PER_BOSS_HP` (team damage taken per tick ÷
team damage dealt per tick, measured from tick logs); soft terms (stations,
spread) are in the same units, so their size is a statement.

**The tick mapping every layer uses** (ORACLE_RULES R1-R5, M1-M6):
- `F.tick = d` is the tick whose END I see; my click goes out between d and d+1
  and moves me during d+1; plan step k is my tile at the END of d+k.
- An npc decision on T reads raider tiles at the end of T-1.
- A prayer pressed after seeing d is in force for swings at d+1.
- Walk routes are the client BFS (W,E,S,N,SW,SE,NW,NE), stepped diagonal-first,
  2 tiles a tick running; only tick-end tiles are sampled by most floor hazards;
  out of run energy every tile is a tick-end tile.
- Melee reach is `collision_map_reached` (wall flags on my edge, never the
  footprint's occupancy). An attack press moves you before the next look; the
  chase is the BFS route to the reach set.
- Every backpack op (eat/drink/wield) ends the attack but not a walk; re-press.
  Food and potions have separate 3-tick timers; all potions share one.
- A stunned player's packets are dropped.

**Shared choices are computed by every seat from the same view, never sent**
(SAME VIEW, SAME ANSWER): the players list only holds seats within ~15 tiles and
other seats' HP is invisible; build shared decisions only from facts all see.
When seats must coordinate supplies, use real public chat on both lanes.

**Key every event on the tick it is FIRST SEEN**, never on lane stamps
(`seq_tick` is one tick early; `face_tick` too). Key a hazard on what makes it
distinct (tile + landing tick), never an id the engine reuses.

**Cost and budget traps:** the Lua budget is 400,000 instructions per tick on
both lanes; keep two-times headroom (`TORIRS_SCRIPTRUN_STEP_BUDGET=200000` while
developing). Floods and searches belong in C behind one binding registered on
both lanes (`aplan`, `meet_rank`). The live sandbox strips `setmetatable`,
`pcall`, `load`, `print`, `require`; scriptrun strips the same. All parts are one
Lua chunk (200-local limit): keep state under `QD.*`, `luac -p` the whole chunk
before installing a file another run reads.

**Planner traps found in ToB:** a ban that applies only at step 1 makes every plan
defer its swing to step 2 forever; a hard rule outranking a lethal hazard trades
the hazard away (keep the rule a cost unless it is truly forbidden); a beam
ranked by cost drops the chase that is furthest along (reserve per target); a
builder that drops a field (`B.chaser` lost `forbid`) prices a lethal touch as
half HP — assert every field you send.

### Phase 7. The probe: one seed, read everything

```sh
TORIRS_STDERR_UNBUFFERED=1 ./src/build_<you>_opt/torirsserver --scriptrun test/raids/<test>.lua \
   --bots <n> --name <seed> --ticks <N> --session $S/runs/<tag>/<seed> \
   --fixture test/raids/fixtures/<fixture>.ini > $S/runs/<tag>/<seed>/stdout.txt 2>&1
```

Verdict: the `SCRIPTRUN PASS|FAIL` line. Then read, before any edit:
- `stdout.txt` for **script aborts** (`grep -n abort`), not just the verdict;
- the decision log, one `mark` row per seat per tick:
  `awk -F'\t' '$3=="mark" && $10 ~ /^t2 p1 /' ticklog.tsv | cut -f10`
  (`t2 p<pid> t<tick> <mode> @x,z <act>>x,z[!] r<risk> l<loss> v<value> hw<hp_worst> [why] ...`);
- the monitor's counts ("mon move a/b swing a/b ..."), which name the model rule
  that was wrong;
- the tick log's `hit_player` rows by source (columns: serial, tick, kind, a..f,
  label, g; `hit_player` a=pid, c=damage, f=npc type).

Write every finding down; fix them as one batch; probe again; then sweep. Hand-
check one seed of every new sweep before reading the sweep (a wrong fixture path
failed 128 seeds at startup, twice).

### Phase 8. The replay oracle and the sweep ladder

- **Score policy changes offline over existing tick logs first** (seconds), then
  implement, verify on 8 seeds, then sweep (owner's choice after a 45-minute
  sweep per micro-change). Tools that paid off: a movement oracle, a damage/heal
  budget per room per seat by source, per-mechanic scorers (ballscore, touch,
  crabhit...), `tlcmp.py`/`kindcmp.py` tick-log diffs, the Lua profiler
  (`TORIRS_SCRIPTRUN_PROFILE=<file>`).
- **Calibrate content against recorded data with per-entity, per-tick
  timelines**, never medians (Verzik's crab rules came from per-crab timelines;
  aggregates hid every one). Use 150-200 recorded rooms.
- Ladder: 1 probe → 8 seeds against the previous version on the same seeds → 48
  → (relay) 128 → one live seed tick-log match before each commit.
- Never edit Lua while a sweep runs (each seed reads files fresh); freeze a
  script root per sweep; discard runs that straddle a rebuild; one change at a
  time once the owner is reading results, each reported as before → after.
- Run lanes in parallel (10-11 on 16 cores, `nice -n 5`); claim seeds atomically.

### Phase 9. Lane parity: one live seed

```sh
python3 tools/raid_gate/run.py <test> --party <n> --name <seed> --no-publish
```

then diff `build/quest_gate/<run>/ticklog.tsv` against the scriptrun log
(`tlcmp.py` aligned on a fight row, npc slots canonicalised; read the digest
row with `dgcmp.py` when visible rows agree and lanes still part). The parity
bugs found in ToB, in order, are a checklist for any new verb: one fence a tick;
the server tick as the driver's clock; tabs settling in their call; scriptrun
building the gate's world under the gate's names; barrier marks honoured a tick
later; login-tick clocks synced (`::synctimers`); npc queues binding their arming
player; `world_op` honouring the copy the script named.

**Never time the start.** Sync the room: the leader runs `::synctimers` (and the
room's own sync, e.g. `::tobsyncroom`) behind a ready barrier, then a synced
barrier.

### Phase 10. Visual verification

Numbers prove timing; only pictures prove the content LOOKS right, and the
owner found most ToB content bugs by watching (wrong graphics, missing poison,
facing, placement, idle seats).

- Photograph in-client only: `TORIRS_PRESENT_BMP` / `TORIRS_BMP_SERIES` from an
  embed build, or `TORIRS_WORLD_BMP` for an offline world frame. Never capture the
  desktop.
- **Visual verification means crops** of every region at 3-4×, after reading the
  content you are judging. Counting is not looking; a full-size frame shows
  almost nothing. Name the regions cropped; never write "verified" otherwise.
- Pair each picture with a tick-log count (e.g. "160 cabbages, each leaving a
  pool for exactly 16 ticks") and, where a reference video exists, a side-by-side.
- Then watch one live run of the changed fight (`./launch run osrs239-scripts
  -- --unlocked`, Scripts tab) before calling it done.

### Phase 11. Commit

Commit content first (OSRS-Content), then the root with the submodule pointer,
only your own hunks; push both. Record the outcome in the content's status doc
and a memory file. Say plainly when a fix exists only in a worktree.

---

## Part 3. Checklists

**Before the first run of a fight**
- [ ] sources saved; guides read; recorded data located
- [ ] tick-level spec with read ticks, geometry, damage, prayer read, tells
- [ ] content audit table; content fixed; server log free of script aborts
- [ ] every tell perceivable through `api_drive` on both lanes
- [ ] room plan: threats, modes, targets, probes, PASS row
- [ ] solver coded in full; whole chunk `luac -p` clean

**After every run**
- [ ] `grep abort stdout.txt`
- [ ] decision log read for every seat around every damage row
- [ ] monitor counts read; each mismatch named a rule
- [ ] findings written down; one batch of fixes

**Before calling it done**
- [ ] PASS criterion on 48+ seeds
- [ ] one live seed with an identical tick log
- [ ] crops of every mechanic's visuals, compared to a reference
- [ ] committed and pushed, status doc and memory updated

---

## Part 4. Applying it to the Chambers of Xeric (2026-10-13)

What the first probe of the Great Olm showed, before any solver existed:

- **Every CoX npc timer aborts.** The CoX content (written before the ToB parity
  work made npc timers player-less) keeps its state in player varps: Olm's
  `[ai_timer,olm_head]` aborts on its first line every tick, as do the Vanguards,
  Muttadiles, Tekton, Vespula, Ice Demon and Guardians timers. The fight cannot
  run at all. Every room's state must move to instance registers. Olm first.
- **Our Olm has no tells.** No head animations (the facing a player reads), no
  projectiles, no special-attack graphics, no claw signs; the specials fire
  unconditionally (the head-turn skip that 4:1 rests on is missing); lightning
  and the teleport are undodgeable; Olm never changes walls between phases.
  Near-Reality's `greatolm/` implements all of these with the cache ids, so it is
  the baseline to port, refined against the wiki, Synq's guide and the CA list.
- The Olm 4:1 maps cleanly onto the plan's terms: Olm acts every 4 ticks and
  reads the zones at the end of the tick before; "be outside the facing zone at
  basic 2's read tick" and "be in another zone at the special's read tick" are
  hits in the threat field priced at the attack's damage; the claw is a target
  whose swing value is its damage. See `docs/minigames/cox/olm_solver.md`.
