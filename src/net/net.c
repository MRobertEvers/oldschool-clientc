#include "net.h"

#include "cmd/cmdbus.h"
#include "rev/gameproto_parse.h"
#include "rev/rsprot_bridge.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* --- login-driver dispatch: rev->login vtable (xrsps) vs classic loginproto.c.
 * Every login touch point below goes through these so the drive/drain loops are
 * generation-blind. */

static int
login_is_active(struct ToriRS_Network* net)
{
    return net->rev->login ? net->login_generic != NULL : net->loginproto != NULL;
}

static int
login_poll(struct ToriRS_Network* net)
{
    return net->rev->login ? net->rev->login->poll(net->login_generic)
                           : loginproto_poll(net->loginproto);
}

static int
login_send(struct ToriRS_Network* net, uint8_t* out, int cap)
{
    return net->rev->login ? net->rev->login->send(net->login_generic, out, cap)
                           : loginproto_send(net->loginproto, out, cap);
}

static int
login_recv(struct ToriRS_Network* net, uint8_t const* data, int size)
{
    return net->rev->login ? net->rev->login->recv(net->login_generic, data, size)
                           : loginproto_recv(net->loginproto, (uint8_t*)data, size);
}

static void
login_free(struct ToriRS_Network* net)
{
    if( net->rev->login )
    {
        if( net->login_generic )
            net->rev->login->free_(net->login_generic);
        net->login_generic = NULL;
    }
    else if( net->loginproto )
    {
        loginproto_free(net->loginproto);
        net->loginproto = NULL;
    }
}

void
ToriRS_Network_Init(
    struct ToriRS_Network* net,
    struct GameProtoRevTable const* rev,
    char const* rsa_exp_hex,
    char const* rsa_mod_hex)
{
    assert(net && rev);
    memset(net, 0, sizeof(*net));
    net->state = TORIRS_NET_DISCONNECTED;
    net->rev = rev;
    net->random_in = isaac_new(NULL, 0);
    net->random_out = isaac_new(NULL, 0);
    net->conn_status = TORIRS_NET_STATUS_DISCONNECTED;
    net->local_index = -1;
    CmdRing_Init(&net->out);

    if( rsa_exp_hex && rsa_mod_hex )
        net->rsa_ready = (rsa_init(&net->rsa, rsa_exp_hex, rsa_mod_hex) == 0);
}

void
ToriRS_Network_SetSeedFn(
    struct ToriRS_Network* net,
    loginproto_seed_fn seed_fn,
    void* user)
{
    assert(net);
    net->seed_fn = seed_fn;
    net->seed_user = user;
    if( net->loginproto )
        loginproto_set_seed_fn(net->loginproto, seed_fn, user);
}

static void
free_packet_list(struct ToriRS_Network* net)
{
    struct RevPacketItem* item = net->packets_head;
    while( item )
    {
        struct RevPacketItem* next = item->next_nullable;
        gameproto_free(&item->packet);
        free(item);
        item = next;
    }
    net->packets_head = NULL;
    net->packets_tail = NULL;
}

void
ToriRS_Network_Free(struct ToriRS_Network* net)
{
    if( !net )
        return;
    login_free(net);
    if( net->packet_buffer.data )
        packetbuffer_reset(&net->packet_buffer);
    free_packet_list(net);
    if( net->random_in )
        isaac_free(net->random_in);
    if( net->random_out )
        isaac_free(net->random_out);
    net->random_in = NULL;
    net->random_out = NULL;
}

static void
loginproto_drive(struct ToriRS_Network* net);

static void
push_out(
    struct ToriRS_Network* net,
    uint32_t type,
    uint8_t const* data,
    int len)
{
    CmdRing_Push(&net->out, type, data, (uint16_t)len);
}

