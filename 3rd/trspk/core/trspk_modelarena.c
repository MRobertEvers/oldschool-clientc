#include "trspk_modelarena.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TRSPK_MODELARENA_INIT_SLOT_CAP 64u

static uint32_t
align_vertex_base(
    uint32_t cur,
    uint32_t vert_count,
    uint32_t page_size)
{
    if( vert_count == 0u || page_size == 0u )
        return cur;

    const uint32_t end = cur + vert_count - 1u;
    if( cur / page_size != end / page_size )
        cur = ((cur + page_size - 1u) / page_size) * page_size;

    return cur;
}

static void
move_vertices(
    struct TRSPK_VBO* vbo,
    uint32_t dst,
    uint32_t src,
    uint32_t count)
{
    if( count == 0u || dst == src )
        return;

    switch( vbo->format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        memmove(
            &vbo->vertices.as_webgl1[dst],
            &vbo->vertices.as_webgl1[src],
            count * sizeof(struct TRSPK_VertexWebGL1));
        break;
    case TRSPK_VERTEX_FORMAT_OPENGL3:
        memmove(
            &vbo->vertices.as_opengl3[dst],
            &vbo->vertices.as_opengl3[src],
            count * sizeof(struct TRSPK_VertexOpenGl3));
        break;
    case TRSPK_VERTEX_FORMAT_D3D9:
        memmove(
            &vbo->vertices.as_d3d9[dst],
            &vbo->vertices.as_d3d9[src],
            count * sizeof(struct TRSPK_VertexD3D9));
        break;
    case TRSPK_VERTEX_FORMAT_GLES2:
        memmove(
            &vbo->vertices.as_gles2[dst],
            &vbo->vertices.as_gles2[src],
            count * sizeof(struct TRSPK_VertexGLES2));
        break;
    default:
        assert(false && "Unsupported vertex format");
        break;
    }

    trspk_vbo_set_dirty(vbo);
}

static void
move_triangles(
    struct TRSPK_Triangles* tri,
    uint32_t dst_tri,
    uint32_t src_tri,
    uint32_t tri_count)
{
    if( tri_count == 0u || dst_tri == src_tri )
        return;

    assert(tri->config != NULL);
    assert(dst_tri + tri_count <= tri->cap);
    assert(src_tri + tri_count <= tri->cap);

    memmove(
        &tri->config[dst_tri],
        &tri->config[src_tri],
        tri_count * sizeof(int));
}

/*
 * The (element_id, pose_id) index.
 *
 * trspk_modelarena_find is called once per model bake and used to scan every
 * slot in the arena, so a frame that rebakes N models did O(N^2) slot visits.
 * The index is a bucket table of slot indices chained through the slots'
 * own lookup_next field: one word per slot, no per-entry allocation, and
 * nothing to keep in step when the arena compacts, because compaction sorts
 * pointers and never changes a slot's index or its keys.
 */

static uint32_t
lookup_hash(int element_id, int pose_id)
{
    /* Both keys are small non-negative integers and pose_id varies fastest,
     * so they are mixed rather than concatenated -- otherwise every pose of
     * one element lands in a handful of adjacent buckets. */
    uint32_t h = (uint32_t)element_id * 2654435761u;
    h ^= (uint32_t)pose_id * 2246822519u;
    h ^= h >> 15;
    return h;
}

static void
lookup_rebuild(struct TRSPK_ModelArena* arena, uint32_t min_buckets)
{
    uint32_t buckets = arena->lookup_bucket_count ? arena->lookup_bucket_count : 64u;
    uint32_t i;

    while( buckets < min_buckets )
        buckets *= 2u;

    if( buckets != arena->lookup_bucket_count || arena->lookup_buckets == NULL )
    {
        uint32_t* grown =
            (uint32_t*)realloc(arena->lookup_buckets, buckets * sizeof(uint32_t));
        assert(grown != NULL);
        arena->lookup_buckets = grown;
        arena->lookup_bucket_count = buckets;
    }

    for( i = 0u; i < arena->lookup_bucket_count; ++i )
        arena->lookup_buckets[i] = TRSPK_MODELSLOT_NULL_IDX;

    for( i = 0u; i < arena->slot_count; ++i )
    {
        struct TRSPK_ModelSlot* slot = &arena->slots[i];
        uint32_t bucket;
        if( !trspk_modelslot_is_alive(slot) )
        {
            slot->lookup_next = TRSPK_MODELSLOT_NULL_IDX;
            continue;
        }
        bucket = lookup_hash(slot->element_id, slot->pose_id) &
            (arena->lookup_bucket_count - 1u);
        slot->lookup_next = arena->lookup_buckets[bucket];
        arena->lookup_buckets[bucket] = i;
    }
}

