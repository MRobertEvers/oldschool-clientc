#include "reference_table.h"

#include "rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_IDENTIFIERS RSCACHE_REFTABLE_FLAG_IDENTIFIERS
#define FLAG_WHIRLPOOL RSCACHE_REFTABLE_FLAG_WHIRLPOOL
#define FLAG_SIZES RSCACHE_REFTABLE_FLAG_SIZES
#define FLAG_HASH RSCACHE_REFTABLE_FLAG_HASH

struct RSCache_ReferenceTable*
RSCache_ReferenceTableNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_ReferenceTable* table = malloc(sizeof(struct RSCache_ReferenceTable));
    if( !table )
        return NULL;
    memset(table, 0, sizeof(struct RSCache_ReferenceTable));

    struct RSCache_Buffer buffer = { .data = (uint8_t*)data, .position = 0, .size = (uint32_t)data_size };

    table->format = g1(&buffer);
    if( table->format < 5 || table->format > 7 )
    {
        free(table);
        return NULL;
    }

    if( table->format >= 6 )
        table->version = g4(&buffer);

    table->flags = g1(&buffer);

    int id_count;
    if( table->format >= 7 )
        id_count = gusmart(&buffer);
    else
        id_count = g2(&buffer);

    int* ids = malloc(id_count * sizeof(int));
    if( !ids )
    {
        free(table);
        return NULL;
    }

    table->ids = ids;
    table->id_count = id_count;

    int accumulator = 0;
    int max_id = -1;
    for( int i = 0; i < id_count; i++ )
    {
        int delta = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
        ids[i] = accumulator += delta;
        if( ids[i] > max_id )
            max_id = ids[i];
    }
    max_id++;

    table->archives = malloc(max_id * sizeof(struct RSCache_ReferenceTableArchive));
    if( !table->archives )
    {
        free(ids);
        free(table);
        return NULL;
    }
    memset(table->archives, 0, max_id * sizeof(struct RSCache_ReferenceTableArchive));
    for( int i = 0; i < max_id; i++ )
        table->archives[i].index = -1;
    table->archive_count = max_id;

    for( int i = 0; i < id_count; i++ )
        table->archives[ids[i]].index = ids[i];

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
            table->archives[ids[i]].identifier = g4(&buffer);
    }

    for( int i = 0; i < id_count; i++ )
        table->archives[ids[i]].crc = g4(&buffer);

    /* Whirlpool digests sit between the CRCs and the sizes. No cache in this repo
     * sets the flag (verified across all seven), so this branch was previously
     * absent — which would have silently misread every field after it for a cache
     * that did set it. */
    if( (table->flags & FLAG_WHIRLPOOL) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
            greadto(
                &buffer,
                (char*)table->archives[ids[i]].whirlpool,
                sizeof(table->archives[ids[i]].whirlpool),
                (int)sizeof(table->archives[ids[i]].whirlpool));
    }

    if( (table->flags & FLAG_SIZES) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            table->archives[id].compressed = g4(&buffer);
            table->archives[id].uncompressed = g4(&buffer);
        }
    }

    for( int i = 0; i < id_count; i++ )
        table->archives[ids[i]].version = g4(&buffer);

    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        int child_count = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
        table->archives[id].children.count = child_count;
        table->archives[id].children.files =
            malloc(child_count * sizeof(struct RSCache_ReferenceTableArchiveFile));
        if( !table->archives[id].children.files )
        {
            for( int j = 0; j < i; j++ )
                free(table->archives[ids[j]].children.files);
            free(ids);
            free(table->archives);
            free(table);
            return NULL;
        }
    }

    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        accumulator = 0;
        for( int j = 0; j < table->archives[id].children.count; j++ )
        {
            int delta = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
            table->archives[id].children.files[j].id = accumulator += delta;
        }
    }

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            for( int j = 0; j < table->archives[id].children.count; j++ )
                table->archives[id].children.files[j].name_hash = g4(&buffer);
        }
    }

    return table;
}

