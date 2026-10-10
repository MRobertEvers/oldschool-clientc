/*
 * The in-process server. See torirs_server_embed.h.
 *
 * This file is short on purpose. Everything an embedded server needs already
 * existed once the session stopped blocking; what is here is the plumbing that
 * points a session at two byte queues instead of a socket, plus the pump the
 * host drives in place of a select() loop.
 *
 * One world, N connections. The connection array is what a socket server gets
 * from accept(); here the host asks for one with ToriRSServer_EmbedConnect, and the
 * two differ only in where the bytes come from — the login sequence below is
 * character for character the socket server's.
 */

#include "torirs_server_embed.h"
#include <assert.h>

#include "torirs_server.h"
#include "torirs_server_bank.h"
#include "torirs_server_container.h"
#include "torirs_server_shop.h"
#include "torirs_server_boot.h"
#include "torirs_server_claim.h"
#include "torirs_server_session.h"
#include "torirs_server_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The party link (torirs_server_embed.h) is a loopback socket: native POSIX
 * hosts only. Everything else keeps the in-process embed and aborts if a
 * party is asked for. */
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(TORIRS_PLATFORM_WEB)
#define EMBED_PARTY_SOCKETS 1
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#define EMBED_PARTY_SOCKETS 0
#endif

struct ToriRSServerEmbedClient
{
    int open;
    struct ToriRSServerSession session;
    struct ToriRSServerTransport transport;

    /* From the server's point of view: it reads `to_server`, writes `to_client`. */
    struct ToriRSServerPipe to_server;
    struct ToriRSServerPipe to_client;
    struct ToriRSServerMemoryEnds ends;

    int online;

    /*
     * A party member: the client is in another process and its bytes come
     * over a link socket (torirs_server_embed.h). -1 for an in-process
     * client, which is every client of every host that existed before the
     * party. The session still reads `to_server` and writes `to_client`
     * exactly as for client 0; the link only moves those bytes, at the
     * boundary.
     */
    int link_fd;
    struct ToriRSServerEmbedLinkReader link_in;
    /** READY seen for the boundary being assembled. */
    int link_ready;
    /** READYs taken since this link was seated: the first is not judged
     *  (a join ticks at once), every later one must carry F frames. */
    int link_readies;
    /** The frame count the last READY carried. */
    int link_frames;
    /** The answer to the MAIL this member sent before its READY (raid
     *  seam37), sent before the boundary's TICK; NULL when it sent none. */
    char* mail_answer;
    int mail_answer_size;
};

struct ToriRSServerEmbed
{
    struct ToriRSServer srv;
    struct ToriRSServerEmbedClient clients[TORIRSSERVER_EMBED_CLIENT_MAX];
    struct ToriRSServerBootConfig config;
    /* Recently ended sessions, for a GAMERECONNECT to reclaim. */
    struct ToriRSServerClaims claims;
    /* TORIRS_BOTDRIVE_AGENT's drive, once the world is built -- or the one a
     * Play asked for (ToriRSServer_EmbedBotDriveRequest), botdrive_requested
     * set, which ends with that Play */
    struct ToriRSServerBotDrive* botdrive;
    int botdrive_tried;
    int botdrive_requested;
    char botdrive_request[512];

    /* The party link, or -1: see ToriRSServer_EmbedPartyAttach. */
    int party_listener;
    int party_size;
    int party_wait_ms;
    /** Members ever accepted. Assembly is over once this reaches
     *  party_size - 1, or once a wait for it timed out. */
    int party_joined;
    int party_assembled;
    /** TORIRS_EMBED_PARTY_TRACE: the leader's boundary line. */
    int party_trace;
    /** Party boundaries run (flushed), for the trace's `boundary k`. */
    int party_boundaries;
    /*
     * Accepted links that have not said their SEAT yet. A member is given
     * the client id its seat names rather than the next free one, because
     * the id is the login order and the login order is the pid: seated by
     * accept order, which member got pid 1 was whichever process the kernel
     * delivered first, and two runs of one party differed in nothing else.
     */
    int pending_fd[TORIRSSERVER_EMBED_CLIENT_MAX];
    struct ToriRSServerEmbedLinkReader pending_in[TORIRSSERVER_EMBED_CLIENT_MAX];
};

/* Milliseconds on a monotonic clock, for the reconnect window. */
long
ToriRSServer_EmbedNowMs(void)
{
    struct timespec ts;

    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0;
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

/*
 * Static data is process-wide (the cache decoders and the content tree are all
 * file-scope tables), so a second concurrent embed would be sharing them. One
 * handle at a time, and say so rather than corrupting quietly. Note this is a
 * limit on *worlds*, not on clients: several clients in one embed is the point.
 */
static int g_embed_live;

static void embed_link_close(struct ToriRSServerEmbedClient* client);

static struct ToriRSServerEmbedClient*
client_at(
    struct ToriRSServerEmbed* embed,
    int client)
{
    if( client < 0 || client >= TORIRSSERVER_EMBED_CLIENT_MAX )
        return NULL;
    assert(embed);
    return embed->clients[client].open ? &embed->clients[client] : NULL;
}

struct ToriRSServerEmbed*
ToriRSServer_EmbedStart(char const* rev_name)
{
    struct ToriRSServerEmbed* embed;

    if( g_embed_live )
    {
        fprintf(stderr, "torirsserver: an embedded server is already running\n");
        return NULL;
    }

    embed = (struct ToriRSServerEmbed*)calloc(1, sizeof(*embed));
    assert(embed);
    embed->party_listener = -1;
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
        embed->pending_fd[i] = -1;

    ToriRSServer_BootDefaults(&embed->config);
    /* No server pack, or a stale one: no world. The message has already named
     * the command that builds it; starting on half a content load is the
     * failure the refusal exists to prevent. */
    if( ToriRSServer_BootLoad(&embed->config) == TORIRSSERVER_BOOT_NO_PACK )
    {
        ToriRSServer_BootFree();
        free(embed);
        return NULL;
    }
    /* Shop definitions are global (ToriRSServer_ContentLoad populated them);
     * seeding a container is per-server-instance, so it happens once `srv`
     * itself exists. Calloc above already zeroed world_containers. */
    ToriRSServer_ShopSeed(&embed->srv);

    embed->srv.verbose = getenv("TORIRSSERVER_VERBOSE") != NULL;
    embed->srv.members_world = ToriRSServer_FlagDefaultOn("TORIRSSERVER_MEMBERS_WORLD");
    /*
     * Which bytes this world writes (and which login block it expects). The
     * caller's revision wins — the embed serves exactly one client, in this
     * process, so its wire is a fact about that client and not a preference
     * (a mismatch surfaces as "rsa decrypt failed" at login). TORIRSSERVER_REV and
     * the osrs230 default remain for hosts that pass NULL (embed_test).
     */
    {
        if( !rev_name )
            rev_name = getenv("TORIRSSERVER_REV");
        const struct ToriRSServerWire* wire =
            rev_name ? ToriRSServer_WireByName(rev_name) : ToriRSServer_WireDefault();

        if( !wire )
        {
            fprintf(stderr, "torirsserver: unknown embed revision '%s' (osrs230, osrs239)\n",
                    rev_name);
            ToriRSServer_BootFree();
            free(embed);
            return NULL;
        }
        embed->srv.wire = wire;
        fprintf(stderr, "torirsserver: embedded wire %s\n", wire->name);
    }

    g_embed_live = 1;

    /* Client 0 opens with the world, so a single-client host — which is every
     * host that existed before this — never has to ask for one. */
    if( ToriRSServer_EmbedConnect(embed) != 0 )
    {
        ToriRSServer_EmbedStop(embed);
        return NULL;
    }
    return embed;
}

/* Open client `i`, which must be free. */
static void
embed_connect_at(
    struct ToriRSServerEmbed* embed,
    int i)
{
    struct ToriRSServerEmbedClient* client;

    assert(embed);
    assert(i >= 0);
    assert(i < TORIRSSERVER_EMBED_CLIENT_MAX);
    client = &embed->clients[i];
    assert(!client->open);
    memset(client, 0, sizeof(*client));
    client->open = 1;
    client->link_fd = -1;
    ToriRSServer_TransportMemory(&client->transport, &client->ends, &client->to_server,
                             &client->to_client);
    ToriRSServer_SessionInit(&client->session, &client->transport, embed->srv.verbose);
}

int
ToriRSServer_EmbedConnect(struct ToriRSServerEmbed* embed)
{
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        if( embed->clients[i].open )
            continue;
        embed_connect_at(embed, i);
        return i;
    }
    fprintf(stderr, "torirsserver: the embedded server holds %d clients already\n",
            TORIRSSERVER_EMBED_CLIENT_MAX);
    return -1;
}

