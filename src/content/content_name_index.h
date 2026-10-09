#ifndef SRC_CONTENT_CONTENT_NAME_INDEX_H
#define SRC_CONTENT_CONTENT_NAME_INDEX_H

/*
 * A name -> int index for the content loader's tables: open addressing over
 * FNV-1a, linear probing, grown at half full.
 *
 * The tables it indexes (`.constant` names, enum symbols) were found by a
 * strcmp scan of the whole table: the enum list holds every server-pack enum,
 * ~5,900 of them, and the appearance encoder asked it for one by name for every
 * slot of every player every tick; the `.constant` loader asked the constants
 * for each new line's name to refuse a duplicate, which made the load O(n^2).
 *
 * **The first name added wins.** A second `Add` of a name already present keeps
 * the first value and returns it, so an index filled in storage order answers
 * exactly what a front-to-back scan answered.
 *
 * The index does not own the names: a slot points at the caller's string, which
 * must outlive the index (every table here strdup's its names and frees them in
 * the same place it frees the index). Values are non-negative; -1 is "absent".
 *
 * Header-only for the reason content_value.h is: the loader is linked into
 * torirsserver, sscompile and the tests, and a .c file would have to be added
 * to every one of those link lines.
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct ContentNameIndexSlot
{
    const char* name; /* NULL: empty */
    uint32_t hash;
    int value;
};

struct ContentNameIndex
{
    struct ContentNameIndexSlot* slots;
    uint32_t mask; /* capacity - 1; capacity is a power of two */
    int count;
};

static inline uint32_t
ContentNameIndex_Hash(const char* name)
{
    uint32_t hash = 2166136261u;

    for( const unsigned char* p = (const unsigned char*)name; *p; p++ )
        hash = (hash ^ *p) * 16777619u;
    return hash;
}

/** Accepts a zeroed (never-added-to) index, like free(NULL). */
static inline void
ContentNameIndex_Free(struct ContentNameIndex* index)
{
    assert(index);
    free(index->slots);
    index->slots = NULL;
    index->mask = 0;
    index->count = 0;
}

/** The slot holding `name`, or the empty slot where it would go. */
static inline struct ContentNameIndexSlot*
ContentNameIndex_Probe(
    const struct ContentNameIndex* index,
    const char* name,
    uint32_t hash)
{
    uint32_t at = hash & index->mask;

    for( ;; )
    {
        struct ContentNameIndexSlot* slot = &index->slots[at];

        if( !slot->name || (slot->hash == hash && strcmp(slot->name, name) == 0) )
            return slot;
        at = (at + 1) & index->mask;
    }
}

static inline void
ContentNameIndex_Grow(struct ContentNameIndex* index)
{
    struct ContentNameIndexSlot* old = index->slots;
    uint32_t old_capacity = old ? index->mask + 1 : 0;
    uint32_t capacity = old_capacity ? old_capacity * 2 : 64;

    index->slots = calloc(capacity, sizeof(*index->slots));
    assert(index->slots);
    index->mask = capacity - 1;
    for( uint32_t i = 0; i < old_capacity; i++ )
    {
        if( old[i].name )
            *ContentNameIndex_Probe(index, old[i].name, old[i].hash) = old[i];
    }
    free(old);
}

/**
 * Bind `name` to `value` unless the name is already bound; the value the name
 * answers afterwards either way.
 */
static inline int
ContentNameIndex_Add(
    struct ContentNameIndex* index,
    const char* name,
    int value)
{
    uint32_t hash;
    struct ContentNameIndexSlot* slot;

    assert(index);
    assert(name);
    assert(value >= 0);
    if( !index->slots || (uint32_t)(index->count + 1) * 2 > index->mask + 1 )
        ContentNameIndex_Grow(index);
    hash = ContentNameIndex_Hash(name);
    slot = ContentNameIndex_Probe(index, name, hash);
    if( slot->name )
        return slot->value;
    slot->name = name;
    slot->hash = hash;
    slot->value = value;
    index->count++;
    return value;
}

/** The value bound to `name`, or -1. */
static inline int
ContentNameIndex_Find(
    const struct ContentNameIndex* index,
    const char* name)
{
    struct ContentNameIndexSlot* slot;

    assert(index);
    assert(name);
    if( !index->slots )
        return -1;
    slot = ContentNameIndex_Probe(index, name, ContentNameIndex_Hash(name));
    return slot->name ? slot->value : -1;
}

#endif
