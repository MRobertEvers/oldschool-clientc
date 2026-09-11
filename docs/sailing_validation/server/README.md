# Sailing — server selftest evidence

Three sections, oldest first. **The last one is current**: the final pair is
`src/torirs 5e34b81f…` / `src/build_opt/torirsserver 6a0e7f04…` against pack
`902a6b8d…`, rebuilt at 09:35 on 2026-09-07. This first section was written
against the 2026-09-06 21:38 pair `c7e62cce` / `a53899eb`.

Everything below was produced by the **shared** binaries root built from the
final sources, except where a run is explicitly labelled *private build* — the
private build exists only because fixing the full-suite failure changed source,
and it is named and kept separate so no shared-binary claim rests on it.

## Provenance

| Artifact | SHA256 |
|---|---|
| `src/torirs` (embedded client/server) | `c7e62cce180aaeb6bc4cd044818b0209ca99edb478f5280c3ebd10d6b79b1d8d` |
| `src/build_opt/torirsserver` (**shared** server) | `a53899eb8ce5d086e36df3653aaccfd2ee93c05f8e767ece05ecc40a94b9c3db` |
| `OSRS-Content/.../server/scripts/build/script.dat` (30,076 scripts) | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` |
| `src/build_server_opt/torirsserver` (**private** build, fix verification only) | `309c9b1eecc64977578b62eef22c00aecec862d02b68d1bad84f1b56e35e56c9` |

All runs: `TORIRSSERVER_REV=osrs239`, `TORIRSSERVER_CACHE=cache.osrs239`.
The three shared hashes were verified against the values root published before
this phase started; they matched exactly.

Elapsed times are **not** recorded here as performance evidence: several
workers were driving clients and builds on this machine concurrently for the
whole of this phase.

## The runs

| # | Binary | Suite | Pack | Result | Evidence |
|---|---|---|---|---|---|
| 1 | shared `build_opt` | sailing-only | canonical | **561 checks, 0 failures** | `sailing-selftest.log`, `sailing-selftest.tsv` (this directory) |
| 2 | shared `build_opt` | full | canonical | 250,069 checks, **1 failure** | `full-selftest-shared-binary.log` (here); TSV `/tmp/sailing-final-full.tsv` |
| 3 | private `build_server_opt` | sailing-only | canonical | **563 checks, 0 failures** | `/tmp/sailing-final-priv-only.log`, `.tsv` |
| 4 | private `build_server_opt` | full | canonical | **250,071 checks, 0 failures** | `full-selftest-fixed-private.log` (here); TSV `/tmp/sailing-final-priv-full.tsv` |
| 5 | private `build_server_opt` | full | isolated `/tmp/sailing-final-packroot/build` | **250,071 checks, 0 failures** | `full-selftest-fixed-private-isolated-pack.log` (here); TSV `/tmp/sailing-final-priv-full-isolated3.tsv` |

Run 1 is the number the phase asked for and it is met exactly: **561 checks,
0 failures**. Runs 3 and 4 are 563/250,071 because the fix below adds two new
assertions to the lifecycle fixture.

The TSV's last line is a `summary` row, not a check; the counts above exclude it.

## The full-suite failure, diagnosed and fixed at the root

Run 2 reproduced the known failure, and only that one:

```
FAIL free evacuates both captain and guest to walkable shore
     (... abs(guest->x - 3059) <= 12 && abs(guest->z - 2979) <= 12 ...
      at torirsserver/test/sailing_lifecycle_selftest.u.h:192)
```

**It was a fixture defect, not an evacuation defect.** A diagnostic build
printed the state on both sides of `ToriRSServer_WorldMapInstanceFree`:

```
sailing-only : SAILDBG shore player 3059,2979,0  guest 3058,2978,0
               SAILDBG post-free player 3058,2978,0 blocked0 sail0; guest 3058,2978,0 blocked0 sail0
full suite   : SAILDBG shore player 3059,2979,0  guest 3222,3218,0
               SAILDBG post-free player 3058,2978,0 blocked0 sail0; guest 3222,3218,0 blocked0 sail0
