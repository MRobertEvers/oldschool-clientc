/*
 * Single-threaded HTTP server that serves CacheDat archives.
 * GET /api/load_archive?epoch=base&table_id=N&archive_id=M
 * Returns serialized archive (same format as luajs_archives) or 404.
 */

#include "osrs/gio_cache_dat.h"
#include "osrs/rscache/cache_dat.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 8096
#define BACKLOG 8
#define REQUEST_BUF_SIZE 4096
#define ARCHIVE_HEADER_SIZE (7 * (int)sizeof(int))

/* Write 32-bit integer in explicit little-endian order. */
static void
write_int(
    uint8_t* p,
    int value)
{
    uint32_t v = (uint32_t)value;
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int
archive_serialized_size(const struct CacheDatArchive* archive)
{
    if( !archive )
        return -1;
    int data_size = archive->data_size >= 0 ? archive->data_size : 0;
    return ARCHIVE_HEADER_SIZE + data_size;
}

static int
archive_serialize_to_buffer(
    const struct CacheDatArchive* archive,
    void* buffer,
    int size)
{
    if( !archive || !buffer || size < 0 )
        return -1;

    int need = archive_serialized_size(archive);
    if( need < 0 || size < need )
        return -1;

    uint8_t* p = (uint8_t*)buffer;
    int data_size = archive->data_size >= 0 ? archive->data_size : 0;

    write_int(p + 0, need);
    write_int(p + 4, data_size);
    write_int(p + 8, archive->archive_id);
    write_int(p + 12, archive->table_id);
    write_int(p + 16, archive->revision);
    write_int(p + 20, archive->file_count);
    write_int(p + 24, (int)archive->format);

    printf(
        "archive_serialize_to_buffer: \n"
        "  need=%d, \ndata_size=%d, \narchive_id=%d, \ntable_id=%d, "
        "\nrevision=%d, \nfile_count=%d, \nformat=%d\n",
        need,
        data_size,
        archive->archive_id,
        archive->table_id,
        archive->revision,
        archive->file_count,
        archive->format);

    if( data_size > 0 && archive->data )
        memcpy(p + ARCHIVE_HEADER_SIZE, archive->data, (size_t)data_size);

    return need;
}

static int
create_server_socket(int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( sockfd < 0 )
    {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 )
    {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in server_addr = { 0 };
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if( bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 )
    {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if( listen(sockfd, BACKLOG) < 0 )
    {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/* Parse "table_id=123" and "archive_id=456" from query string. Returns 0 on success. */
static int
parse_query(
    const char* query,
    int* table_id_out,
    int* archive_id_out,
    int* flags_out)
{
    *table_id_out = -1;
    *archive_id_out = -1;

    const char* p = query;
    while( *p )
    {
        if( strncmp(p, "table_id=", 9) == 0 )
        {
            p += 9;
            *table_id_out = atoi(p);
            while( *p && *p != '&' )
                p++;
            if( *p == '&' )
                p++;
            continue;
        }
        if( strncmp(p, "archive_id=", 11) == 0 )
        {
            p += 11;
            *archive_id_out = atoi(p);
            while( *p && *p != '&' )
                p++;
            if( *p == '&' )
                p++;
            continue;
        }
        if( strncmp(p, "flags=", 6) == 0 )
        {
            p += 6;
            *flags_out = atoi(p);
            while( *p && *p != '&' )
                p++;
            if( *p == '&' )
                p++;
        }
        /* skip to next & or end */
        while( *p && *p != '&' )
            p++;
        if( *p == '&' )
            p++;
    }

    return (*table_id_out >= 0 && *archive_id_out >= 0) ? 0 : -1;
}

static void
send_response(
    int fd,
    int status,
    const void* body,
    int body_len)
{
    char status_line[64];
    const char* status_msg = (status == 200) ? "OK" : "Not Found";
    snprintf(status_line, sizeof(status_line), "HTTP/1.1 %d %s\r\n", status, status_msg);

    char content_length[64];
    snprintf(content_length, sizeof(content_length), "Content-Length: %d\r\n", body_len);

    send(fd, status_line, strlen(status_line), 0);
    send(
        fd,
        "Access-Control-Allow-Origin: *\r\n",
        (size_t)strlen("Access-Control-Allow-Origin: *\r\n"),
        0);
    send(
        fd,
        "Content-Type: application/octet-stream\r\n",
        (size_t)strlen("Content-Type: application/octet-stream\r\n"),
        0);
    send(fd, "Connection: close\r\n", (size_t)strlen("Connection: close\r\n"), 0);
    send(fd, content_length, strlen(content_length), 0);
    send(fd, "\r\n", 2, 0);

    if( body_len > 0 && body )
    {
        const char* b = (const char*)body;
        while( body_len > 0 )
        {
            ssize_t n = send(fd, b, (size_t)body_len, 0);
            if( n <= 0 )
                break;
            b += n;
            body_len -= (int)n;
        }
    }
}

static void
handle_client(
    int client_fd,
    struct CacheDat* cache_dat)
{
    char buf[REQUEST_BUF_SIZE];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if( n <= 0 )
    {
        close(client_fd);
        return;
    }
    buf[n] = '\0';

    /* Only care about first line: "GET /api/load_archive?..." */
    char* line_end = strstr(buf, "\r\n");
    if( !line_end )
    {
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }
    *line_end = '\0';

    if( strncmp(buf, "GET /api/load_archive", 21) != 0 )
    {
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }

    char* query = strchr(buf + 21, '?');
    if( !query )
    {
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }
    query++;

    int table_id, archive_id, flags;
    if( parse_query(query, &table_id, &archive_id, &flags) != 0 )
    {
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }

    struct CacheDatArchive* archive = NULL;
    if( table_id == CACHE_DAT_MAPS )
    {
        int chunk_x = archive_id >> 16;
        int chunk_z = archive_id & 0xFFFF;

        printf("Loading map terrain (flags: %d) %d, %d\n", flags, chunk_x, chunk_z);

        if( flags == 2 )
        {
            archive = gioqb_cache_dat_map_scenery_new_load(cache_dat, chunk_x, chunk_z);
        }
        else if( flags == 1 )
        {
            archive = gioqb_cache_dat_map_terrain_new_load(cache_dat, chunk_x, chunk_z);
        }
    }
    else
    {
        archive = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
    }

    if( !archive )
    {
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }

    int serialized_size = archive_serialized_size(archive);
    if( serialized_size <= 0 )
    {
        cache_dat_archive_free(archive);
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }

    void* payload = malloc((size_t)serialized_size);
    if( !payload )
    {
        cache_dat_archive_free(archive);
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }

    int written = archive_serialize_to_buffer(archive, payload, serialized_size);
    cache_dat_archive_free(archive);

    if( written != serialized_size )
    {
        free(payload);
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }

    send_response(client_fd, 200, payload, serialized_size);
    free(payload);
    close(client_fd);
}

int
main(
    int argc,
    char* argv[])
{
    char const* cache_dir = "../../cache254";
    int port = PORT;

    if( argc > 1 )
        cache_dir = argv[1];
    if( argc > 2 )
        port = atoi(argv[2]);

    struct CacheDat* cache_dat = cache_dat_new_from_directory(cache_dir);
    if( !cache_dat )
    {
        fprintf(stderr, "Failed to load cache from directory: %s\n", cache_dir);
        return 1;
    }

    printf("Cache loaded from %s\n", cache_dir);

    int server_fd = create_server_socket(port);
    if( server_fd < 0 )
    {
        cache_dat_free(cache_dat);
        return 1;
    }

    printf("Archive server listening on port %d (single-threaded)\n", port);
    printf("GET /api/load_archive?epoch=base&table_id=N&archive_id=M\n");

    for( ;; )
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);

        if( client_fd < 0 )
        {
            if( errno == EINTR )
                continue;
            perror("accept");
            continue;
        }

        handle_client(client_fd, cache_dat);
    }

    close(server_fd);
    cache_dat_free(cache_dat);
    return 0;
}
