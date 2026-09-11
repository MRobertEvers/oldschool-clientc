#ifndef HMAP_H
#define HMAP_H

/*
 * hmap.h — single-buffer, open-addressing hash map (STB-style single header)
 *
 * Usage:
 *   #include "hmap.h"                    // declarations only
 *
 *   // In exactly one .c file:
 *   #define HMAP_IMPLEMENTATION
 *   #include "hmap.h"
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HMAP_OK 0
#define HMAP_NOMEM 1
#define HMAP_BADARG 2

/*
 * Hash callback:
 *   - key points to the key field inside the entry
 *   - key_size is the size of the key bytes
 *   - arg is user-supplied context
 *
 * Must never return 0; if so, the table will map it to 1.
 */
typedef uint64_t (*hmap_hash_fn)(
    const void* key,
    size_t key_size,
    void* arg);

/*
 * Equality callback:
 *   - return nonzero if equal
 */
typedef int (*hmap_eq_fn)(
    const void* a,
    const void* b,
    size_t key_size,
    void* arg);

enum HMapAction
{
    HMAP_FIND,
    HMAP_INSERT,
    HMAP_REMOVE
};

struct HashConfig
{
    void* buffer;
    size_t buffer_size;

    size_t key_size;
    size_t entry_size;

    size_t capacity;
    hmap_hash_fn hash_fn_nullable;
    hmap_eq_fn eq_fn_nullable;
    void* arg_nullable;
};

struct HMap
{
    unsigned char* entries;
    uint8_t* original_buffer;
    size_t stride;
    size_t entry_size;
    size_t entry_offset;
    size_t key_size;
    size_t capacity;
    size_t size;

    hmap_hash_fn hash_fn;
    hmap_eq_fn eq_fn;
    void* arg;
};

struct HMapIter
{
    size_t idx;
    struct HMap* m;
};

/*
 * THE HASH IS A PLATFORM CHOICE, AND SO IS THE SLOT INDEX.
 *
 * The original default was FNV-1a over 64 bits, one byte at a time, and the
 * slot was `hash % capacity`. On an ARMv7 core that is three 32-bit multiplies
 * per key BYTE and a software 64-bit divide (__aeabi_uldivmod, ~100 cycles)
 * per lookup: the Moto X profile put the divide alone at 0.5% of a frame
 * with the byte loop on top. The default now depends on the target word:
 *
 *   HMAP_HASH_FNV64     the original. Kept for 64-bit hosts so their slot
 *                       placement and iteration order do not move.
 *   HMAP_HASH_MURMUR32  MurmurHash3 x86_32: a word at a time, 32-bit
 *                       arithmetic only, finished with fmix32. Selected on
 *                       32-bit targets.
 *
 * The slot index goes with it: FNV64 keeps `%`; MURMUR32 uses Fibonacci
 * hashing -- multiply the low word by 2^32/phi and take the high product
 * bits against the capacity (Lemire's fastrange) -- one mul and one umull,
 * no divide, and the golden-ratio multiply spreads a poorly mixed
 * caller-supplied hash (an identity on a small int) across the table.
 *
 * -DHMAP_HASH_DEFAULT=HMAP_HASH_MURMUR32 (or _FNV64) overrides the choice.
 * A map's own hash_fn_nullable still wins over the default.
 */
#define HMAP_HASH_FNV64 1
#define HMAP_HASH_MURMUR32 2
#if !defined(HMAP_HASH_DEFAULT)
#if defined(__LP64__) || defined(_WIN64) || (defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8)
#define HMAP_HASH_DEFAULT HMAP_HASH_FNV64
#else
#define HMAP_HASH_DEFAULT HMAP_HASH_MURMUR32
#endif
#endif

/* The default hash for this build: see HMAP_HASH_DEFAULT. */
uint64_t
hmap_hash_bytes(
    const void* key,
    size_t len,
    void* arg);

/* Both hashes, by name, for a map that wants one regardless of platform. */
uint64_t
hmap_hash_fnv64(
    const void* key,
    size_t len,
    void* arg);

uint64_t
hmap_hash_murmur32(
    const void* key,
    size_t len,
    void* arg);

int
hmap_eq_bytes(
    const void* a,
    const void* b,
    size_t len,
    void* arg);