```

The guest evacuated correctly — to a walkable, non-sailable tile, holding no
reference to the freed instance. It simply evacuated to **3222,3218**, which is
`g_home_x/g_home_z`, Lumbridge, the tile `ToriRSServer_WorldPlayerInit` puts
every fresh slot on.

The chain:

1. The fixture wrote `guest->sailing.shore_x/z = 3059,2979` and then boarded.
2. `ToriRSServer_VesselBoardPlayer` (`torirs_server_vessel.c:1017-1026`)
   records *where you boarded from* over that field, whenever the player's
   current tile is a legitimate shore — unblocked, not on an instance, not
   sailable. That is the field's whole purpose and it is correct.
3. Whether the fresh guest's home tile passes that test is answered by
   `ToriRSServer_SceneWalkBlocked` against **whichever scene window is bound**
   — a fresh slot has no window of its own, so the read lands on the root
   window, whose centre and contents are whatever the previous section left
   there. That is prior state, and the two runs disagreed about it: in the
   sailing-only run the home tile did not qualify and the fixture's assignment
   survived by accident; in the full suite it did qualify and boarding
   legitimately recorded 3222,3218. (The evidence for the disagreement is the
   two `SAILDBG shore` lines above; this worker did not instrument the window
   binding itself, and does not claim to know which earlier section moved it.)

So the assertion was reading a fixture that had never stated where the guest
was standing. Nothing blocks the Pandemonium shore and the landing search is
not wrong: 3059,2979 is itself **sailable** (`SAILDBG anchor 3059,2979
blocked0 sail1`), and the ring search correctly steps to 3058,2978. The captain
kept 3059,2979 in both runs for a reason that makes the same rule visible from
the other side: it re-boards at login from 3059,2979, and because that tile IS
sailable the boarding guard declines to record it as a shore. Only the guest,
standing on dry land at the home tile, ever had its shore rewritten.

**Fix** (`src/torirsserver/test/sailing_lifecycle_selftest.u.h`): stand the
guest on the real Pandemonium shore before it boards, instead of writing the
field the engine is about to overwrite, and assert both halves:

```
PASS  the guest waits on the real Pandemonium shore, got 3058,2978,0
PASS  guest boards retained deck
PASS  boarding records the shore the guest actually left, got 3058,2978,0
PASS  free evacuates both captain and guest to walkable shore
```

Those four rows are identical in the sailing-only and the full run (runs 3 and
4), which is the point: the fixture no longer depends on what ran before it.
The assertion was not loosened — it gained two rows and still demands both
players land within 12 tiles of 3059,2979, walkable and not sailable.

**No engine source changed.** `torirs_server_vessel.c` and
`torirs_server_vessel_lifecycle.u.h` are untouched by this worker.

## Isolated packs for the full suite

`src/torirsserver/torirs_server_world_selftest.c` hard-coded
`"OSRS-Content/osrs239-content/server/scripts/build"` 118 times and its `../`
sibling form 115 times — 233 literals across ~118 load sites. Only the canoe
section read `TORIRSSERVER_SCRIPTS`.
A worker validating its own `sscompile` output could therefore run the
*sailing-only* suite against an isolated pack but never the *full* one — the
full suite silently loaded the checkout's pack instead.

Two helpers now answer for every one of those sites:

```c
static const char* selftest_scripts_dir(void);          /* TORIRSSERVER_SCRIPTS, else repo-root path */
static const char* selftest_scripts_dir_from_src(void); /* TORIRSSERVER_SCRIPTS, else ../ sibling path */
```

The `../` form answers with the **same** selected directory rather than falling
through to the canonical pack: a run that named a pack and could not load it
must fail, not quietly test the checkout's pack under that name.

Proof is in the logs, not in a passing exit code — the run's own load lines
name the directory:

- run 4 (no `TORIRSSERVER_SCRIPTS`): every `scripts loaded from ...` line says
  `OSRS-Content/osrs239-content/server/scripts/build`.
- run 5 (`TORIRSSERVER_SCRIPTS=/tmp/sailing-final-packroot/build`): all **106**
  load lines say `/tmp/sailing-final-packroot/build`, and **zero** say the
  canonical path. Same 220 sections, same 250,071 checks, 0 failures as run 4.
- run 2, the shared binary without the change, logs 106 loads and every one of
  them is the canonical path.

The isolated pack is a byte copy of the shared one
(`/tmp/sailing-final-packroot/build/script.dat` =
`902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59`), so run 5
isolates the *path*, not the content: it proves the variable is now obeyed, and
deliberately does not change what is being tested.

### Put an isolated pack in a `.../build` directory, not loose in `/tmp`

The first attempt used `TORIRSSERVER_SCRIPTS=/tmp/sailing-final-pack` and died
34 minutes in, at section 190 of 220, with

```
torirsserver: STALE SCRIPT PACK — the tree is newer than script.dat
torirsserver:   /tmp/plugin-engine-removal-wt/tools/runescript-lsp/test/fixture/configs/test.varp
torirsserver: refusing to run on a stale script pack.
```

That is not a sailing bug and not a bug in this change. `scripts_newer_than_pack`
(`src/torirsserver/torirs_server_scripts.c:229`) documents its own assumption —
"`dir` is `<tree>/build`; the sources are its parent" — and walks that parent
for anything newer than `script.dat`. A pack sitting directly in `/tmp` makes
that parent **`/tmp` itself**, so the freshness scan walked every other
session's worktree there and tripped on a file a concurrent worker touched
mid-run. It `exit(1)`s, which is why the aborted log has 249,739 checks, zero
failures and no `summary` row.

The fix is the directory shape, not `TORIRSSERVER_ALLOW_STALE_SCRIPTS=1`: give
the isolated pack a private root and put it in `<root>/build`. Run 5 above did
exactly that and needed no escape hatch. Aborted attempt kept as a named
negative control: `/tmp/sailing-final-priv-full-isolated.log`.

## Coverage the handoff demanded

Every row below is a literal assertion message from
`sailing-selftest.tsv` (run 1, shared binary, 561/0).

| Row | Representative assertions |
|---|---|
| **Raw staging** | `root visibility moves three tiles while high-resolution staging retains a two-tile RUN`; `a standing passenger gets no fake PLAYER_INFO walk, respawn or placement as the hull moves`; `and against the own deck, the raw pool tile`; `shore observer decodes the disembark placement without stale deck offsets` |
| **Standing riders** | `a standing rider's OWN tile never moves — the deck tile is the whole of their position, and 0 tick(s) moved it`; `standing passenger stays on its deck tile while root visibility travels with the hull`; `it is OBSERVED somewhere other than it stands, 3073,3156 rather than 6404,71` |
| **Capacity** | `live hull 1..15 reserves a visible view and an independent deck window` + `hull N owns unique view/window resources` (15 pairs); `fifteen real vessel instances coexist within the sixteen-view client bound`; `player activity instance 1..8 remains available alongside fifteen boats`; `sixteenth hull is rejected cleanly before acquiring any deck resource`; `refused spawn leaks neither a hull nor a private map instance`; `the packet publishes all fifteen actual views, with no invisible hull`; `all boat and activity resources are returned after capacity testing` |
| **Social policy** | `owner starts with helm/cargo access while an ungranted passenger has neither`; `captain grants the selected guest navigation and default navigator-only cargo`; `another passenger cannot promote themselves to navigator`; `invalid navigator masks are rejected without changing the existing grant`; `revoking navigation immediately releases a guest's held helm`; `leaving the hull removes navigation rights`; `boarding again cannot revive an old navigator grant`; `even a stale saved grant cannot authorize a different login in the same pid`; `All players cargo privacy admits the ordinary passenger`; `No players cargo privacy excludes the navigator but retains the owner` |
| **Production logout** | `production logout frees both session and owned vessel`; `logout destroys its vessel identity`; `logout releases the deck reservation`; `logout clears the actual navigator lease`; `no helm identity survives logout`; `logout's fallback is the real recorded shore, got 3058,2978,0`; `guest logout retires its job lease without deleting the captain's boat`; `guest logout preserves the loaded facility resources`; `production logout/login keeps the ocean location` |
| **NPC / deck publication** | `real movable deck NPC spawns for shore and same-deck observer checks`; `NPC walks one deck tile while its hull translates: 6401,67 -> 6402,67; hullZ 404480 -> 404416`; `moving deck NPC produces an actual NPC_INFO for observer 0` / `observer 2`; `actual client decodes the complete NPC movement packet`; `observer 0/2 decodes the NPC's step plus hull motion at 3071,3153`; `passenger receives facilities on a different vessel's published view`; `later facility animations reach shore and another hull's passenger`; `quiet published decks do not replay their state or expired animations`; `the shore player's zonemap cannot offer a deck npc — it subscribes to no pool zone` |
| **Stale queues** | `stale callback fixture has real content and a replacement hull identity`; `stale [queue,sailing_salvage_tick]` / `[queue,sailing_trawling_tick]` / `[queue,sailing_cannon_tick]` `cannot stop replacement work, reschedule or award XP`; `stale [queue,sailing_sort_tick] cannot sort the replacement hull's salvage`; `stale arrival cannot open a replacement hull's customization interface`; each paired with its own positive control — `current-identity [queue,sailing_salvage_tick] / [queue,sailing_trawling_tick] / [queue,sailing_cannon_tick] executes its real operator stop control`, `current-identity sort consumes the actual salvage and awards Sailing XP`, `current arrival reaches the actual native customization interface` — so no refusal is a callback that always no-ops. `sailing_stale_queues_selftest.u.h` is registered and called from the lifecycle section. |
| **Chat filter** | `chat filter modes survive logout/login, got 2/1/1`; `a save with no [chat] section keeps the all-ON defaults, got 0/0/0`; `[chat] private survives a trade-only change, got 1`; `this cache packs varbits into varp 1054 (chat_filter_clan)`; `content declares varp 1054 transmit=yes so the varbit reaches the client (server/scripts/**/chat_filter.varp)`; `the social login dump publishes the saved private mode into varbit 13674, got 1` |
| **Board-friend** | `Board-friend needs a captain session` / `a guest session` / `a third session for its control`; `the guest runs the production Board-friend proc`; `Board-friend parks on p_namedialog for the captain's name`; `the control name reply resumes the parked proc`; `Board-friend runs to completion`; `the guest boards the captain's hull found by p_findvisibleplayer`; `naming a passenger who owns no boat boards nobody` |
| **Passenger role** | `captain/navigator/passenger publish native roles 10/6/3, got 10/6/3`; `a captain (role 10) resolves the crew shell to a real npc, got 15256`; `a navigator (role 6) ... got 15257`; `a passenger (role 3) ... got 15257`; `and the old published role 2 is exactly the hidden rung that left a passenger's deck empty`; `the crew npc shell (15255) switches on this very varbit in the cache: 12 rungs on varbit 19233` |

