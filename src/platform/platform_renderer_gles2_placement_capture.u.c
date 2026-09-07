/* Optional capture of one real frame's retained-placement graph and calls. */
#include "../../tools/perf/placement_chain_format.h"
static struct
{
    FILE* file;
    unsigned frame, epoch, count;
    bool initialized, done;
    const char* path;
} placement_capture;
static void
placement_write(
    const void* p,
    size_t n)
{
    if( n && fwrite(p, 1, n, placement_capture.file) != n )
    {
        perror("placement capture");
        abort();
    }
}
static void
placement_capture_open(const struct ToriRS_GLES2* r)
{
    placement_capture.file = fopen(placement_capture.path, "wb");
    if( !placement_capture.file )
    {
        perror("placement capture open");
        abort();
    }
    placement_capture.epoch = r->static_resource_epoch;
    struct PlacementChainHeader h = { PLACEMENT_CHAIN_MAGIC,   2,
                                      placement_capture.frame, r->batch_poses.element_count,
                                      r->static_batch_count,   r->static_page_count,
                                      r->static_resource_epoch };
    placement_write(&h, sizeof(h));
    for( uint32_t e = 0; e < h.elements; e++ )
        for( unsigned a = 0; a < TRSPK_POSE_TRACK_COUNT; a++ )
        {
            const struct TRSPK_PoseTrack* t = &r->batch_poses.elements[e].tracks[a];
            uint32_t shape[] = { t->pose_count, t->pose_cap };
            placement_write(shape, sizeof(shape));
            placement_write(t->vertex_base, (size_t)t->pose_count * 4);
        }
    for( uint32_t i = 0; i < h.batches; i++ )
    {
        const struct GLES2StaticBatch* b = &r->static_batches[i];
        struct PlacementChainBatch bh = {
            b->active, b->cpu != NULL, trspk_batch16_entry_count(b->cpu), b->page_id_capacity
        };
        placement_write(&bh, sizeof(bh));
        for( uint32_t e = 0; e < bh.entries; e++ )
            placement_write(trspk_batch16_get_entry(b->cpu, e), sizeof(struct TRSPK_Batch16Entry));
        placement_write(b->page_ids, (size_t)bh.page_capacity * 4);
    }
    for( uint32_t i = 0; i < h.pages; i++ )
    {
        struct PlacementChainPage page = { r->static_pages[i].valid,
                                           r->static_pages[i].gpu_offset };
        placement_write(&page, sizeof(page));
    }
}
static bool
gles2_placement_capture_wanted(const struct ToriRS_GLES2* r)
{
    if( !placement_capture.initialized )
    {
        placement_capture.initialized = true;
        placement_capture.path = getenv("TORIRS_PLACEMENT_CAPTURE");
        const char* value = getenv("TORIRS_PLACEMENT_CAPTURE_FRAME");
        placement_capture.frame = value ? (unsigned)atoi(value) : 120;
    }
    if( !placement_capture.path || placement_capture.done ||
        r->frame_clock != placement_capture.frame )
        return false;
    if( !placement_capture.file )
        placement_capture_open(r);
    if( placement_capture.epoch != r->static_resource_epoch )
    {
        fprintf(stderr, "placement capture: resource graph changed inside frame\n");
        abort();
    }
    return true;
}
static void
gles2_placement_prefetch_record(
    const struct ToriRS_GLES2* r,
    int a,
    int b,
    int c)
{
    if( !r->has_3d || !gles2_placement_capture_wanted(r) )
        return;
    struct PlacementChainCall call = {
        .magic = PLACEMENT_CHAIN_PREFETCH, .element_id = a, .anim_index = b, .pose_id = c
    };
    placement_write(&call, sizeof(call));
    placement_capture.count++;
}
static int
gles2_static_resolve_recorded(
    const struct ToriRS_GLES2* r,
    int element_id,
    int anim_index,
    int pose_id,
    struct GLES2StaticPrimary* out)
{
    int result = gles2_static_resolve(r, element_id, anim_index, pose_id, out);
    if( !gles2_placement_capture_wanted(r) )
        return result;
    struct PlacementChainCall call = { .magic = PLACEMENT_CHAIN_CALL,
                                       .element_id = element_id,
                                       .anim_index = anim_index,
                                       .pose_id = pose_id,
                                       .result = result };
    if( result == 1 )
        call.placement = *out;
    placement_write(&call, sizeof(call));
    placement_capture.count++;
    return result;
}
static void
gles2_placement_capture_end(void)
{
    if( !placement_capture.file )
        return;
    uint32_t end[] = { PLACEMENT_CHAIN_END, placement_capture.count };
    placement_write(end, sizeof(end));
    if( fclose(placement_capture.file) )
        abort();
    placement_capture.file = NULL;
    placement_capture.done = true;
    fprintf(stderr, "placement capture complete: %u calls\n", placement_capture.count);
}
