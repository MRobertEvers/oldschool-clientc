# Raid orchestrator: Theatre of Blood, Chambers of Xeric, Tombs of Amascut

Written for an orchestrator (Claude) that will take the three raids from "implemented
and selftested from tables" to "played for real, tick-measured, and defensible". It is
the raid counterpart of [`QUEST_ORCHESTRATOR.md`](QUEST_ORCHESTRATOR.md): the same
batch/branch/claim loop, the same closers and samplers, with the quest guide replaced
by a measured encounter spec and the guide grader replaced by a tick ledger.

Read, in this order, before the first launch:

1. [`QUEST_ORCHESTRATOR.md`](QUEST_ORCHESTRATOR.md) in full. Claims on `v3`, work on
   `<host>-b<N>` in both repos, content PR first then parent PR, merge commits, never
   wait on a lock, never stop a running pass, protocol work straight to `v3`.
2. [`minigames/theater_of_blood/ENCOUNTER_TIMING.md`](minigames/theater_of_blood/ENCOUNTER_TIMING.md)
   section 1, the attack pipeline. Every raid technique in every guide is one fact seen
   from several sides: an npc acting on tick T sees the world as it stood at the end of
   tick T-1. Nothing below makes sense without it.
3. [`minigames/theater_of_blood/COMMUNITY_SOURCES.md`](minigames/theater_of_blood/COMMUNITY_SOURCES.md)
   and [`TOB_RESEARCH.md`](TOB_RESEARCH.md). The method in this document is the one
   those two already used; they are the worked example.
4. The three plans and their constants files: `THEATRE_OF_BLOOD_PLAN.md` section 16,
   `cox/COX_PLAN.md` section 11, `tombs_of_amascut/TOMBS_OF_AMASCUT_PLAN.md` section 12,
   and the provenance legends at the top of `tob.constant`, `cox.constant`, `toa.constant`
   (`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_<raid>/configs/`).

## 1. Where the three raids stand (2026-10-02)

| | Theatre of Blood | Chambers of Xeric | Tombs of Amascut |
|---|---|---|---|
| Content | 20 scripts, ~17,900 lines, every room | 22 scripts, ~16,700 lines | 25 scripts, ~12,800 lines |
| Spec | `THEATRE_OF_BLOOD_PLAN.md` (2,673 lines), `ENCOUNTER_TIMING.md`, `nylocas_waves.md` | `COX_PLAN.md` (2,340 lines), `COX_MECHANICS.md` | `TOMBS_OF_AMASCUT_PLAN.md`, `ENCOUNTERS.md`, `ASSET_INDEX.md`, `SOURCES.md` |
| Provenance tags on constants | `[cache] [wiki] [blert] [tmt] [qol] [oosrs] [tool] [Mn]` | geometry measured by `tools/cox_template_survey.py`; tick constants cited in the plan's section 11 with Jagex quotes | `[cache] [wiki] [nr] [toa] [Mn]` |
| Open measurements | section 16: 40 rows, most CLOSED against recordings | section 11.5: six items | section 12: M1-M16 |
| Recorded-raid dataset | **Blert** (`https://blert.io/api/v1`, per-tick event logs; ~440 raids used) | **none**: Blert's CoX page says "Coming Soon" and `type=2` returns `[]` (probed 2026-10-02) | **none**: `type=3` returns `[]` (probed 2026-10-02) |
| Our-side measurement | `tools/verify_tob_timings.py` (recordings vs `tob.constant`), the C harness in `src/torirsserver/torirs_server_world_selftest.c` ("Theatre of Blood harness": drives the server a tick at a time and watches the bosses), `::tobrun` (25+ table checks), `::tobhit/::tobmelee/::tobmaze...` | `tools/cox_verify.sh` (34 in-game checks, mutation-tested), `tools/cox_sim.sh` (the raid under god mode, diffed tick by tick against the strategy guide), `tools/cox_check_timers.py` | `::toarun`-style selftest, `toa_resume_selftest.rs2`, `tools/toa_verify_docs.py`, `tools/toa_cache_dump.py` |
| Played by the quest driver | no | no | no |
| Queue rows | only `nightatthetheatre` (tier 4), whose last step is the entry-mode raid | none | none |

Two gaps decide the work order:

