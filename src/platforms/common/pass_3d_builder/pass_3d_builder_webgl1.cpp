#include "platforms/common/pass_3d_builder/pass_3d_builder.h"
#include "platforms/webgl1/webgl1_internal.h"

/**
 * Pass3DBuilderSubmitWebGL1 — flush a Pass3DBuilder through the WebGL1 3D path.
 *
 * Temporarily wires the builder's BufferedFaceOrder into ctx->bfo3d, then
 * calls wg1_flush_3d to execute the draw stream.  The ctx pointer is
 * restored after the flush.
 *
 * wg1_flush_3d calls finalize_slices() internally and then resets the
 * face order via begin_pass(), so the builder's face order is cleared
 * after this call returns.
 */
void
Pass3DBuilderSubmitWebGL1(
    Pass3DBuilder& builder,
    WebGL1RenderCtx* ctx)
{
    if( !ctx || builder.empty() )
        return;

    // Save existing bfo3d pointer.
    BufferedFaceOrder* saved_bfo3d = ctx->bfo3d;

    // Wire builder's face order into ctx.
    ctx->bfo3d = &builder.face_order();

    // Flush via the existing 3D flush routine.
    wg1_flush_3d(ctx, ctx->bfo3d);

    // Restore ctx.
    ctx->bfo3d = saved_bfo3d;
}
