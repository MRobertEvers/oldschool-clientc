#include "mock_js5.h"

#include <checksum.h>
#include <dat2disk.h>
#include <reference_table.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * dat2 on-disk layout, which this file reads directly. See mock_js5.h for why
 * it does not go through rscache.
 *
 *   main_file_cache.idx<N>   6 bytes per group: p3 length, p3 first sector.
 *                            An all-zero record means "no such group".
 *   main_file_cache.dat2     520-byte sectors. Each carries an 8- or 10-byte
 *                            header and then payload:
 *
 *      small header (group id < 65536)   p2 group, p2 chunk, p3 next, p1 index
 *      large header (group id >= 65536)  p4 group, p2 chunk, p3 next, p1 index
 *
 * The header fields are a self-check, not decoration: every sector states which
 * group and which chunk it is, so a walk that drifts is caught on the next
 * sector instead of returning a plausible mixture of two archives. This reader
 * checks all four.
 */

#define JS5_SECTOR_BYTES 520
#define JS5_IDX_RECORD_BYTES 6
#define JS5_BLOCK_LENGTH 512
#define JS5_REFERENCE_TABLE_INDEX 255
#define JS5_MAX_INDEX 255

/*
 * The master index (255/255) is the one JS5 response no cache stores: the
 * server computes it from the reference tables. Its layout was measured off a
 * live revision-239 server with tools/js5_probe.py rather than assumed --
 * `oldschool1.runescape.com` answers with an UNCOMPRESSED container whose body
 * is exactly
 *
 *      for archive 0 .. count-1:  p4 crc, p4 version
 *
 * with no format byte and no count prefix; the client infers the count from the
 * length. Archives that do not exist occupy their slot as a (0, 0) pair, so the
 * count is "highest index present + 1", not "number of indices present" -- the
 * live cache has holes at 16 and 23 and still sends 25 entries.
 *
 * The CRC is over the reference table's raw CONTAINER bytes (compression byte,
 * lengths and payload as they sit on disk, version trailer excluded), not over
 * the decompressed table. The version is the `version` field inside the decoded
 * table, which is why building this needs a decompress even though serving
 * every other group does not.
 */
#define JS5_MASTER_ENTRY_BYTES 8

struct MockJs5
{
    FILE* dat2;
    FILE* idx[JS5_MAX_INDEX + 1];
    int max_group_bytes;

    /* Built once at open: the master index container, ready to serve. */
    uint8_t* master;
    int master_len;
};

static int
read_u24(uint8_t const* p)
{
    return (p[0] << 16) | (p[1] << 8) | p[2];
}

static int
read_group_raw(struct MockJs5* js5, int index, int group, uint8_t* raw, int cap);
static int
strip_version_trailer(uint8_t const* raw, int len);
static void
build_master_index(struct MockJs5* js5, char const* cache_dir);

struct MockJs5*
mock_js5_open(char const* cache_dir)
{
    char path[1024];
    struct MockJs5* js5 = calloc(1, sizeof(*js5));
    if( !js5 )
        return NULL;

    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", cache_dir);
    js5->dat2 = fopen(path, "rb");
    if( !js5->dat2 )
    {
        fprintf(stderr, "js5: no %s\n", path);
        free(js5);
        return NULL;
    }

    int opened = 0;
    long biggest = 0;
    for( int i = 0; i <= JS5_MAX_INDEX; i++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", cache_dir, i);
        js5->idx[i] = fopen(path, "rb");
        if( !js5->idx[i] )
            continue;
        opened++;
        /* The longest group in the whole cache bounds every response, so the
         * caller can size one buffer instead of guessing per request. */
        fseek(js5->idx[i], 0, SEEK_END);
        long records = ftell(js5->idx[i]) / JS5_IDX_RECORD_BYTES;
        for( long g = 0; g < records; g++ )
        {
            uint8_t rec[JS5_IDX_RECORD_BYTES];
            fseek(js5->idx[i], g * JS5_IDX_RECORD_BYTES, SEEK_SET);
            if( fread(rec, 1, sizeof(rec), js5->idx[i]) != sizeof(rec) )
                break;
            long len = read_u24(rec);
            /* A 3-byte length maxes at 16 MB, and a cache with a torn or
             * never-written idx record reads back as exactly that. Sizing the
             * response buffer off such a record turns one bad record into a
             * 16 MB allocation per session, so the scan ignores anything the
             * dat2 file could not possibly contain. */
            if( len > biggest && (long)len * JS5_SECTOR_BYTES > 0 )
            {
                fseek(js5->dat2, 0, SEEK_END);
                if( len <= ftell(js5->dat2) )
                    biggest = len;
            }
        }
    }
    if( !opened )
    {
        fprintf(stderr, "js5: %s has no main_file_cache.idx* files\n", cache_dir);
        mock_js5_close(js5);
        return NULL;
    }

    /* Response = 3-byte header + the group minus its 2-byte version trailer,
     * plus one 0xFF marker per 511 bytes after the first block. */
    long body = biggest;
    js5->max_group_bytes = (int)(3 + body + (body / (JS5_BLOCK_LENGTH - 1)) + 16);
    fprintf(stderr, "js5: %s, %d indices, largest group %ld bytes\n", cache_dir, opened,
            biggest);
    build_master_index(js5, cache_dir);
    return js5;
}

