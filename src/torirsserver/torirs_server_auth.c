/*
 * Passwords. See torirs_server_auth.h.
 */

#ifdef _WIN32
/* rand_s, the CRT's face of RtlGenRandom: declared only under this, and only
 * when it is defined before the first <stdlib.h>. */
#define _CRT_RAND_S
#endif

#include "torirs_server_auth.h"

#include "torirs_server_save.h"

#include "3rd/ini/ini.h"
#include "content/content_register.h"
#include "net/sha256.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Salt from the operating system's generator.
 *
 * A failure aborts rather than asserts: the shipping build compiles assert()
 * out, and a salt left as zeros is a record that still works -- which is
 * exactly the silent kind of wrong the abort is for.
 */
static void
auth_random(
    uint8_t* out,
    size_t len)
{
#if defined(_WIN32)
    for( size_t i = 0; i < len; i++ )
    {
        unsigned int word;

        if( rand_s(&word) != 0 )
        {
            fprintf(stderr, "torirsserver: rand_s failed; cannot salt a password\n");
            abort();
        }
        out[i] = (uint8_t)word;
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(out, len);
#else
    FILE* urandom = fopen("/dev/urandom", "rb");

    if( !urandom || fread(out, 1, len, urandom) != len )
    {
        fprintf(stderr, "torirsserver: cannot read /dev/urandom; cannot salt a password\n");
        abort();
    }
    fclose(urandom);
#endif
}

/* memset that the optimiser may not drop for writing memory nobody reads
 * again -- which is the point of wiping a password. */
static void
auth_wipe(
    void* p,
    size_t len)
{
    volatile uint8_t* bytes = (volatile uint8_t*)p;

    while( len-- )
        *bytes++ = 0;
}

/* HMAC-SHA256 with the key already absorbed: the inner and outer states after
 * one block of key^ipad / key^opad. Every PBKDF2 iteration starts from these. */
struct AuthHmac
{
    struct sha256_ctx inner;
    struct sha256_ctx outer;
};

static void
auth_hmac_key(
    struct AuthHmac* hmac,
    const uint8_t* key,
    size_t key_len)
{
    uint8_t block[SHA256_BLOCK_BYTES];
    uint8_t hashed[SHA256_DIGEST_BYTES];

    memset(block, 0, sizeof(block));
    if( key_len > SHA256_BLOCK_BYTES )
    {
        sha256(key, key_len, hashed);
        memcpy(block, hashed, sizeof(hashed));
    }
    else if( key_len > 0 )
    {
        memcpy(block, key, key_len);
    }

    for( int i = 0; i < SHA256_BLOCK_BYTES; i++ )
        block[i] ^= 0x36;
    sha256_init(&hmac->inner);
    sha256_update(&hmac->inner, block, sizeof(block));
    for( int i = 0; i < SHA256_BLOCK_BYTES; i++ )
        block[i] ^= 0x36 ^ 0x5c;
    sha256_init(&hmac->outer);
    sha256_update(&hmac->outer, block, sizeof(block));

    auth_wipe(block, sizeof(block));
    auth_wipe(hashed, sizeof(hashed));
}

static void
auth_hmac(
    const struct AuthHmac* hmac,
    const uint8_t* a,
    size_t a_len,
    const uint8_t* b,
    size_t b_len,
    uint8_t out[SHA256_DIGEST_BYTES])
{
    struct sha256_ctx ctx = hmac->inner;
    uint8_t inner[SHA256_DIGEST_BYTES];

    sha256_update(&ctx, a, a_len);
    sha256_update(&ctx, b, b_len);
    sha256_final(&ctx, inner);
    ctx = hmac->outer;
    sha256_update(&ctx, inner, sizeof(inner));
    sha256_final(&ctx, out);
}

void
ToriRSServer_AuthPbkdf2(
    const char* password,
    const uint8_t* salt,
    size_t salt_len,
    int iterations,
    uint8_t* out,
    size_t out_len)
{
    struct AuthHmac hmac;
    uint32_t block_index = 1;

    assert(password);
    assert(salt);
    assert(iterations > 0);
    assert(out);

    auth_hmac_key(&hmac, (const uint8_t*)password, strlen(password));
    while( out_len > 0 )
    {
        uint8_t counter[4] = { (uint8_t)(block_index >> 24), (uint8_t)(block_index >> 16),
                               (uint8_t)(block_index >> 8), (uint8_t)block_index };
        uint8_t u[SHA256_DIGEST_BYTES];
        uint8_t t[SHA256_DIGEST_BYTES];
        size_t take = out_len < sizeof(t) ? out_len : sizeof(t);

        auth_hmac(&hmac, salt, salt_len, counter, sizeof(counter), u);
        memcpy(t, u, sizeof(t));
        for( int i = 1; i < iterations; i++ )
        {
            auth_hmac(&hmac, u, sizeof(u), NULL, 0, u);
            for( size_t j = 0; j < sizeof(t); j++ )
                t[j] ^= u[j];
        }
        memcpy(out, t, take);
        out += take;
        out_len -= take;
        block_index++;
    }
    auth_wipe(&hmac, sizeof(hmac));
}

static void
auth_hex(
    const uint8_t* bytes,
    size_t len,
    char* out)
{
    static const char digits[] = "0123456789abcdef";

    for( size_t i = 0; i < len; i++ )
    {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[len * 2] = '\0';
}

static int
auth_nibble(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return -1;
}

/* Decode exactly `hex_len` hex digits into `out`. 0 on any non-digit. */
static int
auth_unhex(
    const char* hex,
    size_t hex_len,
    uint8_t* out)
{
    if( hex_len % 2 != 0 )
        return 0;
    for( size_t i = 0; i < hex_len / 2; i++ )
    {
        int hi = auth_nibble(hex[i * 2]);
        int lo = auth_nibble(hex[i * 2 + 1]);

        if( hi < 0 || lo < 0 )
            return 0;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 1;
}

#define AUTH_SCHEME "pbkdf2-sha256$"

void
ToriRSServer_AuthRecord(
    const char* password,
    const uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES],
    int iterations,
    char* out)
{
    uint8_t key[TORIRSSERVER_AUTH_KEY_BYTES];
    char salt_hex[TORIRSSERVER_AUTH_SALT_BYTES * 2 + 1];
    char key_hex[TORIRSSERVER_AUTH_KEY_BYTES * 2 + 1];

    assert(out);
    ToriRSServer_AuthPbkdf2(password, salt, TORIRSSERVER_AUTH_SALT_BYTES, iterations, key,
                            sizeof(key));
    auth_hex(salt, TORIRSSERVER_AUTH_SALT_BYTES, salt_hex);
    auth_hex(key, sizeof(key), key_hex);
    snprintf(out, TORIRSSERVER_AUTH_RECORD_MAX, AUTH_SCHEME "%d$%s$%s", iterations, salt_hex,
             key_hex);
    auth_wipe(key, sizeof(key));
}

int
ToriRSServer_AuthVerify(
    const char* password,
    const char* record)
{
    const char* scan;
    const char* salt_hex;
    const char* key_hex;
    char* end;
    long iterations;
    size_t salt_hex_len;
    uint8_t salt[64];
    uint8_t want[TORIRSSERVER_AUTH_KEY_BYTES];
    uint8_t got[TORIRSSERVER_AUTH_KEY_BYTES];
    uint8_t diff = 0;

    assert(password);
    assert(record);
    if( strncmp(record, AUTH_SCHEME, strlen(AUTH_SCHEME)) != 0 )
        return 0;
    scan = record + strlen(AUTH_SCHEME);
    iterations = strtol(scan, &end, 10);
    /* A ceiling, because the count is read from a file and every login pays it
     * on the host loop: a record saying two billion would stall the world. */
    if( end == scan || *end != '$' || iterations < 1 || iterations > 10000000 )
        return 0;
    salt_hex = end + 1;
    scan = strchr(salt_hex, '$');
    if( !scan )
        return 0;
    salt_hex_len = (size_t)(scan - salt_hex);
    key_hex = scan + 1;
    if( salt_hex_len == 0 || salt_hex_len > sizeof(salt) * 2 ||
        strlen(key_hex) != sizeof(want) * 2 )
        return 0;
    if( !auth_unhex(salt_hex, salt_hex_len, salt) || !auth_unhex(key_hex, sizeof(want) * 2, want) )
        return 0;

    ToriRSServer_AuthPbkdf2(password, salt, salt_hex_len / 2, (int)iterations, got, sizeof(got));
    for( size_t i = 0; i < sizeof(got); i++ )
        diff |= (uint8_t)(got[i] ^ want[i]);
    auth_wipe(got, sizeof(got));
    return diff == 0;
}

/*
 * Read the name's record into `record`.
 *
 * 1 = read, 0 = there is no record (the file does not exist), -1 = there is
 * one and it cannot be used. Only 0 lets a login claim the name.
 */
static int
auth_read_record(
    const char* path,
    char* record,
    size_t cap)
{
    uint8_t data[4096];
    size_t size;
    FILE* file;
    struct INIReader reader;
    struct INIElement element;
    int in_account = 0;
    int found = 0;

    file = fopen(path, "rb");
    if( !file )
    {
        if( errno == ENOENT )
            return 0;
        fprintf(stderr, "torirsserver: cannot open account record %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    size = fread(data, 1, sizeof(data), file);
    if( ferror(file) || size == sizeof(data) )
    {
        fclose(file);
        fprintf(stderr, "torirsserver: account record %s is unreadable or too large\n", path);
        return -1;
    }
    fclose(file);

    ini_reader_init(&reader);
    for( ;; )
    {
        int status = ini_reader_next(&reader, data, (uint32_t)size, &element);

        if( status != TORI_INI_ERR_OK )
        {
            if( status != TORI_INI_ERR_NONE )
            {
                fprintf(stderr, "torirsserver: account record %s does not parse\n", path);
                return -1;
            }
            break;
        }
        /* 3rd/ini does not trim — see ContentIni_Trim. */
        if( element.kind == INI_ELEMENT_SECTION )
        {
            ContentIni_Trim(element._section.name);
            in_account = strcmp(element._section.name, "account") == 0;
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL || !in_account )
            continue;
        ContentIni_Trim(element._keyval.name);
        ContentIni_Trim(element._keyval.value);
        if( strcmp(element._keyval.name, "password") == 0 )
        {
            snprintf(record, cap, "%s", element._keyval.value);
            found = 1;
        }
    }
    if( !found )
    {
        fprintf(stderr, "torirsserver: account record %s has no [account] password\n", path);
        return -1;
    }
    return 1;
}

static int
auth_write_record(
    const char* path,
    const char* record)
{
    char text[512];

    snprintf(text, sizeof(text),
             "; ToriRSServer account. Written by torirs_server_auth.c.\n"
             "; Delete this file to reset the password: the next login sets it.\n"
             "\n[account]\n"
             "password = %s\n",
             record);
    return ToriRSServer_SaveWriteText(path, text);
}

enum ToriRSServerAuthResult
ToriRSServer_AuthLogin(
    const char* display_name,
    const char* password)
{
    char path[1024];
    char record[TORIRSSERVER_AUTH_RECORD_MAX * 2];
    uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES];
    int have;

    assert(display_name);
    assert(password);

    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || !password[0] )
        return TORIRSSERVER_AUTH_UNUSABLE;

    have = auth_read_record(path, record, sizeof(record));
    if( have < 0 )
        return TORIRSSERVER_AUTH_REFUSED;
    if( have > 0 )
    {
        if( !ToriRSServer_AuthVerify(password, record) )
            return TORIRSSERVER_AUTH_REFUSED;
        return TORIRSSERVER_AUTH_OK;
    }

    auth_random(salt, sizeof(salt));
    ToriRSServer_AuthRecord(password, salt, TORIRSSERVER_AUTH_ITERATIONS, record);
    /* A claim that could not be written must not log in: the next login would
     * find no record and claim the name again, with any password. */
    if( !auth_write_record(path, record) )
        return TORIRSSERVER_AUTH_REFUSED;
    return TORIRSSERVER_AUTH_CLAIMED;
}