- **The quest driver cannot play a raid yet.** `script/plugins/quest_driver/` has attack,
  cast, eat and await-dead, and no prayer verb at all, no read of an npc's current
  animation or attack, no read of a hazard tile, no "step on this tick" primitive, and
  no per-tick event log a test can assert against. Section 4 is that seam, and it comes
  before any room test.
- **The three raids are not in the loop.** No QUEUE rows, no claims, no gate lane.
  Section 5 puts them in.

## 2. What "correct with relative confidence" means here

The owner's standard for quests is "the guide is the spec". For a raid there is no
guide that states ticks, so the spec is the measured encounter table, and confidence is
how many independent ways a number was confirmed. Keep the existing ranking
(COMMUNITY_SOURCES.md): **a first-party Jagex statement > the game's own cache > code a
competitive player relies on > a dataset built from recordings > a written guide > a
video > a forum post.** Sweep the `Update:` newsposts on the wiki (`?action=raw`) before
reading anybody's code: the Nylocas pillar hitpoints were "unknowable" through four
passes while Jagex had published both ends in a patch note.

Every number an encounter depends on gets a **confidence grade**, written into the
encounter's spec table (section 3) and asserted by the tick ledger (section 6):

| Grade | Means | Example |
|---|---|---|
| **A** | Jagex states it, or the cache states it (`attackrate`, `stat4`, a seq's frame durations, a loc's placement), **and** our server measured equal | Maiden's `stat4` per mode; an attack animation's length from `cache_seq.txt` |
| **B** | A recorder's distribution states it (Blert: hundreds of rooms), **and** our server's measurement falls inside that distribution | Maiden: first attack tick 9, then every 10 (582/582 gaps) |
| **C** | Two independent non-recorder sources agree (plugin code + wiki; plugin code + a documented video measurement), **and** our server matches | Xarpus exhumed counts: OpenOSRS plugin and the wiki |
| **D** | One source (a pinned wiki sentence, one plugin constant, or one documented video measurement), **and** our server matches it | Vespula's grounding at 20% (two wiki pages disagree; one chosen, noted) |
| **E** | A disclosed approximation. The constant carries `[Mn]`, the plan's open table has the row, the ledger row says "approximation, Mn" | Xarpus pebble stomp damage (M40) |

An encounter is **defensible** when: every mechanic on its kill path is grade C or
better, every D and E is listed in the plan's open table with what would close it, no
E sits on a number a published technique depends on (attack cadence, phase triggers,
hazard lifetimes, the T-1 scan) unless no source of any kind exists, and the tick
ledger (section 6) proved our server reproduces each graded number. "Agrees" is always
measured, never asserted: the three-way check is **spec constant** vs **source** vs
**our server's own tick log**, and a number that survives all three is one you can
defend. `tob.constant`'s header says it in one line: a number that survives both the
recorder and the harness is a number worth defending.

Never: loosen a tag to make a check pass; delete an `[Mn]` with the guess; promote a
grade on the strength of a video alone; argue from memory where a quote exists.

## 3. The spec pass (the raid's "content parity")

One worker per encounter (a room or a boss phase group), Sonnet 5.5, Opus closer, pass
name `<batch>-spec`. Its product is the encounter's **spec table**,
`docs/minigames/<raid>/encounters/<room>.tsv`, one row per mechanic the tick ledger will
measure:

```
mechanic_id   quantity                        spec_value   unit    tags            grade   source_ref                                  closes
maiden.cad    attack cadence                  10           ticks   [blert][wiki]   B       TOB_RESEARCH.md M1; blert 582/582 gaps      M1
maiden.first  first attack after room start   9            ticks   [blert]         B       TOB_RESEARCH.md M1 (25 of 26 rooms)         M1
maiden.blood  blood splat damage window       11           ticks   [tmt]           C       TobMistakeTracker MAIDEN_BLOOD_GAME_TICK_LENGTH; wiki "~6 s"  M5
```

The worker, per mechanic, in this order:

1. **Newsposts.** Search the wiki's `Update:` pages for the raid and the room; quote
   the sentence.
2. **Cache.** `configs/all.npc` (`attackrate`, `stat4`, sizes), `cache_seq.txt`
   (animation length = frames x durations, in client ticks of 20 ms; 30 cycles per game
   tick), spotanim and projectile records, the map's loc placements. ToB and ToA ship
   these dumps under `docs/minigames/<raid>/sources/`; CoX gets the same via
   `tools/toa_cache_dump.py`'s pattern. An attack animation's length bounds its cadence;
   a projectile's cycle count gives its flight time (`ENCOUNTER_TIMING.md` 1.2).
