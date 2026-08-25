#include "cachepack.h"
#include "tool_posix_compat.h"

#include "archive.h"
#include "checksum.h"
#include "dat2disk.h"
#include "reference_table.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define cp_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define cp_mkdir(p) mkdir(p, 0755)
#endif

/*
 * Binary tables: everything that is not a config.
 *
 * Configs are text because a config is a record with named fields. A model, a
 * sprite, a map square, a compiled script and a sound bank are not, and inventing a
 * text form for each would mean a decoder, an encoder and a fidelity claim per
 * table — a much larger job than this one, and one the library already partly
 * refuses (see EXCEPTIONS.md B10/B11 on the model tail).
 *
 * So these move as **raw container bytes**: the compression byte, the lengths and
 * the payload exactly as the cache stores them, never decompressed. Three things
 * follow, and all three are the reason this is the right shape:
 *
 *  - the round trip is byte-exact by construction, with no encoder to be wrong;
 *  - XTEA-encrypted archives (the map table) pass through untouched, where
 *    decompressing and re-framing would have silently dropped the encryption;
 *  - the bzip2 encoder's known non-identity to Jagex's (EXCEPTIONS.md A1) never
 *    enters the picture.
 *
 * On import the reference table entry is rewritten from the bytes actually stored,
 * so an edited archive is not left behind a stale CRC that the client rejects.
 */

static int
ensure_dir(const char* path)
{
    struct stat st;
    if( stat(path, &st) == 0 )
        return S_ISDIR(st.st_mode) ? 0 : -1;
    /*
     * EEXIST is success. That is the ordinary mkdir -p rule (another process
     * may have won the race), and on Windows it is also what makes an ABSOLUTE
     * path work at all: the callers below split on '/' from index 1, so the
     * first component of "C:/Users/..." is the bare drive designator "C:",
     * where MSVCRT's stat() fails with ENOENT and _mkdir() then fails with
     * EEXIST. Without this, every absolute path is an error.
     */
    if( cp_mkdir(path) == 0 )
        return 0;
    return errno == EEXIST ? 0 : -1;
}

/**
 * Read one archive's container bytes straight out of the sector chain.
 *
 * `RSCache_Dat2DiskArchiveNewLoad` decompresses on the way out, which is what
 * every other caller wants and exactly what this one must not have.
 */
static uint8_t*
read_raw_container(
    struct CP_Ctx* ctx,
    int table_id,
    int archive_id,
    int* out_size)
{
    *out_size = 0;

    char idx_path[1400];
    snprintf(idx_path, sizeof(idx_path), "%s/main_file_cache.idx%d", ctx->cache.disk->directory,
             table_id);
    FILE* idx = fopen(idx_path, "rb");
    if( !idx )
        return NULL;

    struct RSCache_Dat2DiskIndexRecord record;
    memset(&record, 0, sizeof(record));
    int rc = RSCache_Dat2DiskIndexFileReadRecord(idx, archive_id, &record);
    fclose(idx);
    if( rc != 0 || record.sector <= 0 || record.length <= 0 )
        return NULL;

    struct RSCache_Dat2DiskArchive archive;
    memset(&archive, 0, sizeof(archive));
    if( RSCache_Dat2DiskDat2FileReadArchive(
            ctx->cache.disk->dat2_file, table_id, archive_id, record.sector, record.length,
            &archive) != 0 )
    {
        free(archive.data);
        return NULL;
    }

    *out_size = archive.data_size;
    return (uint8_t*)archive.data;
}

static int
parse_table_list(
    const char* csv,
    int* tables,
    int max)
{
    if( !csv || !*csv )
        return 0;
    int count = 0;
    const char* p = csv;
    while( *p && count < max )
    {
        char* end = NULL;
        long value = strtol(p, &end, 10);
        if( end == p )
            break;
        tables[count++] = (int)value;
        if( *end != ',' )
            break;
        p = end + 1;
    }
    return count;
}

