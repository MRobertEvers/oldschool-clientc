# Sailing collision design

Implemented in `torirs_server_scene.c` and `torirs_server_vessel.c`. The old
`water_stamp` field and restamp operation are removed; the integrated sailing
selftest starts at the real ocean fixture below.

The vessel mover needs its own collision map. A player's blocked floor is not
a water classifier: cliffs and lava have the same flag, while the revision-239
open ocean often has no player floor flag at all.

## Ground truth and ocean fixture

The unpacked revision-239 cache supplies the terrain evidence:

- `OSRS-Content/osrs239-content/maps/m48_49.jm2`, tile `0 0 24`, places
  `(3072,3160)` on `h1 o445;0;0 u48`, a full ocean overlay with no BLOCK setting.
- All 289 tiles in the surrounding 17 by 17 square have full ocean overlays.
  There are no level-zero loc origins within 12 tiles; the nearest land or
  partial shoreline tile is 14 tiles away.
- `configs/all.overlay` defines the sea families at config IDs 441 through 624
  (map overlay IDs 442 through 625, because terrain IDs are one-based). The
  ordinary ocean families use texture 1 and textures 130 through 189. The final
  deep-water record uses texture 208. Transparent companions in each three-row
  family do not establish a water surface by themselves.
- `docs/SAILING.md` documents boat versus terrain collision and intentional
  absence of boat versus boat collision. The local revision-239 deob's
  `class100` and `class177` expose player collision; they do not expose a
  server boat-navigation grid. No second client map is being claimed here.

The ocean classification is therefore an explicit, revision-bound interpretation
of cache terrain geometry. It is not inferred from a renderer's screenshot,
an arbitrary rectangle, player collision, or the unread trailing terrain bytes.

## Separate domains

Each scene window owns four player collision maps and four boat collision maps.
The boat maps start blocked and admit verified full ocean tiles; partial
shoreline overlays remain blocked. Missing terrain and out-of-window queries
remain blocked. Above-ground empty planes never become ocean.

The terrain pass also blocks walking on full sea tiles. Bridge level handling
must preserve a walkable raised pier while retaining the sea beneath it for
the boat domain. Locs stamp their wall and footprint blockers into each map
independently. Runtime loc removal restores overlapping loc and terrain flags
in both maps. Player and NPC occupancy changes affect only player maps.

`SceneBoatCollision(level)` exposes the bound window's boat map for diagnostics
and focused fixtures. `SceneBoatTileFlags(level,x,z)` resolves absolute queries
through any coherent covering window. `VesselCanOccupy(vessel,x,z,angle)` checks
the complete rotated footprint and is the spawn and debug validation seam.

## Native hull geometry

The server loads archive 72 with the same `WevConfig_Decode` and
`WevConfigTable` implementation linked into the client. Collision uses decoded
bounds widths/heights and signed offsets; deck reservation dimensions cannot
shrink or enlarge an ordinary hull. Config 2 remains a 2 by 5 skiff even with
an 8 by 8 deck reservation. Config 3 and 12–14 use a 3 by 10 box offset by
`(0,-256)` fine units, and that offset rotates with the hull. Config 9 carries
zero native bounds and retains the declared-extent fallback. Pivot and deck
plane also come from this shared native decoder instead of mirrored tables.

## Movement transaction

Derive a proposed position and angle without mutating the live vessel. Check
the entire rotated hull and the swept area between the two poses, including
stationary turns with sails down and the final target-arrival step. Commit
position and angle together only after that check succeeds. A refusal preserves
the previous pose, clears movement carry, parks the vessel, and publishes the
existing blocked notice.

Footprint testing must intersect actual hull geometry against blocked tiles;
inset point sampling misses tiles under a hull's corners and narrow edge
overlaps. Translation must cover the path between endpoints, because a fast
one-tile hull can otherwise skip an obstacle. Rotation must cover the arc,
because two clear end poses can sweep through the shore between them.

## Focused acceptance

Verify real ocean permits a hull and blocks a walking player; ordinary ground,
blocked inland ground, partial shore, empty planes, and unknown map reject it.
Verify player occupancy cannot block a hull. Add and remove a real cache loc on
water and verify both maps retain the correct terrain afterward.

Exercise a hull edge overlap missed by the old inset samples, a fast move over
an intervening blocked tile, the same case in the target-arrival branch, a
rotation with sails down into shore, a clear endpoint rotation whose arc crosses
a blocker, an unobstructed turn, and a long sail across a scene-window rebuild.
Use the persistent visual harness to inspect the ocean, hull pose, visible
shoreline, and stopped state after collision; test-state JSON alone is not the
visual evidence.

Automated checks run on 2026-09-06:

- `make -C src test-sailing-collision`: passed all real-ocean, separate-map,
  partial-shore, wall, loc overlap, swept-movement, turn and rebuild checks.
- `make -C src test-collision-doors`: passed 25 scenes, 531 doors and 3,044,276
  legal diagonal steps, with zero changed collision maps after door roundtrips.
- `src/build_opt/walkable_probe cache.osrs239 3072 3160 0 8`: before the
  change, every surveyed ocean tile wrongly reported walkable (`flags=0`);
  afterward the ocean blocks players (`flags=0x200000`).
