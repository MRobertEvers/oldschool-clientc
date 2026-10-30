/*
 * The game server's control link and the live reload (torirs_server_control.h).
 *
 * COMPILED INTO torirs_server_ws.c (an #include at its end), not as its own
 * unit: the server's source lists live in src/makefile, which this change may
 * not edit, and the connection layer is the file whose first-byte select hands
 * a control connection over. It also shares that file's socket portability
 * (winsock names, sock_would_block). integrate-4 may move it to
 * TORIRSSERVER_CORE_SRCS and drop the #include; nothing here depends on being
 * in that unit beyond the helpers named above.
 *
 * One table of connections for the process, file-static: a connection is
 * adopted by the connection layer before any world is in hand, and the first
 * world to tick claims it. A process runs one world that accepts sockets.
 */

#include "torirs_server.h"
#include "torirs_server_auth.h"
#include "torirs_server_boot.h"
#include "torirs_server_control.h"
#include "torirs_server_servpack.h"
#include "torirs_server_varps.h"
#include "torirs_server_wire.h"
#include "net/rev/pktnames.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* Server-side reload of config kinds, by the kinds whose tables can be swapped
 * live: their records are looked up by id at every use, never copied into or
 * pointed at by world state. Declared in torirs_server.h with their loaders. */
#include <datatypes/dat2_configs.h>

enum
{
    CTL_CONN_MAX = 8,
    CTL_SUBSCRIBE_MAX = 256,
};

struct CtlConn
{
    int fd;
    int gated;
    int hello_ok;
    int closing;
    uint8_t* in;
    int in_len;
    int in_cap;
    uint8_t* out;
    int out_len;
    int out_cap;
    /* CTL_SUBSCRIBE: one player's varps, and the values last sent. */
    char sub_player[64];
    int sub_count;
    int sub_ids[CTL_SUBSCRIBE_MAX];
    int32_t sub_values[CTL_SUBSCRIBE_MAX];
    int sub_primed;
};

static struct CtlConn* g_ctl[CTL_CONN_MAX];
static int g_ctl_count;

/* ------------------------------------------------------------------ */
/* Bytes                                                               */
/* ------------------------------------------------------------------ */

static void
ctl_out_reserve(
    struct CtlConn* c,
    int more)
{
    if( c->out_len + more <= c->out_cap )
        return;
    while( c->out_cap < c->out_len + more )
        c->out_cap = c->out_cap ? c->out_cap * 2 : 4096;
    c->out = ToriMem_Realloc(ToriMem_ServerNet(), c->out, (size_t)c->out_cap);
    assert(c->out);
}

static void
ctl_put_u32(
    uint8_t* at,
    uint32_t value)
{
    at[0] = (uint8_t)(value & 0xff);
    at[1] = (uint8_t)((value >> 8) & 0xff);
    at[2] = (uint8_t)((value >> 16) & 0xff);
    at[3] = (uint8_t)((value >> 24) & 0xff);
}

