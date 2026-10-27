/*
 * OAuth 2.0 for player accounts. See torirs_server_oauth.h.
 */
#include "torirs_server_oauth.h"

#include "torirs_server_auth.h"
#include "torirs_server_savefile.h"

#include "3rd/ini/ini.h"
#include "net/flat_json.h"
#include "net/sha256.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OAUTH_KEY_BYTES 32

static uint8_t g_oauth_key[OAUTH_KEY_BYTES];
static int g_oauth_key_loaded;

const char*
ToriRSServer_OAuthErrorName(enum ToriRSServerOAuthError error)
{
    switch( error )
    {
    case TORIRSSERVER_OAUTH_OK:
        return "ok";
    case TORIRSSERVER_OAUTH_INVALID_REQUEST:
        return "invalid_request";
    case TORIRSSERVER_OAUTH_INVALID_CLIENT:
        return "invalid_client";
    case TORIRSSERVER_OAUTH_INVALID_GRANT:
        return "invalid_grant";
    case TORIRSSERVER_OAUTH_UNSUPPORTED_GRANT_TYPE:
        return "unsupported_grant_type";
    case TORIRSSERVER_OAUTH_INVALID_SCOPE:
        return "invalid_scope";
    case TORIRSSERVER_OAUTH_INVALID_TOKEN:
        return "invalid_token";
    case TORIRSSERVER_OAUTH_SERVER_ERROR:
        return "server_error";
    }
    return "server_error";
}

void
ToriRSServer_OAuthKeyLoad(void)
{
    char path[1024];

    snprintf(path, sizeof(path), "%s/oauth/signing.key", ToriRSServer_SaveDir());
    ToriRSServer_KeyFileLoad(path, g_oauth_key, sizeof(g_oauth_key));
    g_oauth_key_loaded = 1;
}

/* ------------------------------------------------------------------ */
/* base64url and JSON text                                             */
/* ------------------------------------------------------------------ */

static const char k_b64url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void
ToriRSServer_OAuthBase64Url(
    const unsigned char* bytes,
    size_t len,
    char* out)
{
    size_t used = 0;

    assert(bytes || len == 0);
    assert(out);
    for( size_t i = 0; i < len; i += 3 )
    {
        uint32_t word = (uint32_t)bytes[i] << 16;
        size_t take = len - i < 3 ? len - i : 3;

        if( take > 1 )
            word |= (uint32_t)bytes[i + 1] << 8;
        if( take > 2 )
            word |= bytes[i + 2];
        out[used++] = k_b64url[word >> 18 & 63];
        out[used++] = k_b64url[word >> 12 & 63];
        if( take > 1 )
            out[used++] = k_b64url[word >> 6 & 63];
        if( take > 2 )
            out[used++] = k_b64url[word & 63];
    }
    out[used] = '\0';
}

static int
b64url_value(char c)
{
    const char* at = strchr(k_b64url, c);

    return c && at ? (int)(at - k_b64url) : -1;
}

/* Decode `len` base64url characters into `out` (cap bytes). The byte count,
 * or -1 when malformed or too long. */
static int
b64url_decode(
    const char* text,
    size_t len,
    unsigned char* out,
    size_t cap)
{
    size_t used = 0;
    uint32_t word = 0;
    int bits = 0;

    if( len % 4 == 1 )
        return -1;
    for( size_t i = 0; i < len; i++ )
    {
        int value = b64url_value(text[i]);

        if( value < 0 )
            return -1;
        word = word << 6 | (uint32_t)value;
        bits += 6;
        if( bits >= 8 )
        {
            bits -= 8;
            if( used >= cap )
                return -1;
            out[used++] = (unsigned char)(word >> bits & 0xff);
        }
    }
    return (int)used;
}

/* `text` as the inside of a JSON string. Names reach here from login screens,
 * so a quote or a backslash in one must not end the string. */
static void
json_escape(
    const char* text,
    char* out,
    size_t cap)
{
    size_t used = 0;

    for( ; *text && used + 7 < cap; text++ )
    {
        unsigned char ch = (unsigned char)*text;

        if( ch == '"' || ch == '\\' )
        {
            out[used++] = '\\';
            out[used++] = (char)ch;
        }
        else if( ch < 0x20 )
            used += (size_t)snprintf(out + used, cap - used, "\\u%04x", ch);
        else
            out[used++] = (char)ch;
    }
    out[used] = '\0';
}

