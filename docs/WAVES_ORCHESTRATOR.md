# Waves minigame orchestrator: the Inferno, then the Fortis Colosseum

Written for the **waves orchestrator**: a separate orchestrator (Claude), in its own
worktree and on its own branch, that takes the two wave minigames to "almost identical to
the real game": every mechanic tick-measured and sourced, every animation, graphic, sound,
loc, interface and reward the cache ships accounted for, and every wave played for real by
the test driver with pictures a reviewer opened. Order of priority: **1. the Inferno,
2. the Fortis Colosseum.** The TzHaar Fight Cave is out of scope unless the owner adds it.

It is the third loop of its kind. The quest orchestrator (`docs/QUEST_ORCHESTRATOR.md`)
and the raid orchestrator (`docs/RAID_ORCHESTRATOR.md`, `docs/minigames/raid_loop/`) came
first; this document is the raid loop's method with its lessons already applied
(section 10). You do not do quest work or raid work, and they do not do yours.

Read, in this order, before the first launch:

1. `docs/RAID_ORCHESTRATOR.md` sections 2 (confidence grades), 3 (the spec pass, video
   method), 6 (a played test and its tick ledger) and 8 (standing rules).
2. `docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md` section 1: an npc acting on tick
   T sees the world as it stood at the end of tick T-1. Every prayer flick and every
   "step out one tick early" in a wave minigame is that one fact.
3. `docs/minigames/raid_loop/README.md`, `DRIVER_NOTES.md` (the driver's raid verbs and
   every trap the authors hit), `test/raids/README.md` (the three kinds of row, the ledger
   row contract, the cheat policy, the lessons), `MERGE_CHECKLIST.md`.
4. `docs/minigames/theater_of_blood/AV_INVENTORY.md`: the worked example of the asset
   inventory you will build first for each minigame.
5. `CLAUDE.md` (assert rules, never mutate a shared tree, no switch in a protothread).

## 1. Where the two minigames stand (2026-10-03)

| | The Inferno | Fortis Colosseum |
|---|---|---|
| Content | `minigame_inferno/`: 11 scripts, about 3,800 lines, all 69 waves; ported from Kronos (`Inferno.java`, `TzKalZuk`) with wiki figures; `::inferno <wave>` enters at a wave | **none**: no `minigame_colosseum/`. The cache ships 48 `colosseum_*` npc records |
| Spec | none (constants carry no provenance tags) | none; no plan document |
| Existing docs | `docs/INFERNO_SOUNDS.md` (the sound table was reconstructed, not decoded: read it) | none |
| Selftests | 144 mentions in `torirs_server_world_selftest.c`; debugprocs `::zuk`, `::zuktest`, `::zuklos`, `::zukhp`, `::infernopause` | none |
| Recorder | **Blert records it**: `ChallengeType.INFERNO`, `GET /api/v1/challenges?type=5` returned a run on 2026-10-03 | **Blert records it**: `ChallengeType.COLOSSEUM`, `type=4` returned a run on 2026-10-03 |
| Played by the driver | no | no |

So the Inferno starts at the raid loop's "spec pass" stage with content to correct, and
the Colosseum starts one stage earlier: a plan and a first implementation have to exist
before anything can be measured (section 8). Both have a recorder, which neither Chambers
of Xeric nor Tombs of Amascut has: grade B is reachable for cadences and spawns.

## 2. Where you work

- **Your own worktree and branch, never the owner's main checkout and never the raid
  worktree.** The driver's raid verbs, the tick log and the raid tooling exist only on
  the raid branch until it merges, so cut from it:
  `git worktree add -b <host>-waves-b1 build/orchestrator/worktrees/waves origin/matthew-mbp-m4-raid-b1`,
  then the content worktree **with an absolute target path**
  (`git -C <main>/OSRS-Content worktree add -b <host>-waves-b1 <abs>/worktrees/waves/OSRS-Content origin/matthew-mbp-m4-raid-b1`;
  a relative path lands inside `OSRS-Content/build/`). Symlink `cache.osrs239` from the
  main checkout, `mkdir build`, and prove the worktree with
  `python3 tools/quest_gate/run.py cooks_assistant --no-publish` before anything else.