## Reproducing

```sh
# the number this phase is graded on (shared binary)
TORIRSSERVER_SELFTEST_SAILING_ONLY=1 TORIRSSERVER_REV=osrs239 \
TORIRSSERVER_CACHE=cache.osrs239 \
TORIRSSERVER_SELFTEST_EVIDENCE=docs/sailing_validation/server/sailing-selftest.tsv \
./src/build_opt/torirsserver --selftest \
  > docs/sailing_validation/server/sailing-selftest.log 2>&1

# the full suite, optionally against an isolated pack.
# NOTE the `/build` on the end: the freshness guard scans the pack dir's PARENT
# as if it were the script tree, so a pack loose in /tmp makes it scan all of
# /tmp and abort on another session's file.
mkdir -p /tmp/mypack/build && cp <pack>/script.{dat,idx} /tmp/mypack/build/
TORIRSSERVER_REV=osrs239 TORIRSSERVER_CACHE=cache.osrs239 \
TORIRSSERVER_SCRIPTS=/tmp/mypack/build \
TORIRSSERVER_SELFTEST_EVIDENCE=/tmp/full.tsv \
./src/build_opt/torirsserver --selftest > /tmp/full.log 2>&1
```

## What this evidence does NOT cover

- Nothing here is a **visual** check. This directory is protocol and lifecycle
  state only; the rendered proof lives in `../multiplayer/`, `../client/` and
  the lifecycle images, and those are other workers' rows.