int
ToriRSServer_EmbedDisconnect(
    struct ToriRSServerEmbed* embed,
    int client_id)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    if( !client )
        return 0;

    /* Order matters and is the socket server's: the world lets go of the player
     * while the session is still addressable, because that is what makes the
     * packets a logout generates reach everyone *else* before the queues go. */
    /* A party member dropped before its login landed (a launched member whose
     * READY never came, raid seam37) has no player: there is nothing in the
     * world to let go of (WorldRemovePlayer asserts its player; under NDEBUG
     * the NULL was a SIGSEGV in the leader, measured). */
    if( client->session.player )
    {
        ToriRSServer_ClaimNoteDeparted(&embed->claims, client->session.player, ToriRSServer_EmbedNowMs());
        ToriRSServer_WorldRemovePlayer(&embed->srv, client->session.player);
    }
    client->session.player = NULL;
    client->online = 0;
    ToriRSServer_SessionFree(&client->session);
    ToriRSServer_PipeFree(&client->to_server);
    ToriRSServer_PipeFree(&client->to_client);
    embed_link_close(client);
    client->open = 0;
    return 1;
}

int
ToriRSServer_EmbedBotDriveRequest(
    struct ToriRSServerEmbed* embed,
    char const* agent)
{
    assert(embed);
    assert(agent);
    assert(agent[0]);
    if( embed->botdrive || getenv("TORIRS_BOTDRIVE_AGENT") )
        return 1;
    snprintf(embed->botdrive_request, sizeof(embed->botdrive_request), "%s", agent);
    return 0;
}

void
ToriRSServer_EmbedBotDriveRelease(struct ToriRSServerEmbed* embed)
{
    assert(embed);
    embed->botdrive_request[0] = '\0';
    if( !embed->botdrive_requested )
        return;
    ToriRSServer_BotDriveStop(embed->botdrive);
    embed->botdrive = NULL;
    embed->botdrive_requested = 0;
    fprintf(stderr, "botdrive: the Play that asked for the agent ended; it drives no one now\n");
}

void
ToriRSServer_EmbedStop(struct ToriRSServerEmbed* embed)
{
    assert(embed);

    ToriRSServer_BotDriveStop(embed->botdrive);
    embed->botdrive = NULL;

    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        struct ToriRSServerEmbedClient* client = &embed->clients[i];

        if( !client->open )
            continue;
        /*
         * Log the player out before freeing anything, exactly as
         * `ToriRSServer_EmbedDisconnect` does.
         *
         * Shutting the host down IS a logout for whoever is still on it, and
         * this loop was skipping it — it freed the session and the pipes and
         * left `ToriRSServer_WorldRemovePlayer` uncalled. That was invisible while
         * `remove_player` only released a slot the process was about to drop
         * anyway; it stopped being invisible when the save moved there, because
         * closing the embedded client (which is how anyone actually plays this)
         * threw the session's progress away while a socket logout kept it.
         */
        if( client->session.player )
        {
            ToriRSServer_WorldRemovePlayer(&embed->srv, client->session.player);
            client->session.player = NULL;
        }
        ToriRSServer_SessionFree(&client->session);
        /* The transport closed the pipes; this releases what they held. */
        ToriRSServer_PipeFree(&client->to_server);
        ToriRSServer_PipeFree(&client->to_client);
        /* A member sees the end of its link as the server going away. The
         * listener is the transport's, which outlives this embed. */
        embed_link_close(client);
    }
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
#if EMBED_PARTY_SOCKETS
        if( embed->pending_fd[i] >= 0 )
            close(embed->pending_fd[i]);
#endif
        ToriRSServer_EmbedLinkReaderFree(&embed->pending_in[i]);
    }
    ToriRSServer_BankShutdown(&embed->srv);
    ToriRSServer_ContainerShutdown(&embed->srv);
    ToriRSServer_ScriptsFree(&embed->srv);
    ToriRSServer_BootFree();

    free(embed);
    g_embed_live = 0;
}

int
ToriRSServer_EmbedWrite(
    struct ToriRSServerEmbed* embed,
    int client_id,
    const uint8_t* data,
    int len)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    if( !client || !ToriRSServer_SessionAlive(&client->session) )
        return -1;
    return ToriRSServer_PipeWrite(&client->to_server, data, len);
}

int
ToriRSServer_EmbedRead(
    struct ToriRSServerEmbed* embed,
    int client_id,
    uint8_t* dst,
    int max)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    if( !client )
        return -1;
    return ToriRSServer_PipeRead(&client->to_client, dst, max);
}

int
ToriRSServer_EmbedPending(
    const struct ToriRSServerEmbed* embed,
    int client_id)
{
    const struct ToriRSServerEmbedClient* client =
        client_at((struct ToriRSServerEmbed*)embed, client_id);

    return client ? ToriRSServer_PipeAvailable(&client->to_client) : 0;
}

struct ToriRSServer*
ToriRSServer_EmbedWorld(struct ToriRSServerEmbed* embed)
{
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
        if( embed->clients[i].open && embed->clients[i].online )
            return &embed->srv;
    return NULL;
}

int
ToriRSServer_EmbedOnline(
    const struct ToriRSServerEmbed* embed,
    int client_id)
{
    const struct ToriRSServerEmbedClient* client =
        client_at((struct ToriRSServerEmbed*)embed, client_id);

    return client ? client->online : 0;
}

int
ToriRSServer_EmbedClientAlive(
    const struct ToriRSServerEmbed* embed,
    int client_id)
{
    const struct ToriRSServerEmbedClient* client =
        client_at((struct ToriRSServerEmbed*)embed, client_id);

    return client ? ToriRSServer_SessionAlive(&client->session) : 0;
}

struct ToriRSServerPlayer*
ToriRSServer_EmbedPlayer(
    struct ToriRSServerEmbed* embed,
    int client_id)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    return client ? client->session.player : NULL;
}

/* ------------------------------------------------------------------ */
/* The party link (torirs_server_embed.h)                              */
/* ------------------------------------------------------------------ */

