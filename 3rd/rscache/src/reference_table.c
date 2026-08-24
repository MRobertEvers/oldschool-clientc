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
        {
            unsigned char* digest = malloc(RSCACHE_REFTABLE_WHIRLPOOL_BYTES);
            assert(digest);
            table->archives[ids[i]].whirlpool = digest;
            greadto(
                &buffer,
                (char*)digest,
                RSCACHE_REFTABLE_WHIRLPOOL_BYTES,
                RSCACHE_REFTABLE_WHIRLPOOL_BYTES);
        }
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

    uint64_t total_children = 0;
    for( int i = 0; i < id_count; i++ )
    {
        int child_count = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
        table->archives[ids[i]].children.count = child_count;
        total_children += (uint64_t)child_count;
    }

    /* Every child costs at least one delta byte in the stream, so a sum past
     * the container is a malformed table — and on a 32-bit size_t the multiply
     * below could wrap and undersize the pool. */
    if( total_children > (uint64_t)buffer.size )
    {
        free(ids);
        free(table->archives);
        free(table);
        return NULL;
    }

    if( total_children > 0 )
    {
        /* Zeroed, because name_hash is filled in only under FLAG_IDENTIFIERS and
         * most tables do not set it -- a malloc'd pool left every child in those
         * tables carrying whatever the heap had there, which the encoder would
         * then write out as a name. */
        table->children_pool = calloc(
            (size_t)total_children, sizeof(struct RSCache_ReferenceTableArchiveFile));
        if( !table->children_pool )
        {
            free(ids);
            free(table->archives);
            free(table);
            return NULL;
        }
    }
    table->children_pool_count = (size_t)total_children;

    size_t pool_used = 0;
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        if( table->archives[id].children.count <= 0 )
            continue;
        table->archives[id].children.files = table->children_pool + pool_used;
        pool_used += (size_t)table->archives[id].children.count;
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
        {
            assert(table->archives[table->ids[i]].whirlpool);
            pbuf(
                &buffer,
                table->archives[table->ids[i]].whirlpool,
                RSCACHE_REFTABLE_WHIRLPOOL_BYTES);
        }
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

bool
RSCache_ReferenceTableChildrenPooled(
    const struct RSCache_ReferenceTable* table,
    const struct RSCache_ReferenceTableArchiveFile* files)
{
    uintptr_t p;
    uintptr_t base;
    uintptr_t end;

    assert(table);
    if( !files || !table->children_pool )
        return false;
    p = (uintptr_t)files;
    base = (uintptr_t)table->children_pool;
    end = (uintptr_t)(table->children_pool + table->children_pool_count);
    return p >= base && p < end;
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
        /* A slice of the decode pool is not its own allocation; only an array a
         * tool replaced after decode (or a hand-built table's) is freed here. */
        if( table->archives[i].children.files &&
            !RSCache_ReferenceTableChildrenPooled(table, table->archives[i].children.files) )
            free(table->archives[i].children.files);
        if( table->archives[i].whirlpool )
            free(table->archives[i].whirlpool);
    }

    free(table->children_pool);

    if( table->archives )
        free(table->archives);

    free(table);
}
