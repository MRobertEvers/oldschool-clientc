#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

/**
 * GpuModelCache<BufH>
 *
 * Three-level flat-array lookup for 3D model GPU vertex buffers.
 * Templated on BufH — the platform's buffer handle type.
 *   Metal:  GpuModelCache<void*>   (void* = bridge-retained id<MTLBuffer>)
 *   OpenGL: GpuModelCache<unsigned> (GLuint VBO name)
 *
 * Lookup is O(1) by (model_gpu_id, anim_id, frame_id) — no hash tables.
 *   model_gpu_id : monotonic from Scene2, starting at 0 (small, consecutive)
 *   anim_id      : animation ID allocated by dash; 0 = base (no animation)
 *   frame_id     : index of the frame within the animation; 0 = base pose
 *
 * Storage layout:
 *   entries_[model_gpu_id]          -> ModelEntry
 *   ModelEntry.anims[anim_id]       -> AnimEntry   (vector, grown on demand)
 *   AnimEntry.frames[frame_id]      -> ModelBufferRange (vector, grown on demand)
 *
 * New slots produced by growth are initialised with loaded=false so callers
 * can distinguish "registered" from "not yet uploaded".
 *
 * Batch storage (world rebuild): up to kMaxBatches concurrent batches.
 * GPU resource lifetime is the CALLER'S responsibility.
 */
template<typename BufH>
class GpuModelCache
{
public:
    static constexpr int kMaxBatches = 64;
    static constexpr int kMaxBatchMembers = 256;

    // ------------------------------------------------------------------
    // Public types
    // ------------------------------------------------------------------

    /** One loaded GPU pose (base or animated frame). */
    struct ModelBufferRange
    {
        BufH buffer;
        uint32_t vtx_byte_offset; ///< Byte offset into batch VBO; 0 for standalone
        uint32_t vtx_count;
        int face_count;
        std::vector<int> per_face_tex_id; ///< Length == face_count
        bool owns_buffer; ///< True for standalone; batch owns the VBO otherwise
        bool loaded;      ///< False = slot allocated but not populated
        /** Shared batch IBO: first index element (uint32) for this model; 0 if unused. */
        uint32_t batch_index_first;
        /** Number of uint32 indices (typically face_count * 3). */
        uint32_t batch_index_count;
        /** Standalone pose: optional index buffer (sequential 0..vtx-1); null for batch. */
        BufH index_buffer;
        bool owns_index_buffer;
    };

    /** Sub-table for one anim_id: indexed by frame_id. */
    struct AnimEntry
    {
        std::vector<ModelBufferRange> frames; ///< frames[frame_id]
    };

    /** Per-model entry: top of the three-level hierarchy. */
    struct ModelEntry
    {
        std::vector<AnimEntry> anims; ///< anims[anim_id]
        bool is_batched;
        uint32_t batch_id;
    };

    struct BatchEntry
    {
        uint32_t id;
        bool open;
        bool in_use; ///< True when open or finalized but not yet unloaded

        /// Raw vertex accumulation — renderer-agnostic; caller casts to its vertex struct.
        std::vector<uint8_t> pending_verts;
        int pending_vert_count; ///< Number of vertices (not bytes)
        int vert_size_bytes;    ///< Bytes per vertex

        std::vector<uint32_t> pending_indices;

        /// GPU handles — valid after end_batch(), null_ before.
        BufH vbo;
        BufH ibo;

        int member_ids[kMaxBatchMembers]; ///< model_gpu_id of each batch member
        int member_count;
    };

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    void
    init(
        int model_capacity,
        BufH null_value)
    {
        null_ = null_value;
        model_capacity_ = model_capacity;
        entries_ = new ModelEntry[model_capacity]();
        for( int i = 0; i < model_capacity; ++i )
        {
            entries_[i].is_batched = false;
            entries_[i].batch_id = 0;
        }
        for( int i = 0; i < kMaxBatches; ++i )
        {
            batches_[i].id = 0;
            batches_[i].open = false;
            batches_[i].in_use = false;
            batches_[i].pending_vert_count = 0;
            batches_[i].vert_size_bytes = 0;
            batches_[i].vbo = null_value;
            batches_[i].ibo = null_value;
            batches_[i].member_count = 0;
        }
    }

    void
    destroy()
    {
        delete[] entries_;
        entries_ = nullptr;
        model_capacity_ = 0;
    }

    // ------------------------------------------------------------------
    // Batch model load path
    // ------------------------------------------------------------------

    void
    begin_batch(uint32_t batch_id)
    {
        BatchEntry* slot = find_or_claim_slot();
        if( !slot )
            return;
        slot->id = batch_id;
        slot->open = true;
        slot->in_use = true;
        slot->pending_vert_count = 0;
        slot->vert_size_bytes = 0;
        slot->vbo = null_;
        slot->ibo = null_;
        slot->member_count = 0;
        slot->pending_verts.clear();
        slot->pending_indices.clear();
    }

