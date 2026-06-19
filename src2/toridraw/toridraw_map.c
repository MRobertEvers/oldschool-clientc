#include "toridraw_map.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum
{
    TORIDRAW_MAP_SLOT_EMPTY = 0,
    TORIDRAW_MAP_SLOT_FULL = 1,
    TORIDRAW_MAP_SLOT_TOMB = 2
};

typedef struct ToriDraw_MapSlotHeader
{
    uint64_t hash;
    uint8_t state;
    uint8_t _pad[7];
} ToriDraw_MapSlotHeader;

struct ToriDraw_Map
{
    unsigned char* entries;
    uint8_t* original_buffer;
    size_t stride;
    size_t entry_size;
    size_t entry_offset;
    size_t key_size;
    size_t capacity;
    size_t size;

    ToriDraw_MapHashFn hash_fn;
    ToriDraw_MapEqFn eq_fn;
    ToriDraw_MapIterableFn iterable_fn;
    void* arg;
};

static inline size_t
ToriDraw_MapAlignUpSize(
    size_t x,
    size_t align)
{
    if( align == 0 )
        return x;
    size_t rem = x % align;
    if( rem == 0 )
        return x;
    return x + (align - rem);
}

static inline unsigned char*
ToriDraw_MapAlignUpPtr(
    unsigned char* p,
    size_t align)
{
    uintptr_t ip = (uintptr_t)p;
    uintptr_t aligned = (ip + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
    return (unsigned char*)aligned;
}

static inline unsigned char*
ToriDraw_MapSlotBaseAt(
    const struct ToriDraw_Map* m,
    size_t idx)
{
    return m->entries + m->stride * idx;
}

static inline ToriDraw_MapSlotHeader*
ToriDraw_MapSlotHeaderAt(
    const struct ToriDraw_Map* m,
    size_t idx)
{
    return (ToriDraw_MapSlotHeader*)ToriDraw_MapSlotBaseAt(m, idx);
}

static inline void*
ToriDraw_MapSlotEntryPtr(
    const struct ToriDraw_Map* m,
    size_t idx)
{
    return (void*)(ToriDraw_MapSlotBaseAt(m, idx) + m->entry_offset);
}

static inline void*
ToriDraw_MapEntryKeyPtr(
    const struct ToriDraw_Map* m,
    void* entry)
{
    (void)m;
    return (unsigned char*)entry;
}

static uint64_t
ToriDraw_MapHashBytes(
    const void* key,
    size_t len,
    void* arg)
{
    (void)arg;
    const unsigned char* p = (const unsigned char*)key;
    uint64_t h = 1469598103934665603ULL;
    for( size_t i = 0; i < len; i++ )
    {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    if( h == 0 )
        h = 1;
    return h;
}

static int
ToriDraw_MapEqBytes(
    const void* a,
    const void* b,
    size_t len,
    void* arg)
{
    (void)arg;
    return memcmp(a, b, len) == 0;
}

int
ToriDraw_MapInit(
    struct ToriDraw_Map* m,
    void* buffer,
    size_t buffer_size,
    size_t entry_size,
    size_t key_size,
    size_t capacity,
    ToriDraw_MapHashFn hash_fn,
    ToriDraw_MapEqFn eq_fn,
    ToriDraw_MapIterableFn iterable_fn,
    void* arg)
{
    if( !m || !buffer || entry_size == 0 || key_size == 0 )
        return TORIDRAW_MAP_BADARG;

    if( hash_fn == NULL )
        hash_fn = ToriDraw_MapHashBytes;
    if( eq_fn == NULL )
        eq_fn = ToriDraw_MapEqBytes;

    if( key_size > entry_size )
        return TORIDRAW_MAP_BADARG;

    memset(m, 0, sizeof(*m));
    memset(buffer, 0, buffer_size);

    const size_t align = 16;

    unsigned char* base = (unsigned char*)buffer;
    unsigned char* aligned = ToriDraw_MapAlignUpPtr(base, align);

    if( aligned > base + buffer_size )
        return TORIDRAW_MAP_NOMEM;

    size_t remaining = (size_t)((base + buffer_size) - aligned);

    size_t header_size = sizeof(ToriDraw_MapSlotHeader);
    size_t entry_offset = ToriDraw_MapAlignUpSize(header_size, align);
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = ToriDraw_MapAlignUpSize(raw_stride, align);

    size_t actual_capacity = capacity;

    if( actual_capacity == 0 )
    {
        actual_capacity = remaining / stride;
        if( actual_capacity == 0 )
            return TORIDRAW_MAP_NOMEM;
    }

    size_t needed = actual_capacity * stride;
    if( needed > remaining )
        return TORIDRAW_MAP_NOMEM;

    m->entries = aligned;
    m->original_buffer = buffer;
    m->stride = stride;
    m->entry_size = entry_size;
    m->entry_offset = entry_offset;
    m->key_size = key_size;
    m->capacity = actual_capacity;
    m->size = 0;
    m->hash_fn = hash_fn;
    m->eq_fn = eq_fn;
    m->iterable_fn = iterable_fn;
    m->arg = arg;

    for( size_t i = 0; i < actual_capacity; i++ )
    {
        ToriDraw_MapSlotHeader* h = ToriDraw_MapSlotHeaderAt(m, i);
        h->state = TORIDRAW_MAP_SLOT_EMPTY;
        h->hash = 0;
    }

    return TORIDRAW_MAP_OK;
}

void*
ToriDraw_MapBufferPtr(struct ToriDraw_Map* m)
{
    return m->original_buffer;
}

struct ToriDraw_Map*
ToriDraw_MapNew(
    const struct ToriDraw_MapConfig* config,
    uint32_t flags)
{
    (void)flags;
    int status;
    struct ToriDraw_Map* m = malloc(sizeof(struct ToriDraw_Map));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct ToriDraw_Map));
    status = ToriDraw_MapInit(
        m,
        config->buffer,
        config->buffer_size,
        config->entry_size,
        config->key_size,
        config->capacity,
        config->hash_fn_nullable,
        config->eq_fn_nullable,
        config->iterable_fn_nullable,
        config->arg_nullable);

    if( status != TORIDRAW_MAP_OK )
    {
        free(m);
        return NULL;
    }
    return m;
}