/* ------------------------------------------------------------------ */
/* JWT                                                                 */
/* ------------------------------------------------------------------ */

/* {"alg":"HS256","typ":"JWT"}, base64url. Fixed, so it is compared, not parsed:
 * a token naming any other algorithm -- "none" above all -- is not ours. */
static const char k_jwt_header[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";

static void
new_id(char out[TORIRSSERVER_OAUTH_ID_HEX + 1])
{
    uint8_t bytes[TORIRSSERVER_OAUTH_ID_HEX / 2];

    ToriRSServer_AuthRandom(bytes, sizeof(bytes));
    ToriRSServer_AuthHex(bytes, sizeof(bytes), out);
}

/* `claims` (a JSON object) as a signed compact JWT into `out`. */
static void
jwt_sign(
    const char* claims,
    char out[TORIRSSERVER_OAUTH_TOKEN_MAX])
{
    char body[TORIRSSERVER_OAUTH_TOKEN_MAX];
    uint8_t mac[32];
    char mac_text[48];
    int len;

    assert(g_oauth_key_loaded);
    len = snprintf(body, sizeof(body), "%s.", k_jwt_header);
    assert(len > 0);
    assert((size_t)len + (strlen(claims) + 2) / 3 * 4 + 1 < sizeof(body));
    ToriRSServer_OAuthBase64Url((const unsigned char*)claims, strlen(claims), body + len);
    ToriRSServer_AuthHmacSha256(g_oauth_key, sizeof(g_oauth_key), (const uint8_t*)body,
                                strlen(body), mac);
    ToriRSServer_OAuthBase64Url(mac, sizeof(mac), mac_text);
    len = snprintf(out, TORIRSSERVER_OAUTH_TOKEN_MAX, "%s.%s", body, mac_text);
    assert(len > 0 && len < TORIRSSERVER_OAUTH_TOKEN_MAX);
}

int
ToriRSServer_OAuthVerify(
    const char* token,
    long long now,
    struct ToriRSServerOAuthClaims* out)
{
    const char* first;
    const char* second;
    char want_text[48];
    char issuer[32];
    char audience[32];
    uint8_t mac[32];
    unsigned char claims[TORIRSSERVER_OAUTH_TOKEN_MAX];
    int claims_len;
    size_t sig_len;
    uint8_t diff = 0;

    assert(token);
    assert(out);
    assert(g_oauth_key_loaded);
    memset(out, 0, sizeof(*out));
    first = strchr(token, '.');
    if( !first || (size_t)(first - token) != strlen(k_jwt_header) ||
        memcmp(token, k_jwt_header, strlen(k_jwt_header)) != 0 )
        return 0;
    second = strchr(first + 1, '.');
    if( !second || strchr(second + 1, '.') )
        return 0;

    ToriRSServer_AuthHmacSha256(g_oauth_key, sizeof(g_oauth_key), (const uint8_t*)token,
                                (size_t)(second - token), mac);
    ToriRSServer_OAuthBase64Url(mac, sizeof(mac), want_text);
    sig_len = strlen(second + 1);
    if( sig_len != strlen(want_text) )
        return 0;
    for( size_t i = 0; i < sig_len; i++ )
        diff |= (uint8_t)(want_text[i] ^ second[1 + i]);
    if( diff != 0 )
        return 0;

    claims_len = b64url_decode(first + 1, (size_t)(second - first - 1), claims,
                               sizeof(claims) - 1);
    if( claims_len < 0 )
        return 0;
    claims[claims_len] = '\0';
    if( !FlatJson_String((const char*)claims, (size_t)claims_len, "iss", issuer, sizeof(issuer)) ||
        strcmp(issuer, TORIRSSERVER_OAUTH_ISSUER) != 0 ||
        !FlatJson_String((const char*)claims, (size_t)claims_len, "aud", audience,
                         sizeof(audience)) ||
        strcmp(audience, TORIRSSERVER_OAUTH_AUDIENCE) != 0 ||
        !FlatJson_String((const char*)claims, (size_t)claims_len, "sub", out->sub,
                         sizeof(out->sub)) ||
        !FlatJson_String((const char*)claims, (size_t)claims_len, "typ", out->typ,
                         sizeof(out->typ)) ||
        !FlatJson_String((const char*)claims, (size_t)claims_len, "jti", out->jti,
                         sizeof(out->jti)) ||
        !FlatJson_Int64((const char*)claims, (size_t)claims_len, "exp", &out->exp) ||
        !FlatJson_Int64((const char*)claims, (size_t)claims_len, "iat", &out->iat) )
        return 0;
    FlatJson_String((const char*)claims, (size_t)claims_len, "name", out->name, sizeof(out->name));
    FlatJson_String((const char*)claims, (size_t)claims_len, "scope", out->scope,
                    sizeof(out->scope));
    FlatJson_String((const char*)claims, (size_t)claims_len, "fam", out->fam, sizeof(out->fam));
    return out->exp > now;
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

struct OAuthRecord
{
    char sub[64];
    char name[64];
    char fam[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char redirect[256];
    char challenge[64];
    char scope[64];
    /** An OTL minted through a web login's session (OAuthUserPlay): that
     *  login, as keyed. "" for every other record. */
    char user[64];
    long long exp;
};

/* An id that arrived from outside, checked before it becomes a file name. */
static int
id_is_valid(const char* id)
{
    if( strlen(id) != TORIRSSERVER_OAUTH_ID_HEX )
        return 0;
    for( const char* scan = id; *scan; scan++ )
        if( !isdigit((unsigned char)*scan) && (*scan < 'a' || *scan > 'f') )
            return 0;
    return 1;
}

static void
record_path(
    const char* kind,
    const char* id,
    char* out,
    size_t cap)
{
    assert(id_is_valid(id));
    snprintf(out, cap, "%s/oauth/%s/%s.ini", ToriRSServer_SaveDir(), kind, id);
}

static void
trim(char* text)
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

static int
record_write(
    const char* path,
    const char* section,
    const struct OAuthRecord* record)
{
    char text[1024];

    snprintf(text, sizeof(text),
             "; Written by torirs_server_oauth.c. One use: deleted when spent.\n"
             "\n[%s]\nsub = %s\nname = %s\nfam = %s\nredirect = %s\nchallenge = %s\n"
             "scope = %s\nuser = %s\nexp = %lld\n",
             section, record->sub, record->name, record->fam, record->redirect,
             record->challenge, record->scope, record->user, record->exp);
    return ToriRSServer_SaveWriteText(path, text);
}

/* 1 = read, 0 = no such record, -1 = one that cannot be read. */
static int
record_read(
    const char* path,
    const char* section,
    struct OAuthRecord* out)
{
    uint8_t data[2048];
    size_t size;
    FILE* file;
    struct INIReader reader;
    struct INIElement element;
    int in_section = 0;

    memset(out, 0, sizeof(*out));
    file = fopen(path, "rb");
    if( !file )
        return errno == ENOENT ? 0 : -1;
    size = fread(data, 1, sizeof(data), file);
    fclose(file);
    if( size == sizeof(data) )
        return -1;
    ini_reader_init(&reader);
    for( ;; )
    {
        int status = ini_reader_next(&reader, data, (uint32_t)size, &element);

        if( status != TORI_INI_ERR_OK )
        {
            if( status != TORI_INI_ERR_NONE )
                return -1;
            break;
        }
        if( element.kind == INI_ELEMENT_SECTION )
        {
            trim(element._section.name);
            in_section = strcmp(element._section.name, section) == 0;
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL || !in_section )
            continue;
        trim(element._keyval.name);
        trim(element._keyval.value);
        if( strcmp(element._keyval.name, "sub") == 0 )
            snprintf(out->sub, sizeof(out->sub), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "name") == 0 )
            snprintf(out->name, sizeof(out->name), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "fam") == 0 )
            snprintf(out->fam, sizeof(out->fam), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "redirect") == 0 )
            snprintf(out->redirect, sizeof(out->redirect), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "challenge") == 0 )
            snprintf(out->challenge, sizeof(out->challenge), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "scope") == 0 )
            snprintf(out->scope, sizeof(out->scope), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "user") == 0 )
            snprintf(out->user, sizeof(out->user), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "exp") == 0 )
            out->exp = strtoll(element._keyval.value, NULL, 10);
    }
    return out->sub[0] ? 1 : -1;
}

