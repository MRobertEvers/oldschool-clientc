#pragma once

#include "platforms/common/buffered_2d_order.h"
#include "platforms/common/buffered_font_2d.h"
#include "platforms/common/buffered_sprite_2d.h"

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
 *   Pass2DBuilderSubmitMetal(b, ctx);   // or WebGL1 variant
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
