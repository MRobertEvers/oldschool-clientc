#include "ht.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Maximum probe length is set so that in a degenerate case of a
 * full table, looking up an non-present key will not cause O(n) behavior.
 *
 */
#define MAXIMUM_PROBE_LENGTH 8

static int
memncmp(const void* a, int lena, const void* b, int lenb)
{
    if( lena != lenb )
        return lena > lenb ? 1 : -1;
    int len = lena < lenb ? lena : lenb;
    return memcmp(a, b, len);
}

struct DefaultString
{
    int len;
    char const* data;
};

enum ScanEntryKind
{
    SEEmpty = -128,   // 0b10000000,
    SETombstone = -2, // 0b11111110,
    SEPopulated = 1
};

struct EntryHeader
{
    uint32_t hash;
    enum ScanEntryKind kind;
};

static uint32_t
fnv_1a(const char* key, int length)
{
    uint32_t hash = 2166136261u;
    for( int i = 0; i < length; i++ )
    {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static inline uint32_t
murmur_32_scramble(uint32_t k)
{
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}

static uint32_t
murmur3_32(const uint8_t* key, size_t len, uint32_t seed)
{
    uint32_t h = seed;
    uint32_t k;

    /* Read in groups of 4. */
    for( size_t i = len >> 2; i; i-- )
    {
        // Here is a source of differing results across endiannesses.
        // A swap here has no effects on hash properties though.
        memcpy(&k, key, sizeof(uint32_t));
        key += sizeof(uint32_t);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }

    /* Read the rest. */
    k = 0;
    for( size_t i = len & 3; i; i-- )
    {
        k <<= 8;
        k |= key[i - 1];
    }
    // A swap is *not* necessary here because the preceding loop already
    // places the low bytes in the low places according to whatever endianness
    // we use. Swaps only apply when the memory is copied in a chunk.
    h ^= murmur_32_scramble(k);
    /* Finalize. */
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

static uint32_t
_murmur3_32_hashof(struct Hash* hash)
{
    return *(uint32_t*)hash->hash;
}

static void
_murmur3_32_sethash(struct Hash* hash, uint32_t seed)
{
    *(uint32_t*)hash->hash = seed;
}

static void
murmur3_32_init(struct Hash* hash, uint32_t seed)
{
    memset(hash, 0x00, sizeof(struct Hash));

    _murmur3_32_sethash(hash, seed);
}

static void
murmur3_32_update(struct Hash* hash, const uint8_t* key, size_t len)
{
    uint32_t k;
    uint32_t h = _murmur3_32_hashof(hash);

    // Read in groups of 4.
    for( size_t i = len >> 2; i; i-- )
    {
        // Here is a source of differing results across endiannesses.
        // A swap here has no effects on hash properties though.
        memcpy(&k, key, sizeof(uint32_t));
        key += sizeof(uint32_t);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }

    /* Read the rest. */
    k = 0;
    for( size_t i = len & 3; i; i-- )
    {
        k <<= 8;
        k |= key[i - 1];
    }
    // A swap is *not* necessary here because the preceding loop already
    // places the low bytes in the low places according to whatever endianness
    // we use. Swaps only apply when the memory is copied in a chunk.
    h ^= murmur_32_scramble(k);

    _murmur3_32_sethash(hash, h);
}

static void
murmur3_32_end(struct Hash* hash)
{
    uint32_t h = _murmur3_32_hashof(hash);
    /* Finalize. */
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    _murmur3_32_sethash(hash, h);
}

static int
slotsize(int size, int key_size)
{
    return size + key_size + sizeof(struct EntryHeader);
}

static void*
alloc_buffer(int element_size, int key_size, int capacity_hint)
{
    int size = slotsize(element_size, key_size);
    void* buffer = malloc(size * capacity_hint);
    memset(buffer, 0, size * capacity_hint);

    return buffer;
}

static struct EntryHeader*
ht_slot(struct HashTable* ht, uint32_t slot)
{
    return (struct EntryHeader*)(((char*)ht->data) +
                                 slot * slotsize(ht->element_size, ht->key_size));
}

static void*
ht_slotkey(struct HashTable* ht, uint32_t slot)
{
    return (char*)ht_slot(ht, slot) + sizeof(struct EntryHeader);
}

static void*
ht_slotvalue(struct HashTable* ht, uint32_t slot)
{
    return (char*)ht_slot(ht, slot) + sizeof(struct EntryHeader) + ht->key_size;
}

struct Find
{
    struct EntryHeader* entry;
    int collisions;
};

void
ht_hash_init(struct Hash* hash)
{
    murmur3_32_init(hash, 0);
}

void
ht_hash_update(struct Hash* hash, char const* data, int len)
{
    murmur3_32_update(hash, (uint8_t*)data, len);
}

void
ht_hash_end(struct Hash* hash)
{
    murmur3_32_end(hash);
}

/**
 * @brief
 *
 * [ [Entries] ... [Values] ...]
 *
 * @param ht
 * @param init
 */
void
ht_init(struct HashTable* ht, struct HashTableInit init)
{
    int capacity_hint = init.capacity_hint > 0 ? init.capacity_hint : 100;
    ht->capacity = capacity_hint;
    ht->size = 0;
    ht->element_size = init.element_size;

    // If not specified, assume strings with a specified length
    ht->key_size = init.key_size == 0 ? sizeof(struct DefaultString) : init.key_size;

    ht->data = alloc_buffer(ht->element_size, ht->key_size, ht->capacity);

    for( int i = 0; i < ht->capacity; i++ )
    {
        struct EntryHeader* entry = ht_slot(ht, i);
        entry->kind = SEEmpty;
    }
}

void
ht_cleanup(struct HashTable* ht)
{
    free(ht->data);
    ht->data = NULL;
    ht->size = 0;
    ht->capacity = 0;
}

static void
iter_init_slot(struct HashTableIter* iter, struct HashTable* ht, uint32_t slot)
{
    iter->_slot = slot;

    struct EntryHeader* entry = ht_slot(ht, slot);
    iter->key = ht_slotkey(ht, slot);
    iter->value = ht_slotvalue(ht, slot);

    iter->empty = entry->kind == SEEmpty || entry->kind == SETombstone;
    iter->tombstone = entry->kind == SETombstone;
    iter->at_end = 0;
    iter->_step = 1;
}

struct HashTableIter
ht_lookuph(struct HashTable* ht, struct Hash* hash)
{
    struct HashTableIter iter = { 0 };
    iter._hash = *hash;

    uint32_t hash_value = _murmur3_32_hashof(hash);

    iter_init_slot(&iter, ht, hash_value % (ht->capacity));

    return iter;
}

struct HashTableIter
ht_atsloth(struct HashTable* ht, int slot)
{
    struct HashTableIter iter = { 0 };

    if( slot < 0 || slot >= ht->capacity )
    {
        iter.at_end = 1;
        return iter;
    }

    iter_init_slot(&iter, ht, slot);

    return iter;
}

int
ht_nexth(struct HashTable* ht, struct HashTableIter* iter)
{
    if( iter->at_end )
        return 0;

    iter->_slot = (iter->_slot + 1) % (ht->capacity);
    struct EntryHeader* entry = ht_slot(ht, iter->_slot);
    iter->key = ht_slotkey(ht, iter->_slot);
    iter->value = ht_slotvalue(ht, iter->_slot);

    iter->_step += 1;

    iter->empty = entry->kind == SEEmpty || entry->kind == SETombstone;
    iter->tombstone = entry->kind == SETombstone;
    iter->at_end = iter->_step == MAXIMUM_PROBE_LENGTH;

    return 1;
}

void*
ht_emplaceh(struct HashTable* ht, struct HashTableIter* iter, void const* key)
{
    struct EntryHeader* entry = ht_slot(ht, iter->_slot);
    // TODO: Assert key is equal?
    if( entry->kind == SEEmpty || entry->kind == SETombstone )
    {
        memcpy(ht_slotkey(ht, iter->_slot), key, ht->key_size);
        entry->kind = SEPopulated;
        entry->hash = _murmur3_32_hashof(&iter->_hash);

        ht->size += 1;
    }

    return ht_slotvalue(ht, iter->_slot);
}

/**
 * @brief Returns the entry
 */
void*
ht_removeh(struct HashTable* ht, struct HashTableIter* iter, void const* key)
{
    struct EntryHeader* entry = ht_slot(ht, iter->_slot);
    if( entry->kind == SEEmpty || entry->kind == SETombstone )
        return NULL;

    if( memncmp(key, ht->key_size, iter->key, ht->key_size) == 0 )
    {
        entry->kind = SETombstone;
        return ht_slotvalue(ht, iter->_slot);
    }

    return NULL;
}

void*
ht_lookup(struct HashTable* ht, char const* key, int len)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, key, len);
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(ht, &hash);
    do
    {
        if( iter.empty )
            return NULL;

        struct EntryHeader* entry = ht_slot(ht, iter._slot);
        struct DefaultString* entry_key = (struct DefaultString*)ht_slotkey(ht, iter._slot);
        if( memncmp(key, len, entry_key->data, entry_key->len) == 0 )
            return ht_slotvalue(ht, iter._slot);

    } while( ht_nexth(ht, &iter) );

    return NULL;
}

void*
ht_remove(struct HashTable* ht, char const* key, int len)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, key, len);
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(ht, &hash);
    do
    {
        struct EntryHeader* entry = ht_slot(ht, iter._slot);

        if( iter.empty )
        {
            return NULL;
        }

        struct DefaultString* entry_key = (struct DefaultString*)ht_slotkey(ht, iter._slot);
        if( memncmp(key, len, entry_key->data, entry_key->len) == 0 )
        {
            void* value = ht_slotvalue(ht, iter._slot);
            entry->kind = SETombstone;
            return value;
        }

    } while( ht_nexth(ht, &iter) );

    return NULL;
}