int
cp_binary_export(
    struct CP_Ctx* ctx,
    const char* tables_csv)
{
    char root[1200];
    snprintf(root, sizeof(root), "%s/binary", ctx->srcdir);
    if( ensure_dir(ctx->srcdir) != 0 || ensure_dir(root) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create %s\n", root);
        return 0;
    }

    int tables[64];
    int table_count = parse_table_list(tables_csv, tables, 64);
    if( table_count == 0 )
    {
        /* Everything the cache actually has, so a plain `--binary` is a full
         * extraction rather than a list the caller has to know in advance. */
        for( int t = 0; t < RSCACHE_DAT2_DISK_TABLE_CAPACITY && table_count < 64; t++ )
        {
            if( ctx->cache.disk->tables[t] )
                tables[table_count++] = t;
        }
    }

    long long total_bytes = 0;
    int total_archives = 0;

    for( int i = 0; i < table_count; i++ )
    {
        int table_id = tables[i];
        struct RSCache_ReferenceTable* rt =
            table_id >= 0 && table_id < RSCACHE_DAT2_DISK_TABLE_CAPACITY
                ? ctx->cache.disk->tables[table_id]
                : NULL;
        if( !rt )
        {
            fprintf(stderr, "cachepack: table %d is not in this cache\n", table_id);
            continue;
        }

        char dir[1300];
        snprintf(dir, sizeof(dir), "%s/idx%d", root, table_id);
        if( ensure_dir(dir) != 0 )
        {
            fprintf(stderr, "cachepack: cannot create %s\n", dir);
            return 0;
        }

        int written = 0;
        long long bytes = 0;
        for( int a = 0; a < rt->id_count; a++ )
        {
            int archive_id = rt->ids[a];
            int size = 0;
            uint8_t* raw = read_raw_container(ctx, table_id, archive_id, &size);
            if( !raw || size <= 0 )
            {
                free(raw);
                continue;
            }
            char path[1500];
            snprintf(path, sizeof(path), "%s/%d.bin", dir, archive_id);
            FILE* out = fopen(path, "wb");
            if( !out )
            {
                fprintf(stderr, "cachepack: cannot write %s: %s\n", path, strerror(errno));
                free(raw);
                return 0;
            }
            fwrite(raw, 1, (size_t)size, out);
            fclose(out);
            free(raw);
            written++;
            bytes += size;
        }
        printf("  idx%-3d %6d archives, %lld bytes\n", table_id, written, bytes);
        total_archives += written;
        total_bytes += bytes;
    }

    printf("Exported %d archives, %lld bytes into %s\n", total_archives, total_bytes, root);
    return 1;
}

static uint32_t
read_u32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

/**
 * Split a stored container into its body and its version trailer.
 *
 * The trailer is not part of the container proper — `RSCache_ArchiveEncode` does
 * not write one — but Jagex's stored archives carry a u16 version after the
 * payload, and **the reference table's CRC covers the body only**.
 *
 * That is measured, not assumed. Over `cache.osrs230` idx13, `crc32` of the whole
 * stored archive matches the table's CRC on 0 of 10 archives and `crc32` of the
 * bytes up to `size - 2` matches on 10 of 10. Getting it wrong is not a cosmetic
 * difference: an import would write a CRC the client then rejects the archive for.
 *
 * Returns the body length. `*out_trailer` receives what is left, which is 0, 2 or
 * 4 bytes. A container whose header does not parse reports the whole thing as body
 * and no trailer, which is the conservative reading.
 */
static int
container_split(
    const uint8_t* data,
    int size,
    int* out_trailer,
    int* out_uncompressed)
{
    *out_trailer = 0;
    *out_uncompressed = 0;
    if( size < 5 )
        return size;

    int compression = data[0];
    int compressed = (int)read_u32(data + 1);
    if( compressed < 0 )
        return size;

    int header;
    if( compression == RSCACHE_ARCHIVE_COMPRESSION_NONE )
    {
        header = 5;
        *out_uncompressed = compressed;
    }
    else
    {
        if( size < 9 )
            return size;
        header = 9;
        *out_uncompressed = (int)read_u32(data + 5);
    }

    long body = (long)header + compressed;
    if( body > size || body < 0 )
        return size;
    *out_trailer = size - (int)body;
    return (int)body;
}