static uint32_t
ctl_get_u32(const uint8_t* at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

/* A frame being built: the header is back-patched by ctl_frame_end. */
struct CtlFrame
{
    struct CtlConn* conn;
    int start;
};

static struct CtlFrame
ctl_frame_begin(
    struct CtlConn* c,
    uint32_t type)
{
    struct CtlFrame frame;

    ctl_out_reserve(c, 8);
    frame.conn = c;
    frame.start = c->out_len;
    ctl_put_u32(c->out + c->out_len, type);
    ctl_put_u32(c->out + c->out_len + 4, 0);
    c->out_len += 8;
    return frame;
}

static void
ctl_frame_u32(
    struct CtlFrame* frame,
    uint32_t value)
{
    ctl_out_reserve(frame->conn, 4);
    ctl_put_u32(frame->conn->out + frame->conn->out_len, value);
    frame->conn->out_len += 4;
}

static void
ctl_frame_str(
    struct CtlFrame* frame,
    const char* text)
{
    size_t length = text ? strlen(text) : 0;

    ctl_frame_u32(frame, (uint32_t)length);
    ctl_out_reserve(frame->conn, (int)length);
    if( length )
        memcpy(frame->conn->out + frame->conn->out_len, text, length);
    frame->conn->out_len += (int)length;
}

static void
ctl_frame_end(struct CtlFrame* frame)
{
    ctl_put_u32(frame->conn->out + frame->start + 4,
                (uint32_t)(frame->conn->out_len - frame->start - 8));
}

/* A request's payload, read front to back. Any read past the end marks it bad
 * and reads zeros: a malformed frame closes the link (ctl_dispatch). */
struct CtlReader
{
    const uint8_t* data;
    uint32_t length;
    uint32_t at;
    int bad;
};

static uint32_t
ctl_read_u32(struct CtlReader* r)
{
    uint32_t value;

    if( r->bad || r->length - r->at < 4 )
    {
        r->bad = 1;
        return 0;
    }
    value = ctl_get_u32(r->data + r->at);
    r->at += 4;
    return value;
}

/* A string into `out` (cut to `cap`). */
static void
ctl_read_str(
    struct CtlReader* r,
    char* out,
    size_t cap)
{
    uint32_t length = ctl_read_u32(r);
    size_t kept;

    out[0] = '\0';
    if( r->bad || r->length - r->at < length )
    {
        r->bad = 1;
        return;
    }
    kept = length < cap - 1 ? length : cap - 1;
    memcpy(out, r->data + r->at, kept);
    out[kept] = '\0';
    r->at += length;
}

/* ------------------------------------------------------------------ */
/* Reload results                                                      */
/* ------------------------------------------------------------------ */

void
ToriRSServer_ReloadLine(
    struct ToriRSServerReloadResult* result,
    const char* fmt,
    ...)
{
    char line[TORIRSSERVER_RELOAD_LINE_MAX];
    va_list args;

    assert(result);
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    fprintf(stderr, "torirsserver: reload: %s\n", line);
    if( result->line_count < TORIRSSERVER_RELOAD_LINES_MAX )
        snprintf(result->lines[result->line_count++], TORIRSSERVER_RELOAD_LINE_MAX, "%s", line);
}

void
ToriRSServer_ReloadRestart(
    struct ToriRSServerReloadResult* result,
    const char* fmt,
    ...)
{
    char line[TORIRSSERVER_RELOAD_LINE_MAX];
    va_list args;

    assert(result);
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    fprintf(stderr, "torirsserver: reload: restart needed: %s\n", line);
    for( int i = 0; i < result->restart_count; i++ )
        if( strcmp(result->restart[i], line) == 0 )
            return;
    if( result->restart_count < TORIRSSERVER_RELOAD_RESTART_MAX )
        snprintf(result->restart[result->restart_count++], TORIRSSERVER_RELOAD_LINE_MAX, "%s",
                 line);
}

/* ------------------------------------------------------------------ */
/* CACHE_INVALIDATE                                                    */
/* ------------------------------------------------------------------ */

int
ToriRSServer_CacheInvalidateEncode(
    int index,
    const int* archives,
    int count,
    uint8_t* out)
{
    int len = 0;

    assert(index >= 0);
    assert(index < 255);
    assert(count >= 0);
    assert(count <= TORIRSSERVER_CACHE_INVALIDATE_PER_PACKET);
    assert(out);
    if( count > 0 )
        assert(archives);
    out[len++] = (uint8_t)index;
    out[len++] = (uint8_t)((count >> 8) & 0xff);
    out[len++] = (uint8_t)(count & 0xff);
    for( int i = 0; i < count; i++ )
    {
        uint32_t archive = (uint32_t)archives[i];

        out[len++] = (uint8_t)(archive >> 24);
        out[len++] = (uint8_t)(archive >> 16);
        out[len++] = (uint8_t)(archive >> 8);
        out[len++] = (uint8_t)archive;
    }
    return len;
}

int
ToriRSServer_SendCacheInvalidate(
    struct ToriRSServerPlayer* player,
    int index,
    const int* archives,
    int count)
{
    static uint8_t payload[3 + 4 * TORIRSSERVER_CACHE_INVALIDATE_PER_PACKET];
    const struct ToriRSServerWire* wire;
    int sent = 0;

    assert(player);
    assert(player->world);
    assert(index >= 0);
    assert(index < 255);
    if( count <= 0 )
        return 1;
    assert(archives);
    wire = player->world->wire ? player->world->wire : ToriRSServer_WireDefault();
    /* Our own prot: a revision that does not carry it (230) is told nothing,
     * and sees the change at its next login, as an old client would. */
    if( ToriRSServer_WireOpcode(wire, PKT_NAME_CACHE_INVALIDATE) < 0 )
        return 0;
    while( sent < count )
    {
        int n = count - sent < TORIRSSERVER_CACHE_INVALIDATE_PER_PACKET
                    ? count - sent
                    : TORIRSSERVER_CACHE_INVALIDATE_PER_PACKET;
        int len = ToriRSServer_CacheInvalidateEncode(index, archives + sent, n, payload);

        ToriRSServer_Send(player, PKT_NAME_CACHE_INVALIDATE, payload, len, 2);
        sent += n;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The reload                                                          */
/* ------------------------------------------------------------------ */

/* The config kinds a reload replaces live (ToriRSServer_ServPackReloadGroups'
 * callback). Each of these tables is looked up by id at every use and its
 * loader frees the old table first. Everything else -- npc, loc and obj
 * definitions (live npcs point at their def, content.c's ContentLoadPack), the
 * param/enum/varp/inv tables content reads as text, varbits (the bank's
 * ranges are derived from them at boot) -- answers 0: restart. */
static int
ctl_kind_reload(
    void* context,
    struct RSCache_ServerPack* pack,
    int config_kind)
{
    (void)context;
    if( config_kind == RSCACHE_DAT2_CONFIG_KIND_SEQUENCE )
        return ToriRSServer_SeqInfoLoad(pack) < 0 ? -1 : 1;
    if( config_kind == RSCACHE_DAT2_CONFIG_KIND_HEALTHBAR )
        return ToriRSServer_HealthbarInfoLoad(pack) < 0 ? -1 : 1;
    if( config_kind == RSCACHE_DAT2_CONFIG_KIND_STRUCT )
        return ToriRSServer_StructInfoLoad(pack) < 0 ? -1 : 1;
    return 0;
}

/* The script pack directory a RELOAD names: the directory of a manifest
 * file, or the path itself when it is a directory (or does not exist, which
 * the load then reports). */
static void
ctl_pack_dir(
    const char* path,
    char* out,
    size_t cap)
{
    struct stat st;
    const char* slash;

    snprintf(out, cap, "%s", path);
    if( stat(path, &st) != 0 || !S_ISREG(st.st_mode) )
        return;
    slash = strrchr(path, '/');
    if( !slash )
    {
        snprintf(out, cap, "%s", ".");
        return;
    }
    snprintf(out, cap, "%.*s", (int)(slash - path), path);
}

void
ToriRSServer_Reload(
    struct ToriRSServer* srv,
    const struct ToriRSServerReloadRequest* request,
    struct ToriRSServerReloadResult* result)
{
    int failed = 0;
    int client_rows = 0;

    assert(srv);
    assert(request);
    assert(result);
    memset(result, 0, sizeof(*result));

    /* 1. Scripts: shadow, check, swap; the old table lives while anything is
     *    parked on it. */
    if( request->script_dir[0] )
    {
        char why[TORIRSSERVER_RELOAD_LINE_MAX];
        char dir[TORIRSSERVER_RELOAD_PATH_MAX];
        int changed = 0;

        /* The wire names the pack's MANIFEST (section 4: script_manifest_path,
         * `<pack>/pack.manifest`); its directory is the pack. A directory is
         * taken as the pack itself. */
        ctl_pack_dir(request->script_dir, dir, sizeof(dir));
        if( ToriRSServer_ScriptsReload(srv, dir, &changed, why, sizeof(why)) )
        {
            result->scripts_swapped = changed;
            ToriRSServer_ReloadLine(result, "scripts: %d changed, swapped in from %s", changed, dir);
        }
        else
        {
            failed = 1;
            ToriRSServer_ReloadLine(result, "scripts: not swapped: %s", why);
        }
    }

    /* 2. Server-pack config groups. */
    {
        int* groups = NULL;
        int group_count = 0;

        for( int i = 0; i < request->group_count; i++ )
        {
            if( request->groups[i].index != TORIRSSERVER_RELOAD_SERVPACK_INDEX )
            {
                client_rows++;
                continue;
            }
            if( !groups )
            {
                groups = ToriMem_Alloc(ToriMem_ServerNet(),
                                       (size_t)request->group_count * 2 * sizeof(int));
                assert(groups);
            }
            groups[2 * group_count] = request->groups[i].archive;
            groups[2 * group_count + 1] = 0;
            group_count++;
        }
        if( group_count > 0 )
        {
            char pack_dir[1024];
            int replaced;

            ToriRSServer_ServPackDir(ToriRSServer_BootContentDir(), ToriRSServer_WorldCacheDir(),
                                     pack_dir, sizeof(pack_dir));
            replaced = ToriRSServer_ServPackReloadGroups(pack_dir, groups, group_count,
                                                         ctl_kind_reload, srv, result);
            if( replaced < 0 )
            {
                failed = 1;
                ToriRSServer_ReloadLine(result, "config: the server pack at %s does not open",
                                        pack_dir);
            }
            else
            {
                result->groups_replaced = replaced;
                ToriRSServer_ReloadLine(result, "config: %d of %d server-pack group(s) replaced",
                                        replaced, group_count);
            }
        }
        ToriMem_Free(ToriMem_ServerNet(), groups);
    }

    /* 3. Map squares: the store re-reads them, covering players are owed a
     *    REBUILD (sent this tick, after the invalidation below). */
    if( request->square_count > 0 )
    {
        int xs[TORIRSSERVER_RELOAD_SQUARES_MAX];
        int zs[TORIRSSERVER_RELOAD_SQUARES_MAX];

        for( int i = 0; i < request->square_count; i++ )
        {
            xs[i] = request->squares[i].map_x;
            zs[i] = request->squares[i].map_z;
        }
        result->squares_reloaded =
            ToriRSServer_WorldReloadSquares(srv, xs, zs, request->square_count);
        ToriRSServer_ReloadLine(result, "squares: %d of %d current", result->squares_reloaded,
                                request->square_count);
    }

    /* 4. The edited lane's spawns. */
    if( request->spawn_count > 0 )
    {
        const char* files[TORIRSSERVER_RELOAD_SPAWNS_MAX];
        char scripts_dir[1100];

        for( int i = 0; i < request->spawn_count; i++ )
            files[i] = request->spawn_files[i];
        snprintf(scripts_dir, sizeof(scripts_dir), "%s/server/scripts",
                 ToriRSServer_BootContentDir());
        result->spawns_changed = ToriRSServer_WorldReloadSpawns(srv, scripts_dir, files,
                                                                request->spawn_count, result);
        ToriRSServer_ReloadLine(result, "spawns: %d added or removed", result->spawns_changed);
    }

    /* 5. The client cache: every logged-in client forgets the rewritten
     *    archives, index by index. Before the REBUILD phase_clients_out sends,
     *    so a client drops its copy of a square before it is told to load it. */
    if( client_rows > 0 )
    {
        int* archives = ToriMem_Alloc(ToriMem_ServerNet(), (size_t)client_rows * sizeof(int));
        uint8_t seen[255];

        assert(archives);
        memset(seen, 0, sizeof(seen));
        for( int i = 0; i < request->group_count; i++ )
        {
            int index = request->groups[i].index;
            int n = 0;
            struct ToriRSServerPlayer* player;

            if( index == TORIRSSERVER_RELOAD_SERVPACK_INDEX || index < 0 || index >= 255 ||
                seen[index] )
                continue;
            seen[index] = 1;
            for( int j = i; j < request->group_count; j++ )
                if( request->groups[j].index == index )
                    archives[n++] = request->groups[j].archive;
            for( int p = 0; p < srv->player_count; p++ )
            {
                player = &srv->players[p];
                if( !player->active )
                    continue;
                if( ToriRSServer_SendCacheInvalidate(player, index, archives, n) )
                    result->clients_invalidated++;
            }
        }
        ToriMem_Free(ToriMem_ServerNet(), archives);
        ToriRSServer_ReloadLine(result, "client cache: %d archive(s) invalidated, %d send(s)",
                                client_rows, result->clients_invalidated);
    }

    if( failed )
        result->status = TORIRSSERVER_RELOAD_FAILED;
    else if( result->restart_count > 0 )
        result->status = TORIRSSERVER_RELOAD_RESTART_NEEDED;
    else
        result->status = TORIRSSERVER_RELOAD_OK;
}

/* ------------------------------------------------------------------ */
/* Connections                                                         */
/* ------------------------------------------------------------------ */

static void
ctl_conn_free(struct CtlConn* c)
{
    if( c->fd >= 0 )
        close(c->fd);
    ToriMem_Free(ToriMem_ServerNet(), c->in);
    ToriMem_Free(ToriMem_ServerNet(), c->out);
    ToriMem_Free(ToriMem_ServerNet(), c);
}

#if defined(_WIN32) || defined(__EMSCRIPTEN__)
int
ToriRSServer_ControlAdopt(
    int fd,
    int gated)
{
    /* No descriptor to keep: winsock sockets cannot be dup()ed, and a browser
     * server accepts no TCP. Not a link on these lanes. */
    (void)fd;
    (void)gated;
    fprintf(stderr, "torirsserver: control link refused: not supported on this platform\n");
    return 0;
}
#else
static int
ctl_peer_is_loopback(int fd)
{
    struct sockaddr_storage peer;
    socklen_t length = sizeof(peer);

    if( getpeername(fd, (struct sockaddr*)&peer, &length) != 0 )
        return 0;
    if( peer.ss_family == AF_INET )
    {
        const struct sockaddr_in* in4 = (const struct sockaddr_in*)&peer;

        return (ntohl(in4->sin_addr.s_addr) >> 24) == 127;
    }
    if( peer.ss_family == AF_INET6 )
    {
        const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)&peer;
        static const uint8_t loop6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
        const uint8_t* a = in6->sin6_addr.s6_addr;

        if( memcmp(a, loop6, 16) == 0 )
            return 1;
        /* ::ffff:127.x.y.z */
        return a[10] == 0xff && a[11] == 0xff && a[12] == 127 &&
               memcmp(a, loop6, 10) == 0;
    }
    return 0;
}

int
ToriRSServer_ControlAdopt(
    int fd,
    int gated)
{
    struct CtlConn* c;
    int slot = -1;
    int mine;

    if( !gated && !ctl_peer_is_loopback(fd) )
    {
        fprintf(stderr, "torirsserver: control link refused: not loopback, and no sign-in gate "
                        "to ask for a ticket\n");
        return 0;
    }
    for( int i = 0; i < CTL_CONN_MAX && slot < 0; i++ )
        if( !g_ctl[i] )
            slot = i;
    if( slot < 0 )
    {
        fprintf(stderr, "torirsserver: control link refused: %d already open\n", CTL_CONN_MAX);
        return 0;
    }
    mine = dup(fd);
    if( mine < 0 )
    {
        perror("torirsserver: control link: dup");
        return 0;
    }
    sock_set_nonblocking(mine);
    c = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*c));
    assert(c);
    c->fd = mine;
    c->gated = gated;
    g_ctl[slot] = c;
    g_ctl_count++;
    fprintf(stderr, "torirsserver: control link opened (slot %d%s)\n", slot,
            gated ? ", ticket required" : ", loopback");
    return 1;
}
#endif