static void
link_put_u32(
    uint8_t* out,
    uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static uint32_t
link_get_u32(const uint8_t* in)
{
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

/* ------------------------------------------------------------------ */
/* Lockstep, pinned (torirs_server_embed.h): the payloads, the digest, */
/* the READY judgement and the frame audit. Pure and host-independent, */
/* so embed_party_link_test.c drives them with no world booted.        */
/* ------------------------------------------------------------------ */

int
ToriRSServer_EmbedLogicCyclesPerFrame(void)
{
    static int cached;

    if( cached == 0 )
    {
        char const* env = getenv("TORIRS_LOGIC_CYCLES_PER_FRAME");
        int k = env && env[0] ? atoi(env) : 1;

        if( k < 1 || k > TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK ||
            TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK % k != 0 )
        {
            fprintf(stderr,
                    "torirsserver: TORIRS_LOGIC_CYCLES_PER_FRAME=%s must divide %d "
                    "(1, 2, 3, 5, 6, 10, 15 or 30)\n",
                    env ? env : "", TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK);
            abort();
        }
        cached = k;
    }
    return cached;
}

int
ToriRSServer_EmbedPartyFramesPerTick(int cycles_per_frame)
{
    assert(cycles_per_frame >= 1);
    assert(TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK % cycles_per_frame == 0);
    return TORIRSSERVER_EMBED_PARTY_FRAMES_PER_TICK / cycles_per_frame;
}

static uint32_t
digest_u32(
    uint32_t hash,
    uint32_t value)
{
    /* FNV-1a, a byte at a time, big-endian, so the digest does not depend on
     * the host's byte order. */
    for( int shift = 24; shift >= 0; shift -= 8 )
    {
        hash ^= (value >> shift) & 0xFFu;
        hash *= 16777619u;
    }
    return hash;
}

uint32_t
ToriRSServer_EmbedWorldDigest(const struct ToriRSServer* srv)
{
    uint32_t hash = 2166136261u;
    uint32_t npcs = 0;

    assert(srv);
    if( !srv->world_built )
        return 0;
    hash = digest_u32(hash, (uint32_t)srv->tick);
    for( int pid = 0; pid < TORIRSSERVER_PLAYER_MAX; pid++ )
    {
        const struct ToriRSServerPlayer* p = &srv->players[pid];

        if( !p->active )
            continue;
        hash = digest_u32(hash, (uint32_t)pid);
        hash = digest_u32(hash, (uint32_t)p->x);
        hash = digest_u32(hash, (uint32_t)p->z);
        hash = digest_u32(hash, (uint32_t)p->level);
        hash = digest_u32(hash, (uint32_t)p->hitpoints);
    }
    for( int slot = 0; slot < srv->npc_slot_max && slot < TORIRSSERVER_NPC_MAX; slot++ )
        if( srv->npcs[slot].active )
            npcs++;
    return digest_u32(hash, npcs);
}

void
ToriRSServer_EmbedLinkSeatEncode(
    uint8_t* out,
    int seat,
    int cycles_per_frame)
{
    assert(out);
    link_put_u32(out, (uint32_t)TORIRSSERVER_EMBED_PARTY_PROTOCOL);
    link_put_u32(out + 4, (uint32_t)seat);
    link_put_u32(out + 8, (uint32_t)cycles_per_frame);
}

int
ToriRSServer_EmbedLinkSeatDecode(
    const uint8_t* payload,
    int len,
    int* out_version,
    int* out_seat,
    int* out_cycles_per_frame)
{
    assert(out_version);
    assert(out_seat);
    assert(out_cycles_per_frame);
    /* A version-1 member sent its seat alone, in 4 bytes: say "version 1"
     * so the leader's refusal names the mismatch, not a torn frame. */
    if( len == 4 )
    {
        assert(payload);
        *out_version = 1;
        *out_seat = (int)link_get_u32(payload);
        *out_cycles_per_frame = 1;
        return 1;
    }
    if( len != TORIRSSERVER_EMBED_LINK_SEAT_LEN )
        return 0;
    assert(payload);
    *out_version = (int)link_get_u32(payload);
    *out_seat = (int)link_get_u32(payload + 4);
    *out_cycles_per_frame = (int)link_get_u32(payload + 8);
    return 1;
}

void
ToriRSServer_EmbedLinkReadyEncode(
    uint8_t* out,
    int frames)
{
    assert(out);
    assert(frames >= 0);
    link_put_u32(out, (uint32_t)frames);
}

int
ToriRSServer_EmbedLinkReadyDecode(
    const uint8_t* payload,
    int len,
    int* out_frames)
{
    assert(out_frames);
    if( len != TORIRSSERVER_EMBED_LINK_READY_LEN )
        return 0;
    assert(payload);
    *out_frames = (int)link_get_u32(payload);
    return 1;
}

void
ToriRSServer_EmbedLinkTickEncode(
    uint8_t* out,
    int tick,
    uint32_t digest)
{
    assert(out);
    link_put_u32(out, (uint32_t)tick);
    link_put_u32(out + 4, digest);
}

int
ToriRSServer_EmbedLinkTickDecode(
    const uint8_t* payload,
    int len,
    int* out_tick,
    uint32_t* out_digest)
{
    assert(out_tick);
    assert(out_digest);
    if( len != TORIRSSERVER_EMBED_LINK_TICK_LEN )
        return 0;
    assert(payload);
    *out_tick = (int)link_get_u32(payload);
    *out_digest = link_get_u32(payload + 4);
    return 1;
}

int
ToriRSServer_EmbedPartyReadyCheck(
    int seat,
    int frames,
    int expected,
    int first)
{
    assert(expected > 0);
    if( first || frames == expected )
        return 1;
    fprintf(stderr, "torirsserver: party: seat %d ran %d frames, expected %d\n", seat, frames,
            expected);
    return 0;
}

static int g_lockstep_tick = TORIRSSERVER_EMBED_LOCKSTEP_NONE;

int
ToriRSServer_EmbedLockstepTick(void)
{
    return g_lockstep_tick;
}

void
ToriRSServer_EmbedLockstepNote(int tick)
{
    g_lockstep_tick = tick;
}

static int g_client_tick = TORIRSSERVER_EMBED_LOCKSTEP_NONE;

int
ToriRSServer_EmbedClientTick(void)
{
    return g_client_tick;
}

void
ToriRSServer_EmbedClientTickNote(int tick)
{
    g_client_tick = tick;
}

/* The frame audit. One process, one frame loop: file scope is the loop's. */
static int g_audit_armed;
static int g_audit_polls;
static int g_audit_frames_since_poll;

void
ToriRSServer_EmbedPartyAuditArm(int armed)
{
    if( armed && !g_audit_armed )
        fprintf(stderr, "torirsserver: party: frame audit armed (one poll per frame, %d logic "
                        "cycle(s) per frame, %d frames per tick)\n",
                ToriRSServer_EmbedLogicCyclesPerFrame(),
                ToriRSServer_EmbedPartyFramesPerTick(ToriRSServer_EmbedLogicCyclesPerFrame()));
    g_audit_armed = armed;
    /* The poll that arms counts as the first: the next one checks the
     * frame between them. */
    g_audit_polls = armed ? 1 : 0;
    g_audit_frames_since_poll = 0;
}

int
ToriRSServer_EmbedPartyAuditArmed(void)
{
    return g_audit_armed;
}

void
ToriRSServer_EmbedPartyAuditPoll(void)
{
    if( g_audit_armed && g_audit_polls > 0 && g_audit_frames_since_poll != 1 )
    {
        /* abort(), not assert(): the quest binary is OPT=1 (NDEBUG), and a
         * skipped or doubled frame is exactly the silent shift this guards. */
        fprintf(stderr,
                "torirsserver: party: %d frame(s) ran between two transport polls, expected "
                "exactly 1 -- a skipped or doubled frame breaks the lock step\n",
                g_audit_frames_since_poll);
        abort();
    }
    g_audit_polls++;
    g_audit_frames_since_poll = 0;
}

void
ToriRSServer_EmbedPartyAuditFrame(void)
{
    g_audit_frames_since_poll++;
}

void
ToriRSServer_EmbedPartyAuditCycles(int cycles)
{
    int const k = ToriRSServer_EmbedLogicCyclesPerFrame();

    if( g_audit_armed && cycles != k )
    {
        fprintf(stderr,
                "torirsserver: party: a frame paid %d logic cycle(s), expected %d "
                "(TORIRS_LOGIC_CYCLES_PER_FRAME); a party client needs TORIRS_MAX_FRAMES so "
                "every frame pays exactly k\n",
                cycles, k);
        abort();
    }
}

/* ---- the launch service on the party link (raid seam37; the header) ---- */

/* The leader's answerer (main.c: its launch service), or NULL. */
static ToriRSServerEmbedMailAnswer g_mail_answer;
static void* g_mail_answer_context;

void
ToriRSServer_EmbedPartySetMailAnswer(
    ToriRSServerEmbedMailAnswer answer,
    void* context)
{
    g_mail_answer = answer;
    g_mail_answer_context = context;
}

/* The member's side: the status it posts, the commands the leader queued for
 * it, the first line of the last answer. One member per process. */
enum
{
    EMBED_MEMBER_STATUS_MAX = 1024,
    EMBED_MEMBER_COMMANDS_MAX = 16,
};
static char g_member_status[EMBED_MEMBER_STATUS_MAX];
static char* g_member_commands[EMBED_MEMBER_COMMANDS_MAX];
static int g_member_command_count;
static char g_member_mail_last[256];
static int g_member_mail_answers;

void
ToriRSServer_EmbedMemberStatusPost(char const* status)
{
    size_t length;

    assert(status);
    snprintf(g_member_status, sizeof(g_member_status), "%s", status);
    /* One line: the service's status= value runs to the end of its line. */
    length = strcspn(g_member_status, "\r\n");
    g_member_status[length] = '\0';
}

int
ToriRSServer_EmbedMemberMailRequest(
    char* out,
    int capacity)
{
    char const* session = getenv("TORIRS_LAUNCH_SESSION");
    char const* seat = getenv("TORIRS_LAUNCH_SEAT");
    char const* token = getenv("TORIRS_LAUNCH_SEAT_TOKEN");
    int written;

    assert(out);
    assert(capacity > 0);
    out[0] = '\0';
    if( !session || !session[0] || !seat || !seat[0] || !token || !token[0] )
        return 0;
    written = snprintf(out, (size_t)capacity, "session=%s\nseat=%s\nseat_token=%s\nstatus=%s\n",
                       session, seat, token, g_member_status[0] ? g_member_status : "-");
    assert(written > 0);
    assert(written < capacity);
    return written;
}

void
ToriRSServer_EmbedMemberMailDeliver(
    const uint8_t* data,
    int len)
{
    char const* text = (char const*)data;
    int at = 0;

    assert(len >= 0);
    if( len > 0 )
        assert(data);
    g_member_mail_answers++;
    g_member_mail_last[0] = '\0';
    while( at < len )
    {
        int end = at;
        int line_length;

        while( end < len && text[end] != '\n' )
            end++;
        line_length = end - at;
        if( at == 0 )
            snprintf(g_member_mail_last, sizeof(g_member_mail_last), "%.*s", line_length, text);
        else if( line_length > 8 && strncmp(text + at, "command=", 8) == 0 )
        {
            char* command;

            if( g_member_command_count == EMBED_MEMBER_COMMANDS_MAX )
            {
                /* The service's mailbox holds as many; a member that let them
                 * pile up loses the oldest, and says so. */
                fprintf(stderr, "torirsserver: party: launch: %d unread commands; dropping '%s'\n",
                        EMBED_MEMBER_COMMANDS_MAX, g_member_commands[0]);
                free(g_member_commands[0]);
                memmove(g_member_commands, g_member_commands + 1,
                        sizeof(g_member_commands[0]) * (EMBED_MEMBER_COMMANDS_MAX - 1));
                g_member_command_count--;
            }
            command = (char*)malloc((size_t)(line_length - 8 + 1));
            assert(command);
            memcpy(command, text + at + 8, (size_t)(line_length - 8));
            command[line_length - 8] = '\0';
            g_member_commands[g_member_command_count++] = command;
        }
        at = end + 1;
    }
    if( strncmp(g_member_mail_last, "ok", 2) != 0 )
        fprintf(stderr, "torirsserver: party: launch: the leader answered this member's mail: %s\n",
                g_member_mail_last);
}

int
ToriRSServer_EmbedMemberCommandTake(
    char* out,
    int capacity)
{
    assert(out);
    assert(capacity > 0);
    if( g_member_command_count == 0 )
        return 0;
    snprintf(out, (size_t)capacity, "%s", g_member_commands[0]);
    free(g_member_commands[0]);
    memmove(g_member_commands, g_member_commands + 1,
            sizeof(g_member_commands[0]) * (size_t)(g_member_command_count - 1));
    g_member_command_count--;
    return 1;
}

char const*
ToriRSServer_EmbedMemberMailLast(int* out_answers)
{
    if( out_answers )
        *out_answers = g_member_mail_answers;
    return g_member_mail_last;
}

void
ToriRSServer_EmbedLaunchWatchdog(void)
{
#if EMBED_PARTY_SOCKETS
    static int leader_pid = -1;
    static long next_check_ms;
    long now;

    if( leader_pid < 0 )
    {
        char const* knob = getenv("TORIRS_LAUNCH_LEADER_PID");
        leader_pid = knob && knob[0] ? atoi(knob) : 0;
        if( leader_pid > 0 )
            fprintf(stderr, "torirsserver: launch: watching the leader, pid %d (exit when it is "
                            "gone)\n", leader_pid);
    }
    if( leader_pid <= 0 )
        return;
    now = ToriRSServer_EmbedNowMs();
    if( now < next_check_ms )
        return;
    next_check_ms = now + 1000;
    if( kill((pid_t)leader_pid, 0) == 0 || errno != ESRCH )
        return;
    fprintf(stderr, "torirsserver: launch: the leader (pid %d) is gone -- this member exits "
                    "(a member never outlives its leader)\n", leader_pid);
    exit(EXIT_FAILURE);
#endif
}

static int g_party_host_pending_listener = -1;
static int g_party_host_pending_size;
static int g_party_hosting;

int
ToriRSServer_EmbedPartyHostRequest(
    int listener,
    int party_size)
{
    assert(listener >= 0);
    assert(party_size >= 2);
    assert(party_size <= TORIRSSERVER_EMBED_CLIENT_MAX);
    if( g_party_hosting || g_party_host_pending_listener >= 0 )
        return -1;
    g_party_host_pending_listener = listener;
    g_party_host_pending_size = party_size;
    return 0;
}

int
ToriRSServer_EmbedPartyHostTake(
    int* out_listener,
    int* out_party_size)
{
    assert(out_listener);
    assert(out_party_size);
    if( g_party_host_pending_listener < 0 )
        return 0;
    *out_listener = g_party_host_pending_listener;
    *out_party_size = g_party_host_pending_size;
    g_party_host_pending_listener = -1;
    g_party_host_pending_size = 0;
    g_party_hosting = 1;
    return 1;
}

void
ToriRSServer_EmbedPartyHostNote(int hosting)
{
    g_party_hosting = hosting;
}

static int g_party_host_release_pending;

void
ToriRSServer_EmbedPartyHostRelease(void)
{
    if( g_party_host_pending_listener >= 0 )
    {
#if EMBED_PARTY_SOCKETS
        close(g_party_host_pending_listener);
#endif
        g_party_host_pending_listener = -1;
        g_party_host_pending_size = 0;
    }
    g_party_host_release_pending = 1;
}

int
ToriRSServer_EmbedPartyHostReleaseTake(void)
{
    int const pending = g_party_host_release_pending;

    g_party_host_release_pending = 0;
    return pending;
}

#if EMBED_PARTY_SOCKETS

/*
 * A link whose other end has exited must read as "closed", not kill this
 * process: send() on it raises SIGPIPE by default, and a member whose leader
 * finished first died of it (exit -13) instead of reporting the link gone.
 * Linux takes a per-send flag, macOS a per-socket option.
 */
#ifdef MSG_NOSIGNAL
#define LINK_SEND_FLAGS MSG_NOSIGNAL
#else
#define LINK_SEND_FLAGS 0
#endif

static void
link_socket_options(int fd)
{
    int nodelay = 1;

    assert(fd >= 0);
    /* Every boundary is a short READY answered by a burst and a TICK; Nagle
     * would hold the READY for the peer's delayed ACK on every tick. */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
#ifdef SO_NOSIGPIPE
    {
        int one = 1;

        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (char*)&one, sizeof(one));
    }
#endif
}

/* write(2) until done; 0 once the peer is gone. */
static int
link_write_all(
    int fd,
    const uint8_t* data,
    int len)
{
    while( len > 0 )
    {
        ssize_t sent = send(fd, data, (size_t)len, LINK_SEND_FLAGS);

        if( sent < 0 && errno == EINTR )
            continue;
        if( sent <= 0 )
            return 0;
        data += sent;
        len -= (int)sent;
    }
    return 1;
}

int
ToriRSServer_EmbedPartyListen(int port)
{
    struct sockaddr_in addr;
    int reuse = 1;
    int fd;

    assert(port > 0);
    assert(port < 65536);
    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* Loopback only: the members are this machine's other client processes,
     * and a development world with no authentication is never offered to
     * the network by a test knob. */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        fprintf(stderr, "torirsserver: party: cannot listen on 127.0.0.1:%d: %s\n", port,
                strerror(errno));
        close(fd);
        return -1;
    }
    listen(fd, 8);
    fprintf(stderr, "torirsserver: party: listening on 127.0.0.1:%d\n", port);
    return fd;
}

