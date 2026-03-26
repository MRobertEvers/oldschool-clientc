/*
 * Single-threaded HTTP server that serves CacheDat archives and Lua config files.
 * GET /api/load_archive?table_id=N&archive_id=M&flags=F
 * GET /api/load_config?path=...
 * POST /api/load_archives, POST /api/load_configs
 * Returns serialized payloads (same format as luajs_archives / luajs_configs) or 404.
 */

#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/rscache/cache_dat.h"
#include "platforms/browser2/luajs_archives.h"
#include "platforms/browser2/luajs_configs.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 8096
#define BACKLOG 8
#define REQUEST_BUF_SIZE 4096
#define MAX_CONFIG_PATHS 128
#define CONFIG_PATH_MAX 512

static int
hex_digit(
    char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    if( c >= 'a' && c <= 'f' )
        return 10 + (c - 'a');
    if( c >= 'A' && c <= 'F' )
        return 10 + (c - 'A');
    return -1;
}

/** Decode %XX and + in place (minimal URL decoding for query path=). */
static void
url_decode_inplace(
    char* s)
{
    char* w = s;
    for( char* r = s; *r; )
    {
        if( *r == '%' && r[1] && r[2] )
        {
            int hi = hex_digit(r[1]);
            int lo = hex_digit(r[2]);
            if( hi >= 0 && lo >= 0 )
            {
                *w++ = (char)((hi << 4) | lo);
                r += 3;
                continue;
            }
        }
        if( *r == '+' )
            *w++ = ' ';
        else
            *w++ = *r;
        r++;
    }
    *w = '\0';
}

static int
config_path_is_safe(
    const char* rel)
{
    if( !rel || !rel[0] )
        return 0;
    if( rel[0] == '/' )
        return 0;
    if( strstr(rel, "..") )
        return 0;
    return 1;
}

/** Parse `path=...` from query string into `out` (cap bytes). Returns 0 on success. */
static int
parse_path_query(
    const char* query,
    char* out,
    int cap)
{
    const char* p = strstr(query, "path=");
    if( !p )
        return -1;
    p += 5;
    const char* end = strchr(p, '&');
    int len = end ? (int)(end - p) : (int)strlen(p);
    if( len >= cap )
        len = cap - 1;
    memcpy(out, p, (size_t)len);
    out[len] = '\0';
    url_decode_inplace(out);
    return 0;
}