3. **Recorder, where one exists.** ToB: Blert, through `tools/verify_tob_timings.py`
   (`--fetch N` at 3 s per request; it is a volunteer service and rate-limits bulk
   crawls). Blert also records Colosseum (`type=4`). It does **not** record CoX or ToA;
   re-probe `GET /api/v1/challenges?type=2|3&limit=1` at each spec pass and record the
   date, because the schema already exists on their side.
4. **Plugin code.** `blert-io/plugin`, `QuestingPet/TobMistakeTracker`, `damencs/tob-qol`,
   OpenOSRS `theatre`, duckblade's Tombs of Amascut plugin, and the RuneLite plugin hub's
   CoX and ToA plugins (search the hub for the room name; quote the constant and the
   file). A client-side observer's constant is a thing the real server does, so it is
   worth more than a wiki sentence and less than a recording.
5. **Reference servers.** Zenyte, RSPS-NEAR-REALITY (`[nr]`), Kronos. Reimplementations:
   good for ids and shapes, suspect for balance; every use is cross-checked against
   cache or wiki, as `toa.constant`'s legend says.
6. **The pinned wiki revision** (`?action=raw`, so the citation templates survive; record
   the oldid in `SOURCES.md`).
7. **Videos, by the method in section 3.1.** Last, never alone.

Where a source has the answer, the worker updates the constant and its tag, the plan's
open row (CLOSED, with the figure and the sample), and the spec table. Where none does,
the row is grade E with the approximation disclosed at the constant. A spec pass never
touches a room script's logic; that is a seam.

### 3.1 Measuring a tick from video (CoX and ToA have nothing better)

Videos are the lowest-ranked source, and for most CoX and ToA cadences they are the
only observational one. They are usable when the measurement is done the same way every
time and written down:

- **Anchor on the attack animation's first frame**, never on the hitsplat or the
  projectile landing (those are flight and rounding, `ENCOUNTER_TIMING.md` 1.2). The
  cache's seq record for that attack tells you what the first frame looks like.
- **Count frames, not seconds.** Frame-step at the video's own frame rate; one game tick
  is 0.6 s, so at 60 fps a 4-tick attack is 144 frames, and at 30 fps a one-frame error
  is 1/18 of a tick. Prefer videos with a tick-counter or attack-timer overlay (RuneLite
  "Tick counter", "Attack timer" plugins) and read the overlay too.
- **At least ten instances, from at least two videos by different players**, within one
  party size and mode. Record each as a row in `docs/minigames/<raid>/sources/videos.tsv`:
  `url  timestamp  fps  frames  ticks  mechanic_id  measured_by`.
- **Report the distribution, not a number.** Ten readings of 4 and one of 5 is "4 ticks
  (10 of 11; the 5 is a recorder offset or a lag spike)". Two videos disagreeing by a
  whole tick with no explanation is an **open row**, not an average.
- A video measurement alone is grade D. Agreeing with a plugin constant or a cache
  bound makes it C.

### 3.2 Our side of the three-way check

The spec pass also measures **our** server, so the constant, the source and the server
are compared before any test is written:

- ToB: `tools/verify_tob_timings.py` (recordings), the C harness `tob_harness_*`
  stanzas (`make -C src test-ToriRSServer`), `::tobrun`.
- CoX: `tools/cox_sim.sh` (the raid under god mode, tick by tick), `tools/cox_verify.sh`.
- ToA: the `toa_selftest.rs2` debugprocs and `toa_resume_selftest.rs2`.
- All three: the **tick log** (section 4) once it exists, which is the one instrument
  that also runs inside a real played test.

A disagreement between our server and a grade A-C source is a content bug: a seam row,
with the source quoted.

## 4. The driver seam (before any room is authored)

One seam pass, `<batch>-seam1`, Opus fixers, with these rows. Each verb gets a
conformance row in `test/quests/_conformance.lua` (`SEAM_COUNT`, `@seam-count`,
`verb_list.py --check`, `check-drive-abi`), proved first on an ordinary npc, never on a
boss.