int
ToriRSServer_EmbedPartyDial(
    int port,
    int wait_ms)
{
    long deadline = ToriRSServer_EmbedNowMs() + wait_ms;

    assert(port > 0);
    assert(port < 65536);
    for( ;; )
    {
        struct sockaddr_in addr;
        int fd = (int)socket(AF_INET, SOCK_STREAM, 0);

        assert(fd >= 0);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)port);
        if( connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0 )
        {
            link_socket_options(fd);
            return fd;
        }
        close(fd);
        /* A launched member whose leader died before it listened: the dial
         * would wait out its whole budget against nobody. */
        ToriRSServer_EmbedLaunchWatchdog();
        /* The leader may still be booting. Refused is the only answer a
         * loopback dial gives while nobody listens, so retry it. */
        if( ToriRSServer_EmbedNowMs() >= deadline )
        {
            fprintf(stderr, "torirsserver: party: nobody listening on 127.0.0.1:%d after %d ms\n",
                    port, wait_ms);
            return -1;
        }
        poll(NULL, 0, 50);
    }
}

int
ToriRSServer_EmbedLinkSend(
    int fd,
    int type,
    const uint8_t* data,
    int len)
{
    uint8_t header[TORIRSSERVER_EMBED_LINK_HEADER];

    assert(fd >= 0);
    assert(len >= 0);
    assert(len == 0 || data);
    assert(type == TORIRSSERVER_EMBED_LINK_DATA || type == TORIRSSERVER_EMBED_LINK_READY ||
           type == TORIRSSERVER_EMBED_LINK_TICK || type == TORIRSSERVER_EMBED_LINK_SEAT ||
           type == TORIRSSERVER_EMBED_LINK_MAIL || type == TORIRSSERVER_EMBED_LINK_MAIL_ANSWER);
    header[0] = (uint8_t)type;
    link_put_u32(header + 1, (uint32_t)len);
    if( !link_write_all(fd, header, (int)sizeof(header)) )
        return 0;
    return len == 0 || link_write_all(fd, data, len);
}