/** Load file under config_dir/rel_path into `out` (caller frees out->data). Returns 0 on success. */
static int
read_config_file_into(
    const char* config_dir,
    const char* rel_path,
    struct LuaConfigFile* out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->name, rel_path, sizeof(out->name) - 1);

    char full[1024];
    if( snprintf(full, sizeof full, "%s/%s", config_dir, rel_path) >= (int)sizeof full )
        return -1;

    FILE* f = fopen(full, "rb");
    if( !f )
        return -1;
    if( fseek(f, 0, SEEK_END) != 0 )
    {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if( sz < 0 || sz > INT_MAX )
    {
        fclose(f);
        return -1;
    }
    out->size = (int)sz;
    if( out->size == 0 )
    {
        out->data = NULL;
        fclose(f);
        return 0;
    }
    if( fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return -1;
    }
    out->data = (uint8_t*)malloc((size_t)out->size);
    if( !out->data )
    {
        fclose(f);
        return -1;
    }
    if( fread(out->data, 1, (size_t)out->size, f) != (size_t)out->size )
    {
        free(out->data);
        out->data = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

struct MultiConfigRequest
{
    char paths[MAX_CONFIG_PATHS][CONFIG_PATH_MAX];
    int count;
};

/* Parse [{"path":"a"},...] — minimal; assumes well-formed JSON from browser. */
static int
parse_multi_config_json(
    const char* json,
    struct MultiConfigRequest* out)
{
    out->count = 0;
    const char* p = json;
    while( *p && out->count < MAX_CONFIG_PATHS )
    {
        const char* key = strstr(p, "\"path\"");
        if( !key )
            break;
        const char* colon = strchr(key, ':');
        if( !colon )
            break;
        const char* quote = strchr(colon + 1, '"');
        if( !quote )
            break;
        quote++;
        const char* endq = strchr(quote, '"');
        if( !endq )
            break;
        int len = (int)(endq - quote);
        if( len >= CONFIG_PATH_MAX )
            len = CONFIG_PATH_MAX - 1;
        memcpy(out->paths[out->count], quote, (size_t)len);
        out->paths[out->count][len] = '\0';
        out->count++;
        p = endq + 1;
    }
    return out->count > 0 ? 0 : -1;
}

static void
send_multipart_config_response(
    int fd,
    const char* config_dir,
    struct MultiConfigRequest* reqs)
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
    send(fd, "\r\n", 2, 0);

    for( int i = 0; i < reqs->count; i++ )
    {
        if( !config_path_is_safe(reqs->paths[i]) )
            continue;

        struct LuaConfigFile cf;
        if( read_config_file_into(config_dir, reqs->paths[i], &cf) != 0 )
            continue;

        int serialized_size = luajs_LuaConfigFile_serialized_size(&cf);
        if( serialized_size <= 0 )
        {
            if( cf.data )
                free(cf.data);
            continue;
        }

        void* payload = malloc((size_t)serialized_size);
        if( !payload )
        {
            if( cf.data )
                free(cf.data);
            continue;
        }

        int written =
            luajs_LuaConfigFile_serialize_to_buffer(&cf, payload, serialized_size);
        if( cf.data )
            free(cf.data);
        cf.data = NULL;

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
            "\r\n",
            boundary);
        send(fd, part_header, (size_t)part_header_len, 0);
        send(fd, payload, (size_t)serialized_size, 0);
        send(fd, "\r\n", 2, 0);
        free(payload);
    }

    char closing[64];
    int closing_len = snprintf(closing, sizeof(closing), "--%s--\r\n", boundary);
    send(fd, closing, (size_t)closing_len, 0);
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

        int serialized_size = luajs_CacheDatArchive_serialized_size(archive);
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

        int written =
            luajs_CacheDatArchive_serialize_to_buffer(archive, payload, serialized_size);
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
    struct CacheDat* cache_dat,
    char const* config_dir)
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

    if( strncmp(buf, "OPTIONS /api/load_archives", 26) == 0 ||
        strncmp(buf, "OPTIONS /api/load_configs", 25) == 0 ||
        strncmp(buf, "OPTIONS /api/load_config", 24) == 0 )
    {
        send_cors_preflight_ok(client_fd);
        close(client_fd);
        return;
    }

    if( strncmp(buf, "GET /api/load_config", 20) == 0 )
    {
        char* query = strchr(buf + 20, '?');
        if( !query )
        {
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }
        query++;

        char rel_path[CONFIG_PATH_MAX];
        if( parse_path_query(query, rel_path, (int)sizeof rel_path) != 0 )
        {
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }

        if( !config_path_is_safe(rel_path) )
        {
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }

        struct LuaConfigFile cf;
        if( read_config_file_into(config_dir, rel_path, &cf) != 0 )
        {
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }

        int serialized_size = luajs_LuaConfigFile_serialized_size(&cf);
        if( serialized_size <= 0 )
        {
            if( cf.data )
                free(cf.data);
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }

        void* payload = malloc((size_t)serialized_size);
        if( !payload )
        {
            if( cf.data )
                free(cf.data);
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }

        int written =
            luajs_LuaConfigFile_serialize_to_buffer(&cf, payload, serialized_size);
        if( cf.data )
            free(cf.data);
        cf.data = NULL;

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

        int serialized_size = luajs_CacheDatArchive_serialized_size(archive);
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

        int written =
            luajs_CacheDatArchive_serialize_to_buffer(archive, payload, serialized_size);
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
    else if( strncmp(buf, "POST ", 5) == 0 && strncmp(buf + 5, "/api/load_configs", 17) == 0 )
    {
        *line_end = '\r';
        char* body = NULL;
        int body_len = 0;
        if( read_request_body(client_fd, buf, (int)n, &body, &body_len) != 0 )
        {
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }

        struct MultiConfigRequest reqs;
        if( parse_multi_config_json(body, &reqs) != 0 )
        {
            free(body);
            send_response(client_fd, 404, NULL, 0);
            close(client_fd);
            return;
        }
        free(body);

        send_multipart_config_response(client_fd, config_dir, &reqs);
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
    /* Relative to CWD when running ./a.out from test/datserver — matches src/osrs/revconfig/configs/ */
    char const* config_dir = "../../src/osrs/revconfig/configs";
    int port = PORT;

    if( argc > 1 )
        cache_dir = argv[1];
    if( argc > 2 )
        port = atoi(argv[2]);
    if( argc > 3 )
        config_dir = argv[3];

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
    printf("GET /api/load_archive?table_id=N&archive_id=M&flags=F\n");
    printf("GET /api/load_config?path=...\n");
    printf("Config dir: %s\n", config_dir);

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

        handle_client(client_fd, cache_dat, config_dir);
    }

    close(server_fd);
    cache_dat_free(cache_dat);
    return 0;
}
