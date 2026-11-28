#ifndef TRSPK_POSE_H
#define TRSPK_POSE_H

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_POSE_VERTEX_BASE_INVALID UINT32_MAX
#define TRSPK_POSE_TABLE_INITIAL_ELEMENT_CAP 64u
#define TRSPK_POSE_TABLE_INITIAL_POSE_CAP 8u
#define TRSPK_POSE_TRACK_COUNT 2

struct TRSPK_PoseTrack
{
    uint32_t* vertex_base;
    /* The table's `now` when each pose was last set or found: when it was last
     * drawn. A const lookup still writes it -- through the pointer, which
     * the table does not own as const. */
    uint32_t* stamp;
    uint32_t pose_count;
    uint32_t pose_cap;
};

struct TRSPK_PoseElement
{
    struct TRSPK_PoseTrack tracks[TRSPK_POSE_TRACK_COUNT];
};

struct TRSPK_PoseTable
{
    struct TRSPK_PoseElement* elements;
    uint32_t element_count;
    uint32_t element_cap;
    /* Frames, counted by trspk_pose_table_tick: the clock `stamp` reads. */
    uint32_t now;
};

struct TRSPK_ModelArena;

/* The cap trspk_pose_cache_trim is run with, in vertices: a desktop's, and a
 * phone's or a low-memory browser tab's (26 MB of the WebGL2 OSRS vertex). */
#define TRSPK_POSE_CACHE_VERTICES (2u * 1024u * 1024u)
#define TRSPK_POSE_CACHE_LOW_VERTICES (512u * 1024u)

/* trspk_pose_table_set for a pose that only MOVED (a compaction rebuilding the
 * table from the arena's new bases): its stamp is kept, so the cache's order
 * survives the move. */
void
trspk_pose_table_relocate(
    struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_base);

/* One frame has passed. */
static inline void
trspk_pose_table_tick(struct TRSPK_PoseTable* table)
{
    table->now++;
}

/*
 * The cap on a retained arena: when its live slots hold more than
 * `max_vertices`, the least recently drawn of them go -- slot unloaded, pose
 * forgotten -- until a quarter under the cap. A pose drawn this frame or the
 * last is never taken. A slot the table does not name is the oldest there is.
 *
 * What it caps: every animation frame of every animated loc in a region,
 * baked once into the static arena the first time it is drawn and kept for
 * the region's life (es3_*_bake_track_pose) -- 3-4 million vertices in
 * Zanaris, 156 MB of pages and a 222 MB GL buffer (2026-11-28). An evicted
 * pose is baked again the next time it is drawn, as any pose the table does
 * not hold is. Nothing moves here: the holes go at the next gc, between
 * frames, which is also why a range a draw of this frame already named stays
 * whole. Run it right before that gc. Returns the slots evicted.
 */
uint32_t
trspk_pose_cache_trim(
    struct TRSPK_PoseTable* table,
    struct TRSPK_ModelArena* arena,
    uint64_t max_vertices);

void
trspk_pose_table_init(struct TRSPK_PoseTable* table);

void
trspk_pose_table_free(struct TRSPK_PoseTable* table);

void
trspk_pose_table_clear(struct TRSPK_PoseTable* table);

void
trspk_pose_table_set(
    struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_base);

bool
trspk_pose_table_get(
    const struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t* out_vertex_base);

void
trspk_pose_table_remove_element(
    struct TRSPK_PoseTable* table,
    int element_id);

void
trspk_pose_table_remove_track(
    struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index);

#endif