- **Nothing of the quest loop**: no `QUEUE.tsv`, no claims, no `docs/quest_authoring/`,
  no `test/quests/<quest>.lua`, nothing pushed to v3. Your branch reaches v3 by one PR per
  repo (content first, merge commits) when the owner asks, after the raid branch.
- **Shared files are shared with the raid orchestrator** (session `Haiku Raids`):
  `script/plugins/quest_driver/`, `src/plugin/`, `src/torirsserver/` (the tick log, combat),
  `tools/raid_gate/`, `tools/quest_gate/`, `test/quests/_conformance.lua`, the tree-wide
  combat and consumption scripts. Add to them freely where a wave needs something new;
  before changing existing behaviour, message that session with the file and the reason.
  Take the raid branch's new commits between passes, never during one
  (`git merge origin/matthew-mbp-m4-raid-b1`, both repos, content first).
- **Your files**: `test/waves/` (tests; run through wrappers like `tools/raid_gate/suite.py`
  that set `TORIRS_QUEST_TESTS_DIR`), `tools/waves_gate/` (your copies of the three cards
  with the worktree path and the nouns changed, and the prefix map of
  `tools/raid_gate/raid_coverage.py` extended to `inferno` and `colosseum`),
  `docs/minigames/inferno/`, `docs/minigames/colosseum/`, `docs/minigames/waves_loop/`
  (triage files, ledgers, content bugs, driver notes additions, merge checklist).
- **One pass at a time in the worktree.** A seam pass, a spec pass and a test pass all
  rebuild one script pack. Only a corpus-only pass (documents, no build) may run beside
  another.

## 3. The standard

**Mechanics.** Every number a wave depends on carries a confidence grade and survived the
three-way check of `RAID_ORCHESTRATOR.md` section 2: spec constant, source, our server's
own tick log. A = Jagex or the cache; B = Blert's distribution with ours inside it; C = two
independent non-recorder sources; D = one source; E = a disclosed approximation with an
`[Mn]` tag and an open row.

**Presentation.** The same bar applies to what the player sees and hears, from the first
spec pass, not as an afterthought: every attack, special, spawn, death and transition
names its sequence, graphic, projectile and sound, sourced from the cache's own bindings
(a sequence's frame sounds, a graphic's or loc's `anim=`, an npc record's animations), a
plugin constant, the wiki, or a video frame seen with `tools/raid_gate/frame_count.py`.
An asset whose purpose no source gives is `unknown_purpose` and is left alone. Never
invent a visual; never change a mechanic to fit a presentation fix.

**Rewards and the surrounding systems.** Entry requirements and fees, the lobby npcs and
their dialogue, the wave counter and HUD, pause and logout rules, death and what is kept,
the reward roll and its tables, the pet roll, the completion broadcast, combat
achievements, music and its unlocks: each is a spec row with a source and a test that
drives it by click.

**Played for real, with pictures.** Section 6.

## 4. The passes, in order

Use the raid loop's three cards as your base (`tools/raid_gate/workflows/`): `raid_seam`
(one Opus fixer per seam, Opus closer), `raid_spec` (a corpus phase, one Sonnet worker
per encounter, Opus closer), `raid_author` (Sonnet author and reviewer per test, Opus
sampler). Sonnet 5.5 (`claude-sonnet-5-5`) authors and works; Opus fixes, closes and
samples; the orchestrator writes triage, cards and protocol, and never does worker work
except landing a pass a closer proved and could not commit.

