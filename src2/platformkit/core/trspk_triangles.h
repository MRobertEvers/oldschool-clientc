#ifndef TRSPK_TRIANGLES_H
#define TRSPK_TRIANGLES_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* Per global triangle: atlas/static UVs. */
#define TRSPK_TRIANGLES_ATLAS (-1)

#define TRSPK_TRIANGLES_INIT_CAP 4096u

struct TRSPK_Triangles
{
    int* config;
    uint32_t cap;
};

void
trspk_triangles_free(struct TRSPK_Triangles* triangles);

void
trspk_triangles_ensure(
    struct TRSPK_Triangles* triangles,
    uint32_t tri_count);

static inline uint32_t
trspk_triangles_index_from_vertex(uint32_t vertex_index)
{
    return vertex_index / 3u;
}

static inline int
trspk_triangles_make_config(
    int tex_id,
    bool is_animated)
{
    return is_animated ? tex_id : TRSPK_TRIANGLES_ATLAS;
}

static inline uint32_t
trspk_triangles_encode_draw_config(int cfg)
{
    return cfg < 0 ? 0u : (uint32_t)(cfg + 1);
}

static inline void
trspk_triangles_set(
    struct TRSPK_Triangles* triangles,
    uint32_t tri_index,
    int value)
{
    assert(tri_index < triangles->cap && "Invalid triangle index");
    triangles->config[tri_index] = (int16_t)value;
}

static inline int
trspk_triangles_get(
    const struct TRSPK_Triangles* triangles,
    uint32_t tri_index)
{
    assert(tri_index < triangles->cap && "Invalid triangle index");
    return triangles->config[tri_index];
}

#endif
