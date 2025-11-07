#include "cache_inet.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

struct CacheInet
{
    char const* ip;
    int port;

    // Platform specific.
    int sockfd;
    struct sockaddr_in server_addr;
};

struct CacheInet*
cache_inet_new_connect(char const* ip, int port)
{
    struct CacheInet* cache_inet = malloc(sizeof(struct CacheInet));
    if( !cache_inet )
    {
        printf("Failed to allocate memory for CacheInet\n");
        return NULL;
    }
    memset(cache_inet, 0, sizeof(struct CacheInet));
    cache_inet->ip = strdup(ip);
    cache_inet->port = port;

    // Connect to the server
    cache_inet->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( cache_inet->sockfd < 0 )
    {
        printf("Socket creation failed\n");
        free(cache_inet);
        return NULL;
    }

    // Set up server address structure
    cache_inet->server_addr.sin_family = AF_INET;
    cache_inet->server_addr.sin_port = htons(port);
    if( inet_pton(AF_INET, ip, &cache_inet->server_addr.sin_addr) <= 0 )
    {
        printf("Invalid address\n");
        close(cache_inet->sockfd);
        free(cache_inet);
        return NULL;
    }

    // Connect to the server
    if( connect(
            cache_inet->sockfd,
            (struct sockaddr*)&cache_inet->server_addr,
            sizeof(cache_inet->server_addr)) < 0 )
    {
        printf("Connection failed\n");
        close(cache_inet->sockfd);
        free(cache_inet);
        return NULL;
    }

#ifdef __EMSCRIPTEN__
    // For Emscripten: WebSocket connection needs time to establish
    // The POSIX socket is a wrapper over WebSocket, which is async
    printf("Emscripten: Waiting for WebSocket connection to stabilize...\n");
    emscripten_sleep(100); // Wait 100ms for WebSocket to fully connect

    // Set socket to blocking mode (though Emscripten may not fully honor this)
    int flags = fcntl(cache_inet->sockfd, F_GETFL, 0);
    if( flags >= 0 )
    {
        fcntl(cache_inet->sockfd, F_SETFL, flags & ~O_NONBLOCK);
    }

    // Set socket receive timeout to work around non-blocking issues
    struct timeval tv;
    tv.tv_sec = 5; // 5 second timeout
    tv.tv_usec = 0;
    setsockopt(cache_inet->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(cache_inet->sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    printf("Emscripten: Socket configured (blocking mode + timeouts)\n");
#endif

    return cache_inet;
}

void
cache_inet_free(struct CacheInet* cache_inet)
{
    if( !cache_inet )
        return;
    free(cache_inet->ip);
    close(cache_inet->sockfd);
    free(cache_inet);
}

struct CacheInetPayload*
cache_inet_payload_new_archive_request(struct CacheInet* cache_inet, int table_id, int archive_id)
{
    struct CacheInetPayload* payload = malloc(sizeof(struct CacheInetPayload));
    if( !payload )
    {
        printf("Failed to allocate memory for CacheInetPayload\n");
        return NULL;
    }
    memset(payload, 0, sizeof(struct CacheInetPayload));
    payload->table_id = table_id;
    payload->archive_id = archive_id;

    char status_buffer[4] = { 0 };
    char payload_size_buffer[4] = { 0 };

    unsigned char request[6] = {
        (unsigned char)CACHE_INET_REQUEST_TYPE_ARCHIVE, (unsigned char)(table_id) & 0xff,
        (unsigned char)(archive_id >> 24) & 0xff,       (unsigned char)(archive_id >> 16) & 0xff,
        (unsigned char)(archive_id >> 8) & 0xff,        (unsigned char)(archive_id & 0xff)
    };
    if( send(cache_inet->sockfd, request, sizeof(request), 0) != sizeof(request) )
    {
        printf("Failed to send request\n");
        free(payload);
        return NULL;
    }

    // Receive status buffer (4 bytes)
    int status_received = 0;
    while( status_received < sizeof(status_buffer) )
    {
    sleep_poll_status:
        int n = recv(
            cache_inet->sockfd,
            status_buffer + status_received,
            sizeof(status_buffer) - status_received,
            0);
        if( n <= 0 )
        {
#ifdef __EMSCRIPTEN__
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                // WebSocket data not ready yet, wait and retry
                emscripten_sleep(10); // Wait 10ms for data (ASYNCIFY-compatible)
                goto sleep_poll_status;
            }
#endif
            printf("Failed to receive status (n: %d, errno: %d/%s)\n", n, errno, strerror(errno));
            free(payload);
            return NULL;
        }
        status_received += n;
    }

    int status = (status_buffer[0] & 0xFF) << 24 | (status_buffer[1] & 0xFF) << 16 |
                 (status_buffer[2] & 0xFF) << 8 | (status_buffer[3] & 0xFF);
    if( status < 0 )
    {
        printf("Error status\n");
        free(payload);
        return NULL;
    }

    // Receive payload size buffer (4 bytes)
    int size_received = 0;
    while( size_received < sizeof(payload_size_buffer) )
    {
    sleep_poll_payload_size:
        int n = recv(
            cache_inet->sockfd,
            payload_size_buffer + size_received,
            sizeof(payload_size_buffer) - size_received,
            0);
        if( n <= 0 )
        {
#ifdef __EMSCRIPTEN__
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                // WebSocket data not ready yet, wait and retry
                emscripten_sleep(10); // Wait 10ms for data (ASYNCIFY-compatible)
                goto sleep_poll_payload_size;
            }
#endif
            printf("Failed to receive status (n: %d, errno: %d/%s)\n", n, errno, strerror(errno));
            free(payload);
            return NULL;
        }
        size_received += n;
    }

    int payload_size;
    payload_size = (payload_size_buffer[0] & 0xFF) << 24 | (payload_size_buffer[1] & 0xFF) << 16 |
                   (payload_size_buffer[2] & 0xFF) << 8 | (payload_size_buffer[3] & 0xFF);

    payload->data = malloc(payload_size + 1 /* compression mode */ + 4 /* size */);
    int buffer_size = payload_size + 1 /* compression mode */ + 4 /* size */;

    payload->data_size = buffer_size;
    if( !payload->data )
    {
        printf("Failed to allocate memory for response data\n");
        free(payload);
        return NULL;
    }

    memset(payload->data, 0, buffer_size);

    // Receive payload data in a loop to handle partial receives
    int total_received = 0;
    while( total_received < payload_size )
    {
    recv_poll:
        int recved_bytes = recv(
            cache_inet->sockfd,
            payload->data + 5 + total_received,
            payload_size - total_received,
            0);

        if( recved_bytes < 0 )
        {
#ifdef __EMSCRIPTEN__
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                emscripten_sleep(10); // Wait 10ms for data (ASYNCIFY-compatible)
                goto recv_poll;
            }
#endif
            printf("Error receiving payload data: recv failed\n");
            free(payload->data);
            free(payload);
            return NULL;
        }
        else if( recved_bytes == 0 )
        {
            printf(
                "Connection closed: %d bytes received, %d bytes expected\n",
                total_received,
                payload_size);
            free(payload->data);
            free(payload);
            return NULL;
        }

        total_received += recved_bytes;
    }

    // TODO: 5 is a hack
    payload->data[0] = 5;
    payload->data[1] = (payload_size >> 24) & 0xFF;
    payload->data[2] = (payload_size >> 16) & 0xFF;
    payload->data[3] = (payload_size >> 8) & 0xFF;
    payload->data[4] = payload_size & 0xFF;

    return payload;
}

void
cache_inet_payload_free(struct CacheInetPayload* payload)
{
    if( !payload )
        return;
    free(payload->data);
    free(payload);
}