#include "sockstream.h"

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
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
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
    int sockfd;
    int status;
};

struct SockStream*
sockstream_new(void)
{
    struct SockStream* stream = malloc(sizeof(struct SockStream));
    if( !stream )
    {
        return NULL;
    }
    memset(stream, 0, sizeof(struct SockStream));
    stream->sockfd = -1;
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
    if( !host || port <= 0 || !stream )
    {
        return;
    }
    memset(stream, 0, sizeof(struct SockStream));
    stream->sockfd = -1;
    stream->status = SOCKSTREAM_STATUS_CONNECTING;

    // Create socket
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if( sock == INVALID_SOCKET )
    {
        printf("Failed to create socket: %d\n", WSAGetLastError());
        return;
    }
    stream->sockfd = (int)sock;
#else
    stream->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( stream->sockfd < 0 )
    {
        printf("Failed to create socket: %s\n", strerror(errno));
        return;
    }
#endif

    // Set socket to non-blocking
#ifdef _WIN32
    u_long mode = 1;
    if( ioctlsocket(stream->sockfd, FIONBIO, &mode) != 0 )
    {
        printf("Failed to set non-blocking: %d\n", WSAGetLastError());
        closesocket(stream->sockfd);
        free(stream);
        return;
    }
#else
    int flags = fcntl(stream->sockfd, F_GETFL, 0);
    if( flags < 0 || fcntl(stream->sockfd, F_SETFL, flags | O_NONBLOCK) < 0 )
    {
        printf("Failed to set non-blocking: %s\n", strerror(errno));
        close(stream->sockfd);
        return;
    }
#endif

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if( inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0 )
    {
        printf("Invalid address: %s\n", host);
#ifdef _WIN32
        closesocket(stream->sockfd);
#else
        close(stream->sockfd);
#endif
        stream->sockfd = -1;
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return;
    }

    // Try to connect (non-blocking)
    int result = connect(stream->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
#ifdef _WIN32
    if( result == 0 )
    {
        // Connection succeeded immediately
        stream->status = SOCKSTREAM_STATUS_CONNECTED;
        printf("Connected to %s:%d\n", host, port);
        return;
    }
    if( result == SOCKET_ERROR )
    {
        int connect_err = WSAGetLastError();
        if( connect_err != WSAEINPROGRESS && connect_err != WSAEWOULDBLOCK )
        {
            printf("Failed to connect: %d\n", connect_err);
            closesocket(stream->sockfd);
            stream->sockfd = -1;
            stream->status = SOCKSTREAM_STATUS_ERROR;
            return;
        }
    }
    // Connection in progress - return stream, caller should poll with sockstream_poll_connect
    printf("Connection in progress to %s:%d\n", host, port);
    return;
#else
    if( result == 0 )
    {
        // Connection succeeded immediately
        stream->status = SOCKSTREAM_STATUS_CONNECTED;
        printf("Connected to %s:%d\n", host, port);
        return;
    }
    if( result < 0 && errno != EINPROGRESS )
    {
        printf("Failed to connect: %s\n", strerror(errno));
        close(stream->sockfd);
        stream->sockfd = -1;
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return;
    }
    // Connection in progress - return stream, caller should poll with sockstream_poll_connect
    printf("Connection in progress to %s:%d\n", host, port);
    return;
#endif
}

int
sockstream_lasterror(struct SockStream* stream)
{
    if( !stream )
    {
        return SOCKSTREAM_ERROR;
    }
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
    if( !stream || stream->status != SOCKSTREAM_STATUS_CONNECTED || stream->sockfd < 0 || !buffer ||
        size <= 0 )
    {
        printf("Socket send error: invalid stream\n");
        return -1;
    }

    int sent = send(stream->sockfd, (const char*)buffer, size, 0);
    if( sent < 0 )
    {
#ifdef _WIN32
        int send_err = WSAGetLastError();
        if( send_err != WSAEWOULDBLOCK )
        {
            printf("Socket send error: %d\n", send_err);
            stream->status = SOCKSTREAM_STATUS_ERROR;
        }
#else
        if( errno != EAGAIN && errno != EWOULDBLOCK )
        {
            printf("Socket send error: %s\n", strerror(errno));
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
    if( !stream || stream->status != SOCKSTREAM_STATUS_CONNECTED || stream->sockfd < 0 || !buffer ||
        size <= 0 )
    {
        printf("Socket recv error: invalid stream\n");
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
        stream->status = SOCKSTREAM_STATUS_IDLE;
        printf("Socket recv error: connection closed\n");
        return SOCKSTREAM_ERROR_CLOSED;
    }
    else
    {
        // Error or would block
#ifdef _WIN32
        int recv_err = WSAGetLastError();
        if( recv_err != WSAEWOULDBLOCK )
        {
            printf("Socket recv error: %d\n", recv_err);
            stream->status = SOCKSTREAM_STATUS_ERROR;
            return SOCKSTREAM_ERROR_NODATA;
        }
        // Would block - return -1, caller should check errno/WSAGetLastError
        return SOCKSTREAM_ERROR_NODATA;
#else
        if( errno != EAGAIN && errno != EWOULDBLOCK )
        {
            printf("Socket recv error: %s\n", strerror(errno));
            stream->status = SOCKSTREAM_STATUS_ERROR;
            return SOCKSTREAM_ERROR_NODATA;
        }

        // Would block - return -1, caller should check errno
        return SOCKSTREAM_ERROR_NODATA;
#endif
    }
}

int
sockstream_poll_connect(struct SockStream* stream)
{
    if( !stream || stream->sockfd < 0 || stream->status != SOCKSTREAM_STATUS_CONNECTING )
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
    int result = select(0, NULL, &write_fds, NULL, &timeout);
#else
    int result = select(stream->sockfd + 1, NULL, &write_fds, NULL, &timeout);
#endif

    if( result <= 0 )
    {
        // Still connecting or error
        return SOCKSTREAM_CONNECT_INFLIGHT;
    }

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
        printf("Connection failed with error: %d\n", error);
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
        printf("Connection failed with error: %s\n", strerror(error));
        stream->status = SOCKSTREAM_STATUS_ERROR;
        return SOCKSTREAM_CONNECT_FAILED;
    }
#endif

    // Connection succeeded
    stream->status = SOCKSTREAM_STATUS_CONNECTED;
    printf("Connection completed\n");
    return SOCKSTREAM_CONNECT_SUCCESS;
}

int
sockstream_is_connected(struct SockStream* stream)
{
    return (stream && stream->status == SOCKSTREAM_STATUS_CONNECTED && stream->sockfd >= 0) ? 1 : 0;
}

int
sockstream_get_fd(struct SockStream* stream)
{
    if( !stream || stream->status != 1 )
    {
        return -1;
    }
    return stream->sockfd;
}

void
sockstream_close(struct SockStream* stream)
{
    if( !stream )
    {
        return;
    }

    if( stream->sockfd >= 0 )
    {
#ifdef _WIN32
        closesocket(stream->sockfd);
#else
        close(stream->sockfd);
#endif
        stream->sockfd = -1;
    }

    stream->status = SOCKSTREAM_STATUS_IDLE;
}