int
ToriRSServer_EmbedLinkFill(
    int fd,
    struct ToriRSServerEmbedLinkReader* reader,
    int timeout_ms)
{
    struct pollfd pfd;
    int got_any = 0;

    assert(fd >= 0);
    assert(reader);
    assert(timeout_ms >= 0);
    pfd.fd = fd;
    pfd.events = POLLIN;
    for( ;; )
    {
        ssize_t got;
        int ready = poll(&pfd, 1, got_any ? 0 : timeout_ms);

        if( ready < 0 && errno == EINTR )
            continue;
        if( ready <= 0 )
            return got_any;
        /* Compact, then make room for at least 64 KB more. */
        if( reader->head > 0 )
        {
            memmove(reader->data, reader->data + reader->head,
                    (size_t)(reader->tail - reader->head));
            reader->tail -= reader->head;
            reader->head = 0;
        }
        if( reader->cap - reader->tail < 65536 )
        {
            int cap = reader->cap ? reader->cap * 2 : 131072;

            while( cap - reader->tail < 65536 )
                cap *= 2;
            reader->data = (uint8_t*)realloc(reader->data, (size_t)cap);
            assert(reader->data);
            reader->cap = cap;
        }
        got = recv(fd, reader->data + reader->tail, (size_t)(reader->cap - reader->tail), 0);
        if( got < 0 && errno == EINTR )
            continue;
        if( got <= 0 )
            return got_any ? 1 : -1;
        reader->tail += (int)got;
        got_any = 1;
    }
}

#else /* !EMBED_PARTY_SOCKETS */

int
ToriRSServer_EmbedPartyListen(int port)
{
    fprintf(stderr, "torirsserver: party: port %d asked for, but this build has no party "
                    "link (native POSIX hosts only)\n", port);
    abort();
}

int
ToriRSServer_EmbedPartyDial(
    int port,
    int wait_ms)
{
    (void)wait_ms;
    fprintf(stderr, "torirsserver: party: port %d asked for, but this build has no party "
                    "link (native POSIX hosts only)\n", port);
    abort();
}

int
ToriRSServer_EmbedLinkSend(
    int fd,
    int type,
    const uint8_t* data,
    int len)
{
    (void)fd; (void)type; (void)data; (void)len;
    assert(0 && "ToriRSServer_EmbedLinkSend: no party link in this build");
    return 0;
}

int
ToriRSServer_EmbedLinkFill(
    int fd,
    struct ToriRSServerEmbedLinkReader* reader,
    int timeout_ms)
{
    (void)fd; (void)reader; (void)timeout_ms;
    assert(0 && "ToriRSServer_EmbedLinkFill: no party link in this build");
    return -1;
}

#endif /* EMBED_PARTY_SOCKETS */

int
ToriRSServer_EmbedLinkNext(
    struct ToriRSServerEmbedLinkReader* reader,
    int* out_type,
    const uint8_t** out_payload,
    int* out_len)
{
    uint32_t len;
    int live;

    assert(reader);
    assert(out_type);
    assert(out_payload);
    assert(out_len);
    live = reader->tail - reader->head;
    if( live < TORIRSSERVER_EMBED_LINK_HEADER )
        return 0;
    len = link_get_u32(reader->data + reader->head + 1);
    /* Both ends are this file's; a length past any packet burst is a torn
     * stream, not a big frame. */
    assert(len <= 64u * 1024u * 1024u);
    if( (uint32_t)live < TORIRSSERVER_EMBED_LINK_HEADER + len )
        return 0;
    *out_type = reader->data[reader->head];
    *out_payload = reader->data + reader->head + TORIRSSERVER_EMBED_LINK_HEADER;
    *out_len = (int)len;
    reader->head += TORIRSSERVER_EMBED_LINK_HEADER + (int)len;
    return 1;
}

void
ToriRSServer_EmbedLinkReaderFree(struct ToriRSServerEmbedLinkReader* reader)
{
    if( !reader )
        return;
    free(reader->data);
    memset(reader, 0, sizeof(*reader));
}

static void
embed_link_close(struct ToriRSServerEmbedClient* client)
{
    assert(client);
#if EMBED_PARTY_SOCKETS
    if( client->link_fd >= 0 )
        close(client->link_fd);
#endif
    client->link_fd = -1;
    client->link_ready = 0;
    free(client->mail_answer);
    client->mail_answer = NULL;
    client->mail_answer_size = 0;
    ToriRSServer_EmbedLinkReaderFree(&client->link_in);
}

void
ToriRSServer_EmbedPartyAttach(
    struct ToriRSServerEmbed* embed,
    int listener,
    int party_size,
    int wait_ms,
    int trace)
{
    assert(embed);
    assert(listener >= 0);
    assert(party_size >= 0);
    assert(party_size <= TORIRSSERVER_EMBED_CLIENT_MAX);
    assert(wait_ms > 0);
    assert(embed->party_listener < 0);
    embed->party_listener = listener;
    embed->party_size = party_size;
    embed->party_wait_ms = wait_ms;
    embed->party_assembled = party_size <= 1;
    embed->party_trace = trace;
    fprintf(stderr,
            "torirsserver: party: attached (party of %d, member wait %d ms, protocol %d, "
            "%d logic cycle(s) per frame, %d frames per tick)\n",
            party_size, wait_ms, TORIRSSERVER_EMBED_PARTY_PROTOCOL,
            ToriRSServer_EmbedLogicCyclesPerFrame(),
            ToriRSServer_EmbedPartyFramesPerTick(ToriRSServer_EmbedLogicCyclesPerFrame()));
}

