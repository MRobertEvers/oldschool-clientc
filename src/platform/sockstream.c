#include "sockstream.h"
#include <assert.h>

#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#ifndef EAGAIN
#define EAGAIN WSAEWOULDBLOCK
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#endif
#define MSG_DONTWAIT 0
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#endif

#ifdef _WIN32
/* inet_pton is not declared when _WIN32_WINNT < 0x0600 (e.g. WinXP targets). */
static int
sockstream_ipv4_from_string(const char* host, struct in_addr* addr)
{
    struct sockaddr_in sa;
    INT sa_len = (INT)sizeof(sa);

    if( WSAStringToAddressA((LPSTR)host, AF_INET, NULL, (struct sockaddr*)&sa, &sa_len)
        == SOCKET_ERROR )
    {
        return 0;
    }
    *addr = sa.sin_addr;
    return 1;
}
#endif

int
sockstream_init(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    if( WSAStartup(MAKEWORD(2, 2), &wsaData) != 0 )
    {
        return -1;
    }
#endif

    return 0;
}

void
sockstream_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

struct SockStream
{
#ifdef _WIN32
    SOCKET sockfd;
#else
    int sockfd;
#endif
    int status;
};

static int
sockstream_has_socket(const struct SockStream* stream)
{
#ifdef _WIN32
    return stream && stream->sockfd != INVALID_SOCKET;
#else
    return stream && stream->sockfd >= 0;
#endif
}

static void
sockstream_clear_socket(struct SockStream* stream)
{
#ifdef _WIN32
    stream->sockfd = INVALID_SOCKET;
#else
    stream->sockfd = -1;
#endif
}

struct SockStream*
sockstream_new(void)
{
    struct SockStream* stream = malloc(sizeof(struct SockStream));
    assert(stream);
    memset(stream, 0, sizeof(struct SockStream));
    sockstream_clear_socket(stream);
    stream->status = SOCKSTREAM_STATUS_IDLE;
    return stream;
}

