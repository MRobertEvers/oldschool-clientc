/*
 * The control link, as the account site speaks it. See website_ctl.h; the wire is
 * torirs_server_control.h's: one selector byte 0xED, then frames
 * `[u32 type][u32 length][payload]`, little-endian, strings `u32 len, bytes`.
 */
#include "website_ctl.h"

#include "mem/torirs_mem.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_control.h"
#include "torirsserver/torirs_server_savefile.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET ctl_socket;
#define CTL_BAD_SOCKET INVALID_SOCKET
#define ctl_close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int ctl_socket;
#define CTL_BAD_SOCKET (-1)
#define ctl_close close
#endif

#define CTL_TIMEOUT_MS 3000
#define CTL_DEFAULT_PORT 43594

struct CtlLink
{
    ctl_socket fd;
};

struct CtlFrame
{
    uint32_t type;
    uint32_t length;
    uint8_t* data;
    uint32_t at;
    int bad;
};

static int
ctl_port(void)
{
    char const* env = getenv("TORIRS_GAME_PORT");
    int port = env ? atoi(env) : 0;

    return port > 0 && port < 65536 ? port : CTL_DEFAULT_PORT;
}

static int
send_all(
    ctl_socket fd,
    void const* data,
    size_t len)
{
    char const* p = data;

    while( len > 0 )
    {
        int n = (int)send(fd, p, (int)len, 0);

        if( n <= 0 )
            return 0;
        p += n;
        len -= (size_t)n;
    }
    return 1;
}

static int
recv_all(
    ctl_socket fd,
    void* data,
    size_t len)
{
    char* p = data;

    while( len > 0 )
    {
        int n = (int)recv(fd, p, (int)len, 0);

        if( n <= 0 )
            return 0;
        p += n;
        len -= (size_t)n;
    }
    return 1;
}

