/*
 * A lost link does not end the session: the socket host keeps the player in
 * the world, and a reconnect takes the same player back.
 *
 * A phone that hides the page loses its socket within seconds, and the host
 * used to answer every closed socket with a logout -- `[logout]`, the save,
 * the slot freed -- so the reconnect that followed built a new player from the
 * save and ran `[login]` again: logged out much sooner than the idle timeout,
 * and whatever the save does not hold (queues, the action in progress, an
 * encounter) gone. The reference keeps an x-logged player in the world and a
 * reconnect re-attaches the new socket to it (World.processLogouts, World.ts
 * "reconnect a new socket with player in the world"); so does this host now
 * (ToriRSServer_WorldDetachPlayer / ToriRSServer_WorldAttachPlayer), and only
 * the idle logout ends the session (TORIRSSERVER_IDLE_LOGOUT_TICKS).
 *
 * The real server binary on a loopback port, with its own account server and
 * a throwaway saves directory, against the real client login machine over a
 * real TCP socket: the host's connection teardown is the thing under test, and
 * the embedded transport (reconnect_claim_test.c) never reaches it. What the
 * server did is read off its own log.
 *
 * Run: make -C src test-torirsserver-host-reattach
 */

#include "torirsserver/torirs_server_session.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/rev_list.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST_PORT 43651

static int g_failures;
static char g_log_path[512];