void
ToriDraw_MapFree(struct ToriDraw_Map* m)
{
    free(m);
}

void*
ToriDraw_MapSearch(
    struct ToriDraw_Map* m,
    const void* key,
    enum ToriDraw_MapAction action)
{
    if( !m || !m->entries || !key )
        return NULL;

    if( m->capacity == 0 )
        return NULL;

    uint64_t hash = m->hash_fn(key, m->key_size, m->arg);
    if( hash == 0 )
        hash = 1;

    size_t idx = hash % m->capacity;
    size_t start = idx;
    int64_t tomb_i = -1;

    for( ;; )
    {
        ToriDraw_MapSlotHeader* h = ToriDraw_MapSlotHeaderAt(m, idx);

        if( h->state == TORIDRAW_MAP_SLOT_EMPTY )
        {
            if( action == TORIDRAW_MAP_FIND || action == TORIDRAW_MAP_REMOVE )
                return NULL;

            if( tomb_i >= 0 )
                idx = (size_t)tomb_i;

            ToriDraw_MapSlotHeader* hh = ToriDraw_MapSlotHeaderAt(m, idx);
            hh->state = TORIDRAW_MAP_SLOT_FULL;
            hh->hash = hash;

            void* entry = ToriDraw_MapSlotEntryPtr(m, idx);
            void* ekey = ToriDraw_MapEntryKeyPtr(m, entry);

            memcpy(ekey, key, m->key_size);
            m->size++;

            return entry;
        }
        else if( h->state == TORIDRAW_MAP_SLOT_TOMB )
        {
            if( tomb_i < 0 )
                tomb_i = (uint32_t)idx;
        }
        else if( h->state == TORIDRAW_MAP_SLOT_FULL && h->hash == hash )
        {
            void* entry = ToriDraw_MapSlotEntryPtr(m, idx);
            void* ekey = ToriDraw_MapEntryKeyPtr(m, entry);

            if( m->eq_fn(ekey, key, m->key_size, m->arg) )
            {
                if( action == TORIDRAW_MAP_REMOVE )
                {
                    h->state = TORIDRAW_MAP_SLOT_TOMB;
                    h->hash = 0;
                    if( m->size > 0 )
                        m->size--;
                    return entry;
                }

                return entry;
            }
        }

        idx++;
        if( idx == m->capacity )
            idx = 0;

        if( idx == start )
            break;
    }

