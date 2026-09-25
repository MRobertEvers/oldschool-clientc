# Canoe animation acceptance test

The canoe test now checks the full visible journey: axe swing, falling tree,
carving, launch, boarding, river/cave rowing, and sinking at the bank. It uses
the real client, real packets, real cache models, and the renderer's posed mesh.

## Run

Build the client and a current script pack once, then start a private session:

```sh
make -C src content-test-client
make -C src torirsserver-scripts
.venv/bin/python tools/content_selftest.py --session build/canoe-check start
.venv/bin/python tools/canoe_animation_test.py --session build/canoe-check --boats 1
```

The Python environment needs Pillow. The client uses `cache.osrs239` and an
isolated save directory. Repeat the final command without restarting anything.
The simulation uses virtual time; screenshots and film export add wall-clock cost.

```sh
# All four complete construction/travel paths
.venv/bin/python tools/canoe_animation_test.py --session build/canoe-check
# River and cave staging, without rebuilding a canoe
.venv/bin/python tools/canoe_animation_test.py --session build/canoe-check --boats 4 --cutscenes
# Cave plus arrival coverage
.venv/bin/python tools/canoe_animation_test.py --session build/canoe-check --caves
# Four hulls at every arrival point (44 cases)
.venv/bin/python tools/canoe_animation_test.py --session build/canoe-check --destinations all
# Full animation films, instead of representative PNGs
.venv/bin/python tools/canoe_animation_test.py --session build/canoe-check --boats 1 --film
```

`--film` writes a PNG for each changed pose/fade step and an animated GIF per
phase. The normal run still renders and examines **every client cycle**, but
saves representative images. Both write contact sheets and JSON traces under
the session's `animations` directory. Named construction/cutscenes/caves/arrivals reports
preserve each suite's result; `report.json` and `trace.json` describe the latest
run. A failed run writes `ok: false`, rather than leaving a previous pass.

## What is asserted

| Action | Cache sequence | Frames |
|---|---:|---:|
| Rune axe swing | 867 | 6 |
| Tree fall / canoe roll | 3304 | 17 |
| Rune axe carving | 3285 | 24 |
| Player push | 3301 | 16 |
| Boarding | 3303 | 13 |
| Rowing | 3302 | 23 |
| Hull bobbing | 3306 | 11 |
| Sinking | 3305 | 24 |

The gate requires every expected frame to be observed while the scene is
revealed. It checks non-looping poses against the renderer's applied pose,
requires real vertices and changing mesh hashes, and checks complete actor/sink
durations. The one-shot tree/launch locs hand off at cycle 90, after their final
frame is reached at 86 and before the cache resets them to their bind pose at 93.
The gate explicitly rejects a height snap-back and checks the final static form. Looping hulls can reuse equivalent bobbing poses.

The player and canoe roll must start together. Cutscene expectations now come
from `tools/testdata/canoes/cutscene-reference.json`, independently of the
RuneScript implementation. The reference is RS Mod's `CanoeTravelling.kt` at
commit `c38dff049fe1d1f4f68ec03737602849938c1399`. Its adaptation carries the
ISC notice in `canoes/LICENSE.rsmod`.

The former tests accepted a wrong shot: they required north-facing actors,
accepted any NPC displacement, and ignored the camera and cutscene zoom.
The reference instead seats the actor at (1817,4515) or (1845,4492), facing
west (yaw 512). Both cameras look east from the west bank. Seven river props
and five cave props enter at specific ticks and move north. Their lifetimes,
lanes, per-tick server movement, per-frame rendered movement, instance identity,
rigid meshes, and constant orientation are checked. The camera's requested
position/target and actual rendered position/yaw, cutscene layout, zoom mode,
and restoration are also checked. The hull's vertex count is checked against
the cache model file, independently of its varbit.

This is an independent implementation reference, **not recorded original-game
video**. The trace proves these specified behaviors and renderer transforms;
films still need visual comparison when assessing exact OSRS presentation.
The small screenshot regression regions include the paddler without masking
them out; their origin is explicitly labelled as local renderer baselines.
A pass must not be described as universal visual correctness.

A sink must use the correct hull, reach a fully submerged final mesh (all local
Y coordinates below the water surface), and disappear. Every arrival tests the
real `canoe_arrive` path through a queued debug entry point; the gameplay route
matrix separately enforces which journeys are legal. Rendering a log in the
cave is an asset fixture, not permission to reach the Wilderness in a log.