/*
 * Read and delete a one-use record: 1 when it was there, whatever it says.
 * Deleted BEFORE the caller judges it, so a code or OTL that fails a check is
 * still spent -- a second try with a corrected verifier or name must not find
 * it waiting.
 */
static int
record_take(
    const char* kind,
    const char* id,
    struct OAuthRecord* out)
{
    char path[1024];

    if( !id_is_valid(id) )
        return 0;
    record_path(kind, id, path, sizeof(path));
    if( record_read(path, kind, out) <= 0 )
        return 0;
    if( remove(path) != 0 )
    {
        /* A record that cannot be deleted can be spent again. */
        fprintf(stderr, "torirsserver: CANNOT DELETE spent %s %s: %s\n", kind, path,
                strerror(errno));
        return 0;
    }
    return 1;
}

/*
 * Delete every refresh record of family `fam`, or every one of account `sub`
 * -- a reuse, a revocation, a link -- and, while the directory is open, every
 * expired one. Either selector may be NULL.
 */
static void
revoke_where(
    const char* fam,
    const char* sub,
    long long now)
{
    char dir[1024];
    DIR* handle;
    struct dirent* entry;

    snprintf(dir, sizeof(dir), "%s/oauth/refresh", ToriRSServer_SaveDir());
    handle = opendir(dir);
    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char path[1300];
        struct OAuthRecord record;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( record_read(path, "refresh", &record) <= 0 )
            continue;
        if( (fam && strcmp(record.fam, fam) == 0) || (sub && strcmp(record.sub, sub) == 0) ||
            record.exp <= now )
            remove(path);
    }
    closedir(handle);
}