    /**
     * Append one model's vertices to the batch accumulation buffer.
     *
     * @param raw_verts       Pointer to vertex data (renderer's vertex struct layout)
     * @param vert_size_bytes Bytes per vertex — must be consistent within a batch
     * @param vert_count      Number of vertices (== face_count * 3)
     * @param face_count      Number of triangular faces
     * @param per_face_tex    Per-face texture IDs (length == face_count); may be nullptr
     */
    void
    accumulate_batch_model(
        uint32_t batch_id,
        int model_gpu_id,
        const void* raw_verts,
        int vert_size_bytes,
        int vert_count,
        int face_count,
        const int* per_face_tex)
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ || vert_count <= 0 || !raw_verts )
            return;
        BatchEntry* batch = find_batch(batch_id);
        if( !batch || !batch->open )
            return;

        const uint32_t byte_offset = (uint32_t)batch->pending_verts.size();
        const size_t append_bytes = (size_t)vert_size_bytes * (size_t)vert_count;
        batch->pending_verts.resize(batch->pending_verts.size() + append_bytes);
        memcpy(batch->pending_verts.data() + byte_offset, raw_verts, append_bytes);
        batch->pending_vert_count += vert_count;
        batch->vert_size_bytes = vert_size_bytes;

        const uint32_t idx_first = (uint32_t)batch->pending_indices.size();
        // Sequential indices (faces are pre-sorted by the caller)
        const uint32_t vtx_base = (uint32_t)(byte_offset / (size_t)vert_size_bytes);
        for( int v = 0; v < vert_count; ++v )
            batch->pending_indices.push_back(vtx_base + (uint32_t)v);

        // Register the base pose slot (anim_id=0, frame_id=0) for this model
        ModelBufferRange& r = ensure_slot(model_gpu_id, 0, 0);
        r.buffer = null_;
        r.vtx_byte_offset = byte_offset;
        r.vtx_count = (uint32_t)vert_count;
        r.face_count = face_count;
        r.owns_buffer = false;
        r.batch_index_first = idx_first;
        r.batch_index_count = (uint32_t)vert_count;
        r.index_buffer = null_;
        r.owns_index_buffer = false;
        r.loaded = true;
        r.per_face_tex_id.clear();
        if( per_face_tex && face_count > 0 )
            r.per_face_tex_id.assign(per_face_tex, per_face_tex + face_count);

        entries_[model_gpu_id].is_batched = true;
        entries_[model_gpu_id].batch_id = batch_id;

        if( batch->member_count < kMaxBatchMembers )
            batch->member_ids[batch->member_count++] = model_gpu_id;
    }

    /** Returns a pointer to the raw vertex accumulation buffer (read before calling end_batch). */
    const void*
    get_batch_pending_verts(uint32_t batch_id) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        return (b && b->open) ? b->pending_verts.data() : nullptr;
    }

    int
    get_batch_pending_vert_count(uint32_t batch_id) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        return (b && b->open) ? b->pending_vert_count : 0;
    }

    int
    get_batch_pending_byte_count(uint32_t batch_id) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        return (b && b->open) ? (int)b->pending_verts.size() : 0;
    }

    /** Commit finalized GPU buffers; frees accumulation storage. */
    void
    end_batch(
        uint32_t batch_id,
        BufH vbo,
        BufH ibo)
    {
        BatchEntry* batch = find_batch(batch_id);
        if( !batch || !batch->open )
            return;
        batch->vbo = vbo;
        batch->ibo = ibo;
        batch->open = false;
        batch->pending_verts.clear();
        batch->pending_indices.clear();
        batch->pending_verts.shrink_to_fit();
        batch->pending_indices.shrink_to_fit();
    }

    // ------------------------------------------------------------------
    // Single-model / animated-pose load path
    // ------------------------------------------------------------------

    /**
     * Register or overwrite a pose slot for (model_gpu_id, anim_id, frame_id).
     *   anim_id = 0, frame_id = 0  -> base (non-animated) pose
     *   anim_id != 0               -> pre-baked animated pose
     *
     * Returns 0 on success, -1 if model_gpu_id is out of range.
     */
    int
    register_instance(
        int model_gpu_id,
        int anim_id,
        int frame_id,
        BufH buf,
        uint32_t vtx_byte_offset,
        uint32_t vtx_count,
        int face_count,
        const int* per_face_tex,
        bool owns_buffer,
        BufH index_buffer,
        bool owns_index_buffer,
        uint32_t batch_index_first,
        uint32_t batch_index_count)
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return -1;
        ModelBufferRange& r = ensure_slot(model_gpu_id, anim_id, frame_id);
        r.buffer = buf;
        r.vtx_byte_offset = vtx_byte_offset;
        r.vtx_count = vtx_count;
        r.face_count = face_count;
        r.owns_buffer = owns_buffer;
        r.index_buffer = index_buffer;
        r.owns_index_buffer = owns_index_buffer;
        r.batch_index_first = batch_index_first;
        r.batch_index_count = batch_index_count;
        r.loaded = true;
        r.per_face_tex_id.clear();
        if( per_face_tex && face_count > 0 )
            r.per_face_tex_id.assign(per_face_tex, per_face_tex + face_count);
        return 0;
    }

    // ------------------------------------------------------------------
    // Lookup
    // ------------------------------------------------------------------

    /**
     * O(1) lookup: entries_[model_gpu_id].anims[anim_id].frames[frame_id].
     * Returns nullptr if any index is out of range or the slot is not loaded.
     */
    ModelBufferRange*
    get_instance(
        int model_gpu_id,
        int anim_id,
        int frame_id)
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return nullptr;
        ModelEntry& entry = entries_[model_gpu_id];
        if( anim_id < 0 || anim_id >= (int)entry.anims.size() )
            return nullptr;
        AnimEntry& anim = entry.anims[(size_t)anim_id];
        if( frame_id < 0 || frame_id >= (int)anim.frames.size() )
            return nullptr;
        ModelBufferRange& r = anim.frames[(size_t)frame_id];
        return r.loaded ? &r : nullptr;
    }

    /** For batched models, returns the shared VBO from the batch entry. */
    BufH
    get_batch_vbo_for_model(int model_gpu_id) const
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return null_;
        const ModelEntry& entry = entries_[model_gpu_id];
        if( !entry.is_batched )
            return null_;
        const BatchEntry* b = find_batch_const(entry.batch_id);
        return b ? b->vbo : null_;
    }

    BufH
    get_batch_ibo_for_model(int model_gpu_id) const
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return null_;
        const ModelEntry& entry = entries_[model_gpu_id];
        if( !entry.is_batched )
            return null_;
        const BatchEntry* b = find_batch_const(entry.batch_id);
        return b ? b->ibo : null_;
    }

    BatchEntry*
    get_batch(uint32_t batch_id)
    {
        return find_batch(batch_id);
    }

    ModelEntry*
    get_model_entry(int model_gpu_id)
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return nullptr;
        return &entries_[model_gpu_id];
    }

    int
    get_model_capacity() const
    {
        return model_capacity_;
    }

    // ------------------------------------------------------------------
    // Unload
    // ------------------------------------------------------------------

    /** Clear all pose slots for the model. Caller must have released owns_buffer GPU handles. */
    void
    unload_model(int model_gpu_id)
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return;
        ModelEntry& entry = entries_[model_gpu_id];
        entry.anims.clear();
        entry.is_batched = false;
        entry.batch_id = 0;
    }

    /**
     * Clear the batch entry and all member model entries.
     * Caller must call get_batch() and release vbo/ibo GPU handles BEFORE this.
     */
    void
    unload_batch(uint32_t batch_id)
    {
        BatchEntry* batch = find_batch(batch_id);
        if( !batch )
            return;
        for( int i = 0; i < batch->member_count; ++i )
            unload_model(batch->member_ids[i]);
        batch->vbo = null_;
        batch->ibo = null_;
        batch->id = 0;
        batch->open = false;
        batch->in_use = false;
        batch->member_count = 0;
        batch->pending_verts.clear();
        batch->pending_indices.clear();
    }

