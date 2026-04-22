#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

/**
 * GpuFontCache<TexH>
 *
 * Flat fixed-array cache for font atlas GPU textures (max 16 fonts, IDs 0-15).
 * Templated on TexH — the platform's texture handle type.
 *   Metal:  GpuFontCache<void*>   (void* = bridge-retained id<MTLTexture>)
 *   OpenGL: GpuFontCache<unsigned> (GLuint texture name)
 *
 * Lookup is O(1) by font_id: slots_[font_id].
 *
 * Batch mode: multiple font atlases are shelf-packed into one CPU pixel buffer,
 * then the caller uploads a single GPU texture and calls set_batch_atlas_handle().
 * Each FontEntry in the batch records its UV sub-region within the shared atlas.
 *
 * Standalone mode: caller creates the GPU texture immediately and calls
 * register_font(). UVs are 0-1 for the full texture.
 *
 * GPU resource lifetime is the CALLER'S responsibility.
 */
template<typename TexH>
class GpuFontCache
{
public:
    static constexpr int kMaxFonts = 16;
    static constexpr int kMaxBatches = 8;

    struct FontEntry
    {
        TexH atlas_texture;
        float u0, v0, u1, v1; ///< UV sub-region within the atlas (0-1 for standalone)
        uint32_t batch_id;
        bool is_batched;
        bool loaded;
    };

    struct AtlasPixelData
    {
        uint8_t* pixels; ///< RGBA bytes, row-major; owned by the batch until set_batch_atlas_handle
        int w;
        int h;
    };

private:
    struct FontBatchState
    {
        uint32_t id;
        bool open;
        bool in_use;

        uint8_t* pixel_buf; ///< CPU RGBA accumulation buffer (malloc'd)
        int atlas_w;
        int atlas_h;

        // Shelf-packer state
        int shelf_x;
        int shelf_y;
        int shelf_h;

        int font_ids[kMaxFonts];
        int font_count;
    };

public:
    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    void
    init(TexH null_value)
    {
        null_ = null_value;
        for( int i = 0; i < kMaxFonts; ++i )
        {
            slots_[i].atlas_texture = null_value;
            slots_[i].u0 = slots_[i].v0 = 0.0f;
            slots_[i].u1 = slots_[i].v1 = 1.0f;
            slots_[i].batch_id = 0;
            slots_[i].is_batched = false;
            slots_[i].loaded = false;
        }
        for( int i = 0; i < kMaxBatches; ++i )
        {
            batches_[i].id = 0;
            batches_[i].open = false;
            batches_[i].in_use = false;
            batches_[i].pixel_buf = nullptr;
            batches_[i].atlas_w = 0;
            batches_[i].atlas_h = 0;
            batches_[i].shelf_x = 0;
            batches_[i].shelf_y = 0;
            batches_[i].shelf_h = 0;
            batches_[i].font_count = 0;
        }
    }

    void
    destroy()
    {
        for( int i = 0; i < kMaxBatches; ++i )
        {
            if( batches_[i].pixel_buf )
            {
                free(batches_[i].pixel_buf);
                batches_[i].pixel_buf = nullptr;
            }
        }
        for( int i = 0; i < kMaxFonts; ++i )
            slots_[i].atlas_texture = null_;
    }

    // ------------------------------------------------------------------
    // Batch load path
    // ------------------------------------------------------------------

    void
    begin_batch(
        uint32_t batch_id,
        int atlas_w,
        int atlas_h)
    {
        FontBatchState* slot = find_or_claim_slot();
        if( !slot )
            return;
        slot->id = batch_id;
        slot->open = true;
        slot->in_use = true;
        slot->atlas_w = atlas_w;
        slot->atlas_h = atlas_h;
        slot->shelf_x = 0;
        slot->shelf_y = 0;
        slot->shelf_h = 0;
        slot->font_count = 0;

        const size_t buf_bytes = (size_t)atlas_w * (size_t)atlas_h * 4u; // RGBA
        slot->pixel_buf = (uint8_t*)malloc(buf_bytes);
        if( slot->pixel_buf )
            memset(slot->pixel_buf, 0, buf_bytes);
    }

