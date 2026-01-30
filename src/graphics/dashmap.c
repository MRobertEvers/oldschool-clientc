#include "dashmap.h"
/*
 * dashmap.c
 *
 * Single-buffer, open-addressing hash map with linear probing,
 * using a Postgres-style unified API:
 *
 *      void *hmap_search(HMap *m, const void *key, HMapAction action);
 *
 * The returned pointer is to the *entire entry struct* (key + value),
 * just like Postgres's hash_search returns a pointer to the table entry.
 *
 * Layout assumptions (like Postgres):
 *  - You define a struct for your entries, e.g.:
 *
 *      typedef struct MyEntry {
 *          MyKey   key;
 *          MyValue val;
 *      } MyEntry;
 *
 *  - You tell the hash map:
 *      - entry_size   = sizeof(MyEntry)
 *      - key_size     = sizeof(MyKey)
 *      - key_offset   = offsetof(MyEntry, key)
 *
 *  - hmap_search() returns a pointer you can cast to (MyEntry *):
 *
 *      MyKey key = ...;
 *      MyEntry *e = hmap_search(&map, &key, HMAP_ENTER);
 *      if (e) {
 *          e->val = some_value;
 *      }
 *
 * The map:
 *  - Uses a single caller-provided buffer for all internal storage.
 *  - Does not allocate or free memory.
 *  - Uses linear probing with tombstones.
 *  - Ensures entries are aligned at least to alignof(max_align_t),
 *    so any normal struct can be safely stored and dereferenced.
 */

#include <stddef.h> /* size_t, offsetof, max_align_t */
#include <stdint.h> /* uint64_t, uintptr_t */
#include <string.h> /* memcpy, memcmp */

enum
{
    DASHMAP_SLOT_EMPTY = 0,
    DASHMAP_SLOT_FULL = 1,
    DASHMAP_SLOT_TOMB = 2
};

/*
 * Each slot in the backing buffer is:
 *
 *   [HMapSlotHeader] [padding to align entry] [entry bytes...]
 *
 * The stride between slots is padded so that:
 *   - Each slot base address is aligned to alignof(max_align_t), and
 *   - The entry region start is also aligned to alignof(max_align_t),
 * so any user-defined entry struct is safely aligned.
 */
typedef struct HMapSlotHeader
{
    uint64_t hash;   /* 0 means “unused”; we never generate 0 hashes */
    uint8_t state;   /* HMAP_SLOT_* */
    uint8_t _pad[7]; /* pad to 16 bytes on typical LP64; safe elsewhere */
} HMapSlotHeader;

struct DashMap
{
    unsigned char* entries;   /* aligned base pointer to first slot */
    uint8_t* original_buffer; /* pointer to key data */
    size_t stride;            /* bytes between slots */
    size_t entry_size;        /* sizeof(user_entry) */
    size_t entry_offset;      /* offset from slot base to entry bytes */
    size_t key_size;          /* bytes of key */
    size_t capacity;          /* number of slots */
    size_t size;              /* number of FULL slots */

    dashmap_hash_fn hash_fn;
    dashmap_eq_fn eq_fn;
    dashmap_iterable_fn iterable_fn;
    void* arg; /* user data passed to hash/eq */
};

/*-----------------------------------------------------------
 * Helper: alignment / layout
 *----------------------------------------------------------*/

