#include "cs2vm2_strpool.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CS2VM2_StrPoolBlock
{
    struct CS2VM2_StrPoolBlock* next;
    size_t capacity;
    size_t used;
    char data[];
};

static struct CS2VM2_StrPoolBlock*
strpool_block_new(
    struct CS2VM2_StrPool* pool,
    size_t capacity)
{
    struct CS2VM2_StrPoolBlock* block = malloc(sizeof(*block) + capacity);
    assert(block);

    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;

    pool->block_count++;
    pool->bytes_reserved += capacity;
    return block;
}

void
CS2VM2_StrPool_Init(struct CS2VM2_StrPool* pool)
{
    assert(pool);
    memset(pool, 0, sizeof(*pool));
}

/* TORIRS_CS2_STRPOOL_DEBUG=1: one line per reset with what the finished script
 * spent, to spot a script whose string churn is out of hand. */
static int
strpool_debug(void)
{
    static int cached = -1;
    if( cached < 0 )
        cached = getenv("TORIRS_CS2_STRPOOL_DEBUG") ? 1 : 0;
    return cached;
}

void
CS2VM2_StrPool_Reset(struct CS2VM2_StrPool* pool)
{
    assert(pool);

    if( strpool_debug() && pool->alloc_count > 0 )
        fprintf(
            stderr,
            "CS2 strpool reset: %d strings, %zu bytes in %d block(s) (peak %zu)\n",
            pool->alloc_count,
            pool->bytes_used,
            pool->block_count,
            pool->bytes_peak);

    /* Retain the first regular block for the next script; oversize blocks (which
     * exist because one string needed them) are not worth holding on to. */
    struct CS2VM2_StrPoolBlock* keep = NULL;
    struct CS2VM2_StrPoolBlock* block = pool->blocks;
    while( block )
    {
        struct CS2VM2_StrPoolBlock* next = block->next;
        if( !keep && block->capacity == CS2VM2_STRPOOL_BLOCK_BYTES )
        {
            keep = block;
            keep->used = 0;
            keep->next = NULL;
        }
        else
        {
            free(block);
        }
        block = next;
    }

    pool->blocks = keep;
    pool->block_count = keep ? 1 : 0;
    pool->bytes_reserved = keep ? keep->capacity : 0;
    pool->bytes_used = 0;
    pool->alloc_count = 0;
    pool->reset_count++;
}

void
CS2VM2_StrPool_ResetKeepBlocks(struct CS2VM2_StrPool* pool)
{
    assert(pool);

    for( struct CS2VM2_StrPoolBlock* block = pool->blocks; block; block = block->next )
        block->used = 0;
    pool->bytes_used = 0;
    pool->alloc_count = 0;
    pool->reset_count++;
}

void*
CS2VM2_StrPool_AllocAligned(
    struct CS2VM2_StrPool* pool,
    size_t bytes,
    size_t align)
{
    assert(pool);
    assert(align > 0);
    assert((align & (align - 1)) == 0);

    /*
     * Over-allocate by align-1 and round the returned pointer up, rather than
     * aligning the block's bump cursor: the cursor is shared with the string
     * allocations, and rounding it would silently change where they land.
     * The slack is at most 7 bytes per array definition.
     */
    char* raw = CS2VM2_StrPool_Alloc(pool, bytes + align - 1u);
    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + (align - 1u)) & ~(uintptr_t)(align - 1u);
    return (void*)aligned;
}

void
CS2VM2_StrPool_Free(struct CS2VM2_StrPool* pool)
{
    assert(pool);

    struct CS2VM2_StrPoolBlock* block = pool->blocks;
    while( block )
    {
        struct CS2VM2_StrPoolBlock* next = block->next;
        free(block);
        block = next;
    }

    size_t peak = pool->bytes_peak;
    CS2VM2_StrPool_Init(pool);
    pool->bytes_peak = peak;
}

char*
CS2VM2_StrPool_Alloc(
    struct CS2VM2_StrPool* pool,
    size_t len)
{
    assert(pool);

    size_t need = len + 1u; /* + the NUL */
    struct CS2VM2_StrPoolBlock* block = pool->blocks;

    if( need > CS2VM2_STRPOOL_BLOCK_BYTES )
    {
        /* Oversize: an exact-fit block of its own, spliced in *behind* the head
         * so the head keeps serving the small allocations that follow. */
        struct CS2VM2_StrPoolBlock* big = strpool_block_new(pool, need);
        if( block )
        {
            big->next = block->next;
            block->next = big;
        }
        else
        {
            pool->blocks = big;
        }
        block = big;
    }
    else if( !block || block->capacity - block->used < need )
    {
        block = strpool_block_new(pool, CS2VM2_STRPOOL_BLOCK_BYTES);
        block->next = pool->blocks;
        pool->blocks = block;
    }

    char* out = block->data + block->used;
    block->used += need;
    out[len] = '\0';

    pool->bytes_used += need;
    if( pool->bytes_used > pool->bytes_peak )
        pool->bytes_peak = pool->bytes_used;
    pool->alloc_count++;
    return out;
}

char*
CS2VM2_StrPool_Dup(
    struct CS2VM2_StrPool* pool,
    char const* text)
{
    assert(pool);
    assert(text);
    return CS2VM2_StrPool_DupLen(pool, text, strlen(text));
}

char*
CS2VM2_StrPool_DupLen(
    struct CS2VM2_StrPool* pool,
    char const* text,
    size_t len)
{
    assert(pool);
    assert(text);

    char* out = CS2VM2_StrPool_Alloc(pool, len);
    if( len > 0 )
        memcpy(out, text, len);
    return out;
}

char*
CS2VM2_StrPool_VFmt(
    struct CS2VM2_StrPool* pool,
    char const* fmt,
    va_list args)
{
    assert(pool);
    assert(fmt);

    /* Format once into a scratch buffer: that both measures the result and, for
     * the common short case, produces it — so only long strings format twice. */
    char scratch[256];
    va_list retry;
    va_copy(retry, args);
    int len = vsnprintf(scratch, sizeof(scratch), fmt, args);
    if( len < 0 )
    {
        va_end(retry);
        return CS2VM2_StrPool_Empty(pool);
    }

    char* out = CS2VM2_StrPool_Alloc(pool, (size_t)len);
    if( (size_t)len < sizeof(scratch) )
        memcpy(out, scratch, (size_t)len);
    else
        (void)vsnprintf(out, (size_t)len + 1u, fmt, retry);
    va_end(retry);
    return out;
}

char*
CS2VM2_StrPool_Fmt(
    struct CS2VM2_StrPool* pool,
    char const* fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);
    char* out = CS2VM2_StrPool_VFmt(pool, fmt, args);
    va_end(args);
    return out;
}

char*
CS2VM2_StrPool_Empty(struct CS2VM2_StrPool* pool)
{
    assert(pool);
    (void)pool;
    /*
     * One shared "", not a pool allocation per call.
     *
     * Distinct storage per call was needed only while UPPERCASE / LOWERCASE
     * rewrote their operand in place; with every VM string immutable, an empty
     * string has nothing to distinguish. This is also the most-pushed string in
     * the VM — every unwritten string cell and every stubbed getter answers with
     * it — so it was a steady drip of pool bumps for no content.
     *
     * Non-const on purpose: the VM moves char* everywhere. Nothing writes
     * through a VM string; if something ever did, it would fault here rather
     * than corrupt a shared buffer, which is the failure worth having.
     */
    static char empty[1] = { '\0' };
    return empty;
}
