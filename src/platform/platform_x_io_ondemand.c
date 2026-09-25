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

    /*
     * The file wire's in-flight table. @see od_pump for the protocol side.
     *
     * Every read the executor parks on this source is an entry here from
     * FetchBegin until FetchTake; the requests go out back to back and the
     * chunks come back in whatever order the server drains its queue, which
     * is what makes the whole table one round trip deep instead of one per
     * file.
     */
    struct OdFetch* fetches;
    int fetch_count;
    int fetch_cap;

    /* Receive state: the six-byte chunk header being assembled, then the
     * chunk payload being written straight into its fetch's buffer. */
    unsigned char rx_header[6];
    int rx_header_have;
    int rx_fetch; /* index into fetches, or -1 between chunks */
    int rx_offset;
    int rx_count;
    int rx_got;

    /* Request bytes queued and not yet accepted by the socket. */
    unsigned char* tx;
    int tx_len;
    int tx_pos;
    int tx_cap;

    /* When the wire last moved a byte either way; the pump's timeout clock. */
    time_t wire_last_activity;
};

/* One file in flight on the wire. */
enum OdFetchState
{
    /** Request not yet handed to the socket. */
    OD_FETCH_QUEUED = 0,
    /** Request written; chunks may be arriving. */
    OD_FETCH_INFLIGHT,
    /** Every byte landed. */
    OD_FETCH_DONE,
    /** The server looked and has no such file -- an answer. */
    OD_FETCH_ABSENT,
    /** The transport gave up on it. */
    OD_FETCH_FAILED,
};

struct OdFetch
{
    int archive; /* wire archive, 0..3 */
    int file;
    enum OdFetchState state;
    int total; /* -1 until the first chunk names it */
    int received;
    char* data;
    /* Request writes so far. A dead socket earns exactly one more, for the
     * same reason the blocking fetch retried once: LostCity hangs up on an
     * idle client and the first request after a quiet spell always finds a
     * dead socket. A timeout earns none. */
    int attempts;
    /* Callers that will Take this. Two reads of one file share one request,
     * and the entry lives until the last of them has taken its copy. */
    int waiters;
};

/*
 * What a read stopped for -- the question the wire's retry turns on, and
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

/* ------------------------------------------------------- the file wire */

/*
 * ## Pipelined, not blocking
 *
 * The wire used to be one request at a time: write four bytes, read every
 * chunk of the answer, return. A region rebuild names hundreds of files and
 * the executor now hands them over together (TaskRunner_Step walks the whole
 * queue), so a blocking fetch turned that into hundreds of round trips end to
 * end -- on the frame thread.
 *
 * Now a fetch is an entry in `od->fetches`. FetchBegin queues its request;
 * od_pump writes every queued request the socket will take and reads every
 * chunk that has arrived, filing each by the (archive, file) in its header;
 * FetchTake hands a finished one back. Nothing here waits for the socket:
 * the executor asks "is it done yet" once a pass, exactly as it asks the JS5
 * client, and the blocking entry points below (ArchiveLoad, ContainerFetch)
 * are a pump loop around the same table for the callers that still want to
 * wait -- io_server's single-container route, and the nine jag archives a
 * boot reads before anything can be parked.
 *
 * The chunk header carries the file the chunk belongs to, so the server is
 * free to interleave answers and the reassembly does not care what order
 * they come in. Every chunk of a file lands at `part * 500`, and the file is
 * done when the byte count reaches the total its first chunk stated.
 */

static int
od_fetch_find(
    struct PlatformXIOOnDemand* od,
    int archive,
    int file)
{
    for( int i = 0; i < od->fetch_count; i++ )
        if( od->fetches[i].archive == archive && od->fetches[i].file == file )
            return i;
    return -1;
}

static int
od_fetch_add(
    struct PlatformXIOOnDemand* od,
    int archive,
    int file)
{
    struct OdFetch* fetch;

    if( od->fetch_count == od->fetch_cap )
    {
        od->fetch_cap = od->fetch_cap ? od->fetch_cap * 2 : 64;
        od->fetches = realloc(od->fetches, (size_t)od->fetch_cap * sizeof(*od->fetches));
        assert(od->fetches);
    }
    fetch = &od->fetches[od->fetch_count];
    memset(fetch, 0, sizeof(*fetch));
    fetch->archive = archive;
    fetch->file = file;
    fetch->state = OD_FETCH_QUEUED;
    fetch->total = -1;
    return od->fetch_count++;
}

