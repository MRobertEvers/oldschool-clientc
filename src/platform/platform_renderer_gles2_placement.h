#ifndef TORIRS_GLES2_PLACEMENT_H
#define TORIRS_GLES2_PLACEMENT_H
/* CPU-only retained-pose resolution, shared by renderer and real-chain replay. */
#include "platform/platform_renderer_gles2_core.h"
#include "toridraw_element_id.h"

#include <stdlib.h>
#include <string.h>

struct GLES2StaticPrimary
{
    uint32_t element_tag;
    uint32_t batch_slot, entry_index, page_id, page_base, vertex_base, vertex_count;
    uint32_t reserved;
};
_Static_assert(
    sizeof(struct GLES2StaticPrimary) == 32,
    "one half-line descriptor");

/* 0: no retained mapping (bake fallback); -1: invalid mapping (skip); 1: ready. */
static inline int
gles2_static_resolve_reference(
    const struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct GLES2StaticPrimary* out)
{
    uint32_t base;
    if( !trspk_pose_table_get(&renderer->batch_poses, element_id, anim_index, pose_id, &base) )
        return 0;
    if( !(base & GLES2_BATCH_POSE_FLAG) )
        return -1;
    uint32_t slot = (base >> GLES2_BATCH_POSE_SLOT_SHIFT) & GLES2_BATCH_POSE_SLOT_MASK;
    uint32_t index = base & GLES2_BATCH_POSE_ENTRY_MASK;
    if( slot >= renderer->static_batch_count )
        return -1;
    const struct GLES2StaticBatch* batch = &renderer->static_batches[slot];
    if( !batch->active || !batch->cpu )
        return -1;
    const struct TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(batch->cpu, index);
    if( !entry || entry->chunk_index >= batch->page_id_capacity )
        return -1;
    uint32_t page = batch->page_ids[entry->chunk_index];
    if( page >= renderer->static_page_count || !renderer->static_pages[page].valid )
        return -1;
    *out = (struct GLES2StaticPrimary){ (uint32_t)element_id + 1u,
                                        slot,
                                        index,
                                        page,
                                        renderer->static_pages[page].gpu_offset,
                                        entry->vertex_base,
                                        entry->vertex_count,
                                        0 };
    return 1;
}

static inline int
gles2_static_resolve(
    const struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct GLES2StaticPrimary* out)
{
    if( renderer->static_primary_enabled && element_id >= 0 && anim_index == 0 && pose_id == 0 )
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
    return gles2_static_resolve_reference(renderer, element_id, anim_index, pose_id, out);
}

/* Called after every complete batch-pose rebuild. Indices/values survive
 * array relocation; page compaction must finish before this snapshot. */
static inline void
gles2_static_primary_rebuild(struct ToriRS_GLES2* renderer)
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
    if( !renderer->static_primary_enabled )
        return;
    uint32_t want = renderer->batch_poses.element_count;
    if( want > renderer->static_primary_capacity )
    {
        struct GLES2StaticPrimary* grown = calloc(want, sizeof(*grown));
        uint32_t* bits = calloc(((size_t)want + 31) / 32, 4);
        if( !grown || !bits )
        {
            free(grown);
            free(bits);
            return;
        }
        free(renderer->static_primary);
        free(renderer->static_primary_bits);
        renderer->static_primary = grown;
        renderer->static_primary_bits = bits;
        renderer->static_primary_capacity = want;
    }
    /* Iterate final mappings, preserving the reference table's last writer
     * even when two entries name the same raw element index. */
    for( uint32_t slot = 0; slot < renderer->static_batch_count; slot++ )
    {
        const struct GLES2StaticBatch* batch = &renderer->static_batches[slot];
        if( !batch->active || !batch->cpu )
            continue;
        uint32_t count = trspk_batch16_entry_count(batch->cpu);
        for( uint32_t i = 0; i < count; i++ )
        {
            const struct TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(batch->cpu, i);
            if( entry->element_id < 0 || entry->anim_index != 0 || entry->pose_id != 0 )
                continue;
            uint32_t index = (uint32_t)ToriDraw_ElementIndexOfRaw(entry->element_id);
            if( index >= renderer->static_primary_capacity ||
                index >= renderer->batch_poses.element_count || slot > GLES2_BATCH_POSE_SLOT_MASK ||
                i > GLES2_BATCH_POSE_ENTRY_MASK )
                continue;
            const struct TRSPK_PoseElement* pose = &renderer->batch_poses.elements[index];
            if( pose->tracks[0].pose_count != 1 || pose->tracks[1].pose_count != 0 ||
                pose->tracks[0].vertex_base[0] !=
                    (GLES2_BATCH_POSE_FLAG | (slot << GLES2_BATCH_POSE_SLOT_SHIFT) | i) )
                continue;
            if( entry->chunk_index >= batch->page_id_capacity )
                continue;
            uint32_t page = batch->page_ids[entry->chunk_index];
            if( page >= renderer->static_page_count || !renderer->static_pages[page].valid )
                continue;
            renderer->static_primary[index] =
                (struct GLES2StaticPrimary){ (uint32_t)entry->element_id + 1u,
                                             slot,
                                             i,
                                             page,
                                             renderer->static_pages[page].gpu_offset,
                                             entry->vertex_base,
                                             entry->vertex_count,
                                             0 };
            renderer->static_primary_bits[index >> 5] |= 1u << (index & 31);
        }
    }
}
/* The descriptor was prefetched at +3. At +2/+1 it can suppress the
 * original dependent prefetch ladder for primary-only retained elements. */
