/*
 * The world's line to the account server. See torirs_server_accountlink.h.
 */
#include "torirs_server_accountlink.h"

#include "accountserver/account_proto.h"
#include "mem/torirs_mem.h"
#include "torirs_server_savefile.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A batch: at most this much of the spool, and this many records (the
 * server takes 512). */
#define LINK_BATCH_BYTES (64 * 1024)
#define LINK_BATCH_RECORDS 256
/* The server's answer is one short line under its headers. */
#define LINK_ANSWER_MAX 2048
/* A step that has not finished in this long is an outage. */
#define LINK_STEP_TIMEOUT_MS 5000
#define LINK_RETRY_MIN_MS 1000
#define LINK_RETRY_MAX_MS 30000
/* Cut a fully acknowledged spool back once it is this long. */
#define LINK_COMPACT_BYTES (256 * 1024)

enum LinkState
{
    LINK_IDLE,
    LINK_CONNECTING,
    LINK_SENDING,
    LINK_READING,
};

struct ToriRSServerAccountLink
{
    char path[1024];
    /** "a+b": appends always land at the end; batches are read from
     *  `send_offset`. */
    FILE* spool;
    long long next_seq;
    /** The highest sequence number acknowledged, or one below the spool's
     *  first record at start (what the server holds of those is unknown,
     *  and a resend is harmless). */
    long long acked;
    /** The file offset of the first line not yet acknowledged. */
    long send_offset;

    char host[128];
    int port;
    char source[64];

    enum LinkState state;
    int fd;
    char* request;
    size_t request_len;
    size_t request_sent;
    long batch_end_offset;
    long long batch_last_seq;
    char answer[LINK_ANSWER_MAX];
    size_t answer_len;
    long deadline_ms;
    long retry_at_ms;
    long retry_ms;
    /** An outage has been reported, and its end will be. */
    int outage;
};

static long
link_now_ms(void)
{
#ifdef _WIN32
    return (long)GetTickCount64();
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((long)tv.tv_sec * 1000L) + (tv.tv_usec / 1000L);
#endif
}

static int
would_block(void)
{
#ifdef _WIN32
    int e = WSAGetLastError();

    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
}

static void
set_nonblocking(int fd)
{
#ifdef _WIN32
    u_long on = 1;

    ioctlsocket(fd, FIONBIO, &on);
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if( flags >= 0 )
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

/* ------------------------------------------------------------------ */
/* The spool                                                           */
/* ------------------------------------------------------------------ */

/* A fresh spool's first number: the clock in microseconds. */
static long long
fresh_base(void)
{
    return (long long)time(NULL) * 1000000LL;
}

static void
spool_open(
    struct ToriRSServerAccountLink* link,
    char const* mode)
{
    link->spool = fopen(link->path, mode);
    if( !link->spool )
    {
        fprintf(stderr, "torirsserver: cannot open the account spool %s: %s\n", link->path,
                strerror(errno));
        abort();
    }
}

/* Start the spool over at `next`, with nothing in it. */
static void
spool_reset(
    struct ToriRSServerAccountLink* link,
    long long next)
{
    if( link->spool )
        fclose(link->spool);
    spool_open(link, "w+b");
    fprintf(link->spool, "next\t%lld\n", next);
    fflush(link->spool);
    link->send_offset = ftell(link->spool);
    fclose(link->spool);
    spool_open(link, "a+b");
    link->next_seq = next;
    link->acked = next - 1;
}

/* Read what an earlier run left: the header's number, and the records not
 * known to be acknowledged. */
static void
spool_load(struct ToriRSServerAccountLink* link)
{
    char line[ACCOUNT_RECORD_LINE_MAX];
    long long base;
    long long first = 0;
    long long last = 0;
    int torn = 0;

    fseek(link->spool, 0, SEEK_SET);
    if( !fgets(line, sizeof(line), link->spool) || strncmp(line, "next\t", 5) != 0 )
    {
        spool_reset(link, fresh_base());
        return;
    }
    base = strtoll(line + 5, NULL, 10);
    link->send_offset = ftell(link->spool);
    while( fgets(line, sizeof(line), link->spool) )
    {
        struct ToriRSServerAccountRecord record;
        long long seq;
        size_t len = strlen(line);

        torn = len == 0 || line[len - 1] != '\n';
        line[strcspn(line, "\n")] = '\0';
        if( !AccountProto_RecordParse(line, &record, &seq) )
            continue;
        if( !first )
            first = seq;
        last = seq;
    }
    /* A crash in the middle of a write leaves a last line without its end:
     * end it, so the next record starts a line of its own. The fragment is
     * skipped when it comes up in a batch. */
    fseek(link->spool, 0, SEEK_END);
    if( torn )
    {
        fputc('\n', link->spool);
        fflush(link->spool);
    }
    link->next_seq = last >= base ? last + 1 : base;
    link->acked = (first ? first : link->next_seq) - 1;
}

struct ToriRSServerAccountLink*
ToriRSServer_AccountLinkOpen(void)
{
    struct ToriRSServerAccountLink* link =
        ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(struct ToriRSServerAccountLink));
    char const* server = getenv("TORIRS_ACCOUNT_SERVER");
    char const* colon = server ? strrchr(server, ':') : NULL;
    char const* world = getenv("TORIRS_WORLD_NAME");

    assert(link);
    link->fd = -1;
    link->retry_ms = LINK_RETRY_MIN_MS;
    if( server && colon && colon > server )
    {
        snprintf(link->host, sizeof(link->host), "%.*s", (int)(colon - server), server);
        link->port = atoi(colon + 1);
    }
    else
    {
        snprintf(link->host, sizeof(link->host), "127.0.0.1");
        link->port = TORIRSSERVER_ACCOUNTLINK_PORT;
    }
    snprintf(link->source, sizeof(link->source), "%s", world && world[0] ? world : "world");
    snprintf(link->path, sizeof(link->path), "%s/account_spool.tsv", ToriRSServer_SaveDir());
    ToriRSServer_SaveMkdirParent(link->path);
    spool_open(link, "a+b");
    spool_load(link);
    fprintf(stderr, "torirsserver: account records go to %s:%d as '%s' (spool %s, %lld waiting)\n",
            link->host, link->port, link->source, link->path,
            ToriRSServer_AccountLinkPending(link));
    return link;
}