| Row | What it adds | Why a raid cannot be played without it |
|---|---|---|
| `prayer.set / prayer.read` | activate or deactivate a protection prayer (or any prayer) by click; read the active set and the overhead back from the server | every boss room is a prayer switch on a known tick |
| `npc.state` | an npc's current animation id, spotanim, overhead, hitpoints ratio, and the tick it last attacked, from the client's entity pool | a tactic reacts to what the boss is doing, not to a timer |
| `world.hazard_at(x, z)` | the loc, spotanim or ground object on a tile (Xarpus acid, Maiden blood, Sotetseg maze tile, Olm crystals, Zebak waves) | "step off the pool" needs the pool |
| `player.step_tick(x, z)` | a one-tile step issued on this tick, with the tick the server resolved it on in the detail | the T-1 rule: the step must land before the boss's scan |
| `tick.log` / `t.ticklog.rows()` | the server records, per tick, npc attack starts (type, attack id), projectile launches, hits, spawns, deaths, loc changes and the player's tile; the test reads the log and the ledger row carries the measurement | the tick ledger (section 6) is built from this; it is the same event set `verify_tob_timings.py` reads from Blert, so the two compare directly |
| instance resume per room | `*_resume_selftest.rs2` already resumes a raid mid-way; expose it as `::<raid>resume <room>` and prove a `--from-leg` run lands in the room with the boss unspawned | one leg per room; a failed Verzik reruns from Verzik |

Engine findings that come out of this seam are rows too (found is not fixed: write the
row). Two are already known and belong here: the client merges identical ground objs on
one tile (`App_WorldObjStackAdd`), and the server snaps a drained stat back to base on
the next xp gain (`torirs_server_combat.c:1174`) where LostCity's `addXp` does not, so
every content stat drain (Sourhog, Verzik, Olm) is cancelled by one hit.

## 5. Putting the raids in the loop

- **Rows.** Add to `test/quests/QUEUE.tsv` one row per encounter, tier 6 ("raids"), ids
  `tob_maiden tob_bloat tob_nylocas tob_sotetseg tob_xarpus tob_verzik`,
  `cox_tekton ... cox_olm` (one per room class the generator can place, plus Olm's
  phases), `toa_het toa_crondis toa_apmeken toa_scabaras toa_akkha toa_zebak toa_baba
  toa_kephri toa_wardens`, plus one full-raid row per raid (`tob_entry`, `cox_solo`,
  `toa_150`) that chains the rooms. `quest_dir` is the minigame directory; `helper_dir`
  / `helper_file` point at the encounter spec table instead of a Quest Helper class.
- **Claims** work unchanged: `claim.py batch <host>-b<N> tob_maiden tob_bloat ...` on
  `v3`, work on the branch, `claim.py done`, content PR then parent PR.
- **Gate lane.** `gate.py` gets a raid lane (protocol work, on `v3`): instead of
  `helper_coverage.py` it runs `raid_coverage.py`, which reads the encounter spec table
  and requires, for every mechanic row, a ledger row whose measurement matches the spec
  value within the stated tolerance and names the grade. `cutscene_row_required` and the
  completion-scroll rule do not apply; a raid row's "scroll" is the reward chest row
  (`tob_chest.rs2`, `cox_rewards.rs2`, `toa_rewards.rs2`) with the points asserted.
- **Cards.** Copy the three workflow cards as `spec_pass`, `raid_seam`, `raid_author`
  under `test/quests/orchestrator/<host>/workflows/`, change only the prompts' nouns
  (encounter for quest, spec table for guide, tick ledger for helper_coverage), and keep
  the Sonnet-authors / Opus-fixers-closers-samplers split.

## 6. Authoring a room test: "fought for real" plus a tick ledger

A room test is a relay leg (`docs/quest_authoring/relay.md`), one leg per room, resumed
through the instance resume verb. Its rows are of three kinds, and all three are
required:

1. **The fight, driven.** Every point of boss damage comes from the player's own
   attacks; every mechanic is met by a real move built from section 4's verbs: the
   prayer switched, the tile stepped, the pillar hidden behind, the maze walked, the
   orb picked up. Bring-alongs are the same as for quests: `::setlevel`, gear, potions,
   food, `::passive` on nothing that is part of the encounter. `::godmode`, killing the
   boss by script, teleporting past a room or a phase, or a debugproc that performs the
   mechanic are not allowed in a test (they are fine inside `cox_sim.sh`, which measures
   content, not play).
