#ifndef TRSPK_POSE_POOL_H
#define TRSPK_POSE_POOL_H

/*
 * Dynamic models' bakes, kept by pose. Shared by every GPU world renderer.
 *
 * A GPU renderer rebuilds its dynamic group every frame, and a dynamic model's
 * bake is a pure function of four things: the model it wears (and what has
 * been done to that model in place -- a tint, a mount -- which
 * ToriDraw_SceneElementModelForWrite counts in model_revision), the pose that
 * model holds (the tuple ToriDraw_SceneElementApplyAnimation records), and
 * where it stands. The renderer asks for it every frame; a sequence holds each
 * frame for two to four cycles, an idle loop comes round again, and a standing
 * actor does not move. So most of those bakes reproduce one already made: the
 * bake and the posing behind it were 16% of a WebGL2 replay's main thread.
 *
 * The pool keeps the last few -- the vertices, in the arena's own vertex
 * format, and the triangle configs -- and a hit copies them into this frame's
 * arena instead of baking. Bounded by bytes and entries, least recently used
 * first out; an entry unused for TRSPK_POSE_POOL_MAX_AGE frames goes
 * regardless. Element ids are recycled and so are allocations, so the model
 * is fingerprinted by its arrays and counts as well as its address, and the
 * age limit keeps any stale entry short-lived.
 *
 * Only for a bake made in the model's own face order (no face_order): an
 * ordered painter bake depends on the camera.
 */

#include "core/trspk_modelarena.h"
#include "core/trspk_triangles.h"
#include "core/trspk_vbo.h"
#include "render/torirs_render.h"
#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_POSE_POOL_ENTRIES 128u
#define TRSPK_POSE_POOL_BYTES (8u * 1024u * 1024u)
#define TRSPK_POSE_POOL_MAX_AGE 300u

struct TRSPK_PoseKey
{
    int element_id;
    const struct ToriDraw_Model* model;
    const void* vertices_x;
    const void* face_colors_c;
    int vertex_count;
    int face_count;
    uint32_t model_revision;
    int posed_primary;
    int posed_frame;
    int posed_frame2;
    const void* posed_track;
    const void* posed_track2;
    struct ToriDraw_Position position;
};

struct TRSPK_PosePoolEntry
{
    struct TRSPK_PoseKey key;
    /** NULL: the entry is free. vertex_count vertices of vertex_size bytes. */
    uint8_t* vertices;
    /** One triangle config per face (TRSPK_Triangles). */
    int* configs;
    uint32_t vertex_count;
    uint32_t vertex_size;
    uint64_t last_used;
};

struct TRSPK_PosePool
{
    struct TRSPK_PosePoolEntry entries[TRSPK_POSE_POOL_ENTRIES];
    uint64_t bytes;
    /** Frames begun (trspk_pose_pool_tick): the LRU's clock. */
    uint64_t frame;
};

/**
 * The key of the bake `command` is about to make, or false when there is no
 * telling what pose the model holds: no element, a shared or lent model, a
 * model the element no longer wears, or no recorded pose. Read after the
 * model was posed for this draw.
 */
bool
trspk_pose_key_for_command(
    struct ToriDraw_Scene* scene,
    const struct ToriRS_RenderCommand_Model* command,
    struct TRSPK_PoseKey* out);

/** Once a frame, before the frame's first draw. */
void
trspk_pose_pool_tick(struct TRSPK_PosePool* pool);

/**
 * When the pool holds `key`: a slot in `arena` (element `arena_element_id`,
 * pose `arena_pose_id`) filled from it -- vertices into `vbo`, configs into
 * `triangles`, the range marked dirty -- and its vertex_base. UINT32_MAX when
 * it does not.
 */
uint32_t
trspk_pose_pool_place(
    struct TRSPK_PosePool* pool,
    const struct TRSPK_PoseKey* key,
    struct TRSPK_ModelArena* arena,
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    int arena_element_id,
    int arena_pose_id);

/** Keep a copy of the bake just made at [vertex_base, + vertex_count). */
void
trspk_pose_pool_store(
    struct TRSPK_PosePool* pool,
    const struct TRSPK_PoseKey* key,
    const struct TRSPK_VBO* vbo,
    const struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    uint32_t vertex_count);

void
trspk_pose_pool_free(struct TRSPK_PosePool* pool);

#endif /* TRSPK_POSE_POOL_H */
