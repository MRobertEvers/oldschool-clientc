The final software-rendered run passed in 1.525 seconds of warm execution, with all nine images visually reviewed. The real peer walked to deck-local fine 448,576 in view 2, was omitted while that boat flattened, and returned when full detail was restored. The maximum real-cache test populated 19,977 static elements across the root and 15 decks without pool corruption.

These fixtures exercise the native client while the player stands aboard a skiff in the actual ocean at 3072,3160. `tools/sailing_client_acceptance.py` uses one warm process and records every mailbox command, native painter count, loaded executable/script hash and renderer capture in `client-results.json`. Each final image must be visually reviewed after a run.

The three-boat fixture checks the native priority groups and independent group draw limits. The aboard boat stays full at limit zero; excess boats contribute no painter markers. Overlap changes a group 1 boat into a live flat silhouette, with its mast, helm and cargo retained and actor/pick population omitted. Returning the limit to 30 restores full geometry on the next frame. The coast case actually sails into the island boundary, verifies that the boat collision map stops the hull, captures the raised shore, and restores the ocean checkpoint.

The reference rules are revision 239 `Statics.method2832`/`method1003` (group budgets and overlap), `method1449` (radius 60 pseudo-loc insertion), `class467.method10419` (0.01 Y scale, pre-scale offset −1200 and flat HSL), and `Statics.method11128` (7900/7901 limit setter/getter, default 30). Boat-to-boat overlap uses exact rotated fine-coordinate AABBs; actor overlap uses the nearest of 16 oriented footprints.

The software painter needs two explicit dependencies to reproduce the scene layering: parent terrain under the transformed deck precedes the boat, and overlapping full boats composite over flattened siblings. The App applies these to the completed command stream; native radius 60 insertion still determines the boat's parent loc placement. Parent actors/locs retain their relative order, nested markers stay balanced, and terrain with any corner above the parent hull surface stays in its original position. The zero-boat path leaves the command stream byte-identical. Radius 60 covers one or two tiles per axis depending on the fine position.

Direct gates (what each one covers; the final-run PASS/FAIL ledger with per-gate source hashes is `../unit-gates.md` and `gates.json`):

- `test-wev-visibility`: fine AABB versus actor OBB, nearest 16 headings, per-group budgets including zero and aboard exemption, per-frame reset, radius 60 subtiles, and camera ±500 snap versus 1/16 easing.
- `test-painters-world-entity`: same-ID cycles, distinct IDs aliasing one active Painter, all 16 active contexts, refused descent at capacity, balanced markers and no duplicate models.
- `test-frame-flat`: real frame emission, exact 0.01 vertical scale within integer geometry precision, HSL/texture override, source geometry preservation, live next-frame changes, ground/full model handles, no-pick, root identity and shared replay lifetime.
- `test-wev-population`: real own/borrowed player/NPC/graphics registration suppressed for flat views while runtime scenery remains; population returns on the next full frame.
- `test-sailing-paint-order`: ground and flat/full dependencies, nested ownership, raised shore and upper-plane exclusions, unchanged parent loc order, stable replay and zero-boat byte equality.
- `test-pick-level`: modes 0–3, aboard contents-only override, blocker depth order, flat/skipped no-pick, and view-local terrain coordinates. Terrain hover remains separate from the model hash override, matching `class121.method4380`/`method4389` versus `method4378`.
- `test-minimenu-world`: five-bit hull operation mask, nearest-hull deduplication and contents-only picks that cannot invent hull operations.
- `test-cs2-worldentity-limit`: real VM/host initialization, 7900/7901 round trips, default 30, zero and negative clamp.
- `test-cs2-array-fill`: native 8010 integer/string/null filling, partial/clamped ranges and balanced stacks. Sailing's facilities scripts require actual −1 initialization, which the previous metadata stub consumed without performing.
- `test-wev-rebuild`: actual cache root scene plus all 15 deck worlds, distinct populated pools, actor migration through every deck and isolated reverse despawn.

### Final-phase gate run — 2026-09-06

Twenty-five make targets were run against the **2026-09-06 21:38 pair**
(`src/torirs` `c7e62cce…`, `src/build_opt/torirsserver` `a53899eb…`,
`script.dat` `902a6b8d…`), one at a time, as
`make -C src EMBED_SERVER=1 <target>` into `src/build_opt_es`. Every requested
target exists in `src/makefile`; none is missing. **23 PASS, 2 FAIL.**
`test-minimenu-world` and `test-wev-rebuild` were rerun at `OPT=0` with asserts
live and both PASS.

The full table, the per-gate test/production source hashes and the failure
analysis are in **`../unit-gates.md`** (machine-readable: **`gates.json`**,
`unit_gates` and `failures`). Logs: `/tmp/sailing-final-gates/<target>.log`.

The ten gates listed above are all PASS. The two failures are outside that list:

- `test-world` — three route failures (`world_test_route.c:131/186/217`).
  `collision_nearest_opts_from_model` in
  `src/engine/world_builder/collision_map.c` never writes `out->unbounded`, so a
  caller with an uninitialised `struct CollisionNearestOpts` reads garbage and a
  click that should decline routes anyway. Pre-existing at HEAD, not a sailing
  regression, and it reaches production through `src/world/entity_pathing.c:280`
  and `ToriRSServer_SceneOpNearestOpts`. Owner: root; no C source was edited by
  this worker.
- `test-torirsserver-embed` — the long-standing decode break, first failure
  `embed: SYNTH_SOUND reached alice's stream (packetin.h:102 is no longer
  PKT_NAME_NONE)`. **Unchanged.**

