---
name: ContourGround iterator
overview: Create `src/osrs/contour_ground.h` and `src/osrs/contour_ground.c` implementing all 5 contour ground types as a command-loop iterator, then update `apply_contour_ground` in `world_scenery.u.c` to use it.
todos:
  - id: create-header
    content: Create src/osrs/contour_ground.h with ContourGround, ContourGroundCommand structs and function declarations
    status: completed
  - id: create-impl
    content: Create src/osrs/contour_ground.c implementing contour_ground_init (bounds checks, flat-terrain check, precompute min/max) and contour_ground_next (all 5 types)
    status: completed
  - id: update-caller
    content: Update apply_contour_ground in world_scenery.u.c to use the new iterator for all 5 types
    status: completed
isProject: false
---

# ContourGround Command-Loop Iterator

## New files

- `[src/osrs/contour_ground.h](src/osrs/contour_ground.h)` — public API
- `[src/osrs/contour_ground.c](src/osrs/contour_ground.c)` — implementation

## Modified files

- `[src/osrs/world_scenery.u.c](src/osrs/world_scenery.u.c)` — replace the type-1-only `apply_contour_ground` with the full iterator

---

## Key design change: no heightmap in the struct

`ContourGround` holds no heightmap pointers. Instead it emits `FETCH` commands that tell the caller which world coordinates to look up. The caller performs the lookup in whatever heightmap it owns, then provides the result back with `contour_ground_provide`. This keeps all heightmap I/O in the caller.

---

## Header design

```c
enum ContourGroundCmdKind {
    CONTOUR_CMD_FETCH_HEIGHT,       // caller: look up heightmap at (draw_x, draw_z, slevel)
    CONTOUR_CMD_FETCH_HEIGHT_ABOVE, // caller: look up heightmap_above at (draw_x, draw_z, slevel)
    CONTOUR_CMD_SET_Y,              // caller: write cmd.contour_y to cmd.vertex_index
};

struct ContourGroundCommand {
    enum ContourGroundCmdKind kind;
    /* FETCH_HEIGHT / FETCH_HEIGHT_ABOVE fields */
    int draw_x;
    int draw_z;
    int slevel;
    /* SET_Y fields */
    int vertex_index;
    int contour_y;
};

struct ContourGround {
    /* public inputs */
    int type;
    int param;
    int scene_x, scene_z, scene_height, slevel;
    const struct CacheModel* model;
    int used_vertex_count;
    /* private state */
    int _i;           // current vertex index
    int _limit;       // total vertices to process
    int _substep;     // position within per-vertex pipeline
    int _height;      // cached from last FETCH_HEIGHT provide
    int _height_above;// cached from last FETCH_HEIGHT_ABOVE provide
    int _min_y;       // for type 2
    int _delta_y;     // maxY - minY, for types 4 & 5
};

// Returns true if contouring is needed; false = caller skips the loop.
// hm_size_x/z and hm_above_size_x/z are the tile dimensions of the two
// heightmaps (pass 0/0 for hm_above when types 1-3 are used).
bool contour_ground_init(
    struct ContourGround*,
    int type, int param,
    int hm_size_x, int hm_size_z,
    int hm_above_size_x, int hm_above_size_z,
    const struct CacheModel*, int used_vertex_count,
    int scene_x, int scene_z, int scene_height, int slevel);

// Advances the iterator and fills *cmd.  Returns false when done.
bool contour_ground_next(struct ContourGround*, struct ContourGroundCommand*);

// After receiving CONTOUR_CMD_FETCH_HEIGHT or FETCH_HEIGHT_ABOVE the caller
// must call this before the next contour_ground_next().
void contour_ground_provide(struct ContourGround*, int height);
```

---

## Caller loop pattern