static void
revoke_family(
    const char* fam,
    long long now)
{
    revoke_where(fam, NULL, now);
}

/* 1 when the account `name` is linked to a web login, or its record cannot
 * be read: either way no player-held token may sign it in. */
static int
account_is_managed(const char* name)
{
    char owner[64];

    return ToriRSServer_AuthManagedBy(name, owner, sizeof(owner)) != 0;
}

/* ------------------------------------------------------------------ */
/* Grants                                                              */
/* ------------------------------------------------------------------ */

/* A requested scope: absent means the default; anything else must be the one
 * scope this server grants. */
static int
scope_ok(
    const char* requested,
    char out[64])
{
    if( !requested || !requested[0] || strcmp(requested, TORIRSSERVER_OAUTH_SCOPE) == 0 )
    {
        snprintf(out, 64, "%s", TORIRSSERVER_OAUTH_SCOPE);
        return 1;
    }
    return 0;
}

/* Mint an access token and a refresh token (recording the refresh token) for
 * account `sub` / `name`, continuing family `fam` or starting one. */
static enum ToriRSServerOAuthError
issue(
    const char* sub,
    const char* name,
    const char* scope,
    const char* fam,
    long long now,
    struct ToriRSServerOAuthTokens* out)
{
    char claims[768];
    char name_json[160];
    char access_jti[TORIRSSERVER_OAUTH_ID_HEX + 1];
    struct OAuthRecord record;
    char path[1024];

    memset(out, 0, sizeof(*out));
    memset(&record, 0, sizeof(record));
    json_escape(name, name_json, sizeof(name_json));
    snprintf(record.sub, sizeof(record.sub), "%s", sub);
    snprintf(record.name, sizeof(record.name), "%s", name);
    snprintf(record.scope, sizeof(record.scope), "%s", scope);
    if( fam && fam[0] )
        snprintf(record.fam, sizeof(record.fam), "%s", fam);
    else
        new_id(record.fam);

