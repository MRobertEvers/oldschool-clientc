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
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server_oauth.h"
#include "torirsserver/torirs_server_save.h"
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

    remove(ToriRSServer_GateAccountPath("dave"));
    remove(ToriRSServer_GateAccountPath("erin"));
    remove(ToriRSServer_AccountPath("dave"));

    check(ToriRSServer_GateLogin("dave", "davespass1") == TORIRSSERVER_AUTH_REFUSED,
          "the gate's login does not claim a name with no record");
    check(!file_exists(ToriRSServer_GateAccountPath("dave")), "and writes nothing");

    check(ToriRSServer_InviteCreate("Dave", named), "an invite for Dave");
    check(strlen(named) == TORIRSSERVER_INVITE_CODE_BYTES * 2, "its code is 32 hex digits");
    check(ToriRSServer_InviteLookup(named, who, sizeof(who)) && strcmp(who, "Dave") == 0,
          "it names Dave");
    check(ToriRSServer_InviteRedeem(named, "erin", "erinspass1", &problem) ==
              TORIRSSERVER_INVITE_WRONG_NAME,
          "another name cannot redeem it");
    check(ToriRSServer_InviteRedeem(named, "dave", "short", &problem) == TORIRSSERVER_INVITE_WEAK &&
              problem,
          "a weak password is refused, with the rule");
    check(ToriRSServer_InviteRedeem(named, "dave", "secret1234", &problem) == TORIRSSERVER_INVITE_OK,
          "Dave redeems it (the name matches as the account is keyed)");
    check(ToriRSServer_GateLogin("Dave", "secret1234") == TORIRSSERVER_AUTH_OK,
          "his web login has his password");
    check(!file_exists(ToriRSServer_AccountPath("dave")), "and it is not a player account");
    check(!ToriRSServer_InviteLookup(named, who, sizeof(who)), "the invite is spent");
    check(ToriRSServer_InviteRedeem(named, "dave", "secret1234", &problem) ==
              TORIRSSERVER_INVITE_UNKNOWN,
          "and cannot be redeemed twice");

    check(ToriRSServer_InviteCreate(NULL, open_code), "an invite for any name");
    check(ToriRSServer_InviteLookup(open_code, who, sizeof(who)) && who[0] == '\0',
          "names nobody");
    check(ToriRSServer_InviteRedeem(open_code, "DAVE", "another12", &problem) ==
              TORIRSSERVER_INVITE_NAME_TAKEN,
          "a taken name is refused");
    check(ToriRSServer_InviteRedeem(open_code, "../x", "another12", &problem) ==
              TORIRSSERVER_INVITE_BAD_NAME,
          "a bad name is refused");
    check(ToriRSServer_InviteRedeem(open_code, "erin", "another12", &problem) == TORIRSSERVER_INVITE_OK,
          "Erin redeems it");
    check(ToriRSServer_InviteRedeem("../../etc/passwd", "erin", "another12", &problem) ==
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

    check_vectors();

    setenv("TORIRSSERVER_REV", "osrs239", 1);
    setenv("TORIRSSERVER_SAVES", "build/login_password_test_saves", 1);
    snprintf(account, sizeof(account), "%s", ToriRSServer_AccountPath("alice"));
    remove(account);
    remove(ToriRSServer_SavePath("alice"));
    remove(ToriRSServer_AccountPath("carol"));

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
     *     cheats on a server that checks passwords. Without it, no cheats --
     *     except in development mode, where a name proves nothing. */
    check(ToriRSServer_AuthSetAdmin("alice", 1), "(make alice an admin)");
    check(ToriRSServer_AuthIsAdmin("alice"), "the record says so");
    check(ToriRSServer_AuthLogin("alice", "correcthorse9") == TORIRSSERVER_AUTH_OK,
          "and setting it kept her password");
    login(embed, &staff, NULL, "alice", "correcthorse9");
    {
        struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, staff.client_id);

        check(player && player->staff_level == 2, "an admin account logs in at staff level 2");
        check(player && ToriRSServer_PlayerMayCheat(world, player), "and may cheat");
    }
    ToriRSServer_EmbedDisconnect(embed, staff.client_id);
    check(ToriRSServer_AuthSetAdmin("alice", 0), "(and not an admin)");
    check(!ToriRSServer_AuthIsAdmin("alice"), "the record says so");
    login(embed, &plain, NULL, "alice", "correcthorse9");
    {
        struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, plain.client_id);

        check(player && player->staff_level == 0, "a plain account logs in at staff level 0");
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
    }

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

    if( g_failures )
    {
        printf("login-password: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("login-password: all checks passed\n");
    return 0;
}
