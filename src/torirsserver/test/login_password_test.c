/*
 * Passwords: a name's first login sets its password, a wrong one is refused
 * with response 3, and development mode lets anything in.
 *
 * The hash is checked against published PBKDF2-HMAC-SHA256 vectors first,
 * because a test that only round-trips its own output would pass a hash that
 * is merely deterministic. The logins are driven through the real client
 * login machine and the real embedded server, as reconnect_claim_test.c
 * drives them: the refusal is a byte the CLIENT has to read as "invalid
 * username or password", and the takeover a wrong password must not cause is
 * the host's, not the check's.
 *
 * Run: make -C src test-torirsserver-login-password
 */

#include "torirsserver/torirs_server.h"
#include "accountserver/account_db.h"
#include "accountserver/account_proto.h"
#include "accountserver/account_server.h"
#include "torirsserver/torirs_server_accountlink.h"
#include "torirsserver/torirs_server_accountlog.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server_oauth.h"
#include "torirsserver/torirs_server_save.h"
#include "torirsserver/torirs_server_savefile.h"
#include "torirsserver/torirs_server_session.h"
#include "torirsserver/torirs_server_wire.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/flat_json.h"
#include "net/net_out.h"
#include "net/sha256.h"
#include "net/rev/gameproto_revisions.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    printf("login-password: %-62s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

static int
hex_is(
    const uint8_t* bytes,
    size_t len,
    const char* want)
{
    char got[256];

    for( size_t i = 0; i < len; i++ )
        snprintf(got + i * 2, 3, "%02x", bytes[i]);
    return strcmp(got, want) == 0;
}

static void
check_vectors(void)
{
    uint8_t out[64];
    const uint8_t salt[] = { 's', 'a', 'l', 't' };
    uint8_t fixed_salt[TORIRSSERVER_AUTH_SALT_BYTES];
    char record[TORIRSSERVER_AUTH_RECORD_MAX];

    ToriRSServer_AuthPbkdf2("password", salt, sizeof(salt), 1, out, 32);
    check(hex_is(out, 32, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"),
          "PBKDF2-SHA256 password/salt c=1");
    ToriRSServer_AuthPbkdf2("password", salt, sizeof(salt), 2, out, 32);
    check(hex_is(out, 32, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"),
          "PBKDF2-SHA256 password/salt c=2");
    ToriRSServer_AuthPbkdf2("password", salt, sizeof(salt), 4096, out, 32);
    check(hex_is(out, 32, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"),
          "PBKDF2-SHA256 password/salt c=4096");
    /* RFC 7914 section 11: two output blocks. */
    ToriRSServer_AuthPbkdf2("passwd", salt, sizeof(salt), 1, out, 64);
    check(hex_is(out, 64,
                 "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
                 "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"),
          "PBKDF2-SHA256 RFC 7914 passwd/salt c=1 dkLen=64");

    memset(fixed_salt, 0xa5, sizeof(fixed_salt));
    ToriRSServer_AuthRecord("hunter2", fixed_salt, 1000, record);
    check(strncmp(record, "pbkdf2-sha256$1000$a5a5", 23) == 0, "a record names its scheme and count");
    check(ToriRSServer_AuthVerify("hunter2", record), "a record verifies its own password");
    check(!ToriRSServer_AuthVerify("hunter3", record), "and not another");
    check(!ToriRSServer_AuthVerify("", record), "nor an empty one");
    check(!ToriRSServer_AuthVerify("hunter2", "hunter2"), "a plaintext record is not a match");
    check(!ToriRSServer_AuthVerify("hunter2", "pbkdf2-sha256$0$a5$00"), "a zero count is refused");
    check(!ToriRSServer_AuthVerify("hunter2", "pbkdf2-sha256$2000000000$a5a5$" /* key cut */),
          "a truncated record is refused");
}

struct Peer
{
    struct ToriRS_Network net;
    int client_id;
    /** The server closed this client's session. */
    int dropped;
};

static void
peer_begin(
    struct Peer* peer,
    int client_id)
{
    memset(peer, 0, sizeof(*peer));
    peer->client_id = client_id;
    ToriRS_Network_Init(&peer->net, GameProtoRev_OSRS239(), TORIRSSERVER_RSA_PUBLIC_EXPONENT,
                        TORIRSSERVER_RSA_PUBLIC_MODULUS);
}

static void
peer_dial(
    struct Peer* peer,
    const char* name,
    const char* password)
{
    uint8_t status = TORIRS_NET_STATUS_CONNECTED;

    ToriRS_Network_ConnectLogin(&peer->net, "embedded:0", name, password);
    ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, &status, 1);
}

/* As peer_dial, with a one-time login token where the password goes. */
static void
peer_dial_token(
    struct Peer* peer,
    const char* name,
    const char* token)
{
    uint8_t status = TORIRS_NET_STATUS_CONNECTED;

    ToriRS_Network_ConnectLoginToken(&peer->net, "embedded:0", name, token);
    ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, &status, 1);
}

/* One round of each side's output into the other's input, as
 * reconnect_claim_test.c pumps it. */
static void
pump(
    struct Peer** peers,
    int peer_count,
    struct ToriRSServerEmbed* embed,
    int run_tick)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[65536];
    static uint8_t inbound[65536];
    int got;

    for( int p = 0; p < peer_count; p++ )
    {
        if( peers[p]->dropped )
            continue;
        while( ToriRS_Network_PopOut(&peers[p]->net, &header, payload) )
        {
            if( header.type != TORIRS_NET_OUT_SEND_DATA )
                continue;
            ToriRSServer_EmbedWrite(embed, peers[p]->client_id, payload, header.length);
            ToriRSServer_EmbedPump(embed, 0);
        }
    }

    ToriRSServer_EmbedPump(embed, run_tick);

    for( int p = 0; p < peer_count; p++ )
    {
        struct Peer* peer = peers[p];
        struct RevPacket packet;

        if( peer->dropped )
            continue;
        while( (got = ToriRSServer_EmbedRead(embed, peer->client_id, inbound,
                                             (int)sizeof(inbound))) > 0 )
            ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_RECV, inbound, got);
        while( ToriRS_Network_PopPacket(&peer->net, &packet) )
            ;
        if( ToriRSServer_EmbedWrite(embed, peer->client_id, inbound, 0) < 0 )
        {
            int32_t status = TORIRS_NET_STATUS_DISCONNECTED;

            ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, (uint8_t*)&status,
                                     (int)sizeof(status));
            peer->dropped = 1;
        }
    }
}

static void
settle(
    struct Peer** peers,
    int peer_count,
    struct ToriRSServerEmbed* embed)
{
    for( int round = 0; round < 120; round++ )
        pump(peers, peer_count, embed, round % 2 == 0);
}

static int
count_active(
    struct ToriRSServer* world,
    const char* name)
{
    int count = 0;

    for( int pid = 0; pid < world->player_count; pid++ )
        if( world->players[pid].active && strcmp(world->players[pid].display_name, name) == 0 )
            count++;
    return count;
}

static int
file_exists(const char* path)
{
    FILE* file = fopen(path, "rb");

    if( !file )
        return 0;
    fclose(file);
    return 1;
}

/* A small file's text, "" when there is none; good until the next call. */
static const char*
file_text(const char* path)
{
    static char text[4096];
    size_t size = 0;
    FILE* file = fopen(path, "rb");

    if( file )
    {
        size = fread(text, 1, sizeof(text) - 1, file);
        fclose(file);
    }
    text[size] = '\0';
    return text;
}

