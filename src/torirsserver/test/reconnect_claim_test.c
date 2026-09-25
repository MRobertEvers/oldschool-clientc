/*
 * Revision 239: a client that comes back gets its character back -- once.
 *
 * A reloaded browser tab used to send a fresh GAMELOGIN, and the server seated
 * it as a second player of the same name next to the first, loading a stale
 * save or none: the player appeared as a brand-new character. Now the page
 * hands the new boot the old session's resume token and the client sends
 * GAMERECONNECT presenting that session's cipher seed
 * (ToriRS_Network_ArmResume); the server hands the character back in the slot
 * the client already holds, and only to that key (torirs_server_claim.c).
 *
 * Everything is driven through the real client login machine and the real
 * embedded server, exactly as login_index_test.c is, because both ends had
 * to change and a test of either alone would pass against the other's bug.
 *
 * Run: make -C src test-torirsserver-reconnect-claim
 */

#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server_session.h"
#include "torirsserver/torirs_server_wire.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"

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
    printf("reconnect-claim: %-62s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

struct Peer
{
    struct ToriRS_Network net;
    int client_id;
    /** The index the client believes is itself, as UPDATE_PID stated it. */
    int local_index;
    /** The server closed this client's session. */
    int dropped;
};

static int32_t g_seed_next[4];

/* Every session gets its own seed, stated by the test, so a server that
 * compared the wrong pair of keys cannot pass by coincidence. */
static void
seed_from_next(
    void* user,
    int32_t* seed)
{
    (void)user;
    memcpy(seed, g_seed_next, sizeof(g_seed_next));
}

static void
peer_begin(
    struct Peer* peer,
    int client_id,
    int32_t first_word)
{
    memset(peer, 0, sizeof(*peer));
    peer->client_id = client_id;
    peer->local_index = -1;
    ToriRS_Network_Init(&peer->net, GameProtoRev_OSRS239(), TORIRSSERVER_RSA_PUBLIC_EXPONENT,
                        TORIRSSERVER_RSA_PUBLIC_MODULUS);
    g_seed_next[0] = first_word;
    g_seed_next[1] = first_word ^ 0x5a5a5a5a;
    g_seed_next[2] = first_word + 0x01010101;
    g_seed_next[3] = ~first_word;
    ToriRS_Network_SetSeedFn(&peer->net, seed_from_next, NULL);
}

static void
peer_dial(
    struct Peer* peer,
    const char* name)
{
    uint8_t status = TORIRS_NET_STATUS_CONNECTED;

    ToriRS_Network_ConnectLogin(&peer->net, "embedded:0", name, name);
    ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, &status, 1);
}

/* One round of each side's output into the other's input. A session the
 * server killed is reported to its client as the real transport would. */
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
            if( packet.packet_type == PKT_NAME_UPDATE_PID )
                peer->local_index = packet._update_pid.local_player_index;
        /* A zero-length write answers -1 once the server has killed the session. */
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

