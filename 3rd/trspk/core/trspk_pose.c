#include "trspk_pose.h"
#include "trspk_modelarena.h"
#include "../trspk_alloc.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool
trspk_pose_table_grow_elements(
    struct TRSPK_PoseTable* table,
    uint32_t min_count)
{
    if( min_count <= table->element_count )
        return true;

    uint32_t new_cap =
        table->element_cap ? table->element_cap : TRSPK_POSE_TABLE_INITIAL_ELEMENT_CAP;
    while( new_cap < min_count )
        new_cap *= 2u;

    struct TRSPK_PoseElement* new_elements = (struct TRSPK_PoseElement*)TRSPK_REALLOC(
        table->elements, new_cap * sizeof(struct TRSPK_PoseElement));
    if( !new_elements )
        return false;

    memset(
        new_elements + table->element_cap,
        0,
        (new_cap - table->element_cap) * sizeof(struct TRSPK_PoseElement));

    table->elements = new_elements;
    table->element_cap = new_cap;
    if( table->element_count < min_count )
        table->element_count = min_count;
    return true;
}

static bool
trspk_pose_track_grow_poses(
    struct TRSPK_PoseTrack* track,
    uint32_t min_cap)
{
    if( min_cap <= track->pose_cap )
        return true;

    uint32_t new_cap = track->pose_cap ? track->pose_cap : TRSPK_POSE_TABLE_INITIAL_POSE_CAP;
    while( new_cap < min_cap )
        new_cap *= 2u;

    uint32_t* new_base = (uint32_t*)TRSPK_REALLOC(track->vertex_base, new_cap * sizeof(uint32_t));
    assert(new_base);
    uint32_t* new_stamp = (uint32_t*)TRSPK_REALLOC(track->stamp, new_cap * sizeof(uint32_t));
    assert(new_stamp);

    for( uint32_t i = track->pose_cap; i < new_cap; ++i )
    {
        new_base[i] = TRSPK_POSE_VERTEX_BASE_INVALID;
        new_stamp[i] = 0u;
    }

    track->vertex_base = new_base;
    track->stamp = new_stamp;
    track->pose_cap = new_cap;
    return true;
}

void
trspk_pose_table_init(struct TRSPK_PoseTable* table)
{
    assert(table != NULL);
    assert(table->elements == NULL);

    table->elements = NULL;
    table->element_count = 0u;
    table->element_cap = 0u;
}

void
trspk_pose_table_free(struct TRSPK_PoseTable* table)
{
    if( !table )
        return;

    for( uint32_t i = 0u; i < table->element_cap; ++i )
    {
        for( int t = 0; t < TRSPK_POSE_TRACK_COUNT; ++t )
        {
            TRSPK_FREE(table->elements[i].tracks[t].vertex_base);
            TRSPK_FREE(table->elements[i].tracks[t].stamp);
        }
    }

    TRSPK_FREE(table->elements);
    table->elements = NULL;
    table->element_count = 0u;
    table->element_cap = 0u;
}

void
trspk_pose_table_clear(struct TRSPK_PoseTable* table)
{
    if( !table )
        return;

    for( uint32_t i = 0u; i < table->element_cap; ++i )
    {
        for( int t = 0; t < TRSPK_POSE_TRACK_COUNT; ++t )
            table->elements[i].tracks[t].pose_count = 0u;
    }
}

void
trspk_pose_table_set(
    struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_base)
{
    assert(table != NULL);
    assert(element_id >= 0);
    assert(anim_index >= 0 && anim_index < TRSPK_POSE_TRACK_COUNT);
    assert(pose_id >= 0);

    const uint32_t element_idx = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    const uint32_t pose_idx = (uint32_t)pose_id;

    if( !trspk_pose_table_grow_elements(table, element_idx + 1u) )
        return;

    struct TRSPK_PoseTrack* track = &table->elements[element_idx].tracks[anim_index];
    if( !trspk_pose_track_grow_poses(track, pose_idx + 1u) )
        return;

    if( pose_idx >= track->pose_count )
    {
        for( uint32_t i = track->pose_count; i < pose_idx; ++i )
            track->vertex_base[i] = TRSPK_POSE_VERTEX_BASE_INVALID;
        track->pose_count = pose_idx + 1u;
    }

    track->vertex_base[pose_idx] = vertex_base;
    track->stamp[pose_idx] = table->now;
}

void
trspk_pose_table_relocate(
    struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_base)
{
    assert(table != NULL);
    assert(element_id >= 0);
    assert(anim_index >= 0 && anim_index < TRSPK_POSE_TRACK_COUNT);
    assert(pose_id >= 0);
    uint32_t const element_idx = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    uint32_t stamp = table->now;
    /* A clear leaves the stamp arrays as they were; read the pose's own. */
    if( element_idx < table->element_cap )
    {
        struct TRSPK_PoseTrack const* track = &table->elements[element_idx].tracks[anim_index];
        if( (uint32_t)pose_id < track->pose_cap )
            stamp = track->stamp[pose_id];
    }
    trspk_pose_table_set(table, element_id, anim_index, pose_id, vertex_base);
    table->elements[element_idx].tracks[anim_index].stamp[pose_id] = stamp;
}

