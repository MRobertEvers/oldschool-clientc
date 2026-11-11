#include "trspk_order_sort.h"
#include "../trspk_alloc.h"

#include <assert.h>
#include <string.h>

#define TRSPK_ORDER_DIGIT_BITS 8u
#define TRSPK_ORDER_DIGIT_MASK ((1u << TRSPK_ORDER_DIGIT_BITS) - 1u)

void
trspk_order_buffer_reserve(
    struct TRSPK_OrderBuffer* buffer,
    uint32_t count)
{
    uint32_t capacity;
    assert(buffer);
    if( count <= buffer->capacity )
        return;
    capacity = buffer->capacity ? buffer->capacity : 1024u;
    while( capacity < count )
        capacity *= 2u;
    buffer->words = (uint64_t*)TRSPK_REALLOC(buffer->words, (size_t)capacity * sizeof(uint64_t));
    assert(buffer->words);
    buffer->scratch =
        (uint64_t*)TRSPK_REALLOC(buffer->scratch, (size_t)capacity * sizeof(uint64_t));
    assert(buffer->scratch);
    buffer->capacity = capacity;
}

void
trspk_order_buffer_free(struct TRSPK_OrderBuffer* buffer)
{
    if( !buffer )
        return;
    if( buffer->words )
        TRSPK_FREE(buffer->words);
    if( buffer->scratch )
        TRSPK_FREE(buffer->scratch);
    buffer->words = NULL;
    buffer->scratch = NULL;
    buffer->capacity = 0u;
}

const uint64_t*
trspk_order_sort(
    struct TRSPK_OrderBuffer* buffer,
    uint32_t count,
    uint32_t index_bits,
    uint32_t key_bits)
{
    uint64_t* src;
    uint64_t* dst;
    uint32_t shift;
    assert(buffer);
    assert(count <= buffer->capacity);
    assert(index_bits + key_bits <= 64u);
    src = buffer->words;
    dst = buffer->scratch;
    if( count < 2u )
        return src;
    for( shift = index_bits; shift < index_bits + key_bits; shift += TRSPK_ORDER_DIGIT_BITS )
    {
        uint32_t histogram[TRSPK_ORDER_DIGIT_MASK + 1u];
        uint32_t offset = 0u;
        uint32_t digit;
        uint32_t i;
        uint64_t* swap;
        memset(histogram, 0, sizeof(histogram));
        for( i = 0u; i < count; i++ )
            histogram[(src[i] >> shift) & TRSPK_ORDER_DIGIT_MASK]++;
        if( histogram[(src[0] >> shift) & TRSPK_ORDER_DIGIT_MASK] == count )
            continue;
        for( digit = 0u; digit <= TRSPK_ORDER_DIGIT_MASK; digit++ )
        {
            uint32_t n = histogram[digit];
            histogram[digit] = offset;
            offset += n;
        }
        for( i = 0u; i < count; i++ )
            dst[histogram[(src[i] >> shift) & TRSPK_ORDER_DIGIT_MASK]++] = src[i];
        swap = src;
        src = dst;
        dst = swap;
    }
    return src;
}