static void
lookup_insert(struct TRSPK_ModelArena* arena, uint32_t slot_index)
{
    struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
    uint32_t bucket;

    /* Keep the table at no more than one entry per bucket on average; the
     * chains stay short and find stays a couple of probes. */
    if( arena->lookup_bucket_count == 0u || arena->slot_count > arena->lookup_bucket_count )
    {
        lookup_rebuild(arena, arena->slot_count * 2u);
        return; /* the rebuild inserted every live slot, this one included */
    }

    bucket = lookup_hash(slot->element_id, slot->pose_id) &
        (arena->lookup_bucket_count - 1u);
    slot->lookup_next = arena->lookup_buckets[bucket];
    arena->lookup_buckets[bucket] = slot_index;
}

static void
lookup_remove(struct TRSPK_ModelArena* arena, uint32_t slot_index)
{
    struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
    uint32_t bucket;
    uint32_t cur;
    uint32_t prev = TRSPK_MODELSLOT_NULL_IDX;

    if( arena->lookup_bucket_count == 0u )
        return;

    bucket = lookup_hash(slot->element_id, slot->pose_id) &
        (arena->lookup_bucket_count - 1u);
    cur = arena->lookup_buckets[bucket];
    while( cur != TRSPK_MODELSLOT_NULL_IDX )
    {
        if( cur == slot_index )
        {
            if( prev == TRSPK_MODELSLOT_NULL_IDX )
                arena->lookup_buckets[bucket] = slot->lookup_next;
            else
                arena->slots[prev].lookup_next = slot->lookup_next;
            slot->lookup_next = TRSPK_MODELSLOT_NULL_IDX;
            return;
        }
        prev = cur;
        cur = arena->slots[cur].lookup_next;
    }
}

static void
ensure_slot_capacity(
    struct TRSPK_ModelArena* arena,
    uint32_t min_capacity)
{
    if( min_capacity <= arena->slot_capacity )
        return;

    uint32_t new_cap = arena->slot_capacity ? arena->slot_capacity * 2u : TRSPK_MODELARENA_INIT_SLOT_CAP;
    while( new_cap < min_capacity )
        new_cap *= 2u;

    struct TRSPK_ModelSlot* grown =
        (struct TRSPK_ModelSlot*)realloc(arena->slots, new_cap * sizeof(struct TRSPK_ModelSlot));
    assert(grown != NULL);
    arena->slots = grown;
    arena->slot_capacity = new_cap;
}

static uint32_t
alloc_slot(struct TRSPK_ModelArena* arena)
{
    if( arena->free_head != TRSPK_MODELSLOT_NULL_IDX )
    {
        const uint32_t idx = arena->free_head;
        arena->free_head = arena->slots[idx].next_free;
        return idx;
    }

    ensure_slot_capacity(arena, arena->slot_count + 1u);
    const uint32_t idx = arena->slot_count;
    arena->slot_count++;
    return idx;
}

static void
free_slot(
    struct TRSPK_ModelArena* arena,
    uint32_t slot_index)
{
    assert(slot_index < arena->slot_count);

    struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
    /* Before the flags are cleared: the bucket is derived from the keys,
     * and a dead slot must not stay reachable from the index. */
    lookup_remove(arena, slot_index);
    slot->flags = 0u;
    slot->next_free = arena->free_head;
    arena->free_head = slot_index;
}

static uint32_t
compute_used_vertex_extent(const struct TRSPK_ModelArena* arena)
{
    uint32_t extent = 0u;

    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[i];
        if( !trspk_modelslot_is_alive(slot) )
            continue;

        const uint32_t end = slot->vertex_base + slot->vertex_count;
        if( end > extent )
            extent = end;
    }

    return extent;
}

