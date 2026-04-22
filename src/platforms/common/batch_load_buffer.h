#pragma once

#ifndef PLATFORMS_COMMON_BATCH_LOAD_BUFFER_H
#define PLATFORMS_COMMON_BATCH_LOAD_BUFFER_H

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

/**
 * CPU-side growable buffer between batch BEGIN and batch END (GPU upload).
 * T must be trivially copyable so append/patch use memcpy.
 */
template<typename T>
class BatchLoadBuffer
{
    static_assert(
        std::is_trivially_copyable<T>::value,
        "BatchLoadBuffer<T>: T must be trivially copyable");

    std::vector<T> buf_;

public:
    void
    clear()
    {
        buf_.clear();
    }

    void
    clear_shrink()
    {
        buf_.clear();
        buf_.shrink_to_fit();
    }

    void
    reserve(size_t n)
    {
        buf_.reserve(n);
    }

    size_t
    size() const
    {
        return buf_.size();
    }

    bool
    empty() const
    {
        return buf_.empty();
    }

    size_t
    byte_size() const
    {
        return buf_.size() * sizeof(T);
    }

    const T*
    data() const
    {
        return buf_.data();
    }

    T*
    data()
    {
        return buf_.data();
    }

    /** Appends n elements copied from src. Returns element index of the start of the new run. */
    size_t
    append(const T* src, size_t n)
    {
        const size_t off = buf_.size();
        buf_.resize(off + n);
        if( n > 0 && src )
            memcpy(buf_.data() + off, src, n * sizeof(T));
        return off;
    }

    void
    push_back(const T& v)
    {
        buf_.push_back(v);
    }

    /** Overwrite buf_[dst_elem .. dst_elem+n). */
    bool
    patch(size_t dst_elem, const T* src, size_t n)
    {
        if( n == 0 )
            return true;
        if( !src || dst_elem + n > buf_.size() )
            return false;
        memcpy(buf_.data() + dst_elem, src, n * sizeof(T));
        return true;
    }

    /** Writable subrange; nullptr if out of range. */
    T*
    mutable_span(size_t elem_off, size_t n)
    {
        if( elem_off + n > buf_.size() )
            return nullptr;
        return buf_.data() + elem_off;
    }
};

#endif /* PLATFORMS_COMMON_BATCH_LOAD_BUFFER_H */