- Runs 3-5 use a private binary. The shared `build_opt/torirsserver` still
  contained the pre-fix fixture at the time, so a full-suite run against the
  **shared** binary still reported that one failure. **Discharged:** run 7
  (below) is 250,071/0 on the shared `214b43d7`.
- The `no free npc slot ... 294 roster spawn(s) declined` warnings in both logs
  are the pre-existing 4096-NPC pool ceiling against two dense window centres.
  They are unrelated to sailing and are present identically in run 1 and run 2.

---

# Final-binary re-stamp — 2026-09-07, worker `restamp`

The section above was written against the *previous* shared server
`a53899eb…`, which still carried the pre-fix lifecycle fixture. Root has since
rebuilt the shared binaries from the final sources. **Both suites were re-run on
the shared server, and the caveat above about the full suite is now discharged.**

## Provenance (verified on disk before the first run)

| Artifact | SHA256 |
|---|---|
| `src/torirs` (embedded client/server) | `4854e3036abf814d07ce97649845d30206bac647b49adce560e3e9b28fa927a4` |
| `src/build_opt/torirsserver` (**shared** server) | `214b43d7a30dd47b67b47693e1b9820d299c2154eeb2193aeb2c4ec0b7082186` |
| `OSRS-Content/.../server/scripts/build/script.dat` (30,076 scripts) | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` |

All three matched the values root published at 08:08 exactly. No private build
was used and no source was edited for either run.

## The runs

| # | Binary | Suite | Pack | Result | Evidence |
|---|---|---|---|---|---|
| 6 | shared `build_opt` `214b43d7` | sailing-only | canonical | **563 checks, 0 failures** | `sailing-selftest-final.log`, `sailing-selftest-final.tsv` |
| 7 | shared `build_opt` `214b43d7` | full | canonical | **250,071 checks, 0 failures** | `full-selftest-final.log`; TSV `/tmp/sailing-fin-full.tsv` |

Exact commands, both from the repository root:

```sh
TORIRSSERVER_SELFTEST_SAILING_ONLY=1 TORIRSSERVER_REV=osrs239 \
TORIRSSERVER_CACHE=cache.osrs239 \
TORIRSSERVER_SELFTEST_EVIDENCE=docs/sailing_validation/server/sailing-selftest-final.tsv \
./src/build_opt/torirsserver --selftest \
  > docs/sailing_validation/server/sailing-selftest-final.log 2>&1     # exit 0

