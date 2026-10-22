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
placement_capture_open(const struct TRSPK_Renderer_ES3* r)
{
    placement_capture.file = fopen(placement_capture.path, "wb");
    if( !placement_capture.file )
    {
        perror("placement capture open");
        abort();
    }
    placement_capture.epoch = r->static_resource_epoch;
    /* Version 3: the world build is one Batch32, so the graph is its entries
     * -- no batch slots, no page table. (This capture has not compiled since
     * it was split from the GLES2 one: PlacementChainCall carries a
     * GLES2StaticPrimary. Kept describing the live graph regardless.) */
    struct PlacementChainHeader h = { PLACEMENT_CHAIN_MAGIC,
                                      3,
                                      placement_capture.frame,
                                      trspk_batch32_entry_count(r->world),
                                      r->world_live ? 1u : 0u,
                                      0u,
                                      r->static_resource_epoch };
    placement_write(&h, sizeof(h));
    for( uint32_t e = 0; e < h.elements; e++ )
        placement_write(trspk_batch32_get_entry(r->world, e), sizeof(struct TRSPK_Batch32Entry));
}
static bool
es3_placement_capture_wanted(const struct TRSPK_Renderer_ES3* r)
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
es3_placement_prefetch_record(
    const struct TRSPK_Renderer_ES3* r,
    int a,
    int b,
    int c)
{
    if( !r->has_3d || !es3_placement_capture_wanted(r) )
        return;
    struct PlacementChainCall call = {
        .magic = PLACEMENT_CHAIN_PREFETCH, .element_id = a, .anim_index = b, .pose_id = c
    };
    placement_write(&call, sizeof(call));
    placement_capture.count++;
}
static int
es3_static_resolve_recorded(
    const struct TRSPK_Renderer_ES3* r,
    int element_id,
    int anim_index,
    int pose_id,
    struct ES3StaticPrimary* out)
{
    int result = es3_static_resolve(r, element_id, anim_index, pose_id, out);
    if( !es3_placement_capture_wanted(r) )
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
es3_placement_capture_end(void)
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
