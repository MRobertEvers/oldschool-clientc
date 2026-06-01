#ifndef TRSPK_MODELARENA_H
#define TRSPK_MODELARENA_H

#include "trspk_flags.h"
#include "trspk_triangles.h"
#include "trspk_vbo.h"

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_MODELSLOT_NULL_IDX UINT32_MAX
#define TRSPK_MODELSLOT_FLAG_ALIVE TRSPK_FLAG(0)

struct TRSPK_ModelSlot
{
    uint32_t vertex_base;
    uint32_t vertex_count;
    uint32_t tri_start;
    uint32_t tri_count;
    int element_id;
    uint32_t flags;
    uint32_t next_free;
};

struct TRSPK_ModelArena
{
    struct TRSPK_ModelSlot* slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint32_t free_head;

    uint32_t write_cursor;
    uint32_t page_size;

    struct TRSPK_VBO* vbo;
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

void
trspk_modelarena_free(struct TRSPK_ModelArena* arena);

uint32_t
trspk_modelarena_load(
    struct TRSPK_ModelArena* arena,
    int element_id,
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
    int element_id);

void
trspk_modelarena_clear(struct TRSPK_ModelArena* arena);

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