TORIRSSERVER_REV=osrs239 TORIRSSERVER_CACHE=cache.osrs239 \
TORIRSSERVER_SELFTEST_EVIDENCE=/tmp/sailing-fin-full.tsv \
./src/build_opt/torirsserver --selftest > /tmp/sailing-fin-full.log 2>&1   # exit 0
# then copied to full-selftest-final.log
```

Both TSVs end in a `summary` row that is not a check:
`summary  sailing  563  0` and `summary  full  250071  0`. Neither log contains a
single `FAIL` line (`grep -c '^FAIL' full-selftest-final.log` → `0`), and the full
log's last verdict line is `ToriRSServer selftest: all checks passed`.

## What changed against runs 1–5

- Run 6 reports **563**, not run 1's 561. The two extra assertions are the ones
  the lifecycle-fixture fix added (the guest now stands on the real Pandemonium
  shore before boarding); they are the same +2 that private runs 3 and 4 showed.
- Run 7 reports **0 failures** on the *shared* binary. Run 2's single failure —
  `free evacuates both captain and guest to walkable shore` — is gone, because
  the fixture fix that runs 4 and 5 proved privately is now in the shared server.
  Runs 4/5 and run 7 agree on the count exactly: **250,071**.
- The `no free npc slot … 294 roster spawn(s) declined` warnings are still
  present in both logs, identically. They remain the pre-existing 4096-NPC pool
  ceiling against two dense window centres, and are unrelated to sailing.

Elapsed times are not recorded as performance evidence: other workers were
driving clients on this machine during this phase.

---

# Final pair `6a0e7f04` — 2026-09-07 09:36

Root rebuilt both shared binaries at **09:35** from the current tree, after the
LIFE-1 reconnect guard and the SRV-1 field initialiser landed (plus the 09:40
review's asserts and test controls). Both sections above belong to earlier
pairs: the first to `a53899eb`, the re-stamp to `214b43d7`.

## Provenance (verified on disk before the run)

| Artifact | SHA256 |
|---|---|
| `src/torirs` (embedded client/server) | `5e34b81f9d2a45eae7306e8ddf93de5c590a0b3cd475d726a7745eb0deffd03b` |
| `src/build_opt/torirsserver` (**shared** server) | `6a0e7f0441c25f1249bc8342a5103744a4dbd991d721b67eaea1846db7d910d4` |
| `OSRS-Content/.../server/scripts/build/script.dat` (30,076 scripts) | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` |

