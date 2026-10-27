/*
 * Passwords. See torirs_server_auth.h.
 */

#ifdef _WIN32
/* rand_s, the CRT's face of RtlGenRandom: declared only under this, and only
 * when it is defined before the first <stdlib.h>. */
#define _CRT_RAND_S
#endif

#include "torirs_server_auth.h"

#include "torirs_server_savefile.h"

#include "3rd/ini/ini.h"
#include "net/sha256.h"

#include <assert.h>
#include <ctype.h>
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
 * The keys an account record or an invite can carry. One reader for both,
 * because both are a single INI section of the same few keys.
 */
struct AuthFile
{
    char password[TORIRSSERVER_AUTH_RECORD_MAX * 2];
    int has_password;
    char name[TORIRSSERVER_AUTH_NAME_MAX + 1];
    int admin;
    /** The web login that owns this player account (ToriRSServer_AuthLink);
     *  "" when it signs in with its own password. */
    char managed_by[64];
    /** SHA-256 hex of the link token the owner's links database holds; the
     *  one link this account answers to. "" with managed_by "". */
    char link_hash[TORIRSSERVER_LINK_TOKEN_HEX + 1];
};

/* 3rd/ini does not trim, and io_server links no content code for
 * ContentIni_Trim, so the same trim lives here. */
static void
auth_trim(char* text)
{
    size_t len = strlen(text);
    size_t start = 0;

    while( len > 0 && isspace((unsigned char)text[len - 1]) )
        text[--len] = '\0';
    while( start < len && isspace((unsigned char)text[start]) )
        start++;
    if( start > 0 )
        memmove(text, text + start, len - start + 1);
}

/*
 * Read `section` of `path` into `out`.
 *
 * 1 = read, 0 = there is no file, -1 = there is one and it cannot be read or
 * parsed. Only 0 lets a login claim the name: a damaged record must not reopen
 * it to whoever logs in next.
 */
static int
auth_read_file(
    const char* path,
    const char* section,
    struct AuthFile* out)
{
    uint8_t data[4096];
    size_t size;
    FILE* file;
    struct INIReader reader;
    struct INIElement element;
    int in_section = 0;

    memset(out, 0, sizeof(*out));
    file = fopen(path, "rb");
    if( !file )
    {
        if( errno == ENOENT )
            return 0;
        fprintf(stderr, "torirsserver: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    size = fread(data, 1, sizeof(data), file);
    if( ferror(file) || size == sizeof(data) )
    {
        fclose(file);
        fprintf(stderr, "torirsserver: %s is unreadable or too large\n", path);
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
                fprintf(stderr, "torirsserver: %s does not parse\n", path);
                return -1;
            }
            break;
        }
        if( element.kind == INI_ELEMENT_SECTION )
        {
            auth_trim(element._section.name);
            in_section = strcmp(element._section.name, section) == 0;
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL || !in_section )
            continue;
        auth_trim(element._keyval.name);
        auth_trim(element._keyval.value);
        if( strcmp(element._keyval.name, "password") == 0 )
        {
            snprintf(out->password, sizeof(out->password), "%s", element._keyval.value);
            out->has_password = 1;
        }
        else if( strcmp(element._keyval.name, "name") == 0 )
            snprintf(out->name, sizeof(out->name), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "admin") == 0 )
            out->admin = atoi(element._keyval.value) != 0;
        else if( strcmp(element._keyval.name, "managed_by") == 0 )
            snprintf(out->managed_by, sizeof(out->managed_by), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "link_hash") == 0 )
            snprintf(out->link_hash, sizeof(out->link_hash), "%s", element._keyval.value);
    }
    return 1;
}

/* An account record, which must carry a password to be one. */
static int
auth_read_record(
    const char* path,
    struct AuthFile* out)
{
    int have = auth_read_file(path, "account", out);

    if( have > 0 && !out->has_password )
    {
        fprintf(stderr, "torirsserver: account record %s has no [account] password\n", path);
        return -1;
    }
    return have;
}

/* `managed_by` is the web login that owns the account and `link_hash` its
 * link token's hash, or both "" for none. */