static inline bool
gles2_primary_prefetch_complete(
    const struct ToriRS_GLES2* r,
    int id)
{
    if( !r->static_primary_enabled || id < 0 )
        return false;
    uint32_t i = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
    /* This is only a prefetch hint. Resolution still checks the full raw ID.
     * The dense bitset is ~4 KiB for 33k elements, rather than a cold descriptor
     * load just to choose which address should be prefetched. */
    return i < r->static_primary_capacity && (r->static_primary_bits[i >> 5] & (1u << (i & 31)));
}
static inline void
gles2_static_prefetch_ids(
    struct ToriRS_GLES2* renderer,
    int id_plus1,
    int id_plus2,
    int id_plus3)
{
    const struct TRSPK_PoseTable* table = &renderer->batch_poses;
    int id;

    assert(renderer);
    if( !table->elements || !renderer->has_3d )
        return;

    if( gles2_primary_prefetch_complete(renderer, id_plus3) )
    {
        uint32_t index = (uint32_t)ToriDraw_ElementIndexOfRaw(id_plus3);
        if( index < renderer->static_primary_capacity )
            __builtin_prefetch(&renderer->static_primary[index], 0, 1);
    }
    id = id_plus3;
    if( id >= 0 && !gles2_primary_prefetch_complete(renderer, id) )
    {
        uint32_t const index = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
        if( index < table->element_count )
            __builtin_prefetch(&table->elements[index], 0, 1);
    }
    id = id_plus2;
    if( id >= 0 && !gles2_primary_prefetch_complete(renderer, id) )
    {
        uint32_t const index = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
        if( index < table->element_count )
        {
            const struct TRSPK_PoseTrack* track = &table->elements[index].tracks[0];
            if( track->vertex_base )
                __builtin_prefetch(track->vertex_base, 0, 1);
        }
    }
    id = id_plus1;
    if( id >= 0 && !gles2_primary_prefetch_complete(renderer, id) )
    {
        uint32_t const index = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
        if( index < table->element_count )
        {
            const struct TRSPK_PoseTrack* track = &table->elements[index].tracks[0];
            if( track->vertex_base && track->pose_count > 0u )
            {
                uint32_t const base = track->vertex_base[0];
                if( base != TRSPK_POSE_VERTEX_BASE_INVALID && (base & GLES2_BATCH_POSE_FLAG) )
                {
                    uint32_t const slot =
                        (base >> GLES2_BATCH_POSE_SLOT_SHIFT) & GLES2_BATCH_POSE_SLOT_MASK;
                    if( slot < renderer->static_batch_count )
                    {
                        const struct GLES2StaticBatch* batch = &renderer->static_batches[slot];
                        if( batch->active && batch->cpu )
                        {
                            const struct TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(
                                batch->cpu, base & GLES2_BATCH_POSE_ENTRY_MASK);
                            if( entry )
                                __builtin_prefetch(entry, 0, 1);
                        }
                    }
                }
            }
        }
    }
}

#endif