void
sockstream_connect(
    struct SockStream* stream,
    const char* host,
    int port,
    int timeout_sec)
{
    if( port <= 0 )
        return;
    assert(host);
    assert(stream);
    memset(stream, 0, sizeof(struct SockStream));
    sockstream_clear_socket(stream);
    stream->status = SOCKSTREAM_STATUS_CONNECTING;
    (void)timeout_sec;

    // Create socket
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if( sock == INVALID_SOCKET )
    {
        TORIRS_ERR("Failed to create socket: %d\n", WSAGetLastError());
        return;
    }
    stream->sockfd = sock;
#else
    stream->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( stream->sockfd < 0 )
    {
        TORIRS_ERR("Failed to create socket: %s\n", strerror(errno));
        return;
    }
#endif

    // Set socket to non-blocking
#ifdef _WIN32
    u_long mode = 1;
    if( ioctlsocket(stream->sockfd, FIONBIO, &mode) != 0 )
    {
        TORIRS_ERR("Failed to set non-blocking: %d\n", WSAGetLastError());
        closesocket(stream->sockfd);
        /* The caller owns the stream and will close/free it after poll reports
         * failure. Freeing here leaves that caller with a dangling pointer. */
        sockstream_clear_socket(stream);
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return;
    }
#else
    int flags = fcntl(stream->sockfd, F_GETFL, 0);
    if( flags < 0 || fcntl(stream->sockfd, F_SETFL, flags | O_NONBLOCK) < 0 )
    {
        TORIRS_ERR("Failed to set non-blocking: %s\n", strerror(errno));
        close(stream->sockfd);
        sockstream_clear_socket(stream);
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return;
    }
#endif

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

#ifndef _WIN32
    if( inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0 )
#else
    if( !sockstream_ipv4_from_string(host, &server_addr.sin_addr) )
#endif
    {
        /* Not an IPv4 literal — resolve as a hostname (getaddrinfo is
         * available on POSIX and WinSock2 alike). */
        struct addrinfo hints;
        struct addrinfo* res = NULL;
        int resolved = 0;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if( getaddrinfo(host, NULL, &hints, &res) == 0 && res )
        {
            server_addr.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
            resolved = 1;
        }
        if( res )
            freeaddrinfo(res);
        if( !resolved )
        {
            TORIRS_ERR("Invalid address: %s\n", host);
#ifdef _WIN32
            closesocket(stream->sockfd);
#else
            close(stream->sockfd);
#endif
            sockstream_clear_socket(stream);
            stream->status = SOCKSTREAM_STATUS_ERROR;
            return;
        }
    }

    // Try to connect (non-blocking)
    int result = connect(stream->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
#ifdef _WIN32
    if( result == 0 )
    {
        // Connection succeeded immediately
        stream->status = SOCKSTREAM_STATUS_CONNECTED;
        TORIRS_LOG("Connected to %s:%d\n", host, port);
        return;
    }
    if( result == SOCKET_ERROR )
    {
        int connect_err = WSAGetLastError();
        if( connect_err != WSAEINPROGRESS && connect_err != WSAEWOULDBLOCK )
        {
            TORIRS_ERR("Failed to connect: %d\n", connect_err);
            closesocket(stream->sockfd);
            sockstream_clear_socket(stream);
            stream->status = SOCKSTREAM_STATUS_ERROR;
            return;
        }
    }
    // Connection in progress - return stream, caller should poll with sockstream_poll_connect
    TORIRS_LOG("Connection in progress to %s:%d\n", host, port);
    return;
#else
    if( result == 0 )
    {
        // Connection succeeded immediately
        stream->status = SOCKSTREAM_STATUS_CONNECTED;
        TORIRS_LOG("Connected to %s:%d\n", host, port);
        return;
    }
    if( result < 0 && errno != EINPROGRESS )
    {
        TORIRS_ERR("Failed to connect: %s\n", strerror(errno));
        close(stream->sockfd);
        sockstream_clear_socket(stream);
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return;
    }
    // Connection in progress - return stream, caller should poll with sockstream_poll_connect
    TORIRS_LOG("Connection in progress to %s:%d\n", host, port);
    return;
#endif
}

int
sockstream_lasterror(struct SockStream* stream)
{
    (void)stream;
    assert(stream);
#ifdef _WIN32
    int error = WSAGetLastError();
    if( error == WSAEWOULDBLOCK )
    {
        return SOCKSTREAM_ERROR_WOULDBLOCK;
    }
    else
    {
        return SOCKSTREAM_ERROR;
    }
#else
    int error = errno;
    if( error == EAGAIN || error == EWOULDBLOCK )
    {
        return SOCKSTREAM_ERROR_WOULDBLOCK;
    }
    else
    {
        return SOCKSTREAM_ERROR;
    }
#endif
}

char*
sockstream_strerror(int error)
{
    switch( error )
    {
    case SOCKSTREAM_ERROR_WOULDBLOCK:
        return "WOULDBLOCK";
    case SOCKSTREAM_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

int
sockstream_send(
    struct SockStream* stream,
    const void* buffer,
    int size)
{
    if( !stream || stream->status != SOCKSTREAM_STATUS_CONNECTED || !sockstream_has_socket(stream) || !buffer ||
        size <= 0 )
    {
        TORIRS_ERR("Socket send error: invalid stream\n");
        return -1;
    }

    int sent = send(stream->sockfd, (const char*)buffer, size, 0);
    if( sent < 0 )
    {
#ifdef _WIN32
        int send_err = WSAGetLastError();
        if( send_err != WSAEWOULDBLOCK )
        {
            TORIRS_ERR("Socket send error: %d\n", send_err);
            stream->status = SOCKSTREAM_STATUS_ERROR;
        }
#else
        if( errno != EAGAIN && errno != EWOULDBLOCK )
        {
            TORIRS_ERR("Socket send error: %s\n", strerror(errno));
            stream->status = SOCKSTREAM_STATUS_ERROR;
        }
#endif
    }

    return sent;
}

int
sockstream_recv(
    struct SockStream* stream,
    void* buffer,
    int size)
{
    if( !stream || stream->status != SOCKSTREAM_STATUS_CONNECTED || !sockstream_has_socket(stream) || !buffer ||
        size <= 0 )
    {
        TORIRS_ERR("Socket recv error: invalid stream\n");
        return SOCKSTREAM_ERROR_INVALID_STREAM;
    }

    int received = recv(stream->sockfd, (char*)buffer, size, MSG_DONTWAIT);
    if( received > 0 )
    {
        return received;
    }
    else if( received == 0 )
    {
        // Connection closed
        /*
         * An ORDERLY close, which this layer cannot judge and must not narrate.
         *
         * recv() returning 0 means the peer shut its write side down. Whether
         * that is a failure depends entirely on what the caller was reading,
         * and every caller already decides: platform_x_http.c and
         * platform_x_io_ondemand.c BREAK on it, because an HTTP/1.0 body ends
         * exactly this way, while net_transport_ws.c and platform_socket.c
         * treat it as a dead connection. The return code carries that
         * distinction; a line of stderr from in here cannot.
         *
         * It was doing real damage. Booting the rev-289 world fetches nine jag
         * archives over HTTP, so every SUCCESSFUL boot printed nine copies of
         * "Socket recv error: connection closed" -- and TORIRS_ERR is compiled
         * in even at OPT=1, so this was the optimized client's loudest output
         * and it described a client that was working correctly. On the XP box
         * a single stderr write has been measured at about 6 ms.
         *
         * The two branches below keep their logs: those are errno/WSA errors,
         * which are failures wherever they happen.
         */
        stream->status = SOCKSTREAM_STATUS_IDLE;
        return SOCKSTREAM_ERROR_CLOSED;
    }
    else
    {
        // Error or would block
#ifdef _WIN32
        int recv_err = WSAGetLastError();
        if( recv_err != WSAEWOULDBLOCK )
        {
            TORIRS_ERR("Socket recv error: %d\n", recv_err);
            stream->status = SOCKSTREAM_STATUS_ERROR;
            /* See the POSIX branch: a real error is a closed connection, not
             * a quiet one. */
            return SOCKSTREAM_ERROR_CLOSED;
        }
        // Would block - return -1, caller should check errno/WSAGetLastError
        return SOCKSTREAM_ERROR_NODATA;
#else
        if( errno != EAGAIN && errno != EWOULDBLOCK )
        {
            TORIRS_ERR("Socket recv error: %s\n", strerror(errno));
            stream->status = SOCKSTREAM_STATUS_ERROR;
            /*
             * Distinct from would-block, which used to share this return.
             *
             * The two are opposites — "nothing yet, ask again" against "this
             * connection is over" — and collapsing them meant a peer that
             * vanished (ECONNRESET, which is what a killed server sends when
             * it still had unread bytes queued) looked exactly like a quiet
             * one. The client kept polling a dead socket forever and only
             * noticed via a fifteen-second silence timer, if at all.
             */
            return SOCKSTREAM_ERROR_CLOSED;
        }

        // Would block - return -1, caller should check errno
        return SOCKSTREAM_ERROR_NODATA;
#endif
    }
}

int
sockstream_poll_connect(struct SockStream* stream)
{
    assert(stream);
    if( !stream || !sockstream_has_socket(stream) || stream->status != SOCKSTREAM_STATUS_CONNECTING )
    {
        // If already connected, return success
        if( stream->status == SOCKSTREAM_STATUS_CONNECTED )
        {
            return SOCKSTREAM_CONNECT_SUCCESS;
        }
        return SOCKSTREAM_CONNECT_FAILED;
    }

    // If already connected, return success
    if( stream->status == SOCKSTREAM_STATUS_CONNECTED )
    {
        return SOCKSTREAM_CONNECT_SUCCESS;
    }

    // Check if socket is writable (connected sockets are writable)
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(stream->sockfd, &write_fds);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

#ifdef _WIN32
    // On Windows, a failed connect signals exceptfds rather than writefds
    fd_set except_fds;
    FD_ZERO(&except_fds);
    FD_SET((SOCKET)stream->sockfd, &except_fds);
    int result = select(0, NULL, &write_fds, &except_fds, &timeout);
#else
    int result = select(stream->sockfd + 1, NULL, &write_fds, NULL, &timeout);
#endif

    if( result < 0 )
    {
        // select() itself failed
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return SOCKSTREAM_CONNECT_FAILED;
    }

    if( result == 0 )
    {
        return SOCKSTREAM_CONNECT_INFLIGHT;
    }

#ifdef _WIN32
    // A failed connect on Windows raises the exception fd, not the write fd
    if( FD_ISSET((SOCKET)stream->sockfd, &except_fds) )
    {
        int error = 0;
        int len = sizeof(error);
        getsockopt(stream->sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
        TORIRS_ERR("Connection failed with error: %d\n", error);
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return SOCKSTREAM_CONNECT_FAILED;
    }
#endif

    // Socket is writable, check if connection succeeded
    if( !FD_ISSET(stream->sockfd, &write_fds) )
    {
        return SOCKSTREAM_CONNECT_INFLIGHT;
    }

    // Check if connection succeeded
    int error = 0;
#ifdef _WIN32
    int len = sizeof(error);
    if( getsockopt(stream->sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == SOCKET_ERROR )
    {
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return SOCKSTREAM_CONNECT_FAILED;
    }
    if( error != 0 )
    {
        TORIRS_ERR("Connection failed with error: %d\n", error);
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return SOCKSTREAM_CONNECT_FAILED;
    }
#else
    socklen_t len = sizeof(error);
    if( getsockopt(stream->sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0 )
    {
        return SOCKSTREAM_CONNECT_INFLIGHT;
    }
    if( error != 0 )
    {
        TORIRS_ERR("Connection failed with error: %s\n", strerror(error));
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return SOCKSTREAM_CONNECT_FAILED;
    }
#endif

    // Connection succeeded
    stream->status = SOCKSTREAM_STATUS_CONNECTED;
    TORIRS_LOG("Connection completed\n");
    return SOCKSTREAM_CONNECT_SUCCESS;
}

int
sockstream_is_connected(struct SockStream* stream)
{
    return (stream && stream->status == SOCKSTREAM_STATUS_CONNECTED && sockstream_has_socket(stream)) ? 1 : 0;
}

intptr_t
sockstream_get_fd(struct SockStream* stream)
{
    assert(stream);
    if( stream->status != SOCKSTREAM_STATUS_CONNECTED )
        return -1;
    return stream->sockfd;
}

void
sockstream_close(struct SockStream* stream)
{
    assert(stream);

    if( sockstream_has_socket(stream) )
    {
#ifdef _WIN32
        closesocket(stream->sockfd);
#else
        close(stream->sockfd);
#endif
        sockstream_clear_socket(stream);
    }

    stream->status = SOCKSTREAM_STATUS_IDLE;
}

void
sockstream_free(struct SockStream* stream)
{
    if( !stream )
        return;
    sockstream_close(stream);
    free(stream);
}