bool
trspk_pose_table_get(
    const struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t* out_vertex_base)
{
    assert(table != NULL);
    assert(out_vertex_base != NULL);

    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT ||
        pose_id < 0 || !table->elements )
        return false;

    const uint32_t element_idx = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    const uint32_t pose_idx = (uint32_t)pose_id;

    if( element_idx >= table->element_count )
        return false;

    const struct TRSPK_PoseTrack* track = &table->elements[element_idx].tracks[anim_index];
    if( pose_idx >= track->pose_count )
        return false;

    const uint32_t base = track->vertex_base[pose_idx];
    if( base == TRSPK_POSE_VERTEX_BASE_INVALID )
        return false;

    track->stamp[pose_idx] = table->now;
    *out_vertex_base = base;
    return true;
}

void
trspk_pose_table_remove_element(
    struct TRSPK_PoseTable* table,
    int element_id)
{
    if( !table || element_id < 0 || !table->elements )
        return;

    const uint32_t element_idx = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    if( element_idx >= table->element_count )
        return;

    for( int t = 0; t < TRSPK_POSE_TRACK_COUNT; ++t )
        table->elements[element_idx].tracks[t].pose_count = 0u;
}

void
trspk_pose_table_remove_track(
    struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index)
{
    if( !table || element_id < 0 || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT || !table->elements )
        return;

    const uint32_t element_idx = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    if( element_idx >= table->element_count )
        return;

    table->elements[element_idx].tracks[anim_index].pose_count = 0u;
}

/* Ages are counted to this many frames; anything older is as old. */
#define TRSPK_POSE_AGE_BUCKETS 256u

/* The pose slot `slot` is, and how long since it was drawn. -1: the table
 * does not name this slot at that key. */
static int
trspk_pose_slot_age(
    const struct TRSPK_PoseTable* table,
    const struct TRSPK_ModelSlot* slot,
    uint32_t** out_base)
{
    uint32_t element_idx;
    uint32_t anim;
    uint32_t pose;
    struct TRSPK_PoseTrack* track;
    uint32_t age;
    *out_base = NULL;
    if( slot->element_id < 0 || slot->pose_id < 0 || !table->elements )
        return -1;
    element_idx = (uint32_t)ToriDraw_ElementIndexOfRaw(slot->element_id);
    anim = (uint32_t)slot->pose_id % (uint32_t)TRSPK_POSE_TRACK_COUNT;
    pose = (uint32_t)slot->pose_id / (uint32_t)TRSPK_POSE_TRACK_COUNT;
    if( element_idx >= table->element_count )
        return -1;
    track = &table->elements[element_idx].tracks[anim];
    if( pose >= track->pose_count || track->vertex_base[pose] != slot->vertex_base )
        return -1;
    *out_base = &track->vertex_base[pose];
    age = table->now - track->stamp[pose];
    return (int)(age < TRSPK_POSE_AGE_BUCKETS ? age : TRSPK_POSE_AGE_BUCKETS - 1u);
}

uint32_t
trspk_pose_cache_trim(
    struct TRSPK_PoseTable* table,
    struct TRSPK_ModelArena* arena,
    uint64_t max_vertices)
{
    uint64_t by_age[TRSPK_POSE_AGE_BUCKETS];
    uint64_t const target = max_vertices - max_vertices / 4u;
    uint64_t freed = 0u;
    uint32_t cutoff;
    uint32_t evicted = 0u;
    assert(table);
    assert(arena);
    if( arena->live_vertices <= max_vertices )
        return 0u;

    /* One pass for how many vertices each age holds, one to take the oldest:
     * no sort, and only on a frame the cap was passed. */
    memset(by_age, 0, sizeof(by_age));
    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[i];
        uint32_t* base;
        int age;
        if( !trspk_modelslot_is_alive(slot) )
            continue;
        age = trspk_pose_slot_age(table, slot, &base);
        by_age[age < 0 ? TRSPK_POSE_AGE_BUCKETS - 1u : (uint32_t)age] += slot->vertex_count;
    }
    /* The youngest age still taken: from the oldest down until enough is
     * freed, never below 2 (this frame's draws and the last's). */
    cutoff = TRSPK_POSE_AGE_BUCKETS;
    while( cutoff > 2u && arena->live_vertices - freed > target )
    {
        cutoff--;
        freed += by_age[cutoff];
    }
    if( cutoff >= TRSPK_POSE_AGE_BUCKETS )
        return 0u;

    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[i];
        uint32_t* base;
        int age;
        if( !trspk_modelslot_is_alive(slot) )
            continue;
        age = trspk_pose_slot_age(table, slot, &base);
        if( age >= 0 && (uint32_t)age < cutoff )
            continue;
        if( base )
            *base = TRSPK_POSE_VERTEX_BASE_INVALID;
        trspk_modelarena_unload(arena, i);
        evicted++;
    }
    return evicted;
}
