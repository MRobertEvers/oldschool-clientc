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

struct MultiArchiveRequest
{
    int table_id;
    int archive_id;
    int flags;
};

/* Case-insensitive search for "content-length:" in request buffer. */
static char*
find_content_length(char* buf)
{
    static char const key[] = "content-length:";
    size_t key_len = sizeof(key) - 1;
    for( char* p = buf; *p; p++ )
    {
        if( p + key_len <= buf + strlen(buf) )
        {
            int match = 1;
            for( size_t i = 0; i < key_len; i++ )
            {
                char c = (char)((unsigned char)p[i] >= 'A' && (unsigned char)p[i] <= 'Z'
                                    ? p[i] + 32
                                    : (unsigned char)p[i]);
                if( c != key[i] )
                {
                    match = 0;
                    break;
                }
            }
            if( match )
                return p + key_len;
        }
    }
    return NULL;
}

static int
read_request_body(
    int fd,
    char* headers_end,
    int received_len,
    char** body_out,
    int* body_len_out)
{
    char* content_length_str = find_content_length(headers_end);
    if( !content_length_str )
        return -1;

    int content_length = atoi(content_length_str);
    if( content_length <= 0 )
        return -1;

    char* body_start = strstr(headers_end, "\r\n\r\n");
    if( !body_start )
        return -1;
    body_start += 4;

    int header_bytes = (int)(body_start - headers_end);
    int already_in_buffer = received_len - (int)(body_start - (char*)headers_end);

    char* body = (char*)malloc((size_t)content_length + 1);
    if( !body )
        return -1;

    if( already_in_buffer > 0 )
    {
        if( already_in_buffer > content_length )
            already_in_buffer = content_length;
        memcpy(body, body_start, (size_t)already_in_buffer);
    }

    int to_read = content_length - already_in_buffer;
    int offset = already_in_buffer;
    while( to_read > 0 )
    {
        ssize_t n = recv(fd, body + offset, (size_t)to_read, 0);
        if( n <= 0 )
            break;
        offset += (int)n;
        to_read -= (int)n;
    }

    body[content_length] = '\0';
    *body_out = body;
    *body_len_out = content_length;
    return 0;
}

/* Very small JSON parser for an array of { "table_id":N, "archive_id":M, "flags":F } objects.
 * Not robust; assumes well-formed input from our own tools. */
static int
parse_multi_archive_json(
    const char* json,
    struct MultiArchiveRequest** out_reqs,
    int* out_count)
{
    int capacity = 8;
    int count = 0;
    struct MultiArchiveRequest* reqs =
        (struct MultiArchiveRequest*)malloc((size_t)capacity * sizeof(struct MultiArchiveRequest));
    if( !reqs )
        return -1;

    const char* p = json;
    while( *p )
    {
        if( *p == '{' )
        {
            int table_id = -1;
            int archive_id = -1;
            int flags = 0;

            const char* obj_end = strchr(p, '}');
            if( !obj_end )
                break;

            const char* q = p;
            while( q < obj_end )
            {
                if( strstr(q, "\"table_id\"") == q )
                {
                    const char* colon = strchr(q, ':');
                    if( colon )
                        table_id = atoi(colon + 1);
                }
                else if( strstr(q, "\"archive_id\"") == q )
                {
                    const char* colon = strchr(q, ':');
                    if( colon )
                        archive_id = atoi(colon + 1);
                }
                else if( strstr(q, "\"flags\"") == q )
                {
                    const char* colon = strchr(q, ':');
                    if( colon )
                        flags = atoi(colon + 1);
                }
                q++;
            }

            if( table_id >= 0 && archive_id >= 0 )
            {
                if( count >= capacity )
                {
                    capacity *= 2;
                    struct MultiArchiveRequest* new_reqs = (struct MultiArchiveRequest*)realloc(
                        reqs, (size_t)capacity * sizeof(struct MultiArchiveRequest));
                    if( !new_reqs )
                    {
                        free(reqs);
                        return -1;
                    }
                    reqs = new_reqs;
                }
                reqs[count].table_id = table_id;
                reqs[count].archive_id = archive_id;
                reqs[count].flags = flags;
                count++;
            }
            p = obj_end;
        }
        else
        {
            p++;
        }
    }

    *out_reqs = reqs;
    *out_count = count;
    return 0;
}

