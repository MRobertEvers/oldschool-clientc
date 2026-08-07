#ifndef TRSPK_BATCH16_H
#define TRSPK_BATCH16_H

#include "trspk_triangles.h"
#include "trspk_vbo.h"

#include <stdbool.h>
#include <stdint.h>

/* Largest page vertex count addressable by the page-local uint16 index path. */
#define TRSPK_BATCH16_MAX_VERTICES 65535u
#define TRSPK_BATCH16_INVALID_CHUNK UINT32_MAX

struct TRSPK_Batch16;

/*
 * One persistent CPU-side page.  The VBO and triangle-config allocation stay
 * attached to this chunk across begin/clear cycles; vertex_count is the live
 * prefix for the current build.
 */
struct TRSPK_Batch16Chunk
{
    struct TRSPK_VBO* vbo;
    struct TRSPK_Triangles triangles;
    uint32_t vertex_count;
};

/* Stable identity and location of one retained model pose in the batch. */
struct TRSPK_Batch16Entry
{
    int element_id;
    int anim_index;
    int pose_id;
    uint32_t chunk_index;
    uint32_t vertex_base;
    uint32_t vertex_count;
};

/* Writable storage returned while a batch build is active. */
struct TRSPK_Batch16Reservation
{
    struct TRSPK_VBO* vbo;
    struct TRSPK_Triangles* triangles;
    uint32_t chunk_index;
    uint32_t vertex_base;
};

struct TRSPK_Batch16*
trspk_batch16_create(enum TRSPK_VertexFormat format);

void
trspk_batch16_destroy(struct TRSPK_Batch16* batch);

/*
 * Start a new build.  Live chunk/entry counts are reset, while all backing
 * allocations are retained for reuse.  A static batch only needs another
 * begin when its contents are rebuilt; a dynamic batch may begin each frame.
 */
void
trspk_batch16_begin(struct TRSPK_Batch16* batch);

/*
 * Reserve one contiguous triangle-list pose.  vertex_count must be non-zero,
 * divisible by three, and no larger than TRSPK_BATCH16_MAX_VERTICES.  A pose
 * never crosses a chunk boundary.  Repeating the same key in one build is
 * idempotent when vertex_count agrees and returns the original reservation.
 */
bool
trspk_batch16_reserve_pose(
    struct TRSPK_Batch16* batch,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_count,
    struct TRSPK_Batch16Reservation* out_reservation);

void
trspk_batch16_end(struct TRSPK_Batch16* batch);

/* Empty the batch without freeing chunk, VBO, triangle, entry, or lookup storage. */
void
trspk_batch16_clear(struct TRSPK_Batch16* batch);

uint32_t
trspk_batch16_chunk_count(const struct TRSPK_Batch16* batch);

struct TRSPK_Batch16Chunk*
trspk_batch16_get_chunk(
    struct TRSPK_Batch16* batch,
    uint32_t chunk_index);

uint32_t
trspk_batch16_entry_count(const struct TRSPK_Batch16* batch);

const struct TRSPK_Batch16Entry*
trspk_batch16_get_entry(
    const struct TRSPK_Batch16* batch,
    uint32_t entry_index);

const struct TRSPK_Batch16Entry*
trspk_batch16_find_entry(
    const struct TRSPK_Batch16* batch,
    int element_id,
    int anim_index,
    int pose_id);

#endif