1. **Asset inventory** (one Opus agent per minigame, read-only, before any spec):
   every sequence, graphic, sound, loc, npc record, varbit/varp, music track and
   interface the cache ships for the minigame against where our scripts use it, with the
   source of each asset's purpose. `docs/minigames/<game>/AV_INVENTORY.tsv` + `.md`. For
   the Inferno read `docs/INFERNO_SOUNDS.md` first: its sounds are reused assets with no
   lineage, so the table is a reconstruction and each row says whether a source states it.
2. **Corpus** (the spec card with `corpus_only: true`; it may run beside the inventory):
   section 9's sources, pinned with dated ledgers in `docs/minigames/<game>/SOURCES.md`.
3. **Spec pass**: one table per encounter unit (section 7 and 8 list them),
   `docs/minigames/<game>/encounters/<unit>.tsv` with the nine columns
   `tools/raid_gate/spec_check.py` defines, a `<unit>.scope.tsv` sidecar from the start
   (which rows a solo run can measure, which are statistical), and presentation and
   reward rows beside the mechanic rows. **The spec workers fight**: a measurement scratch
   attacks, eats, prays and steps, because faults hide until a player acts (the raid loop
   found retaliation, damage types and held hits only after the first fought tests).
4. **Driver seam** for what waves need that raids did not (section 5).
5. **Content seams**: one per script file, from the rows where our server disagrees with
   a grade A-C source and from the inventory's gaps. Found is not fixed: every finding is
   a row in `docs/minigames/waves_loop/CONTENT_BUGS.md` until its commit is named there.
6. **Wave tests** (section 6), re-authored after each seam pass until the sampler keeps
   them, then the **full-run test** that plays wave 1 to the end and takes the reward.
7. **Contact sheet** per test pass: publish the kept tests' screenshots as an artifact for
   the owner, as the quest loop does for every batch.

Expect five to seven seam and test cycles per minigame. That is the loop working.

## 5. The driver seam (before any wave test)

The raid branch already gives the driver `t.prayer.set/read/points`, `t.npc.state`
(animation, graphic, facing, size) and `t.npc.await_anim/await_face`,
`t.world.hazard_at/spotanims/projectiles`, `t.tick`, the server tick log
(`t.ticklog.start/rows/gaps/mark/slot`; kinds for npc animation, graphic, projectile,
hits, spawn, death, retype, loc, obj, tiles, and sound/music/jingle once the raid loop's
presentation seam lands), `t.player.step_tick`, a fast attack press (`ticks <= 2`), and
`t.raid.enter/state/leave`. Waves add:

| Row | What it adds | Why |
|---|---|---|
| `wave.enter / wave.state` | enter the minigame at a wave the way the content's own debugproc builds it (`::inferno <wave>`; the Colosseum's equivalent), settle, and read the wave number, the alive count, the pillars or modifiers, paused or not | one leg per wave group; a failed wave reruns from that wave |
| prayer flick | switch a protection prayer on a named server tick and read back the tick it took effect; two switches inside one tick; the cost in prayer points of a one-tick flick | the Inferno is prayer switching on the attack's tick; a flick that costs a tick is a death |
| line of sight | `t.world.los(from, to)` as the server computes it, and which npcs can see the player this tick | safespotting behind a pillar, the Zuk shield, the Colosseum's pillars are all line-of-sight facts |
| target and pack reads | every npc in the arena with its type, attack style, target and the tick of its next attack if the content exposes it; the fast press on a chosen slot among a pack | twelve npcs attack at once; the test chooses whom to kill and what to pray |
| supplies | drink by dose, the reward chest or shop between waves, the Colosseum's modifier choice interface read and clicked by symbol | wave tests live or die on supplies; choices are player actions |
| pause and logout | the minigame's own pause (log out between waves and resume) driven by click | a real mechanic, and how a long run is split |

Prove each on an ordinary npc or a first wave, never on the final boss; a conformance row
each. Engine findings are rows.

## 6. A wave test: fought for real, measured, and seen