/* ------------------------------------------------------------------ */
/* A listener that takes only the control link                         */
/* ------------------------------------------------------------------ */

enum
{
    /** Connections accepted whose first byte has not arrived yet. */
    CTL_LISTEN_PENDING = 8,
    /** A peer that sends nothing for this long is closed. */
    CTL_LISTEN_FIRST_BYTE_MS = 5000,
};

static int g_ctl_pending[CTL_LISTEN_PENDING] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int64_t g_ctl_pending_since[CTL_LISTEN_PENDING];

static int64_t
ctl_listen_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#if defined(_WIN32) || defined(__EMSCRIPTEN__)
int
ToriRSServer_ControlListenOpen(int port)
{
    (void)port;
    fprintf(stderr, "torirsserver: a control listener is not supported on this platform\n");
    return -1;
}

void
ToriRSServer_ControlListenPump(int listen_fd)
{
    (void)listen_fd;
}

void
ToriRSServer_ControlListenClose(int listen_fd)
{
    (void)listen_fd;
}
#else
int
ToriRSServer_ControlListenOpen(int port)
{
    struct sockaddr_in addr;
    int reuse = 1;
    int fd;

    assert(port > 0);
    assert(port < 65536);
    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if( fd < 0 )
    {
        perror("torirsserver: control listener: socket");
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(fd, 4) < 0 )
    {
        fprintf(stderr, "torirsserver: control listener on 127.0.0.1:%d: %s\n", port,
                strerror(errno));
        close(fd);
        return -1;
    }
    sock_set_nonblocking(fd);
    fprintf(stderr, "torirsserver: control listener on 127.0.0.1:%d\n", port);
    return fd;
}

