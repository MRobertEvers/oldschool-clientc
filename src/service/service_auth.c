/*
 * The servers' signature. See service_auth.h.
 */
#include "service_auth.h"

#include "mem/torirs_mem.h"
#include "net/sha256.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_savefile.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t g_key[SERVICE_AUTH_KEY_BYTES];
static volatile int g_loaded;

static int
hex_digit(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    c = (char)tolower((unsigned char)c);
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    return -1;
}

/* `text`'s first 64 hex digits as the key. 1 when there were 64. */
static int
key_from_hex(char const* text)
{
    for( int i = 0; i < SERVICE_AUTH_KEY_BYTES; i++ )
    {
        int hi = hex_digit(text[2 * i]);
        int lo = hi < 0 ? -1 : hex_digit(text[2 * i + 1]);

        if( lo < 0 )
            return 0;
        g_key[i] = (uint8_t)(hi * 16 + lo);
    }
    return 1;
}

static int
key_file_read(char const* path)
{
    char text[160] = "";
    FILE* file = fopen(path, "rb");

    if( !file )
        return 0;
    if( !fgets(text, sizeof(text), file) )
        text[0] = '\0';
    fclose(file);
    if( !key_from_hex(text) )
    {
        fprintf(stderr, "service: %s does not hold a key (64 hex digits)\n", path);
        abort();
    }
    return 1;
}

void
ServiceAuth_Load(void)
{
    char path[1024];
    char const* env = getenv("TORIRS_SERVICE_KEY");

    if( g_loaded )
        return;
    if( env && env[0] )
    {
        if( strlen(env) < SERVICE_AUTH_KEY_BYTES * 2 || !key_from_hex(env) )
        {
            fprintf(stderr, "service: TORIRS_SERVICE_KEY is not 64 hex digits\n");
            abort();
        }
        g_loaded = 1;
        return;
    }
    snprintf(path, sizeof(path), "%s/service.key", ToriRSServer_SaveDir());
    if( !key_file_read(path) )
    {
        uint8_t fresh[SERVICE_AUTH_KEY_BYTES];
        char text[SERVICE_AUTH_KEY_BYTES * 2 + 2];
        FILE* file;

        ToriRSServer_AuthRandom(fresh, sizeof(fresh));
        ToriRSServer_AuthHex(fresh, sizeof(fresh), text);
        strcat(text, "\n");
        ToriRSServer_SaveMkdirParent(path);
        /* "x": of several servers making it at once, one writes it and the
         * rest find it there and read it. */
        file = fopen(path, "wbx");
        if( file )
        {
            if( fputs(text, file) < 0 || fclose(file) != 0 )
            {
                fprintf(stderr, "service: cannot write %s\n", path);
                abort();
            }
        }
        if( !key_file_read(path) )
        {
            fprintf(stderr, "service: cannot make %s\n", path);
            abort();
        }
    }
    g_loaded = 1;
}

/* The signature's hex for `method` `path` `stamp` and the body's digest. */
static void
signature(
    char const* method,
    char const* path,
    long long stamp,
    void const* body,
    int body_len,
    char out[SHA256_DIGEST_BYTES * 2 + 1])
{
    uint8_t digest[SHA256_DIGEST_BYTES];
    char digest_hex[SHA256_DIGEST_BYTES * 2 + 1];
    char* message;
    size_t cap;
    int len;
    uint8_t mac[32];
    struct sha256_ctx ctx;

    sha256_init(&ctx);
    if( body_len > 0 )
        sha256_update(&ctx, (uint8_t const*)body, (size_t)body_len);
    sha256_final(&ctx, digest);
    ToriRSServer_AuthHex(digest, sizeof(digest), digest_hex);
    cap = strlen(method) + strlen(path) + sizeof(digest_hex) + 48;
    message = ToriMem_Alloc(ToriMem_Net(), cap);
    assert(message);
    len = snprintf(message, cap, "%s\n%s\n%lld\n%s", method, path, stamp, digest_hex);
    assert(len > 0 && (size_t)len < cap);
    ToriRSServer_AuthHmacSha256(g_key, sizeof(g_key), (uint8_t const*)message, (size_t)len, mac);
    ToriMem_Free(ToriMem_Net(), message);
    ToriRSServer_AuthHex(mac, sizeof(mac), out);
}

void
ServiceAuth_HeaderLine(
    char const* method,
    char const* path,
    void const* body,
    int body_len,
    char out[SERVICE_AUTH_LINE_MAX])
{
    char sig[SHA256_DIGEST_BYTES * 2 + 1];
    long long stamp = (long long)time(NULL);

    assert(g_loaded);
    assert(method);
    assert(path);
    assert(body || body_len == 0);
    assert(out);
    signature(method, path, stamp, body, body_len, sig);
    snprintf(out, SERVICE_AUTH_LINE_MAX, "%s: %lld.%s\r\n", SERVICE_AUTH_HEADER, stamp, sig);
}

int
ServiceAuth_Check(struct HttpRequest const* req)
{
    char value[160];
    char const* header;
    int len = 0;
    char* dot;
    long long stamp;
    long long now = (long long)time(NULL);
    char expect[SHA256_DIGEST_BYTES * 2 + 1];
    unsigned diff = 0;

    assert(req);
    if( req->trusted )
        return 1;
    assert(g_loaded);
    header = HttpRequest_Header(req, SERVICE_AUTH_HEADER, &len);
    if( !header || len <= 0 || len >= (int)sizeof(value) )
        return 0;
    memcpy(value, header, (size_t)len);
    value[len] = '\0';
    dot = strchr(value, '.');
    if( !dot || strlen(dot + 1) != SHA256_DIGEST_BYTES * 2 )
        return 0;
    *dot = '\0';
    stamp = atoll(value);
    if( stamp < now - SERVICE_AUTH_WINDOW_SECONDS || stamp > now + SERVICE_AUTH_WINDOW_SECONDS )
        return 0;
    signature(req->method, req->path, stamp, req->body, req->body ? req->body_len : 0, expect);
    for( int i = 0; i < SHA256_DIGEST_BYTES * 2; i++ )
        diff |= (unsigned)(expect[i] ^ dot[1 + i]);
    return diff == 0;
}
