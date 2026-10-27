/*
 * See platform_x_http.h. Lifted out of platform_x_io_ondemand.c, which had the
 * only copy, once a second caller wanted the same thing.
 */

#include "platform/platform_x_http.h"
#include "mem/torirs_mem.h"

#include "platform/sockstream.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#endif

#define HTTP_CONNECT_TIMEOUT_SEC 5
#define HTTP_READ_TIMEOUT_SEC 10

/**
 * Sleep until the socket can be read (or written), or the timeout runs out.
 *
 * The point of it is the SLEEP. sockstream is non-blocking, so a retry loop
 * over it is a busy-wait unless something parks the thread in between -- which
 * is what the dial and the write below were, each in its own way.
 */
static int
http_wait(struct SockStream* stream, int for_write, int timeout_sec)
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
     * the exception fd rather than the write fd. */
    if( for_write )
        result = select((int)fd + 1, NULL, &ready, &failed, &tv);
    else
        result = select((int)fd + 1, &ready, NULL, NULL, &tv);
    return result > 0;
}

/**
 * Connect, within `connect_sec`.
 *
 * That bound is this function's to keep and nobody else's: sockstream_connect
 * takes the number and discards it (`(void)timeout_sec`), and
 * sockstream_poll_connect selects with a zero timeout. The loop that used to
 * be here therefore had no deadline at all -- it polled as fast as the CPU
 * allowed for as long as the OS kept the SYN alive, which against a host that
 * black-holes rather than refuses is a pinned core and a caller that never
 * returns. Every config file, plugin script and jag archive on the remote
 * lanes comes through here.
 */
static struct SockStream*
http_dial(const char* host, int port, int connect_sec)
{
    struct SockStream* stream = sockstream_new();
    time_t deadline;
    int state = SOCKSTREAM_CONNECT_FAILED;

    if( !stream )
        return NULL;

    sockstream_connect(stream, host, port, connect_sec);
    deadline = time(NULL) + connect_sec;
    for( ;; )
    {
        state = sockstream_poll_connect(stream);
        if( state != SOCKSTREAM_CONNECT_INFLIGHT )
            break;
        if( time(NULL) >= deadline )
        {
            TORIRS_ERR("http: %s:%d did not answer in %ds\n", host, port, connect_sec);
            state = SOCKSTREAM_CONNECT_FAILED;
            break;
        }
        /* A second at a time rather than the whole remaining budget, so the
         * poll -- which is what classifies success and failure -- runs again
         * promptly once the socket moves. */
        http_wait(stream, /* for_write */ 1, 1);
    }

    if( state == SOCKSTREAM_CONNECT_SUCCESS )
        return stream;

    sockstream_close(stream);
    sockstream_free(stream);
    return NULL;
}

static int
http_write_all(struct SockStream* stream, const char* data, int size, int timeout_sec)
{
    int written = 0;

    while( written < size )
    {
        int sent = sockstream_send(stream, data + written, size - written);
        if( sent > 0 )
        {
            written += sent;
            continue;
        }
        if( sent != SOCKSTREAM_ERROR_NODATA && sent != SOCKSTREAM_ERROR_WOULDBLOCK )
            return -1;
        /* The same wait the read loop does. A peer that has stopped draining
         * leaves send() answering WOULDBLOCK for as long as it stays that way,
         * and retrying on the spot was a tight loop with no wait in it and no
         * deadline on it at all. */
        if( !http_wait(stream, /* for_write */ 1, timeout_sec) )
            return -1;
    }
    return 0;
}

/*
 * Where the header block ends, or -1 while it is still arriving.
 *
 * memmem is not portable and the header block is small, so this is a scan --
 * resumed from `*scan_from` rather than restarted, because it runs once per
 * recv and a restart would make reading a large body quadratic.
 */
