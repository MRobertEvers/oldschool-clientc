#include "cs2_support.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Arena
 * ---------------------------------------------------------------------- */

#define CS2_ARENA_BLOCK_BYTES (256 * 1024)

struct RSCache_CS2_ArenaBlock
{
    struct RSCache_CS2_ArenaBlock* next;
    size_t size;
    size_t used;
    unsigned char data[];
};

void
RSCache_CS2_ArenaInit(struct RSCache_CS2_Arena* arena)
{
    assert(arena);
    arena->head = NULL;
    arena->bytes_allocated = 0;
}

static struct RSCache_CS2_ArenaBlock*
cs2_arena_new_block(size_t min_size)
{
    size_t size = CS2_ARENA_BLOCK_BYTES;
    if( size < min_size )
        size = min_size;

    struct RSCache_CS2_ArenaBlock* block =
        (struct RSCache_CS2_ArenaBlock*)malloc(sizeof(*block) + size);
    if( !block )
    {
        fprintf(stderr, "rscache cs2: arena out of memory (%zu bytes)\n", size);
        abort();
    }
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

void*
RSCache_CS2_ArenaAlloc(struct RSCache_CS2_Arena* arena, size_t size)
{
    assert(arena);
    if( size == 0 )
        size = 1;
    /* Every IR node is at least pointer-aligned; nothing here needs more. */
    size = (size + (sizeof(void*) - 1)) & ~(size_t)(sizeof(void*) - 1);

    if( !arena->head || arena->head->used + size > arena->head->size )
    {
        struct RSCache_CS2_ArenaBlock* block = cs2_arena_new_block(size);
        block->next = arena->head;
        arena->head = block;
    }

    void* out = arena->head->data + arena->head->used;
    arena->head->used += size;
    arena->bytes_allocated += size;
    memset(out, 0, size);
    return out;
}

char*
RSCache_CS2_ArenaStrDup(struct RSCache_CS2_Arena* arena, const char* s)
{
    if( !s )
        return NULL;
    return RSCache_CS2_ArenaStrDupN(arena, s, strlen(s));
}

char*
RSCache_CS2_ArenaStrDupN(struct RSCache_CS2_Arena* arena, const char* s, size_t n)
{
    char* out = (char*)RSCache_CS2_ArenaAlloc(arena, n + 1);
    if( n )
        memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

void
RSCache_CS2_ArenaFree(struct RSCache_CS2_Arena* arena)
{
    if( !arena )
        return;
    struct RSCache_CS2_ArenaBlock* block = arena->head;
    while( block )
    {
        struct RSCache_CS2_ArenaBlock* next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
    arena->bytes_allocated = 0;
}

/* -------------------------------------------------------------------------
 * Vector of pointers
 * ---------------------------------------------------------------------- */

void
RSCache_CS2_VecInit(struct RSCache_CS2_Vec* vec)
{
    assert(vec);
    vec->items = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

static void
cs2_vec_reserve(struct RSCache_CS2_Vec* vec, int needed)
{
    if( needed <= vec->capacity )
        return;
    int capacity = vec->capacity ? vec->capacity * 2 : 8;
    while( capacity < needed )
        capacity *= 2;
    void** items = (void**)realloc(vec->items, (size_t)capacity * sizeof(void*));
    if( !items )
    {
        fprintf(stderr, "rscache cs2: vector out of memory\n");
        abort();
    }
    vec->items = items;
    vec->capacity = capacity;
}

void
RSCache_CS2_VecPush(struct RSCache_CS2_Vec* vec, void* item)
{
    cs2_vec_reserve(vec, vec->count + 1);
    vec->items[vec->count++] = item;
}

void
RSCache_CS2_VecInsert(struct RSCache_CS2_Vec* vec, int index, void* item)
{
    assert(index >= 0 && index <= vec->count);
    cs2_vec_reserve(vec, vec->count + 1);
    memmove(
        &vec->items[index + 1],
        &vec->items[index],
        (size_t)(vec->count - index) * sizeof(void*));
    vec->items[index] = item;
    vec->count++;
}

void
RSCache_CS2_VecRemoveAt(struct RSCache_CS2_Vec* vec, int index)
{
    assert(index >= 0 && index < vec->count);
    memmove(
        &vec->items[index],
        &vec->items[index + 1],
        (size_t)(vec->count - index - 1) * sizeof(void*));
    vec->count--;
}

bool
RSCache_CS2_VecRemove(struct RSCache_CS2_Vec* vec, void* item)
{
    int index = RSCache_CS2_VecIndexOf(vec, item);
    if( index < 0 )
        return false;
    RSCache_CS2_VecRemoveAt(vec, index);
    return true;
}

int
RSCache_CS2_VecIndexOf(const struct RSCache_CS2_Vec* vec, void* item)
{
    for( int i = 0; i < vec->count; i++ )
    {
        if( vec->items[i] == item )
            return i;
    }
    return -1;
}

bool
RSCache_CS2_VecContains(const struct RSCache_CS2_Vec* vec, void* item)
{
    return RSCache_CS2_VecIndexOf(vec, item) >= 0;
}

bool
RSCache_CS2_VecAddUnique(struct RSCache_CS2_Vec* vec, void* item)
{
    if( RSCache_CS2_VecContains(vec, item) )
        return false;
    RSCache_CS2_VecPush(vec, item);
    return true;
}

void
RSCache_CS2_VecClear(struct RSCache_CS2_Vec* vec)
{
    vec->count = 0;
}

void
RSCache_CS2_VecFree(struct RSCache_CS2_Vec* vec)
{
    if( !vec )
        return;
    free(vec->items);
    vec->items = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

void**
RSCache_CS2_VecToArena(const struct RSCache_CS2_Vec* vec, struct RSCache_CS2_Arena* arena)
{
    if( vec->count == 0 )
        return NULL;
    void** out = (void**)RSCache_CS2_ArenaAlloc(arena, (size_t)vec->count * sizeof(void*));
    memcpy(out, vec->items, (size_t)vec->count * sizeof(void*));
    return out;
}

/* -------------------------------------------------------------------------
 * Vector of ints
 * ---------------------------------------------------------------------- */

void
RSCache_CS2_IntVecInit(struct RSCache_CS2_IntVec* vec)
{
    assert(vec);
    vec->items = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

void
RSCache_CS2_IntVecPush(struct RSCache_CS2_IntVec* vec, int value)
{
    if( vec->count + 1 > vec->capacity )
    {
        int capacity = vec->capacity ? vec->capacity * 2 : 8;
        int* items = (int*)realloc(vec->items, (size_t)capacity * sizeof(int));
        if( !items )
        {
            fprintf(stderr, "rscache cs2: int vector out of memory\n");
            abort();
        }
        vec->items = items;
        vec->capacity = capacity;
    }
    vec->items[vec->count++] = value;
}

bool
RSCache_CS2_IntVecContains(const struct RSCache_CS2_IntVec* vec, int value)
{
    for( int i = 0; i < vec->count; i++ )
    {
        if( vec->items[i] == value )
            return true;
    }
    return false;
}

bool
RSCache_CS2_IntVecAddUnique(struct RSCache_CS2_IntVec* vec, int value)
{
    if( RSCache_CS2_IntVecContains(vec, value) )
        return false;
    RSCache_CS2_IntVecPush(vec, value);
    return true;
}

void
RSCache_CS2_IntVecClear(struct RSCache_CS2_IntVec* vec)
{
    vec->count = 0;
}

void
RSCache_CS2_IntVecFree(struct RSCache_CS2_IntVec* vec)
{
    if( !vec )
        return;
    free(vec->items);
    vec->items = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

/* -------------------------------------------------------------------------
 * Pointer-keyed hash map (open addressing, linear probe)
 *
 * Removal writes a tombstone-free hole and back-shifts the cluster, so lookups
 * stay correct without a deleted marker. Maps here are small (a few thousand
 * entries at most) and removal is rare, so the shift is cheaper than carrying
 * tombstones that never get compacted across the DFA passes' repeated removes.
 * ---------------------------------------------------------------------- */

#define CS2_MAP_MIN_CAPACITY 16

static size_t
cs2_hash_pointer(const void* p)
{
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x;
}

static size_t
cs2_hash_int(int v)
{
    uint32_t x = (uint32_t)v;
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (size_t)x;
}

void
RSCache_CS2_MapInit(struct RSCache_CS2_Map* map)
{
    assert(map);
    map->entries = NULL;
    map->count = 0;
    map->capacity = 0;
}

static int
cs2_map_slot(const struct RSCache_CS2_Map* map, const void* key)
{
    int mask = map->capacity - 1;
    int i = (int)(cs2_hash_pointer(key) & (size_t)mask);
    while( map->entries[i].occupied && map->entries[i].key != key )
        i = (i + 1) & mask;
    return i;
}

static void
cs2_map_grow(struct RSCache_CS2_Map* map)
{
    int capacity = map->capacity ? map->capacity * 2 : CS2_MAP_MIN_CAPACITY;
    struct RSCache_CS2_MapEntry* entries =
        (struct RSCache_CS2_MapEntry*)calloc((size_t)capacity, sizeof(*entries));
    if( !entries )
    {
        fprintf(stderr, "rscache cs2: map out of memory\n");
        abort();
    }

    struct RSCache_CS2_MapEntry* old = map->entries;
    int old_capacity = map->capacity;
    map->entries = entries;
    map->capacity = capacity;

    int mask = capacity - 1;
    for( int i = 0; i < old_capacity; i++ )
    {
        if( !old[i].occupied )
            continue;
        int j = (int)(cs2_hash_pointer(old[i].key) & (size_t)mask);
        while( entries[j].occupied )
            j = (j + 1) & mask;
        entries[j] = old[i];
    }
    free(old);
}

void
RSCache_CS2_MapPut(struct RSCache_CS2_Map* map, const void* key, void* value)
{
    if( map->capacity == 0 || (map->count + 1) * 4 >= map->capacity * 3 )
        cs2_map_grow(map);
    int i = cs2_map_slot(map, key);
    if( !map->entries[i].occupied )
    {
        map->entries[i].occupied = true;
        map->entries[i].key = key;
        map->count++;
    }
    map->entries[i].value = value;
}

void*
RSCache_CS2_MapGet(const struct RSCache_CS2_Map* map, const void* key)
{
    if( map->capacity == 0 )
        return NULL;
    int i = cs2_map_slot(map, key);
    return map->entries[i].occupied ? map->entries[i].value : NULL;
}

bool
RSCache_CS2_MapContains(const struct RSCache_CS2_Map* map, const void* key)
{
    if( map->capacity == 0 )
        return false;
    return map->entries[cs2_map_slot(map, key)].occupied;
}

void*
RSCache_CS2_MapRemove(struct RSCache_CS2_Map* map, const void* key)
{
    if( map->capacity == 0 )
        return NULL;
    int mask = map->capacity - 1;
    int i = cs2_map_slot(map, key);
    if( !map->entries[i].occupied )
        return NULL;

    void* value = map->entries[i].value;
    map->entries[i].occupied = false;
    map->count--;

    /* Back-shift the rest of the cluster so no probe sequence is broken. */
    int j = (i + 1) & mask;
    while( map->entries[j].occupied )
    {
        struct RSCache_CS2_MapEntry moved = map->entries[j];
        map->entries[j].occupied = false;
        map->count--;
        int k = cs2_map_slot(map, moved.key);
        map->entries[k] = moved;
        map->count++;
        j = (j + 1) & mask;
    }
    return value;
}

void
RSCache_CS2_MapFree(struct RSCache_CS2_Map* map)
{
    if( !map )
        return;
    free(map->entries);
    map->entries = NULL;
    map->count = 0;
    map->capacity = 0;
}

/* -------------------------------------------------------------------------
 * Int-keyed hash map
 * ---------------------------------------------------------------------- */

void
RSCache_CS2_IntMapInit(struct RSCache_CS2_IntMap* map)
{
    assert(map);
    map->entries = NULL;
    map->count = 0;
    map->capacity = 0;
}

static int
cs2_int_map_slot(const struct RSCache_CS2_IntMap* map, int key)
{
    int mask = map->capacity - 1;
    int i = (int)(cs2_hash_int(key) & (size_t)mask);
    while( map->entries[i].occupied && map->entries[i].key != key )
        i = (i + 1) & mask;
    return i;
}

static void
cs2_int_map_grow(struct RSCache_CS2_IntMap* map)
{
    int capacity = map->capacity ? map->capacity * 2 : CS2_MAP_MIN_CAPACITY;
    struct RSCache_CS2_IntMapEntry* entries =
        (struct RSCache_CS2_IntMapEntry*)calloc((size_t)capacity, sizeof(*entries));
    if( !entries )
    {
        fprintf(stderr, "rscache cs2: int map out of memory\n");
        abort();
    }

    struct RSCache_CS2_IntMapEntry* old = map->entries;
    int old_capacity = map->capacity;
    map->entries = entries;
    map->capacity = capacity;

    int mask = capacity - 1;
    for( int i = 0; i < old_capacity; i++ )
    {
        if( !old[i].occupied )
            continue;
        int j = (int)(cs2_hash_int(old[i].key) & (size_t)mask);
        while( entries[j].occupied )
            j = (j + 1) & mask;
        entries[j] = old[i];
    }
    free(old);
}

void
RSCache_CS2_IntMapPut(struct RSCache_CS2_IntMap* map, int key, void* value)
{
    if( map->capacity == 0 || (map->count + 1) * 4 >= map->capacity * 3 )
        cs2_int_map_grow(map);
    int i = cs2_int_map_slot(map, key);
    if( !map->entries[i].occupied )
    {
        map->entries[i].occupied = true;
        map->entries[i].key = key;
        map->count++;
    }
    map->entries[i].value = value;
}

void*
RSCache_CS2_IntMapGet(const struct RSCache_CS2_IntMap* map, int key)
{
    if( map->capacity == 0 )
        return NULL;
    int i = cs2_int_map_slot(map, key);
    return map->entries[i].occupied ? map->entries[i].value : NULL;
}

bool
RSCache_CS2_IntMapContains(const struct RSCache_CS2_IntMap* map, int key)
{
    if( map->capacity == 0 )
        return false;
    return map->entries[cs2_int_map_slot(map, key)].occupied;
}

void*
RSCache_CS2_IntMapRemove(struct RSCache_CS2_IntMap* map, int key)
{
    if( map->capacity == 0 )
        return NULL;
    int mask = map->capacity - 1;
    int i = cs2_int_map_slot(map, key);
    if( !map->entries[i].occupied )
        return NULL;

    void* value = map->entries[i].value;
    map->entries[i].occupied = false;
    map->count--;

    int j = (i + 1) & mask;
    while( map->entries[j].occupied )
    {
        struct RSCache_CS2_IntMapEntry moved = map->entries[j];
        map->entries[j].occupied = false;
        map->count--;
        int k = cs2_int_map_slot(map, moved.key);
        map->entries[k] = moved;
        map->count++;
        j = (j + 1) & mask;
    }
    return value;
}

void
RSCache_CS2_IntMapFree(struct RSCache_CS2_IntMap* map)
{
    if( !map )
        return;
    free(map->entries);
    map->entries = NULL;
    map->count = 0;
    map->capacity = 0;
}

/* -------------------------------------------------------------------------
 * String builder
 * ---------------------------------------------------------------------- */

void
RSCache_CS2_StrBufInit(struct RSCache_CS2_StrBuf* buf)
{
    assert(buf);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

static void
cs2_strbuf_reserve(struct RSCache_CS2_StrBuf* buf, int needed)
{
    /* +1 so RSCache_CS2_StrBufCStr can always terminate without growing. */
    if( needed + 1 <= buf->capacity )
        return;
    int capacity = buf->capacity ? buf->capacity * 2 : 256;
    while( capacity < needed + 1 )
        capacity *= 2;
    char* data = (char*)realloc(buf->data, (size_t)capacity);
    if( !data )
    {
        fprintf(stderr, "rscache cs2: string buffer out of memory\n");
        abort();
    }
    buf->data = data;
    buf->capacity = capacity;
}

void
RSCache_CS2_StrBufAppendN(struct RSCache_CS2_StrBuf* buf, const char* s, int n)
{
    if( n <= 0 )
        return;
    cs2_strbuf_reserve(buf, buf->length + n);
    memcpy(buf->data + buf->length, s, (size_t)n);
    buf->length += n;
}

void
RSCache_CS2_StrBufAppend(struct RSCache_CS2_StrBuf* buf, const char* s)
{
    if( !s )
    {
        /* Kotlin's StringBuilder.append(null) writes the four characters. */
        RSCache_CS2_StrBufAppendN(buf, "null", 4);
        return;
    }
    RSCache_CS2_StrBufAppendN(buf, s, (int)strlen(s));
}

void
RSCache_CS2_StrBufAppendChar(struct RSCache_CS2_StrBuf* buf, char c)
{
    cs2_strbuf_reserve(buf, buf->length + 1);
    buf->data[buf->length++] = c;
}

void
RSCache_CS2_StrBufAppendInt(struct RSCache_CS2_StrBuf* buf, int value)
{
    char tmp[16];
    int n = snprintf(tmp, sizeof(tmp), "%d", value);
    RSCache_CS2_StrBufAppendN(buf, tmp, n);
}

void
RSCache_CS2_StrBufAppendFormat(struct RSCache_CS2_StrBuf* buf, const char* fmt, ...)
{
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if( n < 0 )
        return;
    if( n < (int)sizeof(tmp) )
    {
        RSCache_CS2_StrBufAppendN(buf, tmp, n);
        return;
    }

    char* big = (char*)malloc((size_t)n + 1);
    if( !big )
    {
        fprintf(stderr, "rscache cs2: string format out of memory\n");
        abort();
    }
    va_start(args, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, args);
    va_end(args);
    RSCache_CS2_StrBufAppendN(buf, big, n);
    free(big);
}

void
RSCache_CS2_StrBufClear(struct RSCache_CS2_StrBuf* buf)
{
    buf->length = 0;
}

const char*
RSCache_CS2_StrBufCStr(struct RSCache_CS2_StrBuf* buf)
{
    cs2_strbuf_reserve(buf, buf->length);
    buf->data[buf->length] = '\0';
    return buf->data;
}

void
RSCache_CS2_StrBufFree(struct RSCache_CS2_StrBuf* buf)
{
    if( !buf )
        return;
    free(buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}