/**
 * Rewrite one reference-table entry to match the bytes now stored, and write the
 * table back.
 *
 * Skipped when nothing changed: rewriting the table bumps its version, and a
 * version bump the contents do not justify is a difference for its own sake.
 */
/*
 * The table this import is about, created if the cache does not have one yet.
 *
 * `disk->tables[]` is populated from the .idxN files present when the cache was
 * opened, so on a cache built from the content tree alone (`pack` with no
 * --base) every entry is NULL. Both callers below used to read that as "no
 * table, nothing to do" and return success — which meant the archives really
 * were written to the .dat2 and .idxN, and *nothing* was written to idx255. A
 * dat2 archive is only reachable through its reference table, so the whole
 * asset half of a from-scratch pack was invisible to the client while the tool
 * reported success.
 *
 * The empty table matches what the library builds for the same situation on the
 * config side (dat2_edit_ensure_table in src/cache_edit.c): format 7, version 1,
 * sizes present. Keeping the two identical is what makes a from-scratch cache
 * one cache rather than two halves written by different rules.
 */
static struct RSCache_ReferenceTable*
cp_reference_ensure(struct CP_Ctx* ctx, int table_id)
{
    struct RSCache_ReferenceTable* rt;

    if( !ctx->cache.disk || !RSCache_Dat2DiskIsValidTableId(table_id) )
        return NULL;

    rt = ctx->cache.disk->tables[table_id];
    if( rt )
        return rt;

    rt = calloc(1, sizeof(*rt));
    if( !rt )
        return NULL;
    rt->format = 7;
    rt->version = 1;
    rt->flags = RSCACHE_REFTABLE_FLAG_SIZES;
    ctx->cache.disk->tables[table_id] = rt;
    return rt;
}

/*
 * Record an archive's NAME in the reference table, as a djb2 hash.
 *
 * Half the cache is addressed by name rather than id. The client resolves a
 * sprite with
 *
 *     name_hash = RSCache_ArchiveNameHashDat2(name);
 *     for( i ... ) if( table->archives[i].identifier == name_hash ) ...
 *
 * so an archive whose `identifier` is 0 is unreachable by name however
 * correctly its bytes were written. Packing onto a base cache hides this
 * because the identifiers came with the base; a cache packed from the tree
 * alone has none, and the client boots to a frame with no compass, no map
 * scene, no hitmarks — everything looked up by name silently absent.
 *
 * FLAG_IDENTIFIERS is set here rather than at table creation because it must
 * describe what the table actually carries: the encoder writes an identifier
 * for every archive once the flag is on, so claiming it on a table nothing
 * names would write a column of zeros and invite a spurious match on hash 0.
 */
int
cp_reference_set_name(
    struct CP_Ctx* ctx,
    int table_id,
    int archive_id,
    const char* name,
    int* out_dirty)
{
    struct RSCache_ReferenceTable* rt;
    int identifier;

    if( !name || !*name )
        return 1;

    rt = cp_reference_ensure(ctx, table_id);
    if( !rt )
        return 0;
    if( archive_id < 0 || archive_id >= rt->archive_count || rt->archives[archive_id].index < 0 )
        return 1; /* nothing was written for this id; nothing to name */

    identifier = RSCache_ArchiveNameHashDat2((char*)name);
    if( rt->archives[archive_id].identifier != identifier )
    {
        rt->archives[archive_id].identifier = identifier;
        if( out_dirty )
            *out_dirty = 1;
    }
    rt->flags |= RSCACHE_REFTABLE_FLAG_IDENTIFIERS;
    return 1;
}