static inline size_t
hmap_align_up_size(
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
hmap_align_up_ptr(
    unsigned char* p,
    size_t align)
{
    uintptr_t ip = (uintptr_t)p;
    uintptr_t aligned = (ip + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
    return (unsigned char*)aligned;
}

static inline unsigned char*
hmap_slot_base_at(
    const struct DashMap* m,
    size_t idx)
{
    return m->entries + m->stride * idx;
}

static inline HMapSlotHeader*
hmap_slot_header_at(
    const struct DashMap* m,
    size_t idx)
{
    return (HMapSlotHeader*)hmap_slot_base_at(m, idx);
}

static inline void*
hmap_slot_entry_ptr(
    const struct DashMap* m,
    size_t idx)
{
    return (void*)(hmap_slot_base_at(m, idx) + m->entry_offset);
}

/* key pointer inside an entry */
static inline void*
hmap_entry_key_ptr(
    const struct HMap* m,
    void* entry)
{
    return (unsigned char*)entry;
}

/*-----------------------------------------------------------
 * Simple default hash / eq for arbitrary byte keys (FNV-1a)
 *----------------------------------------------------------*/

static uint64_t
dashmap_hash_bytes(
    const void* key,
    size_t len,
    void* arg)
{
    (void)arg;
    const unsigned char* p = (const unsigned char*)key;
    uint64_t h = 1469598103934665603ULL; /* FNV offset basis */
    for( size_t i = 0; i < len; i++ )
    {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL; /* FNV prime */
    }
    if( h == 0 )
        h = 1; /* reserve 0 as sentinel */
    return h;
}

static int
dashmap_eq_bytes(
    const void* a,
    const void* b,
    size_t len,
    void* arg)
{
    (void)arg;
    return memcmp(a, b, len) == 0;
}

/*
 * Initialize an HMap in-place, using a single caller-provided buffer.
 *
 *  m           : pointer to HMap struct to initialize
 *  buffer      : backing storage for all slots
 *  buffer_size : size of backing storage in bytes
 *  entry_size  : sizeof(user_entry)
 *  key_size    : number of bytes in the key (portion inside entry)
 *  key_offset  : offset inside the entry where the key resides (like offsetof)
 *  capacity    : number of slots (must be > 0)
 *  hash_fn     : user hash function (must not be NULL)
 *  eq_fn       : user equality function (must not be NULL)
 *  arg         : user data passed to hash_fn / eq_fn
 *
 * Returns:
 *   HMAP_OK, HMAP_NOMEM, HMAP_BADARG
 */
int
dashmap_init(
    struct DashMap* m,
    void* buffer,
    size_t buffer_size,
    size_t entry_size,
    size_t key_size,
    size_t capacity,
    dashmap_hash_fn hash_fn,
    dashmap_eq_fn eq_fn,
    dashmap_iterable_fn iterable_fn,
    void* arg)
{
    if( !m || !buffer || entry_size == 0 || key_size == 0 )
    {
        return DASHMAP_BADARG;
    }

    if( hash_fn == NULL )
        hash_fn = dashmap_hash_bytes;
    if( eq_fn == NULL )
        eq_fn = dashmap_eq_bytes;

    if( key_size > entry_size )
        return DASHMAP_BADARG;

    memset(m, 0, sizeof(*m));
    memset(buffer, 0, buffer_size);

    const size_t align = 16;

    unsigned char* base = (unsigned char*)buffer;
    unsigned char* aligned = hmap_align_up_ptr(base, align);

    if( aligned > base + buffer_size )
        return DASHMAP_NOMEM;

    size_t remaining = (size_t)((base + buffer_size) - aligned);

    size_t header_size = sizeof(HMapSlotHeader);
    size_t entry_offset = hmap_align_up_size(header_size, align);
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = hmap_align_up_size(raw_stride, align);

    /* NEW LOGIC: auto capacity */
    size_t actual_capacity = capacity;

    if( actual_capacity == 0 )
    {
        actual_capacity = remaining / stride;
        if( actual_capacity == 0 )
            return DASHMAP_NOMEM;
    }

    size_t needed = actual_capacity * stride;
    if( needed > remaining )
        return DASHMAP_NOMEM;

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

    /* Initialize headers */
    for( size_t i = 0; i < actual_capacity; i++ )
    {
        HMapSlotHeader* h = hmap_slot_header_at(m, i);
        h->state = DASHMAP_SLOT_EMPTY;
        h->hash = 0;
    }

    return DASHMAP_OK;
}

void*
dashmap_buffer_ptr(struct DashMap* m)
{
    return m->entries;
}

struct DashMap*
dashmap_new(
    const struct DashMapConfig* config,
    uint32_t flags)
{
    int status;
    struct DashMap* m = malloc(sizeof(struct DashMap));
    if( !m )
        return NULL;
    memset(m, 0, sizeof(struct DashMap));
    status = dashmap_init(
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

    if( status != DASHMAP_OK )
    {
        free(m);
        return NULL;
    }
    return m;
}

void
dashmap_free(struct DashMap* m)
{
    free(m);
}

/*
 * Core unified operation (Postgres-like):
 *
 *  void *hmap_search(HMap *m, const void *key, HMapAction action);
 *
 *  key    : pointer to key bytes (not to the whole entry)
 *  action : HMAP_FIND / HMAP_ENTER / HMAP_REMOVE
 *
 * Returns:
 *  - pointer to entry (user struct) on success
 *  - NULL on:
 *      - HMAP_FIND: key not found
 *      - HMAP_ENTER: table full and no slot available
 *      - HMAP_REMOVE: key not found
 *
 * Notes:
 *  - For HMAP_ENTER, if the key already exists, you just get the existing
 *    entry pointer (no new insert).
 *  - For HMAP_ENTER, if the key does not exist and there is a free slot,
 *    a new entry is created; the key bytes are copied into the entry's key
 *    field (key_offset), and the rest of the entry is left uninitialized;
 *    the pointer returned is to the full entry (you fill in the value).
 *  - For HMAP_REMOVE, if the key exists, the slot is marked as a tombstone,
 *    but the returned pointer points to the entry bytes that were there.
 *    As with Postgres, this pointer is only valid until the next
 *    insertion/clear/destroy affecting that slot.
 */
void*
dashmap_search(
    struct DashMap* m,
    const void* key,
    enum DashMapAction action)
{
    if( !m || !m->entries || !key )
        return NULL;

    if( m->capacity == 0 )
        return NULL;

    uint64_t hash = m->hash_fn(key, m->key_size, m->arg);
    if( hash == 0 )
        hash = 1; /* reserve hash=0 as sentinel */

    size_t idx = hash % m->capacity;
    size_t start = idx;
    int64_t tomb_i = -1;

    for( ;; )
    {
        HMapSlotHeader* h = hmap_slot_header_at(m, idx);

        if( h->state == DASHMAP_SLOT_EMPTY )
        {
            /* ---------- NOT FOUND ---------- */

            if( action == DASHMAP_FIND || action == DASHMAP_REMOVE )
            {
                return NULL;
            }

            /* DASHMAP_ENTER: insert into first tombstone or this empty slot */
            if( tomb_i >= 0 )
                idx = (size_t)tomb_i;

            HMapSlotHeader* hh = hmap_slot_header_at(m, idx);
            hh->state = DASHMAP_SLOT_FULL;
            hh->hash = hash;

            void* entry = hmap_slot_entry_ptr(m, idx);
            void* ekey = hmap_entry_key_ptr(m, entry);

            memcpy(ekey, key, m->key_size);
            m->size++;

            return entry;
        }
        else if( h->state == DASHMAP_SLOT_TOMB )
        {
            /* remember the first tombstone for possible reuse on ENTER */
            if( tomb_i < 0 )
                tomb_i = (ssize_t)idx;
        }
        else if( h->state == DASHMAP_SLOT_FULL && h->hash == hash )
        {
            /* Possible match */
            void* entry = hmap_slot_entry_ptr(m, idx);
            void* ekey = hmap_entry_key_ptr(m, entry);

            if( m->eq_fn(ekey, key, m->key_size, m->arg) )
            {
                if( action == DASHMAP_REMOVE )
                {
                    h->state = DASHMAP_SLOT_TOMB;
                    h->hash = 0;
                    if( m->size > 0 )
                        m->size--;
                    return entry; /* return old entry */
                }

                /* FIND or ENTER (existing) */
                return entry;
            }
        }

        /* move to next slot (linear probing with wrap) */
        idx++;
        if( idx == m->capacity )
            idx = 0;

        if( idx == start )
            break; /* full cycle */
    }

    /* Full cycle: either table is full or key not found.
     *
     * For FIND/REMOVE, we've already exhausted possibilities -> NULL.
     * For ENTER, we may still be able to reuse a tombstone.
     */
    if( action == DASHMAP_INSERT && tomb_i >= 0 )
    {
        size_t t = (size_t)tomb_i;
        HMapSlotHeader* hh = hmap_slot_header_at(m, t);
        hh->state = DASHMAP_SLOT_FULL;
        hh->hash = hash;

        void* entry = hmap_slot_entry_ptr(m, t);
        void* ekey = hmap_entry_key_ptr((void*)m, entry);

        memcpy(ekey, key, m->key_size);
        m->size++;

        return entry;
    }

    return NULL;
}

/*
 * Rebuild the hash map into a new buffer with a new capacity.
 *
 * Parameters:
 *    m              – the hash map to rebuild
 *    new_buffer     – memory for the new resized table
 *    new_buffer_size
 *    new_capacity   – number of slots in the new table
 *    old_buffer_out – receives the pointer to the old backing buffer
 *
 * Notes:
 *    - All entries are rehashed and reinserted.
 *    - The map metadata (stride, entry_offset, etc.) is recomputed.
 *    - On failure, *m and *old_buffer_out remain unchanged.
 *
 * Returns:
 *    HMAP_OK
 *    HMAP_NOMEM
 *    HMAP_BADARG
 */
int
dashmap_resize(
    struct DashMap* m,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer_out)
{
    if( !m || !new_buffer || !old_buffer_out || new_capacity == 0 )
        return DASHMAP_BADARG;

    /* Save old map metadata */
    struct DashMap old = *m;
    void* oldbuf = old.original_buffer; /* old aligned start pointer */
    size_t oldcap = old.capacity;

    /* Temporary map for new table */
    struct DashMap newmap;
    int rc = dashmap_init(
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

    if( rc != DASHMAP_OK )
        return rc;

    /* Reinsert all full entries */
    for( size_t i = 0; i < oldcap; i++ )
    {
        HMapSlotHeader* h = (HMapSlotHeader*)(old.entries + old.stride * i);

        if( h->state == DASHMAP_SLOT_FULL )
        {
            void* old_entry = (void*)(old.entries + old.entry_offset + old.stride * i);
            void* old_key = (unsigned char*)old_entry;

            /* Insert into new table */
            void* dst = dashmap_search(&newmap, old_key, DASHMAP_INSERT);
            if( !dst )
            {
                /* Failure: restore original map */
                *m = old;
                return DASHMAP_NOMEM;
            }

            /* Copy entire entry struct */
            memcpy(dst, old_entry, old.entry_size);
        }
    }

    /* Success — publish new map */
    *m = newmap;

    /* Return old buffer to caller */
    *old_buffer_out = oldbuf;

    return DASHMAP_OK;
}

struct DashMapIter
{
    size_t idx;
    struct DashMap* m;
};

struct DashMap*
dashmap_iter_get_map(struct DashMapIter* it)
{
    return it->m;
}

struct DashMapIter*
dashmap_iter_new(struct DashMap* m)
{
    struct DashMapIter* it = malloc(sizeof(struct DashMapIter));
    if( !it )
        return NULL;
    it->idx = 0;
    it->m = m;
    return it;
}

void
dashmap_iter_free(struct DashMapIter* it)
{
    free(it);
}

/*
 * Advance iterator.
 * Returns:
 *   - pointer to next FULL entry
 *   - NULL when iteration is finished
 */
void*
dashmap_iter_next(struct DashMapIter* it)
{
    if( !it || !it->m || !it->m->entries )
        return NULL;
    if( it->idx >= it->m->capacity )
        return NULL;

    it->idx++;
    while( it->idx < it->m->capacity )
    {
        HMapSlotHeader* h = hmap_slot_header_at(it->m, it->idx);
        if( h->state == DASHMAP_SLOT_FULL )
        {
            void* entry = hmap_slot_entry_ptr(it->m, it->idx);
            if( !it->m->iterable_fn || it->m->iterable_fn(entry, it->m->arg) != 0 )
                return entry;
        }
        it->idx++;
    }
    return NULL;
}
