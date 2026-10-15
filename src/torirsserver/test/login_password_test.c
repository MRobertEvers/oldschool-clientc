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
#include "torirsserver/torirs_server_save.h"
#include "torirsserver/torirs_server_session.h"
#include "torirsserver/torirs_server_wire.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int
main(void)
{
    static struct Peer first, wrong, right, intruder, dev;
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
    login(embed, &first, NULL, "alice", "correct horse");
    check(first.net.state == TORIRS_NET_GAME, "alice's first login is accepted");
    check(file_exists(account), "and writes her account record");
    ToriRSServer_EmbedDisconnect(embed, first.client_id);
    check(count_active(world, "alice") == 0, "she logs out");

    /* 2. The wrong password is refused, as response 3. */
    login(embed, &wrong, NULL, "alice", "battery staple");
    check(wrong.net.state != TORIRS_NET_GAME, "a wrong password is refused");
    check(wrong.net.login_reply == 3, "with 'invalid username or password' (3)");
    check(count_active(world, "alice") == 0, "and seats nobody");

    /* 3. The right one still works. */
    login(embed, &right, NULL, "alice", "correct horse");
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

    ToriRSServer_EmbedStop(embed);

    /* 6. The record, without the world: the stored password survived dev
     *    mode, a damaged record refuses rather than reopening the name, and an
     *    unusable name or empty password is refused outright. */
    check(ToriRSServer_AuthLogin("alice", "correct horse") == TORIRSSERVER_AUTH_OK,
          "dev mode left her password as it was");
    check(ToriRSServer_AuthLogin("alice", "anything") == TORIRSSERVER_AUTH_REFUSED,
          "and did not adopt the one it let in");
    check(ToriRSServer_AuthLogin("Alice", "correct horse") == TORIRSSERVER_AUTH_OK,
          "the name is keyed as her save is (case folded)");
    check(ToriRSServer_AuthLogin("...", "x") == TORIRSSERVER_AUTH_UNUSABLE, "an unusable name is refused");
    check(ToriRSServer_AuthLogin("alice", "") == TORIRSSERVER_AUTH_UNUSABLE, "an empty password is refused");
    check(ToriRSServer_SaveWriteText(account, "[account]\npassword = garbage\n"),
          "(damage her record)");
    check(ToriRSServer_AuthLogin("alice", "correct horse") == TORIRSSERVER_AUTH_REFUSED,
          "a damaged record refuses even the right password");
    check(ToriRSServer_SaveWriteText(account, "; nothing here\n"), "(empty her record)");
    check(ToriRSServer_AuthLogin("alice", "new") == TORIRSSERVER_AUTH_REFUSED,
          "a record with no password does not let the name be claimed");
    remove(account);
    check(ToriRSServer_AuthLogin("alice", "new") == TORIRSSERVER_AUTH_CLAIMED,
          "deleting the record resets it: the next login claims");
    check(ToriRSServer_AuthLogin("alice", "new") == TORIRSSERVER_AUTH_OK, "with the new password");

    if( g_failures )
    {
        printf("login-password: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("login-password: all checks passed\n");
    return 0;
}
