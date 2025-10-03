#include "assets.h"

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

struct Assets
{
    char const* ip;
    int port;

    // Platform specific.
    int sockfd;
    struct sockaddr_in server_addr;
};

struct Assets*
assets_new_inet(char const* ip, int port)
{
    struct Assets* assets = malloc(sizeof(struct Assets));
    if( !assets )
    {
        return NULL;
    }
    memset(assets, 0, sizeof(struct Assets));
    assets->ip = ip;
    assets->port = port;

    // Create socket
    assets->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( assets->sockfd < 0 )
    {
        perror("Socket creation failed");
        free(assets);
        return NULL;
    }

    // Set up server address structure
    assets->server_addr.sin_family = AF_INET;
    assets->server_addr.sin_port = htons(port);
    if( inet_pton(AF_INET, ip, &assets->server_addr.sin_addr) <= 0 )
    {
        perror("Invalid address");
        close(assets->sockfd);
        free(assets);
        return NULL;
    }

    // Connect to the server
    if( connect(
            assets->sockfd, (struct sockaddr*)&assets->server_addr, sizeof(assets->server_addr)) <
        0 )
    {
        perror("Connection failed");
        close(assets->sockfd);
        free(assets);
        return NULL;
    }

    // Set socket to non-blocking mode
    int flags = fcntl(assets->sockfd, F_GETFL, 0);
    if( flags < 0 || fcntl(assets->sockfd, F_SETFL, flags | O_NONBLOCK) < 0 )
    {
        perror("Failed to set non-blocking mode");
        close(assets->sockfd);
        free(assets);
        return NULL;
    }

    return assets;
}

void
assets_free(struct Assets* assets)
{
    if( !assets )
        return;

    if( assets->sockfd >= 0 )
        close(assets->sockfd);
    free(assets);
}

struct CacheArchive*
assets_cache_archive_new_load(
    struct Assets* assets, struct Cache* cache, int table_id, int archive_id)
{
    // Create a dat2 file with an empty first sector.
    // Reference tables, idx255, is required to be loaded,
    // There are NUMBER_OF_TABLE records in the idx255
    // idx's contains the "offsets" in the dat2 file for
    // archives.

    // 1. Create a dat2 file with an zeroed first sector.
    // 2. Get the number of records in idx255.
    // 3. For each record, write the "offsets" in the dat2 set to 0 (i.e. not loaded).
    return NULL;
}
