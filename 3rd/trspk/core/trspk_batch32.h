#ifndef TRSPK_BATCH32_H
#define TRSPK_BATCH32_H

#include "trspk_triangles.h"
#include "trspk_vbo.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The world build for a renderer that draws with 32-bit indices.
 *
 * Batch16's contract without its pages: Batch16 splits a build into chunks of
 * at most 65535 vertices because the page-local uint16 index path cannot
 * address more. A 32-bit index can, so a Batch32 is one VBO and one triangle
 * table, and a pose is located by its vertex base alone.
 *
 * A build is written once between begin and end and then left alone: the
 * renderer uploads it once and draws out of it until the next build or clear.
 * Nothing is ever unloaded from the middle of a batch, so there is nothing to
 * compact -- runtime geometry belongs in the renderer's per-frame buffer, not
 * here.
 */

struct TRSPK_Batch32;

/* Stable identity and location of one baked model pose in the batch. */
struct TRSPK_Batch32Entry
{
    int element_id;
    int anim_index;
    int pose_id;
    uint32_t vertex_base;
    uint32_t vertex_count;
};

/* Writable storage returned while a build is active. */
struct TRSPK_Batch32Reservation
{
    struct TRSPK_VBO* vbo;
    struct TRSPK_Triangles* triangles;
    uint32_t vertex_base;
};

struct TRSPK_Batch32*
trspk_batch32_create(enum TRSPK_VertexFormat format);

void
trspk_batch32_destroy(struct TRSPK_Batch32* batch);

/*
 * Start a new build. The live vertex and entry counts are reset; every backing
 * allocation is kept for reuse.
 */
void
trspk_batch32_begin(struct TRSPK_Batch32* batch);

/*
 * Reserve one contiguous triangle-list pose. vertex_count must be non-zero and
 * divisible by three. Repeating a key in one build returns the original
 * reservation; repeating it with a different vertex count is a caller bug.
 */
void
trspk_batch32_reserve_pose(
    struct TRSPK_Batch32* batch,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_count,
    struct TRSPK_Batch32Reservation* out_reservation);

void
trspk_batch32_end(struct TRSPK_Batch32* batch);

/* Empty the batch without freeing vertex, triangle, entry, or lookup storage. */
void
trspk_batch32_clear(struct TRSPK_Batch32* batch);

bool
trspk_batch32_building(const struct TRSPK_Batch32* batch);

/* The build's vertices, vertex_count of them, and their triangle configs. */
struct TRSPK_VBO*
trspk_batch32_vbo(struct TRSPK_Batch32* batch);

const struct TRSPK_Triangles*
trspk_batch32_triangles(const struct TRSPK_Batch32* batch);

uint32_t
trspk_batch32_entry_count(const struct TRSPK_Batch32* batch);

const struct TRSPK_Batch32Entry*
trspk_batch32_get_entry(
    const struct TRSPK_Batch32* batch,
    uint32_t entry_index);

/* NULL when the key is not in the current build. */
const struct TRSPK_Batch32Entry*
trspk_batch32_find_entry(
    const struct TRSPK_Batch32* batch,
    int element_id,
    int anim_index,
    int pose_id);

#endif
