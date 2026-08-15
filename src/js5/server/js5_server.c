#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "js5_server.h"
#include <assert.h>

#include "js5_server_cache.h"
#include "js5_server_conn.h"
#include "js5_server_session.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET js5_server_socket_t;
#define JS5_SERVER_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int js5_server_socket_t;
#define JS5_SERVER_INVALID_SOCKET (-1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JS5_SERVER_IO_BYTES (16u * 1024u)
#define JS5_SERVER_WRITE_BUDGET (64u * 1024u)
#define JS5_SERVER_REJECT_SLOTS 1u
/* How often the served cache is checked for having changed on disk. Far below
 * any repack's duration, far above the cost of the two stat() calls it is. */
#define JS5_SERVER_REFRESH_INTERVAL_MS 1000u

#ifdef _WIN32
static volatile LONG g_js5_server_stop;
#else
static volatile sig_atomic_t g_js5_server_stop;
#endif

struct Js5ServerClient
{
    js5_server_socket_t socket;
    /* Session + framing. Socket-free by design, which is what lets io_server
     * host the same connection type inside its HTTP loop -- see
     * js5_server_conn.h and docs/WEB_SERVERS.md. */
    struct Js5ServerConn conn;
    char peer[INET_ADDRSTRLEN];
};

static uint64_t
js5_server_now_ms(void)
{
#ifdef _WIN32
    /* GetTickCount64 is newer than the Windows XP target. The server reactor
     * is single-threaded, so extending the 32-bit XP clock here is sufficient
     * to preserve monotonic timeout accounting across its 49-day wrap. */
    static DWORD previous_tick;
    static uint64_t tick_epoch;
    DWORD tick = GetTickCount();
    if( tick < previous_tick )
        tick_epoch += (uint64_t)1u << 32u;
    previous_tick = tick;
    return tick_epoch + (uint64_t)tick;
#else
    struct timespec now;
    if( clock_gettime(CLOCK_MONOTONIC, &now) != 0 )
        return 0u;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static bool
js5_server_parse_ipv4(const char* text, struct in_addr* address)
{
#ifdef _WIN32
    /* inet_pton is unavailable on XP. inet_addr is sufficient here because
     * the dedicated server deliberately accepts numeric IPv4 bind addresses. */
    unsigned long encoded = inet_addr(text);
    if( encoded == INADDR_NONE && strcmp(text, "255.255.255.255") != 0 )
        return false;
    address->s_addr = encoded;
    return true;
#else
    return inet_pton(AF_INET, text, address) == 1;
#endif
}

static void
js5_server_format_peer(
    const struct sockaddr_in* peer_address,
    char* peer,
    size_t peer_size)
{
#ifdef _WIN32
    /* inet_ntop is unavailable on XP. Copy inet_ntoa's static result before
     * the reactor performs another address conversion. */
    const char* text = inet_ntoa(peer_address->sin_addr);
    snprintf(peer, peer_size, "%s", text ? text : "peer");
#else
    if( !inet_ntop(AF_INET, &peer_address->sin_addr, peer, peer_size) )
        snprintf(peer, peer_size, "peer");
#endif
}

void
Js5ServerRequestStop(void)
{
#ifdef _WIN32
    InterlockedExchange(&g_js5_server_stop, 1);
#else
    g_js5_server_stop = 1;
#endif
}

static bool
js5_server_stopping(void)
{
#ifdef _WIN32
    return InterlockedCompareExchange(&g_js5_server_stop, 0, 0) != 0;
#else
    return g_js5_server_stop != 0;
#endif
}

void
Js5ServerConfigInit(struct Js5ServerConfig* config)
{
    assert(config);
    memset(config, 0, sizeof(*config));
    config->bind_address = "127.0.0.1";
    config->port = 43594u;
    config->revision = 239u;
    config->max_clients = 48u;
    config->backlog = 64u;
    config->max_pending_per_lane = 200u;
    config->handshake_timeout_ms = 10000u;
    config->idle_timeout_ms = 300000u;
    config->output_timeout_ms = 30000u;
}

static void
js5_server_socket_close(js5_server_socket_t socket)
{
    if( socket == JS5_SERVER_INVALID_SOCKET )
        return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

static int
js5_server_socket_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool
js5_server_would_block(int error)
{
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static bool
js5_server_interrupted(int error)
{
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

static bool
js5_server_set_nonblocking(js5_server_socket_t socket)
{
#ifdef _WIN32
    u_long enabled = 1u;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void
js5_server_client_reset(
    struct Js5ServerClient* client,
    bool verbose)
{
    assert(client);
    if( verbose && client->conn.session )
    {
        struct Js5ServerSessionStats stats;
        Js5ServerSessionGetStats(client->conn.session, &stats);
        fprintf(
            stderr,
            "js5_server: %s closed: requests=%llu responses=%llu bytes=%llu error=%d\n",
            client->peer[0] ? client->peer : "peer",
            (unsigned long long)stats.requests_received,
            (unsigned long long)stats.responses_served,
            (unsigned long long)stats.bytes_sent,
            (int)stats.error);
    }
    Js5ServerConn_Close(&client->conn);
    js5_server_socket_close(client->socket);
    memset(client, 0, sizeof(*client));
    client->socket = JS5_SERVER_INVALID_SOCKET;
}

static js5_server_socket_t
js5_server_open_listener(const struct Js5ServerConfig* config)
{
    js5_server_socket_t listener;
    struct sockaddr_in address;
    int one = 1;

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if( listener == JS5_SERVER_INVALID_SOCKET )
        return JS5_SERVER_INVALID_SOCKET;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(config->port);
    if( !js5_server_parse_ipv4(config->bind_address, &address.sin_addr) ||
        bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0 ||
        listen(listener, (int)config->backlog) != 0 ||
        !js5_server_set_nonblocking(listener) )
    {
        js5_server_socket_close(listener);
        return JS5_SERVER_INVALID_SOCKET;
    }
    return listener;
}

static int
js5_server_accept_clients(
    js5_server_socket_t listener,
    struct Js5ServerClient* clients,
    uint32_t regular_count,
    struct Js5ServerCache* cache,
    const struct Js5ServerConfig* config,
    uint64_t now_ms)
{
    for( ;; )
    {
        struct sockaddr_in peer_address;
#ifdef _WIN32
        int peer_size = sizeof(peer_address);
#else
        socklen_t peer_size = sizeof(peer_address);
#endif
        js5_server_socket_t socket =
            accept(listener, (struct sockaddr*)&peer_address, &peer_size);
        if( socket == JS5_SERVER_INVALID_SOCKET )
        {
            int error = js5_server_socket_error();
            if( js5_server_would_block(error) || js5_server_interrupted(error) )
                return 0;
            return -1;
        }
#ifndef _WIN32
        if( socket >= FD_SETSIZE )
        {
            js5_server_socket_close(socket);
            continue;
        }
#endif

        uint32_t slot = regular_count + JS5_SERVER_REJECT_SLOTS;
        bool reject = false;
        for( uint32_t i = 0u; i < regular_count; i++ )
            if( clients[i].socket == JS5_SERVER_INVALID_SOCKET )
            {
                slot = i;
                break;
            }
        if( slot == regular_count + JS5_SERVER_REJECT_SLOTS &&
            clients[regular_count].socket == JS5_SERVER_INVALID_SOCKET )
        {
            slot = regular_count;
            reject = true;
        }
        if( slot == regular_count + JS5_SERVER_REJECT_SLOTS ||
            !js5_server_set_nonblocking(socket) )
        {
            js5_server_socket_close(socket);
            continue;
        }

        int one = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
        struct Js5ServerSessionConfig session_config;
        Js5ServerSessionConfigInit(&session_config);
        session_config.revision = config->revision;
        session_config.max_pending_per_lane = config->max_pending_per_lane;
        session_config.handshake_timeout_ms = config->handshake_timeout_ms;
        session_config.idle_timeout_ms = config->idle_timeout_ms;
        session_config.output_timeout_ms = config->output_timeout_ms;
        session_config.server_full = reject;
        if( !Js5ServerConn_Open(&clients[slot].conn, cache, &session_config, now_ms) )
        {
            js5_server_socket_close(socket);
            continue;
        }
        clients[slot].socket = socket;
        js5_server_format_peer(
            &peer_address,
            clients[slot].peer,
            sizeof(clients[slot].peer));
        if( config->verbose )
            fprintf(
                stderr,
                "js5_server: %s connected%s\n",
                clients[slot].peer,
                reject ? " (capacity rejection)" : "");
    }
}

/*
 * Socket in, socket out. Everything between -- the framing decision, the
 * WebSocket handshake, deframing, and the session itself -- is
 * Js5ServerConn's, so this reactor is only responsible for the parts that
 * genuinely need a file descriptor.
 */
static int
js5_server_read_client(
    struct Js5ServerClient* client,
    uint64_t now_ms)
{
    uint8_t input[JS5_SERVER_IO_BYTES];

    for( unsigned iteration = 0u; iteration < 4u; iteration++ )
    {
        int received = recv(client->socket, (char*)input, (int)sizeof(input), 0);
        if( received > 0 )
        {
            if( Js5ServerConn_Feed(&client->conn, input, received, now_ms) != 0 )
                return -1;
            if( received < (int)sizeof(input) )
                return 0;
            continue;
        }
        if( received == 0 )
            return -1;
        int error = js5_server_socket_error();
        if( js5_server_would_block(error) )
            return 0;
        if( js5_server_interrupted(error) )
            continue;
        return -1;
    }
    return 0;
}

static int
js5_server_write_client(
    struct Js5ServerClient* client,
    uint64_t now_ms)
{
    size_t budget = JS5_SERVER_WRITE_BUDGET;

    while( budget > 0u )
    {
        const uint8_t* data;
        int size;
        int available = Js5ServerConn_TakeOutput(&client->conn, budget, &data, &size, now_ms);
        int sent;

        if( available < 0 )
            return -1;
        if( available == 0 )
            return 0;

        sent = send(client->socket, (const char*)data, size, 0);
        if( sent > 0 )
        {
            Js5ServerConn_Consumed(&client->conn, sent, now_ms);
            budget -= (size_t)sent < budget ? (size_t)sent : budget;
            continue;
        }
        if( sent == 0 )
            return -1;
        int error = js5_server_socket_error();
        if( js5_server_would_block(error) )
            return 0;
        if( js5_server_interrupted(error) )
            continue;
        return -1;
    }
    return 0;
}

int
Js5ServerRun(const struct Js5ServerConfig* config)
{
    struct Js5ServerCache* cache = NULL;
    struct Js5ServerClient* clients = NULL;
    js5_server_socket_t listener = JS5_SERVER_INVALID_SOCKET;
    uint32_t slot_count;
    uint64_t last_refresh_ms = 0u;
    int result = -1;
#ifdef _WIN32
    WSADATA winsock;
    bool winsock_started = false;
#endif

    if( !config || !config->cache_dir || !config->cache_dir[0] ||
        !config->bind_address || !config->bind_address[0] || config->port == 0u ||
        config->revision == 0u || config->max_clients == 0u ||
        config->max_clients >
            (uint32_t)(FD_SETSIZE - JS5_SERVER_REJECT_SLOTS - 1u) ||
        config->backlog == 0u || config->max_pending_per_lane == 0u )
    {
        fprintf(stderr, "js5_server: invalid configuration\n");
        return -1;
    }

#ifdef _WIN32
    if( WSAStartup(MAKEWORD(2, 2), &winsock) != 0 )
    {
        fprintf(stderr, "js5_server: WSAStartup failed\n");
        return -1;
    }
    winsock_started = true;
#endif
    cache = Js5ServerCacheOpen(config->cache_dir);
    if( !cache )
        goto cleanup;
    listener = js5_server_open_listener(config);
    if( listener == JS5_SERVER_INVALID_SOCKET )
    {
        fprintf(
            stderr,
            "js5_server: cannot bind %s:%u (socket error %d)\n",
            config->bind_address,
            (unsigned)config->port,
            js5_server_socket_error());
        goto cleanup;
    }
#ifndef _WIN32
    if( listener >= FD_SETSIZE )
    {
        fprintf(stderr, "js5_server: listener descriptor exceeds select() capacity\n");
        goto cleanup;
    }
#endif

    slot_count = config->max_clients + JS5_SERVER_REJECT_SLOTS;
    clients = (struct Js5ServerClient*)calloc(slot_count, sizeof(*clients));
    if( !clients )
        goto cleanup;
    for( uint32_t i = 0u; i < slot_count; i++ )
        clients[i].socket = JS5_SERVER_INVALID_SOCKET;

#ifdef _WIN32
    InterlockedExchange(&g_js5_server_stop, 0);
#else
    g_js5_server_stop = 0;
#endif
    fprintf(
        stdout,
        "READY %s %u %u\n",
        config->bind_address,
        (unsigned)config->port,
        (unsigned)config->revision);
    fflush(stdout);

    result = 0;
    last_refresh_ms = js5_server_now_ms();
    while( !js5_server_stopping() )
    {
        fd_set readable;
        fd_set writable;
        struct timeval timeout = { 0, 100000 };
#ifndef _WIN32
        js5_server_socket_t max_socket = listener;
#endif
        uint64_t now_ms = js5_server_now_ms();

        /*
         * Notice the served cache changing underneath us.
         *
         * Throttled because it is a syscall on a loop that also runs on a
         * 100ms select timeout; a second is far below any repack's duration and
         * far above the cost of two stat() calls. The reload happens here,
         * between selects, rather than inside a request — a session's blobs are
         * owned copies, so nothing in flight is torn by it.
         */
        if( now_ms - last_refresh_ms >= JS5_SERVER_REFRESH_INTERVAL_MS )
        {
            last_refresh_ms = now_ms;
            Js5ServerCacheRefreshIfStale(cache);
        }

        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_SET(listener, &readable);
        for( uint32_t i = 0u; i < slot_count; i++ )
        {
            const uint8_t* output;
            size_t output_size;
            if( clients[i].socket == JS5_SERVER_INVALID_SOCKET )
                continue;
            Js5ServerSessionTick(clients[i].conn.session, now_ms);
            if( Js5ServerSessionWantsClose(clients[i].conn.session) )
            {
                js5_server_client_reset(&clients[i], config->verbose);
                continue;
            }
            FD_SET(clients[i].socket, &readable);
            /* Already-framed bytes count too. A half-written frame, or a
             * handshake response with no session output behind it yet, would
             * otherwise sit in the buffer until the client happened to send
             * something — which for the 101 response is a browser waiting
             * forever for a handshake the server has already computed. */
            if( Js5ServerConn_HasPendingOutput(&clients[i].conn) ||
                Js5ServerSessionPeekOutput(
                    clients[i].conn.session, &output, &output_size) > 0 )
                FD_SET(clients[i].socket, &writable);
#ifndef _WIN32
            if( clients[i].socket > max_socket )
                max_socket = clients[i].socket;
#endif
        }

        int ready = select(
#ifdef _WIN32
            0,
#else
            (int)max_socket + 1,
#endif
            &readable,
            &writable,
            NULL,
            &timeout);
        if( ready < 0 )
        {
            int error = js5_server_socket_error();
            if( js5_server_interrupted(error) )
                continue;
            fprintf(stderr, "js5_server: select failed (socket error %d)\n", error);
            result = -1;
            break;
        }

        now_ms = js5_server_now_ms();
        if( FD_ISSET(listener, &readable) &&
            js5_server_accept_clients(
                listener, clients, config->max_clients, cache, config, now_ms) != 0 )
        {
            fprintf(stderr, "js5_server: accept failed\n");
            result = -1;
            break;
        }
        for( uint32_t i = 0u; i < slot_count; i++ )
        {
            if( clients[i].socket == JS5_SERVER_INVALID_SOCKET )
                continue;
            if( FD_ISSET(clients[i].socket, &readable) &&
                js5_server_read_client(&clients[i], now_ms) != 0 )
            {
                js5_server_client_reset(&clients[i], config->verbose);
                continue;
            }
            if( clients[i].socket != JS5_SERVER_INVALID_SOCKET &&
                FD_ISSET(clients[i].socket, &writable) &&
                js5_server_write_client(&clients[i], now_ms) != 0 )
                js5_server_client_reset(&clients[i], config->verbose);
        }
    }

cleanup:
    if( clients )
    {
        uint32_t count = config->max_clients + JS5_SERVER_REJECT_SLOTS;
        for( uint32_t i = 0u; i < count; i++ )
            js5_server_client_reset(&clients[i], config->verbose);
    }
    free(clients);
    js5_server_socket_close(listener);
    Js5ServerCacheFree(cache);
#ifdef _WIN32
    if( winsock_started )
        WSACleanup();
#endif
    return result;
}
