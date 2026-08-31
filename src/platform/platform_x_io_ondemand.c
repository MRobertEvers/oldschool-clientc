#include "platform_x_io_ondemand.h"

#include "platform_x_http.h"
#include "sockstream.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include "log/torirs_log.h"

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
/*
 * How long the file wire is given to do each of the three things it does.
 *
 * These are enforced HERE and nowhere below: sockstream_connect takes a
 * connect timeout and discards it (`(void)timeout_sec`), and
 * sockstream_poll_connect selects with a zero timeout. A caller that just
 * spins on the poll therefore has no deadline at all -- it has a busy-wait
 * that ends whenever the OS gives up on the SYN, at 100% of a core, with the
 * frame thread inside it.
 */
#define OD_CONNECT_TIMEOUT_SEC 10
#define OD_READ_TIMEOUT_SEC 30
#define OD_WRITE_TIMEOUT_SEC 30
/*
 * How long an endpoint that timed out is left alone for.
 *
 * Every read here happens on the frame thread inside a task drain, so a
 * timeout is a frame that long. One is survivable. What is not is that nothing
 * above asks for one archive: a world rebuild asks for dozens, each fetch
 * redials and re-reads, and an unreachable server therefore charges its full
 * timeout once PER FILE. Ten seconds of connect plus thirty of read, times
 * fifty archives, is the client gone for half an hour -- which is not a slow
 * load, it is a hang, and it is what gets the process killed.
 *
 * The shutter makes an unreachable server cost ONE timeout. What comes back
 * instead is a scene with squares missing and a line in the log saying so, and
 * a client still running to draw it.
 */
#define OD_ENDPOINT_SHUTTER_SEC 5
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
/* Longest stem ("/versionlist") plus a signed 32-bit decimal plus the NUL. */
#define OD_JAG_ROUTE_MAX 32

struct PlatformXIOOnDemand
{
    char host[OD_HOST_MAX];
    int game_port;
    int web_port;

    /* The file pipe. Opened once and kept: the handshake costs a round trip,
     * and the server keys its request queues on the connection. */
    struct SockStream* files;

    struct RSCache_MapSquares* map_squares;

    /*
     * The local hydration cache, or no directory and none of this happens.
     *
     * `disk` is opened lazily and may stay NULL on a first boot -- there is
     * nothing to read until something has been written. `stamped` says the
     * server's checksums have been compared against the directory's, which
     * must happen before any read is trusted.
     */
    char cache_dir[512];
    struct RSCache_Dat1Disk* cache_disk;
    int cache_stamped;

    /* The nine jag checksums, cached after the first `GET /crc`.
     *
     * They are not only the login block's business. A LostCity server routes
     * the jag archives as `/title<crc>` -- the checksum is a path component,
     * and the route answers 404 when it does not match what the server just
     * packed. Bare `/title` is not an older spelling of the same route; it is
     * the same 404 with an empty parameter. So an archive read needs the table
     * before it can name its own URL, which is why this sits here rather than
     * in the login code that also asks for it. */
    int32_t jag_crc[9];
    int jag_crc_valid;

    /*
     * When the file wire may be dialled again, or 0 for "now". @see
     * OD_ENDPOINT_SHUTTER_SEC.
     *
     * Only this wire has one. The jag archives go out through
     * PlatformX_HttpGetTimed, which opens a fresh connection per request and
     * is not called in a loop over archives -- it is the per-file redial that
     * makes a timeout worth remembering.
     */
    time_t files_shutter;
};

/*
 * What a read stopped for -- the question od_fetch_file's retry turns on, and
 * the two answers want opposite treatment.
 *
 * DEAD is ROUTINE here. A LostCity server hangs up on an idle client
 * (`s.setTimeout(30000)` in TcpServer.ts) and says nothing about it, so the
 * first fetch after a quiet spell always finds a dead socket. That is what the
 * retry below exists for, and it costs nothing, because a closed socket fails
 * at once.
 *
 * TIMEOUT is not routine and must not be retried. The connection is open and
 * the server is simply not answering, so the retry buys a second full read
 * timeout and the redial after it buys a third. @see OD_ENDPOINT_SHUTTER_SEC
 * for what that multiplies out to.
 */
enum
{
    OD_IO_OK = 0,
    OD_IO_DEAD = -1,
    OD_IO_TIMEOUT = -2,
};

