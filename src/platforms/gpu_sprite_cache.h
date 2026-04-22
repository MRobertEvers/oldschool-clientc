#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

/**
 * GpuSpriteCache<TexH>
 *
 * Flat 2D array cache for UI sprite GPU textures.
 * Templated on TexH — the platform's texture handle type.
 *   Metal:  GpuSpriteCache<void*>   (void* = bridge-retained id<MTLTexture>)
 *   OpenGL: GpuSpriteCache<unsigned> (GLuint texture name)
 *
 * Lookup is O(1): entries_[element_id * max_atlas_per_element + atlas_index].
 *
 * Batch mode (e.g. cache_media): sprites are shelf-packed into a single CPU
 * pixel buffer, then the caller uploads one GPU texture and calls
 * set_batch_atlas_handle(). All entries in the batch then share that handle,
 * each with their own UV sub-region.
 *
 * Standalone mode: caller creates the GPU texture immediately and calls
 * register_standalone(). UVs are 0-1 for the full texture.
 *
 * GPU resource lifetime is the CALLER'S responsibility.
 */
template<typename TexH>
class GpuSpriteCache
{
public:
    static constexpr int kMaxBatches = 16;

    struct SpriteEntry
    {
        TexH atlas_texture;
        float u0, v0, u1, v1;
        uint32_t batch_id;
        bool is_batched;
        bool loaded;
    };

    struct AtlasPixelData
    {
        uint32_t* pixels; ///< ARGB, row-major; owned by the batch until set_batch_atlas_handle
        int w;
        int h;
    };

private:
    struct SpriteBatchState
    {
        uint32_t id;
        bool open;
        bool in_use;

        uint32_t* pixel_buf; ///< CPU ARGB accumulation buffer (malloc'd)
        int atlas_w;
        int atlas_h;

        // Shelf-packer state
        int shelf_x;
        int shelf_y;
        int shelf_h;