static int
auth_write_record(
    const char* path,
    const char* record,
    int admin,
    const char* managed_by,
    const char* link_hash)
{
    char text[896];
    char managed_line[192] = "";

    assert(managed_by);
    assert(link_hash);
    assert(!managed_by[0] == !link_hash[0]);
    if( managed_by[0] )
        snprintf(managed_line, sizeof(managed_line), "managed_by = %s\nlink_hash = %s\n",
                 managed_by, link_hash);
    snprintf(text, sizeof(text),
             "; ToriRSServer account. Written by torirs_server_auth.c.\n"
             "; Delete this file to reset the password: the next login sets it.\n"
             "; admin = 1 is staff: the jmod crown, and ::commands on any world\n"
             "; (torirsserver --account admin <name> on).\n"
             "; managed_by = <web login> is an account linked to that login: it signs\n"
             "; in only through the gate's session presenting the link token whose\n"
             "; hash is link_hash, never with this password\n"
             "; (torirsserver --account unlink <name> undoes it).\n"
             "\n[account]\n"
             "password = %s\n"
             "%s%s",
             record, admin ? "admin = 1\n" : "", managed_line);
    return ToriRSServer_SaveWriteText(path, text);
}

const char*
ToriRSServer_AuthNameProblem(const char* display_name)
{
    size_t len;
    int letters = 0;

    assert(display_name);
    len = strlen(display_name);
    if( len < 1 || len > TORIRSSERVER_AUTH_NAME_MAX )
        return "Names are 1 to 12 characters long.";
    for( size_t i = 0; i < len; i++ )
    {
        unsigned char ch = (unsigned char)display_name[i];

        if( isalpha(ch) )
            letters++;
        else if( !isdigit(ch) && ch != ' ' && ch != '_' && ch != '-' )
            return "Names use letters, numbers, spaces, '-' and '_' only.";
    }
    if( !letters )
        return "Names need at least one letter.";
    if( display_name[0] == ' ' || display_name[len - 1] == ' ' )
        return "Names cannot start or end with a space.";
    return NULL;
}

const char*
ToriRSServer_AuthPasswordProblem(
    const char* display_name,
    const char* password)
{
    char name[64];
    char lowered[TORIRSSERVER_AUTH_PASSWORD_MAX + 1];
    size_t len;
    int letters = 0;
    int digits = 0;
    int all_same = 1;

    assert(display_name);
    assert(password);
    len = strlen(password);
    if( len < TORIRSSERVER_AUTH_PASSWORD_MIN || len > TORIRSSERVER_AUTH_PASSWORD_MAX )
        return "Passwords are 8 to 20 characters long.";
    for( size_t i = 0; i < len; i++ )
    {
        unsigned char ch = (unsigned char)password[i];

        /* Printable ASCII without the space: what the login screen's field
         * accepts, so a password set here can always be typed there. */
        if( ch <= ' ' || ch > '~' )
            return "Passwords use letters, numbers and symbols, with no spaces.";
        if( isalpha(ch) )
            letters++;
        if( isdigit(ch) )
            digits++;
        if( ch != (unsigned char)password[0] )
            all_same = 0;
        lowered[i] = (char)tolower(ch);
    }
    lowered[len] = '\0';
    if( !letters || !digits )
        return "Passwords need at least one letter and one number.";
    if( all_same )
        return "Passwords cannot be one character repeated.";
    /* The sanitised name, as the account is keyed: "Bob Smith" is
     * "bob_smith", and both "bob_smith1" and "bobsmith1" are refused. */
    ToriRSServer_SaveFileName(display_name, name, sizeof(name));
    if( name[0] )
    {
        char squeezed[64];
        size_t used = 0;

        if( strstr(lowered, name) )
            return "Passwords cannot contain the account name.";
        for( const char* scan = name; *scan && used < sizeof(squeezed) - 1; scan++ )
            if( *scan != '_' )
                squeezed[used++] = *scan;
        squeezed[used] = '\0';
        if( used >= 3 && strstr(lowered, squeezed) )
            return "Passwords cannot contain the account name.";
    }
    return NULL;
}