long long
ToriRSServer_AccountLinkPending(struct ToriRSServerAccountLink const* link)
{
    assert(link);
    return link->next_seq - 1 - link->acked;
}

void
ToriRSServer_AccountLinkWrite(
    struct ToriRSServerAccountLink* link,
    struct ToriRSServerAccountLog* log)
{
    char line[ACCOUNT_RECORD_LINE_MAX];

    assert(link);
    assert(log);
    if( log->count == 0 )
        return;
    fseek(link->spool, 0, SEEK_END);
    for( int i = 0; i < log->count; i++ )
    {
        int len = AccountProto_RecordFormat(&log->records[i], link->next_seq++, line);

        if( fwrite(line, 1, (size_t)len, link->spool) != (size_t)len )
            fprintf(stderr, "torirsserver: ACCOUNT RECORD NOT WRITTEN to %s: %s\n", link->path,
                    strerror(errno));
    }
    log->count = 0;
    /* To the operating system, once a tick; never synced. */
    if( fflush(link->spool) != 0 )
        fprintf(stderr, "torirsserver: ACCOUNT SPOOL NOT FLUSHED (%s): %s\n", link->path,
                strerror(errno));
}

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

static void
link_disconnect(struct ToriRSServerAccountLink* link)
{
    if( link->fd >= 0 )
        close(link->fd);
    link->fd = -1;
    if( link->request )
        ToriMem_Free(ToriMem_ServerNet(), link->request);
    link->request = NULL;
    link->state = LINK_IDLE;
}

static void
link_fail(
    struct ToriRSServerAccountLink* link,
    long now_ms,
    char const* why)
{
    link_disconnect(link);
    link->retry_at_ms = now_ms + link->retry_ms;
    link->retry_ms = link->retry_ms * 2 > LINK_RETRY_MAX_MS ? LINK_RETRY_MAX_MS : link->retry_ms * 2;
    if( !link->outage )
        fprintf(stderr,
                "torirsserver: account server %s:%d: %s; %lld record(s) wait in %s, retrying\n",
                link->host, link->port, why, ToriRSServer_AccountLinkPending(link), link->path);
    link->outage = 1;
}

/* The next batch of the spool into a request. 0 when there is nothing to
 * send (every pending line was a torn fragment, now passed over). */
