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
     * that did set it.
     *
     * The first slot request allocates the whole pool, indexed by archive id.
     * A sparse table pays 64 bytes for each of its holes that way, which is a
     * bad trade only for a cache that sets this flag -- and none does. */
    if( (table->flags & FLAG_WHIRLPOOL) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            greadto(
                &buffer,
                (char*)RSCache_ReferenceTableWhirlpoolSlot(table, ids[i]),
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

    /*
     * The child ids are read twice. The first pass stores nothing; it only asks,
     * per archive, whether the ids come out 0,1,2,... — 45% of the children in a
     * shipped cache are numbered exactly that way, and for those the array only
     * repeats what the index already says.
     *
     * Asking before allocating, rather than decoding and compacting after, is
     * what makes the saving real: the peak is what the memory budget is measured
     * against, and a compaction would have paid the full size first.
     */
    unsigned char* identity_bits = NULL;
    uint64_t stored_children = 0;
    uint32_t child_ids_position = buffer.position;

    if( total_children > 0 )
    {
        identity_bits = calloc(((size_t)id_count + 7) / 8, 1);
        assert(identity_bits);

        for( int i = 0; i < id_count; i++ )
        {
            int child_count = table->archives[ids[i]].children.count;
            bool identity = true;

            accumulator = 0;
            for( int j = 0; j < child_count; j++ )
            {
                int delta = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
                accumulator += delta;
                if( accumulator != j )
                    identity = false;
            }

            if( child_count > 0 && identity )
                identity_bits[i >> 3] |= (unsigned char)(1 << (i & 7));
            else
                stored_children += (uint64_t)child_count;
        }

        buffer.position = child_ids_position;
    }

    if( stored_children > 0 )
    {
        /* Zeroed rather than malloc'd: a table that declares more children than
         * its remaining bytes can supply leaves the tail of the pool unwritten,
         * and the encoder would hand those ids straight back out. */
        table->children_pool = calloc(
            (size_t)stored_children, sizeof(struct RSCache_ReferenceTableArchiveFile));
        assert(table->children_pool);
    }
    table->children_pool_count = (size_t)stored_children;

    /* Only a table with identifiers has anything to put here; the rest leave it
     * NULL and every reader takes that as "unnamed". Sized to every child and
     * not just the stored ones: an identity run holds no ids and still names
     * each of its files, so the two pools no longer share offsets. */
    if( total_children > 0 && (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        table->children_name_hash_pool =
            calloc((size_t)total_children, sizeof(int));
        assert(table->children_name_hash_pool);
        table->children_name_hash_pool_count = (size_t)total_children;
    }

    size_t pool_used = 0;
    size_t hashes_used = 0;
    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        int child_count = table->archives[id].children.count;

        if( child_count <= 0 )
            continue;

        if( table->children_name_hash_pool )
            table->archives[id].children.name_hashes =
                table->children_name_hash_pool + hashes_used;
        hashes_used += (size_t)child_count;

        /* An identity run is left with `files` NULL, which is what says it is
         * one; it gets no slice of the id pool, which was not sized for it. */
        if( (identity_bits[i >> 3] & (1 << (i & 7))) != 0 )
            continue;

        table->archives[id].children.files = table->children_pool + pool_used;
        pool_used += (size_t)child_count;
    }

    for( int i = 0; i < id_count; i++ )
    {
        int id = ids[i];
        struct RSCache_ReferenceTableArchiveFile* files =
            table->archives[id].children.files;

        accumulator = 0;
        for( int j = 0; j < table->archives[id].children.count; j++ )
        {
            int delta = table->format >= 7 ? gusmart(&buffer) : g2(&buffer);
            accumulator += delta;
            /* An identity run is walked again only to carry the cursor past it. */
            if( files )
                files[j].id = accumulator;
        }
    }

    free(identity_bits);

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < id_count; i++ )
        {
            int id = ids[i];
            for( int j = 0; j < table->archives[id].children.count; j++ )
                table->archives[id].children.name_hashes[j] = g4(&buffer);
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
            const unsigned char* digest =
                RSCache_ReferenceTableWhirlpool(table, table->ids[i]);
            /* The flag promises a digest for every listed archive. */
            assert(digest);
            pbuf(&buffer, digest, RSCACHE_REFTABLE_WHIRLPOOL_BYTES);
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
            int child_id = RSCache_ReferenceTableChildId(&table->archives[id], j);
            REFTABLE_PCOUNT(child_id - child_previous);
            child_previous = child_id;
        }
    }

    if( (table->flags & FLAG_IDENTIFIERS) != 0 )
    {
        for( int i = 0; i < table->id_count; i++ )
        {
            int id = table->ids[i];
            for( int j = 0; j < table->archives[id].children.count; j++ )
                p4(&buffer,
                   RSCache_ReferenceTableChildNameHash(&table->archives[id], j));
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

bool
RSCache_ReferenceTableChildNameHashesPooled(
    const struct RSCache_ReferenceTable* table,
    const int* name_hashes)
{
    uintptr_t p;
    uintptr_t base;
    uintptr_t end;

    assert(table);
    if( !name_hashes || !table->children_name_hash_pool )
        return false;
    p = (uintptr_t)name_hashes;
    base = (uintptr_t)table->children_name_hash_pool;
    end = (uintptr_t)(table->children_name_hash_pool + table->children_name_hash_pool_count);
    return p >= base && p < end;
}

int
RSCache_ReferenceTableChildId(
    const struct RSCache_ReferenceTableArchive* archive,
    int child_index)
{
    assert(archive);
    assert(child_index >= 0);
    assert(child_index < archive->children.count);

    /* No array means an identity run, where the id is the index. */
    if( !archive->children.files )
        return child_index;
    return archive->children.files[child_index].id;
}

const unsigned char*
RSCache_ReferenceTableWhirlpool(
    const struct RSCache_ReferenceTable* table,
    int archive_id)
{
    assert(table);
    assert(archive_id >= 0);

    /* "This archive has no digest" is an answer, not a bad argument: every
     * cache in this repo leaves the pool unallocated. */
    if( !table->whirlpools || archive_id >= table->whirlpool_count )
        return NULL;
    return table->whirlpools + (size_t)archive_id * RSCACHE_REFTABLE_WHIRLPOOL_BYTES;
}

unsigned char*
RSCache_ReferenceTableWhirlpoolSlot(
    struct RSCache_ReferenceTable* table,
    int archive_id)
{
    assert(table);
    assert(archive_id >= 0);
    assert(archive_id < table->archive_count);

    /* Sized against archive_count on every call, so a table that grew after the
     * pool was made catches up here instead of in the growth code. */
    if( table->whirlpool_count < table->archive_count )
    {
        size_t was = (size_t)table->whirlpool_count * RSCACHE_REFTABLE_WHIRLPOOL_BYTES;
        size_t now = (size_t)table->archive_count * RSCACHE_REFTABLE_WHIRLPOOL_BYTES;
        unsigned char* grown = realloc(table->whirlpools, now);
        assert(grown);
        memset(grown + was, 0, now - was);
        table->whirlpools = grown;
        table->whirlpool_count = table->archive_count;
    }
    return table->whirlpools + (size_t)archive_id * RSCACHE_REFTABLE_WHIRLPOOL_BYTES;
}

int
RSCache_ReferenceTableChildNameHash(
    const struct RSCache_ReferenceTableArchive* archive,
    int child_index)
{
    assert(archive);
    assert(child_index >= 0);
    assert(child_index < archive->children.count);

    /* Absent means unnamed. Tables without FLAG_IDENTIFIERS never carried a
     * hash for a child; they only ever stored the zero this returns. */
    if( !archive->children.name_hashes )
        return 0;
    return archive->children.name_hashes[child_index];
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
        /* A slice of a decode pool is not its own allocation; only an array a
         * tool replaced after decode (or a hand-built table's) is freed here.
         * Asked of each array separately, because the two no longer travel
         * together: an identity run has no `files` and still has name hashes. */
        if( !RSCache_ReferenceTableChildrenPooled(table, table->archives[i].children.files) )
            free(table->archives[i].children.files);
        if( !RSCache_ReferenceTableChildNameHashesPooled(
                table, table->archives[i].children.name_hashes) )
            free(table->archives[i].children.name_hashes);
    }

    free(table->children_pool);
    free(table->children_name_hash_pool);
    free(table->whirlpools);

    if( table->archives )
        free(table->archives);

    free(table);
}