enum ToriRSServerAuthResult
ToriRSServer_AuthLogin(
    const char* display_name,
    const char* password)
{
    char path[1024];
    char record[TORIRSSERVER_AUTH_RECORD_MAX];
    struct AuthFile file;
    uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES];
    int have;

    assert(display_name);
    assert(password);

    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || !password[0] )
        return TORIRSSERVER_AUTH_UNUSABLE;

    have = auth_read_record(path, &file);
    if( have < 0 )
        return TORIRSSERVER_AUTH_REFUSED;
    if( have > 0 )
    {
        int ok = ToriRSServer_AuthVerify(password, file.password);
        int managed = file.managed_by[0] != '\0';

        auth_wipe(&file, sizeof(file));
        /* Judged after the password, so only someone who knows it learns the
         * account is linked; to anyone else it is a wrong password. */
        if( ok && managed )
            return TORIRSSERVER_AUTH_MANAGED;
        return ok ? TORIRSSERVER_AUTH_OK : TORIRSSERVER_AUTH_REFUSED;
    }

    /* A claim sets the password, so it is held to the same rules as one set
     * through an invite. */
    if( ToriRSServer_AuthPasswordProblem(display_name, password) )
        return TORIRSSERVER_AUTH_WEAK;
    auth_random(salt, sizeof(salt));
    ToriRSServer_AuthRecord(password, salt, TORIRSSERVER_AUTH_ITERATIONS, record);
    /* A claim that could not be written must not log in: the next login would
     * find no record and claim the name again, with any password. */
    if( !auth_write_record(path, record, 0, "", "") )
        return TORIRSSERVER_AUTH_REFUSED;
    return TORIRSSERVER_AUTH_CLAIMED;
}

int
ToriRSServer_AuthIsAdmin(const char* display_name)
{
    char path[1024];
    struct AuthFile file;
    int admin;

    assert(display_name);
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || auth_read_record(path, &file) <= 0 )
        return 0;
    admin = file.admin;
    auth_wipe(&file, sizeof(file));
    return admin;
}

int
ToriRSServer_AuthSetAdmin(
    const char* display_name,
    int admin)
{
    char path[1024];
    struct AuthFile file;
    int written;

    assert(display_name);
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || auth_read_record(path, &file) <= 0 )
        return 0;
    written = auth_write_record(path, file.password, admin, file.managed_by, file.link_hash);
    auth_wipe(&file, sizeof(file));
    return written;
}

/* ------------------------------------------------------------------ */
/* Player accounts linked to a web login                               */
/* ------------------------------------------------------------------ */

int
ToriRSServer_AuthManagedBy(
    const char* display_name,
    char* out_user,
    size_t cap)
{
    char path[1024];
    struct AuthFile file;
    int have;

    assert(display_name);
    assert(out_user);
    assert(cap > 0);
    out_user[0] = '\0';
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] )
        return 0;
    have = auth_read_record(path, &file);
    if( have <= 0 )
        return have;
    snprintf(out_user, cap, "%s", file.managed_by);
    auth_wipe(&file, sizeof(file));
    return out_user[0] ? 1 : 0;
}

/* SHA-256 of a link token's hex, as hex: what the record keeps. */
static void
auth_link_hash(
    const char* link_token,
    char out[TORIRSSERVER_LINK_TOKEN_HEX + 1])
{
    uint8_t digest[SHA256_DIGEST_BYTES];

    sha256((const uint8_t*)link_token, strlen(link_token), digest);
    ToriRSServer_AuthHex(digest, sizeof(digest), out);
}

/* A fresh link token into `out_token`, its hash into `out_hash`. */
static void
auth_link_mint(
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1],
    char out_hash[TORIRSSERVER_LINK_TOKEN_HEX + 1])
{
    uint8_t bytes[TORIRSSERVER_LINK_TOKEN_HEX / 2];

    auth_random(bytes, sizeof(bytes));
    ToriRSServer_AuthHex(bytes, sizeof(bytes), out_token);
    auth_wipe(bytes, sizeof(bytes));
    auth_link_hash(out_token, out_hash);
}