/* Drop entry `index`; the last entry moves into its place. */
static void
od_fetch_remove(
    struct PlatformXIOOnDemand* od,
    int index)
{
    assert(index >= 0);
    assert(index < od->fetch_count);
    free(od->fetches[index].data);
    od->fetch_count--;
    if( od->rx_fetch == index )
        od->rx_fetch = -1;
    if( index != od->fetch_count )
    {
        od->fetches[index] = od->fetches[od->fetch_count];
        if( od->rx_fetch == od->fetch_count )
            od->rx_fetch = index;
    }
}

static void
od_tx_append(
    struct PlatformXIOOnDemand* od,
    const unsigned char* bytes,
    int count)
{
    if( od->tx_len + count > od->tx_cap )
    {
        od->tx_cap = od->tx_cap ? od->tx_cap * 2 : 256;
        while( od->tx_len + count > od->tx_cap )
            od->tx_cap *= 2;
        od->tx = realloc(od->tx, (size_t)od->tx_cap);
        assert(od->tx);
    }
    memcpy(od->tx + od->tx_len, bytes, (size_t)count);
    od->tx_len += count;
}

/*
 * The socket is gone. Whatever was in flight goes back to the queue for one
 * more try, or fails if it has had it; whatever was half-received is
 * discarded, because a resent request is answered from part 0 again.
 *
 * `shutter` says the wire timed out rather than closed: nothing is retried,
 * and the endpoint is left alone for a while. @see OD_ENDPOINT_SHUTTER_SEC.
 */
static long g_od_drops;
static long g_od_requeued;
static long g_od_failed;

static void
od_wire_drop(
    struct PlatformXIOOnDemand* od,
    int shutter)
{
    int inflight = 0;

    for( int i = 0; i < od->fetch_count; i++ )
        if( od->fetches[i].state == OD_FETCH_INFLIGHT || od->fetches[i].state == OD_FETCH_QUEUED )
            inflight++;
    g_od_drops++;
    /* TORIRS_OD_STATS=1 says when the wire went and what it cost: a hang-up
     * with a burst in flight is a whole burst re-sent, and one that repeats
     * is the server refusing the burst rather than idling out. */
    if( getenv("TORIRS_OD_STATS") )
        TORIRS_ERR("ondemand: wire %s with %d in flight (tx %d/%d bytes pending)\n",
            shutter ? "shuttered" : "dropped",
            inflight,
            od->tx_len - od->tx_pos,
            od->tx_len);

    if( od->files )
    {
        sockstream_close(od->files);
        sockstream_free(od->files);
        od->files = NULL;
    }
    od->tx_len = 0;
    od->tx_pos = 0;
    od->rx_header_have = 0;
    od->rx_fetch = -1;
    if( shutter )
        od->files_shutter = time(NULL) + OD_ENDPOINT_SHUTTER_SEC;

    for( int i = 0; i < od->fetch_count; i++ )
    {
        struct OdFetch* fetch = &od->fetches[i];
        if( fetch->state != OD_FETCH_INFLIGHT && fetch->state != OD_FETCH_QUEUED )
            continue;
        free(fetch->data);
        fetch->data = NULL;
        fetch->total = -1;
        fetch->received = 0;
        fetch->state = (!shutter && fetch->attempts < 2) ? OD_FETCH_QUEUED : OD_FETCH_FAILED;
        if( fetch->state == OD_FETCH_QUEUED )
            g_od_requeued++;
        else
            g_od_failed++;
    }
}

/*
 * One chunk header, complete. Returns -1 when the two ends have lost sync --
 * a chunk for a file nobody asked for, or a total that changed mid-file --
 * and the caller drops the connection rather than decode noise.
 */
static int
od_rx_begin_chunk(struct PlatformXIOOnDemand* od)
{
    unsigned char const* h = od->rx_header;
    int const archive = h[0];
    int const file = (h[1] << 8) | h[2];
    int const total = (h[3] << 8) | h[4];
    int const part = h[5];
    int index = od_fetch_find(od, archive, file);
    struct OdFetch* fetch;
    int offset;
    int count;

    od->rx_header_have = 0;
    if( index < 0 )
    {
        TORIRS_ERR("ondemand: server sent %d/%d, which nothing asked for\n", archive, file);
        return -1;
    }
    fetch = &od->fetches[index];
    if( fetch->state != OD_FETCH_INFLIGHT )
    {
        TORIRS_ERR("ondemand: a chunk of %d/%d arrived while it was not in flight\n",
            archive,
            file);
        return -1;
    }

    if( total == 0 )
    {
        fetch->state = OD_FETCH_ABSENT;
        return 0;
    }
    if( fetch->total < 0 )
    {
        fetch->total = total;
        fetch->data = malloc((size_t)total);
        assert(fetch->data);
    }
    else if( fetch->total != total )
    {
        TORIRS_ERR("ondemand: %d/%d changed size mid-file (%d then %d)\n",
            archive,
            file,
            fetch->total,
            total);
        return -1;
    }

    offset = part * OD_CHUNK_PAYLOAD;
    if( offset < 0 || offset >= total )
        return -1;
    count = total - offset;
    if( count > OD_CHUNK_PAYLOAD )
        count = OD_CHUNK_PAYLOAD;

    od->rx_fetch = index;
    od->rx_offset = offset;
    od->rx_count = count;
    od->rx_got = 0;
    return 0;
}

