#pragma once

#ifndef GPU_3D_CACHE_H
#define GPU_3D_CACHE_H

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

/**
 * Gpu3DCache<BufH>
 *
 * Owns logical GPU metadata for 3D: standalone + batched models, animation poses,
 * shared vertex/face arrays, and unified batch chunks (VBO/IBO pairs).
 * VA/FA/model gpu ids are O(1) dense index lookups in fixed tables (kGpuIdTableSize).
 */
template<typename BufH>
class Gpu3DCache
{
public:
    /** Dense tables for scene2_gpu_id (VA/FA) and model_gpu_id; ids must stay in range. */
    static constexpr int kGpuIdTableSize = 65536;
    static constexpr int kMaxBatches = 64;

    struct BatchCaps
    {
        int max_verts_per_chunk;
        int max_indices_per_chunk;
    };

    struct VertexArrayEntry
    {
        BufH vbuf{};
        bool owns_buffer = false;
        uint32_t vert_count = 0;
        int vert_size_bytes = 0;
        bool loaded = false;
    };

    struct FaceArrayEntry
    {
        BufH vbuf{};
        BufH ibuf{};
        bool owns_vbuf = false;
        bool owns_ibuf = false;
        int face_count = 0;
        std::vector<int> per_face_tex_id;
        bool loaded = false;
        int batch_id = -1;
        int batch_chunk_index = 0;
        uint32_t vtx_byte_offset = 0;
        uint32_t batch_index_first = 0;
        uint32_t batch_index_count = 0;
    };

    struct ModelBufferRange
    {
        BufH buffer{};
        uint32_t vtx_byte_offset = 0;
        uint32_t vtx_count = 0;
        int face_count = 0;
        std::vector<int> per_face_tex_id;
        bool owns_buffer = false;
        bool loaded = false;
        uint32_t batch_index_first = 0;
        uint32_t batch_index_count = 0;
        BufH index_buffer{};
        bool owns_index_buffer = false;
        int va_gpu_id = -1;
        int fa_gpu_id = -1;
        uint32_t fa_first_face = 0;
        int batch_chunk_index = -1;
    };

    struct AnimEntry
    {
        std::vector<ModelBufferRange> frames;
    };

    struct ModelEntry
    {
        std::vector<AnimEntry> anims;
        bool is_batched = false;
        uint32_t batch_id = 0;
    };

    struct BatchChunk
    {
        std::vector<uint8_t> pending_verts;
        std::vector<uint32_t> pending_indices;
        int pending_vert_count = 0;
        BufH vbo{};
        BufH ibo{};
    };

    struct BatchEntry
    {
        uint32_t id = 0;
        bool open = false;
        bool in_use = false;
        int vert_size_bytes = 0;
        std::vector<BatchChunk> chunks;
        int active_chunk = 0;
    };

    void
    init(int model_capacity, BufH null_value, BatchCaps caps = { INT_MAX, INT_MAX })
    {
        null_ = null_value;
        caps_ = caps;
        if( model_capacity < 1 )
            model_capacity = 1;
        if( model_capacity > kGpuIdTableSize )
            model_capacity = kGpuIdTableSize;
        model_capacity_ = model_capacity;
        entries_ = new ModelEntry[(size_t)model_capacity_]();
        for( int i = 0; i < kMaxBatches; ++i )
        {
            batches_[i].id = 0;
            batches_[i].open = false;
            batches_[i].in_use = false;
            batches_[i].vert_size_bytes = 0;
            batches_[i].active_chunk = 0;
            batches_[i].chunks.clear();
        }
        va_entries_.assign((size_t)kGpuIdTableSize, VertexArrayEntry{});
        fa_entries_.assign((size_t)kGpuIdTableSize, FaceArrayEntry{});
    }

    void
    destroy()
    {
        delete[] entries_;
        entries_ = nullptr;
        model_capacity_ = 0;
        va_entries_.clear();
        fa_entries_.clear();
    }

    // ------------------------------------------------------------------
    // Vertex / face array tables
    // ------------------------------------------------------------------

