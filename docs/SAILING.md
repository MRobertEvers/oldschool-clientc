# Sailing — research notes

Research for implementing OSRS Sailing (world-entity boats) in this client and
its embedded server. Reference deob: osrs239 (`src_osrs239_rl1_12_33`). Sailing
shipped in the real game on 19 November 2025, so revision 239 contains the
released implementation, not a beta.

## 1. What a world entity is

A **world entity** is a portion of the map that is projected onto another
location, typically the ocean. The only current uses are boats (player and NPC
owned) and trawling shoals.

The key illusion: boats do not actually move through map geometry. Each boat is
a **stationary rectangle of real map, far off to the north-east of the main
map, oriented with the bow facing south**, and the client *projects* that
rectangle onto the sea at the boat's current position and heading. Players and
NPCs standing on the boat live in the boat's own coordinate space and move
around it with ordinary tile movement; the boat itself translates and rotates
under them.

Jagex described it as a genuinely new entity class in the engine: alongside
"player" entities and "NPC" entities, Sailing added a **"world" entity, which
allows rendering players inside boats, interacting with them, while the boat
moves independently**. On the rendering side they also added a z-buffer to the
(C++) client and increased draw distance on the legacy Java client.

## 2. Movement model (server-driven)

All movement is server-side; the client only interpolates what it is told.

- **Position** is in *fine coordinates*: 128 units per tile. Boats travel an
  integer multiple of **0.25 tiles (32 fine units) per tick**, i.e. movement
  snaps to quarter-tiles.
- **Heading** is one of **16 compass directions**, expressed in the standard
  2048-unit angle space (each of the 16 headings is a multiple of 128 units).
- **Speed** tiers supported by the engine: 0.5, 1, 1.5, 2 tiles/tick.
  Announced ship classes: small ~1.5 t/t base with 2 ticks per 90° turn; large
  1 t/t, 4 ticks per 90°; colossal 1 t/t, 6 ticks per 90°.
- **Turning is arc-like**: the boat does not snap to the requested heading; it
  turns at a capped angular rate per tick (turn rate depends on hull class and
  wind), so a course change traces an arc. The player clicks a direction (a
  white arrow appears around the boat toward the cursor); the boat then moves
  *perpetually* in that heading until stopped or steered again.
- **Control scheme**: one player takes the helm ("navigate" on the wheel locks
  the character in place); their clicks steer the boat instead of moving the
  character. Other players on deck walk around normally. Speed is set from an
  interface (with reverse); sails must be set to move; trimming sails on wind
  gusts gives temporary speed boosts. Winds affect speed dynamically and are
  identical for all nearby players; currents are static directional fields at
  fixed map locations.
- **Collision**: there is deliberately no functional boat-vs-boat collision
  ("visual collision" only) — overlapping ships render as shadow/ghost
  versions passing beneath. Exception: charter ships block movement instead of
  ghosting. Boat-vs-terrain collision exists (a boat moves until it collides
  with something or is stopped).

## 3. Rendering rules observed in the live game

- Each boat is its own little world (own scene, own tile grid, own locs), drawn
  inside the main scene at the boat's interpolated position/heading.
- **Stacked/overlapping world entities**: only one (the topmost/active one)
  renders in full detail; the others degrade — the live game shows them as
  "shadows passing unimpeded beneath". Contrary to the common assumption that
  these are baked billboard models, the deob shows the flattened render is a
  degenerate re-draw of the same sub-scene (Y-scale 0.01, flat colour, actors
  skipped) — see §5.3.
- The camera can target the boat (not just the player) while navigating;
  crow's-nest lookout re-centers the camera without affecting the ship.

## 4. Server model

- A boat's deck is a **small instance** (dynamic map) built from cache
  template zones. Sizes are measured in **zones (8×8 tiles)** — the raft is a
  single zone; deck layouts include the helm/sail locs at fixed deck
  coordinates. Published hull footprints: raft 1×3 tiles of deck, skiff 2×5,
  sloop 3×10 (deck sizes; the instance around them is zone-granular).
- Boarding = teleporting the player into the deck instance; the engine then
  maps deck coordinates to root-world coordinates through the entity's current
  position for everything that needs a world position (visibility, player
  info, interactions with the shore or with other boats).