int
main(void)
{
    static struct Peer first, back, thief, returned, again;
    struct ToriRSServerEmbed* embed;
    struct ToriRSServer* world;
    struct ToriRSServerPlayer* player;
    int32_t key[4];
    int pid;
    int index;
    int moved_x;

    setenv("TORIRSSERVER_REV", "osrs239", 1);
    setenv("TORIRSSERVER_SAVES", "build/reconnect_claim_test_saves", 1);
    remove("build/reconnect_claim_test_saves/alice.ini");

    embed = ToriRSServer_EmbedStart(NULL);
    if( !embed )
    {
        fprintf(stderr, "reconnect-claim: could not start the server\n");
        return 1;
    }

    /* 1. alice logs in the ordinary way. */
    peer_begin(&first, 0, 0x11111111);
    peer_dial(&first, "alice");
    {
        struct Peer* peers[] = { &first };
        settle(peers, 1, embed);
    }
    world = ToriRSServer_EmbedWorld(embed);
    player = ToriRSServer_EmbedPlayer(embed, first.client_id);
    check(first.net.state == TORIRS_NET_GAME && world && player, "alice logged in");
    if( !world || !player )
        return 1;
    pid = player->pid;
    index = first.local_index;
    memcpy(key, first.net.prev_seed, sizeof(key));
    check(first.net.has_prev_seed, "the session left a key to come back with");

    /*
     * 2. The page reloads while the server still has her: the old session is
     *    alive as far as it knows. The new boot presents the old key.
     */
    peer_begin(&back, ToriRSServer_EmbedConnect(embed), 0x22222222);
    ToriRS_Network_ArmResume(&back.net, key, index);
    peer_dial(&back, "alice");
    {
        struct Peer* peers[] = { &first, &back };
        settle(peers, 2, embed);
    }
    player = ToriRSServer_EmbedPlayer(embed, back.client_id);
    check(back.net.state == TORIRS_NET_GAME, "the reload's reconnect was accepted");
    check(player && player->pid == pid, "she came back in the slot she had");
    check(back.local_index == index, "and her client kept the index that slot is");
    check(first.dropped, "the stale session was closed");
    check(count_active(world, "alice") == 1, "and there is ONE alice, not two");

    /* 3. Someone presenting a key that is not the live session's is refused,
     *    and the client learns so once -- the cue to use the password. */
    {
        int32_t wrong[4] = { 1, 2, 3, 4 };

        peer_begin(&thief, ToriRSServer_EmbedConnect(embed), 0x33333333);
        ToriRS_Network_ArmResume(&thief.net, wrong, index);
        peer_dial(&thief, "alice");
        {
            struct Peer* peers[] = { &back, &thief };
            settle(peers, 2, embed);
        }
        check(thief.net.state == TORIRS_NET_DISCONNECTED, "a reconnect with the wrong key is refused");
        check(ToriRS_Network_TakeResumeRefused(&thief.net), "the client is told its resume failed");
        check(!ToriRS_Network_TakeResumeRefused(&thief.net), "once");
        check(!thief.net.has_prev_seed, "and drops the dead key");
        check(!back.dropped && ToriRSServer_EmbedPlayer(embed, back.client_id),
              "the live session was not disturbed");
    }

    /*
     * 4. The ordinary reload: the old socket HAS closed and the character was
     *    saved before the new boot dials. The key still gets the slot back.
     */
    player = ToriRSServer_EmbedPlayer(embed, back.client_id);
    moved_x = player->x + 2;
    player->x = moved_x;
    memcpy(key, back.net.prev_seed, sizeof(key));
    ToriRSServer_EmbedDisconnect(embed, back.client_id);
    check(count_active(world, "alice") == 0, "her session ended and she left the world");

    peer_begin(&returned, ToriRSServer_EmbedConnect(embed), 0x44444444);
    ToriRS_Network_ArmResume(&returned.net, key, index);
    peer_dial(&returned, "alice");
    {
        struct Peer* peers[] = { &returned };
        settle(peers, 1, embed);
    }
    player = ToriRSServer_EmbedPlayer(embed, returned.client_id);
    check(returned.net.state == TORIRS_NET_GAME, "a reconnect after the logout was accepted");
    check(player && player->pid == pid, "into the slot her client still holds");
    check(player && player->x == moved_x, "carrying what the save wrote, not a new character");

    /* 5. A fresh login of a character that is online takes it over rather than
     *    seating a second one; the old session is saved first. */
    player->x = moved_x + 1;
    peer_begin(&again, ToriRSServer_EmbedConnect(embed), 0x55555555);
    peer_dial(&again, "alice");
    {
        struct Peer* peers[] = { &returned, &again };
        settle(peers, 2, embed);
    }
    player = ToriRSServer_EmbedPlayer(embed, again.client_id);
    check(again.net.state == TORIRS_NET_GAME, "a second login of an online name is accepted");
    check(returned.dropped, "and closes the session it replaces");
    check(count_active(world, "alice") == 1, "leaving one alice");
    check(player && player->x == moved_x + 1, "who is the character the old session saved");

    ToriRSServer_EmbedStop(embed);

    if( g_failures )
    {
        printf("reconnect-claim: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("reconnect-claim: all checks passed\n");
    return 0;
}