void
ToriRSServer_ControlListenPump(int listen_fd)
{
    assert(listen_fd >= 0);
    for( ;; )
    {
        int fd = (int)accept(listen_fd, NULL, NULL);
        int slot = -1;

        if( fd < 0 )
            break;
        for( int i = 0; i < CTL_LISTEN_PENDING && slot < 0; i++ )
            if( g_ctl_pending[i] < 0 )
                slot = i;
        if( slot < 0 )
        {
            close(fd);
            continue;
        }
        sock_set_nonblocking(fd);
        g_ctl_pending[slot] = fd;
        g_ctl_pending_since[slot] = ctl_listen_now_ms();
    }
    for( int i = 0; i < CTL_LISTEN_PENDING; i++ )
    {
        uint8_t first = 0;
        ssize_t got;
        int fd = g_ctl_pending[i];

        if( fd < 0 )
            continue;
        got = recv(fd, (char*)&first, 1, 0);
        if( got < 0 && (sock_would_block() || sock_interrupted()) )
        {
            if( ctl_listen_now_ms() - g_ctl_pending_since[i] > CTL_LISTEN_FIRST_BYTE_MS )
            {
                close(fd);
                g_ctl_pending[i] = -1;
            }
            continue;
        }
        /* The same first-byte select as the game port's: 0xED is the control
         * link, and here nothing else is taken. */
        if( got == 1 && first == TORIRSSERVER_CTL_FIRST_BYTE )
            (void)ToriRSServer_ControlAdopt(fd, 0);
        else if( got == 1 )
            fprintf(stderr, "torirsserver: the control listener takes only the control link "
                            "(first byte 0x%02x)\n", first);
        close(fd);
        g_ctl_pending[i] = -1;
    }
}