- The reference protocol implementation (rsprot, used by ClockworkRS's design)
  models it as a **WorldEntityAvatar**: index (1–4095), fine-coord position,
  angle 0–2047, size in zones, level count; per tick a mover writes fine
  coords + angle, and the protocol layer encodes smooth motion vs teleport.
  Deck map data is sent with a *rebuild world entity* packet (absolute zone
  coords + per-zone template references, like an instance rebuild), and
  subsequent zone updates for the deck are wrapped in a **SET_ACTIVE_WORLD**
  prefix so the client applies them to the right sub-world. Camera targeting
  of an entity uses a cam-target packet carrying the world-entity index.
- Direction math used by that design: heading is one of 16 compass points
  scaled ×128 into 0–2047; per tick `dx = -sin(θ)`, `dz = -cos(θ)` in fine
  units, angular velocity capped (e.g. 128 units/tick), linear acceleration
  ramped (e.g. 64 fine units/tick²).
- Ownership/spawn state persists as varbits; the entity materializes at the
  dock on login and despawns on disembark.

## 5. Deob findings (osrs239)

From `C:\Users\mrobe\Documents\git_repos\Deob\src_osrs239_rl1_12_33\deob\`.
Name map (obfuscated → meaning): `class467` = WorldEntity, `class100` =
WorldView, `class112` = Scene (and itself a Renderable), `class387` =
WorldEntityConfig, `class61` = the world-view manager, `class166` = a transform
{x, y, z, angle}, `class458` = the interpolator, `class276` = draw-priority
group, `class291` = click-mode enum, `class575`/`class556` = oriented footprint
boxes, `class149` = HSL colour override.

Two decompiler landmines the port must not copy: `class467.method10489` and
`method10492` have wrong field mappings (use the `Statics.method10487` /
`Statics.method10410` twins), and `class166`'s x/z accessors are partially
aliased (use `method5537` for x, `Statics.method5566` for z, `method5540` for
angle).

### 5.1 WorldEntity data structures

- **`class467` (WorldEntity)** owns: its id (= its world-view id), its own
  `class100` WorldView, its `class387` config, the **current interpolated
  transform** `field5692` (x, y/height, z world units; angle 0–2047), a target
  queue `class462[10]` (slot 0 = newest, max 9 pending), a `class458`
  interpolator, the **parent** world-view id `field5700`, a `class276`
  draw-priority group, a 5-bit op-enabled mask, and two animation controllers.
- **`class100` (WorldView)** is a full map view: per-plane object arrays,
  its own player list, npc list, **nested world-entity list**, tile heights
  `int[4][sx+1][sy+1]`, tile settings, and its own `class112` Scene. Capacity
  split: the top-level view (id 0) is sized 512 players / 128 npcs / 32 world
  entities; a sub-view gets **8 / 8 / 1** (nesting is supported — the
  per-frame driver `client.method1894` recurses into nested entities).
- **`class61`** registers all views in one table of **16**; the top-level
  scene is **104×104 tiles, 4 planes**; each world entity spawns its own view
  sized from a packed byte: `sizeX = (b>>4 & 0xF) * 8`, `sizeY = (b & 0xF) * 8`
  — **sub-world sizes are multiples of 8 tiles (zones)**.
- A sub-view is real map: it has `baseX/baseY` in main-world coordinate space
  (the off-map staging region — the "stationary rectangle to the north-east")
  and loads real map regions through the normal rebuild path. The entity's
  transform is what maps that region to where the boat visually is.
- Membership of actors is **purely geometric** (`class109.method3823`): a
  player materializes in whichever view's `[baseX, baseX+sizeX) ×
  [baseY, baseY+sizeY)` rectangle contains its global position; the local
  player is "aboard" (`client.field768 = view id`) iff inside a non-zero
  view's rectangle. Actors carry view-local fine coordinates (128/tile) and
  are drawn into their view's own scene.

### 5.2 Painter recursion and the entity-as-loc trick

- `class112` (Scene) **extends Renderable**. That single fact is the whole
  mechanism: a sub-scene is handed to the parent scene's per-tile object list
  exactly like a Model, and the "draw" virtual on it re-enters the scene
  renderer. The deob uses plain virtual-call recursion; there is no explicit
  stack (our port will use one, per project requirement).
- **Insertion is per-frame and transient** (`Statics.method1449`): every frame
  each world entity is re-inserted into the parent scene as a *temporary*
  GameObject at tile `(x>>7, z>>7)` with radius **60** (< 64 ⇒ always a
  **1×1-tile footprint** in the parent painter grid), menu-hash type 4, and
  its y set from the parent terrain height under it. Temporary objects live in
  per-zone lists cleared at end of frame (max 5 game objects per tile). It
  therefore painter-sorts naturally against real locs, actors and projectiles.
- **The descent transform** (`class112.method4034`), applied to every vertex
  of the sub-scene, in order:
  1. translate by `(-sizeX_tiles*64 − cfgPivotX, flattenYOffset,
     -sizeY_tiles*64 − cfgPivotY)` — recenter on the rotation pivot;
  2. scale `(1, flattenYScale, 1)` — 1.0 normally, **0.01 flattened**;
  3. multiply by the **animation matrix** — the entity's seq drives bone 0 of
     a skeleton and that bone's matrix rocks the entire sub-scene (boat
     bob/pitch/roll);
  4. rotate by yaw (angle & 0x7FF) and translate to the entity position in
     the parent;
  5. the parent camera view matrix.
  The camera is also transformed **into** sub-scene space (inverse of the
  model matrix applied to eye and focus) so the sub-scene's own painter
  ordering and culling run correctly inside the boat.
- Roof-removal behaviour of the main scene switches to sub-view rules while
  the player is aboard (scene mode `class126.field1885` vs `field1886`).

### 5.3 "Billboard" baking — there is no bake

The stacked-boats low-detail render is **not** a baked merged model, sprite,
or render-to-texture. Grep confirms no billboard/impostor machinery. What the
deob actually does (`class467.method10419`) is a **degenerate re-render of the
same sub-scene geometry**:

- Y scale = **0.01** (the whole boat collapses onto its deck plane);
- pre-scale Y offset = **−1200** (−12 world units after scaling — a
  z-fighting bias lifting the silhouette off the water);
- every triangle forced to one **flat HSL colour at strength 127** — from
  config opcode 27, default **39188** (h 38, s 2, l 84) — the "shadow" look;
- **no actors, projectiles, or graphics objects are populated at all** for a
  flattened entity (the population code short-circuits);
- flattened entities are excluded from clicking and from NPC-on-boat tests.

Nothing is precomputed or invalidated; the state is two floats and a colour
recomputed every frame. The savings come from skipping actors and the flat
shading, not from reduced geometry.

**Which boats flatten** (`Statics.method9253`, `method2832`, `method1003`):

- The entity the local player is aboard is drawn first and **never** flattened.
- Remaining entities draw in priority-group order **2, 0 (default), then 1
  last**, in list order within a group.
- A server-configured budget (`client.field824`) caps simultaneous
  full-detail entities; past the cap, the rest are forced flat.
- **Overlap rule** (`method1003` returns "should flatten"): an entity
  flattens if (a) any player is standing on it, (b) any NPC whose type opts
  in is standing on it, or (c) its oriented bounding box intersects another
  world entity **already drawn this frame**. See the port note below for why
  (a)/(b) make sense.
- The **topmost determination** is a frame-stamp: each sub-scene is stamped
  with the frame counter when placed; a later entity whose **oriented
  bounding box** (16 orientation buckets of 128 angle units, corner tables
  precomputed per config bounds) intersects an already-stamped entity's box is
  flattened. First-placed wins; stamps reset each frame.

> Port note on test (a)/(b): `method1003` is evaluated **only for priority
> group 1** (drawn last). Group-1 entities are the "yield" group (e.g.
> trawling shoals): they flatten when a player/NPC/another entity already
> occupies their space. Groups 2 and 0 flatten only via the budget. This is
> exactly the live behaviour "shoals render as shadows passing beneath boats".

### 5.4 Packets and config

**WORLDENTITY_INFO** (`Statics.method977`) — per view, per tick:

```
u8 count                      // entities that remain, in list order
repeat count times:
  u8 op                       // 0=despawn, 1=flags only, 2=enqueue move, 3=snap/teleport
  if op is 2 or 3:
    u8 mask                   // 2 bits per axis: dx, dy(height), dz, dangle
                              // 0 → 0, 1 → i8, 2 → i16, 3 → i32
    (deltas as sized above)
  u8 updateFlags              // bit 1: u16 seq id + u8 delay (65535 clears)
then, while bits remain:      // new entities
  u16 id
  u8  updateFlags
  u8  sizeByte                // sizeX=(b>>4&0xF)*8, sizeY=(b&0xF)*8 tiles
  u8  ownerTypeIndex          // → class276 priority group
  u16 configId
  absolute transform          // x,z tiles<<7, y=0, angle=0, then same 4-delta bitfield
```

Op 2 **enqueues** the target (queue depth 9), op 3 snaps. Trailing entities
beyond `count` despawn.

**Interpolation** (`class458`): each queued segment is evaluated over
**30 client cycles = 600 ms = exactly one game tick** (segment end = enqueue
cycle + 30). Pure **linear lerp on x/z**; **shortest-arc linear lerp on the
2048-unit angle** (`d = (to−from) & 0x7FF; if (d > 1024) d −= 2048`). No
splines, no easing — the server's per-tick quarter-tile steps plus 600 ms
client lerp produce the smooth arc. Height is *not* interpolated: it is
overwritten every frame from the parent terrain under the boat.

**REBUILD for a specific view**: the rebuild packet is prefixed with a world
entity id (0 = main world) + plane; the client throws if the id is unknown.
The targeted view then loads map regions exactly like a main-world rebuild
(instance-template or normal), and player lists are re-bucketed geometrically.

**Config** (`class387`, config index **archive 72**, file = id): op 2 plane;
ops 4/5 pivot offset x/y; ops 6–9 bounds w/h/offsets (baked into 16-orientation
corner tables, with margin variants 256/334/362); op 12 name; ops 14–19
right-click ops; op 20 category; op 23 click mode; op 25 default animation
(the bob); op 27 flattened HSL (default 39188).

**Click routing**: the menu hash gains a **world-view id in bits 52–63**
(4095 = none) alongside type (4 = world entity, 5 = blocker). While drawing a
sub-scene, a per-projection "hash override" substitutes the boat's hash for
non-interactive geometry, honouring the config click mode: 0 = boat always
wins, 1 = contents only (forced while you are aboard your own boat), 2
(default) = interactive contents win / dead geometry falls through to the
boat, 3 = swallow everything. Flattened entities are skipped entirely during
menu construction.

**Camera**: always works in main-world coordinates. The local player's
position is transformed out of its sub-view through the boat transform for
the camera focus (smoothed /16 per frame within ±500 units, else snap), and
the focus height = deck height inside the sub-view **plus** main-world
terrain height under the boat.

### 5.5 Magic numbers

| Value | Meaning |
|---:|---|
| 128 (`<<7`/`>>7`) | world units per tile |
| 2048 (`& 0x7FF`) | full circle, fixed-point angle |
| 1024 | shortest-arc threshold for angle lerp |
| 16 × 128 | orientation buckets for footprint boxes |
| 104×104 / 4 | top-level scene tiles / planes |
| ×8 tiles | sub-world size granularity (packed nibbles) |
| 60 | pseudo-loc insertion radius ⇒ 1×1-tile footprint |
| 0.01 / −1200 | flatten Y scale / pre-scale Y offset |
| 39188 / 127 | default flat HSL (h38 s2 l84) / override strength |
| 30 cycles | interpolation window (= 600 ms = 1 tick) |
| 9 / 10 | max queued targets / queue slots |
| 72 | config-index archive for WorldEntityConfig |
| 16 | max simultaneous world views |
| 512/128/32 vs 8/8/1 | top-level vs sub-view actor/entity capacity |
| bit 19; bits 52–63 | menu hash: "not interactive"; world-view id |

## Sources

- [World entity — OSRS Wiki](https://oldschool.runescape.wiki/w/World_entity)
- [Sailing — OSRS Wiki](https://oldschool.runescape.wiki/w/Sailing)
- [Adding A New Skill: Sailing Navigation Mechanics — OSRS Wiki](https://oldschool.runescape.wiki/w/Update:Adding_A_New_Skill:_Sailing_Navigation_Mechanics)
- [Sailing Development Progress Update — Milestone 1: Navigation — OSRS Wiki](https://oldschool.runescape.wiki/w/Update:Sailing_Development_Progress_Update_-_Milestone_1:_Navigation)
- [Behind the Scenes of Sailing: Volume 1 — OSRS Wiki](https://oldschool.runescape.wiki/w/Update:Behind_the_Scenes_of_Sailing:_Volume_1)
- [March 2025 Sailing Alpha — OSRS Wiki](https://oldschool.runescape.wiki/w/March_2025_Sailing_Alpha)
- [ClockworkRS — Sailing world-entity boat engine design](https://clockworkrs.com/docs/content/skills/sailing/engine-worldentity)