enum ToriRSServerLinkResult
ToriRSServer_AuthLink(
    const char* display_name,
    const char* password,
    const char* user,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1])
{
    char path[1024];
    char owner[64];
    char hash[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    struct AuthFile file;
    int have;
    int written;

    assert(display_name);
    assert(password);
    assert(user);
    assert(out_token);
    out_token[0] = '\0';
    /* The web login as it is keyed: a ticket names it sanitised already, and
     * the comparison at every later play is against this spelling. */
    ToriRSServer_SaveFileName(user, owner, sizeof(owner));
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || !password[0] || !owner[0] )
        return TORIRSSERVER_LINK_REFUSED;

    have = auth_read_record(path, &file);
    if( have < 0 )
        return TORIRSSERVER_LINK_REFUSED;
    /* Only an account that exists can be linked: a link never creates one.
     * The name is made the ordinary way, by logging in to the game. */
    if( have == 0 )
        return TORIRSSERVER_LINK_NO_ACCOUNT;
    /* The password is the proof the player account is the linker's to give:
     * a gate session alone must not be able to take a name. */
    if( !ToriRSServer_AuthVerify(password, file.password) )
    {
        auth_wipe(&file, sizeof(file));
        return TORIRSSERVER_LINK_REFUSED;
    }
    /* One owner. Its own owner linking again gets a NEW token -- the way back
     * when its links database lost the row -- and the old one stops working. */
    if( file.managed_by[0] && strcmp(file.managed_by, owner) != 0 )
    {
        auth_wipe(&file, sizeof(file));
        return TORIRSSERVER_LINK_OTHER_USER;
    }
    auth_link_mint(out_token, hash);
    written = auth_write_record(path, file.password, file.admin, owner, hash);
    auth_wipe(&file, sizeof(file));
    if( written )
        return TORIRSSERVER_LINK_OK;
    out_token[0] = '\0';
    return TORIRSSERVER_LINK_WRITE_FAILED;
}

int
ToriRSServer_AuthUnlink(const char* display_name)
{
    char path[1024];
    struct AuthFile file;
    int written;

    assert(display_name);
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || auth_read_record(path, &file) <= 0 )
        return 0;
    written = auth_write_record(path, file.password, file.admin, "", "");
    auth_wipe(&file, sizeof(file));
    return written;
}

int
ToriRSServer_AuthLinkVerify(
    const char* display_name,
    const char* user,
    const char* link_token)
{
    char path[1024];
    char hash[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    struct AuthFile file;
    unsigned diff = 0;
    int ok;

    assert(display_name);
    assert(user);
    assert(link_token);
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || strlen(link_token) != TORIRSSERVER_LINK_TOKEN_HEX ||
        auth_read_record(path, &file) <= 0 )
        return 0;
    auth_link_hash(link_token, hash);
    /* The same time wherever the hashes differ, as AuthVerify compares keys. */
    for( size_t i = 0; i < TORIRSSERVER_LINK_TOKEN_HEX; i++ )
        diff |= (unsigned)(hash[i] ^ file.link_hash[i]);
    ok = file.managed_by[0] && strcmp(file.managed_by, user) == 0 &&
         strlen(file.link_hash) == TORIRSSERVER_LINK_TOKEN_HEX && diff == 0;
    auth_wipe(&file, sizeof(file));
    return ok;
}

/* ------------------------------------------------------------------ */
/* The gate's ticket                                                   */
/* ------------------------------------------------------------------ */

static uint8_t g_gate_key[TORIRSSERVER_GATE_KEY_BYTES];
static int g_gate_key_loaded;

void
ToriRSServer_KeyFileLoad(
    const char* path,
    uint8_t* out,
    size_t len)
{
    char hex[256];
    FILE* file;
    size_t got;

    assert(path);
    assert(out);
    assert(len * 2 + 2 <= sizeof(hex));
    file = fopen(path, "rb");
    if( !file )
    {
        uint8_t fresh[64];
        char text[sizeof(hex)];

        /* io_server and torirsserver both start here, so the first writes it
         * and the second reads what the first wrote. The atomic replace means
         * a reader never sees half a key; two first starts in the same instant
         * would each keep its own, which a restart of either resolves. */
        assert(len <= sizeof(fresh));
        auth_random(fresh, len);
        auth_hex(fresh, len, text);
        strcat(text, "\n");
        if( !ToriRSServer_SaveWriteText(path, text) )
        {
            fprintf(stderr, "torirsserver: cannot write the key %s\n", path);
            abort();
        }
        auth_wipe(fresh, sizeof(fresh));
        file = fopen(path, "rb");
        if( !file )
        {
            fprintf(stderr, "torirsserver: cannot read back the key %s\n", path);
            abort();
        }
    }
    got = fread(hex, 1, sizeof(hex) - 1, file);
    fclose(file);
    hex[got] = '\0';
    auth_trim(hex);
    if( strlen(hex) != len * 2 || !auth_unhex(hex, strlen(hex), out) )
    {
        fprintf(stderr, "torirsserver: the key %s is not %d hex bytes\n", path, (int)len);
        abort();
    }
    auth_wipe(hex, sizeof(hex));
}