void
ToriRSServer_ControlListenClose(int listen_fd)
{
    for( int i = 0; i < CTL_LISTEN_PENDING; i++ )
    {
        if( g_ctl_pending[i] >= 0 )
            close(g_ctl_pending[i]);
        g_ctl_pending[i] = -1;
    }
    if( listen_fd >= 0 )
        close(listen_fd);
}
#endif

void
ToriRSServer_ControlCloseAll(void)
{
    for( int i = 0; i < CTL_CONN_MAX; i++ )
    {
        if( !g_ctl[i] )
            continue;
        ctl_conn_free(g_ctl[i]);
        g_ctl[i] = NULL;
    }
    g_ctl_count = 0;
}

int
ToriRSServer_ControlCount(void)
{
    return g_ctl_count;
}

static int
ctl_name_equal(
    const char* a,
    const char* b)
{
    while( *a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b) )
    {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static struct ToriRSServerPlayer*
ctl_find_player(
    struct ToriRSServer* srv,
    const char* name)
{
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];

        if( !player->active )
            continue;
        /* Either name, without case: the editor shows the display name, a
         * test knows the account. */
        if( (player->account_name[0] && ctl_name_equal(player->account_name, name)) ||
            (player->display_name[0] && ctl_name_equal(player->display_name, name)) )
            return player;
    }
    return NULL;
}