        // Which entries belong to this batch (for set_batch_atlas_handle / unload_batch)
        std::vector<int> entry_indices; ///< flat index = element_id * max_atlas_ + atlas_index
    };

public:
    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    void
    init(
        int element_capacity,
        int max_atlas_per_element,
        TexH null_value)
    {
        null_ = null_value;
        max_atlas_ = max_atlas_per_element;
        capacity_ = element_capacity;
        entries_ = new SpriteEntry[element_capacity * max_atlas_per_element]();
        for( int i = 0; i < element_capacity * max_atlas_per_element; ++i )
        {
            entries_[i].atlas_texture = null_value;
            entries_[i].u0 = entries_[i].v0 = 0.0f;
            entries_[i].u1 = entries_[i].v1 = 1.0f;
            entries_[i].batch_id = 0;
            entries_[i].is_batched = false;
            entries_[i].loaded = false;
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
        delete[] entries_;
        entries_ = nullptr;
        capacity_ = 0;
        max_atlas_ = 0;
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
        SpriteBatchState* slot = find_or_claim_slot();
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
        slot->entry_indices.clear();

        const size_t buf_bytes = (size_t)atlas_w * (size_t)atlas_h * sizeof(uint32_t);
        slot->pixel_buf = (uint32_t*)malloc(buf_bytes);
        if( slot->pixel_buf )
            memset(slot->pixel_buf, 0, buf_bytes);
    }

    /**
     * Shelf-pack one sprite into the batch atlas.
     *
     * @param argb_pixels  Source ARGB pixels, row-major (width * height uint32_t values)
     * @param w / h        Sprite dimensions in pixels
     */
    void
    add_to_batch(
        uint32_t batch_id,
        int element_id,
        int atlas_index,
        const uint32_t* argb_pixels,
        int w,
        int h)
    {
        if( element_id < 0 || element_id >= capacity_ )
            return;
        if( atlas_index < 0 || atlas_index >= max_atlas_ )
            return;
        if( !argb_pixels || w <= 0 || h <= 0 )
            return;

        SpriteBatchState* batch = find_batch(batch_id);
        if( !batch || !batch->open || !batch->pixel_buf )
            return;

        // Advance shelf if this sprite doesn't fit on the current row
        if( batch->shelf_x + w > batch->atlas_w )
        {
            batch->shelf_y += batch->shelf_h;
            batch->shelf_x = 0;
            batch->shelf_h = 0;
        }
        // No vertical space left — silently skip (caller should size atlas large enough)
        if( batch->shelf_y + h > batch->atlas_h )
            return;

        // Blit into atlas
        for( int row = 0; row < h; ++row )
        {
            const uint32_t* src = argb_pixels + (size_t)row * (size_t)w;
            uint32_t* dst = batch->pixel_buf +
                            (size_t)(batch->shelf_y + row) * (size_t)batch->atlas_w +
                            (size_t)batch->shelf_x;
            memcpy(dst, src, (size_t)w * sizeof(uint32_t));
        }

        // Record UV sub-region
        const float fw = (float)batch->atlas_w;
        const float fh = (float)batch->atlas_h;
        const int flat = element_id * max_atlas_ + atlas_index;
        SpriteEntry& entry = entries_[flat];
        entry.atlas_texture = null_;
        entry.u0 = (float)batch->shelf_x / fw;
        entry.v0 = (float)batch->shelf_y / fh;
        entry.u1 = (float)(batch->shelf_x + w) / fw;
        entry.v1 = (float)(batch->shelf_y + h) / fh;
        entry.batch_id = batch_id;
        entry.is_batched = true;
        entry.loaded = true;

        batch->entry_indices.push_back(flat);

        if( h > batch->shelf_h )
            batch->shelf_h = h;
        batch->shelf_x += w;
    }

    /**
     * Finalize the batch: returns the packed pixel buffer for GPU upload.
     * The buffer remains valid until set_batch_atlas_handle() is called.
     */
    AtlasPixelData
    finalize_batch(uint32_t batch_id)
    {
        SpriteBatchState* batch = find_batch(batch_id);
        if( !batch || !batch->open )
            return { nullptr, 0, 0 };
        batch->open = false;
        return { batch->pixel_buf, batch->atlas_w, batch->atlas_h };
    }

    /**
     * Commit the GPU texture handle for the batch atlas.
     * Writes the handle into every SpriteEntry that belongs to this batch
     * and frees the CPU pixel buffer.
     */
    void
    set_batch_atlas_handle(
        uint32_t batch_id,
        TexH h)
    {
        SpriteBatchState* batch = find_batch(batch_id);
        if( !batch )
            return;
        for( int flat : batch->entry_indices )
            entries_[flat].atlas_texture = h;
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
    register_standalone(
        int element_id,
        int atlas_index,
        TexH h)
    {
        if( element_id < 0 || element_id >= capacity_ )
            return;
        if( atlas_index < 0 || atlas_index >= max_atlas_ )
            return;
        const int flat = element_id * max_atlas_ + atlas_index;
        SpriteEntry& entry = entries_[flat];
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

    SpriteEntry*
    get_sprite(
        int element_id,
        int atlas_index)
    {
        if( element_id < 0 || element_id >= capacity_ )
            return nullptr;
        if( atlas_index < 0 || atlas_index >= max_atlas_ )
            return nullptr;
        SpriteEntry* e = &entries_[element_id * max_atlas_ + atlas_index];
        return e->loaded ? e : nullptr;
    }

    bool
    is_batch_open(uint32_t batch_id) const
    {
        const SpriteBatchState* b = find_batch_const(batch_id);
        return b && b->open;
    }

    int
    get_element_capacity() const
    {
        return capacity_;
    }
    int
    get_atlas_per_element() const
    {
        return max_atlas_;
    }

    // ------------------------------------------------------------------
    // Unload
    // ------------------------------------------------------------------

    /** Clear a single entry. Caller must have released the GPU resource if standalone. */
    void
    unload_sprite(
        int element_id,
        int atlas_index)
    {
        if( element_id < 0 || element_id >= capacity_ )
            return;
        if( atlas_index < 0 || atlas_index >= max_atlas_ )
            return;
        SpriteEntry& entry = entries_[element_id * max_atlas_ + atlas_index];
        entry.atlas_texture = null_;
        entry.u0 = entry.v0 = 0.0f;
        entry.u1 = entry.v1 = 1.0f;
        entry.batch_id = 0;
        entry.is_batched = false;
        entry.loaded = false;
    }

    /**
     * Clear all entries for the batch.
     * Caller must have released the shared GPU atlas texture BEFORE calling this.
     */
    void
    unload_batch(uint32_t batch_id)
    {
        SpriteBatchState* batch = find_batch(batch_id);
        if( !batch )
            return;
        for( int flat : batch->entry_indices )
        {
            entries_[flat].atlas_texture = null_;
            entries_[flat].loaded = false;
            entries_[flat].is_batched = false;
            entries_[flat].batch_id = 0;
        }
        if( batch->pixel_buf )
        {
            free(batch->pixel_buf);
            batch->pixel_buf = nullptr;
        }
        batch->entry_indices.clear();
        batch->id = 0;
        batch->open = false;
        batch->in_use = false;
    }

private:
    SpriteBatchState*
    find_batch(uint32_t id)
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( batches_[i].in_use && batches_[i].id == id )
                return &batches_[i];
        return nullptr;
    }

    const SpriteBatchState*
    find_batch_const(uint32_t id) const
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( batches_[i].in_use && batches_[i].id == id )
                return &batches_[i];
        return nullptr;
    }

    SpriteBatchState*
    find_or_claim_slot()
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( !batches_[i].in_use )
                return &batches_[i];
        return nullptr;
    }

    SpriteEntry* entries_ = nullptr;
    int capacity_ = 0;
    int max_atlas_ = 0;
    SpriteBatchState batches_[kMaxBatches];
    TexH null_;
};