Durations were recorded but are not performance evidence: other workers were
driving clients and servers on the same machine throughout.

Run after the coordinated native build:

```sh
python3 tools/sailing_harness.py --session /tmp/sailing-client-overlap start --headless --boat skiff
python3 tools/sailing_client_acceptance.py --session /tmp/sailing-client-overlap
python3 tools/sailing_harness.py --session /tmp/sailing-client-overlap stop
```

## Final-binary re-run and visual review — 2026-09-06, worker `client`

> **Provenance corrected 2026-09-07.** This section's run used the **2026-09-06
> 21:38 pair** `c7e62cce` / `a53899eb`, not the final one. The client acceptance
> was re-run twice since: on `4854e303` at 08:11, and on the **final pair
> `5e34b81f` / `6a0e7f04`** at 09:36 (session
> `/tmp/sailing-fin3-client-overlap`, `ok: true`, nine captures,
> `/tmp/sailing-fin3/accept/client.log`). `client-results.json` carries the
> final pair's hashes and is the current record. The image descriptions below
> were written against the 21:38 captures and were re-checked on `4854e303`;
> they still describe what the frames show.

Hashes for **this section's** run, verified on disk before it and recorded in
the then-current `client-results.json`:

| Artifact | SHA256 | Which pair |
|---|---|---|
| `src/torirs` | `c7e62cce180aaeb6bc4cd044818b0209ca99edb478f5280c3ebd10d6b79b1d8d` | 2026-09-06 21:38 |
| `src/build_opt/torirsserver` | `a53899eb8ce5d086e36df3653aaccfd2ee93c05f8e767ece05ecc40a94b9c3db` | 2026-09-06 21:38 |
| `script.dat` (30,076 scripts) | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` | unchanged throughout |

The final pair differs from `c7e62cce` / `a53899eb` by two selftest fixtures,
the `collision_map` and `ToriRSServer_SceneOpNearestOpts` `unbounded`
initialisers, the LIFE-1 reconnect guard, one render-path change that removes
two per-frame allocations from the sailing paint-order pass, and a set of
asserts and test controls. `plan-audit.md` lists all of them.

Session `/tmp/sailing-final-client-overlap`, PID 93659, `soft3d`, headless,
`--boat skiff`, `allow_stale_scripts: false`. **PASS: 65 commands, nine
captures, 1489.707 ms**, max capture 40.539 ms. The session was stopped.
Elapsed times in this phase are **not** performance evidence — several workers
ran clients concurrently; a later phase measures alone.

All nine PNGs were opened and read. The `wev` counters recorded with each frame
are quoted beside what the frame actually shows.

| Image | What is visible | `wev` (id: visible/flat, model/actor cmds) |
|---|---|---|
| `client-three-overlap.png` | Two full skiffs side by side on open ocean — the aboard hull on the right carrying the cyan helm square and the player, vessel 2 axis-aligned on the left. Vessel 3 raises **no hull**: only a flat darker shape lies on the water behind them. Chat confirms vessel 2 at 3077,3160 view 2 and vessel 3 at 3078,3161 view 3. | 1: vis/full 7/1 · 2: vis/full 6/0 · 3: vis/**flat** 6/0, picked 0 |
| `client-priority-two.png` | Ownership flips: the **rotated** hull (vessel 3, angle 256) is now drawn in full on the left with its mast and boom across the view, and vessel 2 has become the flat dark hull-shaped patch on the water behind it. Same camera, same tick. | 1: vis/full · 2: **flat** · 3: vis/full |
| `client-budget-one.png` | Aboard hull plus exactly one other full hull (vessel 2). Vessel 3 is **entirely gone** — no hull and no flat patch. | 3: not visible, markers 0, model cmds 0 |
| `client-budget-zero.png` | Only the player's own skiff remains, mast, sail, red pennant and cyan helm square intact. Both neighbours are gone. Aboard exemption at limit 0. | 1: vis/full 7/1 · 2,3: not visible, 0 markers |
| `client-full-restored.png` | Three full hulls again: aboard right, vessel 2 left, vessel 3's rotated mast, pennant and bowsprit overlapping behind them. Restored on the next frame at the native default. | all three vis/full, model cmds 7/6/6 |
| `client-peer-visible.png` | A **second human figure** — green tunic, standing — is on vessel 2's planking. The real server peer `HarnessMate`, decoded onto the borrowed deck. | 2: vis/full, **actor cmds 1** |
| `client-peer-flat.png` | Vessel 3 is now full and vessel 2 has flattened: the peer figure has **disappeared with the geometry**, leaving the dark flat hull patch behind the rotated hull. `peer` still reports the record present. | 2: **flat**, actor cmds 0 |
| `client-peer-restored.png` | The same peer figure is back on vessel 2's deck in the same pose and place as `client-peer-visible.png`, immediately, with no residue. Vessel 3 is the flattened one this time. | 2: vis/full, **actor cmds 1** · 3: flat |
| `client-raised-coast.png` | The skiff has actually sailed into the island. The bow is stopped hard against a raised coast — sandy cliff face, grass on top, a figure standing on the headland — the hull HP bar reads **78/80**, and the chat reads "Vessel 1 sailing heading 15 at tier 3." / "The boat runs aground." The raised shoreline composites correctly around the hull and is *not* painted over the nearer boat. `restore` then returned HP 80. | 1 only (limit 0): vis/full 7/1 |

Not re-run by this worker and untouched: `frame-assert-*` and `world-drain-*`
files, and `gates.json`.
