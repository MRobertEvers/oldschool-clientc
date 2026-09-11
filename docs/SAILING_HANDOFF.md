# Sailing — completion record, 2026-09-07

This replaces the 2026-09-06 pause handoff. That document described work in
flight and is superseded in full; nothing in it should be quoted as status.

**The sailing plan is finished, and nothing is owed before it is committed.**
The rebuild the 09:30 audit was owed happened at 09:35; the two rows that were
waiting on it, **LIFE-1** and **SRV-1**, are fixed, in the shared binaries, and
re-proven on them.

- Repository: `/Users/matthewevers/Documents/git_repos/3draster`, branch
  **`gameframe-2004-osrs239`**.
- Requirement ledger: **`docs/sailing_validation/plan-audit.md`** — 47 Verified,
  4 Open, each row naming its evidence file, check name or image.
- Plan with per-phase status: **`docs/SAILING_PLAN.md`**.
- Research, with the launch-control mapping corrected: **`docs/SAILING.md`**.
- Scenario coverage: **`docs/sailing_coverage.csv`** — 85 PASS, 1 UNTESTED
  by design (SAIL-24, hull-vs-hull collision, which the deob does not have
  either; the absence *is* the requirement).

## The binaries every claim rests on

Root rebuilt these at **09:35 on 2026-09-07** from the current tree, after the
LIFE-1 and SRV-1 fixes and the 09:40 review's edits. Hashes:
`/tmp/sailing-fin3/hashes.txt`; build logs `/tmp/sailing-fin3/server-build.log`
and `client-build.log`, both `exit=0` (`/tmp/sailing-fin3/status.txt`).