    new_id(access_jti);
    snprintf(claims, sizeof(claims),
             "{\"iss\":\"%s\",\"aud\":\"%s\",\"sub\":\"%s\",\"name\":\"%s\",\"scope\":\"%s\","
             "\"typ\":\"access\",\"iat\":%lld,\"exp\":%lld,\"jti\":\"%s\"}",
             TORIRSSERVER_OAUTH_ISSUER, TORIRSSERVER_OAUTH_AUDIENCE, sub, name_json, scope, now,
             now + TORIRSSERVER_OAUTH_ACCESS_SECONDS, access_jti);
    jwt_sign(claims, out->access_token);

    {
        char refresh_jti[TORIRSSERVER_OAUTH_ID_HEX + 1];

        new_id(refresh_jti);
        record.exp = now + TORIRSSERVER_OAUTH_REFRESH_SECONDS;
        snprintf(claims, sizeof(claims),
                 "{\"iss\":\"%s\",\"aud\":\"%s\",\"sub\":\"%s\",\"name\":\"%s\",\"scope\":\"%s\","
                 "\"typ\":\"refresh\",\"iat\":%lld,\"exp\":%lld,\"jti\":\"%s\",\"fam\":\"%s\"}",
                 TORIRSSERVER_OAUTH_ISSUER, TORIRSSERVER_OAUTH_AUDIENCE, sub, name_json, scope,
                 now, record.exp, refresh_jti, record.fam);
        jwt_sign(claims, out->refresh_token);
        record_path("refresh", refresh_jti, path, sizeof(path));
        if( !record_write(path, "refresh", &record) )
            return TORIRSSERVER_OAUTH_SERVER_ERROR;
    }
    snprintf(out->name, sizeof(out->name), "%s", name);
    snprintf(out->scope, sizeof(out->scope), "%s", scope);
    out->expires_in = TORIRSSERVER_OAUTH_ACCESS_SECONDS;
    return TORIRSSERVER_OAUTH_OK;
}

