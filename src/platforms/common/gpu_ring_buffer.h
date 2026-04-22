#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

/**
 * GpuRingBuffer — triple-buffered (or N-buffered) dynamic upload ring with
 * renderer-supplied create/release/map callbacks.
 */
template<typename BufH>
class GpuRingBuffer
{
public:
    using CreateFn = BufH (*)(void* user, size_t bytes);
    using ReleaseFn = void (*)(void* user, BufH buf);
    using MapFn = void* (*)(void* user, BufH buf);

    void
    init(
        int inflight_slots,
        size_t initial_bytes,
        CreateFn create,
        ReleaseFn release,
        MapFn map,
        void* user)
    {
        destroy();
        create_ = create;
        release_ = release;
        map_ = map;
        user_ = user;
        slots_.resize((size_t)(inflight_slots > 0 ? inflight_slots : 1));
        initial_bytes_ = initial_bytes > 0 ? initial_bytes : (size_t)65536;
        for( size_t s = 0; s < slots_.size(); ++s )
        {
            slots_[s].buf = create_(user_, initial_bytes_);
            slots_[s].cap = initial_bytes_;
            slots_[s].write_off = 0;
        }
    }

    void
    destroy()
    {
        for( auto& sl : slots_ )
        {
            if( sl.buf && release_ )
                release_(user_, sl.buf);
            sl.buf = BufH{};
            sl.cap = 0;
            sl.write_off = 0;
        }
        slots_.clear();
        create_ = nullptr;
        release_ = nullptr;
        map_ = nullptr;
        user_ = nullptr;
    }

    void
    begin_slot(int slot)
    {
        if( slot < 0 || (size_t)slot >= slots_.size() )
            return;
        slots_[(size_t)slot].write_off = 0;
    }

    /** Returns CPU-writable pointer at *out_offset bytes into *out_buf. Grows buffer if needed. */
    void*
    reserve(int slot, size_t bytes, BufH* out_buf, size_t* out_offset)
    {
        if( !out_buf || !out_offset || slot < 0 || (size_t)slot >= slots_.size() || !map_ )
            return nullptr;
        Slot& sl = slots_[(size_t)slot];
        const size_t need = sl.write_off + bytes;
        if( need > sl.cap )
        {
            size_t n = sl.cap ? sl.cap : initial_bytes_;
            while( n < need )
                n *= 2;
            if( sl.buf && release_ )
                release_(user_, sl.buf);
            sl.buf = create_ ? create_(user_, n) : BufH{};
            sl.cap = n;
        }
        *out_buf = sl.buf;
        *out_offset = sl.write_off;
        void* ptr = map_(user_, sl.buf);
        if( !ptr )
            return nullptr;
        sl.write_off += bytes;
        return (char*)ptr + *out_offset;
    }

    BufH
    current(int slot) const
    {
        if( slot < 0 || (size_t)slot >= slots_.size() )
            return BufH{};
        return slots_[(size_t)slot].buf;
    }

    size_t
    write_off(int slot) const
    {
        if( slot < 0 || (size_t)slot >= slots_.size() )
            return 0;
        return slots_[(size_t)slot].write_off;
    }

private:
    struct Slot
    {
        BufH buf{};
        size_t cap = 0;
        size_t write_off = 0;
    };

    std::vector<Slot> slots_;
    size_t initial_bytes_ = 65536;
    CreateFn create_ = nullptr;
    ReleaseFn release_ = nullptr;
    MapFn map_ = nullptr;
    void* user_ = nullptr;
};