static int
http_header_size(const char* buf, int len, int* scan_from)
{
    int i = *scan_from;

    assert(buf);
    assert(scan_from);

    for( ; i + 4 <= len; i++ )
    {
        if( memcmp(buf + i, "\r\n\r\n", 4) == 0 )
            return i + 4;
    }
    /* The terminator may straddle this recv and the next, so back up over the
     * three bytes a partial match could occupy. */
    *scan_from = i > 3 ? i - 3 : 0;
    return -1;
}

/*
 * How the response framed itself: Content-Length, or -1 when it did not frame
 * itself at all and the body is "everything until the socket closes".
 */
static int
http_framed_length(const char* buf, int header_size, int* out_chunked)
{
    char* headers = ToriMem_Alloc(ToriMem_Net(), (size_t)header_size + 1);
    char* found;
    int content_length = -1;

    assert(buf);
    assert(out_chunked);

    /* Header names are case-insensitive on the wire. Lowercase a copy of the
     * header block rather than the whole response -- the body is binary and
     * must not be touched. */
    for( int i = 0; i < header_size; i++ )
    {
        char c = buf[i];
        headers[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    headers[header_size] = '\0';

    found = strstr(headers, "\r\ncontent-length:");
    if( found )
        content_length = atoi(found + strlen("\r\ncontent-length:"));
    found = strstr(headers, "\r\ntransfer-encoding:");
    *out_chunked = found && strstr(found, "chunked") ? 1 : 0;
    ToriMem_Free(ToriMem_Net(), headers);
    return content_length;
}

/*
 * One request and its response's body. `head` is the whole request line and
 * headers; `payload` follows it. Only a 200's body is returned unless
 * `any_status`, for a caller (a POST) whose refusals carry a body worth reading.
 */
static char*
http_exchange(
    const char* host,
    int port,
    const char* head,
    const char* payload,
    int payload_size,
    int any_status,
    int* out_size,
    int* out_status,
    int connect_sec,
    int read_sec)
{
    struct SockStream* stream = NULL;
    char* buffer = NULL;
    int capacity = 0;
    int length = 0;
    char* body = NULL;
    int header_size = -1;
    int header_scan = 0;
    int body_size = 0;
    int content_length = -1;
    int chunked = 0;
    char* result = NULL;

    assert(host);
    assert(head);
    assert(out_size);

    *out_size = 0;
    if( out_status )
        *out_status = 0;

    stream = http_dial(host, port, connect_sec);
    if( !stream )
        return NULL;

    if( http_write_all(stream, head, (int)strlen(head), read_sec) != 0 )
        goto done;
    if( payload_size > 0 && http_write_all(stream, payload, payload_size, read_sec) != 0 )
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
            bigger = ToriMem_Realloc(ToriMem_Net(), buffer, (size_t)grown);
            buffer = bigger;
            capacity = grown;
        }
        got = sockstream_recv(stream, buffer + length, capacity - length);
        if( got > 0 )
        {
            length += got;
            if( header_size < 0 )
            {
                header_size = http_header_size(buffer, length, &header_scan);
                if( header_size > 0 )
                    content_length = http_framed_length(buffer, header_size, &chunked);
            }
            /*
             * A response that FRAMED itself is complete the moment its body is,
             * and waiting past that point is not a formality -- it is the whole
             * read timeout, spent waiting for a close that a keep-alive server
             * will never send, ending in a discarded response. io_server
             * answers `Connection: keep-alive` to every request including this
             * HTTP/1.0 one, so before this every desktop read of a config file,
             * a plugin script or a plugin asset stalled ten seconds and then
             * returned nothing.
             *
             * Chunked has no length to compare against and still reads to the
             * close below; nothing this client talks to sends one.
             */
            if( header_size > 0 && !chunked && content_length >= 0 &&
                length - header_size >= content_length )
                break;
            continue;
        }
        if( got == SOCKSTREAM_ERROR_CLOSED )
            break;
        if( got != SOCKSTREAM_ERROR_NODATA && got != SOCKSTREAM_ERROR_WOULDBLOCK )
            goto done;
        /* A timeout keeps what arrived rather than dropping it: an unframed
         * response that stopped short is still framed below, where a short body
         * is refused on its own terms. */
        if( !http_wait(stream, /* for_write */ 0, read_sec) )
            break;
    }

    if( length < 12 || memcmp(buffer, "HTTP/1.", 7) != 0 )
        goto done;

    /* The server answered something, whatever it was. Recorded before the 200
     * test so a caller can tell a refusal from silence. */
    if( out_status )
        *out_status = (buffer[9] - '0') * 100 + (buffer[10] - '0') * 10 + (buffer[11] - '0');
    if( !any_status && memcmp(buffer + 9, "200", 3) != 0 )
        goto done;

    /* Both may already be known -- the loop needs them to decide when to stop.
     * A response that arrived in one recv, or one that never framed itself, is
     * measured here instead. */
    if( header_size < 0 )
    {
        header_scan = 0;
        header_size = http_header_size(buffer, length, &header_scan);
        if( header_size > 0 )
            content_length = http_framed_length(buffer, header_size, &chunked);
    }
    if( header_size < 0 )
        goto done;

    body = buffer + header_size;
    body_size = length - header_size;

    if( chunked )
    {
        /* Decode in place: a chunk's data always starts further into the buffer
         * than the byte it moves to, so the write cursor can never overtake the
         * read cursor. */
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

    result = ToriMem_Alloc(ToriMem_Net(), (size_t)(body_size ? body_size : 1));
    memcpy(result, body, (size_t)body_size);
    *out_size = body_size;

done:
    ToriMem_Free(ToriMem_Net(), buffer);
    if( stream )
    {
        sockstream_close(stream);
        sockstream_free(stream);
    }
    return result;
}

char*
PlatformX_HttpGetTimed(
    const char* host,
    int port,
    const char* route,
    int* out_size,
    int* out_status,
    int connect_sec,
    int read_sec)
{
    char request[512];

    assert(route);
    snprintf(
        request,
        sizeof(request),
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: torirs\r\nAccept: */*\r\n"
        "Connection: close\r\n\r\n",
        route,
        host,
        port);
    return http_exchange(host, port, request, NULL, 0, /* any_status */ 0, out_size,
                         out_status, connect_sec, read_sec);
}

char*
PlatformX_HttpPostForm(
    const char* host,
    int port,
    const char* route,
    const char* cookie,
    const char* body,
    int body_size,
    int* out_size,
    int* out_status)
{
    char request[1024];

    assert(route);
    assert(body || body_size == 0);
    snprintf(
        request,
        sizeof(request),
        "POST %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: torirs\r\nAccept: application/json\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n"
        "%s%s%sConnection: close\r\n\r\n",
        route,
        host,
        port,
        body_size,
        cookie ? "Cookie: " : "",
        cookie ? cookie : "",
        cookie ? "\r\n" : "");
    return http_exchange(host, port, request, body, body_size, /* any_status */ 1, out_size,
                         out_status, HTTP_CONNECT_TIMEOUT_SEC, HTTP_READ_TIMEOUT_SEC);
}

/* The general timeouts: short, because a config or plugin read that hangs
 * hangs the thing that asked for it. The cache client passes its own. */
char*
PlatformX_HttpGetStatus(
    const char* host,
    int port,
    const char* route,
    int* out_size,
    int* out_status)
{
    return PlatformX_HttpGetTimed(
        host, port, route, out_size, out_status,
        HTTP_CONNECT_TIMEOUT_SEC, HTTP_READ_TIMEOUT_SEC);
}

char*
PlatformX_HttpGet(
    const char* host,
    int port,
    const char* route,
    int* out_size)
{
    return PlatformX_HttpGetStatus(host, port, route, out_size, NULL);
}
