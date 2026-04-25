#pragma once

#include "platforms/common/buffered_2d_order.h"
#include "platforms/common/buffered_font_2d.h"
#include "platforms/common/buffered_sprite_2d.h"

struct ToriRSRenderCommand;

/**
 * Pass2DBuilder — frame-scoped accumulator for 2D sprite and font draws.
 *
 * Owns BufferedSprite2D, BufferedFont2D, and Buffered2DOrder internally so
 * that callers need not manage them directly.  The Submit functions wire
 * these into the backend render context and invoke the existing flush routines.
 *
 * Usage:
 *   Pass2DBuilder b;
 *   b.begin_pass();
 *   b.set_font(font_id, atlas_tex);
 *   b.append_glyph_quad(quad_floats);
 *   b.close_font_segment();
 *   b.enqueue_sprite(atlas_tex, six_verts, nullptr);
 *   Pass2DBuilderSubmitMetal(b, ctx);
 */
class Pass2DBuilder
{
public:
    void
    begin_pass();

    void
    enqueue_sprite(
        void* atlas_tex,
        const SpriteQuadVertex six_verts[6],
        const SpriteLogicalScissor* opt_scissor = nullptr);

    /**
     * Enqueue a sprite quad with inverse-rotation fragment constants (rotated blit path).
     * inv_rot must not be null when calling this overload.
     */
    void
    enqueue_sprite(
        void* atlas_tex,
        const SpriteQuadVertex six_verts[6],
        const SpriteLogicalScissor* opt_scissor,
        const SpriteInverseRotParams* inv_rot);

    /**
     * Compute clip-space quad from a TORIRS_GFX_DRAW_SPRITE command and enqueue it.
     *
     * atlas_tex    — pre-looked-up GPU texture handle for the sprite.
     * tex_w/tex_h  — pixel dimensions of the GPU texture (sprite dims for standalone,
     *                atlas dims for batched sprites).
     * atlas_u0/v0  — sub-atlas UV origin in [0,1] (0,0 for standalone sprites).
     * fbw/fbh      — framebuffer dimensions used for logical-pixel → NDC conversion.
     *
     * No-op if the command sprite pointer is null, dimensions are invalid, or atlas_tex
     * is null.
     */
    void
    enqueue_sprite_from_draw(
        const struct ToriRSRenderCommand* cmd,
        void* atlas_tex,
        float tex_w,
        float tex_h,
        float atlas_u0,
        float atlas_v0,
        float fbw,
        float fbh);

    /**
     * Expand a TORIRS_GFX_DRAW_FONT command into glyph quads and append them.
     *
     * atlas_tex — pre-looked-up GPU texture handle for the font atlas.
     * fbw/fbh   — framebuffer dimensions for logical-pixel → NDC conversion.
     *
     * Calls set_font(), appends each visible glyph via append_glyph_quad(), then
     * calls close_font_segment() — equivalent to what the Metal renderer does inside
     * metal_frame_event_font_draw().
     *
     * No-op if the command font pointer is null, has no atlas, or atlas_tex is null.
     */
    void
    append_font_draw(
        const struct ToriRSRenderCommand* cmd,
        void* atlas_tex,
        float fbw,
        float fbh);

    void
    set_font(
        int font_id,
        void* atlas_tex);

    void
    append_glyph_quad(const float vq[48]);

    void
    close_font_segment();

    BufferedSprite2D&
    sprites();

    BufferedFont2D&
    fonts();

    Buffered2DOrder&
    order();

    bool
    empty() const;

private:
    BufferedSprite2D bsp2d_;
    BufferedFont2D bft2d_;
    Buffered2DOrder b2d_order_;

    /** After a font draw, next sprite enqueue starts a new group. */
    bool split_sprite_ = false;
    /** After a sprite draw, next set_font starts a new font segment. */
    bool split_font_ = false;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

/** Reset all internal buffers; call at the start of each frame/pass. */
inline void
Pass2DBuilder::begin_pass()
{
    bsp2d_.begin_pass();
    bft2d_.begin_pass();
    b2d_order_.begin_pass();
    split_sprite_ = false;
    split_font_ = false;
}

/**
 * Enqueue a sprite quad (6 vertices for 2 triangles).
 * See BufferedSprite2D::enqueue for full semantics.
 */
inline void
Pass2DBuilder::enqueue_sprite(
    void* atlas_tex,
    const SpriteQuadVertex six_verts[6],
    const SpriteLogicalScissor* opt_scissor)
{
    bsp2d_.enqueue(atlas_tex, six_verts, opt_scissor, &b2d_order_, &split_sprite_);
    split_font_ = true;
}

inline void
Pass2DBuilder::enqueue_sprite(
    void* atlas_tex,
    const SpriteQuadVertex six_verts[6],
    const SpriteLogicalScissor* opt_scissor,
    const SpriteInverseRotParams* inv_rot)
{
    bsp2d_.enqueue(atlas_tex, six_verts, opt_scissor, &b2d_order_, &split_sprite_, inv_rot);
    split_font_ = true;
}

/**
 * Begin or switch the active font group.
 * See BufferedFont2D::set_font for full semantics.
 */
inline void
Pass2DBuilder::set_font(
    int font_id,
    void* atlas_tex)
{
    bft2d_.set_font(font_id, atlas_tex, &b2d_order_, &split_font_);
    split_sprite_ = true;
}

/**
 * Append a pre-computed glyph quad (48 floats: 6 verts x 8 floats).
 * set_font must have been called first.
 */
inline void
Pass2DBuilder::append_glyph_quad(const float vq[48])
{
    bft2d_.append_glyph_quad(vq);
}

/**
 * Finalize the currently-open font segment into the groups list.
 * Call this before passing the builder to a Submit function so that
 * the final font group is included.
 */
inline void
Pass2DBuilder::close_font_segment()
{
    bft2d_.close_open_segment(&b2d_order_);
}

inline BufferedSprite2D&
Pass2DBuilder::sprites()
{
    return bsp2d_;
}

inline BufferedFont2D&
Pass2DBuilder::fonts()
{
    return bft2d_;
}

inline Buffered2DOrder&
Pass2DBuilder::order()
{
    return b2d_order_;
}

inline bool
Pass2DBuilder::empty() const
{
    return b2d_order_.empty();
}