int
hmap_init(
    struct HMap* m,
    void* buffer,
    size_t buffer_size,
    size_t entry_size,
    size_t key_size,
    size_t capacity,
    hmap_hash_fn hash_fn,
    hmap_eq_fn eq_fn,
    void* arg);

void*
hmap_buffer_ptr(struct HMap* h);

struct HMap*
hmap_new(
    const struct HashConfig* config,
    uint32_t flags);

void*
hmap_free(struct HMap* h);

void*
hmap_search(
    struct HMap* h,
    const void* key,
    enum HMapAction action);

int
hmap_resize(
    struct HMap* h,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer);

struct HMapIter*
hmap_iter_new(struct HMap* h);

void
hmap_iter_free(struct HMapIter* it);

void*
hmap_iter_next(struct HMapIter* it);

#endif /* HMAP_H */

#ifdef HMAP_IMPLEMENTATION
#ifndef HMAP_IMPLEMENTATION_ONCE
#define HMAP_IMPLEMENTATION_ONCE

#include <stdint.h>
#include <string.h>

enum
{
    HMAP_SLOT_EMPTY = 0,
    HMAP_SLOT_FULL = 1,
    HMAP_SLOT_TOMB = 2
};

typedef struct HMapSlotHeader
{
    uint64_t hash;
    uint8_t state;
    uint8_t _pad[7];
} HMapSlotHeader;

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
    const struct HMap* m,
    size_t idx)
{
    return m->entries + m->stride * idx;
}

static inline HMapSlotHeader*
hmap_slot_header_at(
    const struct HMap* m,
    size_t idx)
{
    return (HMapSlotHeader*)hmap_slot_base_at(m, idx);
}

static inline void*
hmap_slot_entry_ptr(
    const struct HMap* m,
    size_t idx)
{
    return (void*)(hmap_slot_base_at(m, idx) + m->entry_offset);
}

static inline void*
hmap_entry_key_ptr(
    const struct HMap* m,
    void* entry)
{
    (void)m;
    return (unsigned char*)entry;
}

