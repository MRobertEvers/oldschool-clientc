#ifndef PLATFORM_ANDROIDARMV7_RENDERER_OPENGLES3_PLACEMENT_H
#define PLATFORM_ANDROIDARMV7_RENDERER_OPENGLES3_PLACEMENT_H

/*
 * androidarmv7_renderer_opengles3: the static-primary table is LIVE here.
 *
 * This was `renderer->static_primary_enabled` -- defaulted on for armv7
 * and off everywhere else, then asked once per query. It is a property
 * of the platform, and this renderer is the armv7 one, so the table
 * simply exists and the question is gone.
 */

/* CPU-only retained-pose resolution, shared by renderer and real-chain replay. */
#include "es3/es3_core.h"
#include "toridraw_element_id.h"
#include "mem/torirs_mem.h"

#include <stdlib.h>
#include <string.h>

struct ES3StaticPrimary
{
    uint32_t element_tag;
    uint32_t entry_index, vertex_base, vertex_count;
};
_Static_assert(
    sizeof(struct ES3StaticPrimary) == 16,
    "one quarter-line descriptor");

/* 0: no mapping in the world build (bake fallback); 1: ready. A detached
 * element -- reloaded, animated or removed since the build -- has none. */
static inline int
es3_static_resolve_reference(
    const struct TRSPK_Renderer_ES3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ES3StaticPrimary* out)
{
    const struct TRSPK_Batch32Entry* entry;
    if( !renderer->world_live || trspk_element_mask_test(&renderer->batch_detached, element_id) )
        return 0;
    entry = trspk_batch32_find_entry(renderer->world, element_id, anim_index, pose_id);
    if( !entry )
        return 0;
    *out = (struct ES3StaticPrimary){ (uint32_t)element_id + 1u,
                                         (uint32_t)(entry - trspk_batch32_get_entry(renderer->world, 0u)),
                                         entry->vertex_base,
                                         entry->vertex_count };
    return 1;
}

static inline int
es3_static_resolve(
    const struct TRSPK_Renderer_ES3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ES3StaticPrimary* out)
{
    if( element_id >= 0 && anim_index == 0 && pose_id == 0 )
    {
        uint32_t index = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
        if( index < renderer->static_primary_capacity &&
            (renderer->static_primary_bits[index >> 5] & (1u << (index & 31))) &&
            renderer->static_primary[index].element_tag == (uint32_t)element_id + 1u )
        {
            *out = renderer->static_primary[index];
            return 1;
        }
    }
    return es3_static_resolve_reference(renderer, element_id, anim_index, pose_id, out);
}

/* Called after every change to the world build: begin, end, clear. An element
 * gets a descriptor only when its sole pose in the build is the rest pose
 * (0, 0); an animated one resolves through the build's own lookup. A detach
 * clears its bit directly (es3_batch_detach). */
static inline void
es3_static_primary_rebuild(struct TRSPK_Renderer_ES3* renderer)
{
    renderer->static_resource_epoch++;
    if( renderer->static_primary_capacity )
    {
        /* The bitmap is the validity gate for queries and prefetch. Old
         * descriptor bytes cannot be consumed after these bits are cleared. */
        memset(
            renderer->static_primary_bits,
            0,
            ((size_t)renderer->static_primary_capacity + 31) / 32 * 4);
    }
    if( !renderer->world_live )
        return;
    uint32_t count = trspk_batch32_entry_count(renderer->world);
    uint32_t want = 0u;
    for( uint32_t i = 0; i < count; i++ )
    {
        const struct TRSPK_Batch32Entry* entry = trspk_batch32_get_entry(renderer->world, i);
        uint32_t index;
        if( entry->element_id < 0 )
            continue;
        index = (uint32_t)ToriDraw_ElementIndexOfRaw(entry->element_id);
        if( index + 1u > want )
            want = index + 1u;
    }
    if( want > renderer->static_primary_capacity )
    {
        struct ES3StaticPrimary* grown = ToriMem_Calloc(ToriMem_Renderer(), want, sizeof(*grown));
        uint32_t* bits = ToriMem_Calloc(ToriMem_Renderer(), ((size_t)want + 31) / 32, 4);
        assert(grown);
        assert(bits);
        ToriMem_Free(ToriMem_Renderer(), renderer->static_primary);
        ToriMem_Free(ToriMem_Renderer(), renderer->static_primary_bits);
        renderer->static_primary = grown;
        renderer->static_primary_bits = bits;
        renderer->static_primary_capacity = want;
    }
    /* Rest poses first, then every other pose withdraws its element's bit:
     * an element with more than one pose must take the full lookup. */
    for( uint32_t i = 0; i < count; i++ )
    {
        const struct TRSPK_Batch32Entry* entry = trspk_batch32_get_entry(renderer->world, i);
        uint32_t index;
        if( entry->element_id < 0 || entry->anim_index != 0 || entry->pose_id != 0 ||
            trspk_element_mask_test(&renderer->batch_detached, entry->element_id) )
            continue;
        index = (uint32_t)ToriDraw_ElementIndexOfRaw(entry->element_id);
        renderer->static_primary[index] = (struct ES3StaticPrimary){
            (uint32_t)entry->element_id + 1u, i, entry->vertex_base, entry->vertex_count
        };
        renderer->static_primary_bits[index >> 5] |= 1u << (index & 31);
    }
    for( uint32_t i = 0; i < count; i++ )
    {
        const struct TRSPK_Batch32Entry* entry = trspk_batch32_get_entry(renderer->world, i);
        uint32_t index;
        if( entry->element_id < 0 || (entry->anim_index == 0 && entry->pose_id == 0) )
            continue;
        index = (uint32_t)ToriDraw_ElementIndexOfRaw(entry->element_id);
        renderer->static_primary_bits[index >> 5] &= ~(1u << (index & 31));
    }
}
/* The descriptor was prefetched at +3. */
static inline bool
es3_primary_prefetch_complete(
    const struct TRSPK_Renderer_ES3* r,
    int id)
{
    if( id < 0 )
        return false;
    uint32_t i = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
    /* This is only a prefetch hint. Resolution still checks the full raw ID.
     * The dense bitset is ~4 KiB for 33k elements, rather than a cold descriptor
     * load just to choose which address should be prefetched. */
    return i < r->static_primary_capacity && (r->static_primary_bits[i >> 5] & (1u << (i & 31)));
}
/* Prefetch the world-build descriptors of the next draws. Only the primary
 * table is a dense array to prefetch into; an element without a descriptor
 * resolves through the build's hash lookup when its draw arrives. */
static inline void
es3_static_prefetch_ids(
    struct TRSPK_Renderer_ES3* renderer,
    int id_plus1,
    int id_plus2,
    int id_plus3)
{
    assert(renderer);
    (void)id_plus1;
    (void)id_plus2;
    if( !renderer->world_live || !renderer->has_3d )
        return;
    if( es3_primary_prefetch_complete(renderer, id_plus3) )
    {
        uint32_t index = (uint32_t)ToriDraw_ElementIndexOfRaw(id_plus3);
        if( index < renderer->static_primary_capacity )
            __builtin_prefetch(&renderer->static_primary[index], 0, 1);
    }
}

#endif