int
ToriRSServer_EmbedPartyJoined(const struct ToriRSServerEmbed* embed)
{
    assert(embed);
    return embed->party_joined;
}

int
ToriRSServer_EmbedPartyOpen(const struct ToriRSServerEmbed* embed)
{
    int open = 0;

    assert(embed);
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
        if( embed->clients[i].open && embed->clients[i].link_fd >= 0 )
            open++;
    return open;
}

void
ToriRSServer_EmbedPartyDetach(struct ToriRSServerEmbed* embed)
{
    assert(embed);
    assert(embed->party_listener >= 0);
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        if( !embed->clients[i].open || embed->clients[i].link_fd < 0 )
            continue;
        fprintf(stderr, "torirsserver: party: client %d: the party was released -- logged out\n", i);
        ToriRSServer_EmbedDisconnect(embed, i);
    }
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
#if EMBED_PARTY_SOCKETS
        if( embed->pending_fd[i] >= 0 )
            close(embed->pending_fd[i]);
#endif
        embed->pending_fd[i] = -1;
        ToriRSServer_EmbedLinkReaderFree(&embed->pending_in[i]);
    }
    embed->party_listener = -1;
    embed->party_size = 0;
    embed->party_joined = 0;
    embed->party_assembled = 0;
    embed->party_boundaries = 0;
}

#if EMBED_PARTY_SOCKETS

static char const*
link_name(const struct ToriRSServerEmbedClient* client)
{
    return client->session.display_name[0] ? client->session.display_name : "(not logged in)";
}

/* Drop a member: the same logout a closed socket gets. */
static void
party_drop(
    struct ToriRSServerEmbed* embed,
    int client_id,
    char const* why)
{
    fprintf(stderr, "torirsserver: party: client %d %s %s -- logged out\n", client_id,
            link_name(&embed->clients[client_id]), why);
    ToriRSServer_EmbedDisconnect(embed, client_id);
}

/* Take every member waiting in the backlog. */
static void
party_accept(struct ToriRSServerEmbed* embed)
{
    for( ;; )
    {
        struct pollfd pfd;
        int fd;
        int id;

        pfd.fd = embed->party_listener;
        pfd.events = POLLIN;
        if( poll(&pfd, 1, 0) <= 0 )
            return;
        fd = (int)accept(embed->party_listener, NULL, NULL);
        if( fd < 0 )
            return;
        link_socket_options(fd);
        for( id = 0; id < TORIRSSERVER_EMBED_CLIENT_MAX; id++ )
            if( embed->pending_fd[id] < 0 )
                break;
        if( id == TORIRSSERVER_EMBED_CLIENT_MAX )
        {
            /* Closed at once: the member reads the end of its link as a
             * refused login rather than waiting out its timeout. */
            fprintf(stderr, "torirsserver: party: %d links already waiting for a seat; "
                            "refusing\n", TORIRSSERVER_EMBED_CLIENT_MAX);
            close(fd);
            continue;
        }
        embed->pending_fd[id] = fd;
    }
}

/*
 * Seat every accepted link whose SEAT frame has arrived: its first frame,
 * always. Seat n is client id n - 1 (the leader is seat 1); seat 0 takes the
 * first free id.
 */
static void
party_seat(struct ToriRSServerEmbed* embed)
{
    for( int k = 0; k < TORIRSSERVER_EMBED_CLIENT_MAX; k++ )
    {
        struct ToriRSServerEmbedLinkReader* in = &embed->pending_in[k];
        int fd = embed->pending_fd[k];
        int type;
        const uint8_t* payload;
        int len;
        int seat;
        int id;
        int closed;

        if( fd < 0 )
            continue;
        closed = ToriRSServer_EmbedLinkFill(fd, in, 0) < 0;
        if( !ToriRSServer_EmbedLinkNext(in, &type, &payload, &len) )
        {
            /* Gone before it said its seat: nothing to log out. */
            if( closed )
            {
                close(fd);
                embed->pending_fd[k] = -1;
                ToriRSServer_EmbedLinkReaderFree(in);
            }
            continue;
        }
        seat = -1;
        if( type == TORIRSSERVER_EMBED_LINK_SEAT )
        {
            int version = 0;
            int cycles = 0;

            if( ToriRSServer_EmbedLinkSeatDecode(payload, len, &version, &seat, &cycles) )
            {
                /* A member built from another protocol, or told to run
                 * another number of cycles per frame, cannot keep this
                 * leader's lock step: say which, and stop the run. */
                if( version != TORIRSSERVER_EMBED_PARTY_PROTOCOL )
                {
                    fprintf(stderr,
                            "torirsserver: party: seat %d speaks link protocol %d, this leader "
                            "%d -- rebuild both from one tree\n",
                            seat, version, TORIRSSERVER_EMBED_PARTY_PROTOCOL);
                    abort();
                }
                if( cycles != ToriRSServer_EmbedLogicCyclesPerFrame() )
                {
                    fprintf(stderr,
                            "torirsserver: party: seat %d runs %d logic cycle(s) per frame, this "
                            "leader %d -- TORIRS_LOGIC_CYCLES_PER_FRAME must be one value for "
                            "the whole party\n",
                            seat, cycles, ToriRSServer_EmbedLogicCyclesPerFrame());
                    abort();
                }
            }
            else
                seat = -1;
        }
        id = -1;
        if( seat == 0 )
        {
            for( id = 1; id < TORIRSSERVER_EMBED_CLIENT_MAX; id++ )
                if( !embed->clients[id].open )
                    break;
        }
        else if( seat >= 2 && seat <= TORIRSSERVER_EMBED_CLIENT_MAX &&
                 !embed->clients[seat - 1].open )
            id = seat - 1;
        if( id < 0 || id >= TORIRSSERVER_EMBED_CLIENT_MAX )
        {
            fprintf(stderr, "torirsserver: party: a link asked for seat %d, which is %s -- "
                            "refused\n", seat,
                    seat < 0 ? "not a SEAT frame" : "taken or out of range");
            close(fd);
            embed->pending_fd[k] = -1;
            ToriRSServer_EmbedLinkReaderFree(in);
            continue;
        }
        embed_connect_at(embed, id);
        embed->clients[id].link_fd = fd;
        embed->clients[id].link_readies = 0;
        /* What followed the SEAT stays buffered for the session. */
        embed->clients[id].link_in = *in;
        memset(in, 0, sizeof(*in));
        embed->pending_fd[k] = -1;
        embed->party_joined++;
        fprintf(stderr, "torirsserver: party: seat %d joined as client %d (%d joined)\n", seat, id,
                embed->party_joined);
    }
}

/*
 * Move one member's buffered frames into its session, up to and including
 * its READY. Returns 0 if the member broke the protocol (a member never
 * sends TICK, and its READY carries its frame count).
 *
 * The READY's count is judged here (ToriRSServer_EmbedPartyReadyCheck): a
 * member that ran other than F clock frames since its last TICK has drifted
 * from the leader's clock, and every input it sent is about to land on a
 * tick it did not mean. That aborts the run -- the loud failure the owner
 * asked for ("I don't want to introduce nondeterminism"), never a shift.
 */