static int
ctl_active_players(const struct ToriRSServer* srv)
{
    int count = 0;

    for( int i = 0; i < srv->player_count; i++ )
        count += srv->players[i].active != 0;
    return count;
}

static void
ctl_send_line(
    struct CtlConn* c,
    const char* text)
{
    struct CtlFrame frame = ctl_frame_begin(c, TORIRSSERVER_CTL_FACT_LINE);

    ctl_frame_str(&frame, text);
    ctl_frame_end(&frame);
}

static void
ctl_send_status(
    struct ToriRSServer* srv,
    struct CtlConn* c)
{
    struct CtlFrame frame = ctl_frame_begin(c, TORIRSSERVER_CTL_FACT_STATUS);
    char scripts[128];
    char servpack[64];
    char pack_dir[1024];

    ToriRSServer_ScriptsStampText(srv, scripts, sizeof(scripts));
    ToriRSServer_ServPackDir(ToriRSServer_BootContentDir(), ToriRSServer_WorldCacheDir(), pack_dir,
                             sizeof(pack_dir));
    ToriRSServer_ServPackStampText(pack_dir, servpack, sizeof(servpack));
    ctl_frame_u32(&frame, (uint32_t)ctl_active_players(srv));
    ctl_frame_u32(&frame, (uint32_t)srv->tick);
    ctl_frame_str(&frame, scripts);
    ctl_frame_str(&frame, servpack);
    /* The per-unit stale list is printed by ToriRSServer_PackCheck at boot and
     * not kept; the count is 0 here until it is (open issue). */
    ctl_frame_u32(&frame, 0);
    ctl_frame_end(&frame);
}

static void
ctl_handle_hello(
    struct ToriRSServer* srv,
    struct CtlConn* c,
    struct CtlReader* r)
{
    char ticket[512];
    char who[64] = "";
    uint32_t version = ctl_read_u32(r);
    struct CtlFrame frame;
    char const* name = getenv("TORIRSSERVER_NAME");

    ctl_read_str(r, ticket, sizeof(ticket));
    if( r->bad )
        return;
    c->hello_ok = version == TORIRSSERVER_CTL_VERSION;
    if( c->hello_ok && c->gated )
        c->hello_ok = ToriRSServer_GateTicketCheck(ticket, strlen(ticket), (long long)time(NULL),
                                                   who, sizeof(who));
    frame = ctl_frame_begin(c, TORIRSSERVER_CTL_FACT_HELLO);
    ctl_frame_u32(&frame, TORIRSSERVER_CTL_VERSION);
    ctl_frame_u32(&frame, (uint32_t)c->hello_ok);
    ctl_frame_str(&frame, name && name[0] ? name : "torirsserver");
    ctl_frame_u32(&frame, (uint32_t)ctl_active_players(srv));
    ctl_frame_end(&frame);
    fprintf(stderr, "torirsserver: control link hello: version %u, %s%s%s\n", version,
            c->hello_ok ? "accepted" : "refused", who[0] ? " for " : "", who);
    if( !c->hello_ok )
        c->closing = 1;
}

static void
ctl_handle_reload(
    struct ToriRSServer* srv,
    struct CtlConn* c,
    struct CtlReader* r)
{
    struct ToriRSServerReloadRequest* request =
        ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*request));
    struct ToriRSServerReloadResult* result =
        ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*result));
    uint32_t n;
    struct CtlFrame frame;

    assert(request);
    assert(result);
    ctl_read_str(r, request->script_dir, sizeof(request->script_dir));
    n = ctl_read_u32(r);
    for( uint32_t i = 0; i < n && !r->bad; i++ )
    {
        int index = (int)ctl_read_u32(r);
        int archive = (int)ctl_read_u32(r);

        if( request->group_count < TORIRSSERVER_RELOAD_GROUPS_MAX )
        {
            request->groups[request->group_count].index = index;
            request->groups[request->group_count].archive = archive;
            request->group_count++;
        }
        else
            r->bad = 1;
    }
    n = ctl_read_u32(r);
    for( uint32_t i = 0; i < n && !r->bad; i++ )
    {
        int x = (int)ctl_read_u32(r);
        int z = (int)ctl_read_u32(r);

        if( request->square_count < TORIRSSERVER_RELOAD_SQUARES_MAX )
        {
            request->squares[request->square_count].map_x = x;
            request->squares[request->square_count].map_z = z;
            request->square_count++;
        }
        else
            r->bad = 1;
    }
    n = ctl_read_u32(r);
    for( uint32_t i = 0; i < n && !r->bad; i++ )
    {
        if( request->spawn_count >= TORIRSSERVER_RELOAD_SPAWNS_MAX )
        {
            r->bad = 1;
            break;
        }
        ctl_read_str(r, request->spawn_files[request->spawn_count],
                     sizeof(request->spawn_files[0]));
        request->spawn_count++;
    }
    if( !r->bad )
    {
        ToriRSServer_Reload(srv, request, result);
        for( int i = 0; i < result->line_count; i++ )
            ctl_send_line(c, result->lines[i]);
        frame = ctl_frame_begin(c, TORIRSSERVER_CTL_FACT_RELOAD_DONE);
        ctl_frame_u32(&frame, (uint32_t)result->status);
        ctl_frame_u32(&frame, (uint32_t)result->scripts_swapped);
        ctl_frame_u32(&frame, (uint32_t)result->groups_replaced);
        ctl_frame_u32(&frame, (uint32_t)result->squares_reloaded);
        ctl_frame_u32(&frame, (uint32_t)result->spawns_changed);
        ctl_frame_u32(&frame, (uint32_t)result->restart_count);
        for( int i = 0; i < result->restart_count; i++ )
            ctl_frame_str(&frame, result->restart[i]);
        ctl_frame_end(&frame);
    }
    ToriMem_Free(ToriMem_ServerNet(), request);
    ToriMem_Free(ToriMem_ServerNet(), result);
}