private:
    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    /**
     * Ensure entries_[model_gpu_id].anims[anim_id].frames[frame_id] exists,
     * growing the vectors as needed. New slots are initialised with loaded=false.
     */
    ModelBufferRange&
    ensure_slot(
        int model_gpu_id,
        int anim_id,
        int frame_id)
    {
        ModelEntry& entry = entries_[model_gpu_id];
        if( anim_id >= (int)entry.anims.size() )
            entry.anims.resize((size_t)anim_id + 1);

        AnimEntry& anim = entry.anims[(size_t)anim_id];
        if( frame_id >= (int)anim.frames.size() )
        {
            const size_t old_sz = anim.frames.size();
            anim.frames.resize((size_t)frame_id + 1);
            for( size_t k = old_sz; k < anim.frames.size(); ++k )
            {
                anim.frames[k].buffer = null_;
                anim.frames[k].vtx_byte_offset = 0;
                anim.frames[k].vtx_count = 0;
                anim.frames[k].face_count = 0;
                anim.frames[k].owns_buffer = false;
                anim.frames[k].batch_index_first = 0;
                anim.frames[k].batch_index_count = 0;
                anim.frames[k].index_buffer = null_;
                anim.frames[k].owns_index_buffer = false;
                anim.frames[k].loaded = false;
            }
        }
        return anim.frames[(size_t)frame_id];
    }

    BatchEntry*
    find_batch(uint32_t id)
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( batches_[i].in_use && batches_[i].id == id )
                return &batches_[i];
        return nullptr;
    }

    const BatchEntry*
    find_batch_const(uint32_t id) const
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( batches_[i].in_use && batches_[i].id == id )
                return &batches_[i];
        return nullptr;
    }

    BatchEntry*
    find_or_claim_slot()
    {
        for( int i = 0; i < kMaxBatches; ++i )
            if( !batches_[i].in_use )
                return &batches_[i];
        return nullptr;
    }

    ModelEntry* entries_ = nullptr;
    int model_capacity_ = 0;
    BatchEntry batches_[kMaxBatches];
    BufH null_;
};