The fades must interpolate, and the cutscene set must remain covered until its
camera is installed. Repeated trips must first visit the seat before arrival
can satisfy the stop predicate—starting at the destination cannot pass a ride.

## Fixes found by the audit

- A first chop could succeed before its axe animation was ever published.
- Carving, pushing, and boarding were cleared before their last frames played.
- The log began rolling before the player's push pose reached the client.
- Waiting beyond the last loc pose briefly restored the upright/dry bind pose.
- The camera, seat, facing, flow direction, and scenery schedule disagreed with the independent canoe reference.
- Scenery inherited wandering AI and reversed toward its spawn radius.
- Cutscene zoom/layout were never enabled; camera offsets compensated for panels that should have been hidden.
- The destination had a sink sound/message but no animated sinking canoe.
- A near-tick-boundary click could teleport before the departure fade completed.
- The departure station could regrow its tree visibly before the screen faded.
- The arrival text used a literal pipe instead of a line break.

The waits now include publication timing and complete cache sequences. The
player turns while covered. Arrival places the appropriate sinking model on
surveyed water tiles, holds it during fade-in, then runs all 128 sink cycles
before removing the temporary loc. The cutscene now uses its dedicated layout and zoom rather than offsetting the camera around the gameplay sidebar.

## Mechanics of the test driver

`observe N X Z VARBIT COMPONENT` advances N 20 ms client cycles, renders the
settled frame, and returns client/server varbit values, player state, the locs
at X/Z, the requested widget, and posed-mesh hashes. An optional final
comma-separated list of NPC type names adds their server/client/render states
to the same observation; every prop is checked without additional mailbox trips. This avoids four or more
filesystem round trips for each animation frame. A read of an animation counter
alone is not considered rendering evidence.

Host button/close/resume requests schedule the normal CS2 settlement pass.
When a paused test receives immediate UI packets, the harness closes their
transaction with the normal `SERVER_TICK_END` packet. It does not advance the
world or fabricate player/NPC updates to force an acknowledgement.

The capture fixture gives the account high combat stats, moves away long
enough to clear old scenery/combat effects, and uses a repeatable camera.
Optional tile-indicator plugins can be disabled in that session's plugin prefs
for unobstructed films; normal player preferences are not changed.

## Reproducing the development audit

Other in-progress content edits made whole-pack builds intermittently invalid.
For this audit, supporting content was frozen at the OSRS-Content HEAD recorded
in `build/canoe-head-support/snapshot.json`, with the **current canoe subtree
copied verbatim** over it. `build/canoe-frozen/content` is that input tree.
The compiler and server both use it; the client reads the normal revision-239
cache. This isolates unrelated edits without substituting old canoe code.

The controller supports alternate roots:

```sh
.venv/bin/python tools/content_selftest.py --session build/canoe-frozen compiler \
  --content-root build/canoe-frozen/content
.venv/bin/python tools/content_selftest.py --session build/canoe-frozen start \
  --content-root build/canoe-frozen/content --scripts build/canoe-frozen/scripts
```

Script-body edits can be recompiled and reloaded through `recompile`; changed
signatures/configuration need a full compiler restart. A live server must not
reuse old numeric timers across a pack whose script IDs moved—start a fresh
session for structural changes.

The current visual fixture is native Soft3D at 807×503. The tests cover the canoe phases described here. Other renderers, viewports,
quests, and character outfits need their own coverage.

## Checking that the gate catches regressions

`tools/canoe_cutscene_mutation_test.py` temporarily changes a script **inside
a private session copy**, recompiles it through the warm compiler, and verifies
that wrong camera, facing, scenery direction, and spawn timing each fail the
corresponding assertion. It restores and reloads the original in a `finally`
block. It refuses source paths outside the session directory.

```sh
.venv/bin/python tools/canoe_cutscene_mutation_test.py \
  --session build/canoe-frozen \
  --source build/canoe-frozen/content/server/scripts/canoes/scripts/canoe_cutscene.rs2
```

The minimap-orbs plugin has a separate hide/restore regression in
`make -C src OPT=1 test-minimap-orbs-v2`. The real-client trace also verifies
that no plugin orb remains visible during the revealed cutscene.

## Latest development measurements

With the warm embedded client on this Mac, the click-through canoe pilot took
5.49 seconds. The eight river/cave cases (all four hulls), observing every
client frame, took 28.29 seconds. Four injected staging regressions were all
rejected in 18.94 seconds; each body compile/reload took about 0.10–0.22 seconds.
These timings exclude initial cache loading and compilation. Film export is
optional and substantially slower than the normal checks.