/* Consume `n` bytes off the wire, whatever the socket happened to hand over. */
static int
od_rx_feed(
    struct PlatformXIOOnDemand* od,
    const unsigned char* bytes,
    int n)
{
    int at = 0;

    while( at < n )
    {
        if( od->rx_fetch < 0 )
        {
            int take = 6 - od->rx_header_have;
            if( take > n - at )
                take = n - at;
            memcpy(od->rx_header + od->rx_header_have, bytes + at, (size_t)take);
            od->rx_header_have += take;
            at += take;
            if( od->rx_header_have == 6 && od_rx_begin_chunk(od) != 0 )
                return -1;
            continue;
        }

        {
            struct OdFetch* fetch = &od->fetches[od->rx_fetch];
            int take = od->rx_count - od->rx_got;
            if( take > n - at )
                take = n - at;
            memcpy(fetch->data + od->rx_offset + od->rx_got, bytes + at, (size_t)take);
            od->rx_got += take;
            at += take;
            if( od->rx_got == od->rx_count )
            {
                fetch->received += od->rx_count;
                if( fetch->received >= fetch->total )
                    fetch->state = OD_FETCH_DONE;
                od->rx_fetch = -1;
            }
        }
    }
    return 0;
}

static int
od_any_fetch_in(
    struct PlatformXIOOnDemand* od,
    enum OdFetchState state)
{
    for( int i = 0; i < od->fetch_count; i++ )
        if( od->fetches[i].state == state )
            return 1;
    return 0;
}

/*
 * Move the wire as far as it will go without waiting: dial if something is
 * queued and nothing is open, write every queued request, read every chunk
 * that has arrived, and give up on a wire that has gone quiet.
 */
static void
od_pump(struct PlatformXIOOnDemand* od)
{
    unsigned char scratch[8192];

    assert(od);

    if( od_any_fetch_in(od, OD_FETCH_QUEUED) )
    {
        if( od_open_files(od) != 0 )
        {
            /* Shuttered, or the dial failed: nothing queued can be served.
             * Fail it now rather than leave callers polling a wire that is
             * not going to open for them. */
            for( int i = 0; i < od->fetch_count; i++ )
                if( od->fetches[i].state == OD_FETCH_QUEUED )
                    od->fetches[i].state = OD_FETCH_FAILED;
            return;
        }
        for( int i = 0; i < od->fetch_count; i++ )
        {
            struct OdFetch* fetch = &od->fetches[i];
            unsigned char request[4];
            if( fetch->state != OD_FETCH_QUEUED )
                continue;
            request[0] = (unsigned char)fetch->archive;
            request[1] = (unsigned char)(fetch->file >> 8);
            request[2] = (unsigned char)fetch->file;
            request[3] = OD_PRIORITY_URGENT;
            od_tx_append(od, request, 4);
            fetch->attempts++;
            fetch->state = OD_FETCH_INFLIGHT;
        }
        od->wire_last_activity = time(NULL);
    }

    if( !od->files )
        return;

    while( od->tx_pos < od->tx_len )
    {
        int sent = sockstream_send(od->files, od->tx + od->tx_pos, od->tx_len - od->tx_pos);
        if( sent > 0 )
        {
            od->tx_pos += sent;
            od->wire_last_activity = time(NULL);
            continue;
        }
        if( sent == SOCKSTREAM_ERROR_NODATA || sent == SOCKSTREAM_ERROR_WOULDBLOCK )
            break;
        od_wire_drop(od, 0);
        return;
    }
    if( od->tx_pos == od->tx_len )
    {
        od->tx_pos = 0;
        od->tx_len = 0;
    }

    for( ;; )
    {
        int got;
        if( !od_any_fetch_in(od, OD_FETCH_INFLIGHT) )
            break;
        got = sockstream_recv(od->files, scratch, (int)sizeof(scratch));
        if( got > 0 )
        {
            od->wire_last_activity = time(NULL);
            if( od_rx_feed(od, scratch, got) != 0 )
            {
                od_wire_drop(od, 0);
                return;
            }
            continue;
        }
        if( got == SOCKSTREAM_ERROR_NODATA || got == SOCKSTREAM_ERROR_WOULDBLOCK )
            break;
        /* Closed or failed. DEAD is routine -- see od_wire_drop -- and costs
         * nothing, because a closed socket fails at once. */
        od_wire_drop(od, 0);
        return;
    }

    if( od_any_fetch_in(od, OD_FETCH_INFLIGHT) &&
        time(NULL) - od->wire_last_activity > OD_READ_TIMEOUT_SEC )
    {
        TORIRS_ERR("ondemand: %s:%d went quiet for %ds with reads in flight; shuttering it "
            "for %ds\n",
            od->host,
            od->game_port,
            OD_READ_TIMEOUT_SEC,
            OD_ENDPOINT_SHUTTER_SEC);
        od_wire_drop(od, 1);
    }
}

