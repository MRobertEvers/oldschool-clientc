#include "platform_x_io_ondemand.h"

#include "sockstream.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#endif

#define OD_DEFAULT_GAME_PORT 43594
#define OD_DEFAULT_WEB_PORT 80
#define OD_CONNECT_TIMEOUT_SEC 10
#define OD_READ_TIMEOUT_SEC 30
#define OD_HOST_MAX 128

/* The on-demand payload cap the server slices to (OnDemandThread.ts). Not a
 * negotiated value: the header carries the part index, not the part size, so
 * both ends have to agree on it by construction. */
#define OD_CHUNK_PAYLOAD 500
/* `length` in the chunk header is two bytes, so no dat1 archive can exceed
 * this -- a server that packed a bigger one could not describe it. */
#define OD_ARCHIVE_MAX 0xFFFF
/* Highest of the server's three request queues (0..2), which is the one it
 * drains first. Every read here is blocking and therefore urgent by
 * definition: nothing else is in flight to be preferred over. */
#define OD_PRIORITY_URGENT 2

/* idx0 file id -> the web route serving it. Index 0 is unused; the config
 * table's archive ids start at 1 (RSCache_Dat1ConfigKind). */
static const char* const g_jag_routes[] = {
    NULL, "/title", "/config", "/interface", "/media", "/versionlist", "/textures", "/wordenc",
    "/sounds",
};
#define OD_JAG_ROUTE_COUNT ((int)(sizeof(g_jag_routes) / sizeof(g_jag_routes[0])))

struct PlatformXIOOnDemand
{
    char host[OD_HOST_MAX];
    int game_port;
    int web_port;

    /* The file pipe. Opened once and kept: the handshake costs a round trip,
     * and the server keys its request queues on the connection. */
    struct SockStream* files;

    struct RSCache_MapSquares* map_squares;
};

/* ------------------------------------------------------------------ socket */

static int
od_wait_readable(
    struct SockStream* stream,
    int timeout_sec)
{
    struct timeval tv;
    fd_set readable;
    intptr_t fd = sockstream_get_fd(stream);
    int ready;

    if( fd < 0 )
        return 0;

    FD_ZERO(&readable);
#ifdef _WIN32
    FD_SET((SOCKET)fd, &readable);
#else
    if( fd >= FD_SETSIZE )
        return 0;
    FD_SET((int)fd, &readable);
#endif
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    ready = select((int)fd + 1, &readable, NULL, NULL, &tv);
    return ready > 0;
}

/**
 * Fill `size` bytes or fail.
 *
 * sockstream is non-blocking, so NODATA is "not yet" and everything else is
 * "never" -- including CLOSED, which is the one this must not spin on. A
 * socket the server hung up on stays readable forever under select(), so
 * treating a close as a retry would busy-loop until the timeout instead of
 * reporting the disconnect.
 */
static int
od_read_exact(
    struct SockStream* stream,
    void* buffer,
    int size)
{
    int filled = 0;

    while( filled < size )
    {
        int got = sockstream_recv(stream, (char*)buffer + filled, size - filled);
        if( got > 0 )
        {
            filled += got;
            continue;
        }
        if( got != SOCKSTREAM_ERROR_NODATA && got != SOCKSTREAM_ERROR_WOULDBLOCK )
            return -1;
        if( !od_wait_readable(stream, OD_READ_TIMEOUT_SEC) )
            return -1;
    }
    return 0;
}

static int
od_write_all(
    struct SockStream* stream,
    const void* buffer,
    int size)
{
    int written = 0;

    while( written < size )
    {
        int sent = sockstream_send(stream, (const char*)buffer + written, size - written);
        if( sent > 0 )
        {
            written += sent;
            continue;
        }
        if( sent != SOCKSTREAM_ERROR_NODATA && sent != SOCKSTREAM_ERROR_WOULDBLOCK )
            return -1;
    }
    return 0;
}