uint64_t
hmap_hash_fnv64(
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

static inline uint32_t
hmap_rotl32(uint32_t x, int r)
{
    return (x << r) | (x >> (32 - r));
}

uint64_t
hmap_hash_murmur32(
    const void* key,
    size_t len,
    void* arg)
{
    /* MurmurHash3_x86_32 (public domain, Austin Appleby), seed 0. Whole
     * words through memcpy -- an aligned ldr where the target allows it and
     * never a misaligned access where it does not. */
    const unsigned char* p = (const unsigned char*)key;
    uint32_t const c1 = 0xcc9e2d51u;
    uint32_t const c2 = 0x1b873593u;
    uint32_t h = 0;
    size_t i;
    (void)arg;
    for( i = 0; i + 4 <= len; i += 4 )
    {
        uint32_t k;
        memcpy(&k, p + i, 4);
        k *= c1;
        k = hmap_rotl32(k, 15);
        k *= c2;
        h ^= k;
        h = hmap_rotl32(h, 13);
        h = h * 5u + 0xe6546b64u;
    }
    {
        uint32_t k = 0;
        switch( len & 3 )
        {
        case 3:
            k ^= (uint32_t)p[i + 2] << 16;
            /* fallthrough */
        case 2:
            k ^= (uint32_t)p[i + 1] << 8;
            /* fallthrough */
        case 1:
            k ^= (uint32_t)p[i];
            k *= c1;
            k = hmap_rotl32(k, 15);
            k *= c2;
            h ^= k;
            break;
        default:
            break;
        }
    }
    h ^= (uint32_t)len;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    if( h == 0 )
        h = 1;
    return (uint64_t)h;
}

uint64_t
hmap_hash_bytes(
    const void* key,
    size_t len,
    void* arg)
{
#if HMAP_HASH_DEFAULT == HMAP_HASH_FNV64
    return hmap_hash_fnv64(key, len, arg);
#else
    return hmap_hash_murmur32(key, len, arg);
#endif
}

/* The slot a hash starts probing at; see HMAP_HASH_DEFAULT for why the
 * reduction is tied to the hash choice. */
static inline size_t
hmap_slot_index(
    uint64_t hash,
    size_t capacity)
{
#if HMAP_HASH_DEFAULT == HMAP_HASH_FNV64
    return (size_t)(hash % capacity);
#else
    uint32_t x = (uint32_t)hash ^ (uint32_t)(hash >> 32);
    x *= 0x9E3779B1u;
    return (size_t)(((uint64_t)x * (uint64_t)capacity) >> 32);
#endif
}

int
hmap_eq_bytes(
    const void* a,
    const void* b,
    size_t len,
    void* arg)
{
    (void)arg;
    return memcmp(a, b, len) == 0;
}

int
hmap_init(
    struct HMap* m,
    void* buffer,
    size_t buffer_size,
    size_t entry_size,
    size_t key_size,
    size_t capacity,
    hmap_hash_fn hash_fn,
    hmap_eq_fn eq_fn,
    void* arg)
{
    assert(m);
    assert(buffer);
    assert(entry_size > 0);
    assert(key_size > 0);

    if( hash_fn == NULL )
        hash_fn = hmap_hash_bytes;
    if( eq_fn == NULL )
        eq_fn = hmap_eq_bytes;

    if( key_size > entry_size )
        return HMAP_BADARG;

    memset(m, 0, sizeof(*m));

    const size_t align = 16;

    unsigned char* base = (unsigned char*)buffer;
    unsigned char* aligned = hmap_align_up_ptr(base, align);

    if( aligned > base + buffer_size )
        return HMAP_NOMEM;

    size_t remaining = (size_t)((base + buffer_size) - aligned);

    size_t header_size = sizeof(HMapSlotHeader);
    size_t entry_offset = hmap_align_up_size(header_size, align);
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = hmap_align_up_size(raw_stride, align);

    size_t actual_capacity = capacity;

    if( actual_capacity == 0 )
    {
        actual_capacity = remaining / stride;
        if( actual_capacity == 0 )
            return HMAP_NOMEM;
    }

    size_t needed = actual_capacity * stride;
    if( needed > remaining )
        return HMAP_NOMEM;

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
    m->arg = arg;

    for( size_t i = 0; i < actual_capacity; i++ )
    {
        HMapSlotHeader* h = hmap_slot_header_at(m, i);
        h->state = HMAP_SLOT_EMPTY;
        h->hash = 0;
    }

    return HMAP_OK;
}

void*
hmap_buffer_ptr(struct HMap* h)
{
    /*
     * The pointer the CALLER gave us, not the aligned one we hand to the slots.
     *
     * hmap_init aligns the buffer up to 16 before storing it as `entries`, so
     * `entries` is an interior pointer whenever malloc returned something that
     * was not already 16-aligned. Every caller of hmap_free does
     * `free(hmap_free(map))` — around ninety sites in src/ — so returning
     * `entries` means freeing an interior pointer and corrupting the heap.
     *
     * It never showed up on the 64-bit hosts because glibc and macOS malloc are
     * 16-byte aligned already, which makes `entries == original_buffer` and the
     * two spellings indistinguishable. 32-bit MinGW (msvcrt) only guarantees 8,
     * so roughly half of these allocations land at 8 mod 16 and the free is
     * wrong — which is how the Windows XP lane found it, as a SIGSEGV inside
     * RtlFreeHeap during App_Shutdown.
     *
     * hmap_resize already treats `original_buffer` as the free-me pointer
     * (`old.original_buffer`); this makes the two agree.
     */
    return h->original_buffer;
}

struct HMap*
hmap_new(
    const struct HashConfig* config,
    uint32_t flags)
{
    (void)flags;
    int status;
    struct HMap* m = malloc(sizeof(struct HMap));
    assert(m);
    memset(m, 0, sizeof(struct HMap));
    status = hmap_init(
        m,
        config->buffer,
        config->buffer_size,
        config->entry_size,
        config->key_size,
        config->capacity,
        config->hash_fn_nullable,
        config->eq_fn_nullable,
        config->arg_nullable);

    if( status != HMAP_OK )
    {
        free(m);
        return NULL;
    }
    return m;
}

void*
hmap_free(struct HMap* m)
{
    void* buffer = hmap_buffer_ptr(m);
    free(m);
    return buffer;
}

void*
hmap_search(
    struct HMap* m,
    const void* key,
    enum HMapAction action)
{
    assert(m);
    assert(m->entries);
    assert(key);

    uint64_t hash = m->hash_fn(key, m->key_size, m->arg);
    if( hash == 0 )
        hash = 1;

    size_t idx = hmap_slot_index(hash, m->capacity);
    size_t start = idx;
    ptrdiff_t tomb_i = -1;

    for( ;; )
    {
        HMapSlotHeader* h = hmap_slot_header_at(m, idx);

        if( h->state == HMAP_SLOT_EMPTY )
        {
            if( action == HMAP_FIND || action == HMAP_REMOVE )
                return NULL;

            if( tomb_i >= 0 )
                idx = (size_t)tomb_i;

            HMapSlotHeader* hh = hmap_slot_header_at(m, idx);
            hh->state = HMAP_SLOT_FULL;
            hh->hash = hash;

            void* entry = hmap_slot_entry_ptr(m, idx);
            void* ekey = hmap_entry_key_ptr(m, entry);

            /* Fresh entries must read as empty: callers test value fields
             * (e.g. `if (entry->object)`) to decide whether to free. */
            memset(entry, 0, m->entry_size);
            memcpy(ekey, key, m->key_size);
            m->size++;

            return entry;
        }
        else if( h->state == HMAP_SLOT_TOMB )
        {
            if( tomb_i < 0 )
                tomb_i = (ptrdiff_t)idx;
        }
        else if( h->state == HMAP_SLOT_FULL && h->hash == hash )
        {
            void* entry = hmap_slot_entry_ptr(m, idx);
            void* ekey = hmap_entry_key_ptr(m, entry);

            if( m->eq_fn(ekey, key, m->key_size, m->arg) )
            {
                if( action == HMAP_REMOVE )
                {
                    h->state = HMAP_SLOT_TOMB;
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

    if( action == HMAP_INSERT && tomb_i >= 0 )
    {
        size_t t = (size_t)tomb_i;
        HMapSlotHeader* hh = hmap_slot_header_at(m, t);
        hh->state = HMAP_SLOT_FULL;
        hh->hash = hash;

        void* entry = hmap_slot_entry_ptr(m, t);
        void* ekey = hmap_entry_key_ptr(m, entry);

        memset(entry, 0, m->entry_size);
        memcpy(ekey, key, m->key_size);
        m->size++;

        return entry;
    }

    return NULL;
}

int
hmap_resize(
    struct HMap* m,
    void* new_buffer,
    size_t new_buffer_size,
    size_t new_capacity,
    void** old_buffer_out)
{
    assert(m);
    assert(new_buffer);
    assert(old_buffer_out);
    assert(new_capacity > 0);

    struct HMap old = *m;
    void* oldbuf = old.original_buffer;
    size_t oldcap = old.capacity;

    struct HMap newmap;
    int rc = hmap_init(
        &newmap,
        new_buffer,
        new_buffer_size,
        old.entry_size,
        old.key_size,
        new_capacity,
        old.hash_fn,
        old.eq_fn,
        old.arg);

    if( rc != HMAP_OK )
        return rc;

    for( size_t i = 0; i < oldcap; i++ )
    {
        HMapSlotHeader* h = (HMapSlotHeader*)(old.entries + old.stride * i);

        if( h->state == HMAP_SLOT_FULL )
        {
            void* old_entry = (void*)(old.entries + old.entry_offset + old.stride * i);
            void* old_key = (unsigned char*)old_entry;

            void* dst = hmap_search(&newmap, old_key, HMAP_INSERT);
            if( !dst )
            {
                *m = old;
                return HMAP_NOMEM;
            }

            memcpy(dst, old_entry, old.entry_size);
        }
    }

    *m = newmap;
    *old_buffer_out = oldbuf;

    return HMAP_OK;
}

struct HMapIter*
hmap_iter_new(struct HMap* m)
{
    struct HMapIter* it = malloc(sizeof(struct HMapIter));
    assert(it);
    memset(it, 0, sizeof(struct HMapIter));
    it->idx = 0;
    it->m = m;
    return it;
}

void
hmap_iter_free(struct HMapIter* it)
{
    free(it);
}

void*
hmap_iter_next(struct HMapIter* it)
{
    assert(it && it->m && it->m->entries);
    assert(it->idx < it->m->capacity);
    it->idx++;
    while( it->idx < it->m->capacity )
    {
        HMapSlotHeader* h = hmap_slot_header_at(it->m, it->idx);
        if( h->state == HMAP_SLOT_FULL )
            return hmap_slot_entry_ptr(it->m, it->idx);
        it->idx++;
    }
    return NULL;
}

#endif /* HMAP_IMPLEMENTATION_ONCE */
#endif /* HMAP_IMPLEMENTATION */
