# Quest inventory for client-driven test planning

Scope: `server/scripts/quests/` in
`/Users/matthewevers/Documents/git_repos/3draster-quest-driver/OSRS-Content/osrs239-content`.
`ls server/scripts/quests` shows **182 entries**, but three of those are not
quests: `configs/` (shared `questcheat.constant`/`questpoints.varp`/
`questscroll.constant`), `scripts/` (shared helper procs, see below) and
`lc_quests.txt` (a reference list, see below). **The actual inventory is 179
`quest_*` directories**, all 179 of which are covered, row-for-row, in
`quest_inventory.tsv` in this folder.

Note: **`quest_recipefordisaster/` is one physical directory holding 10
separate dbrow quests** (`subquest_rfd_intro/amikvarze/dwarf/evildave/
goblins/lumbridgeguide/monkey/ogre/pirate` + `subquest_rfd_finale`, i.e. the
whole "Recipe for Disaster" series). A test plan built "one test per quest
dir" needs ~10 tests for that one row, not one.

## Method

Everything below was produced by grep/regex/Python over the tree (scripts in
this scratchpad: `quest_inventory.py`, `quest_scan.py`, `boss_scan.py`,
`reset_scan.py`, `merge.py`), then spot-checked by reading the actual `.rs2`
files for ~25 quests across the spectrum (fully-audited modern quests,
untouched legacy quests, miniquests, the RFD umbrella, quests with dual/
bitfield/literal progress state). Two real bugs were caught and fixed during
spot-checking (both are why the extraction scripts look the way they do):

1. A loose "any `quest_*`/`miniquest_*`/`subquest_*` token anywhere in the
   directory" regex mis-attributed `quest_arena` (Fight Arena) to "Secrets of
   the North" because an unrelated comment in `khazard_barman.rs2` mentions
   `quest_secretsofthenorth` in passing. Fixed by trusting only explicit
   `~quest_complete_rewards(<dbrow>, ...)` call sites for the quest↔dbrow
   correlation.
2. A non-greedy `(.*?)` regex meant to grab the one comment line above each
   `quest_cheat.rs2` arm instead swallowed the entire ~90-line file preamble
   for the *first* arm in the file (`miniquest_bearyoursoul`), because
   `re.S` let `.` cross newlines and the lazy match ran until the first
   `if ($row = ...)` in the whole file. Fixed by requiring the comment to be
   a single line (`[^\n]*`).