    if( action == TORIDRAW_MAP_INSERT && tomb_i >= 0 )
    {
        size_t t = (size_t)tomb_i;
        ToriDraw_MapSlotHeader* hh = ToriDraw_MapSlotHeaderAt(m, t);
        hh->state = TORIDRAW_MAP_SLOT_FULL;
        hh->hash = hash;

        void* entry = ToriDraw_MapSlotEntryPtr(m, t);
        void* ekey = ToriDraw_MapEntryKeyPtr((void*)m, entry);

        memcpy(ekey, key, m->key_size);
        m->size++;

        return entry;
    }

    return NULL;
}

int
ToriDraw_MapResize(
    struct ToriDraw_Map* m,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer_out)
{
    if( !m || !new_buffer || !old_buffer_out || new_capacity == 0 )
        return TORIDRAW_MAP_BADARG;

    struct ToriDraw_Map old = *m;
    void* oldbuf = old.original_buffer;
    size_t oldcap = old.capacity;

    struct ToriDraw_Map newmap;
    int rc = ToriDraw_MapInit(
        &newmap,
        new_buffer,
        new_buffer_size,
        old.entry_size,
        old.key_size,
        new_capacity,
        old.hash_fn,
        old.eq_fn,
        old.iterable_fn,
        old.arg);

    if( rc != TORIDRAW_MAP_OK )
        return rc;

    for( size_t i = 0; i < oldcap; i++ )
    {
        ToriDraw_MapSlotHeader* h = (ToriDraw_MapSlotHeader*)(old.entries + old.stride * i);

        if( h->state == TORIDRAW_MAP_SLOT_FULL )
        {
            void* old_entry = (void*)(old.entries + old.entry_offset + old.stride * i);
            void* old_key = (unsigned char*)old_entry;

            void* dst = ToriDraw_MapSearch(&newmap, old_key, TORIDRAW_MAP_INSERT);
            if( !dst )
            {
                *m = old;
                return TORIDRAW_MAP_NOMEM;
            }

            memcpy(dst, old_entry, old.entry_size);
        }
    }

    *m = newmap;
    *old_buffer_out = oldbuf;

    return TORIDRAW_MAP_OK;
}

struct ToriDraw_MapIter
{
    size_t idx;
    struct ToriDraw_Map* m;
};

struct ToriDraw_Map*
ToriDraw_MapIterGetMap(struct ToriDraw_MapIter* it)
{
    return it->m;
}

struct ToriDraw_MapIter*
ToriDraw_MapIterNew(struct ToriDraw_Map* m)
{
    struct ToriDraw_MapIter* it = malloc(sizeof(struct ToriDraw_MapIter));
    if( !it )
        return NULL;
    it->idx = 0;
    it->m = m;
    return it;
}

void
ToriDraw_MapIterFree(struct ToriDraw_MapIter* it)
{
    free(it);
}

void*
ToriDraw_MapIterNext(struct ToriDraw_MapIter* it)
{
    if( !it || !it->m || !it->m->entries )
        return NULL;

    while( it->idx < it->m->capacity )
    {
        size_t cur = it->idx;
        it->idx++;
        ToriDraw_MapSlotHeader* h = ToriDraw_MapSlotHeaderAt(it->m, cur);
        if( h->state == TORIDRAW_MAP_SLOT_FULL )
        {
            void* entry = ToriDraw_MapSlotEntryPtr(it->m, cur);
            if( !it->m->iterable_fn || it->m->iterable_fn(entry, it->m->arg) != 0 )
                return entry;
        }
    }
    return NULL;
}

uint32_t
ToriDraw_MapCount(struct ToriDraw_Map* h)
{
    return (uint32_t)h->size;
}

uint32_t
ToriDraw_MapCapacity(struct ToriDraw_Map* h)
{
    return (uint32_t)h->capacity;
}

size_t
ToriDraw_MapEntrySize(struct ToriDraw_Map* h)
{
    return h->entry_size;
}

size_t
ToriDraw_MapBufferSizeFor(
    size_t entry_size,
    size_t count)
{
    const size_t align = 16;
    size_t header_size = sizeof(ToriDraw_MapSlotHeader);
    size_t entry_offset = ToriDraw_MapAlignUpSize(header_size, align);
    size_t stride = ToriDraw_MapAlignUpSize(entry_offset + entry_size, align);
    return count * stride + (align - 1);
}