int
PlatformXIOOnDemand_FetchBegin(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id)
{
    int index;

    assert(od);
    assert(table_id > RSCACHE_DAT1_DISK_TABLE_CONFIGS);
    assert(table_id <= RSCACHE_DAT1_DISK_TABLE_MAPS);

    if( archive_id < 0 || archive_id > 0xFFFF )
        return -1;
    /* A shuttered endpoint answers no at once, so a rebuild against a dead
     * server costs one timeout and not one per file. */
    if( od->files_shutter != 0 && time(NULL) < od->files_shutter )
        return -1;

    index = od_fetch_find(od, table_id - 1, archive_id);
    if( index < 0 )
        index = od_fetch_add(od, table_id - 1, archive_id);
    od->fetches[index].waiters++;
    return 0;
}

void
PlatformXIOOnDemand_Pump(struct PlatformXIOOnDemand* od)
{
    od_pump(od);
}

int
PlatformXIOOnDemand_FetchPending(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id)
{
    int index;

    assert(od);
    index = od_fetch_find(od, table_id - 1, archive_id);
    if( index < 0 )
        return 0;
    return od->fetches[index].state == OD_FETCH_QUEUED ||
           od->fetches[index].state == OD_FETCH_INFLIGHT;
}

int
PlatformXIOOnDemand_FetchTake(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    char** out_data,
    int* out_size)
{
    int index;
    struct OdFetch* fetch;

    assert(od);
    assert(out_data);
    assert(out_size);

    *out_data = NULL;
    *out_size = 0;
    index = od_fetch_find(od, table_id - 1, archive_id);
    /* Nothing by that name: it was never begun, or every waiter has taken
     * it. Either way there is nothing to wait for. */
    if( index < 0 )
        return 1;
    fetch = &od->fetches[index];
    if( fetch->state == OD_FETCH_QUEUED || fetch->state == OD_FETCH_INFLIGHT )
        return 0;

    if( fetch->state == OD_FETCH_DONE )
    {
        if( fetch->waiters > 1 )
        {
            *out_data = malloc((size_t)fetch->total);
            assert(*out_data);
            memcpy(*out_data, fetch->data, (size_t)fetch->total);
        }
        else
        {
            *out_data = fetch->data;
            fetch->data = NULL;
        }
        *out_size = fetch->total;
    }
    if( --fetch->waiters <= 0 )
        od_fetch_remove(od, index);
    return 1;
}

/*
 * The pump loop the blocking callers share: begin, then pump until the fetch
 * settles, sleeping on the socket between pumps rather than spinning. The
 * pump owns every deadline, so a wire that goes quiet resolves as FAILED here
 * exactly as it does for a parked read.
 */
