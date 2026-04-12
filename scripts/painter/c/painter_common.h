/* Shared types for painter_bucket and painter_world3d benchmarks. */
#ifndef PAINTER_COMMON_H
#define PAINTER_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Max grid dimension (128x128 tiles per level). */
#define PAINTER_MAX_GRID 128
/* ceil(n_bits / 64) for n_bits = PAINTER_MAX_GRID * PAINTER_MAX_GRID */
#define PAINTER_FRUSTUM_WORDS ((PAINTER_MAX_GRID * PAINTER_MAX_GRID + 63) / 64)

#define PAINTER_SPAN_WEST 0x1u
#define PAINTER_SPAN_SOUTH 0x2u
#define PAINTER_SPAN_EAST 0x4u
#define PAINTER_SPAN_NORTH 0x8u

#define PAINTER_MAX_PRIMARY 5
#define PAINTER_MAX_SCENERY 64
#define PAINTER_MAX_TILES_PER_SCENERY (PAINTER_MAX_GRID * PAINTER_MAX_GRID)

enum PainterTileStep
{
    PAINTER_STEP_NOT_VISITED = 0,
    PAINTER_STEP_BASE = 1,
    PAINTER_STEP_DONE = 2,
};

/* Origin (ox, oz) = min tile (x, z); footprint w x h tiles on xz plane. */
typedef struct PainterScenerySpec
{
    int ox;
    int oz;
    int w;
    int h;
    int level;
} PainterScenerySpec;

typedef struct PainterConfig
{
    int grid_size;
    int levels;
    double eye_x;
    double eye_z;
    double yaw_deg;
    double fov_deg;
    int radius; /* world3d Chebyshev seed radius; clamped in implementation */
    const PainterScenerySpec *scenery;
    int scenery_count;
} PainterConfig;

static inline int
painter_scenery_min_x(const PainterScenerySpec *s)
{
    return s->ox;
}

static inline int
painter_scenery_max_x(const PainterScenerySpec *s)
{
    return s->ox + s->w - 1;
}

static inline int
painter_scenery_min_z(const PainterScenerySpec *s)
{
    return s->oz;
}

static inline int
painter_scenery_max_z(const PainterScenerySpec *s)
{
    return s->oz + s->h - 1;
}

/* Bit index matches Python: z * grid_size + x (painter.py used y for z). */
static inline int
painter_frustum_bit_index(int x, int z, int grid_size)
{
    return z * grid_size + x;
}

void painter_compute_frustum_mask(
    uint64_t *mask_words,
    int grid_size,
    double eye_x,
    double eye_z,
    double yaw_deg,
    double fov_deg);

bool painter_tile_visible_in_mask(
    const uint64_t *mask_words,
    int x,
    int z,
    int grid_size);

#define PAINTER_GRID_IDX(level, x, z, gs) \
    ((ptrdiff_t)(level) * (gs) * (gs) + (ptrdiff_t)(z) * (gs) + (x))

/* Opaque workspaces allocated by painter_*_alloc. */
void *painter_bucket_alloc(const PainterConfig *cfg);
void painter_bucket_free(void *ws);
void painter_bucket_run(void *ws, const PainterConfig *cfg);

void *painter_world3d_alloc(const PainterConfig *cfg);
void painter_world3d_free(void *ws);
void painter_world3d_run(void *ws, const PainterConfig *cfg);

#endif /* PAINTER_COMMON_H */
