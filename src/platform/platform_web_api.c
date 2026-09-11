/*
 * The platform's C API for the browser's JavaScript IO executor.
 *
 * The executor (platform/platform_web_io.js) does the IO: it awaits IndexedDB
 * and fetch with the language's own async facilities, because that is what a
 * browser is good at and what the queue's outstanding-item contract already
 * accommodates. What it does NOT do is re-implement the cache format.
 *
 * DECODE IS NOT IO. Container framing, bzip2, gzip, XTEA and the reference
 * table are the cache's format, and the format has exactly one implementation
 * in this tree -- 3rd/rscache, the same code the desktop, the servers and the
 * packers all read caches with. Porting it to JavaScript for one platform would
 * make two, and the second would be wrong in ways nothing else could catch: a
 * bad decode does not throw, it yields a plausible archive that surfaces as a
 * wrong model somewhere far away.
 *
 * So this file is the seam. JS supplies bytes it fetched and gets back the
 * pointer the queue expects, with every format decision made by rscache.
 *
 * ## Ownership
 *
 * Everything returned here is a plain malloc'd object the CLIENT owns, exactly
 * as if platform_x_io.c had produced it: the queue hands it to the task that
 * asked, and that task frees it. JS never frees one of these -- it stores the
 * pointer in the item's `data` field and forgets it, which is the whole of its
 * responsibility.
 */

#include <rscache.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#if defined(__EMSCRIPTEN__)

/**
 * Decode one raw dat2 container into the archive the queue expects.
 *
 * `container`/`size` are the exact bytes an idx record addresses -- the
 * compression byte, the lengths, and the payload -- which is precisely what
 * the record store holds and what the boot route serves. `xtea_key` is four
 * uint32 for a keyed map archive, or NULL; the caller decides, because whether
 * a table is encrypted is a property of the cache profile and not of these
 * bytes.
 *
 * Returns NULL when the container does not decode. That is a read failure and
 * not a crash: a hand-patched cache, a truncated download and a wrong key all
 * land here, and the client recovers from each by failing the one item.
 */
EMSCRIPTEN_KEEPALIVE struct RSCache_Dat2DiskArchive*
ToriRS_WebApi_ArchiveDecode(
    uint8_t* container,
    int size,
    int table_id,
    int archive_id,
    uint32_t* xtea_key)
{
    struct RSCache_Dat2DiskArchive* archive;

    if( !container || size <= 0 )
        return NULL;

    archive = malloc(sizeof(*archive));
    assert(archive);
    memset(archive, 0, sizeof(*archive));

    /*
     * The decode works in place and replaces `data` with the decompressed
     * buffer, so it is handed a copy it may own. Borrowing the JS-side buffer
     * would hand rscache a pointer it is entitled to free.
     */
    archive->data = malloc((size_t)size);
    assert(archive->data);
    memcpy(archive->data, container, (size_t)size);
    archive->data_size = size;
    archive->table_id = table_id;
    archive->archive_id = archive_id;

    if( !RSCache_ArchiveDecryptDecompress(archive, xtea_key) )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    return archive;
}

/**
 * The disk table id this cache's branch keeps a logical table at.
 *
 * The client addresses tables by ROLE (`enum RSCache_Dat2Table`) and the cache
 * addresses them by idx number, and the two agree only for 0..15 — OldSchool
 * ships client defaults at 17 where the role's own ordinal is 31, and its
 * world map tables sit where RS2 keeps npc/obj/seq. The desktop executor
 * resolves this against the open disk before it reads anything
 * (platform_x_io.c, dat2_resolve_table); a browser has no disk, so it asks
 * here and gets the same answer from the same table.
 *
 * `game` is what the boot manifest named, handed over by InitCacheId.
 * Returns RSCACHE_DAT2_DISK_TABLE_ABSENT (-1) for a role this branch has no
 * table for, which is a read that must not be attempted rather than an error.
 */
EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_Dat2TableDiskId(
    int game,
    int logical_table)
{
    return RSCache_Dat2DiskTableForGame(game, (enum RSCache_Dat2Table)logical_table);
}

/**
 * Attach the reference table's metadata (file count and ids) to a decoded
 * archive.
 *
 * Separate from the decode because it needs a DIFFERENT container -- the
 * table's -- which the executor may still be fetching when the group arrives.
 * Splitting it lets JS order those two reads however it likes.
 *
 * An archive with no entry in the table is left as it is rather than reported:
 * a hand-patched cache has idx records the table never described, and the
 * client already treats that as a missing archive rather than a fatal one.
 */
EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_ArchiveApplyMetadata(
    struct RSCache_Dat2DiskArchive* archive,
    struct RSCache_ReferenceTable* table)
{
    if( !archive || !table )
        return 0;
    return RSCache_Dat2DiskArchiveInitMetadataFromTable(table, archive) ? 1 : 0;
}