/* The messages the cheat sent its player, read back from a capture of this
 * one call, when no test capture is already running. */
static void
ctl_handle_cheat(
    struct ToriRSServer* srv,
    struct CtlConn* c,
    struct CtlReader* r)
{
    char name[64];
    char line[512];
    const char* text;
    struct ToriRSServerPlayer* player;
    struct ToriRSServerPlayer* caller_active = srv->active_player;
    struct ToriRSServerCapture* capture = NULL;
    enum ToriRSServerTriggerResult verdict;

    ctl_read_str(r, name, sizeof(name));
    ctl_read_str(r, line, sizeof(line));
    if( r->bad )
        return;
    player = ctl_find_player(srv, name);
    if( !player )
    {
        char reply[160];

        snprintf(reply, sizeof(reply), "no player named %s is logged in", name);
        ctl_send_line(c, reply);
        ctl_send_status(srv, c);
        return;
    }
    text = line;
    while( *text == ':' )
        text++;
    if( !srv->capture )
    {
        capture = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*capture));
        assert(capture);
        ToriRSServer_CaptureBegin(srv, capture);
    }
    ToriRSServer_WorldSetActive(srv, player);
    verdict = ToriRSServer_RunCheatForTest(srv, text);
    ToriRSServer_WorldSetActive(srv, caller_active);
    if( capture )
    {
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(capture, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(capture, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            char message[TORIRSSERVER_CAPTURE_BYTES];

            if( capture->packets[i].recipient_pid != player->pid )
                continue;
            if( ToriRSServer_WireReadMessageGame(srv->wire ? srv->wire : ToriRSServer_WireDefault(),
                                                 capture->packets[i].data, capture->packets[i].len,
                                                 message, (int)sizeof(message)) )
                ctl_send_line(c, message);
        }
        ToriMem_Free(ToriMem_ServerNet(), capture);
    }
    ctl_send_line(c, verdict == TORIRSSERVER_TRIGGER_RAN      ? "cheat: ran"
                     : verdict == TORIRSSERVER_TRIGGER_FAILED ? "cheat: refused"
                                                              : "cheat: unknown command");
    ctl_send_status(srv, c);
}

static void
ctl_handle_subscribe(
    struct CtlConn* c,
    struct CtlReader* r)
{
    uint32_t n;

    ctl_read_str(r, c->sub_player, sizeof(c->sub_player));
    n = ctl_read_u32(r);
    c->sub_count = 0;
    c->sub_primed = 0;
    for( uint32_t i = 0; i < n && !r->bad; i++ )
    {
        uint32_t id = ctl_read_u32(r);

        if( c->sub_count < CTL_SUBSCRIBE_MAX && id < TORIRSSERVER_VARPS_EMPTY )
            c->sub_ids[c->sub_count++] = (int)id;
    }
    if( n == 0 )
        c->sub_player[0] = '\0';
}

/* CTL_FACT_VARS for every subscribed varp whose value moved (all of them,
 * the first time). */
static void
ctl_subscription_tick(
    struct ToriRSServer* srv,
    struct CtlConn* c)
{
    struct ToriRSServerPlayer* player;
    int changed[CTL_SUBSCRIBE_MAX];
    int changed_count = 0;
    struct CtlFrame frame;

    if( !c->sub_player[0] || c->sub_count <= 0 )
        return;
    player = ctl_find_player(srv, c->sub_player);
    if( !player )
        return;
    for( int i = 0; i < c->sub_count; i++ )
    {
        int32_t value = ToriRSServer_VarpsGet(&player->varps, c->sub_ids[i]);

        if( c->sub_primed && value == c->sub_values[i] )
            continue;
        c->sub_values[i] = value;
        changed[changed_count++] = i;
    }
    c->sub_primed = 1;
    if( changed_count == 0 )
        return;
    frame = ctl_frame_begin(c, TORIRSSERVER_CTL_FACT_VARS);
    ctl_frame_str(&frame, c->sub_player);
    ctl_frame_u32(&frame, (uint32_t)changed_count);
    for( int i = 0; i < changed_count; i++ )
    {
        ctl_frame_u32(&frame, (uint32_t)c->sub_ids[changed[i]]);
        ctl_frame_u32(&frame, (uint32_t)c->sub_values[changed[i]]);
    }
    ctl_frame_end(&frame);
}

