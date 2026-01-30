#include "sockstream.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define close closesocket
#define EAGAIN WSAEWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EINPROGRESS WSAEINPROGRESS
#define MSG_DONTWAIT 0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define close closesocket
#define EAGAIN WSAEWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EINPROGRESS WSAEINPROGRESS

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
    int is_valid;
};



struct SockStream*
sockstream_connect(const char* host, int port, int timeout_sec)
{
    if( !host || port <= 0 )
    {
        return NULL;
    }

    struct SockStream* stream = malloc(sizeof(struct SockStream));
    if( !stream )
    {
        return NULL;
    }
    memset(stream, 0, sizeof(struct SockStream));
    stream->sockfd = -1;
    stream->is_valid = 0;

    // Create socket
    stream->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( stream->sockfd < 0 )
    {
#ifdef _WIN32
        printf("Failed to create socket: %d\n", WSAGetLastError());
#else
        printf("Failed to create socket: %s\n", strerror(errno));
#endif
        free(stream);
        return NULL;
    }

    // Set socket to non-blocking
#ifdef _WIN32
    u_long mode = 1;
    if( ioctlsocket(stream->sockfd, FIONBIO, &mode) != 0 )
    {
        printf("Failed to set non-blocking: %d\n", WSAGetLastError());
        closesocket(stream->sockfd);
        free(stream);
        return NULL;
    }
#else
    int flags = fcntl(stream->sockfd, F_GETFL, 0);
    if( flags < 0 || fcntl(stream->sockfd, F_SETFL, flags | O_NONBLOCK) < 0 )
    {
        printf("Failed to set non-blocking: %s\n", strerror(errno));
        close(stream->sockfd);
        free(stream);
        return NULL;
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
        free(stream);
        return NULL;
    }

    // Try to connect (non-blocking)
    int result = connect(stream->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
#ifdef _WIN32
    int connect_err = WSAGetLastError();
    if( result < 0 && connect_err != WSAEINPROGRESS && connect_err != WSAEWOULDBLOCK )
    {
        printf("Failed to connect: %d\n", connect_err);
        closesocket(stream->sockfd);
        free(stream);
        return NULL;
    }
#else
    if( result < 0 && errno != EINPROGRESS )
    {
        printf("Failed to connect: %s\n", strerror(errno));
        close(stream->sockfd);
        free(stream);
        return NULL;
    }
#endif

    // For non-blocking connect, wait for connection to complete using select
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(stream->sockfd, &write_fds);
    struct timeval timeout;
    timeout.tv_sec = (timeout_sec > 0) ? timeout_sec : 5;
    timeout.tv_usec = 0;

    result = select(stream->sockfd + 1, NULL, &write_fds, NULL, &timeout);
    if( result <= 0 )
    {
        printf("Connection timeout or error\n");
#ifdef _WIN32
        closesocket(stream->sockfd);
#else
        close(stream->sockfd);
#endif
        free(stream);
        return NULL;
    }

    // Check if connection succeeded
    int error = 0;
    socklen_t len = sizeof(error);
    if( getsockopt(stream->sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0 || error != 0 )
    {
#ifdef _WIN32
        printf("Connection failed: %d\n", error ? error : WSAGetLastError());
        closesocket(stream->sockfd);
#else
        printf("Connection failed: %s\n", error ? strerror(error) : "unknown error");
        close(stream->sockfd);
#endif
        free(stream);
        return NULL;
    }

    stream->is_valid = 1;
    printf("Connected to %s:%d\n", host, port);
    return stream;
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
    else {
        return SOCKSTREAM_ERROR;
    }
#else
    int error = errno;
    if( error == EAGAIN || error == EWOULDBLOCK )
    {
        return SOCKSTREAM_ERROR_WOULDBLOCK;
    }
    else {
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
sockstream_send(struct SockStream* stream, const void* buffer, int size)
{
    if( !stream || !stream->is_valid || stream->sockfd < 0 || !buffer || size <= 0 )
    {
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
            stream->is_valid = 0;
        }
#else
        if( errno != EAGAIN && errno != EWOULDBLOCK )
        {
            printf("Socket send error: %s\n", strerror(errno));
            stream->is_valid = 0;
        }
#endif
    }
    return sent;
}

int
sockstream_recv(struct SockStream* stream, void* buffer, int size)
{
    if( !stream || !stream->is_valid || stream->sockfd < 0 || !buffer || size <= 0 )
    {
        return -1;
    }

    int received = recv(stream->sockfd, (char*)buffer, size, MSG_DONTWAIT);
    if( received > 0 )
    {
        return received;
    }
    else if( received == 0 )
    {
        // Connection closed
        stream->is_valid = 0;
        return 0;
    }
    else
    {
        // Error or would block
#ifdef _WIN32
        int recv_err = WSAGetLastError();
        if( recv_err != WSAEWOULDBLOCK )
        {
            printf("Socket recv error: %d\n", recv_err);
            stream->is_valid = 0;
            return -1;
        }
        // Would block - return -1, caller should check errno/WSAGetLastError
        return -1;
#else
        if( errno != EAGAIN && errno != EWOULDBLOCK )
        {
            printf("Socket recv error: %s\n", strerror(errno));
            stream->is_valid = 0;
            return -1;
        }
        // Would block - return -1, caller should check errno
        return -1;
#endif
    }
}

int
sockstream_is_valid(struct SockStream* stream)
{
    return (stream && stream->is_valid && stream->sockfd >= 0) ? 1 : 0;
}

int
sockstream_get_fd(struct SockStream* stream)
{
    if( !stream || !stream->is_valid )
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

    stream->is_valid = 0;
    free(stream);
}