/* od_fetch_file_once's `*out_size` where there is no file to size: the server
 * looked and has none, or one of the two transport failures above. */
#define OD_FETCH_ABSENT 0
#define OD_FETCH_DEAD -1
#define OD_FETCH_TIMEOUT -2

/* ------------------------------------------------------------------ socket */

/**
 * Sleep until the socket can be read (or written), or the timeout runs out.
 *
 * Nonzero means something happened -- readable, writable, or failed -- and the
 * caller should look; zero means the deadline passed with the socket silent.
 *
 * The point of it is the SLEEP. sockstream is non-blocking, so every retry
 * loop over it is a busy-wait unless something parks the thread in between,
 * and this is that something for all three of connect, read and write.
 */
static int
od_wait(
    struct SockStream* stream,
    int for_write,
    int timeout_sec)
{
    struct timeval tv;
    fd_set ready;
    fd_set failed;
    intptr_t fd = sockstream_get_fd(stream);
    int result;

    if( fd < 0 )
        return 0;

    FD_ZERO(&ready);
    FD_ZERO(&failed);
#ifdef _WIN32
    FD_SET((SOCKET)fd, &ready);
    FD_SET((SOCKET)fd, &failed);
#else
    if( fd >= FD_SETSIZE )
        return 0;
    FD_SET((int)fd, &ready);
    FD_SET((int)fd, &failed);
#endif
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    /* The exception set rides along with the write wait because that is the
     * one a connect in flight uses, and on Windows a refused connect raises
     * the exception fd rather than the write fd. Without it the dial below
     * would sit out its whole timeout against a server that had already said
     * no. */
    if( for_write )
        result = select((int)fd + 1, NULL, &ready, &failed, &tv);
    else
        result = select((int)fd + 1, &ready, NULL, NULL, &tv);
    return result > 0;
}

/**
 * Fill `size` bytes or fail.
 *
 * sockstream is non-blocking, so NODATA is "not yet" and everything else is
 * "never" -- including CLOSED, which is the one this must not spin on. A
 * socket the server hung up on stays readable forever under select(), so
 * treating a close as a retry would busy-loop until the timeout instead of
 * reporting the disconnect.
 *
 * Fails as OD_IO_DEAD or OD_IO_TIMEOUT, which are not the same news: see the
 * enum for why only one of the two is worth asking again.
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
            return OD_IO_DEAD;
        if( !od_wait(stream, /* for_write */ 0, OD_READ_TIMEOUT_SEC) )
            return OD_IO_TIMEOUT;
    }
    return OD_IO_OK;
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
            return OD_IO_DEAD;
        /* The same wait its reading twin does, and for a stronger reason: a
         * peer that has stopped draining leaves send() answering WOULDBLOCK
         * for as long as it stays that way, and retrying on the spot was a
         * tight loop with no wait in it and no deadline on it at all -- the
         * frame thread pinned until the process was killed. */
        if( !od_wait(stream, /* for_write */ 1, OD_WRITE_TIMEOUT_SEC) )
            return OD_IO_TIMEOUT;
    }
    return OD_IO_OK;
}

/**
 * The file wire, dialled with a deadline and a memory.
 *
 * Both halves of that are the point. sockstream_connect takes the timeout and
 * throws it away, and sockstream_poll_connect selects with a zero timeout, so
 * the loop that used to be here polled as fast as the CPU allowed for as long
 * as the OS kept the SYN alive -- a core at 100% and a frame that never ended,
 * which is what a wedged host (as opposed to a refused port, which fails at
 * once) looks like from in here. The wait below is what makes
 * OD_CONNECT_TIMEOUT_SEC a real number.
 *
 * The memory is od->files_shutter: a dial that timed out is remembered,
 * because the caller above is a loop over archives and would otherwise pay for
 * it once per archive. @see OD_ENDPOINT_SHUTTER_SEC.
 */