static void
discard_reclaimed_slot_ranges(struct TRSPK_ModelArena* arena)
{
    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        struct TRSPK_ModelSlot* slot = &arena->slots[i];
        if( trspk_modelslot_is_alive(slot) )
            continue;
        slot->vertex_base = UINT32_MAX;
        slot->vertex_count = 0u;
        slot->tri_start = 0u;
        slot->tri_count = 0u;
    }
}

static int
compare_slots_by_vertex_base(
    const void* a,
    const void* b)
{
    const struct TRSPK_ModelSlot* sa = *(const struct TRSPK_ModelSlot* const*)a;
    const struct TRSPK_ModelSlot* sb = *(const struct TRSPK_ModelSlot* const*)b;

    if( sa->vertex_base < sb->vertex_base )
        return -1;
    if( sa->vertex_base > sb->vertex_base )
        return 1;
    return 0;
}

struct TRSPK_ModelArena*
trspk_modelarena_create(
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t page_size,
    uint32_t initial_slot_capacity)
{
    assert(vbo != NULL);
    assert(triangles != NULL);
    assert(page_size > 0u);

    struct TRSPK_ModelArena* arena = (struct TRSPK_ModelArena*)malloc(sizeof(struct TRSPK_ModelArena));
    assert(arena != NULL);
    memset(arena, 0, sizeof(struct TRSPK_ModelArena));

    if( initial_slot_capacity == 0u )
        initial_slot_capacity = TRSPK_MODELARENA_INIT_SLOT_CAP;

    arena->slots = (struct TRSPK_ModelSlot*)calloc(
        initial_slot_capacity, sizeof(struct TRSPK_ModelSlot));
    assert(arena->slots != NULL);

    arena->slot_capacity = initial_slot_capacity;
    arena->free_head = TRSPK_MODELSLOT_NULL_IDX;
    arena->page_size = page_size;
    arena->vbo = vbo;
    arena->tri = triangles;

    return arena;
}

struct TRSPK_ModelArena*
trspk_modelarena_create_chain16(
    struct TRSPK_VBOChain16* chain,
    struct TRSPK_Triangles* triangles,
    uint32_t initial_slot_capacity)
{
    assert(chain != NULL);

    struct TRSPK_ModelArena* arena = (struct TRSPK_ModelArena*)malloc(sizeof(struct TRSPK_ModelArena));
    assert(arena != NULL);
    memset(arena, 0, sizeof(struct TRSPK_ModelArena));

    if( initial_slot_capacity == 0u )
        initial_slot_capacity = TRSPK_MODELARENA_INIT_SLOT_CAP;

    arena->slots = (struct TRSPK_ModelSlot*)calloc(
        initial_slot_capacity, sizeof(struct TRSPK_ModelSlot));
    assert(arena->slots != NULL);

    arena->slot_capacity = initial_slot_capacity;
    arena->free_head = TRSPK_MODELSLOT_NULL_IDX;
    arena->vbo_chain = chain;
    arena->tri = triangles;

    return arena;
}

void
trspk_modelarena_free(struct TRSPK_ModelArena* arena)
{
    if( arena == NULL )
        return;

    free(arena->lookup_buckets);
    free(arena->slots);
    free(arena);
}

