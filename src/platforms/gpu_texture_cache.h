#pragma once

#include <cstdint>

/**
 * GpuTextureCache<TexH>
 *
 * O(1) flat-array cache for world texture GPU handles (IDs 0-255).
 * Templated on TexH — the platform's texture handle type.
 *   Metal:  GpuTextureCache<void*>   (void* = bridge-retained id<MTLTexture> or array)
 *   OpenGL: GpuTextureCache<unsigned> (GLuint texture object)
 *
 * GPU resource lifetime is the CALLER'S responsibility. The cache only
 * clears handle slots — it never calls CFRelease / glDeleteTextures.
 *
 * Batch semantics:
 *   begin_batch(id) / end_batch(id) bracket a window during which every
 *   register_texture() call is tagged with that batch_id.
 *   unload_batch(id) clears all slots tagged with that batch_id.
 */
template<typename TexH>
class GpuTextureCache
{
public:
    static constexpr int kMaxSlots = 256;
    static constexpr int kMaxBatches = 16;

    void
    init(
        int /*max_texture_id*/,
        TexH null_value)
    {
        null_ = null_value;
        for( int i = 0; i < kMaxSlots; ++i )
        {
            slots_[i] = null_value;
            is_batched_[i] = false;
            batch_id_for_[i] = 0;
        }
        for( int i = 0; i < kMaxBatches; ++i )
            batch_open_[i] = false;
        active_batch_id_ = 0;
    }

    void
    destroy()
    {
        for( int i = 0; i < kMaxSlots; ++i )
            slots_[i] = null_;
        active_batch_id_ = 0;
    }

    void
    begin_batch(uint32_t batch_id)
    {
        active_batch_id_ = batch_id;
        for( int i = 0; i < kMaxBatches; ++i )
        {
            if( !batch_open_[i] )
            {
                batch_ids_[i] = batch_id;
                batch_open_[i] = true;
                return;
            }
        }
    }

    void
    end_batch(uint32_t batch_id)
    {
        if( active_batch_id_ == batch_id )
            active_batch_id_ = 0;
        for( int i = 0; i < kMaxBatches; ++i )
        {
            if( batch_open_[i] && batch_ids_[i] == batch_id )
            {
                batch_open_[i] = false;
                return;
            }
        }
    }

    /** Register (or update) a texture handle for the given ID.
     *  If a batch is currently open the slot is tagged with the active batch_id. */
    void
    register_texture(
        int id,
        TexH h)
    {
        if( id < 0 || id >= kMaxSlots )
            return;
        slots_[id] = h;
        if( active_batch_id_ != 0 )
        {
            is_batched_[id] = true;
            batch_id_for_[id] = active_batch_id_;
        }
        else
        {
            is_batched_[id] = false;
            batch_id_for_[id] = 0;
        }
    }

    TexH
    get_texture(int id) const
    {
        if( id < 0 || id >= kMaxSlots )
            return null_;
        return slots_[id];
    }

    int
    get_capacity() const
    {
        return kMaxSlots;
    }

    bool
    is_loaded(int id) const
    {
        if( id < 0 || id >= kMaxSlots )
            return false;
        return slots_[id] != null_;
    }

    /** Clear the slot. Caller must have already released the GPU resource. */
    void
    unload_texture(int id)
    {
        if( id < 0 || id >= kMaxSlots )
            return;
        slots_[id] = null_;
        is_batched_[id] = false;
        batch_id_for_[id] = 0;
    }

    /** Clear all slots tagged with batch_id. Caller must have already released GPU resources. */
    void
    unload_batch(uint32_t batch_id)
    {
        for( int i = 0; i < kMaxSlots; ++i )
        {
            if( is_batched_[i] && batch_id_for_[i] == batch_id )
                unload_texture(i);
        }
        // Close the batch entry if still recorded as open
        for( int i = 0; i < kMaxBatches; ++i )
        {
            if( batch_open_[i] && batch_ids_[i] == batch_id )
                batch_open_[i] = false;
        }
    }

private:
    TexH slots_[kMaxSlots];
    bool is_batched_[kMaxSlots];
    uint32_t batch_id_for_[kMaxSlots];

    uint32_t batch_ids_[kMaxBatches];
    bool batch_open_[kMaxBatches];

    uint32_t active_batch_id_;
    TexH null_;
};