2. **The technique rows.** The published techniques are the mechanics seen from the
   player's side, and each is a row: "5-tick Xarpus" (the step lands one tick before the
   scan: ledger shows the spit resolved against the previous tile), Bloat's walk (the
   player is never adjacent on a stomp tick), Sotetseg's maze (every tile the player
   stood on is a maze tile), Verzik P1 pillar hiding (no P1 auto resolved while the pillar
   stood between). A kill row alone proves nothing about the encounter.
3. **The tick ledger.** From `t.ticklog.rows()` the test asserts each spec-table
   mechanic: `maiden.cad: 10 ticks, 40 of 40 gaps (spec 10, grade B)`;
   `maiden.first: tick 9 (spec 9)`; `xarpus.exhumed.lifetime: 11, 11, 11 ... (spec 11)`.
   A tolerance is stated per row (cadence: exact; a first-attack offset: +-1 for recorder
   alignment, as TOB_RESEARCH.md M16 explains; a damage range: the spec's range). A
   grade E row is asserted as "measured N; approximation, Mn", so a later measurement
   changes a row, not a hunt.

The reviewer checks the three kinds the way it checks guide coverage: no narrated kill,
no mechanic unmet, no spec row unmeasured. The sampler (Opus) reads the shots, the
ledger and the tick log, and **re-derives three spec numbers independently from the
sources** before keeping the test; a number it cannot re-derive is sent back as an open
row, not accepted.

Frame budgets: a raid room can run thousands of ticks. Set `max_frames` per leg from a
measured run, under the 480000 ceiling (`tools/quest_gate/quest_list.py`); if a full raid
needs more, raise the ceiling with the measurement in the commit, as the Regicide change
did, rather than skip a room.

Determinism: `TORIRS_EMBED_CLOCK_MS=20` locks the server clock; rolls are per-entity
streams seeded from spawn tile, so a boss's roll sequence is reproducible per build and
a test must loop on the boss's state, never on a fixed tick count.

## 7. Recommended order, and what done looks like

1. **Theatre of Blood first.** It has the recorder, the harness and 37 of 40
   measurements closed, and `nightatthetheatre`'s last step is the entry-mode raid.
   Batch: seam1 (section 4), spec pass (close the remaining section-16 rows for entry
   mode, fill the six spec tables), author `tob_maiden ... tob_verzik` as a relay, then
   `tob_entry` and the quest.
2. **Tombs of Amascut second.** Cache dumps and the duckblade plugin give most cadences
   grade C; the video method covers Zebak's roar, Kephri's swarms and the Wardens' P3
   rate (M7: cache 5 vs wiki 7 is exactly the kind of conflict a frame count settles).
3. **Chambers of Xeric last.** No recorder, procedural layouts, and the point formula is
   unpublished (section 11.5). `cox_sim.sh` is the measuring instrument; the video
   method is the observational source; Olm's phase clocks and Tekton's anvil cycle are
   the kill-path numbers to get to grade C first.

An encounter is done when its row is green under the raid lane, every spec row is
grade C or better or listed as open, its test file carries no `::godmode`, no
narrated mechanic and no teleport past a phase, and the sampler kept it. A raid is done
when every encounter is, the full-raid row chains them to the reward chest, and the
plan's open table has only rows that name the measurement that would close them.

## 8. Standing rules carried from the quest loop

- Settle every disagreement from the source line, before releasing or reopening: the
  LostCity file for 2004 content, the newspost, the cache dump, the recorder, the plugin
  file and line. Quote it in the queue note.
- Found is not fixed: a defect with no row fails nothing. Write the seam row.
- Mutate only in a throwaway worktree (CLAUDE.md); a mutation check that edits the shared
  tree poisons every concurrent build.
- Never regenerate a generated file over a hand edit; put the edit in the generator's
  audited tables (`tools/gen_spawns.py`, `tools/gen_npc_stats.py`, `tools/gen_tob_nylo_waves.py`).
- Edit this document and the three plans only on fresh `origin/v3`, after reading their
  last three commits; commit protocol work to `v3` from a worktree under
  `build/orchestrator/worktrees/`.
- Throttle every external crawl (Blert at 3 s per request; the wiki likewise) and record
  the date of each fetch in `SOURCES.md`.