/*
 * Build the master index from the reference tables.
 *
 * Two different reads of the same archive, and they are not interchangeable:
 * the CRC is over the raw container as stored (read_group_raw), while the
 * version lives inside the decoded table, so this loads it twice by design.
 */
static void
build_master_index(struct MockJs5* js5, char const* cache_dir)
{
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "js5: cannot open %s for the master index\n", cache_dir);
        return;
    }

    int32_t crc[JS5_MAX_INDEX];
    int32_t version[JS5_MAX_INDEX];
    memset(crc, 0, sizeof(crc));
    memset(version, 0, sizeof(version));
    int highest = -1;

    uint8_t* raw = malloc((size_t)js5->max_group_bytes);
    if( !raw )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }

    for( int i = 0; i < JS5_MAX_INDEX; i++ )
    {
        int len = read_group_raw(js5, JS5_REFERENCE_TABLE_INDEX, i, raw,
                                 js5->max_group_bytes);
        if( len <= 0 )
            continue;
        len = strip_version_trailer(raw, len);
        crc[i] = (int32_t)RSCache_Crc32Buffer(raw, (size_t)len);

        struct RSCache_Dat2DiskArchive* table =
            RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, i);
        if( table )
        {
            struct RSCache_ReferenceTable* decoded =
                RSCache_ReferenceTableNewDecode(table->data, table->data_size);
            if( decoded )
            {
                version[i] = decoded->version;
                RSCache_ReferenceTableFree(decoded);
            }
            RSCache_Dat2DiskArchiveFree(table);
        }
        highest = i;
    }
    free(raw);
    RSCache_Dat2DiskFree(disk);

    if( highest < 0 )
    {
        fprintf(stderr, "js5: no reference tables; the master index will be empty\n");
        return;
    }

    /* Absent archives keep their slot as a (0, 0) pair -- the count is the
     * highest index present plus one, not the number present. */
    int count = highest + 1;
    int body = count * JS5_MASTER_ENTRY_BYTES;
    js5->master_len = 5 + body;
    js5->master = malloc((size_t)js5->master_len);
    if( !js5->master )
    {
        js5->master_len = 0;
        return;
    }

    uint8_t* p = js5->master;
    *p++ = 0; /* compression: none, as the live server sends it */
    *p++ = (uint8_t)(body >> 24);
    *p++ = (uint8_t)(body >> 16);
    *p++ = (uint8_t)(body >> 8);
    *p++ = (uint8_t)body;
    for( int i = 0; i < count; i++ )
    {
        *p++ = (uint8_t)(crc[i] >> 24);
        *p++ = (uint8_t)(crc[i] >> 16);
        *p++ = (uint8_t)(crc[i] >> 8);
        *p++ = (uint8_t)crc[i];
        *p++ = (uint8_t)(version[i] >> 24);
        *p++ = (uint8_t)(version[i] >> 16);
        *p++ = (uint8_t)(version[i] >> 8);
        *p++ = (uint8_t)version[i];
    }
    fprintf(stderr, "js5: master index built, %d archives (%d bytes)\n", count,
            js5->master_len);
}

void
mock_js5_close(struct MockJs5* js5)
{
    if( !js5 )
        return;
    if( js5->dat2 )
        fclose(js5->dat2);
    for( int i = 0; i <= JS5_MAX_INDEX; i++ )
        if( js5->idx[i] )
            fclose(js5->idx[i]);
    free(js5->master);
    free(js5);
}

int
mock_js5_max_response_bytes(struct MockJs5* js5)
{
    return js5 ? js5->max_group_bytes : 0;
}

/**
 * Read a group's raw bytes by walking its sector chain.
 *
 * Returns the byte count, or -1 if the group is absent or the chain fails a
 * self-check. `raw` must hold at least `cap` bytes.
 */