/**
 * Decode a reference table straight out of its raw container.
 *
 * Both steps in one call on purpose. A reference table arrives as an ordinary
 * container and has to be decompressed exactly like any other, but the
 * intermediate archive is rscache's business: exposing it would mean the
 * executor reading `data` and `data_size` out of a struct, and then the archive
 * layout would be part of this seam for no reason other than to hand it
 * straight back.
 */
EMSCRIPTEN_KEEPALIVE struct RSCache_ReferenceTable*
ToriRS_WebApi_ReferenceTableFromContainer(
    uint8_t* container,
    int size,
    int table_id)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_ReferenceTable* table;

    /* Never keyed: the master index and the reference tables are plain, and a
     * key applied to plain data corrupts rather than fails. */
    archive = ToriRS_WebApi_ArchiveDecode(container, size, table_id, 255, NULL);
    if( !archive )
        return NULL;

    table = RSCache_ReferenceTableNewDecode(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    return table;
}

/*
 * Sizes the queue reports in `data_size` for these two kinds.
 *
 * The client's readers cast `data` and never walk it by length, so these are
 * what platform_x_io.c has always written there rather than a measurement JS
 * could take. Asked of C rather than hard-coded in JS for the reason every
 * other offset in this seam is: a struct that gains a field must not need a
 * second edit somewhere that cannot see it.
 */
EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_ArchiveStructSize(void)
{
    return (int)sizeof(struct RSCache_Dat2DiskArchive);
}

EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_ReferenceTableStructSize(void)
{
    return (int)sizeof(struct RSCache_ReferenceTable);
}

/**
 * Decode one raw dat1 container into the archive the queue expects.
 *
 * The dat1 counterpart of ToriRS_WebApi_ArchiveDecode, and a separate entry
 * point rather than a branch inside it because the two produce DIFFERENT
 * TYPES: a dat2 read fills the item with a Dat2DiskArchive and a dat1 read
 * with a Dat1DiskArchive, and the item's data_size says which. One function
 * returning "whichever" would leave the caller to guess the size, which is
 * exactly the thing the executor must not do.
 *
 * `container`/`size` are the bytes the server serves for (table, archive):
 * the jag file for the config table, and the STORED -- still compressed --
 * file for every other. The format is derived here rather than sent, because
 * it is a function of the table and a wire field would be a second statement
 * of the same fact, free to disagree with the first.
 *
 * Returns NULL when the container does not decode. A read failure, not a
 * crash: a truncated download and a mis-addressed archive both land here, and
 * the client recovers by failing the one item.
 */
EMSCRIPTEN_KEEPALIVE struct RSCache_Dat1DiskArchive*
ToriRS_WebApi_Dat1ArchiveDecode(
    uint8_t* container,
    int size,
    int table_id,
    int archive_id)
{
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_Dat2DiskArchive raw = { 0 };
    enum RSCache_ArchiveFormat format;

    if( !container || size <= 0 )
        return NULL;

    format = table_id == RSCACHE_DAT1_DISK_TABLE_CONFIGS ? RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE
                                                         : RSCACHE_ARCHIVE_FORMAT_DAT;

    /* Decompression works in place and replaces `data`, so it is handed a copy
     * it may own -- borrowing the JS-side buffer would hand rscache a pointer
     * it is entitled to free. */
    raw.data = malloc((size_t)size);
    assert(raw.data);
    memcpy(raw.data, container, (size_t)size);
    raw.data_size = size;
    raw.table_id = table_id;
    raw.archive_id = archive_id;

    /* A jag archive is not compressed at this layer -- its files are, and the
     * jag reader above unpacks them. Everything else arrives gzipped. */
    if( format != RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE &&
        !RSCache_ArchiveDecompressDat(&raw, format) )
    {
        free(raw.data);
        return NULL;
    }

    archive = malloc(sizeof(*archive));
    assert(archive);
    memset(archive, 0, sizeof(*archive));
    archive->data = raw.data;
    archive->data_size = raw.data_size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;
    archive->format = format;
    return archive;
}

EMSCRIPTEN_KEEPALIVE int
ToriRS_WebApi_Dat1ArchiveStructSize(void)
{
    return (int)sizeof(struct RSCache_Dat1DiskArchive);
}

/**
 * Free a decoded archive.
 *
 * For the executor's own error paths only -- an archive that reached the queue
 * belongs to the task that asked for it. This exists because JS can produce an
 * archive and then fail to place it (a heap that could not grow), and dropping
 * the pointer would leak the decompressed group.
 */
EMSCRIPTEN_KEEPALIVE void
ToriRS_WebApi_ArchiveFree(struct RSCache_Dat2DiskArchive* archive)
{
    RSCache_Dat2DiskArchiveFree(archive);
}

#endif /* __EMSCRIPTEN__ */