/* Log `name` in with a one-time login token on a fresh client. */
static void
login_token_once(
    struct ToriRSServerEmbed* embed,
    struct Peer* peer,
    const char* name,
    const char* token)
{
    peer_begin(peer, ToriRSServer_EmbedConnect(embed));
    peer_dial_token(peer, name, token);
    {
        struct Peer* peers[] = { peer };
        settle(peers, 1, embed);
    }
}

/* Log `name` in with `password` on a fresh client; leaves it connected. */
static void
login(
    struct ToriRSServerEmbed* embed,
    struct Peer* peer,
    struct Peer* online,
    const char* name,
    const char* password)
{
    peer_begin(peer, ToriRSServer_EmbedConnect(embed));
    peer_dial(peer, name, password);
    if( online )
    {
        struct Peer* peers[] = { online, peer };
        settle(peers, 2, embed);
    }
    else
    {
        struct Peer* peers[] = { peer };
        settle(peers, 1, embed);
    }
}

/* The password and name rules, which an invite and a claim both apply. */
static void
check_rules(void)
{
    check(!ToriRSServer_AuthPasswordProblem("bob", "tr0ubadour"), "a good password passes");
    check(ToriRSServer_AuthPasswordProblem("bob", "short1") != NULL, "under 8 characters is refused");
    check(ToriRSServer_AuthPasswordProblem("bob", "abcdefghij1234567890x") != NULL,
          "over 20 is refused");
    check(ToriRSServer_AuthPasswordProblem("bob", "allletters") != NULL, "no digit is refused");
    check(ToriRSServer_AuthPasswordProblem("bob", "1234567890") != NULL, "no letter is refused");
    check(ToriRSServer_AuthPasswordProblem("bob", "has space1") != NULL, "a space is refused");
    check(ToriRSServer_AuthPasswordProblem("bob", "xBOB12345") != NULL,
          "containing the name, in any case, is refused");
    check(ToriRSServer_AuthPasswordProblem("Bob Smith", "bobsmith99") != NULL,
          "so is the name without its spaces");
    check(!ToriRSServer_AuthPasswordProblem("Bob Smith", "smithy1234"),
          "a part of it is not the name");
    check(!ToriRSServer_AuthPasswordProblem("bob", "p@ss-w0rd!"), "symbols are allowed");
    check(!ToriRSServer_AuthNameProblem("Bob Smith"), "a good name passes");
    check(ToriRSServer_AuthNameProblem("") != NULL, "an empty name is refused");
    check(ToriRSServer_AuthNameProblem("thirteenchars") != NULL, "a 13-character name is refused");
    check(ToriRSServer_AuthNameProblem("12345") != NULL, "a name with no letter is refused");
    check(ToriRSServer_AuthNameProblem("../etc") != NULL, "a path is refused");
    check(ToriRSServer_AuthNameProblem(" bob") != NULL, "an edge space is refused");
}

/* Invites: web logins for the gate, one name or any, spent once, under the
 * rules, never a player account; and the gate's login never claims. */
static void
check_invites(void)
{
    char named[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1];
    char open_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1];
    char who[64];
    const char* problem = NULL;

    AccountDb_GateLoginRevoke("dave");
    AccountDb_GateLoginRevoke("erin");
    remove(ToriRSServer_AccountPath("dave"));

    check(AccountDb_GateLogin("dave", "davespass1") == TORIRSSERVER_AUTH_REFUSED,
          "the gate's login does not claim a name with no record");
    check(AccountDb_GateLogin("dave", "davespass1") == TORIRSSERVER_AUTH_REFUSED,
          "and made nothing it could sign in with");

    AccountDb_InviteCreate("Dave", named);
    check(strlen(named) == TORIRSSERVER_INVITE_CODE_BYTES * 2, "its code is 32 hex digits");
    check(AccountDb_InviteLookup(named, who, sizeof(who)) && strcmp(who, "Dave") == 0,
          "it names Dave");
    check(AccountDb_InviteRedeem(named, "erin", "erinspass1", &problem) ==
              TORIRSSERVER_INVITE_WRONG_NAME,
          "another name cannot redeem it");
    check(AccountDb_InviteRedeem(named, "dave", "short", &problem) == TORIRSSERVER_INVITE_WEAK &&
              problem,
          "a weak password is refused, with the rule");
    check(AccountDb_InviteRedeem(named, "dave", "secret1234", &problem) == TORIRSSERVER_INVITE_OK,
          "Dave redeems it (the name matches as the account is keyed)");
    check(AccountDb_GateLogin("Dave", "secret1234") == TORIRSSERVER_AUTH_OK,
          "his web login has his password");
    check(!file_exists(ToriRSServer_AccountPath("dave")), "and it is not a player account");
    check(!AccountDb_InviteLookup(named, who, sizeof(who)), "the invite is spent");
    check(AccountDb_InviteRedeem(named, "dave", "secret1234", &problem) ==
              TORIRSSERVER_INVITE_UNKNOWN,
          "and cannot be redeemed twice");

    AccountDb_InviteCreate(NULL, open_code);
    check(AccountDb_InviteLookup(open_code, who, sizeof(who)) && who[0] == '\0',
          "an invite for any name names nobody");
    check(AccountDb_InviteRedeem(open_code, "DAVE", "another12", &problem) ==
              TORIRSSERVER_INVITE_NAME_TAKEN,
          "a taken name is refused");
    check(AccountDb_InviteRedeem(open_code, "../x", "another12", &problem) ==
              TORIRSSERVER_INVITE_BAD_NAME,
          "a bad name is refused");
    check(AccountDb_InviteRedeem(open_code, "erin", "another12", &problem) == TORIRSSERVER_INVITE_OK,
          "Erin redeems it");
    check(AccountDb_InviteRedeem("../../etc/passwd", "erin", "another12", &problem) ==
              TORIRSSERVER_INVITE_UNKNOWN,
          "a code that is not a code is no invite");
}

