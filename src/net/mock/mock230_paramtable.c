/*
 * The shared (owner, key) -> value param table. See mock230_paramtable.h for
 * why there is one of these rather than one per config type.
 */

#include "mock230_paramtable.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
compare_row(
    const void* a,
    const void* b)
{
    const struct Mock230ParamRow* left = (const struct Mock230ParamRow*)a;
    const struct Mock230ParamRow* right = (const struct Mock230ParamRow*)b;

    if( left->owner != right->owner )
        return left->owner < right->owner ? -1 : 1;
    if( left->key != right->key )
        return left->key < right->key ? -1 : 1;
    return 0;
}

static void
add_row(
    struct Mock230ParamTable* table,
    int owner,
    int key,
    int ival,
    const char* sval)
{
    struct Mock230ParamRow* row;

    if( table->count == table->capacity )
    {
        int capacity = table->capacity ? table->capacity * 2 : 4096;
        struct Mock230ParamRow* grown =
            (struct Mock230ParamRow*)realloc(table->rows, (size_t)capacity * sizeof(*grown));

        if( !grown )
            return;
        table->rows = grown;
        table->capacity = capacity;
    }
    row = &table->rows[table->count++];
    row->owner = owner;
    row->key = key;
    row->ival = ival;
    row->sval = sval ? strdup(sval) : NULL;
}

void
mock230_paramtable_read(
    struct Mock230ParamTable* table,
    int owner,
    const struct RSCache_Params* params)
{
    if( !table || !params )
        return;

    /* Anything appended invalidates the order the last `_sort` established. */
    table->sorted = 0;

    for( int i = 0; i < params->count; i++ )
    {
        if( !params->values[i] )
            continue;
        if( params->kinds[i] == RSCACHE_PARAM_STRING )
            add_row(table, owner, params->keys[i], 0, (const char*)params->values[i]);
        else if( params->kinds[i] == RSCACHE_PARAM_INT )
            add_row(table, owner, params->keys[i], *(const int*)params->values[i], NULL);
        /* RSCACHE_PARAM_LONG is dropped — see the header. */
    }
}

void
mock230_paramtable_sort(struct Mock230ParamTable* table)
{
    if( !table )
        return;
    if( table->rows && table->count > 1 )
        qsort(table->rows, (size_t)table->count, sizeof(*table->rows), compare_row);
    table->sorted = 1;
}

const struct Mock230ParamRow*
mock230_paramtable_find(
    const struct Mock230ParamTable* table,
    int owner,
    int key)
{
    int low;
    int high;

    if( !table || !table->rows || table->count <= 0 )
        return NULL;
    if( !table->sorted )
    {
        /*
         * The binary search below would answer *sometimes* on an unsorted
         * table, which is exactly the failure this whole file exists to
         * prevent. Refuse loudly instead of half-working.
         */
        fprintf(stderr,
                "mock230: param table queried before it was sorted (owner %d, key %d)\n",
                owner, key);
        return NULL;
    }

    low = 0;
    high = table->count - 1;
    while( low <= high )
    {
        int mid = low + (high - low) / 2;
        struct Mock230ParamRow* row = &table->rows[mid];

        if( row->owner < owner || (row->owner == owner && row->key < key) )
            low = mid + 1;
        else if( row->owner > owner || (row->owner == owner && row->key > key) )
            high = mid - 1;
        else
            return row;
    }
    return NULL;
}

void
mock230_paramtable_free(struct Mock230ParamTable* table)
{
    if( !table )
        return;
    for( int i = 0; i < table->count; i++ )
        free(table->rows[i].sval);
    free(table->rows);
    table->rows = NULL;
    table->count = 0;
    table->capacity = 0;
    table->sorted = 0;
}

size_t
mock230_paramtable_bytes(const struct Mock230ParamTable* table)
{
    size_t bytes;

    if( !table )
        return 0;
    bytes = (size_t)table->count * sizeof(struct Mock230ParamRow);
    for( int i = 0; i < table->count; i++ )
        if( table->rows[i].sval )
            bytes += strlen(table->rows[i].sval) + 1;
    return bytes;
}
