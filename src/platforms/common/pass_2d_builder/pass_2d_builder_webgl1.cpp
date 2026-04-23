#include "platforms/common/pass_2d_builder/pass_2d_builder.h"
#include "platforms/webgl1/webgl1_internal.h"

/**
 * Pass2DBuilderSubmitWebGL1 — flush a Pass2DBuilder through the WebGL1 2D path.
 *
 * Temporarily wires the builder's BufferedSprite2D, BufferedFont2D, and
 * Buffered2DOrder into the WebGL1RenderCtx, then calls the existing flush
 * routine (wg1_flush_2d) which handles the interleaved sprite/font draw order.
 * The ctx pointers are restored to their original values after the flush,
 * leaving the render context in a consistent state for subsequent operations.
 *
 * close_font_segment() is called on the builder before flushing to ensure
 * the final open font group is included.
 */
void
Pass2DBuilderSubmitWebGL1(
    Pass2DBuilder& builder,
    WebGL1RenderCtx* ctx)
{
    if( !ctx || builder.empty() )
        return;

    // Finalize any open font segment.
    builder.close_font_segment();

    // Save existing ctx buffer pointers.
    BufferedSprite2D* saved_bsp2d = ctx->bsp2d;
    BufferedFont2D* saved_bft2d = ctx->bft2d;
    Buffered2DOrder* saved_b2d_order = ctx->b2d_order;
    bool saved_split_sprite = ctx->split_sprite_before_next_enqueue;
    bool saved_split_font = ctx->split_font_before_next_set_font;

    // Wire builder buffers into ctx.
    ctx->bsp2d = &builder.sprites();
    ctx->bft2d = &builder.fonts();
    ctx->b2d_order = &builder.order();
    ctx->split_sprite_before_next_enqueue = false;
    ctx->split_font_before_next_set_font = false;

    // Flush via the existing 2D flush routine (handles sprite/font interleave).
    wg1_flush_2d(ctx);

    // Restore ctx.
    ctx->bsp2d = saved_bsp2d;
    ctx->bft2d = saved_bft2d;
    ctx->b2d_order = saved_b2d_order;
    ctx->split_sprite_before_next_enqueue = saved_split_sprite;
    ctx->split_font_before_next_set_font = saved_split_font;
}