static void
check(
    int condition,
    const char* what)
{
    printf("host-reattach: %-62s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

static long
now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

/* How many lines of the server's log contain `needle`. */
static int
log_count(const char* needle)
{
    char line[4096];
    int count = 0;
    FILE* file = fopen(g_log_path, "r");

    if( !file )
        return 0;
    while( fgets(line, sizeof(line), file) )
        if( strstr(line, needle) )
            count++;
    fclose(file);
    return count;
}

/* Wait for the log to say `needle` at least `want` times. */
static int
log_wait(
    const char* needle,
    int want,
    long timeout_ms)
{
    long until = now_ms() + timeout_ms;

    while( now_ms() < until )
    {
        if( log_count(needle) >= want )
            return 1;
        usleep(50 * 1000);
    }
    return log_count(needle) >= want;
}

static int
dial(void)
{
    struct sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if( fd < 0 )
        return -1;
    if( connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        close(fd);
        return -1;
    }
    return fd;
}

struct Client
{
    struct ToriRS_Network net;
    int fd;
    int rebuilds;
    int stats;
    int varp_resets;
    int logouts;
};

static int32_t g_seed_next[4];

static void
seed_from_next(
    void* user,
    int32_t* seed)
{
    (void)user;
    memcpy(seed, g_seed_next, sizeof(g_seed_next));
}

static void
client_init(
    struct Client* client,
    int32_t first_word)
{
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    ToriRS_Network_Init(&client->net, GameProtoRev_OSRS242(), TORIRSSERVER_RSA_PUBLIC_EXPONENT,
                        TORIRSSERVER_RSA_PUBLIC_MODULUS);
    g_seed_next[0] = first_word;
    g_seed_next[1] = first_word ^ 0x5a5a5a5a;
    g_seed_next[2] = first_word + 0x01010101;
    g_seed_next[3] = ~first_word;
    ToriRS_Network_SetSeedFn(&client->net, seed_from_next, NULL);
}

static void
client_connected(struct Client* client)
{
    uint8_t status = TORIRS_NET_STATUS_CONNECTED;

    client->fd = dial();
    if( client->fd >= 0 )
        fcntl(client->fd, F_SETFL, fcntl(client->fd, F_GETFL, 0) | O_NONBLOCK);
    ToriRS_Network_HandleCmd(&client->net, TORIRS_CMD_NET_STATUS, &status, 1);
}

/* The link goes the way a hidden phone tab's does: the socket closes, no
 * logout is asked for, and the client sees its connection gone. */
static void
client_drop(struct Client* client)
{
    uint8_t status = TORIRS_NET_STATUS_DISCONNECTED;

    if( client->fd >= 0 )
        close(client->fd);
    client->fd = -1;
    ToriRS_Network_HandleCmd(&client->net, TORIRS_CMD_NET_STATUS, &status, 1);
}

/* One round of bytes both ways, and the packets the client parsed. A
 * REBUILD is acknowledged as the client's world builder would. */
static void
client_pump(struct Client* client)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[65536];
    static uint8_t inbound[65536];
    struct RevPacket packet;

    while( ToriRS_Network_PopOut(&client->net, &header, payload) )
    {
        if( header.type != TORIRS_NET_OUT_SEND_DATA || client->fd < 0 )
            continue;
        for( int off = 0; off < (int)header.length; )
        {
            ssize_t n = write(client->fd, payload + off, header.length - off);

            if( n > 0 )
                off += (int)n;
            else if( n < 0 && errno != EAGAIN && errno != EINTR )
                break;
        }
    }
    if( client->fd >= 0 )
    {
        struct pollfd pfd = { client->fd, POLLIN, 0 };

        if( poll(&pfd, 1, 20) > 0 )
        {
            ssize_t got = read(client->fd, inbound, sizeof(inbound));

            if( got > 0 )
                ToriRS_Network_HandleCmd(&client->net, TORIRS_CMD_NET_RECV, inbound, (int)got);
            else if( got == 0 )
                client_drop(client);
        }
    }
    while( ToriRS_Network_PopPacket(&client->net, &packet) )
    {
        if( packet.packet_type == PKT_NAME_REBUILD_NORMAL ||
            packet.packet_type == PKT_NAME_REBUILD_REGION )
        {
            uint8_t buf[64];
            int len;

            client->rebuilds++;
            len = net_out_map_build_complete(client->net.rev, client->net.random_out, buf,
                                             (int)sizeof(buf));
            if( len > 0 )
                ToriRS_Network_SendRaw(&client->net, buf, len);
        }
        if( packet.packet_type == PKT_NAME_UPDATE_STAT )
            client->stats++;
        if( packet.packet_type == PKT_NAME_VARP_RESET )
            client->varp_resets++;
        if( packet.packet_type == PKT_NAME_LOGOUT )
            client->logouts++;
    }
}

/* Pump until the client is in game and the state burst has arrived. */
static int
client_until_game(
    struct Client* client,
    long timeout_ms)
{
    long until = now_ms() + timeout_ms;

    while( now_ms() < until )
    {
        client_pump(client);
        if( client->net.state == TORIRS_NET_GAME && client->stats >= 23 )
            return 1;
        if( client->net.state == TORIRS_NET_DISCONNECTED && client->fd < 0 )
            return 0;
    }
    return 0;
}

static void
client_settle(
    struct Client* client,
    long ms)
{
    long until = now_ms() + ms;

    while( now_ms() < until )
        client_pump(client);
}

static pid_t
server_start(
    const char* binary,
    const char* saves)
{
    pid_t pid = fork();

    if( pid == 0 )
    {
        int fd = open(g_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        char port[16];

        dup2(fd, 1);
        dup2(fd, 2);
        setenv("TORIRSSERVER_SAVES", saves, 1);
        setenv("TORIRSSERVER_WORLD", "reattach-test:43651", 1);
        setenv("TORIRSSERVER_BIND", "127.0.0.1", 1);
        /* 12 s instead of five minutes; every wait above it stays well under. */
        setenv("TORIRSSERVER_IDLE_LOGOUT_TICKS", "20", 1);
        snprintf(port, sizeof(port), "%d", TEST_PORT);
        execl(binary, binary, port, "--rev", "osrs242", (char*)NULL);
        _exit(127);
    }
    return pid;
}

int
main(
    int argc,
    char** argv)
{
    const char* binary = argc > 1 ? argv[1] : "src/build_opt/torirsserver";
    const char* scratch = argc > 2 ? argv[2] : "src/build_opt";
    char saves[512];
    struct Client a;
    struct Client fresh;
    pid_t server;
    int up = 0;

    signal(SIGPIPE, SIG_IGN);
    snprintf(saves, sizeof(saves), "%s/host_reattach_saves_%d", scratch, (int)getpid());
    mkdir(saves, 0755);
    snprintf(g_log_path, sizeof(g_log_path), "%s/server.log", saves);
    server = server_start(binary, saves);
    check(server > 0, "the server started");
    for( long until = now_ms() + 240000; now_ms() < until && !up; usleep(200 * 1000) )
    {
        int fd = dial();

        if( fd >= 0 )
        {
            close(fd);
            up = 1;
        }
    }
    check(up, "the server is listening");
    if( !up )
        goto done;

    /* 1. A login, all the way into the game. */
    client_init(&a, 0x11223344);
    ToriRS_Network_ConnectLogin(&a.net, "127.0.0.1", "reattach", "Tabhidden-7q");
    client_connected(&a);
    check(client_until_game(&a, 60000), "the login reached the game and its state burst");
    check(log_wait("reattach logged in (pid", 1, 5000), "the server seated the character");
    client_settle(&a, 1500);

    /* 2. The tab is hidden: the socket goes, nobody asked to log out. */
    client_drop(&a);
    check(log_wait("lost its connection", 1, 5000), "a closed socket detaches the player");
    usleep(3000 * 1000);
    check(log_count("reattach logged out") == 0, "and does not log it out");

    /* 3. The tab comes back: the same client reconnects with its key. */
    a.stats = 0;
    a.rebuilds = 0;
    a.varp_resets = 0;
    check(ToriRS_Network_Reconnect(&a.net), "the client redials");
    client_connected(&a);
    check(client_until_game(&a, 60000), "the reconnect reached the game again");
    check(log_wait("reattach re-attached (pid", 1, 5000), "the server re-attached the player");
    check(log_count("reattach logged in (pid") == 1, "nothing was seated a second time");
    check(a.rebuilds >= 1, "the scene was rebuilt for the client");
    check(a.varp_resets == 1, "the varps were restated from a VARP_RESET");
    client_settle(&a, 1500);

    /* 4. A reconnect that beats the old socket's close: the old connection
     *    lets go, the player is not removed. */
    {
        struct Client b = a; /* the same seed, the same index */
        int before = log_count("re-attached (pid");

        b.fd = -1;
        b.stats = 0;
        check(ToriRS_Network_Reconnect(&b.net), "a second connection redials with the key");
        client_connected(&b);
        check(client_until_game(&b, 60000), "and reaches the game");
        check(log_wait("re-attached (pid", before + 1, 5000), "it took the same player");
        check(log_count("reattach logged out") == 0, "which never left the world");
        if( a.fd >= 0 )
            close(a.fd);
        a = b;
    }

    /* 5. A reloaded page: a fresh client presenting the key it was handed
     *    holds no UI, so it gets a whole login, not a resync. */
    {
        int32_t seed[4];
        int index = a.net.local_index;

        memcpy(seed, a.net.prev_seed, sizeof(seed));
        client_init(&fresh, 0x55667788);
        check(ToriRS_Network_ArmResume(&fresh.net, seed, index), "a fresh boot arms the resume");
        ToriRS_Network_ConnectLogin(&fresh.net, "127.0.0.1", "reattach", "Tabhidden-7q");
        client_connected(&fresh);
        check(client_until_game(&fresh, 60000), "the fresh boot reached the game");
        check(log_wait("reattach logged in (pid", 2, 5000),
              "and was given a login, the character's save loaded");
        if( a.fd >= 0 )
            close(a.fd);
        client_settle(&fresh, 1000);
    }

    /* 6. The idle logout, connected: no input for the idle clock and the
     *    client is told LOGOUT, the player removed rather than detached. */
    {
        long until = now_ms() + 30000;

        while( now_ms() < until && !fresh.logouts )
            client_pump(&fresh);
        check(fresh.logouts == 1, "an idle connected client is sent LOGOUT");
        check(log_wait("reattach idle for", 1, 2000), "the server says why");
        client_settle(&fresh, 1500);
        check(log_count("reattach lost its connection") == 2,
              "and its socket's close did not detach it");
        if( fresh.fd >= 0 )
            close(fresh.fd);
    }

    /* 7. The idle logout, detached: the same clock, with no client. The key
     *    that would have taken it back is refused afterwards. */
    {
        int32_t seed[4];
        int index;

        client_init(&a, 0x0badf00d);
        ToriRS_Network_ConnectLogin(&a.net, "127.0.0.1", "reattach", "Tabhidden-7q");
        client_connected(&a);
        check(client_until_game(&a, 60000), "a new login reached the game");
        client_settle(&a, 1000);
        memcpy(seed, a.net.prev_seed, sizeof(seed));
        index = a.net.local_index;
        client_drop(&a);
        check(log_wait("lost its connection", 3, 5000), "its dropped socket detaches it");
        check(log_wait("reattach logged out (idle", 1, 30000),
              "the idle clock logs the detached player out");
        client_init(&fresh, 0x13572468);
        ToriRS_Network_ArmResume(&fresh.net, seed, index);
        /* A same-tab reconnect: as ToriRS_Network_Reconnect sends it. */
        fresh.net.resume_in_flight = 0;
        fresh.net.resume_armed = 0;
        memcpy(fresh.net.username, "reattach", 9);
        memcpy(fresh.net.host, "127.0.0.1", 10);
        check(ToriRS_Network_Reconnect(&fresh.net), "the tab redials after the idle logout");
        client_connected(&fresh);
        check(!client_until_game(&fresh, 10000), "and is refused: the session ended");
        check(log_wait("has no session to reclaim", 1, 2000), "the server says so");
    }

done:
    if( server > 0 )
    {
        int status = 0;

        kill(server, SIGTERM);
        waitpid(server, &status, 0);
        check(log_count("shutting down") == 1, "the server shut down cleanly");
    }
    if( g_failures )
    {
        printf("host-reattach: %d failure(s); server log in %s\n", g_failures, g_log_path);
        return 1;
    }
    printf("host-reattach: all checks passed\n");
    return 0;
}
