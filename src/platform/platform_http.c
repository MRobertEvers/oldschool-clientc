/*
 * See platform_http.h. Lifted out of platform_x_io_ondemand.c, which had the
 * only copy, once a second caller wanted the same thing.
 */

#include "platform/platform_http.h"

#include "platform/sockstream.h"
#include "log/torirs_log.h"

#include <assert.h>
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

#define HTTP_CONNECT_TIMEOUT_SEC 5
#define HTTP_READ_TIMEOUT_SEC 10

static int
http_wait_readable(struct SockStream* stream, int timeout_sec)
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

static struct SockStream*
http_dial(const char* host, int port)
{
    struct SockStream* stream = sockstream_new();

    if( !stream )
        return NULL;

    sockstream_connect(stream, host, port, HTTP_CONNECT_TIMEOUT_SEC);
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

static int
http_write_all(struct SockStream* stream, const char* data, int size)
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
    }
    return 0;
}

char*
Platform_HttpGetStatus(
    const char* host,
    int port,
    const char* route,
    int* out_size,
    int* out_status)
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

    assert(host);
    assert(route);
    assert(out_size);

    *out_size = 0;
    if( out_status )
        *out_status = 0;

    stream = http_dial(host, port);
    if( !stream )
        return NULL;

    snprintf(
        request,
        sizeof(request),
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: torirs\r\nAccept: */*\r\n\r\n",
        route,
        host,
        port);
    if( http_write_all(stream, request, (int)strlen(request)) != 0 )
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
        if( !http_wait_readable(stream, HTTP_READ_TIMEOUT_SEC) )
            goto done;
    }

    if( length < 12 || memcmp(buffer, "HTTP/1.", 7) != 0 )
        goto done;

    /* The server answered something, whatever it was. Recorded before the 200
     * test so a caller can tell a refusal from silence. */
    if( out_status )
        *out_status = (buffer[9] - '0') * 100 + (buffer[10] - '0') * 10 + (buffer[11] - '0');
    if( memcmp(buffer + 9, "200", 3) != 0 )
        goto done;

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
         * the header block rather than the whole response -- the body is binary
         * and must not be touched. */
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

char*
Platform_HttpGet(
    const char* host,
    int port,
    const char* route,
    int* out_size)
{
    return Platform_HttpGetStatus(host, port, route, out_size, NULL);
}
