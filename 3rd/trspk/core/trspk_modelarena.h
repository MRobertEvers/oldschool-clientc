#ifndef TRSPK_MODELARENA_H
#define TRSPK_MODELARENA_H

#include "trspk_flags.h"
#include "trspk_triangles.h"
#include "trspk_vbo.h"
#include "trspk_vbochain16.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRSPK_MODELSLOT_NULL_IDX UINT32_MAX
#define TRSPK_MODELSLOT_FLAG_ALIVE TRSPK_FLAG(0)

struct TRSPK_ModelSlot
{
    uint32_t vertex_base;
    uint32_t vertex_count;
    uint32_t tri_start;
    uint32_t tri_count;
    uint32_t page;
    int element_id;
    int pose_id;
    uint32_t flags;
    uint32_t next_free;
    /** Next slot in this slot's (element_id, pose_id) bucket. The lookup
     *  index chains through the slots themselves, so it costs one word per
     *  slot and never allocates per entry. */
    uint32_t lookup_next;
};

struct TRSPK_ModelArena
{
    struct TRSPK_ModelSlot* slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint32_t free_head;

    /* (element_id, pose_id) -> slot. trspk_modelarena_find used to walk
     * every slot, and it is called once per model bake, so a frame that
     * rebakes N models walked N^2 slots. Bucket heads, chained through
     * slot->lookup_next; bucket_count is a power of two and 0 means the
     * index is not built yet (an empty arena). */
    uint32_t* lookup_buckets;
    uint32_t lookup_bucket_count;

    uint32_t write_cursor;
    uint32_t page_size;

    struct TRSPK_VBO* vbo;
    struct TRSPK_VBOChain16* vbo_chain;
    struct TRSPK_Triangles* tri;
};

struct TRSPK_ModelArenaGCResult
{
    uint32_t relocated_count;
    bool did_compact;
};

typedef void (*TRSPK_ModelArenaRebuildFn)(
    struct TRSPK_ModelArena* arena,
    uint32_t slot_index,
    void* user_data);

typedef void (*TRSPK_ModelArenaVisitFn)(
    const struct TRSPK_ModelSlot* slot,
    void* user_data);

struct TRSPK_ModelArena*
trspk_modelarena_create(
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t page_size,
    uint32_t initial_slot_capacity);

struct TRSPK_ModelArena*
trspk_modelarena_create_chain16(
    struct TRSPK_VBOChain16* chain,
    struct TRSPK_Triangles* triangles,
    uint32_t initial_slot_capacity);

void
trspk_modelarena_free(struct TRSPK_ModelArena* arena);

uint32_t
trspk_modelarena_load(
    struct TRSPK_ModelArena* arena,
    int element_id,
    int pose_id,
    uint32_t vertex_count);

void
trspk_modelarena_unload(
    struct TRSPK_ModelArena* arena,
    uint32_t slot_index);

const struct TRSPK_ModelSlot*
trspk_modelarena_get(
    const struct TRSPK_ModelArena* arena,
    uint32_t slot_index);

uint32_t
trspk_modelarena_find(
    const struct TRSPK_ModelArena* arena,
    int element_id,
    int pose_id);

void
trspk_modelarena_unload_element(
    struct TRSPK_ModelArena* arena,
    int element_id);

void
trspk_modelarena_clear(struct TRSPK_ModelArena* arena);

/**
 * Reclaim unloaded ranges in a flat VBO, packing live slots across pages while
 * preserving the rule that one slot never crosses a page boundary.  A compact
 * result may change live slots' vertex_base values; callers that cache those
 * bases (for example TRSPK_PoseTable) must rebuild the cache when did_compact
 * is true.  Allocated capacities are intentionally retained for reuse.
 */
struct TRSPK_ModelArenaGCResult
trspk_modelarena_gc(struct TRSPK_ModelArena* arena);

void
trspk_modelarena_rebuild(
    struct TRSPK_ModelArena* arena,
    TRSPK_ModelArenaRebuildFn rebuild_fn,
    void* user_data);

void
trspk_modelarena_visit_alive(
    const struct TRSPK_ModelArena* arena,
    TRSPK_ModelArenaVisitFn visit_fn,
    void* user_data);

static inline bool
trspk_modelslot_is_alive(const struct TRSPK_ModelSlot* slot)
{
    return slot != NULL && trspk_flags_test(slot->flags, TRSPK_MODELSLOT_FLAG_ALIVE);
}

#endif
