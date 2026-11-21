#ifndef SRC_UI_UITREE_POOL_H
#define SRC_UI_UITREE_POOL_H

/*
 * THE UI TREE'S OWN POOLS.
 *
 * A container rebuild reclaims every row and creates it again. Opening the
 * bank used to cost about 3,500 trips to the allocator for the per-node data
 * those rows carry: 2,000 option and handler blocks of 900 to 1,300 bytes,
 * 1,000 handler argument lists, 200 short strings. A tree keeps what a
 * reclaimed row releases and hands it to the next row, in two pools of its
 * own. Both belong to one tree, are emptied when it is torn down, and start
 * empty in a checkpoint's copy.
 *
 * Under AddressSanitizer neither keeps anything (UITREE_POOLS_ON is 0): a
 * recycled block would hide the use after free the sanitizer reports.
 */

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
#define UITREE_POOLS_ON 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define UITREE_POOLS_ON 0
#endif
#endif
#ifndef UITREE_POOLS_ON
#define UITREE_POOLS_ON 1
#endif

/*
 * UITreeBlockPool: released blocks of ONE size.
 *
 * For a node's menu-options block, its handler block and a handler's int
 * argument list. These stay ordinary ToriMem_Ui() blocks, each its own
 * allocation, because code that holds only a component (a copy of a handler,
 * a test, a checkpoint) frees them with ToriMem_Free; a pooled one is simply
 * a block nobody has freed yet.
 */
struct UITreeBlockPool
{
    /** Released blocks, linked through their first pointer-sized bytes. */
    void* head;
    uint32_t count;
};

/** A zeroed block of `size` bytes, or NULL when the pool is empty. */
static inline void*
UITreeBlockPool_Take(struct UITreeBlockPool* pool, size_t size)
{
    void* block;
    assert(pool);
    assert(size >= sizeof(void*));
    block = pool->head;
    if( !block )
        return NULL;
    memcpy(&pool->head, block, sizeof(void*));
    pool->count--;
    /* A released block has not been touched since, so it is cold, and a
     * rebuild takes hundreds in a row: ask for the next one now. */
    if( pool->head )
        __builtin_prefetch(pool->head, 1);
    memset(block, 0, size);
    return block;
}

/** Keep `block` for reuse. False when the pool already holds `max`: the
 *  caller frees it. The block must own nothing by now. */
static inline bool
UITreeBlockPool_Give(struct UITreeBlockPool* pool, void* block, uint32_t max)
{
    assert(pool);
    assert(block);
    if( !UITREE_POOLS_ON || pool->count >= max )
        return false;
    memcpy(block, &pool->head, sizeof(void*));
    pool->head = block;
    pool->count++;
    return true;
}

/** Free every pooled block. */
static inline void
UITreeBlockPool_Drain(struct UITreeBlockPool* pool)
{
    assert(pool);
    while( pool->head )
    {
        void* const block = pool->head;
        memcpy(&pool->head, block, sizeof(void*));
        ToriMem_Free(ToriMem_Ui(), block);
    }
    pool->count = 0;
}

/*
 * UITreeStringPool: short strings, in cells the tree owns.
 *
 * A node's text is nearly always a short label. A string that fits a cell
 * (UITREE_STRING_CELL bytes with its terminator) is stored in one, carved
 * from chunks of UITREE_STRING_CHUNK_CELLS cells; a released cell goes on a
 * free list for the next string. So a short string costs the allocator
 * nothing, new or recycled, beyond one chunk per thousand strings. A longer
 * string is an ordinary allocation.
 *
 * A cell is not an allocation of its own: it must come back through
 * UITreeStringPool_Free, which answers false for a string that is not the
 * pool's (a long one, or one a checkpoint copied) so the caller frees that.
 * Every free of a node's text is in ui/uitree.c and has the tree in hand.
 */
#define UITREE_STRING_CELL 32u
#define UITREE_STRING_CHUNK_CELLS 1024u

struct UITreeStringChunk
{
    struct UITreeStringChunk* next;
    char cells[UITREE_STRING_CHUNK_CELLS * UITREE_STRING_CELL];
};

struct UITreeStringPool
{
    struct UITreeStringChunk* chunks;
    /** Released cells, linked through their first pointer-sized bytes. */
    void* free_cells;
    /** Cells of the newest chunk not handed out yet. */
    uint32_t fresh;
    uint32_t live;
};

/** A copy of `text` in a cell, or NULL when it does not fit one (the caller
 *  allocates it). */
char*
UITreeStringPool_Dup(struct UITreeStringPool* pool, char const* text);

/** Release a string. False when it is not one of the pool's cells. */
bool
UITreeStringPool_Free(struct UITreeStringPool* pool, char* text);

/** Free every chunk. No cell may still be in use. */
void
UITreeStringPool_Release(struct UITreeStringPool* pool);

#endif
