#include "ssvm_strpool.h"
#include <assert.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SSVM_StrPoolBlock
{
    struct SSVM_StrPoolBlock* next;
    size_t capacity;
    size_t used;
    /* The block's bytes follow the header in the same allocation. */
    char data[];
};

void
SSVM_StrPoolInit(struct SSVM_StrPool* pool)
{
    assert(pool);
    pool->head = NULL;
    pool->total_bytes = 0;
    pool->exhausted = 0;
}

void
SSVM_StrPoolFree(struct SSVM_StrPool* pool)
{
    struct SSVM_StrPoolBlock* block;

    if( !pool )
        return;

    block = pool->head;
    while( block )
    {
        struct SSVM_StrPoolBlock* next = block->next;

        free(block);
        block = next;
    }
    pool->head = NULL;
    pool->total_bytes = 0;
    pool->exhausted = 0;
}

char*
SSVM_StrPoolAlloc(
    struct SSVM_StrPool* pool,
    size_t length)
{
    size_t needed;
    struct SSVM_StrPoolBlock* block;
    char* out;

    assert(pool);
    if( pool->exhausted )
        return NULL;

    needed = length + 1; /* terminator */

    /* Only the head block is ever bumped. An allocation that does not fit gets
     * a fresh block rather than searching older ones — the wasted tail of the
     * previous block is cheaper than the search, and scripts allocate in a
     * roughly monotonic pattern anyway. */
    if( pool->head && pool->head->capacity - pool->head->used >= needed )
    {
        out = pool->head->data + pool->head->used;
        pool->head->used += needed;
        memset(out, 0, needed);
        return out;
    }

    {
        size_t capacity = needed > SSVM_STRPOOL_BLOCK_BYTES ? needed : SSVM_STRPOOL_BLOCK_BYTES;

        if( pool->total_bytes + capacity > SSVM_STRPOOL_MAX_BYTES )
        {
            pool->exhausted = 1;
            return NULL;
        }

        block = (struct SSVM_StrPoolBlock*)malloc(sizeof(*block) + capacity);
        assert(block);
        block->next = pool->head;
        block->capacity = capacity;
        block->used = needed;
        pool->head = block;
        pool->total_bytes += capacity;

        memset(block->data, 0, needed);
        return block->data;
    }
}

char*
SSVM_StrPoolDupLen(
    struct SSVM_StrPool* pool,
    const char* text,
    size_t length)
{
    char* out = SSVM_StrPoolAlloc(pool, length);

    if( !out )
        return NULL;
    if( text && length )
        memcpy(out, text, length);
    out[length] = '\0';
    return out;
}

char*
SSVM_StrPoolDup(
    struct SSVM_StrPool* pool,
    const char* text)
{
    return SSVM_StrPoolDupLen(pool, text, text ? strlen(text) : 0);
}

char*
SSVM_StrPoolCat(
    struct SSVM_StrPool* pool,
    const char* left,
    const char* right)
{
    size_t left_length = left ? strlen(left) : 0;
    size_t right_length = right ? strlen(right) : 0;
    char* out = SSVM_StrPoolAlloc(pool, left_length + right_length);

    if( !out )
        return NULL;
    if( left_length )
        memcpy(out, left, left_length);
    if( right_length )
        memcpy(out + left_length, right, right_length);
    out[left_length + right_length] = '\0';
    return out;
}

char*
SSVM_StrPoolFmt(
    struct SSVM_StrPool* pool,
    const char* fmt,
    ...)
{
    va_list args;
    va_list measure;
    int length;
    char* out;

    assert(pool);
    assert(fmt);

    va_start(args, fmt);
    va_copy(measure, args);
    length = vsnprintf(NULL, 0, fmt, measure);
    va_end(measure);

    if( length < 0 )
    {
        va_end(args);
        return NULL;
    }

    out = SSVM_StrPoolAlloc(pool, (size_t)length);
    if( out )
        vsnprintf(out, (size_t)length + 1, fmt, args);
    va_end(args);
    return out;
}