static void
send_multipart_response(
    int fd,
    struct CacheDat* cache_dat,
    struct MultiArchiveRequest* reqs,
    int req_count)
{
    const char* boundary = "BOUNDARY123456789";
    char header_buf[512];
    int header_len = snprintf(
        header_buf,
        sizeof(header_buf),
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Type: multipart/form-data; boundary=%s\r\n"
        "Connection: close\r\n",
        boundary);

    send(fd, header_buf, (size_t)header_len, 0);
    /* End of headers; start of body */
    send(fd, "\r\n", 2, 0);

    for( int i = 0; i < req_count; i++ )
    {
        struct CacheDatArchive* archive = NULL;
        int table_id = reqs[i].table_id;
        int archive_id = reqs[i].archive_id;
        int flags = reqs[i].flags;

        if( table_id == CACHE_DAT_MAPS )
        {
            int chunk_x = archive_id >> 16;
            int chunk_z = archive_id & 0xFFFF;
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
            continue;

        int serialized_size = archive_serialized_size(archive);
        if( serialized_size <= 0 )
        {
            cache_dat_archive_free(archive);
            continue;
        }

        void* payload = malloc((size_t)serialized_size);
        if( !payload )
        {
            cache_dat_archive_free(archive);
            continue;
        }

        int written = archive_serialize_to_buffer(archive, payload, serialized_size);
        cache_dat_archive_free(archive);
        if( written != serialized_size )
        {
            free(payload);
            continue;
        }

        char part_header[256];
        int part_header_len = snprintf(
            part_header,
            sizeof(part_header),
            "--%s\r\n"
            "Content-Type: application/octet-stream\r\n"
            "X-Table-Id: %d\r\n"
            "X-Archive-Id: %d\r\n"
            "X-Flags: %d\r\n"
            "\r\n",
            boundary,
            table_id,
            archive_id,
            flags);
        send(fd, part_header, (size_t)part_header_len, 0);
        send(fd, payload, (size_t)serialized_size, 0);
        send(fd, "\r\n", 2, 0);
        free(payload);
    }

    char closing[64];
    int closing_len = snprintf(closing, sizeof(closing), "--%s--\r\n", boundary);
    send(fd, closing, (size_t)closing_len, 0);
}

/* Respond to CORS preflight (OPTIONS) with 200 OK and allow POST + Content-Type. */
static void
send_cors_preflight_ok(int fd)
{
    const char* headers = "HTTP/1.1 200 OK\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                          "Access-Control-Allow-Headers: Content-Type\r\n"
                          "Content-Length: 0\r\n"
                          "Connection: close\r\n"
                          "\r\n";
    send(fd, headers, (size_t)strlen(headers), 0);
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

    /* Only care about first line and method/path */
    char* line_end = strstr(buf, "\r\n");
    if( !line_end )
    {
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }
    *line_end = '\0';

    if( strncmp(buf, "OPTIONS /api/load_archives", 26) == 0 )
    {
        send_cors_preflight_ok(client_fd);
        close(client_fd);
        return;
    }

    if( strncmp(buf, "GET /api/load_archive", 21) == 0 )
    {
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
        return;
    }
    else if( strncmp(buf, "POST ", 5) == 0 && strncmp(buf + 5, "/api/load_archives", 18) == 0 )
    {
        printf("Loading archives\n");
        *line_end = '\r'; /* restore first \r so read_request_body can find \r\n\r\n */
        char* body = NULL;
        int body_len = 0;
        if( read_request_body(client_fd, buf, (int)n, &body, &body_len) != 0 )
        {
            printf("Failed to read request body\n");
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }

        struct MultiArchiveRequest* reqs = NULL;
        int req_count = 0;
        if( parse_multi_archive_json(body, &reqs, &req_count) != 0 || req_count <= 0 )
        {
            printf("Failed to parse request body\n");
            free(body);
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }
        free(body);

        send_multipart_response(client_fd, cache_dat, reqs, req_count);
        free(reqs);
        close(client_fd);
        return;
    }
    else
    {
        send_response(client_fd, 404, NULL, 0);
        close(client_fd);
        return;
    }
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