static char*
od_fetch_file_blocking(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    int* out_size)
{
    char* data = NULL;

    assert(out_size);
    *out_size = 0;
    if( PlatformXIOOnDemand_FetchBegin(od, table_id, archive_id) != 0 )
        return NULL;
    for( ;; )
    {
        od_pump(od);
        if( PlatformXIOOnDemand_FetchTake(od, table_id, archive_id, &data, out_size) )
            return data;
        if( od->files )
            od_wait(od->files, /* for_write */ 0, 1);
    }
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
    od->rx_fetch = -1;
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
    for( int i = 0; i < od->fetch_count; i++ )
        free(od->fetches[i].data);
    free(od->fetches);
    free(od->tx);
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
    fprintf(stderr,
            "od_stats: wire drops=%ld requeued=%ld failed=%ld\n",
            g_od_drops, g_od_requeued, g_od_failed);
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
        char* data = od_fetch_file_blocking(od, table_id, archive_id, out_size);
        if( !data )
            return NULL;
        od_tally(table_id, archive_id, *out_size);
        /* Stored as served: still gzipped. The decode is the reader's, which
         * is the whole difference between this and ArchiveLoad. */
        *out_format = RSCACHE_ARCHIVE_FORMAT_DAT;
        return (uint8_t*)data;
    }
}

int
PlatformXIOOnDemand_ContainerFetchMany(
    struct PlatformXIOOnDemand* od,
    int count,
    const int* table_ids,
    const int* archive_ids,
    uint8_t** out_data,
    int* out_sizes,
    int* out_formats)
{
    int served = 0;
    /* Which entries are on the wire; the rest were answered in line or
     * refused, and are not waited for. */
    unsigned char* on_wire;

    assert(od);
    assert(count >= 0);
    assert(table_ids);
    assert(archive_ids);
    assert(out_data);
    assert(out_sizes);
    assert(out_formats);

    on_wire = calloc((size_t)(count > 0 ? count : 1), 1);
    assert(on_wire);

    /*
     * Every wire file first, so all of them are on the socket before anything
     * waits; the jag archives are HTTP and answered in line as they come.
     */
    for( int i = 0; i < count; i++ )
    {
        out_data[i] = NULL;
        out_sizes[i] = 0;
        out_formats[i] = RSCACHE_ARCHIVE_FORMAT_DAT;
        if( table_ids[i] > RSCACHE_DAT1_DISK_TABLE_CONFIGS &&
            table_ids[i] <= RSCACHE_DAT1_DISK_TABLE_MAPS )
            on_wire[i] = PlatformXIOOnDemand_FetchBegin(od, table_ids[i], archive_ids[i]) == 0;
    }
    for( int i = 0; i < count; i++ )
    {
        if( table_ids[i] == RSCACHE_DAT1_DISK_TABLE_CONFIGS )
        {
            out_data[i] = PlatformXIOOnDemand_ContainerFetch(
                od, table_ids[i], archive_ids[i], &out_formats[i], &out_sizes[i]);
            if( out_data[i] )
                served++;
        }
    }
    for( ;; )
    {
        int waiting = 0;

        od_pump(od);
        for( int i = 0; i < count; i++ )
        {
            char* data = NULL;
            int size = 0;

            if( !on_wire[i] )
                continue;
            if( !PlatformXIOOnDemand_FetchTake(od, table_ids[i], archive_ids[i], &data, &size) )
            {
                waiting = 1;
                continue;
            }
            on_wire[i] = 0;
            if( data )
            {
                od_tally(table_ids[i], archive_ids[i], size);
                out_data[i] = (uint8_t*)data;
                out_sizes[i] = size;
                served++;
            }
        }
        if( !waiting )
            break;
        if( od->files )
            od_wait(od->files, /* for_write */ 0, 1);
    }
    free(on_wire);
    return served;
}

/*
 * The last step of a wire read: what ArchiveLoad does with the bytes once
 * they have arrived, shared with the parked path so a blocking read and a
 * parked one produce the same archive from the same bytes.
 *
 * Takes ownership of `data`.
 */
