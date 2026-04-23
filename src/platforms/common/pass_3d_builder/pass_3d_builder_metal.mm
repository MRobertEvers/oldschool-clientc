// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/pass_3d_builder/pass_3d_builder.h"
#include "platforms/metal/metal_internal.h"

/**
 * Pass3DBuilderSubmitMetal — flush a Pass3DBuilder through the Metal 3D path.
 *
 * Temporarily wires the builder's BufferedFaceOrder into ctx->bfo3d, then
 * calls metal_flush_3d to execute the draw stream.  The ctx pointer is
 * restored after the flush.
 *
 * The BufferedFaceOrder is finalized (finalize_slices called internally by
 * metal_flush_3d) before drawing.
 */
void
Pass3DBuilderSubmitMetal(
    Pass3DBuilder& builder,
    MetalRenderCtx* ctx)
{
    if( !ctx || !ctx->encoder || builder.empty() )
        return;

    // Save existing bfo3d pointer.
    BufferedFaceOrder* saved_bfo3d = ctx->bfo3d;

    // Wire builder's face order into ctx.
    ctx->bfo3d = &builder.face_order();

    // Flush via the existing 3D flush routine.
    metal_flush_3d(ctx, ctx->bfo3d);

    // Restore ctx.
    ctx->bfo3d = saved_bfo3d;
}
