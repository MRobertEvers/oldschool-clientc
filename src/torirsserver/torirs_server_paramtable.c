/*
 * The shared (owner, key) -> value param table. See torirs_server_paramtable.h for
 * why there is one of these rather than one per config type.
 */

#include "torirs_server_paramtable.h"
#include <assert.h>

#include <rscache.h>

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
    assert(table);
    /* Linear, because of who calls it: the content overlay, once at boot, with
     * hundreds of rows against a table the cache filled with tens of thousands.
     * A binary search would need the table sorted and this runs *after* the
     * cache's `_read` pass and *before* `_sort`. */
    for( int i = 0; i < table->count; i++ )
    {
        if( table->rows[i].owner != owner || table->rows[i].key != key )
            continue;
        /* An overlay row wins over the cache's — that is what an overlay is. */
        free(table->rows[i].sval);
        table->rows[i].sval = NULL;
        table->rows[i].ival = ival;
        return;
    }
    add_row(table, owner, key, ival, NULL);
    table->sorted = 0;
}

void
ToriRSServer_ParamTableSort(struct ToriRSServerParamTable* table)
{
    assert(table);
    if( table->rows && table->count > 1 )
        qsort(table->rows, (size_t)table->count, sizeof(*table->rows), compare_row);
    table->sorted = 1;
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
    table->rows = NULL;
    table->count = 0;
    table->capacity = 0;
    table->sorted = 0;
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
