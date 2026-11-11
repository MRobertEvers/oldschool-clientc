#ifndef TRSPK_ORDER_SORT_H
#define TRSPK_ORDER_SORT_H

#include <stdint.h>

/*
 * A draw order, sorted without moving the records it orders.
 *
 * The depth renderers gather a frame's opaque work as small records (an array
 * range, a page segment) and sort them every frame so that runs which can be
 * one draw sit together. qsort of the records spent ~1 ms a frame on a WebGL2
 * replay -- 10% of the main thread -- nearly all of it in musl's smoothsort
 * moving 12- and 16-byte elements, not comparing them.
 *
 * So each record becomes one 64-bit word: its sort key above, its own index in
 * the low `index_bits`. An LSD radix sort over the key bits only, eight at a
 * time, orders the words; it is stable, so the index rides along and records
 * with equal keys keep submission order. A digit every word shares costs one
 * counting pass and no scatter, so a frame whose ranges all sit on one binding
 * pays for the bits that differ and nothing else.
 */
struct TRSPK_OrderBuffer
{
    uint64_t* words;
    uint64_t* scratch;
    uint32_t capacity;
};

/** Room for `count` words in both halves; grows, never shrinks. */
void
trspk_order_buffer_reserve(
    struct TRSPK_OrderBuffer* buffer,
    uint32_t count);

void
trspk_order_buffer_free(struct TRSPK_OrderBuffer* buffer);

static inline uint64_t
trspk_order_word(
    uint64_t key,
    uint32_t index,
    uint32_t index_bits)
{
    return (key << index_bits) | (uint64_t)index;
}

static inline uint32_t
trspk_order_index(
    uint64_t word,
    uint32_t index_bits)
{
    return (uint32_t)(word & ((1ull << index_bits) - 1ull));
}

/**
 * Sort buffer->words[0, count) by bits [index_bits, index_bits + key_bits).
 * Returns the half holding the result (words or scratch).
 */
const uint64_t*
trspk_order_sort(
    struct TRSPK_OrderBuffer* buffer,
    uint32_t count,
    uint32_t index_bits,
    uint32_t key_bits);

#endif /* TRSPK_ORDER_SORT_H */