static void
connect_login(
    struct ToriRS_Network* net,
    char const* host,
    char const* username,
    char const* password,
    int reconnect)
{
    assert(net);

    net->reconnect = reconnect;
    strncpy(net->host, host ? host : "", sizeof(net->host) - 1);
    strncpy(net->username, username ? username : "", sizeof(net->username) - 1);
    strncpy(net->password, password ? password : "", sizeof(net->password) - 1);

    login_free(net);

    if( net->rev->login )
    {
        /* Generation-specific handshake (xrsps: plaintext welcome/hello/login). */
        net->login_generic = net->rev->login->new_(net, net->username, net->password);
        assert(net->login_generic);
    }
    else
    {
        net->loginproto = loginproto_new(
            net->random_in, net->random_out, &net->rsa, net->rev, net->username, net->password);
        assert(net->loginproto);
        if( net->seed_fn )
            loginproto_set_seed_fn(net->loginproto, net->seed_fn, net->seed_user);
        /* Whether the opcode actually changes is the revision's call, not this
         * one's -- see rev->reconnect_kind. */
        loginproto_set_reconnect(net->loginproto, net->reconnect);
    }

    net->state = TORIRS_NET_LOGIN;
    push_out(net, TORIRS_NET_OUT_CONNECT, (uint8_t const*)net->host, (int)strlen(net->host));

    /* Run SEND_CONNECT now (v0 drove the login machine from game_poll before
     * the first drain): emits the connect opcode and arms await_recv_cnt so
     * the first inbound bytes are consumed rather than dropped. */
    loginproto_drive(net);
}

int
ToriRS_Network_Reconnect(struct ToriRS_Network* net)
{
    assert(net);
    if( !net->host[0] )
        return 0;

    /* Drop whatever is left of the dead session first: a half-read frame in
     * the packet buffer would otherwise be prefixed onto the new stream, and
     * the parsed FIFO holds packets addressed to a world that is about to be
     * rebuilt. */
    login_free(net);
    if( net->packet_buffer.data )
        packetbuffer_reset(&net->packet_buffer);
    free_packet_list(net);

    /* Copies, because ConnectLogin writes these same fields — strncpy onto
     * its own source is not a thing to rely on. */
    {
        char host[sizeof(net->host)];
        char user[sizeof(net->username)];
        char pass[sizeof(net->password)];

        memcpy(host, net->host, sizeof(host));
        memcpy(user, net->username, sizeof(user));
        memcpy(pass, net->password, sizeof(pass));
        connect_login(net, host, user, pass, /* reconnect */ 1);
    }
    return 1;
}

void
ToriRS_Network_ConnectLogin(
    struct ToriRS_Network* net,
    char const* host,
    char const* username,
    char const* password)
{
    connect_login(net, host, username, password, /* reconnect */ 0);
}

void
ToriRS_Network_Logout(struct ToriRS_Network* net)
{
    assert(net);
    /* Tell the transport the connection is over. The peer's FIN-driven
     * bookkeeping (a server that persists a character on disconnect) has to
     * run before anything re-establishes the session. */
    push_out(net, TORIRS_NET_OUT_DISCONNECT, NULL, 0);
    login_free(net);
    if( net->packet_buffer.data )
        packetbuffer_reset(&net->packet_buffer);
    free_packet_list(net);
    net->state = TORIRS_NET_DISCONNECTED;
}

void
ToriRS_Network_SendRaw(
    struct ToriRS_Network* net,
    uint8_t const* data,
    int len)
{
    assert(net);
    if( len > 0 && data )
    {
        if( getenv("TORIRS_NET_DEBUG") )
            TORIRS_LOG("net: -> %d bytes (first 0x%02x)\n", len, data[0]);
        push_out(net, TORIRS_NET_OUT_SEND_DATA, data, len);
    }
}

static void
push_parsed_packet(
    struct ToriRS_Network* net,
    struct RevPacket const* packet);

/* osrs239_entity_info.c -- see the local-index note in loginproto_drive. */
void
osrs239_playerinfo_set_local(int local_index);
void
osrs239_playerinfo_init(uint8_t const* data, int len);

/* Flush login outbound bytes, then act on the poll result (port of v0
 * loginproto_drive): on success free the login machine and arm the packet
 * buffer for the game stream — fixing v0's missing packetbuffer_init. */