| Artifact | SHA256 |
|---|---|
| `src/torirs` (embedded client + server) | `5e34b81f9d2a45eae7306e8ddf93de5c590a0b3cd475d726a7745eb0deffd03b` |
| `src/build_opt/torirsserver` | `6a0e7f0441c25f1249bc8342a5103744a4dbd991d721b67eaea1846db7d910d4` |
| `OSRS-Content/.../server/scripts/build/script.dat`, 30,076 scripts | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` (**unchanged**) |

The server hash is byte-identical to the intermediate 09:34 build because the
last batch of edits added only `assert()` calls, which the `OPT=1` server
compiles out. The source difference against the previous server is real and is
exactly three things: SRV-1's `out->unbounded = 0`, a cached
`TORIRSSERVER_EXT_DEBUG` `getenv`, and those asserts.

**The 08:08 pair `4854e303` / `214b43d7`**, same pack, is the immediately
preceding one. Much of the evidence in `docs/sailing_validation/` was produced
on it and is still cited; every such citation now says so. The final pair
differs from it by the nine items in the review outcome below — two behavioural
fixes and seven asserts, comments and test controls. **The 2026-09-06 21:38 pair
`c7e62cce` / `a53899eb`** is one step further back, and differs additionally by
two selftest fixtures and the `collision_map` `unbounded` initialiser. Anything
older — `9c2a58e3`, `092d1e5b`, `54a09b3f`, `8d16856e`, `51e2b023`, `622ebea1`,
or packs `b9bed76d` / `9a8b44bb` / `78a5a8ee` / `a14736bc` — is **historical**
and closes nothing.

Nothing under `src/` is newer than the build.

## What was run on the final pair

- **Twelve unit gates, all exit 0**, serially in `src/build_opt_es`:
  `test-sailing-paint-order`, `test-painters-world-entity`, `test-frame-flat`,
  `test-world`, `test-minimenu-world`, `test-cs2-sailing-settings`,
  `test-sailing-collision`, `test-mock239-playerinfo`, `test-wev-rebuild`,
  `test-wev`, `test-rsprot-bridge`, `test-social`
  (`/tmp/sailing-fin3/status.txt`, one `<target>.log` each).
- **Sailing-only server suite: 563 checks, 0 failures**
  (`/tmp/sailing-fin3/sailing-selftest.log`, `.tsv`).
- **All nine acceptance tools `ok: true`** — facility, crew, extractor,
  multiplayer, social (12/12), collision, cargo, deck, client (9 captures).
  Logs `/tmp/sailing-fin3/accept/*.log`; sessions `/tmp/sailing-fin3-*`; the
  results JSON under `docs/sailing_validation/` were regenerated in place and
  carry `5e34b81f` provenance. Two regenerated captures were opened and matched
  against their prose: `collision-stopped.png` and
  `social/editnav-armed-on-guest.png`.
- **LIFE-1 closed.** Five consecutive aboard `raw logout` cycles in session
  `/tmp/sailing-fin3-life`: five `torirsserver: reconnect user='fin3life'
  session=ok` lines, **zero** `login: rejected`, **zero** `connection lost`
  (`/tmp/sailing-fin3-life/client.log`). `lifecycle-reconnect-fixed.png`
  re-taken on this pair.
- **SRV-1 closed in source**, with `test-world` exit 0 on the final pair.

### Three things that were NOT re-run, said plainly

1. **The full server suite.** It was **250,071 checks, 0 failures** on
   `214b43d7` earlier the same day (`server/full-selftest-final.log`) and was
   not repeated on `6a0e7f04`. The only server-side source differences since are
   SRV-1's one-line initialiser, the cached `getenv` and asserts `OPT=1`
   compiles out. That is a reasoned carry-forward, not a measurement.
2. **Performance.** `harness-performance.json` and `render-final-results.json`
   were measured on `4854e303` and are unchanged in relevance: the final pair
   differs only by the items above, and the one render-path change — the
   paint-order pass's grown-once scratch buffers — *removes* two per-frame
   allocations. **No new number was produced and none is claimed.**
3. **Thirteen of the 25 make targets**, `test-torirsserver-embed` included, and
   the GPU lane. Their state on the final pair is unknown.

### One artefact on disk that is not a result

The first acceptance pass in this session recorded five failures in
`/tmp/sailing-fin3/accept/status.txt`. Those were a shell-scripting mistake —
`zsh` did not word-split a variable — not product failures. The real results are
`status2.txt` and the per-tool logs. The failing file was left on disk rather
than deleted.

## The 09:40 review: what was fixed, what was left

Nine changes went in between the 08:08 pair and the final one. Two are
behavioural:

1. **LIFE-1** — the reconnect guard in `ToriRSServer_WorldMarkVarp`
   (`torirs_server_world.c:11260`).
2. **SRV-1** — `ToriRSServer_SceneOpNearestOpts` writes `out->unbounded = 0`
   (`torirs_server_scene.c:2446`).

Seven are hardening, and none changes behaviour in a release build:

3. **M-1** — `src/game/sailing_paint_order.u.h` no longer `malloc`s per frame:
   grown-once file-scope scratch buffers, a `_Static_assert` tying
   `WORLDVIEW_MAX` to `PAINTER_MAX_WORLD_VIEWS`, span view bounds asserted.
4. **M-2** — the `TORIRSSERVER_EXT_DEBUG` `getenv` in the player extended-info
   v5 writer cached in a static.
5. **M-4** — the three compound asserts in `content_test_sailing.c` split to one
   condition per assert.
6. **L-1 / L-2** — parameter asserts on the lifecycle static helpers, the
   `mock239` helpers and the `app.c` paint-order callback.
7. **L-4** — `World_ResetSceneAlloc`'s assert carries its reason string again.
8. **L-5** — a doc-comment ordering fix in `src/world/world.h`.
9. **L-6** — `sailing_paint_order_test.c` gained `memcmp` byte-identity controls
   for the flat pass, including `count == 0`.

**Four were deliberately left**, with the reason:

- **M-3 — duplicated shore constants.** Consolidating them is a cross-file
  refactor in a tree three other sessions are editing. The duplication is inert
  and visible; a bad merge is the larger risk.
- **M-6 — `test-torirsserver-embed`'s private-mode assertion.** It sits behind
  the long-standing embed decode break (**EMB-1**) and cannot run. The behaviour
  it wanted is covered instead by lifecycle selftest rows inside the 563/0
  suite. Re-enabling it waits on EMB-1.
- **L-3 — the `!vm` guard pattern.** Pre-existing and used throughout the CS2
  host. Changing it in the sailing hunk alone would make it inconsistent with
  every neighbour without fixing anything.
- **L-7 — `ContentSymbol` zero fallbacks.** The house pattern for that API
  across the content layer. Same argument as L-3.

## What the original requirements asked for, and where each one stands

| Requirement | Status | Pointer |
|---|---|---|
| Fix the buggy implementation using repository, online and video research | **Done** | `SAILING.md` §5 (deob) and §7 (launch controls); the corrections list below |
| A blazing-fast visual harness first — pause, step, checkpoint, restore, in the **actual ocean** | **Done** | `harness-performance.json` (measured on `4854e303`, not re-measured on the final pair); fixture 3072,3160 aboard a wooden skiff |
| An independent boat collision map the whole moving/turning hull respects | **Done** | 563/0 sailing suite on `6a0e7f04` + `test-sailing-collision` exit 0 + `collision-results.json` on `5e34b81f` |
| Boat facilities and the complete native sailing interfaces | **Done** | `activities.md`, nine acceptance tools all `ok: true` on `5e34b81f`, `privacy/results.json` (on `c7e62cce`, with `test-cs2-sailing-settings` green on the final pair) |
| Visual verification, not state assertions | **Done** | every acceptance run opened its images; 90+ PNGs described across the phase |
| C0–C5, S0–S3, integration checkpoints, constraints | **Done** | `SAILING_PLAN.md` status table; `plan-audit.md` per row |

## Evidence index

Everything lives under `docs/sailing_validation/`.

| Area | File |
|---|---|
| **Status and ledger** | `README.md` (top-level status), `plan-audit.md` |
| **Server suites** | `server/README.md` (§"Final pair `6a0e7f04`": **563/0** on the rebuilt server, `/tmp/sailing-fin3/sailing-selftest.log`); `server/sailing-selftest-final.{log,tsv}` (**563/0** on `214b43d7`), `server/full-selftest-final.log` (**250,071/0** on `214b43d7`, not re-run since) |
| **Unit gates** | `unit-gates.md` (§"Final pair `5e34b81f`", **12/12 exit 0**), `client/gates.json` (`final_pair_5e34b81f_2026_09_07`) |
| **Collision** | `collision-results.json`, `collision-ocean-map.json`, `collision-shore-map.json`, `collision-*.png` incl. the named negative control `collision-land-rejected.png` |
| **Cargo and its policy** | `cargo/results.json`, `cargo/native-cargo-roundtrip.png`, `cargo/native-cargo-whitelist.png` |
| **Deck models and animation** | `deck-results.json`, `deck-*.png` |
| **Facilities and activities** | `activities.md`, `activity-results.json`, `activity-*.png`, `utility-*`, `shipyard-*`, `native-panel-*` |
| **Crew** | `crew-actions.json`, `crew-*.png` |
| **Extractor shore grace** | `extractor-grace-results.json`, `extractor-grace-*.png` |
| **Client rendering / overlap / budget** | `client/README.md`, `client/client-results.json`, `client/client-*.png` |
| **Multiplayer** | `multiplayer/README.md`, `multiplayer/results.json`, five reviewed PNGs |
| **Lifecycle: dock, helm, voyage, disembark, relog** | `lifecycle-dock-results.json`, the `lifecycle-*.png` chain |
| **Chat-mode persistence** | `lifecycle-chat-results.json`, `lifecycle-chat-*.png` |
| **The reconnect defect (LIFE-1), now fixed** | `lifecycle-reconnect-results.json` (§`final_pair_verification`), `lifecycle-reconnect-fixed.png` |
| **Social, permissions, boarding by name** | `social/results.json` (12/12), `social/editnav-arming-results.json`, `social/roles-results.json` |
| **Cargo privacy** | `privacy/README.md`, `privacy/results.json`, `privacy/privacy-choice{0,1,2}-*.png` |
| **GPU lane recheck** | `gpu/README.md`, `gpu/gl3-*.png`, `gpu/soft3d-*.png` |
| **Zero-boat render benchmark** | `render-final-results.json`, `render/`, plus the preserved `render-initial-*` and `render-lumbridge-repeat-*` |
| **Harness timing** | `harness-performance.json`, `../sailing_harness.md` |
| **Protocol notes** | `server-protocol.md` |
| **Two engineering write-ups** | `client/world-drain.md` (the root rebuild drain), `client/frame-assert.md` (the prefetch across a view marker) |
| **Retired** | `gpu/historical/` (nine `54a09b3f` captures + its note) |

**Negative controls, which must never be read as success:**
`collision-land-rejected.png`, `lifecycle-login-stale-menu-before.png`,
`lifecycle-aground-control.png` (2026-09-06),
`lifecycle-cheatberth-aground.png` (2026-09-07, the HARN-1 control),
`client/client-budget-zero.png`, `privacy/sailing-privacy-native-before.png` and
`privacy/sailing-privacy-native-bug.png`.

## Research corrections folded back in

Each of these was wrong in an earlier draft and was settled from the shipped
cache, the deob or source — not from a summary.

1. **The draw budget omits, it does not flatten.** Rev-239 `Statics.method2832`
   initialises the count per priority group and calls `method1449` only below
   the cap. Over-budget entities are *skipped*; flattening is a separate,
   still-required overlap gate. Default 30, CS2 7900/7901
   (`Statics.method11128`).
2. **Two different overlap tests.** Boat↔boat is a fine-unit enclosing
   rectangle from quantized actual-yaw corners (`method8755` /
   `class521.method11500`); boat↔actor is the nearest-16 oriented footprint
   (`method5535`). Neither is a tile AABB and neither is a full SAT.
3. **Radius 60 is not always 1×1.** A subtile position can occupy 2×2.
4. **There is no bake.** The merged-model bake was written, became the *only*
   path, and was deleted. Flatten is live per-frame view state — scale 0.01,
   pre-scale Y offset −1200, config `flat_hsl`.
5. **The bob is real and applies to flattened hulls too.** Bone-0 Y translation
   from the config's op-25 idle seq (13424/26/28) into the descent transform;
   the wire's updateFlags-0x1 seq (the sink family 13425/27/29) overrides it
   once and completion restarts the idle at frame 0. Pitch and roll from the
   full 4×4 remain deferred.
6. **`REBUILD_WORLDENTITY_V4` carries neither an id nor a plane** — only
   `p2 baseX, p2 baseZ` plus the zone grid. The view comes from the captured
   `SET_ACTIVE_WORLD` cursor and the plane from that packet's level byte. The
   old "assert on an unknown id" was right for the registry lookup and wrong for
   wire data: internal lookups assert, wire-driven bad states are guards that
   diagnose and drop.
7. **The drain rule covers the root rebuild too**, and did not: see
   `client/world-drain.md`.
8. **The emit prefetch must not read across a view marker**: see
   `client/frame-assert.md`.
9. **Passenger role is 3, not 2.** Varbit 19233; the crew NPC shells state a
   real npc only at rungs 0, 3, 6 and 10, so rung 2 resolves to −1 and a
   passenger saw a deck with no crew on it. (`social/roles-results.json`.)
10. **Private chat reaches the client through varbit 13674, not a login
    opcode 5.** 13674 = `chat_filter_private` = bits 13..15 of varp 1054; proc
    113 is `interface_162:0`'s `if_setonvartransmit` hook with var1054 first in
    its list, so the varp transmit *is* the repaint trigger. Varp 1054 must be
    declared `transmit=yes`, `scope=temp`. (`docs/FRIENDS_PRIVATE_CHAT.md`
    §11.3–11.5.)
11. **A targeted send carries the WIRE component identity and the published GPI
    player index**, never the runtime tree id — a `CC_CREATE` child's id
    (measured 937|49210) is a runtime allocation the server has never heard of.
    `net_out.c` is deliberately unchanged: writing the child index into
    rev-239's `g2Alt2` sub field would trip the `0xffff/0xffff` sentinel and
    route the grant to `OPPLAYERU`. (`social/editnav-arming-results.json`.)
12. **IF3 target priority: −1 resets to the default 4**, 1..32 stores
    *value − 1*, anything else is ignored (`UITree_ApplyTargetPriority`). The
    default of 4 matters — zero there demotes ops 2..4 on every script-created
    cell. The row gate is separate: an IF3 node's *effective* target mask is the
    decoded mask OR'd with **`IF_SETEVENTS` bits 11..16** (shift 11, mask
    `0x3F`, `src/engine/torirs_types.h:1012-1013`).
13. **`::vesselspawnat` does not honour the 16-heading berth clearance** that
    `ToriRSServer_VesselRecover` requires, so the fixture cheat cannot prove
    turning at a dock. Use the content selector. (HARN-1.)
14. **The `reply=<n>` login rejection is not a response code.** It is a byte of
    ISAAC keystream, which is why the same failure reports 16, 195, 209 or 239.
    (LIFE-1.)

## Known limitations that remain

Four of these are the audit's Open rows. The rest are boundaries, deferred
review items and collateral that are real, recorded, and not claimed as passing.

### Open rows

1. **C0.3 — the five wire refusals are untested.** Root cursor, dead cursor,
   non-root `world_area`, size-mismatched grid, unaligned base, plus
   `SET_ACTIVE_WORLD`'s not-live refusal. All implemented and read in source;
   none exercised, so a regression that silently *accepted* one would not be
   caught.
2. **GPU-1 — chat-filter captions ride 5 px low on the `gl3-zbuffer` lane** and
   collide with the state line under them. Plates do not move; the `Report`
   caption is unaffected; a windowed soft3d control rules out
   headless-vs-windowed. Renderer-lane specific.
3. **GPU-2 — on `gl3-zbuffer` the deck occludes the player at the deck
   camera.** `actor_commands` is 1 in both lanes, so it is a depth/ordering
   fault, not a culled command. **Pre-existing**, proven by the same occlusion
   in `gpu/historical/deck-baseline.png` from client `54a09b3f`.
4. **GPU-3 — the `gl3-zbuffer` process RSS is ~15× soft3d** and grows at scene
   transitions rather than per capture (537 MiB → 2.27 GiB across the script;
   2969 MiB after the run against 199 MiB). Recorded as an observation with its
   measurement, not as a pass or a fail.
5. **HARN-1 — `::vesselspawnat` picks illegal berths.** `ToriRSServer_VesselRecover`
   requires a footprint clear at all 16 headings; the cheat applies no such
   check, so anything proving turning at a dock must use the content selector.
6. **EMB-1 — `test-torirsserver-embed` decode break.** It failed on
   `c7e62cce` with `embed: SYNTH_SOUND reached alice's stream` and was **not**
   re-run on `4854e303` or on the final pair, so its current state is neither
   confirmed nor cleared. `embed_test.c:2259` (`saw_filter_private`) sits behind
   it, which is why the chat proof was done natively. The other twelve unrun
   targets from the 25-target table are likewise unverified on the final pair.

(GPU-1/2/3 count as one row in the ledger, which is why the tally reads 4 Open.)

### Fixed, but with a residual worth knowing

7. **LIFE-1's guard covers one emitter.** `ToriRSServer_WorldMarkVarp` is the
   only writer ever measured sending inside the reconnect window. The general
   guard arguably belongs one line into `ToriRSServer_Send`
   (`torirs_server_encode.c`) so no future login-time writer can reopen the
   hole. Also: a varp written back to **0** inside that window would be held and
   then skipped by the login flush's `if (value == 0) continue;` — harmless in
   practice, never observed, recorded rather than hidden. **No unit gate drives
   a reconnect**, so reverting the guard would not fail a test.
8. **SRV-1 has no gate on the two server callers.** `test-world` exercises
   `collision_nearest_opts_from_model` only. The
   `ToriRSServer_SceneOpNearestOpts` half is verified by source inspection plus
   the 563/0 suite, not by a test that would fail if it were reverted.

### Review items deliberately left

9. **M-3** (duplicated shore constants), **M-6** (the embed private-mode
   assertion, blocked by EMB-1 and replaced by lifecycle selftest rows),
   **L-3** (the pre-existing `!vm` guard pattern) and **L-7** (`ContentSymbol`
   zero fallbacks, the house pattern) — each with its reason in the section
   above and in `plan-audit.md`.

### Collateral, boundaries and things not photographed

10. **varp 1054 has nine tenants.** Transmitting it to carry the private-chat
    varbit was previously recorded as zeroing the client's local Channel and Clan
    filter bits. The 2026-09-07 chat re-proof never moved either off its default,
    so both read *On* throughout and the collateral was **neither confirmed nor
    cleared**. Anyone touching chat filters should test Channel and Clan
    explicitly.
11. **A passenger's view of their own deck is not photographed.** Two players on
    one deck is proven from the captain's side and on the wire
    (`multiplayer-aboard.png`, the server suite's same-deck observer rows), but
    no capture exists taken *by the guest client* looking at the deck it is
    standing on.
12. **The peer fixture is a genuine second server player, not a second external
    transport.** It sends production packets through
    `ContentTestSailing_PeerCommand`. Nothing here proves two independent socket
    connections.
13. **Sort and extractor-shore queue coverage.** The stale-queue helper pins
    arrival, hook, net and cannon with stale/current serial controls, all
    running in the 563/0 suite. The **sort** guard is in source with no test of
    its own — left out deliberately rather than pinned by an uninformative one.
    The extractor's 17-tick shore grace is covered by acceptance
    (`extractor-grace-results.json`, five checks through real mouse activation)
    but not by a selftest row.
14. **Wider world content is out of scope and must not be reported as proven.**
    `activities.md` names the boundaries: encounter producers, rare tables, the
    crystal-water reward consumer, the Kraken encounter perk. Named hazard
    waters are likewise not implemented.
15. **Deferred by the plan, still deferred:** the optional merged deck bake
    (which must come back, if ever, as a compare-mode optimisation *behind* the
    live flatten path, never in front of it), full bone-matrix pitch and roll,
    and validation of genuinely nested world-entity *content*.
16. **Not everything was re-run on the final pair**, and the docs say which:
    the full server suite, both performance suites, thirteen make targets, the
    GPU lane, the privacy run, the Checkpoint-D lifecycle capture chain and the
    `social/editnav-arming` prerequisites. Each of those files now names the
    pair its evidence came from and the difference to the final one.
17. **Two acceptance tools were repaired mid-phase**, and the repairs matter for
    anyone reading older results: `sailing_collision_acceptance.py` recorded no
    provenance at all, and `sailing_cargo_acceptance.py` stated all 26 of its
    conditions with bare `assert`, which `python3 -O` deletes outright — it
    would have printed `ok: true` while checking nothing. Both were fixed with
    named negative controls and re-run.
18. **Deleted as stale, so nothing cites them any more:**
    `docs/sailing_validation/crew-validation.json` (read `crew-actions.json`),
    `collision-maps.png` and `collision-maps.json` (read
    `collision-ocean-map.json` and `collision-shore-map.json`).
19. **Left on disk for whoever cleans up:** private objdirs
    `src/build_perf_opt_es`, `src/build_lifecycle*`, binaries `src/torirs_perf`,
    `src/torirs_lifecycle_opt`, and the `/tmp/sailing-*` session, log and render
    directories — `/tmp/sailing-fin3*` above all, which is the provenance for
    every claim on this page. No worker deleted them.

## Before committing

1. **No rebuild is owed.** The binaries above are built from the current tree,
   and LIFE-1 and SRV-1 are proven on them.
2. Consider moving LIFE-1's guard to `ToriRSServer_Send`, and adding a gate for
   the two `ToriRSServer_SceneOpNearestOpts` callers, so neither fix can
   silently return.
3. **Stage only sailing hunks.** Other sessions edit Canoe, Krait and
   plugin-engine files in this same tree. Especially mixed: `src/app.c`,
   `src/game/content_test.c`, `src/makefile`,
   `src/torirsserver/torirs_server_world_selftest.c`, UI/renderer files,
   `src/plugin/plugins/minimap_orbs.c` and the OSRS-Content submodule. Never
   `git add -A`; never reset the working tree. Commit the submodule's sailing
   content first, then the root source, tests and docs, then the submodule
   pointer.

## Sources used

Repository `docs/SAILING.md`, the real revision-239 cache, and:

- https://secure.runescape.com/m=news/prepare-for-sailing---launching-november-19th?oldschool=1
- https://secure.runescape.com/m=news/sailing-is-out-today?oldschool=1
- https://secure.runescape.com/m=news/sailing-xp-review--further-fixes?oldschool=1
- https://secure.runescape.com/m=news/the-red-reef-is-out-today?oldschool=1
- https://www.youtube.com/watch?v=17G2iSghB4I
- https://www.youtube.com/watch?v=CFtwftwNWRI
- https://osrsindex.com/wiki/cargo-hold?site=osrs_wiki
- https://github.com/blurite/rsprot/blob/master/protocol/osrs-239/osrs-239-desktop/src/test/kotlin/net/rsprot/protocol/game/outgoing/info/PlayerInfoClient.kt

Local exact deob source:
`/Users/matthewevers/Documents/git_repos/Deobfuscator/src_osrs239_rl1_12_33/deob/`
(`Statics.java`, `class121.java`, `class155.java`, `class467.java`, …).
Decompiled CS2 sources are in `OSRS-Content/osrs239-content/scripts/`; config,
DB and interface definitions under `configs/` and `interfaces/`. **Actual cache
bytes and runtime behaviour take precedence when the reconstructed source and
the shipped cache disagree.** Video storyboards were inspected under
`/tmp/sailing-references`; Junior Jim's real position 3059,2979 was
cross-checked against QuestHelper's Pandemonium data.
