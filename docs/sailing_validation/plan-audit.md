# Sailing plan completion audit

Audit opened 2026-09-06 against the working tree following sailing commits
`e3d110a7a` and OSRS-Content `802d03e8ff`. First closed 2026-09-07 ~09:30 CDT by
the `audit` worker at **45 Verified, 6 Open**. **Re-closed 2026-09-07 ~09:50 CDT**
against the *rebuilt* shared pair, which discharges the rebuild that close was
owed and turns the two rows that were waiting on it — **LIFE-1** and **SRV-1** —
into Verified.

`Verified` requires direct current evidence — a named run on the final binaries,
or a named run on a preceding pair with the difference stated.
`Open` means a defect or a missing proof remains, and the row says exactly what.
Source references identify the production path being checked; an old screenshot
or a passing unrelated test does not close a row.

**Final tally: 47 Verified, 4 Open, 51 rows.** All four Open rows are named, none
of them is a sailing requirement that went untested, and every one of them is
pre-existing, environmental or a harness fixture rather than a sailing
regression. The earlier reconciliations of this ledger (45/6 at 09:30 today,
25/21 at 18:30 on 2026-09-06, and the two earlier partial passes) are superseded
in full.

## Provenance the rest of this file depends on

### The final shared binaries

Root rebuilt both binaries at **09:35 on 2026-09-07** from the current tree,
after applying the LIFE-1 and SRV-1 fixes and the review items listed under
"The 09:40 review" below. This is the pair every claim in this ledger now rests
on. Hashes: `/tmp/sailing-fin3/hashes.txt`; build logs
`/tmp/sailing-fin3/{server,client}-build.log`, both `exit=0`
(`/tmp/sailing-fin3/status.txt`).