static void
put_u32(
    uint8_t* p,
    uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t
get_u32(uint8_t const* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* One frame of `type` carrying `strs` strings (each `u32 len, bytes`) after
 * an optional leading u32 `lead` (has_lead). */
static int
send_frame(
    struct CtlLink* link,
    uint32_t type,
    int has_lead,
    uint32_t lead,
    char const* const* strs,
    int str_count)
{
    uint8_t buf[2048];
    size_t at = 8;

    if( has_lead )
    {
        put_u32(buf + at, lead);
        at += 4;
    }
    for( int i = 0; i < str_count; i++ )
    {
        size_t len = strlen(strs[i]);

        if( at + 4 + len > sizeof(buf) )
            return 0;
        put_u32(buf + at, (uint32_t)len);
        memcpy(buf + at + 4, strs[i], len);
        at += 4 + len;
    }
    put_u32(buf, type);
    put_u32(buf + 4, (uint32_t)(at - 8));
    return send_all(link->fd, buf, at);
}

/* The next frame; its payload heap-owned until frame_free. 0 on a closed or
 * silent link. */
static int
recv_frame(
    struct CtlLink* link,
    struct CtlFrame* out)
{
    uint8_t head[8];

    memset(out, 0, sizeof(*out));
    if( !recv_all(link->fd, head, sizeof(head)) )
        return 0;
    out->type = get_u32(head);
    out->length = get_u32(head + 4);
    if( out->length > TORIRSSERVER_CTL_FRAME_MAX )
        return 0;
    out->data = ToriMem_Alloc(ToriMem_Net(), out->length ? out->length : 1);
    assert(out->data);
    if( out->length && !recv_all(link->fd, out->data, out->length) )
    {
        ToriMem_Free(ToriMem_Net(), out->data);
        out->data = NULL;
        return 0;
    }
    return 1;
}

static void
frame_free(struct CtlFrame* f)
{
    if( f->data )
        ToriMem_Free(ToriMem_Net(), f->data);
    f->data = NULL;
}

static uint32_t
frame_u32(struct CtlFrame* f)
{
    uint32_t v;

    if( f->at + 4 > f->length )
    {
        f->bad = 1;
        return 0;
    }
    v = get_u32(f->data + f->at);
    f->at += 4;
    return v;
}

static void
frame_str(
    struct CtlFrame* f,
    char* out,
    size_t cap)
{
    uint32_t len = frame_u32(f);

    out[0] = '\0';
    if( f->bad || f->at + len > f->length )
    {
        f->bad = 1;
        return;
    }
    snprintf(out, cap, "%.*s", (int)len, (char const*)f->data + f->at);
    f->at += len;
}

static void
link_close(struct CtlLink* link)
{
    if( link->fd != CTL_BAD_SOCKET )
    {
        char const* none[1];

        send_frame(link, TORIRSSERVER_CTL_BYE, 0, 0, none, 0);
        ctl_close(link->fd);
    }
    link->fd = CTL_BAD_SOCKET;
}

/* FACT_PLAYERS into `out` (NULL to skip). */
static void
read_players(
    struct CtlFrame* f,
    struct WebsiteCtlPlayers* out)
{
    uint32_t rows;

    (void)frame_u32(f); /* logins */
    (void)frame_u32(f); /* active */
    rows = frame_u32(f);
    if( out )
        out->count = 0;
    for( uint32_t i = 0; i < rows && !f->bad; i++ )
    {
        struct WebsiteCtlPlayer row;

        frame_str(f, row.account, sizeof(row.account));
        frame_str(f, row.display, sizeof(row.display));
        (void)frame_u32(f); /* slot */
        (void)frame_u32(f); /* login generation */
        row.x = (int)frame_u32(f);
        row.z = (int)frame_u32(f);
        row.level = (int)frame_u32(f);
        if( out && out->count < WEBSITE_CTL_PLAYERS_MAX && !f->bad )
            out->rows[out->count++] = row;
    }
}

/* Connect, say HELLO as `staff`, and read the answer and the player list
 * that follows an accepted one. */
static enum WebsiteCtlResult
link_open(
    struct CtlLink* link,
    char const* staff,
    struct WebsiteCtlPlayers* players)
{
    struct sockaddr_in addr;
    char ticket[TORIRSSERVER_GATE_TICKET_MAX];
    char const* strs[1];
    struct CtlFrame f;
    uint8_t selector = TORIRSSERVER_CTL_FIRST_BYTE;
    uint32_t ok;
#ifdef _WIN32
    DWORD timeout = CTL_TIMEOUT_MS;
#else
    struct timeval timeout = { CTL_TIMEOUT_MS / 1000, (CTL_TIMEOUT_MS % 1000) * 1000 };
#endif

    link->fd = socket(AF_INET, SOCK_STREAM, 0);
    if( link->fd == CTL_BAD_SOCKET )
        return WEBSITE_CTL_DOWN;
    setsockopt(link->fd, SOL_SOCKET, SO_RCVTIMEO, (char const*)&timeout, sizeof(timeout));
    setsockopt(link->fd, SOL_SOCKET, SO_SNDTIMEO, (char const*)&timeout, sizeof(timeout));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)ctl_port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if( connect(link->fd, (struct sockaddr const*)&addr, sizeof(addr)) != 0 )
    {
        ctl_close(link->fd);
        link->fd = CTL_BAD_SOCKET;
        return WEBSITE_CTL_DOWN;
    }
    /* A short ticket: this link lives for one request. */
    ToriRSServer_GateTicketMint(staff, (long long)time(NULL) + 60, ticket);
    strs[0] = ticket;
    if( !send_all(link->fd, &selector, 1) ||
        !send_frame(link, TORIRSSERVER_CTL_HELLO, 1, TORIRSSERVER_CTL_VERSION, strs, 1) ||
        !recv_frame(link, &f) || f.type != TORIRSSERVER_CTL_FACT_HELLO )
    {
        memset(ticket, 0, sizeof(ticket));
        link_close(link);
        return WEBSITE_CTL_REFUSED;
    }
    memset(ticket, 0, sizeof(ticket));
    (void)frame_u32(&f); /* version */
    ok = frame_u32(&f);
    frame_free(&f);
    if( !ok )
    {
        fprintf(stderr, "website_server: control link refused '%s'\n", staff);
        link_close(link);
        return WEBSITE_CTL_REFUSED;
    }
    if( !recv_frame(link, &f) || f.type != TORIRSSERVER_CTL_FACT_PLAYERS )
    {
        frame_free(&f);
        link_close(link);
        return WEBSITE_CTL_REFUSED;
    }
    read_players(&f, players);
    frame_free(&f);
    return WEBSITE_CTL_OK;
}

enum WebsiteCtlResult
WebsiteCtl_Players(
    char const* staff,
    struct WebsiteCtlPlayers* out)
{
    struct CtlLink link;
    enum WebsiteCtlResult result;

    assert(staff);
    assert(out);
    out->count = 0;
    result = link_open(&link, staff, out);
    if( result == WEBSITE_CTL_OK )
        link_close(&link);
    return result;
}

struct WebsiteCtlPlayer const*
WebsiteCtl_Find(
    struct WebsiteCtlPlayers const* players,
    char const* key)
{
    assert(players);
    assert(key);
    for( int i = 0; i < players->count; i++ )
    {
        char a[WEBSITE_CTL_NAME_MAX];
        char d[WEBSITE_CTL_NAME_MAX];

        ToriRSServer_SaveFileName(players->rows[i].account, a, sizeof(a));
        ToriRSServer_SaveFileName(players->rows[i].display, d, sizeof(d));
        if( strcmp(a, key) == 0 || (d[0] && strcmp(d, key) == 0) )
            return &players->rows[i];
    }
    return NULL;
}

/* Send one request and gather FACT_LINEs up to its FACT_STATUS. */
static enum WebsiteCtlResult
request(
    char const* staff,
    uint32_t type,
    char const* const* strs,
    int str_count,
    struct WebsiteCtlReply* out)
{
    struct CtlLink link;
    enum WebsiteCtlResult result = link_open(&link, staff, NULL);

    out->count = 0;
    if( result != WEBSITE_CTL_OK )
        return result;
    if( !send_frame(&link, type, 0, 0, strs, str_count) )
    {
        link_close(&link);
        return WEBSITE_CTL_REFUSED;
    }
    for( ;; )
    {
        struct CtlFrame f;

        if( !recv_frame(&link, &f) )
        {
            result = WEBSITE_CTL_REFUSED;
            break;
        }
        if( f.type == TORIRSSERVER_CTL_FACT_LINE && out->count < WEBSITE_CTL_LINES_MAX )
            frame_str(&f, out->lines[out->count++], WEBSITE_CTL_LINE_MAX);
        if( f.type == TORIRSSERVER_CTL_FACT_STATUS )
        {
            frame_free(&f);
            break;
        }
        frame_free(&f);
    }
    link_close(&link);
    return result;
}

enum WebsiteCtlResult
WebsiteCtl_Cheat(
    char const* staff,
    char const* player,
    char const* line,
    struct WebsiteCtlReply* out)
{
    char const* strs[2];

    assert(staff);
    assert(player);
    assert(line);
    assert(out);
    strs[0] = player;
    strs[1] = line;
    return request(staff, TORIRSSERVER_CTL_CHEAT, strs, 2, out);
}

enum WebsiteCtlResult
WebsiteCtl_Snapshot(
    char const* staff,
    char const* player,
    struct WebsiteCtlReply* out)
{
    char const* strs[1];

    assert(staff);
    assert(player);
    assert(out);
    strs[0] = player;
    return request(staff, TORIRSSERVER_CTL_SNAPSHOT, strs, 1, out);
}

enum WebsiteCtlResult
WebsiteCtl_Logout(
    char const* staff,
    char const* player,
    struct WebsiteCtlReply* out)
{
    char const* strs[1];

    assert(staff);
    assert(player);
    assert(out);
    strs[0] = player;
    return request(staff, TORIRSSERVER_CTL_LOGOUT, strs, 1, out);
}