static void
loginproto_drive(struct ToriRS_Network* net)
{
    uint8_t scratch[4096];
    int poll_result;
    int bytes;

    if( !login_is_active(net) )
        return;

    poll_result = login_poll(net);

    while( (bytes = login_send(net, scratch, sizeof(scratch))) > 0 )
        ToriRS_Network_SendRaw(net, scratch, bytes);

    if( poll_result == LOGINPROTO_SUCCESS )
    {
        /*
         * The local player's index, before the login handle is freed.
         *
         * Revision 239 deleted UPDATE_PID: LoginResponse.Ok carries the index
         * and nothing else on the wire ever restates it. Everything downstream
         * already keys on `esync.local_pid`, which UPDATE_PID sets, so the
         * handshake states it as one — rather than a second path into the same
         * field that could disagree with the first. A revision whose vtable
         * leaves `local_index` NULL keeps using the packet.
         *
         * Without this the client has no local player at all: PLAYER_INFO v5 is
         * keyed on the index, so every high-resolution record lands on a slot
         * the client does not believe is itself.
         */
        if( net->rev->login && net->rev->login->local_index )
        {
            int index = net->rev->login->local_index(net->login_generic);

            if( index >= 0 )
            {
                struct RevPacket packet;

                /* The v5 player stream is keyed on this index and its init
                 * block skips exactly this slot, so the entity reader has to
                 * know it before the login REBUILD arrives. */
                if( net->rev->player_info_read )
                    osrs239_playerinfo_set_local(index);

                memset(&packet, 0, sizeof(packet));
                packet.packet_type = PKT_NAME_UPDATE_PID;
                packet._update_pid.local_player_index = index;
                push_parsed_packet(net, &packet);
            }
        }
        /*
         * A reconnect's player table, after the index and before the handle
         * that holds the block is freed.
         *
         * Order is the whole of it: osrs239_playerinfo_set_local wipes the
         * table to install the index, so seeding first would seed nothing.
         * On a fresh login there is no block here — REBUILD_LOGIN carries it
         * instead — and the hook returns NULL.
         */
        if( net->rev->login && net->rev->login->reconnect_block )
        {
            int block_len = 0;
            uint8_t const* block = net->rev->login->reconnect_block(net->login_generic,
                                                                    &block_len);

            if( block && block_len > 0 && net->rev->player_info_read )
                osrs239_playerinfo_init(block, block_len);
        }
        net->reconnect = 0;
        login_free(net);
        packetbuffer_init(&net->packet_buffer, net->random_in, net->rev);
        net->state = TORIRS_NET_GAME;
    }
    else if( poll_result == LOGINPROTO_ERROR )
    {
        /* Carried out of the protocol before its state is dropped: the screen
         * that has to explain this reads it after the transition. */
        if( net->loginproto )
            net->login_reply = net->loginproto->reply_code;
        net->reconnect = 0;
        net->state = TORIRS_NET_DISCONNECTED;
    }
}

static int
loginproto_drain(
    struct ToriRS_Network* net,
    uint8_t const* buffer,
    int size)
{
    int remaining = size;
    int consumed;

    do
    {
        consumed = login_recv(net, buffer, remaining);
        buffer += consumed;
        remaining -= consumed;
        loginproto_drive(net);
    } while( consumed > 0 && remaining > 0 && net->state == TORIRS_NET_LOGIN );

    return size - remaining;
}

static void
push_parsed_packet(
    struct ToriRS_Network* net,
    struct RevPacket const* packet)
{
    struct RevPacketItem* item = malloc(sizeof(*item));
    assert(item);
    item->packet = *packet;
    item->next_nullable = NULL;
    if( net->packets_tail )
        net->packets_tail->next_nullable = item;
    else
        net->packets_head = item;
    net->packets_tail = item;
}