/* One request. Returns 0 to close the link. */
static int
ctl_dispatch(
    struct ToriRSServer* srv,
    struct CtlConn* c,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    struct CtlReader r = { payload, length, 0, 0 };

    if( type == TORIRSSERVER_CTL_HELLO )
        ctl_handle_hello(srv, c, &r);
    else if( !c->hello_ok )
    {
        fprintf(stderr, "torirsserver: control link: frame %u before an accepted hello\n", type);
        return 0;
    }
    else if( type == TORIRSSERVER_CTL_STATUS )
        ctl_send_status(srv, c);
    else if( type == TORIRSSERVER_CTL_RELOAD )
        ctl_handle_reload(srv, c, &r);
    else if( type == TORIRSSERVER_CTL_CHEAT )
        ctl_handle_cheat(srv, c, &r);
    else if( type == TORIRSSERVER_CTL_SUBSCRIBE )
        ctl_handle_subscribe(c, &r);
    else if( type == TORIRSSERVER_CTL_BYE )
        c->closing = 1;
    else
    {
        fprintf(stderr, "torirsserver: control link: unknown frame %u\n", type);
        return 0;
    }
    if( r.bad )
    {
        fprintf(stderr, "torirsserver: control link: malformed frame %u (%u bytes)\n", type,
                length);
        return 0;
    }
    return 1;
}

/* Read what is there, answer every whole frame, write what the socket takes.
 * Returns 0 when the connection is done. */
static int
ctl_pump(
    struct ToriRSServer* srv,
    struct CtlConn* c)
{
    int consumed = 0;

    for( ;; )
    {
        int got;

        if( c->in_cap - c->in_len < 4096 )
        {
            c->in_cap = c->in_cap ? c->in_cap * 2 : 8192;
            c->in = ToriMem_Realloc(ToriMem_ServerNet(), c->in, (size_t)c->in_cap);
            assert(c->in);
        }
        got = (int)recv(c->fd, (char*)c->in + c->in_len, (size_t)(c->in_cap - c->in_len), 0);
        if( got > 0 )
        {
            c->in_len += got;
            continue;
        }
        if( got == 0 )
        {
            c->closing = 1;
            break;
        }
        if( sock_interrupted() )
            continue;
        if( sock_would_block() )
            break;
        return 0;
    }
    while( c->in_len - consumed >= 8 )
    {
        uint32_t type = ctl_get_u32(c->in + consumed);
        uint32_t length = ctl_get_u32(c->in + consumed + 4);

        if( length > TORIRSSERVER_CTL_FRAME_MAX )
        {
            fprintf(stderr, "torirsserver: control link: frame of %u bytes refused\n", length);
            return 0;
        }
        if( (uint32_t)(c->in_len - consumed - 8) < length )
            break;
        if( !ctl_dispatch(srv, c, type, c->in + consumed + 8, length) )
            return 0;
        consumed += 8 + (int)length;
        if( c->closing )
            break;
    }
    if( consumed > 0 )
    {
        memmove(c->in, c->in + consumed, (size_t)(c->in_len - consumed));
        c->in_len -= consumed;
    }
    if( c->hello_ok )
        ctl_subscription_tick(srv, c);
    /* Write what the socket takes now; the rest goes out next tick. */
    while( c->out_len > 0 )
    {
        int put = (int)send(c->fd, (const char*)c->out, (size_t)c->out_len, 0);

        if( put > 0 )
        {
            memmove(c->out, c->out + put, (size_t)(c->out_len - put));
            c->out_len -= put;
            continue;
        }
        if( put < 0 && sock_interrupted() )
            continue;
        if( put < 0 && sock_would_block() )
            break;
        return 0;
    }
    return !(c->closing && c->out_len == 0);
}

void
ToriRSServer_ControlTick(struct ToriRSServer* srv)
{
    assert(srv);
    if( g_ctl_count > 0 )
    {
        for( int i = 0; i < CTL_CONN_MAX; i++ )
        {
            if( !g_ctl[i] )
                continue;
            if( ctl_pump(srv, g_ctl[i]) )
                continue;
            fprintf(stderr, "torirsserver: control link closed (slot %d)\n", i);
            ctl_conn_free(g_ctl[i]);
            g_ctl[i] = NULL;
            g_ctl_count--;
        }
    }
    /* Whatever a reload retired and nothing runs on any more. */
    if( ToriRSServer_ScriptsRetiredCount(srv) > 0 )
        ToriRSServer_ScriptsRetireSweep(srv);
}