Hashes: `/tmp/sailing-fin3/hashes.txt`. Build logs
`/tmp/sailing-fin3/server-build.log` and `client-build.log`, both `exit=0`
(`/tmp/sailing-fin3/status.txt`). The pack was **not** recompiled.

**The server hash is byte-identical to the intermediate 09:34 build.** That is
expected and is not a sign the rebuild did nothing: the last batch of edits
added only `assert()` calls, and the `OPT=1` server compiles them out. The
server-side source difference against `214b43d7` is real and is exactly three
things — SRV-1's `out->unbounded = 0`, the cached `TORIRSSERVER_EXT_DEBUG`
`getenv`, and those asserts.

## The runs

| # | Binary | Suite | Pack | Result | Evidence |
|---|---|---|---|---|---|
| 8 | shared `build_opt` `6a0e7f04` | sailing-only | canonical | **563 checks, 0 failures** | `/tmp/sailing-fin3/sailing-selftest.log`, `.tsv`; `status.txt` line `sailing-suite exit=0` |

Same 563 as runs 6 and 3/4, so the fixes changed no assertion count and broke
nothing.

## The full suite was NOT re-run on `6a0e7f04`

Run 7 measured **250,071 checks, 0 failures** on `214b43d7` earlier the same day
(`full-selftest-final.log`). It was not repeated on the rebuilt server. The only
server-side source differences between those two binaries are the three listed
above: a one-line field initialiser on a path `test-world` covers, a `getenv`
cached in a static, and asserts an `OPT=1` build removes. Carrying 250,071/0
forward across that difference is a judgement, and it is written here as one so
nobody reads it as a fresh run.

## LIFE-1 and SRV-1 on this server

- **LIFE-1 closed.** Five consecutive aboard `raw logout` cycles in session
  `/tmp/sailing-fin3-life`: five `torirsserver: reconnect user='fin3life'
  session=ok` lines in `client.log`, **zero** `login: rejected`, **zero**
  `connection lost`. The guard is `ToriRSServer_WorldMarkVarp` returning early
  while `player->session->reconnect` is set
  (`src/torirsserver/torirs_server_world.c:11260`).
- **SRV-1 closed in source.** `ToriRSServer_SceneOpNearestOpts`
  (`torirs_server_scene.c:2435`) writes `out->unbounded = 0` at line 2446, so
  `ToriRSServer_SceneRouteOp` (line 2595) and the NPC router
  (`torirs_server_world.c:3993`) no longer pass indeterminate memory into
  `collision_map_route_tiles`. **No dedicated gate exercises either caller** —
  `test-world` covers `collision_nearest_opts_from_model` only. The fix is
  verified by source inspection plus this 563/0 suite and `test-world` exit 0,
  not by a test that would fail if it were reverted.