A test is `test/waves/<game>_<unit>.lua`, a quest-driver test with a tick ledger. Three
kinds of row, all required, exactly as `test/raids/README.md` states them:

1. **The fight, driven.** Every kill is the player's own attacks; every mechanic is met by
   a real move: the prayer switched on the tick, the tile stepped, the pillar stood
   behind, the supply drunk. Setup takes bring-alongs only (`::setlevel`, `::give`, a
   spellbook). Inside the run the only cheats are read-only state readouts the content
   provides. Never a god mode, a kill cheat, a heal, a wave skip, a teleport past a
   phase, or a debugproc that performs a mechanic. `wave.enter` is the bring-along that
   places the player at the unit's first wave; the full-run test uses it only for wave 1.
2. **The technique rows.** Each published technique is a row proved from the tick log:
   the prayer was up on the tick the hit was rolled; no hit landed while the pillar stood
   between; the blob's style was read from its scan tick; the shield was followed.
3. **The tick ledger.** A `spec.scope` row first, then one `spec.<mechanic_id>` row per
   in-scope table row: the measured value computed from log rows named in the detail,
   the whole distribution, a bracket as a bracket, the expect the comparison itself.
   `raid_coverage.py` must read FULL.

**Visual verification.** Every interaction row carries a screenshot. The reviewer opens
every failing shot, the entry, each prayer switch, each technique row, each death
animation, the reward, and a spread sample of the rest, and says how many it opened. The
sampler (Opus) opens the same for every kept test, reads the tick log beside the ledger,
re-derives three spec numbers from the sources, and sends back any row that restates the
content instead of measuring it. Presentation rows are asserted from the log (the
sequence, graphic, projectile and sound rows on the right tick) AND seen in a picture.
Never screencapture the desktop: shots come from the client's own shot writer.

Budget and determinism: `max_frames` per test from a measured run under the ceiling;
loop on state, never on a fixed tick count; `TORIRS_EMBED_CLOCK_MS=20` locks the clock.

## 7. The Inferno: units and what to measure

Units (one spec table and one test each; the corpus decides the final cut): the waves by
the monster they introduce (nibblers and the pillars; the bat; the blob and its splits;
the melee; the ranger; the mager and its resurrection; the mixed late waves), the single
Jad wave, the triple Jad wave, TzKal-Zuk, and the surrounding systems (entry and the
sacrificed cape, the wave table 1-69, pause and logout, death, the reward and the pet,
the practice mode the content already has a fee for).

For each, the rows the corpus must settle (name them from the sources; do not take the
current constants as the spec, they came from a private-server port):

- **Spawn logic**: which monsters each wave carries, the spawn positions and the rule
  that assigns them, the order they act in. Blert's Inferno recordings carry spawn
  positions per wave: this is a grade B table, not a rotation counter.
- **Each monster**: hitpoints, levels and defence from the cache; attack style and the
  rule that picks it; attack cadence and the first attack after spawning; the tick the
  prayer is read on (the blob reads it a tick early: the T-1 rule); projectile flights;
  max hits prayed and unprayed; special behaviour (nibblers target the pillars, the
  bat's run drain, the blob's three splits, the melee's dig, the mager's resurrection
  rule and what it may revive); movement and line of sight around the pillars.
- **Pillars**: hitpoints, what damages them, the collapse and its damage and radius.
- **Jad** (and the three): the attack tells and the ticks from tell to hit per style, the
  healers' spawn threshold, count, behaviour and healing, the triple wave's offsets.
- **Zuk**: the shield's path, speed and hitpoints; his attack cadence and what blocks it;
  the timed sets (which monsters, when, where), the Jad at its threshold and the
  healers at theirs, the enrage; the ancestral glyph's interactions.
- **Presentation**: the entrance cutscene and its camera, each monster's spawn, attack
  and death sequence, graphics and projectiles, the sounds (`INFERNO_SOUNDS.md`), the
  wave-complete message, the music tracks.