    int
    register_vertex_array(int va_id, BufH vbuf, bool owns, uint32_t vert_count, int vert_size)
    {
        if( va_id < 0 || va_id >= kGpuIdTableSize )
            return -1;
        VertexArrayEntry& e = va_entries_[(size_t)va_id];
        e.vbuf = vbuf;
        e.owns_buffer = owns;
        e.vert_count = vert_count;
        e.vert_size_bytes = vert_size;
        e.loaded = true;
        return 0;
    }

    int
    register_face_array(
        int fa_id,
        BufH vbuf,
        BufH ibuf,
        bool owns_v,
        bool owns_i,
        int face_count,
        const int* per_face_tex)
    {
        if( fa_id < 0 || fa_id >= kGpuIdTableSize )
            return -1;
        FaceArrayEntry& e = fa_entries_[(size_t)fa_id];
        e.vbuf = vbuf;
        e.ibuf = ibuf;
        e.owns_vbuf = owns_v;
        e.owns_ibuf = owns_i;
        e.face_count = face_count;
        e.batch_id = -1;
        e.batch_chunk_index = 0;
        e.vtx_byte_offset = 0;
        e.batch_index_first = 0;
        e.batch_index_count = 0;
        e.per_face_tex_id.clear();
        if( per_face_tex && face_count > 0 )
            e.per_face_tex_id.assign(per_face_tex, per_face_tex + face_count);
        e.loaded = true;
        return 0;
    }

    void
    unload_vertex_array(int va_id)
    {
        if( va_id < 0 || va_id >= kGpuIdTableSize )
            return;
        va_entries_[(size_t)va_id] = VertexArrayEntry{};
    }

    void
    unload_face_array(int fa_id)
    {
        if( fa_id < 0 || fa_id >= kGpuIdTableSize )
            return;
        fa_entries_[(size_t)fa_id] = FaceArrayEntry{};
    }

    VertexArrayEntry*
    get_vertex_array(int va_id)
    {
        if( va_id < 0 || va_id >= kGpuIdTableSize )
            return nullptr;
        VertexArrayEntry& e = va_entries_[(size_t)va_id];
        return e.loaded ? &e : nullptr;
    }

    FaceArrayEntry*
    get_face_array(int fa_id)
    {
        if( fa_id < 0 || fa_id >= kGpuIdTableSize )
            return nullptr;
        FaceArrayEntry& e = fa_entries_[(size_t)fa_id];
        return e.loaded ? &e : nullptr;
    }

