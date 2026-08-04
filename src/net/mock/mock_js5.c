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
            if( len > biggest )
                biggest = len;
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
    return js5;
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

    /* Archive 255 is not a real index: group N of it is archive N's reference
     * table, and group 255 is the master index over all of them. Both live in
     * idx255, so the lookup is the same read with a different index number. */
    int index = (archive == JS5_REFERENCE_TABLE_INDEX) ? JS5_REFERENCE_TABLE_INDEX
                                                       : archive;
    int lookup_group = group;
    if( archive == JS5_REFERENCE_TABLE_INDEX )
        index = JS5_REFERENCE_TABLE_INDEX;

    uint8_t* raw = malloc((size_t)js5->max_group_bytes);
    if( !raw )
        return 0;

    int len;
    if( archive == JS5_REFERENCE_TABLE_INDEX && group == JS5_REFERENCE_TABLE_INDEX )
    {
        /*
         * The master index is not stored: it is built from every reference
         * table's version and CRC. Serving it means reading idx255 and
         * summarising it, which the cache tools already do -- so rather than
         * duplicating the CRC computation here and risking a second opinion,
         * this reads the group the packer wrote if it exists and reports the
         * gap loudly if it does not.
         */
        len = read_group_raw(js5, JS5_REFERENCE_TABLE_INDEX, JS5_REFERENCE_TABLE_INDEX,
                             raw, js5->max_group_bytes);
        if( len < 0 )
        {
            fprintf(stderr,
                    "js5: no stored master index (255/255). A client cannot start "
                    "without it; build one with cachepack.\n");
            free(raw);
            return 0;
        }
    }
    else
    {
        len = read_group_raw(js5, index, lookup_group, raw, js5->max_group_bytes);
    }

    if( len < 0 )
    {
        free(raw);
        return 0;
    }
    len = strip_version_trailer(raw, len);

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
    memcpy(out + pos, raw, (size_t)first);
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
        memcpy(out + pos, raw + off, (size_t)take);
        pos += take;
        off += take;
    }

    free(raw);
    return pos;
}