| Artifact | SHA256 | Short |
|---|---|---|
| `src/torirs` (embedded client + server) | `5e34b81f9d2a45eae7306e8ddf93de5c590a0b3cd475d726a7745eb0deffd03b` | `5e34b81f` |
| `src/build_opt/torirsserver` (standalone server) | `6a0e7f0441c25f1249bc8342a5103744a4dbd991d721b67eaea1846db7d910d4` | `6a0e7f04` |
| `OSRS-Content/.../server/scripts/build/script.dat`, 30,076 scripts | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` | `902a6b8d` |

The pack is **unchanged** — no content was recompiled for this rebuild.

The server hash `6a0e7f04` is byte-identical to the intermediate 09:34 build
taken before the last batch of review edits, because those edits added only
`assert()` calls and the `OPT=1` server compiles them out. That is an
explanation, not a claim that the source is identical; the source difference is
listed below.

### The 08:08 pair `4854e303` / `214b43d7`, and what separates it from the final one

Everything in this ledger that names `4854e303` / `214b43d7` / `902a6b8d` was
produced by the pair root built at 08:08 today. The final pair differs from it
by exactly these source changes and nothing else:

1. **LIFE-1** — the reconnect guard in `ToriRSServer_WorldMarkVarp`
   (`src/torirsserver/torirs_server_world.c:11260`), by the lifecycle worker.
2. **SRV-1** — `ToriRSServer_SceneOpNearestOpts` now writes `out->unbounded = 0`
   (`src/torirsserver/torirs_server_scene.c:2446`).
3. **M-1** — `src/game/sailing_paint_order.u.h` no longer `malloc`s per frame:
   grown-once file-scope scratch buffers, a `_Static_assert` tying
   `WORLDVIEW_MAX` to `PAINTER_MAX_WORLD_VIEWS`, and span view bounds asserted.
   This is the only change on a render path, and it *removes* work.
4. **M-2** — the `TORIRSSERVER_EXT_DEBUG` `getenv` in the player extended-info
   v5 writer is cached in a static
   (`src/torirsserver/torirs_server_encode.c:4529-4531`).
5. **M-4** — the three compound asserts in `src/game/content_test_sailing.c`
   split to one condition per assert.
6. **L-1 / L-2** — parameter asserts added to the lifecycle static helpers, the
   `mock239` helpers and the `app.c` paint-order callback.
7. **L-4** — `World_ResetSceneAlloc`'s assert carries its reason string again
   (`src/world/world.c:518`).
8. **L-5** — a doc-comment ordering fix in `src/world/world.h`.
9. **L-6** — `sailing_paint_order_test.c` gained `memcmp` byte-identity controls
   for the flat pass, including the `count == 0` case.

Items 3–9 are assertions, test controls and a comment; items 1 and 2 are the two
behavioural fixes, and both are re-proven below on the final pair. So a result
recorded against `4854e303` / `214b43d7` is **current except where LIFE-1 or
SRV-1 is the thing being measured**, and the rows that measured those two were
re-run.

The 2026-09-06 21:38 pair `c7e62cce` / `a53899eb`, against the same pack, is one
step further back: it additionally lacks the lifecycle selftest fixture (which
is why the sailing suite reported 561 rather than 563), the
`TORIRSSERVER_SCRIPTS` fallback in `torirs_server_world_selftest.c`, and
`collision_nearest_opts_from_model` initialising `out->unbounded`. A
`c7e62cce` / `a53899eb` result is treated as current for everything except those
two fixtures, the `test-world` route case and the two fixes above; every row
that rests on one says so.

A result from any earlier binary (`9c2a58e3`, `092d1e5b`, `54a09b3f`,
`8d16856e`, `51e2b023`, `622ebea1`) or any earlier pack (`b9bed76d`, `9a8b44bb`,
`78a5a8ee`, `a14736bc`) is **historical**: it proves what it measured at that
build and closes nothing here.

### What was re-run on the final pair

**Unit gates — twelve targets, all exit 0**, run serially in the shared objdir
`src/build_opt_es` (`/tmp/sailing-fin3/status.txt`, one
`/tmp/sailing-fin3/<target>.log` each): `test-sailing-paint-order`,
`test-painters-world-entity`, `test-frame-flat`, `test-world`,
`test-minimenu-world`, `test-cs2-sailing-settings`, `test-sailing-collision`,
`test-mock239-playerinfo`, `test-wev-rebuild`, `test-wev`, `test-rsprot-bridge`,
`test-social`. Two more targets than the 08:08 re-stamp ran, and the same ten
plus `test-painters-world-entity` and `test-wev`.

**Server, sailing-only suite: 563 checks, 0 failures** on `6a0e7f04`
(`/tmp/sailing-fin3/sailing-selftest.log`, `.tsv`; `status.txt` records
`sailing-suite exit=0`).

**The full server suite was NOT re-run on `6a0e7f04`.** It was **250,071 checks,
0 failures** on `214b43d7` earlier today (`server/full-selftest-final.log`), and
the only server-side source differences since are SRV-1's one-line
initialisation, the `getenv` cache, and asserts that `OPT=1` compiles out. That
is a reasoned carry-forward, not a re-measurement, and it is stated here so no
reader mistakes it for one.

**Acceptance on the final pair, every tool `ok: true`** — logs in
`/tmp/sailing-fin3/accept/*.log`, results JSON regenerated in place under
`docs/sailing_validation/` carrying `5e34b81f` provenance:

| Tool | Session | Result JSON | Result |
|---|---|---|---|
| collision | `/tmp/sailing-fin3-core` | `collision-results.json` | `ok: true` |
| cargo | `/tmp/sailing-fin3-core` | `cargo/results.json` | `ok: true`, 10 checks |
| deck | `/tmp/sailing-fin3-core` | `deck-results.json` | `ok: true` |
| facility | `/tmp/sailing-fin3-facility` | `activity-results.json` | `ok: true`, 9 captures |
| crew | `/tmp/sailing-fin3-crew` | `crew-actions.json` | `ok: true`, 6 captures |
| extractor | `/tmp/sailing-fin3-extractor` | `extractor-grace-results.json` | `ok: true`, 5 checks |
| client | `/tmp/sailing-fin3-client-overlap` | `client/client-results.json` | `ok: true`, 9 captures |
| multiplayer | `/tmp/sailing-fin3-multiplayer` | `multiplayer/results.json` | `ok: true` |
| social | `/tmp/sailing-fin3-social-ocean`, `-berth` | `social/results.json` | `ok: true`, 12/12 |

Two of the regenerated captures were opened and checked against their prose:
`collision-stopped.png` (the skiff stopped against a shore, chat *The boat runs
aground.*, hull 78/80, Facilities panel reading *Steering*) and
`social/editnav-armed-on-guest.png` (the *Edit-navigator → Deckhand (level-1)*
hover with the guest outlined on deck). Both match.

> **The first acceptance pass in this session is not evidence.**
> `/tmp/sailing-fin3/accept/status.txt` shows five failures
> (`core-start exit=127`, then collision/cargo/deck/client). Those are a
> shell-scripting mistake — `zsh` did not word-split a variable, so the tools
> were invoked with a malformed argument. The real results are
> `/tmp/sailing-fin3/accept/status2.txt` and the per-tool logs listed above.
> Recorded rather than deleted, because the failing file is still on disk.

**Performance numbers were not re-measured and are not claimed to have been.**
`harness-performance.json` and `render-final-results.json` were measured on
`4854e303`. The final pair differs from it only by the nine items above; the one
that touches a render path, M-1, replaces two per-frame `malloc`/`free` pairs
with grown-once scratch buffers, which can only reduce the measured cost. The
numbers are therefore carried forward as still relevant, and **no new number was
invented for the final pair.**

### Where each cited evidence file stands

**Final pair `5e34b81f` / `6a0e7f04` / `902a6b8d`:**
`collision-results.json`, `cargo/results.json`, `deck-results.json`,
`activity-results.json`, `crew-actions.json`, `extractor-grace-results.json`,
`client/client-results.json`, `multiplayer/results.json`, `social/results.json`,
`README.md`, `server/README.md` (§"Final pair `6a0e7f04`"), `unit-gates.md`
(§"Final pair `5e34b81f`"), `client/gates.json`
(`final_pair_5e34b81f_2026_09_07`), `lifecycle-reconnect-results.json`
(§`final_pair_verification`).

**Pair `4854e303` / `214b43d7` / `902a6b8d`, difference stated above:**
`lifecycle-dock-results.json`, `lifecycle-chat-results.json`,
`harness-performance.json`, `render-final-results.json`,
`server/sailing-selftest-final.{log,tsv}`, `server/full-selftest-final.log`,
`server/README.md` (§"Final-binary re-stamp"), `unit-gates.md`
(§"Final-binary re-stamp"), `client/gates.json`
(`final_binary_restamp_2026_09_07`), `gpu/historical/README.md`.

**Pair `c7e62cce` / `a53899eb` / `902a6b8d`, difference stated above:**
`social/editnav-arming-results.json`, `privacy/results.json`,
`privacy/README.md`, `gpu/gl3-results.json`, `gpu/soft3d-results.json`,
`gpu/gpu-results.json`, `gpu/README.md`, `server/README.md` (§runs 1–5),
`unit-gates.md` (§the 25-target table), `client/README.md` and
`multiplayer/README.md` (§the 2026-09-06 run), `server-protocol.md`,
`activities.md`.

**Carrying no provenance of their own:** `collision-ocean-map.json` and
`collision-shore-map.json`. The map dump the collision tool writes records the
survey, not the binary; both files were last rewritten by a run on the final
pair (only their `runtime_ms`/`elapsed_ms` moved), but **the files themselves do
not say so** and cannot be told apart from a run on any other build. Read
`collision-results.json`, which does carry `5e34b81f`, for the collision
provenance.

**Historical, retained, closing nothing:** `final-core-results.json`,
`reload-results.json`, `cannon-panel-results.json`,
`ammunition-effects-results.json`, `native-build-xp-results.json`,
`native-core-*`, `native-panel-results.json`, `utility-*-results.json`,
`shipyard-range-results.json`, `sloop-controls.json`, `social-results.json`
(a superseded top-level file — read `social/results.json`),
`render-initial-results.json`, `render-lumbridge-repeat-results.json`,
`social/roles-results.json` (private `roles` build `1c41816a`),
and everything now under **`gpu/historical/`**.

### Housekeeping

- **`collision-maps.json` deleted** (09:30 pass). It was a byte-for-byte
  duplicate of `collision-ocean-map.json` apart from two timing fields
  (`runtime_ms` 1.392 vs 1.328, `elapsed_ms` 2.965 vs 3.081), carried no
  provenance, was written by no current tool, and was referenced from nowhere in
  `docs/` or `tools/`.
- **`collision-maps.png` deleted** (09:45, by root). The 2026-09-06 11:11 render
  of that same deleted data. It was referenced only by the prose that called it
  stale. The current collision tool writes `collision-ocean-map.json` and
  `collision-shore-map.json`.
- **`crew-validation.json` deleted** (09:45, by root). A 2026-09-06 11:12
  artifact that no current tool writes; the crew acceptance writes
  `crew-actions.json`, which is regenerated on the final pair. It too was
  referenced only by the prose that called it stale.
- **Nine stale GPU artifacts moved to `gpu/historical/`** with a note:
  `deck-baseline.png`, `deck-restored.png`, `deck-net-installed.png`,
  `deck-net-restored.png`, `deck-sailing.png`, `deck-sailing-next.png`,
  `deck-sailing-looped.png`, `ocean-skiff.png` and their `deck-results.json`.
  All nine came from client `54a09b3f` at 13:00–13:23 on 2026-09-06 with
  `allow_stale_scripts: true`. `gpu/README.md` cites `deck-baseline.png` as
  prior art for defect GPU-2; that citation now reads
  `gpu/historical/deck-baseline.png` in that file.

### No source file is newer than the shared binaries

The 09:30 close recorded `src/torirsserver/torirs_server_world.c` (08:37) as
newer than the 08:08 build, and said a shared rebuild was owed. **That rebuild
happened at 09:35 and this note is discharged.** Both binaries were built from
the current tree; nothing under `src/` is newer than them.

### The 09:40 review, and what was deliberately left

A read-only review of the sailing hunks raised six medium and seven low items.
Six were fixed (M-1, M-2, M-4, L-1/L-2, L-4, L-5, L-6 — items 3–9 in the
difference list above). Four were **left, deliberately**, and are repeated in
the limitations list rather than hidden:

- **M-3 — the shore constants are duplicated.** The same shore coordinates are
  written in more than one fixture. Consolidating them is a refactor across
  files other sessions are editing in this tree; the duplication is inert and
  visible, and a bad merge is the larger risk.
- **M-6 — `test-torirsserver-embed`'s private-mode assertion.** It sits behind
  the long-standing embed decode break (**EMB-1**), so it cannot be exercised.
  The behaviour it wanted to pin is instead covered by lifecycle selftest rows
  in the 563/0 suite. Replacing a blocked assertion with a running one is the
  better trade; re-enabling it waits on EMB-1.
- **L-3 — the `!vm` guard pattern.** Pre-existing, and the same shape appears
  throughout the CS2 host. Changing it here alone would make the sailing hunk
  inconsistent with every neighbour without fixing anything.
- **L-7 — `ContentSymbol` zero fallbacks.** The house pattern for that API
  across the content layer. Same argument as L-3.

## Requirement ledger

Rows that name `4854e303` / `214b43d7` were measured on the **08:08 pair**, and
rows that name `c7e62cce` / `a53899eb` on the **2026-09-06 21:38 pair**; the
difference between each of those and the final pair `5e34b81f` / `6a0e7f04` is
stated in full above. Rows that name no hash cite a unit gate, which was re-run
on the final pair.

| Requirement | Current evidence / outstanding verification | State |
|---|---|---|
| C0.1 root alias and 16-view registry | `App.worldviews` + `src/world/worldview.c:138` (`WorldviewRegistry_Get` asserts `reg`, `id >= 0`, `id < WORLDVIEW_MAX`, `views[id].live` — one condition per assert). Bound proven twice: `test-wev-rebuild` "maximum 16 views: root +15 actual-cache decks hold 19977 static elements; one actor migrated all 15 pools; isolated despawn PASS" (`client/gates.json` gate `max_views`), re-run PASS on the final objdir (`unit-gates.md` re-stamp gate 2), and `test-painters-world-entity` "the chain nests to the registry bound" / "capacity guard adds one refused pair, never another descent". | Verified |
| C0.2 active-world cursor, zone routing, tick-end reset | `test-net-exec`, `src/game/test/rs_gameproto_exec_test.c:340-406`: SET_ACTIVE_WORLD decodes id then plane; the exec arms `active_world` **and** `active_world_level`; SERVER_TICK_END resets both to root/0. PASS in the 25-target table (`unit-gates.md` row 3, `c7e62cce` objdir; no client source changed between the pairs). | Verified |
| C0.3 rebuild id/plane routing and unknown-id contract | Positive half fully proven and re-run: `REBUILD_WORLDENTITY_V4` carries only `p2 baseX, p2 baseZ` (`3rd/rsprot/packets/rebuild_worldentity_v4.c`), the view comes from the captured SET_ACTIVE_WORLD cursor (`src/game/task_gameproto_exec.c:280`), the plane from that packet's level byte — `test-wev-rebuild` "baseX carried"/"baseZ carried"/"grid carried raw, header stripped"/"grid decodes at 1x1 zones"/"bigger view than the stream rejected"/"short grid rejected at decode"/"trailing grid byte rejected at decode" (`src/world/test/wev_rebuild_test.c:136`), PASS on the final objdir. **Residual gap, unchanged and precisely this:** no automated check pins the five wire-refusal diagnostics — root cursor, dead cursor, non-root `world_area`, size-mismatched grid, unaligned base — nor SET_ACTIVE_WORLD's not-live refusal. All six are implemented and were read in source; none is exercised by a test, so a regression that silently *accepted* one of them would not be caught. CLOSES ON: negative cases added to `test-net-exec` / `test-wev-rebuild`. | **Open** |
| C1.1 archive-72 config and 16 headings | `test-wev` current real cache: 14 records, four pinned; synthetic fields and rotated footprints pass (`unit-gates.md` row 1). | Verified |
| C1.2 entity state and bounded target queue | `src/world/wev.h:183-200` — `WEV_TARGET_QUEUE_SLOTS 10`, `WEV_TARGET_PENDING_MAX 9` guarded by a `_Static_assert` (the release lane defines NDEBUG so it cannot be a runtime assert), `WEV_INTERP_CYCLES 30`, `WEV_OP_MASK_ALL 31`; `struct Wev` (`wev.h:215-300`) carries id/view/parent/config/transform/queue/teleport serial/interpolator/priority group/op mask/seq state. `test-wev` queue/backlog/drain tests pass. | Verified |
| C1.3 spawn, despawn and delta decoder | `test-wev` real client reader arms and trailer checks pass. | Verified |
| C1.4 30-cycle interpolation and iterative driver | `test-wev` linear/wrap/snap/hold/backlog/worklist/terrain tests pass. | Verified |
| C2.1 isolated deck worlds/builders/painters, shared scene | `test-wev-rebuild` (PASS on the final objdir): "per-view scene pools are distinct and fit the element pool tag", "an actor's element moves between view pools without leaking or stranding", "boat and root rebuilds isolate their scene pools, despawn frees only the deck", plus the 16-view capacity gate "root +15 actual-cache decks hold 19977 static elements". | Verified |
| C2.2 native deck rebuild templates | Current real-cache 1-zone raft rebuild passes ("ok - 1-zone raft deck rebuilt into the boat world against ../cache.osrs239"). | Verified |
| C2.3 removed-entity queue drainage — **including the root** | The boat half was always drained (`task_gameproto_exec.c` REBUILD_WORLDENTITY branch calls `App_WorldDrainEntityRemovedFor(app, view->world)` before the deck load; `App_WevDespawn` drains the departing view; `app_world_load_begin` drains). **The root rebuild did not**, and that was a real abort: a despawn arriving in the same packet pump as a teleport-driven rebuild reached `World_ResetSceneAlloc` with `event_count == 1` — `Assertion failed: (world->event_count == 0 …), world.c:515` under an `OPT=0` client, and a silent dropped-removal / orphaned DYNAMIC element under a release client. Fixed in the REBUILD_NORMAL / REBUILD_REGION branch of `src/game/task_gameproto_exec.c`, pinned by `test_root_rebuild_drains_entity_removed()` in `src/world/test/wev_rebuild_test.c` **with a named negative control** (removing the drain call makes the test abort at the same assert), and re-run PASS on the final objdir. Full write-up and three reviewed captures: `client/world-drain.md`. | Verified |
| C3.1 pseudo-loc insertion and parent height/order | `test-sailing-paint-order` PASS on the final objdir: "scene dependencies: flat before full, parent ground before boat; nested ownership, raised shore, loc order and zero-boat bytes PASS". Radius-60 insertion width: `test-wev-visibility` "radius60 inside a tile" / "radius60 crosses tile boundaries". Ordering: `test-painters-world-entity` "a loc behind the boat emits before BEGIN_WORLD" / "a loc in front of the boat emits after END_WORLD" / "the batch really is cut". | Verified |
| C3.2 bounded checked stack and balanced markers | `test-painters-world-entity`: `test_cycle_is_refused_not_re_entered`, `test_distinct_view_alias_is_refused`, `test_nesting_to_the_registry_bound` ("one BEGIN per view", "capacity refusal is balanced", "no duplicate models after capacity refusal"), `test_unbound_view_emits_an_empty_pair`. | Verified |
| C3.3 painter-local sorting scratch | The three file-scope qsort statics are gone. `src/painters/painters.c:160-175` is `scenery_queue_insertion_sort(queue, len, painter, cam_sx, cam_sz)` — painter and camera are parameters, with the comment naming the exact hazard. Reentrancy pinned by the nesting/cycle/alias tests in C3.2. No sailing hunk touches `src/painters/painters.c`. | Verified |
| C3.4 per-view transforms and frame emit — **including the prefetch fix** | `test-frame-flat` PASS on the final objdir ("frame flat live geometry, scale, colour, no-pick, replay lifetime and root identity"), against `frame_view_push`/`frame_view_apply`/`try_emit_world_draw_model` in `src/render/torirs_frame.c` and `src/render/torirs_frame_flat.u.h`. **A real defect was found and fixed here:** the emit loop's four-deep prefetch pipeline resolved element ids *across an unconsumed view marker* — its reach walk tested commands `cur + 1 .. cur + depth` and never `cur` itself, so when the current command *was* a `BEGIN_WORLD`/`END_WORLD` marker it resolved the three commands after it against the wrong view, aborting an `OPT=0` client in `frame_lookahead_element_id`. Fix: start the marker test at `cur`. It changes only which ids the prefetch resolves early, not the emitted set. Write-up and captures: `client/frame-assert.md`. Visual: `client/client-peer-flat.png`, `client/client-full-restored.png`, both reviewed. | Verified |
| C4.1 native flat scale/offset/HSL, no actors/picks | The unconditional merge bake is **deleted** (`app_wev_flat_ensure`, `app_wev_flat_free`, `App_WevFlatInvalidate` removed from `src/app.c`/`src/app.h`). Flatten is live per-frame view state — `flatten_scale = 0.01f`, `flatten_y_offset = -1200`, `flat_hsl`. `test-frame-flat` (exact scale, pre-scale offset, HSL/texture override, no-pick, live next-frame changes), `test-wev-population` "flat view population: own/borrowed actors and graphics removed; runtime scenery retained; next-frame restoration PASS", `test-pick-level` "flattened views contribute no actor, hull or scenery picks". Visual: `client/client-peer-flat.png`, `client/client-peer-restored.png`. | Verified |
| C4.2 aboard priority, group ordering, budget, oriented overlap | The `TORIRS_WEV_BUDGET` static override is gone; the native limit is CS2 7900/7901. `test-cs2-worldentity-limit`: real VM/host init, round trip, default 30, zero and negative clamp. `test-wev-visibility`: "native fine AABB touching boundary intersects", "actor outside diagonal OBB is not an overlap", "boat overlap uses native transformed AABB, not full SAT", "aboard is uncapped and each group receives its own slot", "over-budget boats are skipped", "zero limit preserves only aboard", "flatten consumes its group draw slot", "group1 yields to an overlapping actor", "aboard never flattens under actor or budget", "native Q16 trig … keeps the actual skiff edge at 343 fine units". Visual: `client/client-three-overlap.png`, `client-priority-two.png`, `client-budget-one.png`, `client-budget-zero.png`, all reviewed on `4854e303`. | Verified |
| C4.3 HSL override at emit | `test-frame-flat`, against `try_emit_world_draw_model` → `frame_flat_model(arena, model, scale_y, flat_hsl)`. Visual: `client/client-peer-flat.png` — "Both priority cases composite the flat silhouette below the overlapping full boat." Confirmed on the GPU lane too: `gpu/gl3-flat-silhouette-x3.png` shows `gl3-zbuffer` and `soft3d` drawing the same flat hull. | Verified |
| C5.1 geometric actor membership and per-view cycles | `test-wev-population` (own/borrowed player/NPC/graphics membership per view, restoration next frame). Native, re-run on the final pair `5e34b81f` at 09:36: `multiplayer/results.json` — peer `HarnessMate` is `view 1`, `home_view 1`, deck-local fine 448,472→448,448 with `route_run 1` while the hull translates, then removed and restored; `multiplayer/multiplayer-aboard.png` and `-running.png` reviewed. Server mirror, run on `214b43d7`: "RUN takes exactly two deck tiles while the hull moves", "a standing passenger gets no fake PLAYER_INFO walk, respawn or placement as the hull moves". | Verified |
| C5.2 all click modes, view identity, op mask, deduplication | `test-pick-level`: "mode0 replaces interactive and inert hits with one hull target", "mode1 keeps native contents and drops inert hull", "mode2 falls back to boat only for noninteractive geometry", "mode3 swallows underlying root picks and hover", "aboard forces contents-only even for blocker configuration", "flattened views contribute no actor, hull or scenery picks", "budget-skipped views contribute no picks", "aboard terrain remains in its deck coordinate frame". `test-minimenu-world` (PASS on the final objdir) covers the five-bit hull op mask, nearest-hull deduplication and contents-only picks that cannot invent hull operations. Native: the deck helm loc right-clicks as "Navigate Helm" (`lifecycle-helm-menu.png`) and the shore gangplank as "Board"/"Disembark" (`lifecycle-shore-board-menu.png`, `lifecycle-disembark-menu.png`). | Verified |
| C5.3 composed camera, threshold smoothing, roof mode | `test-wev-visibility`: "camera smooths exactly at inclusive500 boundary", "over-threshold axis snaps both camera axes", "negative camera threshold snaps", "sub16 camera distances converge instead of stalling". | Verified |
| S0 eight player-scoped scene windows | **Run on the shared server `214b43d7`**, sailing-only suite, `server/sailing-selftest-final.tsv` rows 000001–000015, all PASS: "a second player joins to hold the far window", "the fixture has to separate them by more than half a scene, got 129", "the far player's own tile is inside their window", "which cannot also hold the home player's tile", "both tiles are covered by the union of windows", "five ticks with both standing still move nobody's window", "neither window is reconstructed at the same centre while stationary". The plan's "genuine multiplayer limit" is deleted. | Verified |
| S1.1 vessel registry/fields | Run on `214b43d7`: "fifteen real vessel instances coexist within the sixteen-view client bound", "sixteenth hull is rejected cleanly before acquiring any deck resource", "refused spawn leaks neither a hull nor a private map instance", "freeing one hull permits a visible replacement with a new serial", "all boat and activity resources are returned after capacity testing", plus per-hull rows 000362–000391 ("live hull *n* reserves a visible view and an independent deck window", "hull *n* owns unique view/window resources"). | Verified |
| S1.2 turn/quantized swept movement, separate boat map, no hull collision | Run on `214b43d7`: "tick %d turns to exactly %d" (8 rows), "tick %d lands on the quarter-tile quantum" (8 rows), "the blocked sail parks short of the land tile", "boat and player collision maps have independent storage", "the real ocean's 17x17 area permits boats and blocks walking". Standalone gate `test-sailing-collision` PASS on the final objdir ("passed (0 failures)"). Native, re-run on the final pair `5e34b81f` at 09:36 (previously `4854e303`): `collision-results.json` (now carrying provenance — see the tool repair in `README.md`) with `collision-underway.png`, `collision-stopped.png`, `collision-restored.png`, `collision-mid-motion-restored.png` and the named negative control `collision-land-rejected.png`. Hull-vs-hull is **deliberately absent** and is not a gap (`docs/sailing_coverage.csv` SAIL-24). | Verified |
| S1.3 bidirectional projection at 16 headings | Client: `test-painters-world-entity` "deck <-> parent round trips at all 16 headings", "every deck point is aboard from the parent side too", "the bow stays at the bow through a full 360", "the bow traces a circle around the hull, not a drift". Server, run on `214b43d7`: "the pivot maps to the hull position at angle %d" (7 rows) and "deck (%d,%d) round-trips at angle %d" (28 rows). | Verified |
| S2.1 per-observer spawn/delta/snap/despawn; rev230 refusal | Run on `214b43d7`: "every tick under way carries an op-2 record", "op 0 carries no flags byte", "after which the observer tracks nothing", and the explicit negative case "revision230 refuses vessel packets and does not mutate v239 observer tracking". | Verified |
| S2.2 native deck rebuild encoding | Run on `214b43d7`: "the spawn tick sends the deck's REBUILD_WORLDENTITY", "its header is the deck's SW tile (%d,%d)", "zone-aligned, as the client asserts", "and names the one source map square the deck came from", "the descriptor grid is %d bits over %dx%d zones". Matches the C0.3 wire contract. | Verified |
| S2.3 per-view zone sandwich | Run on `214b43d7`: "the deck map is bracketed by two world selects", "the first names the hull's view %d", "and the second puts the cursor back on the root". | Verified |
| S2.4 cross-world PLAYER_INFO/NPC_INFO coherence | Server, run on `214b43d7`: "moving deck NPC produces an actual NPC_INFO for observer %d", "observer %d decodes the NPC's step plus hull motion", "shore observer receives the other hull's initial native facility snapshot", "quiet published decks do not replay their state or expired animations". Native, re-run on the final pair `5e34b81f` (previously `4854e303`): `multiplayer/results.json` (shore observer and same-deck observer, hull motion vs deck RUN, preserved model id, removal and restore) and `client/client-results.json` (real peer on boat 2 at deck-local fine 448,576). All five `multiplayer/*.png` opened and described in `multiplayer/README.md`. | Verified |
| S3 dock/helm/steer/disembark, persistence and opcodes | Server, run on `214b43d7`: "save while physically aboard", "save never persisted the deck pool coordinates", "save records an owned slot and relative deck tile", "logout's fallback is the real recorded shore", "free evacuates both captain and guest to walkable shore" (the previous full-suite failure — a fixture defect, now fixed and green on the *shared* server), "production logout frees both session and owned vessel", "production login restores aboard its raft under the new session uid", "a save with no [chat] section keeps the all-ON defaults". Native, on `4854e303`, `lifecycle-dock-results.json` + `lifecycle-chat-results.json`: gangplank → native 934 selector → Board; helm taken from the **physical** helm loc 59537; Set sails; out-and-back voyage 3073,2984 → 3081,2997 → 3074,2984 with zero hull damage; disembark ("You walk down the gangplank.", combat tab restored); raw logout then a fresh `--resume-save` process resuming ashore with inventory intact; aboard resume with deck pose, angle 128, HP 78/80, stopped and no stale helm lease. Chat modes persist: Private→Friends survives a raw logout and a genuinely fresh resume process, and a later trade-only change leaves Private alone. The former residual, LIFE-1 — the logged-out client's own auto-reconnect being rejected when the save reconstructs a boat — is **fixed and re-proven on the final pair** (five clean cycles, `/tmp/sailing-fin3-life/client.log`); see the LIFE-1 row. | Verified |
| Checkpoint A: actual offline build + server mover | Client half: `test-wev`, `test-wev-rebuild` against the real `cache.osrs239`, no skips. Server mover half: the S1.2 rows above, now run on `214b43d7`. | Verified |
| Checkpoint B: native sailing arc and shore ordering | Command-stream ordering: `test-sailing-paint-order`, `test-painters-world-entity`, both PASS on the final objdir. Ocean-motion imagery **re-captured on `4854e303`** by the deck, collision and client acceptance runs and reviewed. This worker independently opened three of them: `collision-underway.png` — skiff under way on open water, sail set and bellied white, red masthead pennant, player amidships in the cyan deck square, chat "Vessel 1 sailing heading 0 at tier 2."; `deck-sailing.png` — the same hull at an oblique heading with boom and forestay drawn, chat "You patch the hull. 80/80" above the sailing line; `lifecycle-native-steering.png` — under way from the Pandemonium pier with the sail full, the sails control lit red in the Facilities row and the pier receding to the frame edge. Shore ordering is additionally visible in `client/client-raised-coast.png` (bow stopped against the raised coast, hull 78/80). | Verified |
| Checkpoint C: overlapping full/flat boats | `client/client-results.json`, re-run on the final pair `5e34b81f` at 09:36 (9 captures, `/tmp/sailing-fin3/accept/client.log`), previously on `4854e303`, `visual_review.passed = true`, reviewed images `three-overlap`, `priority-two`, `budget-one`, `budget-zero`, `full-restored`, `peer-visible`, `peer-flat`, `peer-restored`, `raised-coast`, with the recorded findings "All full hulls retain uninterrupted planking and native mast/helm/cargo geometry", "Both priority cases composite the flat silhouette below the overlapping full boat", "The real peer is visible on the neighboring deck, absent while that deck flattens, and visible again when restored". Confirmed on the GPU lane: `gpu/gl3-three-overlap.png` vs `gpu/soft3d-three-overlap.png` flatten the same vessel 3, with identical `wev` counters. | Verified |
| Checkpoint D: normal dock-to-ocean-to-dock flow | Re-walked end to end **on the shared binary `4854e303`** by the `lifecycle` worker, with every capture re-taken and opened: `lifecycle-shore-board-menu.png` → `lifecycle-dock-boat-selector.png` (native 934, row "1 - Adamant Bane, Location: The Pandemonium") → `lifecycle-dock-boarded.png` → `lifecycle-helm-menu.png` / `lifecycle-dock-helm.png` ("Navigate Helm", panel flips Not steering → Steering) → `lifecycle-native-steering.png` → `lifecycle-returned-dock.png` → `lifecycle-disembark-menu.png` → `lifecycle-disembarked.png` → `lifecycle-before-logout.png` / `lifecycle-after-login.png`. Turning at a berth is proven with four headings (4, 8, 13, 1 = a full 360°) at a fixed fine position 393408,382016 with HP steady at 78/80 and no grounding line — `lifecycle-turn-heading8.png` and `lifecycle-turn-heading1.png`, both of which this worker also opened. Initial-login menu reads "Disembark Gangplank" (`lifecycle-login-disembark-menu.png`), confirming root's memo fix; the 2026-09-06 negative controls `lifecycle-login-stale-menu-before.png` and `lifecycle-aground-control.png` were left untouched and are **not** success evidence. **The berth caveat is settled rather than waived** — see the `::vesselspawnat` row below. | Verified |
| CLAUDE assertion/allocation conventions | Read-only review of the sailing hunks and the new sailing files. Every allocation is asserted: `src/render/torirs_frame.c` `calloc(1, sizeof(*frame->flat_arena)); assert(frame->flat_arena);`, `src/render/torirs_frame_flat.u.h:71` `assert(entry)`, `src/game/sailing_paint_order.u.h:68/106-111/156` (five separate `assert()` calls, one per pointer). Pointer parameters are asserted one condition per assert; the only NULL-tolerant guard, `if( !arena ) return;` at `torirs_frame_flat.u.h:81`, is inside a deallocator, which the convention exempts. **The forbidden pattern the earlier pass found is gone:** `sailing_lifecycle_selftest.u.h:19` is now `int32_t* old_varps = malloc(sizeof(player->varps)); assert(old_varps);` — the `SELFTEST_CHECK(old_varps != NULL, …); if( !old_varps ) return;` pair that both handled an allocation failure as an `if` and pinned the silent-failure behaviour has been deleted. Remaining, cosmetic only: `sailing_lifecycle_selftest.u.h:236` allocates `legacy` and asserts it at line 240, with an unrelated `fopen` between them — the assert is present and correct, only not adjacent. | Verified |
| Rasterizer remains 32-bit; 4-pixel palette blocking | Invariant holds by construction: `git diff --stat` shows **no** change under `3rd/toridraw` and **no** change to any `src/painters/painters*.c`/`.u.c` — the only `src/painters` entry in the whole working tree is the new test `painters_test_world_entity.c`. `test-scanline` passes all variant parity checks ("all scanline variants agree with their branching counterparts"). The zero-boat A/B benchmark is the independent confirmation: 12 scenes × 6 runs each produced **one unique image SHA256 and one unique counter dict per scene**, re-derived from the raw rows rather than taken from the tool's own verdict. | Verified |
| OQ1 real config and staging maps | `test-wev` and `test-wev-rebuild` loaded actual `cache.osrs239`, no skips. | Verified |
| OQ2 pick view identity | `test-pick-level` proves the side-channel view id across all four click modes plus the aboard override and the deck-local terrain frame; `test-minimenu-world` proves the row it produces cannot invent hull ops. | Verified |
| OQ3 structural nested worlds | `test-painters-world-entity` `test_nesting_to_the_registry_bound` — "the chain nests to the registry bound", "one BEGIN per view", "every level in the chain painted exactly once", "the innermost view is strictly inside the outermost", plus the capacity refusal at the 16-context boundary. Native *nested content* validation stays explicitly deferred by the plan and is not claimed. | Verified |
| OQ4 scene element capacity across 16 views | `client/gates.json`: `max_view_static_elements = 19977`, `max_registered_views = 16`, cache `cache.osrs239` — root plus 15 real-cache decks, one actor migrated through all 15 pools, isolated reverse despawn, no SKIP. | Verified |
| R1 player-info regression across switches | Server, run on `214b43d7`: "different-plane distant rider leaves the actual client's high-resolution table", "coarse-region re-entry promotes the same rider into its real deck space and appearance", "shore observer decodes the disembark placement without stale deck offsets", "moving aboard keeps the same remote player without remove/re-add". Native: `multiplayer/results.json` re-run on `4854e303` and again on the final pair `5e34b81f`, with **all five `multiplayer/*.png` opened and described** on the `4854e303` captures (`multiplayer/README.md`), plus `multiplayer-second-boarder-tile.png` and the x8 restore diff. The final two-coordinate contract — projected root coordinates for visibility/coarse presence, raw coordinates for high-resolution PLAYER_INFO — is what retires this risk. | Verified |
| R2 zero-boat render p50 neutrality | `render-final-results.json`: the complete 12-scene manifest suite, 3 repeats, 72 runs, all exit 0. Pixel and counter parity 12/12, re-derived independently from the per-run rows. Median render Δ **+0.32 %**, mean +1.09 %, median paint Δ −1.97 %. The two scenes that exceeded noise were re-measured alone and did not reproduce: `grand-exchange-orbit` +8.23 % → +0.18 %; `falador-ground` +4.53 % → +1.99 % (median diff 0.069 ms, exact two-sided p = 0.119, i.e. *not* neutral) → an independent second 5-repeat run at +0.32 %, p = 1.0. Both unfavourable measurements are preserved with their follow-ups, as are `render-initial-results.json` and `render-lumbridge-repeat-results.json`. All 24 scene images were opened and described. The causal control is exact: the two executables link the same frozen application objects and differ only by the control's `painters_bucket.u.c` replacing the two `if( scenery_is_world_entity(element) )` branches with `if( 0 )` (a 2-line diff; `painters.c` byte-identical). **Two caveats recorded rather than hidden:** the benchmark pair was built from the 08:54 sources through a sanctioned private objdir, not from root's exact 08:08 objects (the A/B is unaffected — both halves share the same frozen objects — but the pair is not the shared binary); and two of the twelve scenes (`grand-exchange`, `grand-exchange-ground`) render a nearly featureless cobble floor, so they carry timing weight but little visual-parity weight. | Verified |
| Fast harness, pause/step/save/restore, actual ocean | `harness-performance.json`, measured on the final shared binaries with the hashes recorded in the file, on an otherwise idle machine, 20 samples per operation: `state` 2.795 ms median / 2.865 p95; `pause` 2.825 / 5.308; `save` 2.780 / 2.849; `restore` 7.799 / 7.865; `step 30` 15.424 / 17.324; `capture` 42.295 / 42.977. No operation regressed against the 2026-09-06 baseline; `restore` −21.6 %, `capture` −11.4 % with its tail collapsing from 81.18 ms max to 43.07 ms. Cold starts are reported separately (2.479 s and 2.473 s on the idle machine; 2.698 s for the first launch of a run). A second, **unfavourable** set taken minutes earlier while a foreign `-j8` LTO link was running is retained in the same file rather than discarded. The fixture is the natural ocean at 3072,3160 aboard a wooden skiff — not stamped water and not an arena. | Verified |
| Boat facilities and full native sailing interfaces | Facility, crew, extractor, cargo and deck acceptance all re-run on `4854e303` at 08:11 and **again at 09:36 on the final pair `5e34b81f`**, `ok: true` every time (`/tmp/sailing-fin3/accept/{facility,crew,extractor,cargo,deck}.log`) and images re-reviewed (`activity-results.json` 9 captures, `crew-actions.json` 6 captures, `extractor-grace-results.json` 5 checks, `cargo/results.json` 10 checks, `deck-results.json`); the wider inventory of interfaces and facilities is in `activities.md`. **The one known defect is fixed and proven natively.** The native cargo-privacy dropdown used to move its label while client and server privacy stayed 0, because clientscript 3852 called `~settings_set_dropdown` only when `$int12 == 0` and cargo struct 6372 carries `param1085=1`. The bridge in `src/game/rs_cs2_host.c` identifies exactly that row from script 3852's integer locals (`[0]==2`, `[10]==470`, `[13]==6372`, `[12]!=0`) on a CC_SETTEXT whose `UITree_ApplyText` succeeded, writes varbit 19614 and queues the existing settings mirror; no generic varp-sync path was added. Proven through **real mouse input for all three choices, each driven from a different starting value** (0→2, 2→1, 1→0) with client and server queried separately and agreeing every time, and persisted across a raw logout into a genuinely fresh `--resume-save` process (`privacy/results.json`, `privacy/privacy-choice{0,1,2}-*.png`; save line `4849 = 4194304 = 1 << 22`). Unit gate `test-cs2-sailing-settings` PASS on the final objdir, on both `4854e303` and the final pair (`/tmp/sailing-fin3/test-cs2-sailing-settings.log`). The privacy run itself used `c7e62cce` and was **not** repeated; nothing in either rebuild since touches `rs_cs2_host.c`, script 3852 or varbit 19614. Negative controls are preserved **in the repository**, not only in `/tmp`: `privacy/sailing-privacy-native-before.png` and `privacy/sailing-privacy-native-bug.png`. | Verified |
| Research and visual verification | Research corrections are folded back into `docs/SAILING.md` and `docs/SAILING_PLAN.md` — the per-group budget, oriented vs AABB overlap, radius-60 subtiles, bounded nested calls, the C0.3 rebuild/plane contract, and (2026-09-07) passenger role 3, the private-chat varbit 13674 route, the OPPLAYERT wire identity, IF3 target priority and the IF_SETEVENTS bits 11..16 row gate. Every acceptance run in the final phase opened its images: 18 PNGs by `restamp` (with two collision descriptions **rewritten** where the frame did not match the prose, and one apparent mismatch investigated at 3× and found to be correct), 9 by `client`, 5 + 2 by `multiplayer`, 27 by `lifecycle`, 24 render scenes by `perf`, 6 by `privacy`, and 6 independently re-opened by this worker. | Verified |
| Stale recurring-queue callbacks bound to hull serial | Wired and **run on `214b43d7`**: `torirs_server_world_selftest.c` includes `test/sailing_stale_queues_selftest.u.h` before the lifecycle header, and `sailing_lifecycle_selftest.u.h` calls `selftest_sailing_stale_queues(srv, player, boat, old_serial)` after its reconstructed-cargo assertions. Ten rows PASS in `sailing-selftest-final.tsv`: "%s accepts its persisted callback signature", "stale %s cannot stop replacement work, reschedule or award XP", "current-identity %s executes its real operator stop control" over salvage/trawling/cannon, plus "stale arrival cannot open a replacement hull's customization interface" and "current arrival reaches the actual native customization interface". | Verified |
| Guest crew roster and granted-navigator NPC control | The defect is fixed and proven natively: `social/results.json` (12 of 12 PASS, no failures, no blocked, **re-run 2026-09-07 09:36 on the final pair `5e34b81f` / `6a0e7f04`**, sessions `/tmp/sailing-fin3-social-ocean` and `-berth`, log `/tmp/sailing-fin3/accept/social.log`) check **"guest crew mirror follows the captain's roster, not the guest's own"**, alongside "Edit-navigator grants and revokes navigation by mouse alone" and its server-side control "Edit-navigator grant and revoke reach vessel_stat 11, role 6 and helm permission" (`vessel_stat 11` mask 0→2→0, peer role varbit 19233 3→6→3, `ToriRSServer_VesselCanNavigate` False→True→False), "Board-friend: the guest boards the captain's boat by name", "boarding by name grants no navigation permission", and all four cargo-ACL rows at privacy 0/1/2. This worker opened `social/editnav-armed-on-guest.png` from the final-pair run and confirms it: the *Edit-navigator → Deckhand (level-1)* hover with the guest outlined on deck. The separate `social/editnav-arming-results.json` was **not** re-run and stays on `c7e62cce`, with the difference stated at the top of this file. Two prerequisites for the mouse half landed in `src/app.c` and are recorded there: the TGT_BUTTON arm now reads the **effective** target mask, and all five targeted sends put the **wire** component identity on the wire rather than the runtime tree id. | Verified |
| **LIFE-1** — a logged-out client's auto-reconnect is rejected when the save reconstructs a boat | **Closed on the final pair `5e34b81f` / `6a0e7f04`.** Symptom was `osrs239 login: rejected reply=195` (also 16, 209, 239 — the byte is ISAAC keystream, not a response code; the real set is in `src/net/rev/osrs239/loginblock.h` and is 0/2/3/6/10/15/22/65/69), with the user-visible face "Connection lost — please wait, attempting to reestablish" forever (`lifecycle-chat-after-logout.png`). Root cause, measured with `TORIRSSERVER_VERBOSE=1`: a reconnect's `RECONNECT_OK` is deferred until after the save is read, but reading an **aboard** save *sends* — `ToriRSServer_VesselLogin`'s deck reconstruction writes varbits, and those `VARP_SMALL` packets go out four-plus packets ahead of the login response while the client is still in `OSRS239_AWAIT_REPLY` and reads `h->in[0]` as the verdict (`src/net/loginproto_osrs239.c:425`). Fix, now **in the shared binaries**: `ToriRSServer_WorldMarkVarp` returns early while `player->session->reconnect` is set (`src/torirsserver/torirs_server_world.c:11260`) — nothing is lost, because step 4b of `WorldLoginFinish` already restates every non-zero transmitted varp. **Proof on the final pair:** five consecutive aboard `raw logout` cycles in session `/tmp/sailing-fin3-life`, whose `client.log` carries five `torirsserver: reconnect user='fin3life' session=ok` lines, **zero** `login: rejected` and **zero** `connection lost` (grep counts 5 / 0 / 0). Capture `lifecycle-reconnect-fixed.png` was re-taken on the final pair. The prior 5/5-rejected control on `4854e303` against 5/5-clean on the private client `99890ea1` is preserved in `lifecycle-reconnect-results.json`, alongside the final-pair result. Sailing suite 563/0 on `6a0e7f04`. **Two sub-items remain, recorded not hidden:** the general guard arguably belongs one line into `ToriRSServer_Send` (`torirs_server_encode.c`) so no future login-time writer can reopen the hole, and a varp written back to **0** inside the reconnect window would be held and then skipped by step 4b's `if (value == 0) continue;` — harmless in practice and never observed. | Verified |
| **SRV-1** — `ToriRSServer_SceneOpNearestOpts` never wrote `out->unbounded` | **Closed on the final pair.** `ToriRSServer_SceneOpNearestOpts` (`src/torirsserver/torirs_server_scene.c:2435`) now writes `out->unbounded = 0` at line 2446, under a comment saying why op clicks never take the whole-flood last resort. The two production callers that handed it a bare uninitialised stack struct — `torirs_server_scene.c:2595` (`ToriRSServer_SceneRouteOp`, op-click routing) and `torirs_server_world.c:3993` (NPC routing) — therefore no longer feed indeterminate memory to `collision_map_route_tiles`'s unconditional read at `collision_map.c:1130`. The other half, `collision_nearest_opts_from_model` writing the field in every arm, landed earlier (`collision_map.c:975/989/1000`). `test-world` exits 0 on the final pair (`/tmp/sailing-fin3/test-world.log`, `All tests passed.`, `OPT=1`). **Stated plainly: no dedicated gate exercises the two server callers.** `test-world` covers `collision_nearest_opts_from_model` only; the server-side fix is verified by source inspection plus the whole 563/0 sailing suite passing on the rebuilt server, not by a test that would fail if it were reverted. It was pre-existing and never a sailing regression. | Verified |
| **GPU-1/2/3** — three `gl3-zbuffer` findings | **Open, none of them a sailing regression.** GPU-1: chat-filter button captions ride **5 px low** on the GPU lane and collide with the green state line under them — name rows gl3 486–493 against soft3d 481–488 both headless *and* windowed, the plates themselves not moving (dy-correlation peaks at 0), the `Report` caption unaffected, sidebar and chat text pixel-identical (`gpu/gl3-chatfilter-caption-shift-x6.png`). GPU-2: at the deck camera 256/256/1000 the gl3 lane draws the hull's plank/gunwale band **over** the standing player, leaving a sliver of tunic and one arm, while soft3d draws the whole figure; `actor_commands` is 1 in both, so it is a depth/ordering fault, not a culled command (`gpu/gl3-deck-figure-occluded-x6.png`). Proven **pre-existing** by the same occlusion in `gpu/historical/deck-baseline.png` from client `54a09b3f` (`gpu/gl3-deck-figure-occluded-prior-x5.png`) — this worker re-opened that image and confirms the deck carries no player figure at all. GPU-3: the gl3 process RSS is ~15× soft3d and grows at scene transitions rather than per capture — 537 MiB → 1.84 GiB → 2.27 GiB across the script, 2969 MiB after the run against soft3d's 199 MiB; recorded as an observation with its measurement, not as an assertion. What is **not** a defect and was checked: the floating grey water wedge (an animated element whose phase differs between processes — both renderers draw it after `hover 302 197`), and `world_order_mode` 1 vs 0 (the z-buffer lane selects its own world ordering; `wev` counts and bounds are identical). CLOSES ON: work on the `gl3-zbuffer` lane, outside the sailing scope. | **Open** |
| **HARN-1** — `::vesselspawnat` does not pick a legal berth | **Open, harness fixture defect, and the reason Checkpoint D had to be re-walked through content.** `ToriRSServer_VesselRecover` requires a footprint clear at **all 16 headings**; `::vesselspawnat` applies no such check, so the harness's `--fixture-tile 3073 2987` spawn grounds on its *second* turn: native SET_HEADING took heading 0→3 fine, then 3→8 printed "The boat runs aground", HP 80→78 and the angle stalled at 256 (`lifecycle-cheatberth-aground.png`, a **labelled control** taken today; the 2026-09-06 `lifecycle-aground-control.png` was left untouched). Materialising the hull through the content selector instead — gangplank `oploc1` → `~sailing_select_owned(3, dock, coord)` → native 934 → Board — puts it at the recovery berth 3073,2984, reproduced identically by two independent processes, where four headings turn in place with no grounding. Consequence for anyone writing a harness script: **anything proving turning at a dock must use the content selector, not the fixture cheat.** CLOSES ON: making the cheat share the 16-heading clearance check. Recorded in `docs/sailing_harness.md`. | **Open** |
| **EMB-1** — `test-torirsserver-embed` decode break | **Open, long-standing, outside the sailing lane.** The target failed in the 25-target run on `c7e62cce` with `embed: SYNTH_SOUND reached alice's stream (packetin.h:102 is no longer PKT_NAME_NONE)`, and it was **not** re-run on `4854e303` (ten targets) nor on the final pair `5e34b81f` (twelve targets), so its state on the final binaries is neither confirmed nor cleared. It is the known `embed_test` decode break, not a sailing assertion; `embed_test.c:2259` (`saw_filter_private`) sits behind it, which is why the chat re-proof was done natively instead. The other twelve unrun targets from the 25-target table are likewise unverified on the final binaries. CLOSES ON: fixing the embed decode, or re-running the target to record its current state. | **Open** |

## Notes on scope and on what does not count

Optional baked-deck optimization and full bone-matrix pitch/roll were explicitly
deferred by the plan. The native flatten path is required regardless of any
optional bake, and it is now the *only* flatten path — the merged bake was
removed rather than kept behind a flag. The vertical bob **is** implemented
(bone-0 Y translation into the descent transform); pitch and roll from the full
4×4 need a wider descent transform and remain deferred.

The budget correction follows direct inspection of revision-239
`Statics.method2832`; it does not remove the separate overlap gate.

A failed command due to a mistyped make target is not a passing gate:
`test-scanline-parity` does not exist; its correct replacement
`make -C src EMBED_SERVER=1 test-scanline` completed successfully.

`activities.md` documents the wider world-content boundaries that remain
(encounter producers, rare tables, the crystal-water reward consumer, the Kraken
encounter perk). Those boundaries are not the same claim as "every requested
facility is proven", and must not be reported as such.

**Elapsed times from the acceptance phase are not performance evidence.**
Several workers drove clients and builds on this machine concurrently for its
whole duration. The only performance numbers in this ledger are the ones in
`harness-performance.json` and `render-final-results.json`, both measured on a
verified-idle machine with the process list recorded.

## What a reader should do next

1. **Nothing is owed before committing.** The rebuild the 09:30 close asked for
   happened at 09:35; LIFE-1 and SRV-1 are fixed, in the shared binaries, and
   re-proven on them.
2. Decide whether LIFE-1's guard should also move to `ToriRSServer_Send`, so no
   future login-time writer can reopen the same hole. The current guard covers
   the only emitter that was ever measured in that window.
3. Add a gate that exercises the two `ToriRSServer_SceneOpNearestOpts` callers,
   so SRV-1 cannot silently return.
4. **C0.3**, **GPU-1/2/3**, **HARN-1** and **EMB-1** are each a bounded,
   independent piece of work that no sailing requirement waits on.
5. The four deliberately-deferred review items — **M-3** (duplicated shore
   constants), **M-6** (the embed private-mode assertion, blocked by EMB-1),
   **L-3** (the `!vm` guard pattern) and **L-7** (`ContentSymbol` zero
   fallbacks) — are recorded above with the reason each was left.