static int
link_build(struct ToriRSServerAccountLink* link)
{
    static char body[LINK_BATCH_BYTES];
    char line[ACCOUNT_RECORD_LINE_MAX];
    char parsed[ACCOUNT_RECORD_LINE_MAX];
    size_t body_len = 0;
    long offset = link->send_offset;
    int count = 0;
    char head[256];
    int head_len;

    fseek(link->spool, link->send_offset, SEEK_SET);
    while( count < LINK_BATCH_RECORDS && fgets(line, sizeof(line), link->spool) )
    {
        struct ToriRSServerAccountRecord record;
        long long seq;
        size_t len = strlen(line);
        int ok;

        if( len == 0 || line[len - 1] != '\n' )
            break;
        if( body_len + len > sizeof(body) )
            break;
        memcpy(parsed, line, len);
        parsed[len - 1] = '\0';
        ok = AccountProto_RecordParse(parsed, &record, &seq);
        if( !ok || seq <= link->acked )
        {
            /* A torn fragment, or a record already acknowledged: passed
             * over for good when it leads the spool. */
            if( !ok )
                fprintf(stderr, "torirsserver: account spool: a torn line passed over\n");
            offset += (long)len;
            if( count == 0 )
                link->send_offset = offset;
            continue;
        }
        memcpy(body + body_len, line, len);
        body_len += len;
        offset += (long)len;
        link->batch_last_seq = seq;
        count++;
    }
    fseek(link->spool, 0, SEEK_END);
    if( count == 0 )
        return 0;
    link->batch_end_offset = offset;
    head_len = snprintf(head, sizeof(head),
                        "POST /events?source=%s HTTP/1.0\r\nHost: %s:%d\r\n"
                        "Content-Type: text/plain\r\nContent-Length: %zu\r\n"
                        "Connection: close\r\n\r\n",
                        link->source, link->host, link->port, body_len);
    link->request = ToriMem_Alloc(ToriMem_ServerNet(), (size_t)head_len + body_len);
    assert(link->request);
    memcpy(link->request, head, (size_t)head_len);
    memcpy(link->request + head_len, body, body_len);
    link->request_len = (size_t)head_len + body_len;
    link->request_sent = 0;
    return 1;
}

static void
link_connect(
    struct ToriRSServerAccountLink* link,
    long now_ms)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)link->port);
    if( inet_pton(AF_INET, link->host, &addr.sin_addr) != 1 )
    {
        link_fail(link, now_ms, "TORIRS_ACCOUNT_SERVER is not an IPv4 address");
        return;
    }
    link->fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if( link->fd < 0 )
    {
        link_fail(link, now_ms, "no socket");
        return;
    }
    set_nonblocking(link->fd);
    link->answer_len = 0;
    link->deadline_ms = now_ms + LINK_STEP_TIMEOUT_MS;
    if( connect(link->fd, (struct sockaddr*)&addr, sizeof(addr)) == 0 )
        link->state = LINK_SENDING;
    else if( would_block() )
        link->state = LINK_CONNECTING;
    else
        link_fail(link, now_ms, "refused");
}

/* Whether the socket is ready now, for writing or for reading. */
static int
ready_now(
    int fd,
    int for_write)
{
    fd_set set;
    struct timeval zero = { 0, 0 };

    FD_ZERO(&set);
    FD_SET(fd, &set);
    return select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &zero) > 0;
}

/* Whether the answer has arrived whole: its headers, and as much body as
 * they announce. The server may keep the connection open after it. */
static int
answer_complete(struct ToriRSServerAccountLink* link)
{
    char* end;
    char const* length;

    link->answer[link->answer_len] = '\0';
    end = strstr(link->answer, "\r\n\r\n");
    if( !end )
        return 0;
    /* Spelled as http_server.c writes it. */
    length = strstr(link->answer, "Content-Length:");
    if( length && length < end )
        return link->answer + link->answer_len >= end + 4 + atol(length + 15);
    return 0;
}

/* The answer is complete: acknowledge what it says. */
static void
link_answered(
    struct ToriRSServerAccountLink* link,
    long now_ms)
{
    char* body;
    long long acked;

    link->answer[link->answer_len] = '\0';
    body = strstr(link->answer, "\r\n\r\n");
    if( strncmp(link->answer, "HTTP/1.", 7) != 0 || strncmp(link->answer + 8, " 200", 4) != 0 ||
        !body || strncmp(body + 4, "acked\t", 6) != 0 )
    {
        char why[160];

        snprintf(why, sizeof(why), "refused the batch: %.100s",
                 body ? body + 4 : link->answer);
        why[strcspn(why, "\r\n")] = '\0';
        /* A refusal is not an outage the next try will fix; say so every
         * time, until someone looks. */
        link->outage = 0;
        link_fail(link, now_ms, why);
        return;
    }
    acked = strtoll(body + 10, NULL, 10);
    if( acked < link->batch_last_seq )
    {
        link_fail(link, now_ms, "acknowledged less than it was sent");
        return;
    }
    link->acked = acked;
    link->send_offset = link->batch_end_offset;
    link_disconnect(link);
    link->retry_ms = LINK_RETRY_MIN_MS;
    if( link->outage )
        fprintf(stderr, "torirsserver: account server %s:%d answers again\n", link->host,
                link->port);
    link->outage = 0;
    if( ToriRSServer_AccountLinkPending(link) == 0 )
    {
        long size;

        fseek(link->spool, 0, SEEK_END);
        size = ftell(link->spool);
        if( size > LINK_COMPACT_BYTES )
            spool_reset(link, link->next_seq);
    }
}