static void
net_process_packets(struct ToriRS_Network* net)
{
    if( !packetbuffer_ready(&net->packet_buffer) )
        return;

    {
        struct RevPacket packet;
        int wire = packetbuffer_packet_type(&net->packet_buffer);
        enum GameProtoPktName name = (enum GameProtoPktName)net->rev->packetin_code(wire);

        memset(&packet, 0, sizeof(packet));
        if( getenv("TORIRS_NET_DEBUG") )
            TORIRS_LOG("net: <- wire=%d name=%d size=%d\n",
                wire,
                (int)name,
                packetbuffer_size(&net->packet_buffer));
        if( name == PKT_NAME_NONE )
        {
            /* Both "not in the rev table" and "in it with no decoder" land here
             * — the framer already sized the packet either way, so the drop is
             * clean. Once per opcode is enough to notice it: SERVER_TICK_END
             * alone would otherwise print every tick, which buries the arrival
             * of a packet actually worth wiring up. */
            static uint8_t warned[256];

            if( wire >= 0 && wire < 256 && !warned[wire] )
            {
                warned[wire] = 1;
                TORIRS_LOG("net: no decoder for wire opcode %d (%s) — dropped\n",
                    wire, net->rev->name);
            }
        }
        else
        {
            /*
             * Three parsers, tried in order, each returning <0 for "not mine".
             *
             *   1. rsprot   — the generated per-layout codecs in
             *                 3rd/rsprot/packets/, selected by (packet,
             *                 revision). This is where packets are migrating
             *                 TO; see net/rev/rsprot_bridge.h.
             *   2. rev->parse — the hand-written per-revision overrides
             *                 (osrs239_parse.c, osrs230_parse.c).
             *   3. gameproto_parse — the shared lc-style parser.
             *
             * rsprot goes first so that moving a packet across is adding one
             * row to the bridge table, with the arm it supersedes becoming
             * dead code rather than needing to be deleted in the same step.
             * The order also means a disagreement surfaces as rsprot's answer
             * winning, not as two parsers both running.
             */
            uint8_t* pdata = packetbuffer_data(&net->packet_buffer);
            int psize = packetbuffer_size(&net->packet_buffer);
            int parsed = -1;
            /* Set the canonical name up front so a rev->parse override need not
             * (the shared gameproto_parse also sets it). */
            packet.packet_type = name;
            parsed = rsprot_bridge_parse(net->rev, name, pdata, psize, &packet);
            if( parsed < 0 && net->rev->parse )
                parsed = net->rev->parse(net->rev, name, pdata, psize, &packet);
            if( parsed < 0 )
                parsed = gameproto_parse(net->rev, name, pdata, psize, &packet);
            if( parsed > 0 )
            {
                /* Owned payloads transfer to the queued copy; the stack one
                 * is abandoned here and never read again. */
                push_parsed_packet(net, &packet);
            }
            else
            {
                /* A parser that allocated before deciding the packet was
                 * malformed -- or not its own -- still owns what it
                 * allocated, and nothing downstream will ever see this
                 * packet to free it. */
                gameproto_free(&packet);
            }
        }
    }
    packetbuffer_reset(&net->packet_buffer);
}

static int
gameproto_drain(
    struct ToriRS_Network* net,
    uint8_t const* data,
    int size)
{
    int remaining = size;
    int consumed;

    do
    {
        consumed = packetbuffer_read(&net->packet_buffer, (uint8_t*)data, remaining);
        data += consumed;
        remaining -= consumed;
        net_process_packets(net);
    } while( consumed > 0 && remaining > 0 );

    return size - remaining;
}

static void
net_drain(
    struct ToriRS_Network* net,
    uint8_t const* data,
    int size)
{
    int remaining = size;
    int consumed;

    do
    {
        consumed = 0;
        switch( net->state )
        {
        case TORIRS_NET_LOGIN:
            consumed = loginproto_drain(net, data, remaining);
            break;
        case TORIRS_NET_GAME:
            consumed = gameproto_drain(net, data, remaining);
            break;
        case TORIRS_NET_DISCONNECTED:
            break;
        }
        data += consumed;
        remaining -= consumed;
    } while( consumed > 0 && remaining > 0 );
}

void
ToriRS_Network_HandleCmd(
    struct ToriRS_Network* net,
    uint32_t cmd_type,
    uint8_t const* data,
    int len)
{
    assert(net);

    switch( cmd_type )
    {
    case TORIRS_CMD_NET_RECV:
        if( len > 0 && data )
            net_drain(net, data, len);
        break;
    case TORIRS_CMD_NET_STATUS:
        if( len >= (int)sizeof(int32_t) )
        {
            int32_t status;
            memcpy(&status, data, sizeof(status));
            net->conn_status = status;
            if( status == TORIRS_NET_STATUS_DISCONNECTED ||
                status == TORIRS_NET_STATUS_FAILED )
                net->state = TORIRS_NET_DISCONNECTED;
        }
        break;
    case TORIRS_CMD_NET_CONNECT:
        /* Producer-only echo; ConnectLogin drives the real transition. */
        break;
    default:
        break;
    }
}

int
ToriRS_Network_PopPacket(
    struct ToriRS_Network* net,
    struct RevPacket* out)
{
    struct RevPacketItem* item;

    assert(net && out);
    item = net->packets_head;
    if( !item )
        return 0;

    net->packets_head = item->next_nullable;
    if( !net->packets_head )
        net->packets_tail = NULL;

    *out = item->packet; /* heap fields transfer to the caller */
    free(item);
    return 1;
}

int
ToriRS_Network_PopOut(
    struct ToriRS_Network* net,
    struct ToriRS_CmdHeader* header,
    uint8_t* payload)
{
    assert(net && header && payload);
    return CmdRing_Pop(&net->out, header, payload);
}