uint32_t
trspk_modelarena_load(
    struct TRSPK_ModelArena* arena,
    int element_id,
    int pose_id,
    uint32_t vertex_count)
{
    assert(arena != NULL);
    assert(element_id >= 0);
    assert(pose_id >= 0);
    assert(vertex_count > 0u);
    assert((vertex_count % 3u) == 0u);

    const uint32_t tri_count = vertex_count / 3u;
    const uint32_t slot_index = alloc_slot(arena);
    struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];

    if( arena->vbo_chain != NULL )
    {
        struct TRSPK_VBOChain16PushResult pushed =
            trspk_vbochain16_push(arena->vbo_chain, vertex_count);

        slot->vertex_base = pushed.base_vertex;
        slot->page = arena->vbo_chain->current_page;
        slot->tri_start = 0u;
    }
    else
    {
        assert(arena->vbo != NULL);

        uint32_t base = align_vertex_base(arena->write_cursor, vertex_count, arena->page_size);
        if( base > arena->write_cursor )
            trspk_vbo_growby(arena->vbo, base - arena->write_cursor);

        trspk_vbo_growby(arena->vbo, vertex_count);

        const uint32_t tri_start = base / 3u;
        if( arena->tri != NULL )
            trspk_triangles_ensure(arena->tri, tri_start + tri_count);

        slot->vertex_base = base;
        slot->page = 0u;
        slot->tri_start = tri_start;

        arena->write_cursor = base + vertex_count;
    }

    slot->vertex_count = vertex_count;
    slot->tri_count = tri_count;
    slot->element_id = element_id;
    slot->pose_id = pose_id;
    slot->flags = TRSPK_MODELSLOT_FLAG_ALIVE;
    slot->next_free = TRSPK_MODELSLOT_NULL_IDX;
    slot->lookup_next = TRSPK_MODELSLOT_NULL_IDX;
    lookup_insert(arena, slot_index);

    return slot_index;
}

void
trspk_modelarena_unload(
    struct TRSPK_ModelArena* arena,
    uint32_t slot_index)
{
    assert(arena != NULL);
    assert(slot_index < arena->slot_count);

    struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
    if( !trspk_modelslot_is_alive(slot) )
        return;

    free_slot(arena, slot_index);
}

void
trspk_modelarena_unload_element(
    struct TRSPK_ModelArena* arena,
    int element_id)
{
    assert(arena != NULL);
    assert(element_id >= 0);

    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        struct TRSPK_ModelSlot* slot = &arena->slots[i];
        if( trspk_modelslot_is_alive(slot) && slot->element_id == element_id )
            free_slot(arena, i);
    }
}

const struct TRSPK_ModelSlot*
trspk_modelarena_get(
    const struct TRSPK_ModelArena* arena,
    uint32_t slot_index)
{
    assert(arena != NULL);
    if( slot_index >= arena->slot_count )
        return NULL;

    return &arena->slots[slot_index];
}

uint32_t
trspk_modelarena_find(
    const struct TRSPK_ModelArena* arena,
    int element_id,
    int pose_id)
{
    assert(arena != NULL);

    if( arena->lookup_bucket_count == 0u )
        return TRSPK_MODELSLOT_NULL_IDX;

    {
        const uint32_t bucket = lookup_hash(element_id, pose_id) &
            (arena->lookup_bucket_count - 1u);
        uint32_t cur = arena->lookup_buckets[bucket];

        while( cur != TRSPK_MODELSLOT_NULL_IDX )
        {
            const struct TRSPK_ModelSlot* slot = &arena->slots[cur];
            if( trspk_modelslot_is_alive(slot) && slot->element_id == element_id &&
                slot->pose_id == pose_id )
                return cur;
            cur = slot->lookup_next;
        }
    }

    return TRSPK_MODELSLOT_NULL_IDX;
}

void
trspk_modelarena_clear(struct TRSPK_ModelArena* arena)
{
    assert(arena != NULL);

    arena->slot_count = 0u;
    arena->free_head = TRSPK_MODELSLOT_NULL_IDX;
    arena->write_cursor = 0u;

    /* Every bucket head names a slot that no longer exists. Leaving them
     * would let find hand back a slot index from the previous scene. */
    {
        uint32_t i;
        for( i = 0u; i < arena->lookup_bucket_count; ++i )
            arena->lookup_buckets[i] = TRSPK_MODELSLOT_NULL_IDX;
    }

    if( arena->vbo_chain != NULL )
        trspk_vbochain16_reset(arena->vbo_chain);
    else if( arena->vbo != NULL )
        trspk_vbo_reset(arena->vbo);
}