Where a value could not be determined confidently the TSV says `?` rather
than guessing (currently: 3 quests for `varp`, `elemental_workshop` +
`ikov` + `recipefordisaster` for `const_not_started`/`const_complete` — all
explained in each row's `notes`).

## Column notes (`quest_inventory.tsv`)

- **`varp`/`const_not_started`/`const_complete`**: primarily sourced from
  `server/scripts/quests/scripts/quest_cheat.rs2`'s `~quest_cheat_complete`
  proc, which has one hand-written arm per quest (`%<varp> >= ^<complete>`)
  for 166 of 179 dirs (it also documents that 140/166 of those constants
  agree value-for-value with the cache's own `quest.endstate` column). The
  remaining `not_started` constant is read from each quest's own
  `configs/*.constant` file. A handful of quests have no single linear progress
  var: `quest_ikov` (Temple of Ikov) has two alternate endings
  (`^ikov_completed_armadyl` / `^ikov_completed_lucien`), `quest_atailoftwocats`
  compares against a bare literal `70` (no named constant — the `.constant`
  file is an intentionally-empty stub, see its own comment), and
  `quest_elemental_workshop` (Elemental Workshop I) tracks state via
  `testbit()` on a bitfield varp, not a staged int.
- **`has_run_selftest`/`run_name`**: a `[debugproc,<abbr>run]` exists for 110
  of 179 quests. Per its own doc comment in `arena_selftest.rs2` and others,
  this is the server-only "drive the whole quest through its varp ladder and
  assert state/rewards at each step" gate the audit loop
  (`docs/QUEST_AUDIT_PROMPT.md`) requires before a quest can be marked
  `audited-clean`. It runs **inside `--selftest`**, not through the client.
- **`bmp_count`/`bmp_numbered`/`journal_bmp_count`/`leftover_count`**: 93 of
  179 quests have at least one `[debugproc,<abbr>bmp_...]`. 6,807 such
  debugprocs exist tree-wide. 72 quests follow the numbered
  `<abbr>bmp_NN_<scene>` scheme (one incrementing counter across the whole
  playthrough, e.g. `aflbmp_01_qualify_fail_cots` … `aflbmp_60_complete_scroll`);
  21 use an unnumbered `<abbr>bmp_<scene>` scheme with no ordinal (e.g.
  `cookbmp_talk`, `coa_bmp_mastaba_entry`) — these read as an older/simpler
  convention that predates the numbered one (several quests, e.g.
  `quest_anothersliceofham`, carry **both**: a full numbered `ashbmp_NN_*`
  set plus a leftover unnumbered `slicebmp_*` set from an earlier pass that
  was never deleted). 750 `<abbr>bmp_journal_NN_<state>` entries exist
  (screenshots of the quest journal text at a given progress value — see
  `aflbmp_journal_00_not_started` … `_12_complete`) and 452
  `<abbr>bmp_leftover_<thing>` entries exist tree-wide (78 quests have at
  least one). A "leftover" is **not a screenshot** — it's a documented,
  named admission that one specific piece of the real quest was *not*
  built (a puzzle, a cutscene, an NPC fight, a UI). Reading three real
  ones: `aflbmp_leftover_cat_toy_wind_animation` /
  `aflbmp_leftover_jerboa_catch_rolls` /
  `aflbmp_leftover_guild_stairs_multilocs` (At First Light: the toy-mouse
  winding animation, the jerboa hunter minigame's random-roll capture
  mechanic, and the multi-loc ladder pair in the Hunters' Guild are all
  skipped/simplified); `akdbmp_leftover_xamphur_fight` /
  `akdbmp_leftover_yama_fight` (A Kingdom Divided: **both required boss
  fights are simply not implemented** — the automated walk skips past
  them); `dt2bmp_leftover_vardorvis_path`/`_sucellus_path`/
  `_whisperer_path`/`_leviathan_path`/`_golem_fight` (Desert Treasure II:
  **all four boss-key dungeons and the golem fight are unimplemented** —
  the quest only proves the narrative frame, not the four superboss
  encounters that are DT2's actual content). This is a load-bearing
  finding for test planning: **a quest's `leftover_*` list is the
  authoritative "what a client test cannot actually drive yet" checklist**,
  more so than reading the quest's own `.rs2` files cold.
- **`has_reset`/`reset_name`**: yes for 91/179 quests. Two conventions:
  a bare `[debugproc,<questdir-suffix>]` (e.g. `::cook`, `::atfirstlight`,
  `::curseofarrav` — the common case, confirmed by finding
  `p_teleport(...)` + a `%<varp> = 0`/`^..._not_started` write in the body)
  and a handful of `<suffix>reset` names (`::biohazardreset`). Per
  `quest_cheat.rs2`'s own header comment, this tree has "~170
  `[debugproc,<quest>]` setup cheats … that put a player at a quest's START
  with its prerequisites met" — so the true count of start-teleport cheats
  is likely higher than the 91 my heuristics matched with confidence; the
  remaining ones use a name that doesn't match either convention I checked
  for (marked `?`, not guessed).
- **`boss_fight`/`boss_npcs`/`post_boss_stage`**: this engine drives combat
  through the normal attack system (no scripted damage) for the overwhelming
  majority of quest fights — the tell is an `[ai_queue3,<npc>]` block (the
  npc-death trigger) that writes the quest varp or `queue()`s the completion
  proc. 305 such blocks exist across 80 quest dirs; 65 of those dirs have a
  block that clearly gates quest *progress* (not just a loot roll) and are
  marked `boss_fight=yes`. Concrete example — Dragon Slayer I, Elvarg:
  `server/scripts/quests/quest_dragon/scripts/elvarg.rs2:22-28`
  (`[ai_queue3,elvarg]` → `if (%dragonquest < ^dragon_complete) { queue(dragon_complete, 0, 0); }`,
  duplicated for the multi-npc alive form at line 30). Fight Arena's four
  gladiator kills are each their own `[ai_queue3,...]` block:
  `arena_encounter.rs2:116` (`arena_ogre` → `@arena_defeat_ogre` →
  `%arenaquest = ^arena_defeated_ogre`), `:144` (`arena_scorpion`), `:166`
  (`arena_bouncer`), plus `general_khazard_arena` for the finale. Only two
  quests script actual damage output themselves rather than relying on the
  engine's ordinary combat: `quest_mm/scripts/mm_demon.rs2:89`
  (`npc_damage(hitsplat_damage, $gnome_hit)`, Monkey Madness I's gnome-in-
  demon-suit fight) and `quest_soulsbane/scripts/soulsbane_confu.rs2:117`.
  Conversely, several of the most recently-audited quests show
  `boss_fight=no` **specifically because their boss encounters are the
  `leftover_*` items above** — A Kingdom Divided, Desert Treasure II, Song
  of the Elves, The Curse of Arrav, Sins of the Father, Temple of the Eye,
  and Twilight's Promise all have zero implemented `[ai_queue3,...]`
  combat-completion hooks even though their wiki pages have real boss
  fights; their photo-lane walk instead has a `..._skip`/`..._idle`
  debugproc at that point (e.g. `dt2bmp_47_vardorvis_skip`,
  `dt2bmp_49_whisperer_skip`). A client-driven test for these can currently
  only prove the frame around the fight, not the fight.
- **`cutscene_or_instance`**: yes for 98/179 quests (regex over `map_clock`,
  `cutscene`, `instance`, `p_telejump`, `cam_moveto`/`cam_lock`/`cam_reset`).
  This is a coarse signal (a single `cam_lock` used once for a dialogue
  close-up counts the same as a multi-stage scripted cutscene) — treat it as
  "worth reading before assuming a plain click-driven test suffices," not as
  a fight-vs-no-fight-grade boolean.
- **`notes`**: flags legacy/unnumbered bmp sets living alongside numbered
  ones, TODO/UNIMPLEMENTED/stub marker counts, empty quest directories, and
  (for 4 quests: `quest_gobdip`, `quest_theslugmenace`, `quest_doric`,
  `quest_imp`) a drift where `server/scripts/selftest/<dir>/*.bmp` fixture
  images are checked in but no `bmp_` debugproc currently exists in that
  quest's scripts to reproduce them — those are stale screenshots from an
  earlier pass of the content, not evidence about the current build.

## Summary counts (of 179 quest dirs)

| Signal | Count |
|---|---|
| Has a `<abbr>run` selftest debugproc | 110 |
| Has ≥1 `<abbr>bmp_...` screenshot debugproc | 93 (0 debugprocs: 86) |
| ...following the numbered `bmp_NN_` scheme | 72 |
| ...following the unnumbered `bmp_<scene>` scheme only | 21 |
| Has both run + bmp | 72 |
| Has run but no bmp | 38 |
| Has bmp but no run | 21 |
| Has neither run nor bmp (untouched by the audit loop) | 48 |
| Has a reset/start-teleport debugproc (best-effort) | 91 |
| Has a real implemented boss/required-combat completion hook | 65 |
| Shows a cutscene/instance/camera-scripted signal | 98 |
| Has ≥1 `leftover_*` (disclosed unimplemented piece) | 78 |
| Total `bmp_*` debugprocs tree-wide | 6,807 |
| Total `bmp_journal_*` entries tree-wide | 750 |
| Total `bmp_leftover_*` entries tree-wide | 452 |

Reading this as a test-planning signal: the ~61 quests with **run + reset +
≥1 bmp** are the ones already proven drivable end-to-end server-side and
already have named, screenshot-worthy checkpoints picked out by a human —
they're the cheapest ports to a client-driven test. The 48 with neither a
`run` nor a `bmp` debugproc have had no audit pass at all; a client test for
one of those is greenfield work (confirm the varp/journal wiring is even
real) before it is a scripting task.

## `lc_quests.txt`

`server/scripts/quests/lc_quests.txt` (131 lines, no header, one
`quest_<dir>` name per line) is a **lookup/reference table, not executable
content and not a manifest read by the compiler**. It lists which quest
directories originate from LostCity's own reference `.rs2` source tree (the
2004Scape-era open-source server this project ports content shape from) —
i.e. "does LostCity already have a script for this quest I can port the
*shape* of, or does this have to be authored fresh from the OSRS wiki."
It's grepped constantly (but never executed) throughout
`docs/QUESTHELPER_CONTENT_PORT_QUEUE.md` as a first-checked source-ladder
step ("grep-first per methodology: `lc_quests.txt` clean, no `X` hits
anywhere in `server/scripts`" → author from the wiki with no LC scaffold to
crib from). It has no bearing on which quests are complete, tested, or
even present in this tree today — several dirs it lists have since been
fully rewritten past whatever LC originally offered, and several dirs in
`server/scripts/quests/` (the newer OSRS-only quests, e.g. `quest_atfirstlight`,
`quest_kingdomdivided`, `quest_curseofarrav`) correctly have **no** entry in
it at all, because LostCity (frozen ~2004-05) predates them.

## `server/scripts/quests/scripts/` — shared quest helpers

Three files, all proc libraries every individual quest's `.rs2` calls into
rather than reimplementing:

- **`quest_cheat.rs2`** — `[debugproc,complete]` (`::complete <dbrow>`),
  the "skip to the end" cheat. `~quest_cheat_complete(dbrow)` is one big
  `if ($row = <dbrow>) { ...; return(^questcheat_set); }` chain, one arm per
  quest (167 arms currently), each writing that quest's own progress
  varp straight to its own `^..._complete` constant (or, for the 4 quests
  whose completion goes through a setter proc rather than a plain varp
  write — Clock Tower, The Dig Site, Plague City, Vampyre Slayer — calling
  that proc instead). It deliberately does **not** hand out XP/items/the
  completion scroll (those stay behind the quest's own dialogue/reward path)
  — it only fixes up state, quest points and the completed-count so every
  `%qp`/`%<varp>` gate elsewhere in the tree answers correctly. This is
  effectively source-of-truth documentation of every quest's human name,
  varp, and complete constant, and is the primary source for those three
  TSV columns.
- **`questpoints.rs2`** — `~quest_complete_rewards(dbrow, "reward|lines",
  namedobj icon)` is the one proc every quest's own completion script calls
  at the finish line. It looks up questpoints/displayname from the cache's
  `quest` dbtable, awards points (`~quest_award_points`), bumps
  `%quests_completed_count`, and paints+queues the reward scroll
  (`~quest_scroll_paint` → `questscroll.rs2`). It also owns
  `~quest_complete_jingle`, which plays one of three shared "Quest Complete"
  MIDI jingles keyed on the cache's `quest:difficulty` column, with five
  hand-coded exceptions (Recruitment Drive/Regicide play nothing, Sins of
  the Father/Mountain Daughter/Monkey Madness II each have their own).
- **`questscroll.rs2`** — interface 153, the post-completion reward scroll
  (name/points/reward lines/rotating reward-item model). Its own header
  comment explains a real engine gotcha worth knowing before writing a
  client test around quest completion: the scroll **cannot** be mounted
  synchronously from the completion script (the player is always parked on
  a `~chatnpc`/`~mesbox` at that instant, so the engine's own
  close-dialogue-on-script-finish logic would suppress the mount) — it's
  deferred through `queue(quest_scroll_show, 0, $row)`, which only fires
  once the player's current dialogue closes. A client test asserting "the
  scroll is up" right after the completion click needs to `resume`/close
  the pending dialogue and tick at least once first, or it's asserting
  against a race.

## Where the bmp-debugproc convention is documented, and how it runs

There is **no single canonical spec doc** for the exact
`<abbr>bmp_NN_<scene>` naming scheme — it's documented per-quest, at the top
of each quest's own `*bmp*.rs2` file (e.g.
`server/scripts/quests/quest_atfirstlight/scripts/*bmp*.rs2`'s header:
*"At First Light — Gate D named-BMP setup. Each debugproc parks on ONE
authored mesbox / chathead / p_choice / journal / complete scroll so
`TORIRS_EXIT_BMP` is not a washed tele with only 'godmode on'."*) and in a
matching `server/scripts/selftest/<quest_dir>/INTERACTIONS.txt` manifest
(column 1 = the `.bmp` filename stem, column 2 = the
`TORIRS_NET_CHEAT` debugproc that produces it), plus disclosure of that
quest's own `Allowed leftovers` list. The house *rules* for the convention
— one named BMP per player-facing interaction, never a "highlight reel,"
the player must be unkillable unless the step is a death test, BMPs live in
the `OSRS-Content` submodule and must be committed+pushed before the parent
gitlink moves — are in `docs/QUEST_PORTING_FIELD_GUIDE.md` §1 ("Named Gate
D BMPs..." block) in the 3draster repo, and the audit loop that produces
them end-to-end is `docs/QUEST_AUDIT_PROMPT.md`.

Mechanically (`src/main.c`, `src/app/app_tick.c`): the **native client**
(not the `--selftest` binary, which has no plugin/render host) is booted
with `TORIRS_PLUGINS=0` (overlays are never content evidence) and three
env vars: `TORIRS_NET_CHEAT="god 1;<quest>bmp_<scene>"` (queues `::god 1`
then the target debugproc as chat-line cheats the instant the client
connects), `TORIRS_EXIT_BMP=/absolute/path.bmp` (dump the final rendered
frame to disk on exit — distinct from `TORIRS_BMP_SERIES`, which instead
writes a numbered sequence of frames for animation capture), and a manifest
naming the compiled script pack. The debugproc itself resets relevant quest
state, sets up inventory/NPC/loc context, and opens exactly one dialogue/
interface, then the harness lets a couple of ticks pass and exits, dumping
that one frame. There is **no single checked-in runner script** that drives
this end-to-end for a whole quest today (unlike the canoe/sailing pilots,
which do have `tools/content_selftest.py` / `tools/sailing_harness.py`) —
each quest's BMP set is currently produced by an agent hand-running this
recipe once per named scene during its audit pass, and the resulting
`.bmp` files are committed under `server/scripts/selftest/quest_<name>/`
alongside that quest's `INTERACTIONS.txt`. `docs/CONTENT_SELFTEST.md`
describes the *sibling*, more mature pattern this should probably converge
toward (a persistent client+embedded server on one controlled clock, driven
over a shared mailbox with `state`/`step N`/`cheat TEXT`/`varbit NAME`/
`widget COMPONENT SUB`/`shot /path.png` commands, pixel-region comparison
against reviewed fixtures) — but that doc is written for the canoe pilot
specifically and explicitly says extending it to quests is future work
("Add a scenario function that seeds an isolated account... No quest is
marked validated simply because the canoe pilot passes").

## Quest journal — how it's opened, and how a client could read "complete"

`server/scripts/interface_questjournal/scripts/quest_journal.rs2` is the
one dispatcher for all ~190 quests' journals. Flow:

1. **Opening it**: `[if_button2,questlist:list]` (op2, "Read journal:" on a
   quest-list row) → `~quest_journal_open_by_id(last_slot)` where
   `last_slot` is the quest's cache `quest:id`. That proc `db_find`s the
   dbrow and runs a 190-arm `if ($row = <dbrow>) { ~<abbr>_journal; return; }`
   chain (same shape as `quest_cheat.rs2`'s dispatch) into each quest's own
   journal proc. A quest with no arm falls through to
   `~quest_journal_unwritten`, which paints a generic "This world does not
   run this quest yet" page — itself a useful machine-readable signal for
   "this quest dir is not wired into the journal at all yet."
2. **Per-quest journal proc** (e.g. `~cook_journal`, `~arena_journal`):
   a `switch_int(%<varp>)` over that quest's own progress constants, each
   case building up `$text` (with `<col=...>`/`<str>` tags) and calling the
   shared `~quest_journal($title, $text)` in
   `interface_questjournal/scripts/quest_journal.rs2`, which paints
   `questjournal:title` and up to 50 `qj1..qj50` rows (via `split_init`/
   `split_get`) and mounts interface 119. The `^..._complete` case's text is
   the reliable "done" marker — by convention it starts with a
   `QUEST COMPLETE!` banner (confirmed in the Fight Arena/At First Light
   journal sources read directly).
3. **Reading "complete" from a client-driven test**: three levels of
   evidence, cheapest/least-visual to most:
   - **Server-authoritative**: read `%<varp>` directly (the shared mailbox's
     `varbit NAME` command in `docs/CONTENT_SELFTEST.md`, or the
     `--selftest` binary's own state dump) and compare to `^<abbr>_complete`
     — proves state, nothing about the client rendering it.
   - **Quest list**: the client-side quest-list clientscript (CS2 4024,
     `quest_progress_get`) reads the same varp and renders a completion
     marker/colour on that row without opening anything — cheap to probe
     via a `widget questlist:list` read, but is a CS2-rendered indicator,
     not text.
   - **Visual/textual proof** (what Gate D actually captures): open the
     journal (`button questjournal:list ... op2` or drive the normal click
     path) and either OCR/compare the rendered `questjournal:title`/`qj1`
     text for the `QUEST COMPLETE!` banner, or screenshot it — this is the
     only one of the three that also proves the dispatcher, the per-quest
     journal proc, and the render pipeline all agree, which is exactly the
     property `docs/CONTENT_SELFTEST.md` insists on ("does not treat a
     server varbit or the existence of a PNG as rendering proof" — check
     server and client state *separately*). The reward scroll
     (`questscroll.rs2`, interface 153, "You have completed <name>!") is an
     even more direct but transient signal, live for one interaction right
     after the finishing click (see the mount-timing race noted above).

## Files produced

- `quest_inventory.tsv` — 179 data rows, 18 columns (see task spec), one per
  `quest_*` directory.
- `quest_inventory.md` — this file.
- Intermediate scripts/data (kept for reproducibility, not deliverables):
  `quest_inventory.py`, `quest_scan.py`, `boss_scan.py`, `reset_scan.py`,
  `merge.py`, `cheat_arms.json`, `journal_dispatch.json`,
  `quest_scan_raw.json`, `boss_scan.json`, `reset_scan.json`.
