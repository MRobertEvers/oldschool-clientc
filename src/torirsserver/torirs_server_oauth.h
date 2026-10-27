#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_OAUTH_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_OAUTH_H

/*
 * OAuth 2.0 for player accounts, and the one-time login token the game takes.
 *
 * The shape is the rev-239 deob's own (client.java method1579 / method2096 and
 * the login state machine that follows them): the client holds a REFRESH
 * token, trades it at the token endpoint (grant_type=refresh_token, scope
 * gamesso.token.create) for an ACCESS token, presents that as a Bearer to a
 * "play" endpoint, and gets back a short one-time-login token (OTL) -- which is
 * what the login block carries, in the RSA section where a password would go,
 * with auth type TOKEN (class660.field7035). The access token never reaches the
 * game server: it does not fit in a 1024-bit RSA block, and the deob does not
 * try.
 *
 * Here:
 *
 *   io_server (`--gate`, src/ioserver/io_gate.c), behind the gate's cookie:
 *     POST /oauth/token      grant_type = authorization_code (PKCE S256),
 *                            password, refresh_token          RFC 6749 / 7636
 *     GET|POST /oauth/authorize   a player-account login page, then a
 *                            redirect with ?code=&state=
 *     POST /oauth/revoke     RFC 7009
 *     POST /game/play        Bearer access token -> {"token": OTL}
 *     GET  /.well-known/oauth-authorization-server              RFC 8414
 *   torirsserver: a login with auth type TOKEN redeems the OTL
 *     (ToriRSServer_OAuthRedeemOtl).
 *
 * Tokens are JWTs (RFC 7519) signed HS256 with `<saves>/oauth/signing.key`,
 * which both processes load. Access tokens are stateless and short; refresh
 * tokens are also JWTs but each one is ALSO a record,
 * `<saves>/oauth/refresh/<jti>.ini`, so that using one consumes it (rotation)
 * and presenting a consumed one revokes its whole family -- the reuse detection
 * RFC 6819 5.2.2.3 and the OAuth 2.1 draft ask for. Authorization codes and
 * OTLs are one-use records under `<saves>/oauth/codes/` and `otl/`.
 *
 * Everything here is a PLAYER account (torirs_server_auth.h), not the gate's
 * web login: the gate only decides who may reach these endpoints at all.
 */

#include <stddef.h>

#define TORIRSSERVER_OAUTH_ISSUER "torirs"
#define TORIRSSERVER_OAUTH_AUDIENCE "torirs"
/* The one client, public (no secret), as the deob's launcher client is. */
#define TORIRSSERVER_OAUTH_CLIENT_ID "torirs-client"
/* The deob's own scope name for "may mint a game login". */
#define TORIRSSERVER_OAUTH_SCOPE "gamesso.token.create"

#define TORIRSSERVER_OAUTH_ACCESS_SECONDS 600
#define TORIRSSERVER_OAUTH_REFRESH_SECONDS (30LL * 24 * 60 * 60)
#define TORIRSSERVER_OAUTH_CODE_SECONDS 60
#define TORIRSSERVER_OAUTH_OTL_SECONDS 60

#define TORIRSSERVER_OAUTH_TOKEN_MAX 1024
/* An id, code or OTL: 16 random bytes as hex. */
#define TORIRSSERVER_OAUTH_ID_HEX 32

enum ToriRSServerOAuthError
{
    TORIRSSERVER_OAUTH_OK = 0,
    TORIRSSERVER_OAUTH_INVALID_REQUEST,
    TORIRSSERVER_OAUTH_INVALID_CLIENT,
    TORIRSSERVER_OAUTH_INVALID_GRANT,
    TORIRSSERVER_OAUTH_UNSUPPORTED_GRANT_TYPE,
    TORIRSSERVER_OAUTH_INVALID_SCOPE,
    /** RFC 6750: the Bearer token is missing, bad or expired. */
    TORIRSSERVER_OAUTH_INVALID_TOKEN,
    TORIRSSERVER_OAUTH_SERVER_ERROR,
};

/** The RFC's own spelling: "invalid_grant", ... */
const char*
ToriRSServer_OAuthErrorName(enum ToriRSServerOAuthError error);