/* The token endpoint's grants, without the HTTP. */
static void
check_oauth(void)
{
    static struct ToriRSServerOAuthTokens first, second, third;
    struct ToriRSServerOAuthClaims claims;
    char code[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char challenge[48];
    char tampered[TORIRSSERVER_OAUTH_TOKEN_MAX];
    uint8_t digest[SHA256_DIGEST_BYTES];
    const char* verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    const char* why = NULL;
    long long now = 2000000000LL;

    ToriRSServer_OAuthKeyLoad();
    /* RFC 7636 appendix B's own example. */
    sha256((const uint8_t*)verifier, strlen(verifier), digest);
    ToriRSServer_OAuthBase64Url(digest, sizeof(digest), challenge);
    check(strcmp(challenge, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM") == 0,
          "oauth: PKCE S256 matches RFC 7636's example");

    check(ToriRSServer_OAuthPasswordGrant("alice", "nope12345", NULL, now, &first, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "oauth: a wrong password is invalid_grant");
    check(ToriRSServer_OAuthPasswordGrant("alice", "newpass12", "everything", now, &first, &why) ==
              TORIRSSERVER_OAUTH_INVALID_SCOPE,
          "oauth: an unknown scope is invalid_scope");
    check(ToriRSServer_OAuthPasswordGrant("alice", "newpass12", NULL, now, &first, &why) ==
              TORIRSSERVER_OAUTH_OK,
          "oauth: the password grant issues tokens");
    check(ToriRSServer_OAuthVerify(first.access_token, now, &claims) &&
              strcmp(claims.sub, "alice") == 0 && strcmp(claims.typ, "access") == 0 &&
              strcmp(claims.scope, TORIRSSERVER_OAUTH_SCOPE) == 0,
          "oauth: the access token is a JWT for alice with the game scope");
    check(!ToriRSServer_OAuthVerify(first.access_token, now + TORIRSSERVER_OAUTH_ACCESS_SECONDS,
                                    &claims),
          "oauth: and expires");
    snprintf(tampered, sizeof(tampered), "%s", first.access_token);
    tampered[strlen(tampered) - 2] = tampered[strlen(tampered) - 2] == 'A' ? 'B' : 'A';
    check(!ToriRSServer_OAuthVerify(tampered, now, &claims), "oauth: a tampered signature is refused");
    check(!ToriRSServer_OAuthVerify(
              "eyJhbGciOiJub25lIn0.eyJzdWIiOiJhbGljZSJ9.", now, &claims),
          "oauth: alg=none is refused");

    check(ToriRSServer_OAuthRefreshGrant(first.refresh_token, NULL, now + 1, &second, &why) ==
              TORIRSSERVER_OAUTH_OK,
          "oauth: the refresh token buys a new pair");
    check(strcmp(second.refresh_token, first.refresh_token) != 0, "oauth: and is rotated");
    check(ToriRSServer_OAuthRefreshGrant(first.refresh_token, NULL, now + 2, &third, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "oauth: the spent refresh token is refused");
    check(ToriRSServer_OAuthRefreshGrant(second.refresh_token, NULL, now + 3, &third, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "oauth: and its reuse revoked the whole family");

    check(ToriRSServer_OAuthPasswordGrant("alice", "newpass12", NULL, now, &first, &why) ==
              TORIRSSERVER_OAUTH_OK,
          "oauth: (sign in again)");
    ToriRSServer_OAuthRevoke(first.refresh_token, now);
    check(ToriRSServer_OAuthRefreshGrant(first.refresh_token, NULL, now + 1, &second, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "oauth: a revoked refresh token is refused");

    check(ToriRSServer_OAuthRedirectAllowed("/play/callback"), "oauth: a same-origin path may redirect");
    check(ToriRSServer_OAuthRedirectAllowed("http://127.0.0.1:51234/cb"),
          "oauth: so may a native app's loopback");
    check(!ToriRSServer_OAuthRedirectAllowed("//evil.example/cb"), "oauth: //host may not");
    check(!ToriRSServer_OAuthRedirectAllowed("https://evil.example/cb"),
          "oauth: nor another origin");
    check(ToriRSServer_OAuthAuthorize("alice", "newpass12", "/cb", challenge, NULL, now, code, &why) ==
              TORIRSSERVER_OAUTH_OK,
          "oauth: authorize issues a code");
    check(ToriRSServer_OAuthCodeGrant(code, "/cb", "wrong-verifier-wrong-verifier-wrong-verifier",
                                      now, &first, &why) == TORIRSSERVER_OAUTH_INVALID_GRANT,
          "oauth: a wrong verifier is refused");
    check(ToriRSServer_OAuthCodeGrant(code, "/cb", verifier, now, &first, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "oauth: and spent the code, so the right one is too late");
    check(ToriRSServer_OAuthAuthorize("alice", "newpass12", "/cb", challenge, NULL, now, code, &why) ==
              TORIRSSERVER_OAUTH_OK,
          "oauth: (a fresh code)");
    check(ToriRSServer_OAuthCodeGrant(code, "/other", verifier, now, &first, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "oauth: a different redirect_uri is refused");
    check(ToriRSServer_OAuthAuthorize("alice", "newpass12", "/cb", challenge, NULL, now, code, &why) ==
              TORIRSSERVER_OAUTH_OK,
          "oauth: (a fresh code)");
    check(ToriRSServer_OAuthCodeGrant(code, "/cb", verifier, now, &first, &why) ==
              TORIRSSERVER_OAUTH_OK &&
              strcmp(first.name, "alice") == 0,
          "oauth: the code and its verifier sign alice in");
}

/* Player accounts linked to a web login (torirs_server_auth.h "Linked player
 * accounts"): one owner, the token's hash on the record, nothing else signs
 * it in. */
static void
check_links(void)
{
    static struct ToriRSServerOAuthTokens tokens, after;
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    char again[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    char other[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    char otl[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char early_otl[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char name[64];
    char owner[64];
    const char* why = NULL;
    long long now = (long long)time(NULL);

    remove(ToriRSServer_AccountPath("frank"));
    remove(ToriRSServer_AccountPath("gina"));
    ToriRSServer_OAuthKeyLoad();
    check(ToriRSServer_OAuthPasswordGrant("frank", "blue7horse", NULL, now, &tokens, &why) ==
              TORIRSSERVER_OAUTH_OK,
          "link: (frank claims his name and signs in)");
    check(ToriRSServer_OAuthPlay(tokens.access_token, now, early_otl, name, sizeof(name)) ==
              TORIRSSERVER_OAUTH_OK,
          "link: (and mints a one-time login before linking)");
    check(ToriRSServer_AuthManagedBy("frank", owner, sizeof(owner)) == 0,
          "link: an account starts unlinked");

    check(ToriRSServer_AuthLink("frank", "wrongpass1", "matt", token) == TORIRSSERVER_LINK_REFUSED &&
              !token[0],
          "link: the wrong password links nothing");
    check(ToriRSServer_AuthLink("frank", "blue7horse", "Matt", token) == TORIRSSERVER_LINK_OK &&
              strlen(token) == TORIRSSERVER_LINK_TOKEN_HEX,
          "link: the right one links him and mints a token");
    check(ToriRSServer_AuthManagedBy("frank", owner, sizeof(owner)) == 1 &&
              strcmp(owner, "matt") == 0,
          "link: the record names its owner as keyed");
    check(!strstr(file_text(ToriRSServer_AccountPath("frank")), token) &&
              strstr(file_text(ToriRSServer_AccountPath("frank")), "link_hash = "),
          "link: the record keeps the token's hash, not the token");
    check(ToriRSServer_AuthLinkVerify("frank", "matt", token), "link: the token verifies");
    check(!ToriRSServer_AuthLinkVerify("frank", "nina", token), "link: for its owner only");
    check(ToriRSServer_AuthLink("frank", "blue7horse", "nina", other) ==
              TORIRSSERVER_LINK_OTHER_USER,
          "link: another login cannot take him, even with his password");
    check(ToriRSServer_AuthLinkVerify("frank", "matt", token), "link: and the link stands");

    check(ToriRSServer_AuthLogin("frank", "blue7horse") == TORIRSSERVER_AUTH_MANAGED,
          "link: his password now answers 'managed'");
    check(ToriRSServer_AuthLogin("frank", "wrongpass1") == TORIRSSERVER_AUTH_REFUSED,
          "link: a wrong one is still just wrong");
    check(ToriRSServer_OAuthPasswordGrant("frank", "blue7horse", NULL, now, &after, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "link: the password grant refuses him");
    check(ToriRSServer_OAuthPlay(tokens.access_token, now, otl, name, sizeof(name)) ==
              TORIRSSERVER_OAUTH_INVALID_TOKEN,
          "link: his old access token buys no play");
    check(ToriRSServer_OAuthRefreshGrant(tokens.refresh_token, NULL, now, &after, &why) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "link: nor does his old refresh token");
    check(!ToriRSServer_OAuthRedeemOtl(early_otl, "frank", now),
          "link: a one-time login from before the link is refused");

    check(ToriRSServer_OAuthUserPlay("matt", "Frank", token, now, otl, name, sizeof(name)) ==
                  TORIRSSERVER_OAUTH_OK &&
              strcmp(name, "Frank") == 0,
          "link: his owner's session plays him with the token");
    check(ToriRSServer_OAuthRedeemOtl(otl, "frank", now), "link: and that one-time login logs in");
    check(ToriRSServer_OAuthUserPlay("nina", "frank", token, now, otl, name, sizeof(name)) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "link: another login's session cannot");
    snprintf(other, sizeof(other), "%s", token);
    other[0] = other[0] == 'a' ? 'b' : 'a';
    check(ToriRSServer_OAuthUserPlay("matt", "frank", other, now, otl, name, sizeof(name)) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "link: nor the owner with a wrong token");

    check(ToriRSServer_AuthLink("frank", "blue7horse", "matt", again) == TORIRSSERVER_LINK_OK &&
              strcmp(again, token) != 0,
          "link: the owner linking again mints a new token");
    check(ToriRSServer_OAuthUserPlay("matt", "frank", token, now, otl, name, sizeof(name)) ==
              TORIRSSERVER_OAUTH_INVALID_GRANT,
          "link: and the old one is dead");
    check(ToriRSServer_OAuthUserPlay("matt", "frank", again, now, otl, name, sizeof(name)) ==
              TORIRSSERVER_OAUTH_OK,
          "link: the new one plays");

    check(ToriRSServer_AuthSetAdmin("frank", 1) && ToriRSServer_AuthLinkVerify("frank", "matt", again),
          "link: setting admin keeps the link");
    check(ToriRSServer_AuthSetAdmin("frank", 0), "(and clearing it)");
    check(ToriRSServer_AuthUnlink("frank"), "link: unlink");
    check(ToriRSServer_AuthManagedBy("frank", owner, sizeof(owner)) == 0 &&
              !ToriRSServer_AuthLinkVerify("frank", "matt", again),
          "link: leaves him unlinked, the token dead");
    check(ToriRSServer_AuthLogin("frank", "blue7horse") == TORIRSSERVER_AUTH_OK,
          "link: and his password his own again");

    check(ToriRSServer_AuthLink("gina", "green9owl", "matt", token) ==
                  TORIRSSERVER_LINK_NO_ACCOUNT &&
              !token[0],
          "link: a name with no account cannot be linked");
    check(!file_exists(ToriRSServer_AccountPath("gina")),
          "link: and the link does not create the account");
    check(ToriRSServer_AuthLogin("gina", "green9owl") == TORIRSSERVER_AUTH_CLAIMED,
          "link: (gina logs in, which makes her account)");
    check(ToriRSServer_AuthLink("gina", "green9owl", "matt", token) == TORIRSSERVER_LINK_OK &&
              ToriRSServer_AuthLinkVerify("gina", "matt", token),
          "link: and now it links");
}

/* A world's log through the account server's own route, as the link sends
 * it: each record a line, POST /events, `acked <seq>` back. The log is
 * emptied; `*next_seq` numbers the records. Returns what was acknowledged. */
static long long
send_log(
    struct ToriRSServerAccountLog* log,
    long long* next_seq,
    long long* out_first_seq)
{
    static char body[64 * 1024];
    size_t len = 0;
    struct HttpRequest req;
    struct HttpResponse res;
    long long acked = -1;

    if( out_first_seq )
        *out_first_seq = *next_seq;
    for( int i = 0; i < log->count; i++ )
        len += (size_t)AccountProto_RecordFormat(&log->records[i], (*next_seq)++, body + len);
    log->count = 0;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    snprintf(req.method, sizeof(req.method), "POST");
    snprintf(req.path, sizeof(req.path), "/events?source=test");
    req.body = (uint8_t const*)body;
    req.body_len = (int)len;
    AccountServer_Handle(NULL, &req, &res);
    if( res.status == 200 && res.body && strncmp((char const*)res.body, "acked\t", 6) == 0 )
        acked = strtoll((char const*)res.body + 6, NULL, 10);
    if( res.owns_body )
        ToriMem_Free(ToriMem_Net(), res.body);
    return acked;
}

/* The same batch again, as the link resends after a lost answer. */
static long long
resend(
    char const* body,
    size_t len)
{
    struct HttpRequest req;
    struct HttpResponse res;
    long long acked = -1;

    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    snprintf(req.method, sizeof(req.method), "POST");
    snprintf(req.path, sizeof(req.path), "/events?source=test");
    req.body = (uint8_t const*)body;
    req.body_len = (int)len;
    AccountServer_Handle(NULL, &req, &res);
    if( res.status == 200 && res.body && strncmp((char const*)res.body, "acked\t", 6) == 0 )
        acked = strtoll((char const*)res.body + 6, NULL, 10);
    if( res.owns_body )
        ToriMem_Free(ToriMem_Net(), res.body);
    return acked;
}

/* The account database: what the world queues, written by one apply, read
 * back as the site reads it -- staff never ranked, a minute's stats only when
 * they moved, a session per logout, the adventure log. */
static void
check_accounts(void)
{
    static struct ToriRSServerPlayer quinn;
    static struct ToriRSServerPlayer root;
    struct ToriRSServerAccountLog* log = ToriRSServer_AccountLogCreate();
    long long seq = 1000;
    long long first = 0;
    char again[ACCOUNT_RECORD_LINE_MAX * 2];
    size_t again_len = 0;
    struct AccountPlayer row;
    struct AccountPlayer staff;
    struct AccountRow top[2];
    struct AccountSession sessions[4];
    struct AccountEvent events[4];
    long long t = 1800000000LL;
    long long day = t / 86400;
    int total = 0;
    long long played = 0;

    ToriRSServer_WorldSetDisplayName(&quinn, "Quinn");
    quinn.stat_xp_tenths[0] = 130344310; /* 99 Attack */
    quinn.stat_xp_tenths[3] = 11540;
    quinn.varps[TORIRSSERVER_ACCOUNTLOG_VARP_QP] = 3;
    ToriRSServer_WorldSetDisplayName(&root, "Root");
    root.account_admin = 1;
    root.stat_xp_tenths[0] = 2000000000; /* 200M: more than anyone */
    root.stat_xp_tenths[3] = 11540;

    ToriRSServer_AccountLogLogin(log, &quinn, t);
    ToriRSServer_AccountLogLogin(log, &root, t);
    check(send_log(log, &seq, NULL) == 1001, "accounts: the server acknowledges the batch");
    check(AccountDb_Player("quinn", &row) && row.xp[0] == 13034431 &&
              row.quest_points == 3 && row.ranked && strcmp(row.name, "Quinn") == 0,
          "accounts: a login's stats are the player's");
    check(AccountDb_Player("root", &staff) && !staff.ranked && staff.xp[0] == 200000000,
          "accounts: staff keep their stats, unranked");
    check(AccountDb_Rank(staff.id, 0) == 0 && AccountDb_Rank(row.id, 0) == 1,
          "accounts: staff are not ranked and push nobody down");
    check(AccountDb_Hiscores(0, 0, 2, top) >= 1 && strcmp(top[0].key, "quinn") == 0,
          "accounts: the Attack hiscores lead with Quinn, not Root");

    quinn.stat_xp_tenths[0] += 10000;
    ToriRSServer_AccountLogStats(log, &quinn, t + 60);
    ToriRSServer_AccountLogStats(log, &quinn, t + 120);
    check(log->count == 1, "accounts: stats queue only when they moved");
    ToriRSServer_AccountLogEvent(log, &quinn, t + 130, TORIRSSERVER_ADVENTURE_QUEST, 1,
                                 "Cook's Assistant");
    ToriRSServer_AccountLogLogout(log, &quinn, t + 3600);
    for( int i = 0; i < log->count; i++ )
        again_len += (size_t)AccountProto_RecordFormat(&log->records[i], seq + i, again + again_len);
    check(send_log(log, &seq, &first) == first + 2, "accounts: stats, quest and logout acknowledged");
    check(resend(again, again_len) == first + 2, "accounts: a resent batch is acknowledged again");
    check(AccountDb_Player("quinn", &row) && row.xp[0] == 13035431,
          "accounts: the change is the player's");
    check(AccountDb_XpAt(row.id, 0, day) == 13035431 &&
              AccountDb_XpAt(row.id, 0, day - 1) == -1 &&
              AccountDb_XpFirstDay(row.id, 0) == day,
          "accounts: the day's experience, and nothing before the first day");
    check(AccountDb_Sessions(row.id, sessions, 4, &total, &played) == 1 && total == 1 &&
              played == 3600 && sessions[0].start == t && sessions[0].xp_gained == 1000 &&
              sessions[0].top_stat == 0 && sessions[0].top_gain == 1000,
          "accounts: a logout is a session, with its gain");
    check(AccountDb_Events(row.id, events, 4) == 1 &&
              events[0].kind == TORIRSSERVER_ADVENTURE_QUEST && events[0].amount == 1 &&
              strcmp(events[0].label, "Cook's Assistant") == 0,
          "accounts: a quest is a line of the adventure log");

    AccountDb_SetRanked("Root", 1);
    check(AccountDb_Rank(staff.id, 0) == 1 && AccountDb_Rank(row.id, 0) == 2,
          "accounts: ranked again, Root leads");
    AccountDb_SetRanked("Root", 0);
    check(AccountDb_Rank(staff.id, 0) == 0 && AccountDb_Rank(row.id, 0) == 1,
          "accounts: and admin again, off the list at once");

    /* A display name set on Tutorial Island: the same account, shown by it. */
    snprintf(quinn.display_name, sizeof(quinn.display_name), "Quinn Fair");
    ToriRSServer_AccountLogStats(log, &quinn, t + 4000);
    quinn.stat_xp_tenths[0] += 10000;
    ToriRSServer_AccountLogStats(log, &quinn, t + 4060);
    send_log(log, &seq, NULL);
    check(AccountDb_Player("quinn", &row) && strcmp(row.name, "Quinn Fair") == 0 &&
              !AccountDb_Player("quinn_fair", &staff),
          "accounts: a display name is the account's name on the hiscores, under its key");
    ToriRSServer_AccountLogFree(log);
}

/* Display names (torirs_server_savefile.h): one account each, and nobody
 * logs in as a name somebody else shows. */
static void
check_display_names(void)
{
    char owner[64];

    check(!ToriRSServer_DisplayNameOwner("Mara Vell", owner, sizeof(owner)),
          "names: a name nobody holds is free");
    check(ToriRSServer_DisplayNameClaim("Mara Vell", "kit"), "names: an account reserves one");
    check(ToriRSServer_DisplayNameOwner("mara_vell", owner, sizeof(owner)) &&
              strcmp(owner, "kit") == 0,
          "names: held by that account, however it is spelled");
    check(ToriRSServer_DisplayNameClaim("MARA VELL", "kit"), "names: which may claim it again");
    check(!ToriRSServer_DisplayNameClaim("Mara Vell", "lou"), "names: and nobody else may");
    check(ToriRSServer_AuthLogin("Mara Vell", "plum4tree9") == TORIRSSERVER_AUTH_REFUSED,
          "names: nobody logs in under a name another account shows");
    {
        enum ToriRSServerAuthResult odo = ToriRSServer_AuthLogin("odo", "plum4tree9");

        check((odo == TORIRSSERVER_AUTH_CLAIMED || odo == TORIRSSERVER_AUTH_OK) &&
                  ToriRSServer_DisplayNameOwner("Odo", owner, sizeof(owner)) &&
                  strcmp(owner, "odo") == 0,
              "names: an account's login name is its own");
    }
    check(!ToriRSServer_DisplayNameClaim("Odo", "kit"),
          "names: and no other account's display name");
    ToriRSServer_DisplayNameRelease("Mara Vell", "lou");
    check(ToriRSServer_DisplayNameOwner("Mara Vell", owner, sizeof(owner)),
          "names: only the holder gives one up");
    ToriRSServer_DisplayNameRelease("Mara Vell", "kit");
    check(!ToriRSServer_DisplayNameOwner("Mara Vell", owner, sizeof(owner)),
          "names: and then it is free");
}

/* The world's spool: records wait in it while the account server does not
 * answer, a send step never waits, and a restart finds them still there. */
static void
check_spool(void)
{
    static struct ToriRSServerPlayer pat;
    struct ToriRSServerAccountLog* log = ToriRSServer_AccountLogCreate();
    struct ToriRSServerAccountLink* link;
    char path[1024];
    struct timespec t0;
    struct timespec t1;
    double ms;

    snprintf(path, sizeof(path), "%s/account_spool.tsv", ToriRSServer_SaveDir());
    remove(path);
    /* Nothing listens on port 1: the account server is down. */
    setenv("TORIRS_ACCOUNT_SERVER", "127.0.0.1:1", 1);
    link = ToriRSServer_AccountLinkOpen();
    check(ToriRSServer_AccountLinkPending(link) == 0, "spool: a new spool has nothing waiting");
    ToriRSServer_WorldSetDisplayName(&pat, "Pat");
    pat.stat_xp_tenths[3] = 11540;
    ToriRSServer_AccountLogLogin(log, &pat, 100);
    ToriRSServer_AccountLogLogout(log, &pat, 200);
    ToriRSServer_AccountLinkWrite(link, log);
    check(log->count == 0 && ToriRSServer_AccountLinkPending(link) == 2,
          "spool: written records wait in it");
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for( int i = 0; i < 50; i++ )
        ToriRSServer_AccountLinkPump(link, 100000L + i * 10L);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    check(ms < 50.0, "spool: fifty send steps against a dead server return at once");
    check(ToriRSServer_AccountLinkPending(link) == 2, "spool: and the records still wait");
    ToriRSServer_AccountLinkClose(link);
    link = ToriRSServer_AccountLinkOpen();
    check(ToriRSServer_AccountLinkPending(link) == 2, "spool: a restart finds them still waiting");
    ToriRSServer_AccountLinkClose(link);
    unsetenv("TORIRS_ACCOUNT_SERVER");
    remove(path);
    ToriRSServer_AccountLogFree(log);
}

/* With ACCOUNT_SERVER_LIVE=<host:port>, the link against a running account
 * server: two records over a real socket, until the spool is empty. */
static void
check_link_live(void)
{
    static struct ToriRSServerPlayer lia;
    char const* live = getenv("ACCOUNT_SERVER_LIVE");
    struct ToriRSServerAccountLog* log;
    struct ToriRSServerAccountLink* link;
    char path[1024];

    if( !live )
        return;
    log = ToriRSServer_AccountLogCreate();
    snprintf(path, sizeof(path), "%s/account_spool.tsv", ToriRSServer_SaveDir());
    remove(path);
    setenv("TORIRS_ACCOUNT_SERVER", live, 1);
    link = ToriRSServer_AccountLinkOpen();
    ToriRSServer_WorldSetDisplayName(&lia, "Lia");
    lia.stat_xp_tenths[3] = 11540;
    lia.stat_xp_tenths[10] = 123450;
    ToriRSServer_AccountLogLogin(log, &lia, (long long)time(NULL) - 60);
    ToriRSServer_AccountLogLogout(log, &lia, (long long)time(NULL));
    ToriRSServer_AccountLinkWrite(link, log);
    ToriRSServer_AccountLinkDrain(link, 3000);
    check(ToriRSServer_AccountLinkPending(link) == 0,
          "live: the account server acknowledged everything sent");
    ToriRSServer_AccountLinkClose(link);
    unsetenv("TORIRS_ACCOUNT_SERVER");
    remove(path);
    ToriRSServer_AccountLogFree(log);
}

/* The first open imports the files the database replaced: the gate's
 * logins and invites, the saves' stats, the history logs. */
static void
check_accounts_import(void)
{
    char path[1024];
    char record[TORIRSSERVER_AUTH_RECORD_MAX];
    char text[512];
    char who[64];
    uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES] = { 1, 2, 3 };
    char const* code = "0123456789abcdef0123456789abcdef";
    struct AccountPlayer row;
    struct AccountSession sessions[4];
    int total = 0;
    long long played = 0;
    FILE* file;

    AccountDb_Close();
    snprintf(path, sizeof(path), "%s", AccountDb_Path());
    remove(path);
    strcat(path, "-wal");
    remove(path);
    snprintf(path, sizeof(path), "%s-shm", AccountDb_Path());
    remove(path);

    ToriRSServer_AuthRecord("olgapass12", salt, 1000, record);
    snprintf(text, sizeof(text), "[account]\npassword = %s\n", record);
    check(ToriRSServer_SaveWriteText(ToriRSServer_GateAccountPath("olga"), text),
          "(an old web login file)");
    snprintf(path, sizeof(path), "%s/gate/invites/%s.ini", ToriRSServer_SaveDir(), code);
    check(ToriRSServer_SaveWriteText(path, "[invite]\nname = Pia\n"), "(an old invite file)");
    check(ToriRSServer_SaveWriteText(ToriRSServer_SavePath("rex"),
                                     "[player]\nname = Rex\n[stats]\n0 = 1 50000\n3 = 10 11540\n"
                                     "[varps]\n101 = 7\n"),
          "(an old save)");
    snprintf(path, sizeof(path), "%s/history/rex.log", ToriRSServer_SaveDir());
    ToriRSServer_SaveMkdirParent(path);
    file = fopen(path, "wb");
    check(file != NULL, "(an old history log)");
    if( file )
    {
        fputs("1000 login\n1600 logout 40000 0 0 11540", file);
        for( int i = 4; i < 25; i++ )
            fputs(" 0", file);
        fputs("\n", file);
        fclose(file);
    }

    AccountDb_Open();
    check(AccountDb_GateLogin("olga", "olgapass12") == TORIRSSERVER_AUTH_OK,
          "import: an old web login signs in");
    check(AccountDb_InviteLookup(code, who, sizeof(who)) && strcmp(who, "Pia") == 0,
          "import: an old invite is still good");
    check(AccountDb_Player("rex", &row) && row.xp[0] == 5000 && row.quest_points == 7 &&
              strcmp(row.name, "Rex") == 0,
          "import: a save's stats");
    check(AccountDb_XpAt(row.id, 0, 0) == 4000,
          "import: the history's logout is that day's experience");
    check(AccountDb_Sessions(row.id, sessions, 4, &total, &played) == 1 &&
              sessions[0].start == 1000 && sessions[0].end == 1600 && played == 600,
          "import: the history's login and logout are a session");

    AccountDb_GateLoginRevoke("olga");
    remove(ToriRSServer_GateAccountPath("olga"));
    snprintf(path, sizeof(path), "%s/gate/invites/%s.ini", ToriRSServer_SaveDir(), code);
    remove(path);
    remove(ToriRSServer_SavePath("rex"));
    snprintf(path, sizeof(path), "%s/history/rex.log", ToriRSServer_SaveDir());
    remove(path);
}

/* Accounts an operator makes (torirsserver --account, torirsctl). */
static void
check_operator_accounts(void)
{
    const char* why = NULL;
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];

    remove(ToriRSServer_AccountPath("jack"));
    AccountDb_GateLoginRevoke("kate");
    check(ToriRSServer_AuthCreate("jack", "short", 1, &why) == TORIRSSERVER_CREATE_WEAK && why,
          "create: a weak password is refused, with the rule");
    check(!file_exists(ToriRSServer_AccountPath("jack")), "create: and writes nothing");
    check(ToriRSServer_AuthCreate("../x", "blue7horse", 1, &why) == TORIRSSERVER_CREATE_BAD_NAME,
          "create: a bad name is refused");
    check(ToriRSServer_AuthCreate("Jack", "blue7horse", 1, &why) == TORIRSSERVER_CREATE_OK,
          "create: an admin player account");
    check(ToriRSServer_AuthIsAdmin("jack") &&
              ToriRSServer_AuthLogin("jack", "blue7horse") == TORIRSSERVER_AUTH_OK,
          "create: it is staff, and its password logs in");
    check(ToriRSServer_AuthCreate("jack", "other8pass", 0, &why) == TORIRSSERVER_CREATE_EXISTS &&
              ToriRSServer_AuthLogin("jack", "blue7horse") == TORIRSSERVER_AUTH_OK,
          "create: never over an existing account");
    check(ToriRSServer_AuthLink("jack", "blue7horse", "matt", token) == TORIRSSERVER_LINK_OK,
          "(link jack)");
    check(!ToriRSServer_AuthSetPassword("jack", "short", &why) && why,
          "password: a weak one is refused");
    check(ToriRSServer_AuthSetPassword("jack", "red8fox99", &why),
          "password: a new one is set");
    check(ToriRSServer_AuthIsAdmin("jack") && ToriRSServer_AuthLinkVerify("jack", "matt", token),
          "password: keeping the admin flag and the link");
    check(!ToriRSServer_AuthSetPassword("nobody_here", "red8fox99", &why),
          "password: only for an account that exists");
    check(AccountDb_GateLoginCreate("kate", "blue7horse", &why) == TORIRSSERVER_CREATE_OK &&
              AccountDb_GateLogin("kate", "blue7horse") == TORIRSSERVER_AUTH_OK,
          "login: a web login made without an invite signs in");
    check(AccountDb_GateLoginCreate("kate", "other8pass", &why) == TORIRSSERVER_CREATE_EXISTS,
          "login: never over an existing one");
    check(AccountDb_GateLoginRevoke("Kate") &&
              AccountDb_GateLogin("kate", "blue7horse") == TORIRSSERVER_AUTH_REFUSED,
          "revoke: the login cannot sign in again");
    check(!AccountDb_GateLoginRevoke("kate"), "revoke: and is gone");
}

/* The gate's ticket: what io_server sets and torirsserver reads. */
static void
check_tickets(void)
{
    char ticket[TORIRSSERVER_GATE_TICKET_MAX];
    char cookie[TORIRSSERVER_GATE_TICKET_MAX + 64];
    char name[64];
    long long now = 2000000000LL;

    ToriRSServer_GateKeyLoad();
    ToriRSServer_GateTicketMint("Dave", now + 60, ticket);
    check(ToriRSServer_GateTicketCheck(ticket, strlen(ticket), now, name, sizeof(name)) &&
              strcmp(name, "dave") == 0,
          "a minted ticket checks, naming the account as keyed");
    check(!ToriRSServer_GateTicketCheck(ticket, strlen(ticket), now + 61, name, sizeof(name)),
          "an expired one does not");
    ticket[0] = 'e';
    check(!ToriRSServer_GateTicketCheck(ticket, strlen(ticket), now, name, sizeof(name)),
          "nor one with another name on it");
    ticket[0] = 'd';
    snprintf(cookie, sizeof(cookie), "a=1; torirs_gate=bogus; " TORIRSSERVER_GATE_COOKIE "=%s; b=2",
             ticket);
    check(ToriRSServer_GateCookieTicket(cookie, strlen(cookie), now, name, sizeof(name)),
          "the cookie is found among others, past a bad one");
    check(!ToriRSServer_GateCookieTicket("torirs_gate=", 12, now, name, sizeof(name)),
          "an empty cookie is no ticket");
}

int
main(void)
{
    static struct Peer first, wrong, right, intruder, dev, staff, plain;
    struct ToriRSServerEmbed* embed;
    struct ToriRSServer* world;
    char account[1024];
    int reused_client = -1;
    int wrong_name_client = -1;

    check_vectors();

    setenv("TORIRSSERVER_REV", "osrs239", 1);
    setenv("TORIRSSERVER_SAVES", "build/login_password_test_saves", 1);
    snprintf(account, sizeof(account), "%s", ToriRSServer_AccountPath("alice"));
    remove(account);
    remove(ToriRSServer_SavePath("alice"));
    remove(ToriRSServer_AccountPath("carol"));
    /* Web logins an older build of this test left as files: a fresh database
     * would import them. */
    remove(ToriRSServer_GateAccountPath("dave"));
    remove(ToriRSServer_GateAccountPath("erin"));
    remove(ToriRSServer_GateAccountPath("kate"));
    /* Henry's save from an earlier run would be imported as a player; 5d
     * checks that his session leaves none. */
    remove(ToriRSServer_SavePath("henry"));
    /* First, while nothing has opened the database: it starts fresh. */
    check_accounts_import();

    embed = ToriRSServer_EmbedStart(NULL);
    if( !embed )
    {
        fprintf(stderr, "login-password: could not start the server\n");
        return 1;
    }
    /* 0. The embedded world serves one local player and checks nothing: a
     *    name with no record logs in and is NOT given one. */
    peer_begin(&first, 0);
    peer_dial(&first, "carol", "whatever");
    {
        struct Peer* peers[] = { &first };
        settle(peers, 1, embed);
    }
    world = ToriRSServer_EmbedWorld(embed);
    check(first.net.state == TORIRS_NET_GAME && world, "the embedded world logs anyone in");
    if( !world )
        return 1;
    check(!world->check_passwords, "because it does not check passwords");
    check(!file_exists(ToriRSServer_AccountPath("carol")), "and writes no account record");
    ToriRSServer_EmbedDisconnect(embed, first.client_id);
    world->check_passwords = 1;
    /* As torirsserver_main.c gives a world that checks passwords. */
    world->account_log = ToriRSServer_AccountLogCreate();

    /* 1. A name with no record: the login sets its password. */
    login(embed, &first, NULL, "alice", "correcthorse9");
    check(first.net.state == TORIRS_NET_GAME, "alice's first login is accepted");
    check(file_exists(account), "and writes her account record");
    ToriRSServer_EmbedDisconnect(embed, first.client_id);
    check(count_active(world, "alice") == 0, "she logs out");

    /* 2. The wrong password is refused, as response 3. */
    login(embed, &wrong, NULL, "alice", "batterystaple1");
    check(wrong.net.state != TORIRS_NET_GAME, "a wrong password is refused");
    check(wrong.net.login_reply == 3, "with 'invalid username or password' (3)");
    check(count_active(world, "alice") == 0, "and seats nobody");

    /* 3. The right one still works. */
    login(embed, &right, NULL, "alice", "correcthorse9");
    check(right.net.state == TORIRS_NET_GAME, "the password she set logs her in");
    check(count_active(world, "alice") == 1, "once");

    /* 4. A wrong password must not take over the session she is playing. */
    login(embed, &intruder, &right, "alice", "letmein");
    check(intruder.net.state != TORIRS_NET_GAME, "a wrong password while she plays is refused");
    check(!right.dropped && ToriRSServer_EmbedPlayer(embed, right.client_id),
          "and does not kick her off");
    ToriRSServer_EmbedDisconnect(embed, right.client_id);

    /* 5. Development mode: anything logs in, and nothing is rewritten. */
    world->check_passwords = 0;
    login(embed, &dev, NULL, "alice", "anything");
    check(dev.net.state == TORIRS_NET_GAME, "development mode takes any password");
    ToriRSServer_EmbedDisconnect(embed, dev.client_id);
    world->check_passwords = 1;

    /* 5b. An account with `admin = 1` logs in as staff: the jmod crown, and
     *     cheats on any server. Without it, no cheats -- except in development
     *     mode, where a name proves nothing. */
    check(ToriRSServer_AuthSetAdmin("alice", 1), "(make alice an admin)");
    check(ToriRSServer_AuthIsAdmin("alice"), "the record says so");
    check(ToriRSServer_AuthLogin("alice", "correcthorse9") == TORIRSSERVER_AUTH_OK,
          "and setting it kept her password");
    login(embed, &staff, NULL, "alice", "correcthorse9");
    {
        struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, staff.client_id);

        check(player && player->staff_level == 2, "an admin account logs in at staff level 2");
        check(staff.net.staff_mod_level == 2,
              "and the client reads staffModLevel 2 off the login response (the crown)");
        check(player && player->account_admin, "as an admin account (no Tutorial Island)");
        check(player && ToriRSServer_PlayerMayCheat(world, player), "and may cheat");
    }
    ToriRSServer_EmbedDisconnect(embed, staff.client_id);
    /* The admin flag holds whatever the server's settings: in development mode
     * too, or the rev-239 client would never send her ::commands as cheats. */
    world->check_passwords = 0;
    login(embed, &dev, NULL, "alice", "anything");
    {
        struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, dev.client_id);

        check(player && player->staff_level == 2,
              "an admin account is staff in development mode too");
        check(player && player->account_admin, "and an admin account there");
        world->check_passwords = 1;
        check(player && ToriRSServer_PlayerMayCheat(world, player),
              "and may cheat whatever the server checks");
    }
    ToriRSServer_EmbedDisconnect(embed, dev.client_id);
    check(ToriRSServer_AuthSetAdmin("alice", 0), "(and not an admin)");
    check(!ToriRSServer_AuthIsAdmin("alice"), "the record says so");
    login(embed, &plain, NULL, "alice", "correcthorse9");
    {
        struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, plain.client_id);

        check(player && player->staff_level == 0, "a plain account logs in at staff level 0");
        check(plain.net.staff_mod_level == 0, "and the client reads staffModLevel 0");
        check(player && !player->account_admin, "and not as an admin account");
        check(player && !ToriRSServer_PlayerMayCheat(world, player),
              "and may not cheat where passwords are checked");
        world->check_passwords = 0;
        check(player && ToriRSServer_PlayerMayCheat(world, player),
              "but may in development mode");
        world->check_passwords = 1;
    }
    ToriRSServer_EmbedDisconnect(embed, plain.client_id);

    /* 5c. OAuth, as the deob's launcher login runs it: tokens from the
     *     password grant, an access token traded at "play" for a one-time
     *     login token, and that token in the login block (auth type TOKEN). */
    {
        static struct ToriRSServerOAuthTokens tokens;
        static struct Peer otl_peer, reused, wrong_name;
        char otl[TORIRSSERVER_OAUTH_ID_HEX + 1];
        char name[64];
        const char* why = NULL;
        long long now = (long long)time(NULL);

        ToriRSServer_OAuthKeyLoad();
        check(ToriRSServer_OAuthPasswordGrant("alice", "correcthorse9", NULL, now, &tokens, &why) ==
                  TORIRSSERVER_OAUTH_OK,
              "oauth: the password grant signs alice in");
        check(ToriRSServer_OAuthPlay(tokens.access_token, now, otl, name, sizeof(name)) ==
                  TORIRSSERVER_OAUTH_OK &&
                  strcmp(name, "alice") == 0,
              "oauth: play trades her access token for a one-time login");
        peer_begin(&otl_peer, ToriRSServer_EmbedConnect(embed));
        peer_dial_token(&otl_peer, "alice", otl);
        {
            struct Peer* peers[] = { &otl_peer };
            settle(peers, 1, embed);
        }
        check(otl_peer.net.state == TORIRS_NET_GAME, "oauth: the one-time login logs her in");
        ToriRSServer_EmbedDisconnect(embed, otl_peer.client_id);
        login_token_once(embed, &reused, "alice", otl);
        check(reused.net.state != TORIRS_NET_GAME && reused.net.login_reply == 3,
              "oauth: and is spent: the same token again is refused (3)");
        check(ToriRSServer_OAuthPlay(tokens.access_token, now, otl, name, sizeof(name)) ==
                  TORIRSSERVER_OAUTH_OK,
              "oauth: (another one-time login)");
        login_token_once(embed, &wrong_name, "bob", otl);
        check(wrong_name.net.state != TORIRS_NET_GAME,
              "oauth: a one-time login is for its own account only");
        reused_client = reused.client_id;
        wrong_name_client = wrong_name.client_id;
    }

    /* 5d. A linked account: its password is refused at the game's own login
     *     (3, like any refusal), and its owner's one-time login is not. */
    {
        static struct Peer by_password, by_owner;
        char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];
        char otl[TORIRSSERVER_OAUTH_ID_HEX + 1];
        char name[64];
        long long now = (long long)time(NULL);

        remove(ToriRSServer_AccountPath("henry"));
        check(ToriRSServer_AuthLogin("henry", "red8fox99") == TORIRSSERVER_AUTH_CLAIMED,
              "(henry makes his account)");
        check(ToriRSServer_AuthLink("henry", "red8fox99", "matt", token) == TORIRSSERVER_LINK_OK,
              "(link henry to matt)");
        login(embed, &by_password, NULL, "henry", "red8fox99");
        check(by_password.net.state != TORIRS_NET_GAME && by_password.net.login_reply == 3,
              "a linked account's password is refused at login (3)");
        /* The embed has five client slots and 5c's refusals still hold two. */
        ToriRSServer_EmbedDisconnect(embed, by_password.client_id);
        ToriRSServer_EmbedDisconnect(embed, reused_client);
        ToriRSServer_EmbedDisconnect(embed, wrong_name_client);
        check(ToriRSServer_OAuthUserPlay("matt", "henry", token, now, otl, name, sizeof(name)) ==
                  TORIRSSERVER_OAUTH_OK,
              "(matt's session mints henry a one-time login)");
        peer_begin(&by_owner, ToriRSServer_EmbedConnect(embed));
        peer_dial_token(&by_owner, "henry", otl);
        {
            struct Peer* peers[] = { &by_owner };
            settle(peers, 1, embed);
        }
        check(by_owner.net.state == TORIRS_NET_GAME, "and that logs him in");
        ToriRSServer_EmbedDisconnect(embed, by_owner.client_id);
        {
            struct AccountPlayer henry;

            /* This client never reports its map built, so the world never
             * finishes the login: the session must leave nothing in the
             * account database, above all not a logout of zeroed stats. */
            long long henry_seq = 1;

            check(world->account_log->count == 0 || send_log(world->account_log, &henry_seq, NULL) >= 0,
                  "(whatever the world queued is sent)");
            check(!AccountDb_Player("henry", &henry),
                  "a session dropped before its login finished records nothing");
        }
    }

    ToriRSServer_AccountLogFree(world->account_log);
    world->account_log = NULL;
    ToriRSServer_EmbedStop(embed);

    /* 6. The record, without the world: the stored password survived dev
     *    mode, a damaged record refuses rather than reopening the name, and an
     *    unusable name or empty password is refused outright. */
    check(ToriRSServer_AuthLogin("alice", "correcthorse9") == TORIRSSERVER_AUTH_OK,
          "dev mode left her password as it was");
    check(ToriRSServer_AuthLogin("alice", "anything") == TORIRSSERVER_AUTH_REFUSED,
          "and did not adopt the one it let in");
    check(ToriRSServer_AuthLogin("Alice", "correcthorse9") == TORIRSSERVER_AUTH_OK,
          "the name is keyed as her save is (case folded)");
    check(ToriRSServer_AuthLogin("...", "x") == TORIRSSERVER_AUTH_UNUSABLE, "an unusable name is refused");
    check(ToriRSServer_AuthLogin("alice", "") == TORIRSSERVER_AUTH_UNUSABLE, "an empty password is refused");
    check(ToriRSServer_SaveWriteText(account, "[account]\npassword = garbage\n"),
          "(damage her record)");
    check(ToriRSServer_AuthLogin("alice", "correcthorse9") == TORIRSSERVER_AUTH_REFUSED,
          "a damaged record refuses even the right password");
    check(ToriRSServer_SaveWriteText(account, "; nothing here\n"), "(empty her record)");
    check(ToriRSServer_AuthLogin("alice", "newpass12") == TORIRSSERVER_AUTH_REFUSED,
          "a record with no password does not let the name be claimed");
    remove(account);
    check(ToriRSServer_AuthLogin("alice", "new") == TORIRSSERVER_AUTH_WEAK,
          "a claim is held to the password rules");
    check(!file_exists(account), "and a refused claim writes nothing");
    check(ToriRSServer_AuthLogin("alice", "newpass12") == TORIRSSERVER_AUTH_CLAIMED,
          "deleting the record resets it: the next login claims");
    check(ToriRSServer_AuthLogin("alice", "newpass12") == TORIRSSERVER_AUTH_OK,
          "with the new password");

    check_rules();
    check_invites();
    check_tickets();
    check_oauth();
    check_links();
    check_accounts();
    check_display_names();
    check_spool();
    check_link_live();
    check_operator_accounts();

    if( g_failures )
    {
        printf("login-password: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("login-password: all checks passed\n");
    return 0;
}