void
ToriRSServer_GateKeyLoad(void)
{
    char path[1024];

    snprintf(path, sizeof(path), "%s/gate/gate.key", ToriRSServer_SaveDir());
    ToriRSServer_KeyFileLoad(path, g_gate_key, sizeof(g_gate_key));
    g_gate_key_loaded = 1;
}

void
ToriRSServer_AuthRandom(
    uint8_t* out,
    size_t len)
{
    assert(out);
    auth_random(out, len);
}

void
ToriRSServer_AuthHex(
    const uint8_t* bytes,
    size_t len,
    char* out)
{
    assert(bytes);
    assert(out);
    auth_hex(bytes, len, out);
}

void
ToriRSServer_AuthHmacSha256(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* message,
    size_t message_len,
    uint8_t out[32])
{
    struct AuthHmac hmac;

    assert(key);
    assert(message);
    assert(out);
    auth_hmac_key(&hmac, key, key_len);
    auth_hmac(&hmac, message, message_len, NULL, 0, out);
    auth_wipe(&hmac, sizeof(hmac));
}

/* HMAC(gate key, "<name>.<expires>") as hex. */
static void
auth_gate_mac(
    const char* name,
    long long expires,
    char out[SHA256_DIGEST_BYTES * 2 + 1])
{
    struct AuthHmac hmac;
    char body[96];
    uint8_t mac[SHA256_DIGEST_BYTES];
    int len;

    assert(g_gate_key_loaded);
    len = snprintf(body, sizeof(body), "%s.%lld", name, expires);
    auth_hmac_key(&hmac, g_gate_key, sizeof(g_gate_key));
    auth_hmac(&hmac, (const uint8_t*)body, (size_t)len, NULL, 0, mac);
    auth_hex(mac, sizeof(mac), out);
    auth_wipe(&hmac, sizeof(hmac));
}

void
ToriRSServer_GateTicketMint(
    const char* name,
    long long expires,
    char out[TORIRSSERVER_GATE_TICKET_MAX])
{
    char file[64];
    char mac[SHA256_DIGEST_BYTES * 2 + 1];

    assert(name);
    assert(out);
    ToriRSServer_SaveFileName(name, file, sizeof(file));
    assert(file[0]);
    auth_gate_mac(file, expires, mac);
    snprintf(out, TORIRSSERVER_GATE_TICKET_MAX, "%s.%lld.%s", file, expires, mac);
}

int
ToriRSServer_GateTicketCheck(
    const char* ticket,
    size_t len,
    long long now,
    char* out_name,
    size_t cap)
{
    char copy[TORIRSSERVER_GATE_TICKET_MAX];
    char want[SHA256_DIGEST_BYTES * 2 + 1];
    char* first;
    char* second;
    char* end;
    long long expires;
    uint8_t diff = 0;

    assert(ticket);
    assert(out_name);
    if( len == 0 || len >= sizeof(copy) )
        return 0;
    memcpy(copy, ticket, len);
    copy[len] = '\0';
    first = strchr(copy, '.');
    if( !first )
        return 0;
    second = strchr(first + 1, '.');
    if( !second )
        return 0;
    *first = '\0';
    *second = '\0';
    if( !copy[0] || strlen(copy) >= cap )
        return 0;
    for( const char* scan = copy; *scan; scan++ )
        if( !islower((unsigned char)*scan) && !isdigit((unsigned char)*scan) && *scan != '_' )
            return 0;
    expires = strtoll(first + 1, &end, 10);
    if( end == first + 1 || *end != '\0' || expires < now )
        return 0;
    if( strlen(second + 1) != sizeof(want) - 1 )
        return 0;
    auth_gate_mac(copy, expires, want);
    for( size_t i = 0; i < sizeof(want) - 1; i++ )
        diff |= (uint8_t)(want[i] ^ second[1 + i]);
    if( diff != 0 )
        return 0;
    snprintf(out_name, cap, "%s", copy);
    return 1;
}