static struct SockStream*
od_dial(struct PlatformXIOOnDemand* od)
{
    struct SockStream* stream;
    time_t deadline;
    int state = SOCKSTREAM_CONNECT_FAILED;

    assert(od);

    if( od->files_shutter != 0 && time(NULL) < od->files_shutter )
        return NULL;

    stream = sockstream_new();
    if( !stream )
        return NULL;

    sockstream_connect(stream, od->host, od->game_port, OD_CONNECT_TIMEOUT_SEC);
    deadline = time(NULL) + OD_CONNECT_TIMEOUT_SEC;
    for( ;; )
    {
        state = sockstream_poll_connect(stream);
        if( state != SOCKSTREAM_CONNECT_INFLIGHT )
            break;
        if( time(NULL) >= deadline )
        {
            TORIRS_ERR("ondemand: %s:%d did not answer in %ds\n",
                od->host,
                od->game_port,
                OD_CONNECT_TIMEOUT_SEC);
            state = SOCKSTREAM_CONNECT_FAILED;
            break;
        }
        /* A second at a time rather than the whole remaining budget, so the
         * poll -- which is what actually classifies success and failure --
         * runs again promptly once the socket moves. */
        od_wait(stream, /* for_write */ 1, 1);
    }

    if( state == SOCKSTREAM_CONNECT_SUCCESS )
    {
        od->files_shutter = 0;
        return stream;
    }

    od->files_shutter = time(NULL) + OD_ENDPOINT_SHUTTER_SEC;
    sockstream_close(stream);
    sockstream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------------- http */

/**
 * Read a whole response body, through the client's one HTTP fetch.
 *
 * This used to be its own ninety-line copy of PlatformX_HttpGetTimed's loop,
 * and the two drifted exactly as duplicated protocol does: the shared one
 * learned to stop as soon as a Content-Length body was complete, and this one
 * kept reading to the close -- while its docstring claimed otherwise, because
 * it did read the header, just only to size the body afterwards. Every rev-289
 * boot pulls nine jag archives through here, so that gap is where "Socket recv
 * error: connection closed" kept coming from after the framing fix landed.
 *
 * The longer timeouts are this client's own and are passed rather than
 * inherited: a cache stream against a remote server stutters where a config
 * read must not hang the UI.
 */
static char*
od_http_get(
    struct PlatformXIOOnDemand* od,
    const char* route,
    int* out_size)
{
    assert(od);
    assert(route);
    assert(out_size);

    return PlatformX_HttpGetTimed(
        od->host,
        od->web_port,
        route,
        out_size,
        NULL,
        OD_CONNECT_TIMEOUT_SEC,
        OD_READ_TIMEOUT_SEC);
}
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

    od->files = od_dial(od);
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
 * One attempt at one file. Returns NULL both for "the server does not have it"
 * (a zero-length header, a legitimate answer at the edges of a built world) and
 * for a transport failure; `*out_size` distinguishes them -- OD_FETCH_ABSENT
 * for the former, and for the latter the two failures the retry has to tell
 * apart, OD_FETCH_DEAD and OD_FETCH_TIMEOUT.
 */
static char*
od_fetch_file_once(
    struct PlatformXIOOnDemand* od,
    int archive,
    int file,
    int* out_size)
{
    unsigned char request[4];
    char* data = NULL;
    int total = -1;
    int received = 0;
    /* Only a read that ran out of time sets this. The other ways out of the
     * loop below are a misframed stream, which is a dead connection by another
     * name and is retried like one. */
    int timed_out = 0;

    assert(od);
    assert(archive >= 0);
    assert(archive <= 3);
    assert(out_size);

    *out_size = OD_FETCH_DEAD;
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
        int io;

        io = od_read_exact(od->files, header, 6);
        if( io != OD_IO_OK )
        {
            timed_out = io == OD_IO_TIMEOUT;
            goto failed;
        }

        chunk_archive = header[0];
        chunk_file = (header[1] << 8) | header[2];
        chunk_total = (header[3] << 8) | header[4];
        part = header[5];

        /* One request in flight at a time, so anything else on this socket
         * means the two ends have lost sync and every later read would be
         * misframed. Drop the connection rather than decode noise. */
        if( chunk_archive != archive || chunk_file != file )
        {
            TORIRS_ERR("ondemand: expected %d/%d, server sent %d/%d\n",
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

        io = od_read_exact(od->files, data + offset, count);
        if( io != OD_IO_OK )
        {
            timed_out = io == OD_IO_TIMEOUT;
            goto failed;
        }
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
    /* A server that held the connection open and then said nothing is the one
     * failure worth remembering: asking again costs another full read timeout,
     * and the caller above is about to ask for the next of fifty archives.
     * @see OD_ENDPOINT_SHUTTER_SEC. */
    if( timed_out )
    {
        TORIRS_ERR("ondemand: %s:%d went quiet mid-file (%d/%d); shuttering it for %ds\n",
            od->host,
            od->game_port,
            archive,
            file,
            OD_ENDPOINT_SHUTTER_SEC);
        *out_size = OD_FETCH_TIMEOUT;
        od->files_shutter = time(NULL) + OD_ENDPOINT_SHUTTER_SEC;
    }
    return NULL;
}

/**
 * One file off the on-demand wire, still compressed.
 *
 * Retries once through a fresh connection, because the socket dying is a
 * ROUTINE state here rather than an error: LostCity puts a 30-second idle
 * timeout on it (`s.setTimeout(30000)` in TcpServer.ts, and the handler
 * destroys the socket), and a client that has everything it needs for half a
 * minute -- standing still in a loaded scene -- gets hung up on. Nothing tells
 * it: `sockstream_is_connected` still says yes, so od_open_files sees no reason
 * to redial, the request goes into a dead socket, and the read fails.
 *
 * Without the retry that first fetch after any idle spell is simply lost, and
 * nothing above asks again -- the model, map square or animation frame it was
 * for never arrives. That is what "Failed to decode dat1 model 63" was, on
 * bytes the server sends perfectly well.
 *
 * Only a DEAD socket is retried. A read that ran out of time is not the same
 * failure and is not asked again: the connection is up and the server is not
 * answering on it, so a second attempt is a second full timeout spent on the
 * frame thread to hear the same silence. @see OD_ENDPOINT_SHUTTER_SEC.
 *
 * A zero-length answer means the server
 * looked and has no such file; asking a second time would just be a slower way
 * to hear the same thing.
 */
static char*
od_fetch_file(
    struct PlatformXIOOnDemand* od,
    int archive,
    int file,
    int* out_size)
{
    char* data = od_fetch_file_once(od, archive, file, out_size);

    if( data || *out_size != OD_FETCH_DEAD )
        return data;

    /* od_fetch_file_once has already dropped the socket, so this reconnects. */
    return od_fetch_file_once(od, archive, file, out_size);
}

/* ------------------------------------------------------------- jag routes */

/**
 * Fill od->jag_crc from `GET /crc`, once.
 *
 * Cached because every jag archive read needs it: the checksum is part of the
 * route (see od_jag_route), so an uncached table would mean a second TCP
 * connection to the web port before each of the nine. The table cannot go
 * stale within a session -- a server that repacks its cache changes the
 * checksums, and a client it was already serving is out of date by definition,
 * which login reports as reply 6 rather than as a bad read here.
 *
 * This sits above PlatformXIOOnDemand_New because New is itself a caller:
 * it reads /versionlist to build the map index, and that read needs a route.
 */
static int
od_jag_crc_load(struct PlatformXIOOnDemand* od)
{
    char* body = NULL;
    int size = 0;

    assert(od);

    if( od->jag_crc_valid )
        return 0;

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
        od->jag_crc[i] =
            (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                      (uint32_t)p[3]);
    }
    free(body);
    od->jag_crc_valid = 1;
    return 0;
}

/**
 * Write the route serving jag archive `archive_id` into `out`.
 *
 * `/title-564448443`, not `/title`. The server checks the checksum against
 * what it last packed and answers 404 when it differs, so the bare stem is not
 * an older spelling of the same route -- it is that same 404 with an empty
 * parameter, which is why a client that never appended one saw every archive
 * as missing rather than as stale.
 *
 * The archive ids indexing g_jag_routes are the ids indexing the checksum
 * table, so one id names both halves.
 */
static int
od_jag_route(
    struct PlatformXIOOnDemand* od,
    int archive_id,
    char* out,
    size_t out_size)
{
    assert(od);
    assert(out);
    assert(archive_id > 0);
    assert(archive_id < OD_JAG_ROUTE_COUNT);

    if( od_jag_crc_load(od) != 0 )
        return -1;

    snprintf(out, out_size, "%s%d", g_jag_routes[archive_id], (int)od->jag_crc[archive_id]);
    return 0;
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
    int web_port,
    const char* cache_dir)
{
    struct PlatformXIOOnDemand* od = NULL;
    char* versionlist = NULL;
    int versionlist_size = 0;
    char route[OD_JAG_ROUTE_MAX];

    assert(host);

    if( sockstream_init() != 0 )
        return NULL;

    od = malloc(sizeof(struct PlatformXIOOnDemand));
    assert(od);
    memset(od, 0, sizeof(struct PlatformXIOOnDemand));

    snprintf(od->host, sizeof(od->host), "%s", host);
    od->game_port = game_port > 0 ? game_port : OD_DEFAULT_GAME_PORT;
    od->web_port = web_port > 0 ? web_port : OD_DEFAULT_WEB_PORT;
    if( cache_dir && cache_dir[0] )
        snprintf(od->cache_dir, sizeof(od->cache_dir), "%s", cache_dir);

    /* Prove both wires before anything above this depends on them, and get the
     * map_index out of the way while doing it: `dat1_map_archive_id` has no
     * way to report "the table has not loaded yet", so a handle that exists
     * has to be a handle that can answer. */
    if( od_jag_route(od, RSCACHE_DAT1_CONFIG_VERSION_LIST, route, sizeof(route)) != 0 )
    {
        TORIRS_LOG("ondemand: no checksums from http://%s:%d/crc -- is the LostCity "
            "server running?\n",
            od->host,
            od->web_port);
        free(od);
        return NULL;
    }

    versionlist = od_http_get(od, route, &versionlist_size);
    if( !versionlist || versionlist_size <= 0 )
    {
        TORIRS_LOG("ondemand: no versionlist from http://%s:%d%s -- is the LostCity "
            "server running?\n",
            od->host,
            od->web_port,
            route);
        free(versionlist);
        free(od);
        return NULL;
    }

    od->map_squares = od_decode_map_squares(versionlist, versionlist_size);
    free(versionlist);
    if( !od->map_squares )
    {
        TORIRS_LOG("ondemand: the versionlist archive carries no map_index\n");
        free(od);
        return NULL;
    }

    if( od_open_files(od) != 0 )
    {
        TORIRS_ERR("ondemand: cannot open the file connection to %s:%d\n",
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
    if( od && od->cache_disk )
    {
        RSCache_Dat1DiskFree(od->cache_disk);
        od->cache_disk = NULL;
    }
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

/* -- fetch tally (TORIRS_OD_STATS=1) ------------------------------------ */

#define OD_TALLY_MAX 4096

struct OdTallyEntry
{
    int table_id;
    int archive_id;
    int hits;
    long bytes;
};

static struct OdTallyEntry g_od_tally[OD_TALLY_MAX];
static int g_od_tally_used;
static long g_od_fetches;
static long g_od_bytes;
static long g_od_refetches;
static long g_od_refetch_bytes;
static double g_od_fetch_ms;
static double g_od_http_ms;

static void
od_tally_report(void)
{
    fprintf(stderr,
            "od_stats: wire_ms=%.0f (file=%.0f http=%.0f) per_fetch=%.1fms\n",
            g_od_fetch_ms + g_od_http_ms, g_od_fetch_ms, g_od_http_ms,
            g_od_fetches ? (g_od_fetch_ms + g_od_http_ms) / (double)g_od_fetches
                         : 0.0);
    fprintf(stderr,
            "od_stats: fetches=%ld distinct=%d refetches=%ld "
            "bytes=%ld refetch_bytes=%ld\n",
            g_od_fetches, g_od_tally_used, g_od_refetches,
            g_od_bytes, g_od_refetch_bytes);
    if( g_od_tally_used > 0 )
    {
        int worst = 0;
        for( int i = 1; i < g_od_tally_used; i++ )
            if( g_od_tally[i].hits > g_od_tally[worst].hits )
                worst = i;
        fprintf(stderr,
                "od_stats: most re-fetched container table=%d archive=%d "
                "x%d (%ld bytes each time)\n",
                g_od_tally[worst].table_id,
                g_od_tally[worst].archive_id,
                g_od_tally[worst].hits,
                g_od_tally[worst].hits ? g_od_tally[worst].bytes / g_od_tally[worst].hits : 0);
    }
}

static int
od_tally_on(void)
{
    static int on = -1;
    if( on < 0 )
    {
        on = getenv("TORIRS_OD_STATS") ? 1 : 0;
        if( on )
            atexit(od_tally_report);
    }
    return on;
}

static void
od_tally(int table_id, int archive_id, int size)
{
    if( !od_tally_on() )
        return;
    g_od_fetches++;
    g_od_bytes += size;
    for( int i = 0; i < g_od_tally_used; i++ )
    {
        if( g_od_tally[i].table_id == table_id &&
            g_od_tally[i].archive_id == archive_id )
        {
            g_od_tally[i].hits++;
            g_od_tally[i].bytes += size;
            g_od_refetches++;
            g_od_refetch_bytes += size;
            return;
        }
    }
    if( g_od_tally_used < OD_TALLY_MAX )
    {
        g_od_tally[g_od_tally_used].table_id = table_id;
        g_od_tally[g_od_tally_used].archive_id = archive_id;
        g_od_tally[g_od_tally_used].hits = 1;
        g_od_tally[g_od_tally_used].bytes = size;
        g_od_tally_used++;
    }
}

/* -- the local hydration cache ------------------------------------------ */

#define OD_STAMP_NAME "ondemand.stamp"

/*
 * Wipe the cached cache.
 *
 * Called when the server's checksums do not match the ones this directory was
 * filled from, which means the server repacked. Reconciling would mean knowing
 * WHICH archives moved, and the checksums do not say -- so the whole directory
 * goes. It is a cache; the cost of being wrong here is a slow boot, and the
 * cost of being wrong the other way is login reply 6 with nothing to explain
 * it.
 */
static void
od_cache_wipe(const char* dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat", dir);
    remove(path);
    for( int table = 0; table <= RSCACHE_DAT1_DISK_TABLE_MAPS; table++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, table);
        remove(path);
    }
    snprintf(path, sizeof(path), "%s/%s", dir, OD_STAMP_NAME);
    remove(path);
}

/*
 * Compare the server's nine jag checksums with the ones this directory holds,
 * and make them agree -- by wiping, when they do not.
 *
 * These are the right stamp rather than a convenient one. They are fetched
 * every boot regardless (the jag routes embed them), and they cover the whole
 * cache: the versionlist that governs tables 1-4 is itself one of the nine, so
 * a server that repacks a single map square changes a checksum here.
 */
/* The directory has to exist before RSCache_Dat1DiskWriteArchive can put a
 * main_file_cache.dat in it, and a profile naming a hydration directory is
 * asking for one rather than promising it already made it. Failure is
 * ignored: the next write fails too, and a cache that cannot be created is a
 * slow boot, not a broken one. */
static void
od_cache_mkdir_one(const char* dir)
{
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0777);
#endif
}

/*
 * Every component, not just the leaf.
 *
 * One mkdir was enough while the directory sat beside the manifest and its
 * parent was the checkout. The default is now <home>/torirs_cache/<game>/<world>,
 * where `torirs` and `<world>` are both routinely missing, and a single mkdir
 * fails on the first one and then fails again on the second.
 *
 * Each intermediate result is ignored for the same reason the whole function
 * is: EEXIST is the common case and indistinguishable here from a permission
 * failure that the next write reports anyway. A directory that cannot be
 * created is a slow boot, not a broken one.
 */
static void
od_cache_mkdir(const char* dir)
{
    char path[1024];
    size_t n = strlen(dir);

    if( n == 0 || n >= sizeof(path) )
        return;
    memcpy(path, dir, n + 1);

    /* From 1: a leading separator is the root, not a component to create.
     * A drive prefix is skipped for the same reason -- `C:` is not a
     * directory, and asking to create it fails harmlessly but pointlessly. */
    for( size_t i = (path[1] == ':' ? 3 : 1); i < n; i++ )
    {
        if( path[i] != '/' && path[i] != '\\' )
            continue;
        char const sep = path[i];
        path[i] = '\0';
        od_cache_mkdir_one(path);
        path[i] = sep;
    }
    od_cache_mkdir_one(path);
}

static void
od_cache_stamp(struct PlatformXIOOnDemand* od)
{
    char path[1024];
    FILE* f;
    int32_t stored[9];
    int match = 0;

    if( od->cache_stamped || !od->cache_dir[0] )
        return;
    od->cache_stamped = 1;

    /* An `idb:<name>` location is an IndexedDB database, which this file
     * cannot write to and must not turn into a directory of that name. The web
     * build stores its containers through the browser rather than here; a
     * native build handed one streams instead, which is what it did before any
     * location was stated at all. */
    if( strncmp(od->cache_dir, "idb:", 4) == 0 )
        return;

    if( od_jag_crc_load(od) != 0 )
        return;

    od_cache_mkdir(od->cache_dir);
    snprintf(path, sizeof(path), "%s/%s", od->cache_dir, OD_STAMP_NAME);
    f = fopen(path, "rb");
    if( f )
    {
        match = fread(stored, sizeof(stored[0]), 9, f) == 9 &&
                memcmp(stored, od->jag_crc, sizeof(stored)) == 0;
        fclose(f);
    }

    if( !match )
    {
        if( f )
            TORIRS_LOG("ondemand: %s was packed from different checksums; wiping\n",
                od->cache_dir);
        od_cache_wipe(od->cache_dir);
        f = fopen(path, "wb");
        if( f )
        {
            fwrite(od->jag_crc, sizeof(od->jag_crc[0]), 9, f);
            fclose(f);
        }
    }

    /*
     * Only open a cache that is actually there.
     *
     * A hydrating directory has no main_file_cache.dat until the first archive
     * is stored in it, and asking the disk layer to open one anyway made every
     * first launch print "Failed to open dat file" and name the directory --
     * which reads as a configuration error and is not one. It is the ordinary
     * state of a cache nothing has filled yet, and it is now the state EVERY
     * first launch is in, since the hydration directory defaults to one under
     * the user's documents rather than one shipped beside the client.
     *
     * The message is left alone where it belongs: a `source=disk` world names
     * a cache to READ, and a missing dat there is a genuine fault. Which one
     * this is, is knowable only to the caller -- so it is tested here rather
     * than softened in the library.
     *
     * Returning leaves cache_disk NULL, which od_cache_load already treats as
     * a miss. The first store creates the dat and reopens.
     */
    {
        char dat[1024];
        FILE* probe;
        snprintf(dat, sizeof(dat), "%s/main_file_cache.dat", od->cache_dir);
        probe = fopen(dat, "rb");
        if( !probe )
            return;
        fclose(probe);
    }

    od->cache_disk = RSCache_Dat1DiskNewFromDirectory(od->cache_dir);
}

/* A cached copy, or NULL. The disk layer decompresses tables 1-4 on read, the
 * same as the fetch path does, so a hit and a miss return the same shape. */
static struct RSCache_Dat1DiskArchive*
od_cache_load(struct PlatformXIOOnDemand* od, int table_id, int archive_id)
{
    if( !od->cache_dir[0] )
        return NULL;
    od_cache_stamp(od);
    if( !od->cache_disk )
        return NULL;
    return RSCache_Dat1DiskArchiveNewLoad(od->cache_disk, table_id, archive_id);
}

/*
 * Store what was just fetched.
 *
 * `data` must be the bytes AS SERVED -- still gzipped for tables 1-4 -- since
 * that is what the disk layer stores and what it decompresses on the way back
 * out. Handing it the decompressed form would make the next boot's hit differ
 * from this boot's miss.
 */
static void
od_cache_store(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    const void* data,
    int size)
{
    if( !od->cache_dir[0] || size <= 0 )
        return;
    od_cache_stamp(od);
    if( RSCache_Dat1DiskWriteArchive(
            od->cache_dir, table_id, archive_id, (const uint8_t*)data, size) != 0 )
        return;
    /* Reopen so the write is visible to reads in THIS session too; the disk
     * handle caches its index files. */
    if( od->cache_disk )
        RSCache_Dat1DiskFree(od->cache_disk);
    od->cache_disk = RSCache_Dat1DiskNewFromDirectory(od->cache_dir);
}

uint8_t*
PlatformXIOOnDemand_ContainerFetch(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    int* out_format,
    int* out_size)
{
    assert(od);
    assert(out_format);
    assert(out_size);
    assert(table_id >= 0);
    assert(archive_id >= 0);

    *out_size = 0;

    /*
     * Table 0 is not on the socket at all, and that asymmetry is the server's,
     * not a shortcut here. The eight jag archives are HTTP resources whose
     * path carries the checksum the server last packed them with; the on-demand
     * protocol serves the OTHER tables, and numbers them from zero, which is
     * where the -1 below comes from.
     */
    if( table_id == RSCACHE_DAT1_DISK_TABLE_CONFIGS )
    {
        char route[OD_JAG_ROUTE_MAX];
        char* data;

        if( archive_id <= 0 || archive_id >= OD_JAG_ROUTE_COUNT )
            return NULL;
        if( od_jag_route(od, archive_id, route, sizeof(route)) != 0 )
            return NULL;

        data = od_http_get(od, route, out_size);
        if( !data )
            return NULL;
        od_tally(table_id, archive_id, *out_size);
        *out_format = RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE;
        return (uint8_t*)data;
    }

    if( table_id > RSCACHE_DAT1_DISK_TABLE_MAPS )
        return NULL;

    {
        char* data = od_fetch_file(od, table_id - 1, archive_id, out_size);
        if( !data )
            return NULL;
        od_tally(table_id, archive_id, *out_size);
        /* Stored as served: still gzipped. The decode is the reader's, which
         * is the whole difference between this and ArchiveLoad. */
        *out_format = RSCACHE_ARCHIVE_FORMAT_DAT;
        return (uint8_t*)data;
    }
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

    /* The local copy first. Nothing below runs on a hit -- no socket, no HTTP,
     * no decompression -- which is the whole point of having one. */
    archive = od_cache_load(od, table_id, archive_id);
    if( archive )
        return archive;

    if( table_id == RSCACHE_DAT1_DISK_TABLE_CONFIGS )
    {
        char route[OD_JAG_ROUTE_MAX];

        if( archive_id <= 0 || archive_id >= OD_JAG_ROUTE_COUNT )
            return NULL;
        format = RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE;

        if( od_jag_route(od, archive_id, route, sizeof(route)) != 0 )
            return NULL;

        {
            clock_t const t0 = clock();
            raw.data = od_http_get(od, route, &raw.data_size);
            g_od_http_ms += (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        }
        if( !raw.data )
            return NULL;
        od_tally(table_id, archive_id, raw.data_size);
        /* A jag archive is stored as it arrives. */
        od_cache_store(od, table_id, archive_id, raw.data, raw.data_size);
    }
    else
    {
        int size = 0;
        if( table_id > RSCACHE_DAT1_DISK_TABLE_MAPS )
            return NULL;
        format = RSCACHE_ARCHIVE_FORMAT_DAT;
        {
            clock_t const t0 = clock();
            raw.data = od_fetch_file(od, table_id - 1, archive_id, &size);
            g_od_fetch_ms += (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        }
        if( !raw.data )
            return NULL;
        raw.data_size = size;

        /*
         * The last two bytes are the file's VERSION, not payload.
         *
         * The reference client reads them as a big-endian u16 and CRCs only
         * what precedes them (jagex2/io/OnDemand.validate: `len - 2`), so the
         * gzip member is everything before them. Carrying them meant the
         * member did not end where the buffer did, and ISIZE -- which is read
         * from the last four bytes OF THE BUFFER -- came back as two bytes of
         * real length followed by two bytes of version. For any archive under
         * 64 KiB the real half is zero and that reads as exactly 0x01000000:
         * 16 MiB, on every file.
         *
         * On a 32-bit client that is not merely wasteful. A 16 MiB allocation
         * per model against a few KB of data exhausts the address space it
         * fragments; malloc then fails, the decompress answers false, and the
         * model, loc or sprite is gone for the session -- silently, because
         * that miss is logged with TORIRS_LOG and OPT=1 compiles it out. It
         * cost 68 of 351 models on the LostCity lane.
         *
         * Dropped before the store as well as before the decompress, so the
         * copy the local dat1 cache keeps is a clean member too and the disk
         * read back does not inherit the same mis-read.
         */
        if( raw.data_size < 2 )
        {
            free(raw.data);
            return NULL;
        }
        raw.data_size -= 2;

        od_tally(table_id, archive_id, raw.data_size);

        /* Stored BEFORE the decompress below: gzipped is the form the disk
         * layer keeps and the form it decompresses on the way back out. */
        od_cache_store(od, table_id, archive_id, raw.data, raw.data_size);

        /* Tables 1..4 are stored gzipped and served as stored. The disk layer
         * decompresses at the same point, so callers above see one shape. */
        if( !RSCache_ArchiveDecompressDat(&raw, format) )
        {
            TORIRS_LOG("ondemand: table %d file %d did not decompress (%d bytes)\n",
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
    assert(od);
    assert(out);

    if( od_jag_crc_load(od) != 0 )
        return -1;
    memcpy(out, od->jag_crc, sizeof(od->jag_crc));
    return 0;
}