void*
ht_emplace(struct HashTable* ht, char const* key, int len)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, key, len);
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(ht, &hash);
    do
    {
        struct EntryHeader* entry = ht_slot(ht, iter._slot);

        if( iter.empty )
        {
            struct DefaultString storage;
            storage.data = key;
            storage.len = len;

            return ht_emplaceh(ht, &iter, &storage);
        }

        struct DefaultString* entry_key = (struct DefaultString*)ht_slotkey(ht, iter._slot);
        if( memncmp(key, len, entry_key->data, entry_key->len) )
            return ht_slotvalue(ht, iter._slot);

    } while( ht_nexth(ht, &iter) );

    return NULL;
}

void
ht_resize(struct HashTable* ht, int new_capacity)
{
    struct HashTable replacement = { 0 };
    assert(new_capacity > ht->capacity);

    struct HashTableInit init;
    init.capacity_hint = new_capacity;
    init.element_size = ht->element_size;
    init.key_size = ht->key_size;
    ht_init(&replacement, init);

    struct Hash hash = { 0 };

    // Rehash
    for( int i = 0; i < ht->capacity; i++ )
    {
        struct EntryHeader* entry = ht_slot(ht, i);
        if( entry->kind != SEPopulated )
            continue;

        _murmur3_32_sethash(&hash, entry->hash);

        struct HashTableIter iter = ht_lookuph(&replacement, &hash);
        do
        {
            if( iter.empty )
            {
                struct EntryHeader* new_entry = ht_slot(&replacement, iter._slot);
                memcpy(new_entry, entry, slotsize(ht->element_size, ht->key_size));
                break;
            }
        } while( ht_nexth(&replacement, &iter) );
    }

    free(ht->data);
    ht->data = replacement.data;
    ht->capacity = replacement.capacity;
}

int
ht_keyslot(struct HashTable* ht, char const* key, int len)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, key, len);
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(ht, &hash);
    do
    {
        if( iter.empty )
            return -1;

        struct EntryHeader* entry = ht_slot(ht, iter._slot);
        if( entry->kind == SETombstone )
            continue;

        struct DefaultString* entry_key = (struct DefaultString*)ht_slotkey(ht, iter._slot);
        if( memncmp(key, len, entry_key->data, entry_key->len) == 0 )
            return iter._slot;

    } while( ht_nexth(ht, &iter) );

    return -1;
}

int
ht_memusage(struct HashTable* ht)
{
    return ht->capacity * slotsize(ht->element_size, ht->key_size);
}