/* The player account check the password grant and the authorize page share. */
static enum ToriRSServerOAuthError
check_account(
    const char* username,
    const char* password,
    char sub[64],
    const char** out_description)
{
    enum ToriRSServerAuthResult auth;

    if( !username[0] || !password[0] )
    {
        *out_description = "username and password are required";
        return TORIRSSERVER_OAUTH_INVALID_REQUEST;
    }
    /* The name is written into token records and claims; a control character
     * in it would end a line of either. No login screen can type one. */
    for( const char* scan = username; *scan; scan++ )
        if( (unsigned char)*scan < ' ' || strlen(username) >= 64 )
        {
            *out_description = "Invalid username or password.";
            return TORIRSSERVER_OAUTH_INVALID_GRANT;
        }
    auth = ToriRSServer_AuthLogin(username, password);
    if( auth == TORIRSSERVER_AUTH_WEAK )
    {
        *out_description = ToriRSServer_AuthPasswordProblem(username, password);
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    if( auth == TORIRSSERVER_AUTH_MANAGED )
    {
        *out_description = TORIRSSERVER_OAUTH_MANAGED_DESCRIPTION;
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    if( auth != TORIRSSERVER_AUTH_OK && auth != TORIRSSERVER_AUTH_CLAIMED )
    {
        *out_description = "Invalid username or password.";
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    ToriRSServer_SaveFileName(username, sub, 64);
    return TORIRSSERVER_OAUTH_OK;
}

enum ToriRSServerOAuthError
ToriRSServer_OAuthPasswordGrant(
    const char* username,
    const char* password,
    const char* scope,
    long long now,
    struct ToriRSServerOAuthTokens* out,
    const char** out_description)
{
    char sub[64];
    char granted[64];
    enum ToriRSServerOAuthError error;

    assert(username);
    assert(password);
    assert(out);
    assert(out_description);
    *out_description = NULL;
    if( !scope_ok(scope, granted) )
        return TORIRSSERVER_OAUTH_INVALID_SCOPE;
    error = check_account(username, password, sub, out_description);
    if( error != TORIRSSERVER_OAUTH_OK )
        return error;
    return issue(sub, username, granted, NULL, now, out);
}

enum ToriRSServerOAuthError
ToriRSServer_OAuthRefreshGrant(
    const char* refresh_token,
    const char* scope,
    long long now,
    struct ToriRSServerOAuthTokens* out,
    const char** out_description)
{
    struct ToriRSServerOAuthClaims claims;
    struct OAuthRecord record;
    char granted[64];

    assert(refresh_token);
    assert(out);
    assert(out_description);
    *out_description = NULL;
    if( !ToriRSServer_OAuthVerify(refresh_token, now, &claims) ||
        strcmp(claims.typ, "refresh") != 0 || !id_is_valid(claims.jti) ||
        !id_is_valid(claims.fam) )
    {
        *out_description = "The refresh token is invalid or expired.";
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    /* Narrowing is allowed, widening is not; there is one scope, so the only
     * question is whether it was asked for something else. */
    if( scope && scope[0] && strcmp(scope, claims.scope) != 0 )
        return TORIRSSERVER_OAUTH_INVALID_SCOPE;
    if( !record_take("refresh", claims.jti, &record) )
    {
        /* Signed, unexpired, and not on file: it was already spent. Whoever
         * holds this family's tokens, one of them is not the player -- so
         * none of them is trusted any more. */
        fprintf(stderr, "torirsserver: refresh token reuse for '%s'; revoking its family\n",
                claims.sub);
        revoke_family(claims.fam, now);
        *out_description = "The refresh token was already used; sign in again.";
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    if( strcmp(record.sub, claims.sub) != 0 || strcmp(record.fam, claims.fam) != 0 )
    {
        revoke_family(claims.fam, now);
        *out_description = "The refresh token does not match its record.";
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    /* Linking revoked this account's families; one minted in the moment
     * between the check and the link still ends here. */
    if( account_is_managed(claims.sub) )
    {
        revoke_family(claims.fam, now);
        *out_description = TORIRSSERVER_OAUTH_MANAGED_DESCRIPTION;
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    snprintf(granted, sizeof(granted), "%s", claims.scope);
    return issue(claims.sub, claims.name[0] ? claims.name : claims.sub, granted, claims.fam, now,
                 out);
}

/* PKCE S256: BASE64URL(SHA256(verifier)) == challenge, with the verifier's
 * own shape checked first (RFC 7636 4.1: 43-128 unreserved characters). */
static int
pkce_matches(
    const char* verifier,
    const char* challenge)
{
    size_t len = strlen(verifier);
    uint8_t digest[SHA256_DIGEST_BYTES];
    char want[48];
    uint8_t diff = 0;

    if( len < 43 || len > 128 )
        return 0;
    for( size_t i = 0; i < len; i++ )
    {
        char c = verifier[i];

        if( !isalnum((unsigned char)c) && c != '-' && c != '.' && c != '_' && c != '~' )
            return 0;
    }
    sha256((const uint8_t*)verifier, len, digest);
    ToriRSServer_OAuthBase64Url(digest, sizeof(digest), want);
    if( strlen(challenge) != strlen(want) )
        return 0;
    for( size_t i = 0; i < strlen(want); i++ )
        diff |= (uint8_t)(want[i] ^ challenge[i]);
    return diff == 0;
}

enum ToriRSServerOAuthError
ToriRSServer_OAuthCodeGrant(
    const char* code,
    const char* redirect_uri,
    const char* code_verifier,
    long long now,
    struct ToriRSServerOAuthTokens* out,
    const char** out_description)
{
    struct OAuthRecord record;

    assert(code);
    assert(redirect_uri);
    assert(code_verifier);
    assert(out);
    assert(out_description);
    *out_description = NULL;
    if( !record_take("codes", code, &record) || record.exp <= now )
    {
        *out_description = "The authorization code is invalid, used or expired.";
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    if( strcmp(record.redirect, redirect_uri) != 0 )
    {
        *out_description = "redirect_uri does not match the authorization request.";
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    if( !pkce_matches(code_verifier, record.challenge) )
    {
        *out_description = "code_verifier does not match the code_challenge.";
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    }
    return issue(record.sub, record.name, record.scope, NULL, now, out);
}

int
ToriRSServer_OAuthRedirectAllowed(const char* redirect_uri)
{
    size_t len;
    const char* rest = NULL;

    assert(redirect_uri);
    len = strlen(redirect_uri);
    if( len == 0 || len >= 200 )
        return 0;
    for( size_t i = 0; i < len; i++ )
        if( (unsigned char)redirect_uri[i] <= ' ' || redirect_uri[i] == '#' ||
            redirect_uri[i] == '\\' || (unsigned char)redirect_uri[i] >= 0x7f )
            return 0;
    /* This origin: a path, never "//host" (which a browser reads as another
     * origin). */
    if( redirect_uri[0] == '/' )
        return redirect_uri[1] != '/';
    /* A native app's loopback listener, any port (RFC 8252 7.3). */
    if( strncmp(redirect_uri, "http://127.0.0.1:", 17) == 0 )
        rest = redirect_uri + 17;
    else if( strncmp(redirect_uri, "http://localhost:", 17) == 0 )
        rest = redirect_uri + 17;
    if( !rest || !isdigit((unsigned char)*rest) )
        return 0;
    while( isdigit((unsigned char)*rest) )
        rest++;
    return *rest == '/' || *rest == '\0';
}

enum ToriRSServerOAuthError
ToriRSServer_OAuthAuthorize(
    const char* username,
    const char* password,
    const char* redirect_uri,
    const char* code_challenge,
    const char* scope,
    long long now,
    char out_code[TORIRSSERVER_OAUTH_ID_HEX + 1],
    const char** out_description)
{
    struct OAuthRecord record;
    char path[1024];
    enum ToriRSServerOAuthError error;

    assert(username);
    assert(password);
    assert(redirect_uri);
    assert(code_challenge);
    assert(out_code);
    assert(out_description);
    *out_description = NULL;
    memset(&record, 0, sizeof(record));
    /* The caller showed the page only for an allowed redirect and an S256
     * challenge; both are checked again here, where the code is made. */
    if( !ToriRSServer_OAuthRedirectAllowed(redirect_uri) || strlen(code_challenge) != 43 )
    {
        *out_description = "redirect_uri or code_challenge is not acceptable.";
        return TORIRSSERVER_OAUTH_INVALID_REQUEST;
    }
    if( !scope_ok(scope, record.scope) )
        return TORIRSSERVER_OAUTH_INVALID_SCOPE;
    error = check_account(username, password, record.sub, out_description);
    if( error != TORIRSSERVER_OAUTH_OK )
        return error;
    snprintf(record.name, sizeof(record.name), "%s", username);
    snprintf(record.redirect, sizeof(record.redirect), "%s", redirect_uri);
    snprintf(record.challenge, sizeof(record.challenge), "%s", code_challenge);
    record.exp = now + TORIRSSERVER_OAUTH_CODE_SECONDS;
    new_id(out_code);
    record_path("codes", out_code, path, sizeof(path));
    if( !record_write(path, "codes", &record) )
        return TORIRSSERVER_OAUTH_SERVER_ERROR;
    return TORIRSSERVER_OAUTH_OK;
}

void
ToriRSServer_OAuthRevoke(
    const char* token,
    long long now)
{
    struct ToriRSServerOAuthClaims claims;

    assert(token);
    if( ToriRSServer_OAuthVerify(token, now, &claims) && strcmp(claims.typ, "refresh") == 0 &&
        id_is_valid(claims.fam) )
        revoke_family(claims.fam, now);
}

enum ToriRSServerOAuthError
ToriRSServer_OAuthPlay(
    const char* access_token,
    long long now,
    char out_otl[TORIRSSERVER_OAUTH_ID_HEX + 1],
    char* out_name,
    size_t cap)
{
    struct ToriRSServerOAuthClaims claims;
    struct OAuthRecord record;
    char path[1024];

    assert(access_token);
    assert(out_otl);
    assert(out_name);
    if( !ToriRSServer_OAuthVerify(access_token, now, &claims) ||
        strcmp(claims.typ, "access") != 0 || strcmp(claims.scope, TORIRSSERVER_OAUTH_SCOPE) != 0 )
        return TORIRSSERVER_OAUTH_INVALID_TOKEN;
    /* An access token outlives a link by up to ten minutes; it buys nothing. */
    if( account_is_managed(claims.sub) )
        return TORIRSSERVER_OAUTH_INVALID_TOKEN;
    memset(&record, 0, sizeof(record));
    snprintf(record.sub, sizeof(record.sub), "%s", claims.sub);
    snprintf(record.name, sizeof(record.name), "%s", claims.name);
    record.exp = now + TORIRSSERVER_OAUTH_OTL_SECONDS;
    new_id(out_otl);
    record_path("otl", out_otl, path, sizeof(path));
    if( !record_write(path, "otl", &record) )
        return TORIRSSERVER_OAUTH_SERVER_ERROR;
    snprintf(out_name, cap, "%s", claims.name[0] ? claims.name : claims.sub);
    return TORIRSSERVER_OAUTH_OK;
}

enum ToriRSServerOAuthError
ToriRSServer_OAuthUserPlay(
    const char* user,
    const char* display_name,
    const char* link_token,
    long long now,
    char out_otl[TORIRSSERVER_OAUTH_ID_HEX + 1],
    char* out_name,
    size_t cap)
{
    struct OAuthRecord record;
    char owner[64];
    char sub[64];
    char path[1024];

    assert(user);
    assert(display_name);
    assert(link_token);
    assert(out_otl);
    assert(out_name);
    /* The name goes into the record and the answer; a control character in it
     * would end a line of either. */
    for( const char* scan = display_name; *scan; scan++ )
        if( (unsigned char)*scan < ' ' )
            return TORIRSSERVER_OAUTH_INVALID_REQUEST;
    ToriRSServer_SaveFileName(display_name, sub, sizeof(sub));
    if( !sub[0] || strlen(display_name) >= sizeof(record.name) )
        return TORIRSSERVER_OAUTH_INVALID_REQUEST;
    /* The owner AND the live link's token: a row left behind by an unlink, or
     * by a re-link that minted a newer token, buys nothing. */
    if( ToriRSServer_AuthManagedBy(display_name, owner, sizeof(owner)) != 1 ||
        strcmp(owner, user) != 0 || !ToriRSServer_AuthLinkVerify(display_name, user, link_token) )
        return TORIRSSERVER_OAUTH_INVALID_GRANT;
    memset(&record, 0, sizeof(record));
    snprintf(record.sub, sizeof(record.sub), "%s", sub);
    snprintf(record.name, sizeof(record.name), "%s", display_name);
    snprintf(record.user, sizeof(record.user), "%s", owner);
    record.exp = now + TORIRSSERVER_OAUTH_OTL_SECONDS;
    new_id(out_otl);
    record_path("otl", out_otl, path, sizeof(path));
    if( !record_write(path, "otl", &record) )
        return TORIRSSERVER_OAUTH_SERVER_ERROR;
    snprintf(out_name, cap, "%s", display_name);
    return TORIRSSERVER_OAUTH_OK;
}

void
ToriRSServer_OAuthRevokeAccount(
    const char* display_name,
    long long now)
{
    char sub[64];

    assert(display_name);
    ToriRSServer_SaveFileName(display_name, sub, sizeof(sub));
    if( sub[0] )
        revoke_where(NULL, sub, now);
}

int
ToriRSServer_OAuthRedeemOtl(
    const char* otl,
    const char* display_name,
    long long now)
{
    struct OAuthRecord record;
    char name[64];
    char owner[64];
    int managed;

    assert(otl);
    assert(display_name);
    if( !record_take("otl", otl, &record) || record.exp <= now )
        return 0;
    ToriRSServer_SaveFileName(display_name, name, sizeof(name));
    if( !name[0] || strcmp(name, record.sub) != 0 )
        return 0;
    /* A linked account takes only a token its owner's session minted: one the
     * player's own access token bought in the minute before the link is
     * refused, and so is one from a login it has since been moved away from. */
    managed = ToriRSServer_AuthManagedBy(display_name, owner, sizeof(owner));
    if( managed < 0 )
        return 0;
    if( managed > 0 )
        return strcmp(record.user, owner) == 0;
    return 1;
}