    /** `vert_stride_bytes` — e.g. sizeof(MetalVertex) for interleaved bakes. */
    int
    register_va_model(
        int model_gpu_id,
        int anim_id,
        int frame_id,
        int va_gpu_id,
        int fa_gpu_id,
        uint32_t fa_first_face,
        int face_count,
        int vert_stride_bytes)
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return -1;
        FaceArrayEntry* fa = get_face_array(fa_gpu_id);
        if( !fa )
            return -1;
        ModelBufferRange& r = ensure_slot(model_gpu_id, anim_id, frame_id);
        r.buffer = null_;
        r.vtx_byte_offset = fa->vtx_byte_offset + fa_first_face * 3u * (uint32_t)vert_stride_bytes;
        r.vtx_count = (uint32_t)(face_count * 3);
        r.face_count = face_count;
        r.owns_buffer = false;
        r.batch_index_first = fa->batch_index_first + fa_first_face * 3u;
        r.batch_index_count = (uint32_t)(face_count * 3);
        r.index_buffer = null_;
        r.owns_index_buffer = false;
        r.va_gpu_id = va_gpu_id;
        r.fa_gpu_id = fa_gpu_id;
        r.fa_first_face = fa_first_face;
        r.batch_chunk_index = fa->batch_chunk_index;
        r.loaded = true;
        r.per_face_tex_id.clear();
        if( fa->per_face_tex_id.size() >= (size_t)fa_first_face + (size_t)face_count )
        {
            r.per_face_tex_id.assign(
                fa->per_face_tex_id.begin() + (ptrdiff_t)fa_first_face,
                fa->per_face_tex_id.begin() + (ptrdiff_t)fa_first_face + (ptrdiff_t)face_count);
        }
        entries_[model_gpu_id].is_batched = (fa->batch_id >= 0);
        entries_[model_gpu_id].batch_id = fa->batch_id >= 0 ? (uint32_t)fa->batch_id : 0u;
        return 0;
    }

    // ------------------------------------------------------------------
    // Batch
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
        slot->vert_size_bytes = 0;
        slot->active_chunk = 0;
        slot->chunks.clear();
        slot->chunks.emplace_back();
    }

    void
    roll_chunk_if_needed(BatchEntry* batch, int vert_count, int idx_count)
    {
        if( batch->chunks.empty() )
            batch->chunks.emplace_back();
        BatchChunk& cur = batch->chunks[(size_t)batch->active_chunk];
        if( cur.pending_vert_count + vert_count > caps_.max_verts_per_chunk ||
            (int)cur.pending_indices.size() + idx_count > caps_.max_indices_per_chunk )
        {
            batch->chunks.emplace_back();
            batch->active_chunk = (int)batch->chunks.size() - 1;
        }
    }

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

        roll_chunk_if_needed(batch, vert_count, vert_count);

        BatchChunk& ch = batch->chunks[(size_t)batch->active_chunk];
        const uint32_t byte_offset = (uint32_t)ch.pending_verts.size();
        const size_t append_bytes = (size_t)vert_size_bytes * (size_t)vert_count;
        ch.pending_verts.resize(ch.pending_verts.size() + append_bytes);
        memcpy(ch.pending_verts.data() + byte_offset, raw_verts, append_bytes);
        ch.pending_vert_count += vert_count;
        batch->vert_size_bytes = vert_size_bytes;

        const uint32_t idx_first = (uint32_t)ch.pending_indices.size();
        const uint32_t vtx_base = (uint32_t)(byte_offset / (size_t)vert_size_bytes);
        for( int v = 0; v < vert_count; ++v )
            ch.pending_indices.push_back(vtx_base + (uint32_t)v);

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
        r.va_gpu_id = -1;
        r.fa_gpu_id = -1;
        r.fa_first_face = 0;
        r.batch_chunk_index = batch->active_chunk;
        r.per_face_tex_id.clear();
        if( per_face_tex && face_count > 0 )
            r.per_face_tex_id.assign(per_face_tex, per_face_tex + face_count);

        entries_[model_gpu_id].is_batched = true;
        entries_[model_gpu_id].batch_id = batch_id;
    }

    void
    accumulate_batch_face_array(
        uint32_t batch_id,
        int fa_id,
        const void* raw_verts,
        int vert_size_bytes,
        int vert_count,
        int face_count,
        const int* per_face_tex)
    {
        if( fa_id < 0 || fa_id >= kGpuIdTableSize || vert_count <= 0 || !raw_verts )
            return;
        BatchEntry* batch = find_batch(batch_id);
        if( !batch || !batch->open )
            return;

        roll_chunk_if_needed(batch, vert_count, vert_count);

        BatchChunk& ch = batch->chunks[(size_t)batch->active_chunk];
        const uint32_t byte_offset = (uint32_t)ch.pending_verts.size();
        const size_t append_bytes = (size_t)vert_size_bytes * (size_t)vert_count;
        ch.pending_verts.resize(ch.pending_verts.size() + append_bytes);
        memcpy(ch.pending_verts.data() + byte_offset, raw_verts, append_bytes);
        ch.pending_vert_count += vert_count;
        batch->vert_size_bytes = vert_size_bytes;

        const uint32_t idx_first = (uint32_t)ch.pending_indices.size();
        const uint32_t vtx_base = (uint32_t)(byte_offset / (size_t)vert_size_bytes);
        for( int v = 0; v < vert_count; ++v )
            ch.pending_indices.push_back(vtx_base + (uint32_t)v);

        FaceArrayEntry& fe = fa_entries_[(size_t)fa_id];
        fe.vbuf = null_;
        fe.ibuf = null_;
        fe.owns_vbuf = false;
        fe.owns_ibuf = false;
        fe.face_count = face_count;
        fe.batch_id = (int)batch_id;
        fe.batch_chunk_index = batch->active_chunk;
        fe.vtx_byte_offset = byte_offset;
        fe.batch_index_first = idx_first;
        fe.batch_index_count = (uint32_t)vert_count;
        fe.per_face_tex_id.clear();
        if( per_face_tex && face_count > 0 )
            fe.per_face_tex_id.assign(per_face_tex, per_face_tex + face_count);
        fe.loaded = true;
    }

    void
    register_batch_vertex_array(uint32_t batch_id, int va_id, uint32_t vert_count, int vert_size)
    {
        (void)batch_id;
        register_vertex_array(va_id, null_, false, vert_count, vert_size);
    }

    void
    register_batch_va_model(
        uint32_t batch_id,
        int model_gpu_id,
        int va_id,
        int fa_id,
        uint32_t fa_first_face,
        int face_count,
        int vert_stride_bytes)
    {
        (void)batch_id;
        register_va_model(model_gpu_id, 0, 0, va_id, fa_id, fa_first_face, face_count, vert_stride_bytes);
    }

    int
    get_batch_chunk_count(uint32_t batch_id) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        return b ? (int)b->chunks.size() : 0;
    }

    const void*
    get_chunk_pending_verts(uint32_t batch_id, int chunk_index) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        if( !b || chunk_index < 0 || (size_t)chunk_index >= b->chunks.size() )
            return nullptr;
        return b->chunks[(size_t)chunk_index].pending_verts.data();
    }

    int
    get_chunk_pending_bytes(uint32_t batch_id, int chunk_index) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        if( !b || chunk_index < 0 || (size_t)chunk_index >= b->chunks.size() )
            return 0;
        return (int)b->chunks[(size_t)chunk_index].pending_verts.size();
    }

    const uint32_t*
    get_chunk_pending_indices(uint32_t batch_id, int chunk_index) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        if( !b || chunk_index < 0 || (size_t)chunk_index >= b->chunks.size() )
            return nullptr;
        return b->chunks[(size_t)chunk_index].pending_indices.data();
    }

    int
    get_chunk_pending_index_count(uint32_t batch_id, int chunk_index) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        if( !b || chunk_index < 0 || (size_t)chunk_index >= b->chunks.size() )
            return 0;
        return (int)b->chunks[(size_t)chunk_index].pending_indices.size();
    }

    void
    set_chunk_buffers(uint32_t batch_id, int chunk_index, BufH vbo, BufH ibo)
    {
        BatchEntry* b = find_batch(batch_id);
        if( !b || chunk_index < 0 || (size_t)chunk_index >= b->chunks.size() )
            return;
        BatchChunk& ch = b->chunks[(size_t)chunk_index];
        ch.vbo = vbo;
        ch.ibo = ibo;
        ch.pending_verts.clear();
        ch.pending_verts.shrink_to_fit();
        ch.pending_indices.clear();
        ch.pending_indices.shrink_to_fit();
        ch.pending_vert_count = 0;
    }

    void
    end_batch(uint32_t batch_id)
    {
        BatchEntry* batch = find_batch(batch_id);
        if( !batch || !batch->open )
            return;
        batch->open = false;
        for( auto& ch : batch->chunks )
        {
            ch.pending_verts.clear();
            ch.pending_verts.shrink_to_fit();
            ch.pending_indices.clear();
            ch.pending_indices.shrink_to_fit();
            ch.pending_vert_count = 0;
        }
    }

    void
    unload_batch(uint32_t batch_id)
    {
        BatchEntry* batch = find_batch(batch_id);
        if( !batch )
            return;
        batch->chunks.clear();
        batch->id = 0;
        batch->open = false;
        batch->in_use = false;
        batch->vert_size_bytes = 0;
        batch->active_chunk = 0;
    }

    /** Walk every in-use batch slot (for shutdown / leak sweep). */
    template<typename Fn>
    void
    for_each_in_use_batch(Fn&& fn)
    {
        for( int i = 0; i < kMaxBatches; ++i )
        {
            if( batches_[i].in_use )
                fn(batches_[i].id, &batches_[i]);
        }
    }

    const BatchChunk*
    get_batch_chunk(uint32_t batch_id, int chunk_index) const
    {
        const BatchEntry* b = find_batch_const(batch_id);
        if( !b || chunk_index < 0 || (size_t)chunk_index >= b->chunks.size() )
            return nullptr;
        return &b->chunks[(size_t)chunk_index];
    }

    // ------------------------------------------------------------------
    // Single-model path
    // ------------------------------------------------------------------

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
        r.va_gpu_id = -1;
        r.fa_gpu_id = -1;
        r.fa_first_face = 0;
        r.batch_chunk_index = -1;
        r.per_face_tex_id.clear();
        if( per_face_tex && face_count > 0 )
            r.per_face_tex_id.assign(per_face_tex, per_face_tex + face_count);
        return 0;
    }

    ModelBufferRange*
    get_instance(int model_gpu_id, int anim_id, int frame_id)
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

    BufH
    get_batch_vbo_for_chunk(uint32_t batch_id, int chunk_index) const
    {
        const BatchChunk* ch = get_batch_chunk(batch_id, chunk_index);
        return ch ? ch->vbo : null_;
    }

    BufH
    get_batch_ibo_for_chunk(uint32_t batch_id, int chunk_index) const
    {
        const BatchChunk* ch = get_batch_chunk(batch_id, chunk_index);
        return ch ? ch->ibo : null_;
    }

    const ModelBufferRange*
    get_instance_const(int model_gpu_id, int anim_id, int frame_id) const
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return nullptr;
        const ModelEntry& entry = entries_[model_gpu_id];
        if( anim_id < 0 || anim_id >= (int)entry.anims.size() )
            return nullptr;
        const AnimEntry& anim = entry.anims[(size_t)anim_id];
        if( frame_id < 0 || frame_id >= (int)anim.frames.size() )
            return nullptr;
        const ModelBufferRange& r = anim.frames[(size_t)frame_id];
        return r.loaded ? &r : nullptr;
    }

    BufH
    get_batch_vbo_for_model(int model_gpu_id) const
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return null_;
        const ModelEntry& entry = entries_[model_gpu_id];
        if( !entry.is_batched )
            return null_;
        const ModelBufferRange* r = get_instance_const(model_gpu_id, 0, 0);
        if( !r || r->batch_chunk_index < 0 )
            return null_;
        return get_batch_vbo_for_chunk(entry.batch_id, r->batch_chunk_index);
    }

    BufH
    get_batch_ibo_for_model(int model_gpu_id) const
    {
        if( model_gpu_id < 0 || model_gpu_id >= model_capacity_ )
            return null_;
        const ModelEntry& entry = entries_[model_gpu_id];
        if( !entry.is_batched )
            return null_;
        const ModelBufferRange* r = get_instance_const(model_gpu_id, 0, 0);
        if( !r || r->batch_chunk_index < 0 )
            return null_;
        return get_batch_ibo_for_chunk(entry.batch_id, r->batch_chunk_index);
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

private:
    ModelBufferRange&
    ensure_slot(int model_gpu_id, int anim_id, int frame_id)
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
                ModelBufferRange& z = anim.frames[k];
                z.buffer = null_;
                z.vtx_byte_offset = 0;
                z.vtx_count = 0;
                z.face_count = 0;
                z.owns_buffer = false;
                z.batch_index_first = 0;
                z.batch_index_count = 0;
                z.index_buffer = null_;
                z.owns_index_buffer = false;
                z.va_gpu_id = -1;
                z.fa_gpu_id = -1;
                z.fa_first_face = 0;
                z.batch_chunk_index = -1;
                z.loaded = false;
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
    BufH null_{};
    BatchCaps caps_{ INT_MAX, INT_MAX };
    std::vector<VertexArrayEntry> va_entries_;
    std::vector<FaceArrayEntry> fa_entries_;
};

#endif /* GPU_3D_CACHE_H */
