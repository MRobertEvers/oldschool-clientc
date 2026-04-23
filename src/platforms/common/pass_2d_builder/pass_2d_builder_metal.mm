// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/pass_2d_builder/pass_2d_builder.h"
#include "platforms/metal/metal_internal.h"

/**
 * Pass2DBuilderSubmitMetal — flush a Pass2DBuilder through the Metal 2D path.
 *
 * Temporarily wires the builder's BufferedSprite2D, BufferedFont2D, and
 * Buffered2DOrder into the MetalRenderCtx, then calls the existing flush
 * routines (metal_flush_2d_sprites_only / metal_flush_2d_fonts) in the order
 * recorded by the builder's Buffered2DOrder.  The ctx pointers are restored
 * to their original values after the flush, leaving the render context in a
 * consistent state for subsequent operations.
 *
 * close_font_segment() is called on the builder before flushing to ensure
 * the final open font group is included.
 */
void
Pass2DBuilderSubmitMetal(
    Pass2DBuilder& builder,
    MetalRenderCtx* ctx)
{
    if( !ctx || !ctx->encoder || builder.empty() )
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

    // Flush in recorded order.
    for( const Tori2DFlushOp& op : builder.order().ops() )
    {
        if( op.kind == Tori2DFlushKind::Sprite )
            metal_flush_2d_sprites_only(ctx);
        else
            metal_flush_2d_fonts(ctx);
    }

    // Restore ctx.
    ctx->bsp2d = saved_bsp2d;
    ctx->bft2d = saved_bft2d;
    ctx->b2d_order = saved_b2d_order;
    ctx->split_sprite_before_next_enqueue = saved_split_sprite;
    ctx->split_font_before_next_set_font = saved_split_font;
}