static int
party_take_frames(
    struct ToriRSServerEmbedClient* client,
    int client_id)
{
    int type;
    const uint8_t* payload;
    int len;

    while( !client->link_ready &&
           ToriRSServer_EmbedLinkNext(&client->link_in, &type, &payload, &len) )
    {
        if( type == TORIRSSERVER_EMBED_LINK_DATA )
            ToriRSServer_PipeWrite(&client->to_server, payload, len);
        else if( type == TORIRSSERVER_EMBED_LINK_MAIL )
        {
            /* A launched member's mailbox poll (raid seam37): answered now,
             * in this process's launch service, and sent before the TICK. */
            static char const unsupported[] = "unsupported: no launch service in the leader\n";

            if( len > TORIRSSERVER_EMBED_LINK_MAIL_MAX )
                return 0;
            free(client->mail_answer);
            client->mail_answer = NULL;
            client->mail_answer_size = 0;
            if( g_mail_answer )
                client->mail_answer = g_mail_answer(g_mail_answer_context, (char const*)payload,
                                                    len, &client->mail_answer_size);
            else
            {
                client->mail_answer = (char*)malloc(sizeof(unsupported));
                assert(client->mail_answer);
                memcpy(client->mail_answer, unsupported, sizeof(unsupported));
                client->mail_answer_size = (int)sizeof(unsupported) - 1;
            }
            assert(client->mail_answer);
            if( client->mail_answer_size > TORIRSSERVER_EMBED_LINK_MAIL_MAX )
                client->mail_answer_size = TORIRSSERVER_EMBED_LINK_MAIL_MAX;
        }
        else if( type == TORIRSSERVER_EMBED_LINK_READY )
        {
            int frames = 0;

            if( !ToriRSServer_EmbedLinkReadyDecode(payload, len, &frames) )
                return 0;
            client->link_ready = 1;
            client->link_frames = frames;
            client->link_readies++;
            if( !ToriRSServer_EmbedPartyReadyCheck(
                    client_id + 1, frames,
                    ToriRSServer_EmbedPartyFramesPerTick(ToriRSServer_EmbedLogicCyclesPerFrame()),
                    client->link_readies == 1) )
                abort();
        }
        else
            return 0;
    }
    return 1;
}

/*
 * The barrier: hold this boundary until every member has said READY for it
 * (and, until the party has assembled, until party_size - 1 have joined).
 */
static void
party_barrier(struct ToriRSServerEmbed* embed)
{
    long deadline = ToriRSServer_EmbedNowMs() + embed->party_wait_ms;

    for( ;; )
    {
        struct pollfd pfds[2 * TORIRSSERVER_EMBED_CLIENT_MAX + 1];
        int ids[TORIRSSERVER_EMBED_CLIENT_MAX + 1];
        int count = 0;
        int waiting = 0;
        long left;

        party_accept(embed);
        party_seat(embed);
        if( !embed->party_assembled && embed->party_joined >= embed->party_size - 1 )
        {
            embed->party_assembled = 1;
            fprintf(stderr, "torirsserver: party: assembled, %d member(s) at tick %d\n",
                    embed->party_joined, embed->srv.world_built ? embed->srv.tick : -1);
        }
        for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
        {
            struct ToriRSServerEmbedClient* client = &embed->clients[i];

            if( !client->open || client->link_fd < 0 )
                continue;
            if( !party_take_frames(client, i) )
            {
                party_drop(embed, i, "sent a frame a member never sends");
                continue;
            }
            if( client->link_ready )
                continue;
            waiting++;
            pfds[count].fd = client->link_fd;
            pfds[count].events = POLLIN;
            ids[count] = i;
            count++;
        }
        if( !waiting && embed->party_assembled )
            return;

        left = deadline - ToriRSServer_EmbedNowMs();
        if( left <= 0 )
        {
            for( int k = 0; k < count; k++ )
                party_drop(embed, ids[k], "sent no READY within TORIRS_EMBED_PARTY_WAIT_S");
            if( !embed->party_assembled )
            {
                fprintf(stderr,
                        "torirsserver: party: only %d of %d member(s) joined within "
                        "TORIRS_EMBED_PARTY_WAIT_S -- going on without the rest\n",
                        embed->party_joined, embed->party_size - 1);
                embed->party_assembled = 1;
            }
            return;
        }
        pfds[count].fd = embed->party_listener;
        pfds[count].events = POLLIN;
        ids[count] = -1;
        /* A link still to say its seat wakes the wait too (party_seat reads
         * it at the top of the next pass). */
        {
            int polled = count + 1;

            for( int k = 0; k < TORIRSSERVER_EMBED_CLIENT_MAX; k++ )
            {
                if( embed->pending_fd[k] < 0 )
                    continue;
                pfds[polled].fd = embed->pending_fd[k];
                pfds[polled].events = POLLIN;
                polled++;
            }
            if( poll(pfds, (nfds_t)polled, left > 50 ? 50 : (int)left) <= 0 )
                continue;
        }
        for( int k = 0; k < count; k++ )
        {
            if( !(pfds[k].revents & (POLLIN | POLLHUP | POLLERR)) )
                continue;
            if( ToriRSServer_EmbedLinkFill(pfds[k].fd, &embed->clients[ids[k]].link_in, 0) < 0 )
            {
                /* Whatever it sent before closing still counts. */
                party_take_frames(&embed->clients[ids[k]], ids[k]);
                party_drop(embed, ids[k], "closed its link");
            }
        }
    }
}

/* The boundary ran: hand each member its output, then the TICK. */
static void
party_flush(struct ToriRSServerEmbed* embed)
{
    uint8_t tick[TORIRSSERVER_EMBED_LINK_TICK_LEN];
    int const tick_now = embed->srv.world_built ? embed->srv.tick : -1;
    uint32_t const digest = ToriRSServer_EmbedWorldDigest(&embed->srv);

    ToriRSServer_EmbedLinkTickEncode(tick, tick_now, digest);
    embed->party_boundaries++;
    /* This process's lockstep tick (driver state stamped with it is
     * honoured from the next boundary on; torirs_plugin_drive.c). */
    ToriRSServer_EmbedLockstepNote(tick_now);
    if( embed->party_trace )
        fprintf(stderr, "net: party: boundary %d -> tick %d digest %08x\n",
                embed->party_boundaries, tick_now, digest);
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        struct ToriRSServerEmbedClient* client = &embed->clients[i];
        const uint8_t* bytes;
        int len;
        int ok = 1;

        if( !client->open || client->link_fd < 0 )
            continue;
        bytes = ToriRSServer_PipePeek(&client->to_client, &len);
        if( bytes && len > 0 )
        {
            ok = ToriRSServer_EmbedLinkSend(client->link_fd, TORIRSSERVER_EMBED_LINK_DATA,
                                             bytes, len);
            ToriRSServer_PipeDrop(&client->to_client, len);
        }
        if( ok && client->mail_answer )
            ok = ToriRSServer_EmbedLinkSend(client->link_fd, TORIRSSERVER_EMBED_LINK_MAIL_ANSWER,
                                             (const uint8_t*)client->mail_answer,
                                             client->mail_answer_size);
        free(client->mail_answer);
        client->mail_answer = NULL;
        client->mail_answer_size = 0;
        if( ok )
            ok = ToriRSServer_EmbedLinkSend(client->link_fd, TORIRSSERVER_EMBED_LINK_TICK,
                                             tick, TORIRSSERVER_EMBED_LINK_TICK_LEN);
        client->link_ready = 0;
        if( !ok )
            party_drop(embed, i, "stopped reading its link");
        else if( !ToriRSServer_SessionAlive(&client->session) )
            /* Its output is sent; the end of the link tells it the rest. */
            party_drop(embed, i, "ended its session");
    }
}

#else

static void party_barrier(struct ToriRSServerEmbed* embed) { (void)embed; }
static void party_flush(struct ToriRSServerEmbed* embed) { (void)embed; }

#endif /* EMBED_PARTY_SOCKETS */


/* Decode whatever this client sent and bring its world up if the handshake just
 * completed. Identical to the socket server's, deliberately: the world coming up
 * is not transport business, so both callers answer the same signal the same
 * way. */
static int
pump_client(
    struct ToriRSServerEmbed* embed,
    struct ToriRSServerEmbedClient* client)
{
    if( !ToriRSServer_SessionAlive(&client->session) )
        return 0;

    if( !ToriRSServer_SessionPump(&client->session, &embed->srv) )
        return 0;