int
cp_reference_sync(
    struct CP_Ctx* ctx,
    int table_id,
    int archive_id,
    const uint8_t* container,
    int container_size,
    const int* file_ids,
    int file_count,
    int* out_dirty)
{
    struct RSCache_ReferenceTable* rt = cp_reference_ensure(ctx, table_id);
    if( !rt )
        return 0;

    /*
     * `archives` is indexed *by archive id*, not packed by position — the decoder
     * writes `table->archives[ids[i]]` and leaves the gaps at `index == -1`. A
     * linear search for a matching `index` finds a different entry entirely, and
     * the symptom is a reference table rewritten on every import with a CRC
     * belonging to some other archive.
     */
    struct RSCache_ReferenceTableArchive* archive = NULL;
    if( archive_id >= 0 && archive_id < rt->archive_count && rt->archives[archive_id].index >= 0 )
        archive = &rt->archives[archive_id];
    if( !archive )
    {
        /*
         * A new archive: extend the table rather than write bytes nothing points at.
         *
         * This used to warn and return, and the warning was easy to read as
         * cosmetic — the payload really was written and the per-table count really
         * did go up. But a dat2 archive is only reachable through its reference
         * table, so an id the table does not list is invisible to the client. Adding
         * a model at the declared allocation base wrote 61,616 archives and shipped
         * 61,615.
         *
         * Two arrays have to grow together: `archives` is indexed *by archive id*
         * with `index == -1` in the gaps, and `ids` is the ascending list the
         * encoder walks. Only ids in `ids` are written.
         */
        if( archive_id < 0 )
            return 1;
        if( archive_id >= rt->archive_count )
        {
            int next = archive_id + 1;

            /* Double rather than fit exactly. Fitting exactly meant a realloc
             * per archive over an array that ends up tens of thousands long. */
            if( next > rt->archive_capacity )
            {
                int capacity = rt->archive_capacity > rt->archive_count ? rt->archive_capacity
                                                                        : rt->archive_count;
                struct RSCache_ReferenceTableArchive* grown;

                if( capacity < 16 )
                    capacity = 16;
                while( capacity < next )
                    capacity *= 2;

                grown = realloc(rt->archives, (size_t)capacity * sizeof(*grown));
                if( !grown )
                    return 0;
                rt->archives = grown;
                rt->archive_capacity = capacity;
            }

            memset(rt->archives + rt->archive_count, 0,
                   (size_t)(next - rt->archive_count) * sizeof(*rt->archives));
            for( int i = rt->archive_count; i < next; i++ )
                rt->archives[i].index = -1;
            rt->archive_count = next;
        }

        if( rt->id_count + 1 > rt->id_capacity )
        {
            int capacity = rt->id_capacity > rt->id_count ? rt->id_capacity : rt->id_count;
            int* grown;

            if( capacity < 16 )
                capacity = 16;
            while( capacity < rt->id_count + 1 )
                capacity *= 2;

            grown = realloc(rt->ids, (size_t)capacity * sizeof(int));
            if( !grown )
                return 0;
            rt->ids = grown;
            rt->id_capacity = capacity;
        }
        /* Ascending, because that is the order the encoder writes and the decoder
         * reads back as a delta chain. */
        int at = rt->id_count;
        while( at > 0 && rt->ids[at - 1] > archive_id )
        {
            rt->ids[at] = rt->ids[at - 1];
            at--;
        }
        rt->ids[at] = archive_id;
        rt->id_count++;

        archive = &rt->archives[archive_id];
        memset(archive, 0, sizeof(*archive));
        archive->index = archive_id;
        *out_dirty = 1;
        cp_warn(ctx, &ctx->warn_reference_added,
                "idx%d archive %d added to the reference table", table_id, archive_id);
    }

    int trailer = 0;
    int uncompressed = 0;
    int body = container_split(container, container_size, &trailer, &uncompressed);
    int crc = (int)RSCache_Crc32Buffer(container, (uint32_t)body);

    /*
     * CRC alone decides "unchanged". The size fields are only written when the
     * table carries them, and are not part of the test, because whether a
     * particular cache sets RSCACHE_REFTABLE_FLAG_SIZES is not something an
     * unchanged-archive check should depend on.
     *
     * A changed child list counts as changed even at an equal CRC, which cannot
     * happen from the same bytes but can from a caller that rebuilt the archive
     * with a different file set.
     */
    int children_differ = 0;
    if( file_ids && file_count > 0 )
    {
        children_differ = archive->children.count != file_count;
        for( int i = 0; !children_differ && i < file_count; i++ )
            children_differ = RSCache_ReferenceTableChildId(archive, i) != file_ids[i];
    }
    if( archive->crc == crc && !children_differ )
        return 1;

    if( children_differ )
    {
        struct RSCache_ReferenceTableArchiveFile* grown;
        int* grown_hashes = NULL;
        int old_count = archive->children.count;
        bool pooled = RSCache_ReferenceTableChildrenPooled(rt, archive->children.files);

        /* The name hash is the client's `getArchive(name)` key, so the hashes of
         * files the tree kept have to survive the regrow. They sit in an array
         * parallel to the ids now, present only on a table with identifiers. A
         * file the tree did not previously hold has no name to hash, so it gets
         * -1 — what the decoder reads for "unnamed" and what every id-addressed
         * table already carries. */
        if( (rt->flags & RSCACHE_REFTABLE_FLAG_IDENTIFIERS) != 0 )
        {
            const int* old_hashes = archive->children.name_hashes;

            grown_hashes = malloc((size_t)file_count * sizeof(int));
            if( !grown_hashes )
                return 0;
            for( int i = 0; i < file_count; i++ )
                grown_hashes[i] = (old_hashes && i < old_count) ? old_hashes[i] : -1;
        }

        if( pooled )
        {
            /* A decoded archive's children are a slice of the table's pool and
             * cannot be realloc'd; replace with an owned array and abandon the
             * slice. */
            grown = malloc((size_t)file_count * sizeof(*grown));
            if( !grown )
            {
                free(grown_hashes);
                return 0;
            }
        }
        else
        {
            grown = realloc(archive->children.files, (size_t)file_count * sizeof(*grown));
            if( !grown )
            {
                free(grown_hashes);
                return 0;
            }
            /* Read above into grown_hashes, so it is safe to drop now -- unless
             * it is a slice of the table's pool, which an identity run's is
             * even though its ids never were. */
            if( !RSCache_ReferenceTableChildNameHashesPooled(
                    rt, archive->children.name_hashes) )
                free(archive->children.name_hashes);
        }
        for( int i = 0; i < file_count; i++ )
            grown[i].id = file_ids[i];
        archive->children.files = grown;
        archive->children.name_hashes = grown_hashes;
        archive->children.count = file_count;
    }

    archive->crc = crc;
    if( rt->flags & RSCACHE_REFTABLE_FLAG_SIZES )
    {
        archive->compressed = body;
        if( uncompressed > 0 )
            archive->uncompressed = uncompressed;
    }
    if( rt->flags & RSCACHE_REFTABLE_FLAG_WHIRLPOOL )
    {
        RSCache_Whirlpool(
            container,
            (uint32_t)body,
            RSCache_ReferenceTableWhirlpoolSlot(rt, archive_id));
    }

    /*
     * Take the version from the archive's own trailer rather than incrementing.
     * The two have to agree — the client checks the stored version against the
     * table's — and the bytes being imported are the authority on what version
     * they are. Only when there is no trailer to read does this fall back to a
     * bump, which at least moves the entry off a value the new bytes contradict.
     */
    if( trailer >= 2 )
        archive->version = (container[body] << 8) | container[body + 1];
    else
        archive->version += 1;
    *out_dirty = 1;
    return 1;
}