- **Rewards**: the cape, the tokkul for a failed run by wave, the pet roll and the cape
  sacrifice rule, the broadcast, the combat achievements.

Techniques to prove as rows: prayer switching by attack order, the pillar safespots and
the stack, the blob flick, corner trapping, Jad prayer on the tell, healer tagging, the
Zuk shield walk and set handling.

## 8. The Fortis Colosseum: build, then measure

There is no content. Before the loop's passes:

1. **Plan** (`docs/minigames/colosseum/COLOSSEUM_PLAN.md`, one Opus agent after the
   inventory and the corpus exist): what the cache ships (npcs, locs, interfaces, vars,
   the arena's map square), the raid-wide systems, each wave unit, the open measurement
   table, and a build order. `THEATRE_OF_BLOOD_PLAN.md` is the shape.
2. **Build seams**: one seam per script file of the plan's build order, each sourced row
   by row from the spec tables written first for that unit. Nothing is built from memory.

Units: the twelve waves and their fixed and random monsters (the corpus gives the
table), the reinforcements that arrive during a wave, each monster kind as its own table
(the Fremennik trio, the javelin thrower, the jaguar warrior and its mager, the serpent
shaman, the manticore and its orb order, the shockwave colossus, the minotaur), the
modifier system (the choice between waves, each modifier and its levels and stacking),
Sol Heredit (every attack, its tell, its tick to hit and its safe tiles; the phase
thresholds; the pools and the shrinking arena; the enrage), and the surrounding systems
(entry, Minimus and his dialogue, the reward pool that grows per wave and the cash-out
choice, glory, the quiver, the pet, death and what is lost, music, combat achievements).

Blert is the first source for wave contents, spawn tiles, attack cadences and Sol
Heredit's attack sequence; the cache for hitpoints, levels and animation lengths; the
open simulators for geometry and line of sight; guides and frame counts for the tells.

## 9. Sources (the ranking is `COMMUNITY_SOURCES.md`'s)

A first-party Jagex statement > the cache > code a competitive player relies on > a
dataset built from recordings > a written guide > a video > a forum post. Sweep the
wiki's `Update:` newsposts first (they are namespace 112: search with `srnamespace=112`).

- **Newsposts**: the Inferno's release and every balance change; the Colosseum's release,
  its beta feedback changes and hotfixes. A sentence that states a number is grade A.
- **Cache dumps** under `docs/minigames/<game>/sources/` (follow `tools/toa_cache_dump.py`):
  npc records, sequences with frame durations and frame sounds, graphics, projectiles,
  locs, vars, interfaces, enums (the Colosseum's modifier and reward tables may be enums
  or dbtables: look).
- **Blert**: `https://blert.io/api/v1/challenges?type=5` (Inferno) and `type=4`
  (Colosseum) and each challenge's event stream; extend `tools/verify_tob_timings.py`'s
  approach (or write its sibling) so recordings and our own `ticklog.tsv` are measured by
  one tool. Clone `blert-io/blert` and `blert-io/plugin` for the event schema, the npc
  attack tables and the spawn index. Throttle to one request per three seconds; it is a
  volunteer service. Separate what Blert observes from what it asserts
  (`TOB_RESEARCH.md` "Provenance audit").
- **Plugin code** (RuneLite plugin hub and OpenOSRS): the Inferno plugins (wave and
  prayer helpers, the Zuk shield and set timers), the Colosseum plugins (wave spawns,
  line of sight, Sol Heredit attack timers). Quote the constant and the file.
- **Open simulators**, built from observed data and used by players to practise: the
  Inferno trainer and the Colosseum simulator (find their repositories; cite commit and
  file). Good for geometry, line of sight and spawn rules; cross-check every number.
- **Reference servers** already in the tree's history (Kronos for the Inferno): ids and
  shapes only, never balance.
- **The pinned wiki**: each monster, the wave tables, the Strategies pages, the reward
  pages, the combat achievements, with `tools/toa_fetch_wiki.py` (it never overwrites a
  pinned page).
- **Strategy guides**: the wiki's, community guides, and video guides as transcripts
  (`yt-dlp` auto-subs, `tools/raid_gate/vtt_to_md.py`), two players per unit at least.
  A guide is a legitimate source for a technique and the mechanic it rests on.
- **Videos, frame-counted**: `tools/raid_gate/frame_count.py` (yt-dlp over HLS, ffmpeg);
  anchor on the animation's first frame, ten instances from two players, the
  distribution not a number. A frame count alone is grade D.

## 10. What the raid loop learned, so you do not pay for it twice

1. **Inventory first.** The raid loop measured mechanics for a day before learning that a
   treasure vault did not exist and thirty sounds were never played.
2. **Fight in the spec pass.** A boss measured from a safe tile had a flat cadence; the
   first melee hit showed a stray retaliation swing. Three tree-wide combat faults hid
   until a test fought.
3. **A spec row is a measurement.** Rows that copy the constant, report only the first
   instance, or write the spec's figure for a bracket are sent back by the sampler.
4. **Scope from the start.** A solo run cannot measure party rows or another mode's rows;
   sidecars say which rows a test is held to. Statistical rows are settled by the
   recorder tool, not by one run.
5. **Re-read the source before fixing the server.** Eight raid rows were misreadings (an
   odds ratio taken as a rate, an animation length taken as server ticks, a client HUD
   reading compared with a server value). Restate the row with its source line.
6. **Mode and scaled records come from the cache.** Spawn the record the cache has for
   the variant; do not scale one record by hand.
7. **The authors' own play fails first.** Prayer and food run out, attacks stop after an
   eat, a death is read as a kill, a footprint is guessed. `test/raids/README.md`'s
   lessons are yours to hand the authors on the first launch.
8. **Generated files are changed in their generator** (`tools/wiki_droptable.py`,
   `tools/gen_npc_combat.py`); a seam that hand-edits one is reverted.
9. **Search discipline.** Never a recursive grep or find over the whole content tree
   (about 238,000 files; it has crashed a session): scope to one directory, `--include`,
   `head`; match whole symbols and numeric ids separately.
10. **A fixer once wrote into the owner's main checkout** by using the short path. Every
    card's first rule is the absolute worktree path, and a fixer checks `pwd` and the
    branch before its first edit.
11. **New var ids collide with v3's** by merge time; names carry the id. Keep a merge
    checklist from the first allocation.
12. **A tree-wide fix can move quest tests.** The full quest suite is the closer's gate;
    when a faithful fix reddens a quest test, the owner decides and the quest
    orchestrator (session `Haiku Quests`) is told with the first failing row and cause.
13. **Never stop a running pass; never run two building passes in one worktree; never
    use `resumeFromRunId`** (the state directory is the resume).
14. **Eating no longer holds incoming hits** on the raid branch (LostCity's behaviour):
    tests must survive honestly.

## 11. Done

A unit is done when its test is kept by the sampler with coverage FULL, every row on its
kill path is grade C or better or listed open with what would close it, its inventory
rows are used or sourced as unknown, and its pictures were opened. A minigame is done
when every unit is, the full-run test plays from the entrance to the reward by click,
the plan's open table holds only rows that name the measurement that would close them,
and the owner has the contact sheet. Then the next minigame.

## 12. Standing rules

Settle every disagreement from the source line and quote it. Found is not fixed: write
the row. Never loosen a provenance tag, never delete an `[Mn]` with its guess, never
promote a grade on a video alone. Mutate only in a throwaway worktree. Commit by explicit
path, submodule first, the attribution trailer your session names; never stash, reset,
clean, amend or `add -A`. Throttle every external fetch and date it in `SOURCES.md`.
Report honestly: what was verified, what failed, what was skipped.