```c
struct ContourGround cg;
if (contour_ground_init(&cg, type, param,
                        heightmap->size_x, heightmap->size_z,
                        above ? above->size_x : 0, above ? above->size_z : 0,
                        model, model->vertex_count,
                        scene_x, scene_z, scene_height, slevel)) {
    struct ContourGroundCommand cmd;
    while (contour_ground_next(&cg, &cmd)) {
        switch (cmd.kind) {
        case CONTOUR_CMD_FETCH_HEIGHT:
            contour_ground_provide(&cg,
                heightmap_get_interpolated(heightmap, cmd.draw_x, cmd.draw_z, cmd.slevel));
            break;
        case CONTOUR_CMD_FETCH_HEIGHT_ABOVE:
            contour_ground_provide(&cg,
                heightmap_get_interpolated(above, cmd.draw_x, cmd.draw_z, cmd.slevel));
            break;
        case CONTOUR_CMD_SET_Y:
            model->vertices_y[cmd.vertex_index] = cmd.contour_y;
            break;
        }
    }
}
```

---

## `contour_ground_init` logic

1. If `used_vertex_count == 0` → return `false`.
2. Scan `vertices_x/z[0..used_vertex_count)` to compute `min_x, max_x, min_z, max_z`.
3. **Heightmap bounds check** (types 1, 2, 3, 5) using `hm_size_x / hm_size_z`:

- `start_x = scene_x + min_x`, `end_x = scene_x + max_x` (same for Z).
- If `start_x < 0 || (end_x+128)>>7 >= hm_size_x || start_z < 0 || (end_z+128)>>7 >= hm_size_z` → return `false`.

1. **Heightmap-above bounds check** (types 4, 5) using `hm_above_size_x / hm_above_size_z`:

- If either dimension is 0, or the same arithmetic fails → return `false`.

1. Scan `vertices_y[0..used_vertex_count)` to precompute `_min_y` and `_delta_y = max_y - min_y`.
2. `_limit = used_vertex_count` for types 3/4/5; `_limit = model->vertex_count` for types 1/2.
3. `_i = 0`, `_substep = 0`. Return `true`.

Note: the flat-terrain early-exit optimisation (all 4 bbox corners equal `scene_height`) is intentionally left to the caller, since it requires heightmap access that now lives outside the iterator.

---

## `contour_ground_next` per-vertex pipeline

Each call advances the internal state machine (`_i`, `_substep`) and fills one command:

**Types 1 & 2** — per vertex:

- Step 0 (`_substep = 0`): check per-vertex bounds (`i >= used_vertex_count`); if out of bounds emit `SET_Y` with `contour_y = vertices_y[i]` immediately. Otherwise emit `FETCH_HEIGHT` with `draw_x = vertices_x[i] + scene_x`, `draw_z = vertices_z[i] + scene_z`.
- Step 1 (`_substep = 1`): use `_height` (from `contour_ground_provide`) to compute `contour_y` and emit `SET_Y`.
  - Type 1: `contour_y = vertices_y[i] + _height - scene_height`
  - Type 2: if `y_ratio < param`: `contour_y = vertices_y[i] + ((_height - scene_height) * (param - y_ratio)) / param`; else `contour_y = vertices_y[i]`

**Type 3** — per vertex: emit `SET_Y` with `contour_y = vertices_y[i]` directly (no FETCH needed).

**Type 4** — per vertex:

- Step 0: emit `FETCH_HEIGHT_ABOVE`.
- Step 1: `contour_y = vertices_y[i] + _height_above - scene_height + _delta_y`; emit `SET_Y`.

**Type 5** — per vertex:

- Step 0: emit `FETCH_HEIGHT`.
- Step 1: emit `FETCH_HEIGHT_ABOVE`.
- Step 2: `delta_height = _height - _height_above`; `contour_y = (((vertices_y[i] << 8) / _delta_y) * delta_height) >> 8 - (scene_height - _height)`; emit `SET_Y`.

`contour_ground_provide` writes into `_height` for FETCH_HEIGHT responses and `_height_above` for FETCH_HEIGHT_ABOVE responses (the iterator knows which is expected from `_substep`).

---

## Updated `apply_contour_ground` in `world_scenery.u.c`

The caller loop replaces the current type-1-only implementation, using the pattern shown above. `heightmap_get_interpolated` is already available and matches the bilinear formula from the TypeScript exactly.