struct TRSPK_ModelArenaGCResult
trspk_modelarena_gc(struct TRSPK_ModelArena* arena)
{
    assert(arena != NULL);

    struct TRSPK_ModelArenaGCResult result = { 0u, false };

    if( arena->vbo_chain != NULL )
        return result;

    if( arena->slot_count == 0u )
        return result;

    const uint32_t live_extent = compute_used_vertex_extent(arena);
    if( live_extent == 0u )
    {
        trspk_modelarena_clear(arena);
        return result;
    }

    /* Replacing the most recently baked pose is the common retained-renderer
     * path.  Reclaim that dead tail without allocating or sorting the entire
     * arena.  A dead slot below the live extent is a real interior hole and
     * needs the full compaction below. */
    bool has_interior_hole = false;
    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[i];
        if( !trspk_modelslot_is_alive(slot) && slot->vertex_base < live_extent )
        {
            has_interior_hole = true;
            break;
        }
    }
    if( !has_interior_hole )
    {
        arena->write_cursor = live_extent;
        if( arena->vbo->vertex_count > live_extent )
            arena->vbo->vertex_count = live_extent;
        discard_reclaimed_slot_ranges(arena);
        return result;
    }

    struct TRSPK_ModelSlot** alive = (struct TRSPK_ModelSlot**)malloc(
        arena->slot_count * sizeof(struct TRSPK_ModelSlot*));
    assert(alive != NULL);

    uint32_t alive_count = 0u;
    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        if( trspk_modelslot_is_alive(&arena->slots[i]) )
            alive[alive_count++] = &arena->slots[i];
    }

    assert(alive_count > 0u);

    qsort(alive, alive_count, sizeof(struct TRSPK_ModelSlot*), compare_slots_by_vertex_base);

    uint32_t write_cursor = 0u;

    for( uint32_t i = 0u; i < alive_count; ++i )
    {
        struct TRSPK_ModelSlot* slot = alive[i];
        const uint32_t compacted_base = align_vertex_base(
            write_cursor, slot->vertex_count, arena->page_size);

        if( slot->vertex_base != compacted_base )
        {
            move_vertices(
                arena->vbo,
                compacted_base,
                slot->vertex_base,
                slot->vertex_count);

            const uint32_t dst_tri = compacted_base / 3u;
            move_triangles(arena->tri, dst_tri, slot->tri_start, slot->tri_count);

            slot->vertex_base = compacted_base;
            slot->tri_start = dst_tri;
            result.relocated_count++;
            result.did_compact = true;
        }

        write_cursor = compacted_base + slot->vertex_count;
    }

    free(alive);

    arena->write_cursor = write_cursor;
    if( arena->vbo->vertex_count > write_cursor )
        arena->vbo->vertex_count = write_cursor;

    if( result.did_compact )
        trspk_vbo_set_dirty(arena->vbo);

    discard_reclaimed_slot_ranges(arena);

    return result;
}

void
trspk_modelarena_rebuild(
    struct TRSPK_ModelArena* arena,
    TRSPK_ModelArenaRebuildFn rebuild_fn,
    void* user_data)
{
    assert(arena != NULL);
    assert(rebuild_fn != NULL);

    struct TRSPK_ModelSlot* saved = NULL;
    uint32_t saved_count = 0u;

    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        if( trspk_modelslot_is_alive(&arena->slots[i]) )
            saved_count++;
    }

    if( saved_count == 0u )
    {
        trspk_modelarena_clear(arena);
        return;
    }

    saved = (struct TRSPK_ModelSlot*)malloc(saved_count * sizeof(struct TRSPK_ModelSlot));
    assert(saved != NULL);

    uint32_t write = 0u;
    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        if( trspk_modelslot_is_alive(&arena->slots[i]) )
            saved[write++] = arena->slots[i];
    }

    trspk_modelarena_clear(arena);

    for( uint32_t i = 0u; i < saved_count; ++i )
    {
        const uint32_t slot_index = trspk_modelarena_load(
            arena,
            saved[i].element_id,
            saved[i].pose_id,
            saved[i].vertex_count);
        rebuild_fn(arena, slot_index, user_data);
    }

    free(saved);
}

void
trspk_modelarena_visit_alive(
    const struct TRSPK_ModelArena* arena,
    TRSPK_ModelArenaVisitFn visit_fn,
    void* user_data)
{
    assert(arena != NULL);
    assert(visit_fn != NULL);

    for( uint32_t i = 0u; i < arena->slot_count; ++i )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[i];
        if( trspk_modelslot_is_alive(slot) )
            visit_fn(slot, user_data);
    }
}