    if( ToriRSServer_SessionTakeLogin(&client->session) )
    {
        struct ToriRSServerPlayer* player;
        struct ToriRSServerPlayer* evict;
        int claim = ToriRSServer_ClaimSlot(&embed->claims, &embed->srv, &client->session,
                                           ToriRSServer_EmbedNowMs(), &evict);

        if( evict )
        {
            /* The old session dies first, then the world lets go of its player
             * -- which writes the save this login is about to read. */
            for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
            {
                struct ToriRSServerEmbedClient* other = &embed->clients[i];

                if( !other->open || other->session.player != evict )
                    continue;
                ToriRSServer_SessionKill(&other->session);
                other->session.player = NULL;
                other->online = 0;
            }
            ToriRSServer_WorldRemovePlayer(&embed->srv, evict);
        }
        /* The client reads the closed link as a failed reconnect and logs in
         * with its password instead (ToriRS_Network_TakeResumeRefused). */
        if( claim == TORIRSSERVER_CLAIM_REFUSE )
        {
            ToriRSServer_SessionKill(&client->session);
            return 0;
        }

        ToriRSServer_ScriptsLoad(&embed->srv, embed->config.script_dir);
        ToriRSServer_WorldInit(&embed->srv, ToriRSServer_BootZone(embed->config.home_x),
                           ToriRSServer_BootZone(embed->config.home_z));
        player = claim == TORIRSSERVER_CLAIM_ANY_SLOT
                     ? ToriRSServer_WorldAddPlayer(&embed->srv, &client->session)
                     : ToriRSServer_WorldAddPlayerAt(&embed->srv, &client->session, claim);
        if( !player )
            return 0;
        client->session.player = player;
        ToriRSServer_WorldPlayerInit(player);
        ToriRSServer_WorldSetDisplayName(player, client->session.display_name);
        ToriRSServer_WorldLogin(player);
        client->online = 1;
        /* Anything sent behind the login block is still buffered. */
        if( !ToriRSServer_SessionPump(&client->session, &embed->srv) )
            return 0;
    }

    return ToriRSServer_SessionAlive(&client->session);
}

/*
 * TORIRS_SERVER_BREAKDOWN=<ms>: when one pump exceeds <ms>, say how much of it
 * was draining client input versus running the world tick. ToriRSServer_WorldTick
 * splits its own half by phase under the same switch. A host that shares its
 * frame thread with this pump -- the client does -- drops a frame for every
 * millisecond spent here, and the two halves have nothing in common, so
 * knowing which one ran long is the first question.
 */
static int g_pump_bd_ms = -1;

/*
 * Script cost inside the *client* half, from torirs_server_scripts.c.
 *
 * ToriRSServer_WorldTick zeroes these at its own start, so anything a packet
 * handler ran before the tick was overwritten before the tick reported. That
 * mattered: a click arrives as a packet, its `[opnpc]`/`[oploc]` trigger runs
 * here rather than in a phase, and a pump that spent 300 ms decoding input
 * looked like it spent it on nothing at all.
 */
extern uint64_t g_ToriRSServer_ScriptUs;
extern int g_ToriRSServer_ScriptRuns;
extern uint64_t g_ToriRSServer_ScriptSlowUs;
extern char g_ToriRSServer_ScriptSlowName[96];

static int
pump_bd_on(void)
{
    if( g_pump_bd_ms < 0 )
    {
        char const* v = getenv("TORIRS_SERVER_BREAKDOWN");
        g_pump_bd_ms = (v && v[0]) ? atoi(v) : 0;
    }
    return g_pump_bd_ms > 0;
}

static uint64_t
pump_bd_now_us(void)
{
    struct timespec ts;

    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

int
ToriRSServer_EmbedPump(
    struct ToriRSServerEmbed* embed,
    int run_tick)
{
    int any_alive = 0;
    int any_online = 0;
    int bd_on = pump_bd_on();
    uint64_t bd_t0 = bd_on ? pump_bd_now_us() : 0;
    uint64_t bd_clients = 0;
    uint64_t bd_tick = 0;
    uint64_t bd_script_us = 0;
    int bd_script_runs = 0;
    uint64_t bd_script_slow_us = 0;
    char bd_script_slow[96];

    bd_script_slow[0] = '\0';
    if( bd_on )
    {
        g_ToriRSServer_ScriptUs = 0;
        g_ToriRSServer_ScriptRuns = 0;
        g_ToriRSServer_ScriptSlowUs = 0;
        g_ToriRSServer_ScriptSlowName[0] = '\0';
    }

    /*
     * A party boundary: every member's input up to its READY goes into its
     * session now, so the loop below decodes it in client-id order ahead of
     * the tick, exactly where an in-process client's would be.
     */
    if( run_tick && embed->party_listener >= 0 )
        party_barrier(embed);

    /*
     * Every client's input first, then *one* tick. The tick is the world's, not
     * a client's: running it per client would advance the world N times per
     * 600 ms and give whoever is pumped first N moves to everyone else's one.
     */
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        struct ToriRSServerEmbedClient* client = &embed->clients[i];

        if( !client->open )
            continue;
        if( pump_client(embed, client) )
            any_alive = 1;
        if( client->online )
            any_online = 1;
    }

    if( bd_on )
    {
        bd_clients = pump_bd_now_us() - bd_t0;
        /* Snapshot before the tick, which zeroes these for its own accounting. */
        bd_script_us = g_ToriRSServer_ScriptUs;
        bd_script_runs = g_ToriRSServer_ScriptRuns;
        bd_script_slow_us = g_ToriRSServer_ScriptSlowUs;
        snprintf(bd_script_slow, sizeof(bd_script_slow), "%s", g_ToriRSServer_ScriptSlowName);
    }

    if( run_tick && any_online )
    {
        /* The bot runner's drive (torirs_server_botrun.c): an agent decides
         * for every player in this world before each tick. Started on the
         * first tick with a world -- here, in the process that hosts it, so a
         * party's leader and never its members. */
        if( !embed->botdrive_tried && getenv("TORIRS_BOTDRIVE_AGENT") && embed->srv.world_built )
        {
            embed->botdrive_tried = 1;
            embed->botdrive = ToriRSServer_BotDriveStart(&embed->srv, getenv("TORIRS_BOTDRIVE_AGENT"));
        }
        if( !embed->botdrive && embed->botdrive_request[0] && embed->srv.world_built )
        {
            embed->botdrive = ToriRSServer_BotDriveStart(&embed->srv, embed->botdrive_request);
            embed->botdrive_requested = 1;
            embed->botdrive_request[0] = '\0';
        }
        if( embed->botdrive )
            ToriRSServer_BotDriveStep(embed->botdrive);
        ToriRSServer_WorldTick(&embed->srv);
    }
    /* Even with no world yet: a member waiting at its boundary is released
     * by the TICK, built or not. */
    if( run_tick && embed->party_listener >= 0 )
        party_flush(embed);
    /* The boundary's tick, handed to this process's own client with the
     * tick's output -- what a member's TICK frame carries (party_flush). */
    if( run_tick )
        ToriRSServer_EmbedClientTickNote(embed->srv.world_built ? embed->srv.tick : -1);

    if( bd_on )
    {
        uint64_t total = pump_bd_now_us() - bd_t0;

        bd_tick = total - bd_clients;
        if( total >= (uint64_t)g_pump_bd_ms * 1000u )
        {
            fprintf(stderr,
                    "server_pump: total %.2f ms clients %.2f tick %.2f (tick_ran %d)"
                    " | client scripts %.2f x%d",
                    total / 1000.0, bd_clients / 1000.0, bd_tick / 1000.0,
                    run_tick && any_online, bd_script_us / 1000.0, bd_script_runs);
            if( bd_script_slow[0] )
                fprintf(stderr, " slowest %s %.2f", bd_script_slow,
                        bd_script_slow_us / 1000.0);
            fprintf(stderr, "\n");
        }
    }

    return any_alive;
}