int
ToriRSServer_GateCookieTicket(
    const char* cookie,
    size_t len,
    long long now,
    char* out_name,
    size_t cap)
{
    static const char want[] = TORIRSSERVER_GATE_COOKIE "=";
    size_t at = 0;

    assert(cookie);
    assert(out_name);
    /* `a=1; torirs_gate=...; b=2`: every pair, so a cookie of the same name
     * set for a parent domain cannot hide ours by coming first. */
    while( at < len )
    {
        size_t end;

        while( at < len && (cookie[at] == ' ' || cookie[at] == ';') )
            at++;
        end = at;
        while( end < len && cookie[end] != ';' )
            end++;
        if( end - at > sizeof(want) - 1 && memcmp(cookie + at, want, sizeof(want) - 1) == 0 &&
            ToriRSServer_GateTicketCheck(cookie + at + sizeof(want) - 1,
                                         end - at - (sizeof(want) - 1), now, out_name, cap) )
            return 1;
        at = end;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Accounts made by an operator (torirsserver --account, torirsctl)    */
/* ------------------------------------------------------------------ */

/* The name and password rules every new account meets, and that no record
 * exists at `path` (a damaged one counts: it is not to be overwritten). */
static enum ToriRSServerCreateResult
auth_create_check(
    const char* name,
    const char* password,
    const char* path,
    const char** out_problem)
{
    struct AuthFile existing;
    int have;

    *out_problem = ToriRSServer_AuthNameProblem(name);
    if( *out_problem )
        return TORIRSSERVER_CREATE_BAD_NAME;
    assert(path[0]); /* AuthNameProblem passed, so the name has a letter */
    have = auth_read_record(path, &existing);
    auth_wipe(&existing, sizeof(existing));
    if( have != 0 )
        return TORIRSSERVER_CREATE_EXISTS;
    *out_problem = ToriRSServer_AuthPasswordProblem(name, password);
    if( *out_problem )
        return TORIRSSERVER_CREATE_WEAK;
    return TORIRSSERVER_CREATE_OK;
}

enum ToriRSServerCreateResult
ToriRSServer_AuthCreate(
    const char* display_name,
    const char* password,
    int admin,
    const char** out_problem)
{
    char path[1024];
    char record[TORIRSSERVER_AUTH_RECORD_MAX];
    uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES];
    enum ToriRSServerCreateResult result;

    assert(display_name);
    assert(password);
    assert(out_problem);
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    result = auth_create_check(display_name, password, path, out_problem);
    if( result != TORIRSSERVER_CREATE_OK )
        return result;
    auth_random(salt, sizeof(salt));
    ToriRSServer_AuthRecord(password, salt, TORIRSSERVER_AUTH_ITERATIONS, record);
    return auth_write_record(path, record, admin, "", "") ? TORIRSSERVER_CREATE_OK
                                                          : TORIRSSERVER_CREATE_WRITE_FAILED;
}

int
ToriRSServer_AuthSetPassword(
    const char* display_name,
    const char* password,
    const char** out_problem)
{
    char path[1024];
    char record[TORIRSSERVER_AUTH_RECORD_MAX];
    uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES];
    struct AuthFile file;
    int written;

    assert(display_name);
    assert(password);
    assert(out_problem);
    *out_problem = ToriRSServer_AuthPasswordProblem(display_name, password);
    if( *out_problem )
        return 0;
    snprintf(path, sizeof(path), "%s", ToriRSServer_AccountPath(display_name));
    if( !path[0] || auth_read_record(path, &file) <= 0 )
    {
        *out_problem = "There is no player account by that name.";
        return 0;
    }
    auth_random(salt, sizeof(salt));
    ToriRSServer_AuthRecord(password, salt, TORIRSSERVER_AUTH_ITERATIONS, record);
    /* Admin and any link stay as they were: this is the password only. */
    written = auth_write_record(path, record, file.admin, file.managed_by, file.link_hash);
    auth_wipe(&file, sizeof(file));
    auth_wipe(record, sizeof(record));
    if( !written )
        *out_problem = "The record could not be written.";
    return written;
}