static struct RSCache_Dat1DiskArchive*
od_archive_from_wire(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    char* data,
    int size)
{
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_Dat2DiskArchive raw = { 0 };

    raw.data = data;
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
    if( !RSCache_ArchiveDecompressDat(&raw, RSCACHE_ARCHIVE_FORMAT_DAT) )
    {
        TORIRS_LOG("ondemand: table %d file %d did not decompress (%d bytes)\n",
            table_id,
            archive_id,
            raw.data_size);
        free(raw.data);
        return NULL;
    }

    archive = malloc(sizeof(struct RSCache_Dat1DiskArchive));
    assert(archive);
    memset(archive, 0, sizeof(struct RSCache_Dat1DiskArchive));
    archive->data = raw.data;
    archive->data_size = raw.data_size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;
    archive->format = RSCACHE_ARCHIVE_FORMAT_DAT;
    return archive;
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
        char* data;

        if( table_id > RSCACHE_DAT1_DISK_TABLE_MAPS )
            return NULL;
        {
            clock_t const t0 = clock();
            data = od_fetch_file_blocking(od, table_id, archive_id, &size);
            g_od_fetch_ms += (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        }
        if( !data )
            return NULL;
        return od_archive_from_wire(od, table_id, archive_id, data, size);
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
PlatformXIOOnDemand_ArchiveLoadBegin(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    struct RSCache_Dat1DiskArchive** out_archive)
{
    assert(od);
    assert(table_id >= 0);
    assert(archive_id >= 0);
    assert(out_archive);

    /* The local copy, and the jag archives (HTTP, nine per boot, before
     * anything can be parked): both answered here and now, exactly as the
     * blocking load answers them. */
    *out_archive = od_cache_load(od, table_id, archive_id);
    if( *out_archive )
        return 1;
    if( table_id == RSCACHE_DAT1_DISK_TABLE_CONFIGS )
    {
        *out_archive = PlatformXIOOnDemand_ArchiveLoad(od, table_id, archive_id);
        return 1;
    }
    if( table_id > RSCACHE_DAT1_DISK_TABLE_MAPS )
        return 1;
    if( PlatformXIOOnDemand_FetchBegin(od, table_id, archive_id) != 0 )
        return 1;
    return 0;
}

int
PlatformXIOOnDemand_ArchiveLoadPoll(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    struct RSCache_Dat1DiskArchive** out_archive)
{
    char* data = NULL;
    int size = 0;

    assert(od);
    assert(out_archive);

    *out_archive = NULL;
    if( !PlatformXIOOnDemand_FetchTake(od, table_id, archive_id, &data, &size) )
        return 0;
    if( data )
        *out_archive = od_archive_from_wire(od, table_id, archive_id, data, size);
    return 1;
}

struct PlatformXIOOnDemand*
PlatformXIOOnDemand_NewWireOnly(
    const char* host,
    int game_port)
{
    struct PlatformXIOOnDemand* od;

    assert(host);
    if( sockstream_init() != 0 )
        return NULL;

    od = malloc(sizeof(struct PlatformXIOOnDemand));
    assert(od);
    memset(od, 0, sizeof(struct PlatformXIOOnDemand));
    snprintf(od->host, sizeof(od->host), "%s", host);
    od->rx_fetch = -1;
    od->game_port = game_port > 0 ? game_port : OD_DEFAULT_GAME_PORT;
    od->web_port = OD_DEFAULT_WEB_PORT;

    if( od_open_files(od) != 0 )
    {
        free(od);
        return NULL;
    }
    return od;
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

int
PlatformXIOOnDemand_JagChecksumsRefresh(
    struct PlatformXIOOnDemand* od,
    int32_t out[9])
{
    char* body = NULL;
    int size = 0;
    int32_t fresh[9];

    assert(od);
    assert(out);

    /*
     * A fresh `GET /crc`, bypassing the session cache.
     *
     * The cached table is right for ROUTES -- nine reads per boot, one fetch
     * -- and wrong for LOGIN: the server repacks whenever its content
     * changes, and a client that has been sitting at the title since before
     * the repack would send boot-time sums with every attempt, earning a
     * reply=6 loop that no amount of retrying leaves. One extra HTTP GET per
     * Login click is what it costs to make the retry advice true.
     *
     * On a changed answer the session table is updated too, so later archive
     * fetches build routes the repacked server still answers. IO workers may
     * read the table concurrently; a torn read there costs one 404 and a
     * refetch, which is the same failure a genuinely stale route already
     * produces.
     */
    body = od_http_get(od, "/crc", &size);
    if( !body || size < 9 * 4 )
    {
        free(body);
        /* Unreachable right now: the cached table (if any) is still the best
         * answer, and login will say out-of-date if it is stale. */
        if( !od->jag_crc_valid )
            return -1;
        memcpy(out, od->jag_crc, sizeof(od->jag_crc));
        return 0;
    }
    for( int i = 0; i < 9; i++ )
    {
        const unsigned char* p = (const unsigned char*)body + i * 4;
        fresh[i] =
            (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                      (uint32_t)p[3]);
    }
    free(body);

    if( od->jag_crc_valid && memcmp(fresh, od->jag_crc, sizeof(fresh)) != 0 )
        TORIRS_ERR("ondemand: the cache server repacked since boot; refreshed the login "
            "checksums\n");
    memcpy(od->jag_crc, fresh, sizeof(od->jag_crc));
    od->jag_crc_valid = 1;
    memcpy(out, od->jag_crc, sizeof(od->jag_crc));
    return 0;
}