int
cp_reference_write(
    struct CP_Ctx* ctx,
    const char* out_dir,
    int table_id)
{
    /* Ensure, not fetch: this is only reached when cp_reference_sync marked the
     * table dirty, and on a from-scratch cache that table was created by the
     * sync rather than loaded from disk. Returning "nothing to write" here was
     * the second half of the bug described above. */
    struct RSCache_ReferenceTable* rt = cp_reference_ensure(ctx, table_id);
    if( !rt )
        return 0;
    rt->version += 1;

    uint32_t bound = RSCache_ReferenceTableEncodeBound(rt);
    uint8_t* raw = malloc(bound ? bound : 1);
    if( !raw )
        return 0;
    uint32_t raw_size = RSCache_ReferenceTableEncode(rt, raw, bound);
    if( raw_size == 0 )
    {
        free(raw);
        fprintf(stderr, "cachepack: failed to encode the reference table for idx%d\n", table_id);
        return 0;
    }

    uint32_t cbound = RSCache_ArchiveEncodeBound(raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
    uint8_t* container = malloc(cbound ? cbound : 1);
    if( !container )
    {
        free(raw);
        return 0;
    }
    uint32_t container_size = RSCache_ArchiveEncode(
        container, cbound, raw, raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP, NULL);
    free(raw);
    if( container_size == 0 )
    {
        free(container);
        return 0;
    }

    int rc = RSCache_Dat2DiskWriteArchive(out_dir, RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID,
                                          table_id, container, (int)container_size);
    free(container);
    return rc == 0;
}

int
cp_binary_import(
    struct CP_Ctx* ctx,
    const char* out_cache_dir)
{
    char root[1200];
    snprintf(root, sizeof(root), "%s/binary", ctx->srcdir);
    struct stat st;
    if( stat(root, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        printf("No binary/ directory in the source tree; nothing to import.\n");
        return 1;
    }

    int total = 0;
    for( int table_id = 0; table_id < RSCACHE_DAT2_DISK_TABLE_CAPACITY; table_id++ )
    {
        char dir[1300];
        snprintf(dir, sizeof(dir), "%s/idx%d", root, table_id);
        if( stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) )
            continue;

        int written = 0;
        int dirty = 0;
        struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[table_id];
        int limit = rt ? rt->id_count : 0;

        /*
         * Walk the reference table's own id list rather than the directory, so an
         * archive id the table does not know about is reported instead of being
         * written where nothing will look for it.
         */
        for( int a = 0; a < limit; a++ )
        {
            int archive_id = rt->ids[a];
            char path[1500];
            snprintf(path, sizeof(path), "%s/%d.bin", dir, archive_id);
            FILE* in = fopen(path, "rb");
            if( !in )
                continue;
            fseek(in, 0, SEEK_END);
            long size = ftell(in);
            fseek(in, 0, SEEK_SET);
            if( size <= 0 )
            {
                fclose(in);
                continue;
            }
            uint8_t* data = malloc((size_t)size);
            if( !data || fread(data, 1, (size_t)size, in) != (size_t)size )
            {
                free(data);
                fclose(in);
                fprintf(stderr, "cachepack: failed to read %s\n", path);
                return 0;
            }
            fclose(in);

            if( RSCache_Dat2DiskWriteArchive(out_cache_dir, table_id, archive_id, data,
                                             (int)size) != 0 )
            {
                free(data);
                fprintf(stderr, "cachepack: failed to write idx%d archive %d\n", table_id,
                        archive_id);
                return 0;
            }
            cp_reference_sync(ctx, table_id, archive_id, data, (int)size, NULL, 0, &dirty);
            free(data);
            written++;
        }

        if( dirty && !cp_reference_write(ctx, out_cache_dir, table_id) )
            return 0;
        if( written )
            printf("  idx%-3d %6d archives%s\n", table_id, written,
                   dirty ? " (reference table updated)" : "");
        total += written;
    }

    printf("Imported %d binary archives.\n", total);
    return 1;
}
