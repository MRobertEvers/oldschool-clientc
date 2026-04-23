// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/buffered_face_order.h"
#include "platforms/common/pass_3d_builder/pass_3d_builder2.h"
#include "platforms/gpu_3d_cache.h"
#include "platforms/metal/metal_internal.h"

namespace
{
constexpr int kMaxInstancesPerPass = BufferedFaceOrder::kMaxInstancesPerPass;

void
open_slice_if_needed(
    std::vector<PassFlushSlice>& slices,
    void*& cur_vbo,
    int& cur_chunk,
    int batch_chunk_index,
    void* static_vbo,
    uint32_t stream_pos)
{
    if( static_vbo == cur_vbo && batch_chunk_index == cur_chunk )
        return;
    if( !slices.empty() )
    {
        PassFlushSlice& back = slices.back();
        back.entry_count = stream_pos - back.entry_offset;
    }
    PassFlushSlice s;
    s.chunk_index = batch_chunk_index;
    s.vbo_handle = static_vbo;
    s.entry_offset = stream_pos;
    s.entry_count = 0;
    slices.push_back(s);
    cur_vbo = static_vbo;
    cur_chunk = batch_chunk_index;
}

void
finalize_slices(
    std::vector<PassFlushSlice>& slices,
    uint32_t stream_size,
    bool& finalized)
{
    if( finalized || slices.empty() )
        return;
    const uint32_t n = stream_size;
    for( size_t i = 0; i < slices.size(); ++i )
    {
        PassFlushSlice& s = slices[i];
        const uint32_t next_off = (i + 1 < slices.size()) ? slices[i + 1].entry_offset : n;
        s.entry_count = next_off - s.entry_offset;
    }
    finalized = true;
}
} // namespace

/**
 * Expand Pass3DBuilder2 commands + indices_pool into one draw stream + instance buffer,
 * then upload with the same single memcpy + slice draw pattern as metal_flush_3d.
 */
void
Pass3DBuilder2SubmitMetal(
    Pass3DBuilder2& builder,
    MetalRenderCtx* ctx,
    Gpu3DCache<void*>* cache)
{
    if( !ctx || !ctx->encoder || !ctx->renderer || !builder.HasCommands() || !cache )
        return;
}