/** A token endpoint's success. */
struct ToriRSServerOAuthTokens
{
    char access_token[TORIRSSERVER_OAUTH_TOKEN_MAX];
    char refresh_token[TORIRSSERVER_OAUTH_TOKEN_MAX];
    /** The display name the account was signed in as. */
    char name[64];
    char scope[64];
    int expires_in;
};

/** Load (or create) `<saves>/oauth/signing.key`. Aborts when it cannot. */
void
ToriRSServer_OAuthKeyLoad(void);

/**
 * grant_type=password: `username` / `password` against the player account,
 * exactly as a game login checks it -- including a first login claiming the
 * name under the password rules (ToriRSServer_AuthLogin). `scope` may be NULL
 * or "" for the default. `*out_description` is a sentence for the error, or
 * NULL.
 */
enum ToriRSServerOAuthError
ToriRSServer_OAuthPasswordGrant(
    const char* username,
    const char* password,
    const char* scope,
    long long now,
    struct ToriRSServerOAuthTokens* out,
    const char** out_description);

/** grant_type=refresh_token, rotating: the token presented is spent. */
enum ToriRSServerOAuthError
ToriRSServer_OAuthRefreshGrant(
    const char* refresh_token,
    const char* scope,
    long long now,
    struct ToriRSServerOAuthTokens* out,
    const char** out_description);

/** grant_type=authorization_code, with the PKCE verifier (RFC 7636). */
enum ToriRSServerOAuthError
ToriRSServer_OAuthCodeGrant(
    const char* code,
    const char* redirect_uri,
    const char* code_verifier,
    long long now,
    struct ToriRSServerOAuthTokens* out,
    const char** out_description);

/**
 * The authorization endpoint's second half: the player signed in on the
 * authorize page. Checks the account like the password grant, then records a
 * one-use code bound to `redirect_uri` and the S256 `code_challenge`.
 */
enum ToriRSServerOAuthError
ToriRSServer_OAuthAuthorize(
    const char* username,
    const char* password,
    const char* redirect_uri,
    const char* code_challenge,
    const char* scope,
    long long now,
    char out_code[TORIRSSERVER_OAUTH_ID_HEX + 1],
    const char** out_description);

/** 1 when `redirect_uri` may receive a code: a path on this origin
 *  ("/..."), or a loopback http URI for a native app (RFC 8252 7.3). */
int
ToriRSServer_OAuthRedirectAllowed(const char* redirect_uri);

/** RFC 7009: a refresh token revokes its whole family; anything else (an
 *  access token, which is stateless, or garbage) is answered the same. */
void
ToriRSServer_OAuthRevoke(
    const char* token,
    long long now);

/**
 * The deob's "play" call: a Bearer access token holding the game scope mints
 * a one-time login token for its account, good for
 * TORIRSSERVER_OAUTH_OTL_SECONDS. `out_name` gets the account's display name.
 */
enum ToriRSServerOAuthError
ToriRSServer_OAuthPlay(
    const char* access_token,
    long long now,
    char out_otl[TORIRSSERVER_OAUTH_ID_HEX + 1],
    char* out_name,
    size_t cap);

/** torirsserver's side: 1 when `otl` is an unexpired one-time login token for
 *  `display_name` (as the account is keyed). It is spent either way. */
int
ToriRSServer_OAuthRedeemOtl(
    const char* otl,
    const char* display_name,
    long long now);

/** The claims of a token this key signed, for the endpoints and the tests. */
struct ToriRSServerOAuthClaims
{
    char sub[64];
    char name[64];
    char scope[64];
    char typ[16];
    char jti[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char fam[TORIRSSERVER_OAUTH_ID_HEX + 1];
    long long iat;
    long long exp;
};

/** 1 when `token` is a JWT this key signed (HS256, our issuer and audience),
 *  unexpired at `now`; `out` gets its claims. */
int
ToriRSServer_OAuthVerify(
    const char* token,
    long long now,
    struct ToriRSServerOAuthClaims* out);

/** base64url without padding (RFC 4648 5), for PKCE and the tests.
 *  `out` holds 4 * ceil(len / 3) + 1 bytes. */
void
ToriRSServer_OAuthBase64Url(
    const unsigned char* bytes,
    size_t len,
    char* out);

#endif