    /**
     * Shelf-pack one font glyph atlas into the batch.
     *
     * @param rgba_pixels  Source RGBA bytes, row-major (glyph_w * glyph_h * 4 bytes)
     * @param glyph_w / glyph_h  Dimensions of this font's atlas sub-image
     */
    void
    add_to_batch(
        uint32_t batch_id,
        int font_id,
        const uint8_t* rgba_pixels,
        int glyph_w,
        int glyph_h)
    {
        if( font_id < 0 || font_id >= kMaxFonts )
            return;
        if( !rgba_pixels || glyph_w <= 0 || glyph_h <= 0 )
            return;

        FontBatchState* batch = find_batch(batch_id);
        if( !batch || !batch->open || !batch->pixel_buf )
            return;

        // Advance shelf if atlas glyph block doesn't fit on current row
        if( batch->shelf_x + glyph_w > batch->atlas_w )
        {
            batch->shelf_y += batch->shelf_h;
            batch->shelf_x = 0;
            batch->shelf_h = 0;
        }
        if( batch->shelf_y + glyph_h > batch->atlas_h )
            return; // Out of vertical space

        // Blit RGBA rows
        for( int row = 0; row < glyph_h; ++row )
        {
            const uint8_t* src = rgba_pixels + (size_t)row * (size_t)glyph_w * 4u;
            uint8_t* dst =
                batch->pixel_buf +
                ((size_t)(batch->shelf_y + row) * (size_t)batch->atlas_w + (size_t)batch->shelf_x) *
                    4u;
            memcpy(dst, src, (size_t)glyph_w * 4u);
        }

        // Record UV sub-region
        const float fw = (float)batch->atlas_w;
        const float fh = (float)batch->atlas_h;
        FontEntry& entry = slots_[font_id];
        entry.atlas_texture = null_;
        entry.u0 = (float)batch->shelf_x / fw;
        entry.v0 = (float)batch->shelf_y / fh;
        entry.u1 = (float)(batch->shelf_x + glyph_w) / fw;
        entry.v1 = (float)(batch->shelf_y + glyph_h) / fh;
        entry.batch_id = batch_id;
        entry.is_batched = true;
        entry.loaded = true;

        if( batch->font_count < kMaxFonts )
            batch->font_ids[batch->font_count++] = font_id;

        if( glyph_h > batch->shelf_h )
            batch->shelf_h = glyph_h;
        batch->shelf_x += glyph_w;
    }

    /**
     * Finalize the batch: returns the packed pixel buffer for GPU upload.
     * The buffer remains valid until set_batch_atlas_handle() is called.
     */
    AtlasPixelData
    finalize_batch(uint32_t batch_id)
    {
        FontBatchState* batch = find_batch(batch_id);
        if( !batch || !batch->open )
            return { nullptr, 0, 0 };
        batch->open = false;
        return { batch->pixel_buf, batch->atlas_w, batch->atlas_h };
    }

    /**
     * Commit the GPU texture handle for the batch atlas.
     * Writes the handle into every FontEntry that belongs to this batch
     * and frees the CPU pixel buffer.
     */
    void
    set_batch_atlas_handle(
        uint32_t batch_id,
        TexH h)
    {
        FontBatchState* batch = find_batch(batch_id);
        if( !batch )
            return;
        for( int i = 0; i < batch->font_count; ++i )
        {
            const int fid = batch->font_ids[i];
            if( fid >= 0 && fid < kMaxFonts )
                slots_[fid].atlas_texture = h;
        }
        if( batch->pixel_buf )
        {
            free(batch->pixel_buf);
            batch->pixel_buf = nullptr;
        }
    }

    // ------------------------------------------------------------------
    // Standalone load path
    // ------------------------------------------------------------------

    void
    register_font(
        int font_id,
        TexH h)
    {
        if( font_id < 0 || font_id >= kMaxFonts )
            return;
        FontEntry& entry = slots_[font_id];
        entry.atlas_texture = h;
        entry.u0 = entry.v0 = 0.0f;
        entry.u1 = entry.v1 = 1.0f;
        entry.batch_id = 0;
        entry.is_batched = false;
        entry.loaded = true;
    }

    // ------------------------------------------------------------------
    // Lookup
    // ------------------------------------------------------------------

    FontEntry*
    get_font(int font_id)
    {
        if( font_id < 0 || font_id >= kMaxFonts )
            return nullptr;
        return slots_[font_id].loaded ? &slots_[font_id] : nullptr;
    }

    bool
    is_batch_open(uint32_t batch_id) const
    {
        const FontBatchState* b = find_batch_const(batch_id);
        return b && b->open;
    }

    // ------------------------------------------------------------------
    // Unload
    // ------------------------------------------------------------------

    /** Clear a single font slot. Caller must have released the GPU resource if standalone. */
    void
    unload_font(int font_id)
    {
        if( font_id < 0 || font_id >= kMaxFonts )
            return;
        FontEntry& entry = slots_[font_id];
        entry.atlas_texture = null_;
        entry.u0 = entry.v0 = 0.0f;
        entry.u1 = entry.v1 = 1.0f;
        entry.batch_id = 0;
        entry.is_batched = false;
        entry.loaded = false;
    }

    /**
     * Clear all font entries for the batch.
     * Caller must have released the shared GPU atlas texture BEFORE calling this.
     */
    void
    unload_batch(uint32_t batch_id)
    {
        FontBatchState* batch = find_batch(batch_id);
        if( !batch )
            return;
        for( int i = 0; i < batch->font_count; ++i )
            unload_font(batch->font_ids[i]);
        if( batch->pixel_buf )
        {
            free(batch->pixel_buf);
            batch->pixel_buf = nullptr;
        }
        batch->font_count = 0;
        batch->id = 0;
        batch->open = false;
        batch->in_use = false;
    }

private:
    FontBatchState*
    find_batch(uint32_t id)
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( batches_[i].in_use && batches_[i].id == id )
                return &batches_[i];
        return nullptr;
    }

    const FontBatchState*
    find_batch_const(uint32_t id) const
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( batches_[i].in_use && batches_[i].id == id )
                return &batches_[i];
        return nullptr;
    }

    FontBatchState*
    find_or_claim_slot()
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( !batches_[i].in_use )
                return &batches_[i];
        return nullptr;
    }

    FontEntry slots_[kMaxFonts];
    FontBatchState batches_[kMaxBatches];
    TexH null_;
};
