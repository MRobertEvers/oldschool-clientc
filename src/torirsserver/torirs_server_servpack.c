#include "torirs_server_servpack.h"

#include "archive.h"
#include "checksum.h"
#include "dat2disk.h"

#include <stdlib.h>
#include <string.h>

/*
 * The archive header, restated from cachepack's `cp_fields.h`.
 *
 * These four constants are the framing both ends of the wire must agree on, and
 * they are restated rather than included for the reason `cp_fields.h` itself
 * gives: cachepack links nothing from `src/` so the tools stay usable apart
 * from the client, which cuts the other way too. The magic makes the pairing
 * hard to drift silently — a writer that changes it produces archives every
 * read here refuses on the first byte, loudly, rather than misreads.
 */
enum
{
    SERVPACK_HEADER = 8,
    SERVPACK_VERSION = 1,
    /** `kind` byte: an opcode band. 2 is a name table, which this reader has no
     *  caller for yet — refusing it keeps "wrong grammar" a loud failure. */
    SERVPACK_KIND_BAND = 1,
};

/** A dat2 idx entry is 3 bytes of length + 3 bytes of sector. */
enum
{
    SERVPACK_IDX_ENTRY = 6,
};

int
ToriRSServer_ServPackOpen(
    struct ToriRSServerServPack* pack,
    const char* content_dir)
{
    char path[640];

    memset(pack, 0, sizeof(*pack));
    snprintf(pack->dir, sizeof(pack->dir), "%s/server/pack", content_dir);
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", pack->dir);
    pack->dat2 = fopen(path, "rb");
    return pack->dat2 ? 0 : -1;
}

void
ToriRSServer_ServPackClose(struct ToriRSServerServPack* pack)
{
    if( pack->dat2 )
        fclose(pack->dat2);
    for( int i = 0; i < pack->idx_count; i++ )
    {
        if( pack->idx[i].file )
            fclose(pack->idx[i].file);
    }
    memset(pack, 0, sizeof(*pack));
}

static struct ToriRSServerServPackIdx*
idx_for_group(
    struct ToriRSServerServPack* pack,
    int group)
{
    char path[640];
    struct ToriRSServerServPackIdx* idx;
    long size;

    for( int i = 0; i < pack->idx_count; i++ )
    {
        if( pack->idx[i].group == group )
            return &pack->idx[i];
    }
    if( pack->idx_count >= (int)(sizeof(pack->idx) / sizeof(pack->idx[0])) )
        return NULL;

    /* A group with no idx file is recorded too (file NULL, 0 entries), so a
     * scan over an absent group asks the filesystem once, not per id. */
    idx = &pack->idx[pack->idx_count++];
    idx->group = group;
    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", pack->dir, group);
    idx->file = fopen(path, "rb");
    if( !idx->file )
        return idx;
    fseek(idx->file, 0, SEEK_END);
    size = ftell(idx->file);
    idx->entries = size > 0 ? (int)(size / SERVPACK_IDX_ENTRY) : 0;
    return idx;
}

int
ToriRSServer_ServPackEntryCount(
    struct ToriRSServerServPack* pack,
    int group)
{
    struct ToriRSServerServPackIdx* idx = idx_for_group(pack, group);

    return idx ? idx->entries : 0;
}

int
ToriRSServer_ServPackReadBand(
    struct ToriRSServerServPack* pack,
    int group,
    int id,
    uint8_t* out,
    int out_capacity)
{
    struct ToriRSServerServPackIdx* idx = idx_for_group(pack, group);
    struct RSCache_Dat2DiskIndexRecord record = { 0 };
    struct RSCache_Dat2DiskArchive archive = { 0 };
    const uint8_t* data;
    uint32_t stated;
    int size;

    if( !idx || !idx->file || id < 0 || id >= idx->entries )
        return TORIRSSERVER_SERVPACK_ABSENT;
    /* Nonzero is "no such archive", not corruption: the idx zero-fills the gap
     * entries between the ids that exist, and the read helper reports any
     * `sector <= 0` entry as -1 without touching `record` (dat2disk.c). A
     * *damaged* archive can only be told apart once there are bytes to hold to
     * the header, which is the check below. */
    if( RSCache_Dat2DiskIndexFileReadRecord(idx->file, id, &record) != 0 )
        return TORIRSSERVER_SERVPACK_ABSENT;
    if( record.sector <= 0 || record.length <= 0 )
        return TORIRSSERVER_SERVPACK_ABSENT;

    if( RSCache_Dat2DiskDat2FileReadArchive(
            pack->dat2, group, id, record.sector, record.length, &archive) != 0 )
        return TORIRSSERVER_SERVPACK_INVALID;
    /* The container framing: compression byte + length + payload. The bands are
     * stored uncompressed, but the framing is validated the same either way. */
    if( !RSCache_ArchiveDecryptDecompress(&archive, NULL) )
    {
        free(archive.data);
        return TORIRSSERVER_SERVPACK_INVALID;
    }

    data = (const uint8_t*)archive.data;
    size = archive.data_size;
    if( size < SERVPACK_HEADER || data[0] != 'S' || data[1] != 'P' )
        goto invalid;
    /* Exact, not `>=`: a version this build has never seen may have changed the
     *  band grammar itself, and decoding it as if it had not is the silent
     *  failure the version byte exists to prevent. */
    if( data[2] != SERVPACK_VERSION || data[3] != SERVPACK_KIND_BAND )
        goto invalid;
    stated = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) |
             (uint32_t)data[7];
    if( stated !=
        RSCache_Crc32Buffer(data + SERVPACK_HEADER, (size_t)(size - SERVPACK_HEADER)) )
        goto invalid;

    size -= SERVPACK_HEADER;
    if( size > out_capacity )
        goto invalid;
    memcpy(out, data + SERVPACK_HEADER, (size_t)size);
    free(archive.data);
    return size;

invalid:
    free(archive.data);
    return TORIRSSERVER_SERVPACK_INVALID;
}
