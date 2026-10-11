/*
 * The shared (owner, key) -> value param table. See torirs_server_paramtable.h for
 * why there is one of these rather than one per config type.
 */

#include "torirs_server_paramtable.h"
#include <assert.h>

#include <rscache.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
compare_row(
    const void* a,
    const void* b)
{
    const struct ToriRSServerParamRow* left = (const struct ToriRSServerParamRow*)a;
    const struct ToriRSServerParamRow* right = (const struct ToriRSServerParamRow*)b;

    if( left->owner != right->owner )
        return left->owner < right->owner ? -1 : 1;
    if( left->key != right->key )
        return left->key < right->key ? -1 : 1;
    return 0;
}

static void
add_row(
    struct ToriRSServerParamTable* table,
    int owner,
    int key,
    int ival,
    const char* sval)
{
    struct ToriRSServerParamRow* row;

    if( table->count == table->capacity )
    {
        int capacity = table->capacity ? table->capacity * 2 : 4096;
        struct ToriRSServerParamRow* grown =
            (struct ToriRSServerParamRow*)realloc(table->rows, (size_t)capacity * sizeof(*grown));

        assert(grown);
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
ToriRSServer_ParamTableRead(
    struct ToriRSServerParamTable* table,
    int owner,
    const struct RSCache_Params* params)
{
    assert(table);
    assert(params);

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
ToriRSServer_ParamTableSetInt(
    struct ToriRSServerParamTable* table,
    int owner,
    int key,
    int ival)
{
    struct ToriRSServerParamRow* row;

    assert(table);
    if( table->pending_count == table->pending_capacity )
    {
        int capacity = table->pending_capacity ? table->pending_capacity * 2 : 4096;

        table->pending = (struct ToriRSServerParamRow*)realloc(
            table->pending, (size_t)capacity * sizeof(*table->pending));
        assert(table->pending);
        table->pending_capacity = capacity;
    }
    /* `sval` carries nothing for an int row, so it holds the call order: two
     * sets of one (owner, key) keep the later one, as the old in-place write
     * did. */
    row = &table->pending[table->pending_count];
    row->owner = owner;
    row->key = key;
    row->ival = ival;
    row->sval = (char*)(intptr_t)table->pending_count;
    table->pending_count++;
    table->sorted = 0;
}

static int
compare_pending(
    const void* a,
    const void* b)
{
    int order = compare_row(a, b);

    if( order )
        return order;
    return (intptr_t)((const struct ToriRSServerParamRow*)a)->sval <
                   (intptr_t)((const struct ToriRSServerParamRow*)b)->sval
               ? -1
               : 1;
}

void
ToriRSServer_ParamTableSort(struct ToriRSServerParamTable* table)
{
    int cache_rows;

    assert(table);
    if( table->rows && table->count > 1 )
        qsort(table->rows, (size_t)table->count, sizeof(*table->rows), compare_row);
    table->sorted = 1;
    if( !table->pending_count )
        return;

    /* The overlay: the last set of each (owner, key) replaces the cache's row,
     * or joins the table when the cache had none. Searched over the cache's
     * rows only, which are sorted now and which the appends do not disturb. */
    qsort(table->pending, (size_t)table->pending_count, sizeof(*table->pending),
          compare_pending);
    cache_rows = table->count;
    for( int i = 0; i < table->pending_count; i++ )
    {
        const struct ToriRSServerParamRow* set = &table->pending[i];
        struct ToriRSServerParamRow* row;

        if( i + 1 < table->pending_count && !compare_row(set, &table->pending[i + 1]) )
            continue;
        row = (struct ToriRSServerParamRow*)bsearch(set, table->rows, (size_t)cache_rows,
                                                    sizeof(*table->rows), compare_row);
        if( row )
        {
            /* An overlay row wins over the cache's — that is what an overlay is. */
            free(row->sval);
            row->sval = NULL;
            row->ival = set->ival;
        }
        else
            add_row(table, set->owner, set->key, set->ival, NULL);
    }
    free(table->pending);
    table->pending = NULL;
    table->pending_count = 0;
    table->pending_capacity = 0;
    if( table->count > cache_rows )
        qsort(table->rows, (size_t)table->count, sizeof(*table->rows), compare_row);
}

const struct ToriRSServerParamRow*
ToriRSServer_ParamTableFind(
    const struct ToriRSServerParamTable* table,
    int owner,
    int key)
{
    int low;
    int high;

    assert(table);
    if( !table->rows || table->count <= 0 )
        return NULL;
    if( !table->sorted )
    {
        /*
         * The binary search below would answer *sometimes* on an unsorted
         * table, which is exactly the failure this whole file exists to
         * prevent. Refuse loudly instead of half-working.
         */
        fprintf(stderr,
                "torirsserver: param table queried before it was sorted (owner %d, key %d)\n",
                owner, key);
        return NULL;
    }

    low = 0;
    high = table->count - 1;
    while( low <= high )
    {
        int mid = low + (high - low) / 2;
        struct ToriRSServerParamRow* row = &table->rows[mid];

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
ToriRSServer_ParamTableFree(struct ToriRSServerParamTable* table)
{
    if( !table )
        return;
    for( int i = 0; i < table->count; i++ )
        free(table->rows[i].sval);
    free(table->rows);
    free(table->pending);
    table->rows = NULL;
    table->count = 0;
    table->capacity = 0;
    table->sorted = 0;
    table->pending = NULL;
    table->pending_count = 0;
    table->pending_capacity = 0;
}

size_t
ToriRSServer_ParamTableBytes(const struct ToriRSServerParamTable* table)
{
    size_t bytes;

    assert(table);
    bytes = (size_t)table->count * sizeof(struct ToriRSServerParamRow);
    for( int i = 0; i < table->count; i++ )
        if( table->rows[i].sval )
            bytes += strlen(table->rows[i].sval) + 1;
    return bytes;
}