static int
read_group_raw(struct MockJs5* js5, int index, int group, uint8_t* raw, int cap)
{
    if( index < 0 || index > JS5_MAX_INDEX || !js5->idx[index] || group < 0 )
        return -1;

    uint8_t rec[JS5_IDX_RECORD_BYTES];
    if( fseek(js5->idx[index], (long)group * JS5_IDX_RECORD_BYTES, SEEK_SET) != 0 )
        return -1;
    if( fread(rec, 1, sizeof(rec), js5->idx[index]) != sizeof(rec) )
        return -1;

    int length = read_u24(rec);
    int sector = read_u24(rec + 3);
    if( length <= 0 || sector <= 0 || length > cap )
        return -1;

    /* Group ids at or above 65536 need the wide sector header. The threshold is
     * the group id, not the index: idx2 (configs) stays small while idx5 (maps)
     * does not, and a reader that picks the header shape from the index is
     * wrong on exactly the archives that matter most. */
    int const wide = group > 0xffff;
    int const header = wide ? 10 : 8;
    int written = 0;
    int chunk = 0;

    while( written < length )
    {
        if( sector <= 0 )
            return -1;

        uint8_t buf[JS5_SECTOR_BYTES];
        if( fseek(js5->dat2, (long)sector * JS5_SECTOR_BYTES, SEEK_SET) != 0 )
            return -1;
        size_t got = fread(buf, 1, sizeof(buf), js5->dat2);
        if( got < (size_t)header )
            return -1;

        int sec_group = wide ? ((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3])
                             : ((buf[0] << 8) | buf[1]);
        int sec_chunk = (buf[header - 6] << 8) | buf[header - 5];
        int sec_next = read_u24(buf + header - 4);
        int sec_index = buf[header - 1];

        if( sec_group != group || sec_chunk != chunk || sec_index != index )
        {
            fprintf(stderr,
                    "js5: sector chain mismatch at index %d group %d chunk %d "
                    "(sector says group %d chunk %d index %d)\n",
                    index, group, chunk, sec_group, sec_chunk, sec_index);
            return -1;
        }

        int payload = JS5_SECTOR_BYTES - header;
        int remaining = length - written;
        if( payload > remaining )
            payload = remaining;
        if( got < (size_t)(header + payload) )
            return -1;
        memcpy(raw + written, buf + header, (size_t)payload);
        written += payload;
        sector = sec_next;
        chunk++;
    }
    return written;
}

/**
 * Strip the version trailer.
 *
 * An archive on disk is `p1 compression, p4 compressedLength, <payload>` and
 * then, when the reference table says the group is versioned, a 2-byte version.
 * JS5 must not send that version, and the client asserts on the length, so this
 * derives the true end from the header rather than trusting the idx length:
 * a group with no version has no trailer, and cutting two bytes off it would
 * truncate the payload.
 */
static int
strip_version_trailer(uint8_t const* raw, int len)
{
    if( len < 5 )
        return len;
    int compression = raw[0];
    int compressed = (raw[1] << 24) | (raw[2] << 16) | (raw[3] << 8) | raw[4];
    if( compressed < 0 )
        return len;
    /* Uncompressed (0) carries no decompressed-length field; the others carry
     * a 4-byte one ahead of the payload. */
    int body = 5 + compressed + (compression == 0 ? 0 : 4);
    if( body > 0 && body <= len )
        return body;
    return len;
}

int
mock_js5_build_response(
    struct MockJs5* js5,
    int archive,
    int group,
    uint8_t* out,
    int out_cap)
{
    if( !js5 )
        return 0;

    uint8_t* raw = NULL;
    uint8_t const* body;
    int len;

    if( archive == JS5_REFERENCE_TABLE_INDEX && group == JS5_REFERENCE_TABLE_INDEX )
    {
        /* The master index is computed at open, not stored. */
        if( !js5->master )
            return 0;
        body = js5->master;
        len = js5->master_len;
    }
    else
    {
        /* Archive 255 is not a real index: group N of it is archive N's
         * reference table. Everything else is (index, group) as given. */
        raw = malloc((size_t)js5->max_group_bytes);
        if( !raw )
            return 0;
        len = read_group_raw(js5, archive, group, raw, js5->max_group_bytes);
        if( len < 0 )
        {
            free(raw);
            return 0;
        }
        len = strip_version_trailer(raw, len);
        body = raw;
    }

    /* Chunk into 512-byte blocks separated by 0xFF, header included in the
     * first block's budget. */
    int pos = 0;
    if( out_cap < 3 )
    {
        free(raw);
        return -1;
    }
    out[pos++] = (uint8_t)archive;
    out[pos++] = (uint8_t)(group >> 8);
    out[pos++] = (uint8_t)group;

    int first = len < JS5_BLOCK_LENGTH - 3 ? len : JS5_BLOCK_LENGTH - 3;
    if( pos + first > out_cap )
    {
        free(raw);
        return -1;
    }
    memcpy(out + pos, body, (size_t)first);
    pos += first;

    int off = first;
    while( off < len )
    {
        int take = len - off;
        if( take > JS5_BLOCK_LENGTH - 1 )
            take = JS5_BLOCK_LENGTH - 1;
        if( pos + 1 + take > out_cap )
        {
            free(raw);
            return -1;
        }
        out[pos++] = 0xff;
        memcpy(out + pos, body + off, (size_t)take);
        pos += take;
        off += take;
    }

    free(raw);
    return pos;
}