void
ToriRSServer_AccountLinkPump(
    struct ToriRSServerAccountLink* link,
    long now_ms)
{
    assert(link);
    if( link->state == LINK_IDLE )
    {
        if( ToriRSServer_AccountLinkPending(link) == 0 || now_ms < link->retry_at_ms )
            return;
        if( !link_build(link) )
            return;
        link_connect(link, now_ms);
        if( link->state == LINK_IDLE )
            return;
    }
    if( now_ms > link->deadline_ms )
    {
        link_fail(link, now_ms, "timed out");
        return;
    }
    if( link->state == LINK_CONNECTING )
    {
        int error = 0;
        socklen_t len = sizeof(error);

        if( !ready_now(link->fd, 1) )
            return;
        if( getsockopt(link->fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) != 0 || error != 0 )
        {
            link_fail(link, now_ms, "not answering");
            return;
        }
        link->state = LINK_SENDING;
    }
    if( link->state == LINK_SENDING )
    {
        while( link->request_sent < link->request_len )
        {
            long n = (long)send(link->fd, link->request + link->request_sent,
                                (int)(link->request_len - link->request_sent), 0);

            if( n <= 0 )
            {
                if( n < 0 && would_block() )
                    return;
                link_fail(link, now_ms, "dropped the connection while sending");
                return;
            }
            link->request_sent += (size_t)n;
        }
        link->state = LINK_READING;
    }
    if( link->state == LINK_READING )
    {
        for( ;; )
        {
            long n;

            if( link->answer_len + 1 >= sizeof(link->answer) )
            {
                link_fail(link, now_ms, "answered too much");
                return;
            }
            n = (long)recv(link->fd, link->answer + link->answer_len,
                           (int)(sizeof(link->answer) - 1 - link->answer_len), 0);
            if( n > 0 )
            {
                link->answer_len += (size_t)n;
                if( answer_complete(link) )
                {
                    link_answered(link, now_ms);
                    return;
                }
                continue;
            }
            if( n < 0 && would_block() )
                return;
            if( n < 0 )
            {
                link_fail(link, now_ms, "dropped the connection while answering");
                return;
            }
            /* The server closes after its answer (Connection: close). */
            link_answered(link, now_ms);
            return;
        }
    }
}

void
ToriRSServer_AccountLinkWatch(
    struct ToriRSServerAccountLink* link,
    fd_set* readable,
    fd_set* writable,
    int* top)
{
    assert(link);
    assert(readable);
    assert(writable);
    assert(top);
    if( link->fd < 0 )
        return;
    if( link->state == LINK_READING )
        FD_SET(link->fd, readable);
    else
        FD_SET(link->fd, writable);
    if( link->fd > *top )
        *top = link->fd;
}

void
ToriRSServer_AccountLinkDrain(
    struct ToriRSServerAccountLink* link,
    long budget_ms)
{
    long end = link_now_ms() + budget_ms;

    assert(link);
    link->retry_at_ms = 0;
    while( ToriRSServer_AccountLinkPending(link) > 0 && link_now_ms() < end )
    {
        fd_set readable;
        fd_set writable;
        int top = -1;
        struct timeval wait = { 0, 50 * 1000 };

        ToriRSServer_AccountLinkPump(link, link_now_ms());
        /* Down: what is left waits in the spool for the next start. */
        if( link->state == LINK_IDLE && link->retry_at_ms > link_now_ms() )
            break;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        ToriRSServer_AccountLinkWatch(link, &readable, &writable, &top);
        if( top >= 0 )
            select(top + 1, &readable, &writable, NULL, &wait);
    }
    if( ToriRSServer_AccountLinkPending(link) > 0 )
        fprintf(stderr, "torirsserver: %lld account record(s) left in %s for the next start\n",
                ToriRSServer_AccountLinkPending(link), link->path);
}

void
ToriRSServer_AccountLinkClose(struct ToriRSServerAccountLink* link)
{
    if( !link )
        return;
    link_disconnect(link);
    if( link->spool )
        fclose(link->spool);
    ToriMem_Free(ToriMem_ServerNet(), link);
}