static struct SockStream*
od_dial(
    const char* host,
    int port)
{
    struct SockStream* stream = sockstream_new();

    if( !stream )
        return NULL;

    sockstream_connect(stream, host, port, OD_CONNECT_TIMEOUT_SEC);
    for( ;; )
    {
        int state = sockstream_poll_connect(stream);
        if( state == SOCKSTREAM_CONNECT_SUCCESS )
            return stream;
        if( state == SOCKSTREAM_CONNECT_FAILED )
            break;
    }

    sockstream_close(stream);
    sockstream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------------- http */

/**
 * Read a whole response body.
 *
 * The request goes out as HTTP/1.0, which is what makes this small: the server
 * answers it with `Connection: close` and an unframed body, so "the body" is
 * "everything until the socket closes". Content-Length and chunked are still
 * honoured when present rather than assumed absent -- a response that framed
 * itself and was read to EOF anyway would silently gain the framing bytes.
 */
static char*
od_http_get(
    struct PlatformXIOOnDemand* od,
    const char* route,
    int* out_size)
{
    struct SockStream* stream = NULL;
    char request[512];
    char* buffer = NULL;
    int capacity = 0;
    int length = 0;
    char* body = NULL;
    char* header_end = NULL;
    int header_size = 0;
    int body_size = 0;
    int content_length = -1;
    int chunked = 0;
    char* result = NULL;

    assert(od);
    assert(route);
    assert(out_size);

    stream = od_dial(od->host, od->web_port);
    if( !stream )
        return NULL;

    snprintf(
        request,
        sizeof(request),
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: torirs\r\nAccept: */*\r\n\r\n",
        route,
        od->host,
        od->web_port);
    if( od_write_all(stream, request, (int)strlen(request)) != 0 )
        goto done;

    for( ;; )
    {
        int got;
        if( length + 4096 > capacity )
        {
            int grown = capacity ? capacity * 2 : 65536;
            char* bigger;
            while( grown < length + 4096 )
                grown *= 2;
            bigger = realloc(buffer, (size_t)grown);
            assert(bigger);
            buffer = bigger;
            capacity = grown;
        }
        got = sockstream_recv(stream, buffer + length, capacity - length);
        if( got > 0 )
        {
            length += got;
            continue;
        }
        if( got == SOCKSTREAM_ERROR_CLOSED )
            break;
        if( got != SOCKSTREAM_ERROR_NODATA && got != SOCKSTREAM_ERROR_WOULDBLOCK )
            goto done;
        if( !od_wait_readable(stream, OD_READ_TIMEOUT_SEC) )
            goto done;
    }

    if( length < 12 || memcmp(buffer, "HTTP/1.", 7) != 0 )
        goto done;
    if( memcmp(buffer + 9, "200", 3) != 0 )
    {
        fprintf(
            stderr,
            "ondemand: %s%s answered %.3s, not 200\n",
            od->host,
            route,
            buffer + 9);
        goto done;
    }

    /* memmem is not portable; the header block is small enough to scan. */
    for( int i = 0; i + 4 <= length; i++ )
    {
        if( memcmp(buffer + i, "\r\n\r\n", 4) == 0 )
        {
            header_end = buffer + i;
            header_size = i + 4;
            break;
        }
    }
    if( !header_end )
        goto done;

    body = buffer + header_size;
    body_size = length - header_size;

    {
        /* Header names are case-insensitive on the wire. Lowercase a copy of
         * the header block rather than the whole response -- the body is
         * binary and must not be touched. */
        char* headers = malloc((size_t)header_size + 1);
        char* found;
        assert(headers);
        for( int i = 0; i < header_size; i++ )
        {
            char c = buffer[i];
            headers[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        headers[header_size] = '\0';

        found = strstr(headers, "\r\ncontent-length:");
        if( found )
            content_length = atoi(found + strlen("\r\ncontent-length:"));
        found = strstr(headers, "\r\ntransfer-encoding:");
        if( found && strstr(found, "chunked") )
            chunked = 1;
        free(headers);
    }

    if( chunked )
    {
        /* Decode in place: a chunk's data always starts further into the
         * buffer than the byte it moves to, so the write cursor can never
         * overtake the read cursor. */
        char* read_at = body;
        char* end = body + body_size;
        int decoded = 0;
        while( read_at < end )
        {
            long chunk_size = strtol(read_at, NULL, 16);
            char* newline = memchr(read_at, '\n', (size_t)(end - read_at));
            if( !newline )
                goto done;
            read_at = newline + 1;
            if( chunk_size <= 0 )
                break;
            if( read_at + chunk_size > end )
                goto done;
            memmove(body + decoded, read_at, (size_t)chunk_size);
            decoded += (int)chunk_size;
            read_at += chunk_size + 2; /* trailing CRLF */
        }
        body_size = decoded;
    }
    else if( content_length >= 0 )
    {
        if( content_length > body_size )
            goto done;
        body_size = content_length;
    }

    result = malloc((size_t)(body_size ? body_size : 1));
    assert(result);
    memcpy(result, body, (size_t)body_size);
    *out_size = body_size;

done:
    free(buffer);
    if( stream )
    {
        sockstream_close(stream);
        sockstream_free(stream);
    }
    return result;
}

/* --------------------------------------------------------------- ondemand */

static int
od_open_files(struct PlatformXIOOnDemand* od)
{
    unsigned char hello = 15;
    unsigned char reply[8];

    assert(od);

    if( od->files && sockstream_is_connected(od->files) )
        return 0;

    if( od->files )
    {
        sockstream_close(od->files);
        sockstream_free(od->files);
        od->files = NULL;
    }

    od->files = od_dial(od->host, od->game_port);
    if( !od->files )
        return -1;

    /* Opcode 15 moves the connection out of login negotiation for good. The
     * eight bytes back are the same unused seed a login handshake gets. */
    if( od_write_all(od->files, &hello, 1) != 0 || od_read_exact(od->files, reply, 8) != 0 )
    {
        sockstream_close(od->files);
        sockstream_free(od->files);
        od->files = NULL;
        return -1;
    }
    return 0;
}

/**
 * One file off the on-demand wire, still compressed.
 *
 * `archive` is 0..3 (models, anims, midi, maps). Returns NULL both for "the
 * server does not have it" (a zero-length header, which is a legitimate answer
 * at the edges of a built world) and for a transport failure; the two are
 * distinguished by `*out_size`, which is 0 for the former and -1 for the
 * latter, so a caller that wants to retry can.
 */
static char*
od_fetch_file(
    struct PlatformXIOOnDemand* od,
    int archive,
    int file,
    int* out_size)
{
    unsigned char request[4];
    char* data = NULL;
    int total = -1;
    int received = 0;

    assert(od);
    assert(archive >= 0);
    assert(archive <= 3);
    assert(out_size);

    *out_size = -1;
    if( file < 0 || file > 0xFFFF )
        return NULL;
    if( od_open_files(od) != 0 )
        return NULL;

    request[0] = (unsigned char)archive;
    request[1] = (unsigned char)(file >> 8);
    request[2] = (unsigned char)file;
    request[3] = OD_PRIORITY_URGENT;
    if( od_write_all(od->files, request, 4) != 0 )
        return NULL;

    for( ;; )
    {
        unsigned char header[6];
        int chunk_archive;
        int chunk_file;
        int chunk_total;
        int part;
        int offset;
        int count;

        if( od_read_exact(od->files, header, 6) != 0 )
            goto failed;

        chunk_archive = header[0];
        chunk_file = (header[1] << 8) | header[2];
        chunk_total = (header[3] << 8) | header[4];
        part = header[5];

        /* One request in flight at a time, so anything else on this socket
         * means the two ends have lost sync and every later read would be
         * misframed. Drop the connection rather than decode noise. */
        if( chunk_archive != archive || chunk_file != file )
        {
            fprintf(
                stderr,
                "ondemand: expected %d/%d, server sent %d/%d\n",
                archive,
                file,
                chunk_archive,
                chunk_file);
            goto failed;
        }

        if( chunk_total == 0 )
        {
            *out_size = 0;
            free(data);
            return NULL;
        }

        if( total < 0 )
        {
            total = chunk_total;
            data = malloc((size_t)total);
            assert(data);
        }
        else if( chunk_total != total )
        {
            goto failed;
        }

        offset = part * OD_CHUNK_PAYLOAD;
        if( offset < 0 || offset >= total )
            goto failed;
        count = total - offset;
        if( count > OD_CHUNK_PAYLOAD )
            count = OD_CHUNK_PAYLOAD;

        if( od_read_exact(od->files, data + offset, count) != 0 )
            goto failed;
        received += count;
        if( received >= total )
            break;
    }

    *out_size = total;
    return data;

failed:
    free(data);
    sockstream_close(od->files);
    sockstream_free(od->files);
    od->files = NULL;
    return NULL;
}

/* ------------------------------------------------------------ versionlist */

static struct RSCache_MapSquares*
od_decode_map_squares(
    char* versionlist,
    int versionlist_size)
{
    struct RSCache_FileListDat* filelist = NULL;
    struct RSCache_MapSquares* squares = NULL;
    int name_hash = RSCache_ArchiveNameHashDat("map_index");

    assert(versionlist);

    filelist = RSCache_FileListDatNewFromDecode(versionlist, versionlist_size);
    if( !filelist )
        return NULL;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] != name_hash )
            continue;
        squares = RSCache_MapSquaresNewDecode(filelist->files[i], filelist->file_sizes[i]);
        break;
    }

    RSCache_FileListDatFree(filelist);
    return squares;
}

/* ------------------------------------------------------------------- api */

struct PlatformXIOOnDemand*
PlatformXIOOnDemand_New(
    const char* host,
    int game_port,
    int web_port)
{
    struct PlatformXIOOnDemand* od = NULL;
    char* versionlist = NULL;
    int versionlist_size = 0;

    assert(host);

    if( sockstream_init() != 0 )
        return NULL;

    od = malloc(sizeof(struct PlatformXIOOnDemand));
    assert(od);
    memset(od, 0, sizeof(struct PlatformXIOOnDemand));

    snprintf(od->host, sizeof(od->host), "%s", host);
    od->game_port = game_port > 0 ? game_port : OD_DEFAULT_GAME_PORT;
    od->web_port = web_port > 0 ? web_port : OD_DEFAULT_WEB_PORT;

    /* Prove both wires before anything above this depends on them, and get the
     * map_index out of the way while doing it: `dat1_map_archive_id` has no
     * way to report "the table has not loaded yet", so a handle that exists
     * has to be a handle that can answer. */
    versionlist = od_http_get(
        od, g_jag_routes[RSCACHE_DAT1_CONFIG_VERSION_LIST], &versionlist_size);
    if( !versionlist || versionlist_size <= 0 )
    {
        fprintf(
            stderr,
            "ondemand: no versionlist from http://%s:%d/versionlist -- is the LostCity "
            "server running?\n",
            od->host,
            od->web_port);
        free(versionlist);
        free(od);
        return NULL;
    }

    od->map_squares = od_decode_map_squares(versionlist, versionlist_size);
    free(versionlist);
    if( !od->map_squares )
    {
        fprintf(stderr, "ondemand: the versionlist archive carries no map_index\n");
        free(od);
        return NULL;
    }

    if( od_open_files(od) != 0 )
    {
        fprintf(
            stderr,
            "ondemand: cannot open the file connection to %s:%d\n",
            od->host,
            od->game_port);
        RSCache_MapSquaresFree(od->map_squares);
        free(od);
        return NULL;
    }

    return od;
}

void
PlatformXIOOnDemand_Free(struct PlatformXIOOnDemand* od)
{
    if( !od )
        return;

    if( od->files )
    {
        sockstream_close(od->files);
        sockstream_free(od->files);
    }
    RSCache_MapSquaresFree(od->map_squares);
    free(od);
}

struct RSCache_MapSquares*
PlatformXIOOnDemand_MapSquares(struct PlatformXIOOnDemand* od)
{
    assert(od);
    return od->map_squares;
}

struct RSCache_Dat1DiskArchive*
PlatformXIOOnDemand_ArchiveLoad(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id)
{
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct RSCache_Dat2DiskArchive raw = { 0 };
    enum RSCache_ArchiveFormat format;

    assert(od);
    assert(table_id >= 0);
    assert(archive_id >= 0);

    if( table_id == RSCACHE_DAT1_DISK_TABLE_CONFIGS )
    {
        if( archive_id <= 0 || archive_id >= OD_JAG_ROUTE_COUNT )
            return NULL;
        format = RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE;
        raw.data = od_http_get(od, g_jag_routes[archive_id], &raw.data_size);
        if( !raw.data )
            return NULL;
    }
    else
    {
        int size = 0;
        if( table_id > RSCACHE_DAT1_DISK_TABLE_MAPS )
            return NULL;
        format = RSCACHE_ARCHIVE_FORMAT_DAT;
        raw.data = od_fetch_file(od, table_id - 1, archive_id, &size);
        if( !raw.data )
            return NULL;
        raw.data_size = size;

        /* Tables 1..4 are stored gzipped and served as stored. The disk layer
         * decompresses at the same point, so callers above see one shape. */
        if( !RSCache_ArchiveDecompressDat(&raw, format) )
        {
            fprintf(
                stderr,
                "ondemand: table %d file %d did not decompress (%d bytes)\n",
                table_id,
                archive_id,
                raw.data_size);
            free(raw.data);
            return NULL;
        }
    }

    archive = malloc(sizeof(struct RSCache_Dat1DiskArchive));
    assert(archive);
    memset(archive, 0, sizeof(struct RSCache_Dat1DiskArchive));
    archive->data = raw.data;
    archive->data_size = raw.data_size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;
    archive->format = format;
    return archive;
}

int
PlatformXIOOnDemand_JagChecksums(
    struct PlatformXIOOnDemand* od,
    int32_t out[9])
{
    char* body = NULL;
    int size = 0;

    assert(od);
    assert(out);

    body = od_http_get(od, "/crc", &size);
    if( !body )
        return -1;

    /* Nine big-endian checksums. The server appends a tenth int -- a hash over
     * the nine, which it recomputes itself and the login block never carries
     * -- so a short read is a failure but a long one is not. */
    if( size < 9 * 4 )
    {
        free(body);
        return -1;
    }

    for( int i = 0; i < 9; i++ )
    {
        const unsigned char* p = (const unsigned char*)body + i * 4;
        out[i] = (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                           (uint32_t)p[3]);
    }
    free(body);
    return 0;
}