uint32_t
RSCache_ReferenceTableEncodeBound(const struct RSCache_ReferenceTable* table)
{
    if( !table )
        return 0;

    /* Per-archive worst case: identifier 4 + crc 4 + whirlpool 64 + sizes 8 +
     * version 4 + child count 4 (usmart) = 88, plus 8 per child (delta + name
     * hash). Plus the fixed header. */
    uint32_t total = 16;
    total += (uint32_t)table->id_count * 4u; /* id deltas */
    total += (uint32_t)table->id_count * 88u;

    for( int i = 0; i < table->id_count; i++ )
    {
        int id = table->ids[i];
        if( id >= 0 && id < table->archive_count )
            total += (uint32_t)table->archives[id].children.count * 8u;
    }

    return total;
}

uint32_t
RSCache_ReferenceTableEncode(
    const struct RSCache_ReferenceTable* table,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !table || !out )
        return 0;
    if( table->format < 5 || table->format > 7 )
        return 0;
    if( !table->ids || !table->archives )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Format 7 widened every count and delta from u16 to usmart. */
    bool smart = table->format >= 7;
#define REFTABLE_PCOUNT(value)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( smart )                                                                                \
            pusmart(&buffer, (value));                                                             \
        else                                                                                       \
            p2(&buffer, (value));                                                                  \
    } while( 0 )

    p1(&buffer, table->format);
    if( table->format >= 6 )
        p4(&buffer, table->version);
    p1(&buffer, table->flags);

    REFTABLE_PCOUNT(table->id_count);

    /* Ids are stored as deltas from the previous id. */
    int previous = 0;
    for( int i = 0; i < table->id_count; i++ )
    {
        REFTABLE_PCOUNT(table->ids[i] - previous);
        previous = table->ids[i];
    }

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < table->id_count; i++ )
            p4(&buffer, table->archives[table->ids[i]].identifier);
    }

    for( int i = 0; i < table->id_count; i++ )
        p4(&buffer, table->archives[table->ids[i]].crc);

    if( (table->flags & FLAG_WHIRLPOOL) != 0 )
    {
        for( int i = 0; i < table->id_count; i++ )
            pbuf(
                &buffer,
                table->archives[table->ids[i]].whirlpool,
                (int)sizeof(table->archives[table->ids[i]].whirlpool));
    }

    if( (table->flags & FLAG_SIZES) != 0 )
    {
        for( int i = 0; i < table->id_count; i++ )
        {
            int id = table->ids[i];
            p4(&buffer, table->archives[id].compressed);
            p4(&buffer, table->archives[id].uncompressed);
        }
    }

    for( int i = 0; i < table->id_count; i++ )
        p4(&buffer, table->archives[table->ids[i]].version);

    for( int i = 0; i < table->id_count; i++ )
        REFTABLE_PCOUNT(table->archives[table->ids[i]].children.count);

    for( int i = 0; i < table->id_count; i++ )
    {
        int id = table->ids[i];
        int child_previous = 0;
        for( int j = 0; j < table->archives[id].children.count; j++ )
        {
            REFTABLE_PCOUNT(table->archives[id].children.files[j].id - child_previous);
            child_previous = table->archives[id].children.files[j].id;
        }
    }

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < table->id_count; i++ )
        {
            int id = table->ids[i];
            for( int j = 0; j < table->archives[id].children.count; j++ )
                p4(&buffer, table->archives[id].children.files[j].name_hash);
        }
    }

#undef REFTABLE_PCOUNT

    return buffer.position;
}

void
RSCache_ReferenceTableFree(struct RSCache_ReferenceTable* table)
{
    if( !table )
        return;

    if( table->ids )
        free(table->ids);

    for( int i = 0; i < table->archive_count; i++ )
    {
        if( table->archives[i].children.files )
            free(table->archives[i].children.files);
    }

    if( table->archives )
        free(table->archives);

    free(table);
}
