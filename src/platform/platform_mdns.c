#include "platform_mdns.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "log/torirs_log.h"

/*
 * Where this can run at all.
 *
 * Every host with BSD sockets can do this; the web lane cannot. Emscripten's
 * socket layer tunnels through a WebSocket bridge and has no notion of a
 * multicast group, so a query there would not fail loudly -- it would sit in a
 * select() until the budget expired, on every boot, for a name it can never
 * answer. Compiling the body out makes the miss free instead.
 */
#if defined(__EMSCRIPTEN__) || defined(TORIRS_PLATFORM_WEB)
#define TORIRS_MDNS_AVAILABLE 0
#else
#define TORIRS_MDNS_AVAILABLE 1
#endif

#if TORIRS_MDNS_AVAILABLE
/* The same include split sockstream.c and platform_socket.c use. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
/* GetTickCount, for the receive deadline. winsock2.h must come first. */
#include <windows.h>
typedef SOCKET mdns_socket_t;
/* WinSock's send/recv take a signed int length where POSIX takes a size_t;
 * spelling it once here keeps the call sites free of #if. */
typedef int mdns_iolen_t;
#define MDNS_INVALID_SOCKET INVALID_SOCKET
#define mdns_close_socket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int mdns_socket_t;
typedef size_t mdns_iolen_t;
#define MDNS_INVALID_SOCKET (-1)
#define mdns_close_socket close
#endif
#endif /* TORIRS_MDNS_AVAILABLE */

/*
 * The budget. Three tries at 200 ms is 600 ms worst case, which is the whole
 * cost of a name nothing on the link owns. A responder that IS present answers
 * in single-digit milliseconds, so the retries only ever pay for a dropped
 * datagram -- and multicast UDP is unacknowledged, so they do get dropped.
 */
#define TORIRS_MDNS_ATTEMPTS 3
#define TORIRS_MDNS_TIMEOUT_MS 200

#define TORIRS_MDNS_GROUP "224.0.0.251"
#define TORIRS_MDNS_PORT 5353

/* RFC 1035 caps a wire name at 255 bytes; +1 for the NUL of the dotted form. */
#define TORIRS_MDNS_NAME_MAX 256
/* One datagram. mDNS responses are small; anything larger is not for us. */
#define TORIRS_MDNS_PACKET_MAX 2048

static char
mdns_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int
PlatformMdns_IsLocalName(const char* host)
{
    static const char suffix[] = ".local";
    size_t len;
    size_t suffix_len;

    assert(host);

    len = strlen(host);
    /* A trailing dot is the fully-qualified spelling of the same name. */
    if( len > 0 && host[len - 1] == '.' )
        len--;
    suffix_len = sizeof(suffix) - 1;
    if( len <= suffix_len )
        return 0;
    for( size_t i = 0; i < suffix_len; i++ )
    {
        if( mdns_lower(host[len - suffix_len + i]) != suffix[i] )
            return 0;
    }
    return 1;
}

#if TORIRS_MDNS_AVAILABLE

/*
 * Milliseconds from an arbitrary origin, for the receive deadline.
 *
 * Only DIFFERENCES are used, so the origin is irrelevant -- but the source has
 * to be one the caller cannot be starved by. select()'s timeval is not usable
 * for this: Linux writes the remaining time back into it, macOS and WinSock do
 * not, so a loop that trusted the struct would spin forever on the two hosts
 * that leave it alone.
 */
static int64_t
mdns_now_ms(void)
{
#ifdef _WIN32
    return (int64_t)GetTickCount();
#else
    struct timeval now;
    gettimeofday(&now, NULL);
    return ((int64_t)now.tv_sec * 1000) + ((int64_t)now.tv_usec / 1000);
#endif
}

/* Case-insensitive compare that also treats a single trailing dot as absent --
 * "matthewllm.local" and "matthewllm.local." are the same name on the wire. */
static int
mdns_names_equal(
    const char* lhs,
    const char* rhs)
{
    size_t lhs_len;
    size_t rhs_len;

    assert(lhs);
    assert(rhs);

    lhs_len = strlen(lhs);
    rhs_len = strlen(rhs);
    while( lhs_len > 0 && lhs[lhs_len - 1] == '.' )
        lhs_len--;
    while( rhs_len > 0 && rhs[rhs_len - 1] == '.' )
        rhs_len--;
    if( lhs_len != rhs_len )
        return 0;
    for( size_t i = 0; i < lhs_len; i++ )
    {
        if( mdns_lower(lhs[i]) != mdns_lower(rhs[i]) )
            return 0;
    }
    return 1;
}

/*
 * Encode a dotted name into DNS label form at `out`.
 *
 * Returns the number of bytes written, or -1 if the name does not fit the wire
 * format (a label over 63 bytes, an empty label, or a name over 255 bytes).
 * That is a caller-supplied hostname, not a caller bug: manifests are edited by
 * hand and a malformed one must be rejected, not asserted on.
 */
static int
mdns_name_write(
    const char* name,
    uint8_t* out,
    int out_cap)
{
    int written = 0;
    const char* label = name;

    assert(name);
    assert(out);

    for( ;; )
    {
        const char* dot = strchr(label, '.');
        int label_len = dot ? (int)(dot - label) : (int)strlen(label);

        if( label_len == 0 )
        {
            /* A trailing dot ends the name; an empty label anywhere else (".."
             * or a leading dot) is malformed. */
            if( dot && *(dot + 1) == '\0' && written > 0 )
                break;
            return -1;
        }
        if( label_len > 63 )
            return -1;
        if( written + 1 + label_len + 1 > out_cap )
            return -1;
        out[written++] = (uint8_t)label_len;
        memcpy(out + written, label, (size_t)label_len);
        written += label_len;
        if( !dot )
            break;
        label = dot + 1;
        if( *label == '\0' )
            break;
    }
    if( written + 1 > out_cap )
        return -1;
    out[written++] = 0; /* root label */
    return written;
}

/*
 * Decode a DNS name at `offset`, following 0xC0 compression pointers.
 *
 * mDNS responders compress ROUTINELY -- the answer's owner name is almost
 * always a two-byte pointer back into the question, and the additional-section
 * AAAA record points at the answer. A parser that treats 0xC0 as a length byte
 * reads 192 bytes of whatever follows and then misreads every record after it,
 * so this is not an optional refinement.
 *
 * Writes the dotted, lowercased name to `out`. Returns the number of bytes
 * consumed AT `offset` -- which is where the record stream continues, and is
 * NOT the length of the name once a pointer was followed -- or -1 if the packet
 * is malformed. Every read is bounds-checked against `len`: this is data off
 * the network, and a hostile responder is as likely as a buggy one.
 */
static int
mdns_name_read(
    const uint8_t* buf,
    int len,
    int offset,
    char* out,
    int out_cap)
{
    int pos = offset;
    int consumed = -1;
    int jumps = 0;
    int out_len = 0;

    assert(buf);
    assert(out);
    assert(out_cap > 0);

    if( offset < 0 || offset >= len )
        return -1;

    for( ;; )
    {
        uint8_t c;

        if( pos < 0 || pos >= len )
            return -1;
        c = buf[pos];

        if( (c & 0xC0) == 0xC0 )
        {
            int target;

            if( pos + 1 >= len )
                return -1;
            target = ((int)(c & 0x3F) << 8) | (int)buf[pos + 1];
            if( consumed < 0 )
                consumed = pos + 2 - offset;
            /* A pointer must go BACKWARDS. Two records pointing at each other
             * is the classic decompression bomb; the jump cap is the second
             * belt, but refusing a forward pointer is what makes progress
             * monotonic. */
            if( target >= pos )
                return -1;
            pos = target;
            if( ++jumps > 64 )
                return -1;
            continue;
        }
        if( (c & 0xC0) != 0 )
            return -1; /* 0x40/0x80 label types are reserved */

        if( c == 0 )
        {
            pos++;
            if( consumed < 0 )
                consumed = pos - offset;
            break;
        }

        if( pos + 1 + (int)c > len )
            return -1;
        if( out_len > 0 )
        {
            if( out_len + 1 >= out_cap )
                return -1;
            out[out_len++] = '.';
        }
        if( out_len + (int)c >= out_cap )
            return -1;
        for( int i = 0; i < (int)c; i++ )
            out[out_len++] = mdns_lower((char)buf[pos + 1 + i]);
        pos += 1 + (int)c;
    }

    out[out_len] = '\0';
    return consumed;
}

/*
 * Pick the interface the multicast should leave by.
 *
 * A UDP connect() to the group address performs a route lookup without sending
 * anything, so getsockname then reports the source address the kernel would
 * have used. Android phones routinely hold more than one route at once (wlan0
 * plus a cellular default, plus whatever a VPN added), and the default route is
 * often the one that CANNOT carry link-local multicast -- so leaving the choice
 * implicit is how a query goes out the mobile interface and is never heard.
 *
 * Best-effort: a failure here leaves the socket on INADDR_ANY, which is the
 * behaviour we would have had anyway.
 */
static void
mdns_bind_multicast_interface(
    mdns_socket_t sock,
    const struct sockaddr_in* group)
{
    mdns_socket_t probe;
    struct sockaddr_in local;
#ifdef _WIN32
    int local_len = (int)sizeof(local);
#else
    socklen_t local_len = (socklen_t)sizeof(local);
#endif

    assert(group);

    probe = socket(AF_INET, SOCK_DGRAM, 0);
    if( probe == MDNS_INVALID_SOCKET )
        return;
    memset(&local, 0, sizeof(local));
    if( connect(probe, (const struct sockaddr*)group, sizeof(*group)) == 0 &&
        getsockname(probe, (struct sockaddr*)&local, &local_len) == 0 &&
        local.sin_family == AF_INET && local.sin_addr.s_addr != 0 )
    {
        setsockopt(
            sock,
            IPPROTO_IP,
            IP_MULTICAST_IF,
            (const char*)&local.sin_addr,
            (int)sizeof(local.sin_addr));
    }
    mdns_close_socket(probe);
}

/*
 * Scan one response datagram for an A record whose owner name is `host`.
 *
 * Returns 1 and stores the address (network byte order) on a match, 0
 * otherwise -- including for every malformed shape. Nothing in here asserts:
 * every byte it touches came off the network, so a truncated section, a
 * nonsense RDLENGTH or a compression pointer into the weeds are all runtime
 * states this must survive, not contract violations by the caller.
 */
static int
mdns_response_find_a(
    const uint8_t* buf,
    int len,
    const char* host,
    uint32_t* out_addr_net)
{
    char decoded[TORIRS_MDNS_NAME_MAX];
    int qdcount;
    int ancount;
    int pos;

    assert(buf);
    assert(host);
    assert(out_addr_net);

    if( len < 12 )
        return 0;
    /* QR must be set. Our own multicast query loops back to this socket when
     * IP_MULTICAST_LOOP is on, and it carries the same name we asked about. */
    if( (buf[2] & 0x80) == 0 )
        return 0;
    qdcount = ((int)buf[4] << 8) | (int)buf[5];
    ancount = ((int)buf[6] << 8) | (int)buf[7];
    if( ancount <= 0 )
        return 0;

    pos = 12;
    /* Skip the echoed question section. Its name can itself be compressed,
     * which is why this goes through the same reader rather than a length
     * walk -- getting it wrong here desynchronises every answer that follows. */
    for( int i = 0; i < qdcount; i++ )
    {
        int used = mdns_name_read(buf, len, pos, decoded, sizeof(decoded));
        if( used < 0 )
            return 0;
        pos += used + 4; /* QTYPE + QCLASS */
        if( pos > len )
            return 0;
    }

    for( int i = 0; i < ancount; i++ )
    {
        int used;
        int rtype;
        int rdlength;

        used = mdns_name_read(buf, len, pos, decoded, sizeof(decoded));
        if( used < 0 )
            return 0;
        pos += used;
        /* TYPE(2) CLASS(2) TTL(4) RDLENGTH(2) */
        if( pos + 10 > len )
            return 0;
        rtype = ((int)buf[pos] << 8) | (int)buf[pos + 1];
        rdlength = ((int)buf[pos + 8] << 8) | (int)buf[pos + 9];
        pos += 10;
        if( pos + rdlength > len )
            return 0;

        if( rtype == 1 && rdlength == 4 && mdns_names_equal(decoded, host) )
        {
            uint32_t addr;
            memcpy(&addr, buf + pos, 4);
            *out_addr_net = addr;
            return 1;
        }
        pos += rdlength;
    }
    return 0;
}

int
PlatformMdns_ResolveIpv4(
    const char* host,
    uint32_t* out_addr_net)
{
    uint8_t query[12 + TORIRS_MDNS_NAME_MAX + 4];
    uint8_t response[TORIRS_MDNS_PACKET_MAX];
    struct sockaddr_in group;
    mdns_socket_t sock;
    int64_t deadline_ms;
    int name_len;
    int query_len;
    int ttl = 255;
    int loop = 1;
    int found = 0;

    assert(host);
    assert(out_addr_net);

    /* --- the question ------------------------------------------------------
     *
     * id 0: mDNS ignores it (RFC 6762 s18.1), and the reply is matched by NAME
     * below, which is the only thing that actually proves the answer is ours.
     * flags 0, qdcount 1, everything else 0. */
    memset(query, 0, sizeof(query));
    query[5] = 1; /* qdcount low byte */
    name_len = mdns_name_write(host, query + 12, TORIRS_MDNS_NAME_MAX);
    if( name_len < 0 )
    {
        TORIRS_LOG("mdns: '%s' is not an encodable DNS name\n", host);
        return 0;
    }
    query_len = 12 + name_len;
    query[query_len++] = 0x00; /* QTYPE  = A (1) */
    query[query_len++] = 0x01;
    /*
     * QCLASS = IN with the top bit set: the QU ("unicast response requested")
     * bit of RFC 6762 s5.4. It asks the responder to reply straight to this
     * socket's ephemeral port rather than re-multicasting to the whole link.
     * That matters twice over here: the reply arrives without us having to bind
     * 5353 (which mDNSResponder already owns on macOS, and which would need
     * SO_REUSEPORT and a group join to share), and it arrives immediately
     * instead of after the 20-120 ms delay a responder applies to a shared
     * multicast answer.
     */
    query[query_len++] = 0x80;
    query[query_len++] = 0x01;

    memset(&group, 0, sizeof(group));
    group.sin_family = AF_INET;
    group.sin_port = htons(TORIRS_MDNS_PORT);
#ifdef _WIN32
    group.sin_addr.s_addr = inet_addr(TORIRS_MDNS_GROUP);
#else
    if( inet_pton(AF_INET, TORIRS_MDNS_GROUP, &group.sin_addr) != 1 )
        return 0;
#endif

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if( sock == MDNS_INVALID_SOCKET )
        return 0;

    /* RFC 6762 s11: mDNS packets are sent with TTL 255 and responders MAY drop
     * anything else. The default is 1, which happens to work on most stacks --
     * "happens to" is not what a boot path should rely on. */
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, (int)sizeof(ttl));
    /* Loopback on, so a query for THIS machine's own name is answered by the
     * responder in this same host. That is the macOS developer case. */
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, (int)sizeof(loop));
    mdns_bind_multicast_interface(sock, &group);

    for( int attempt = 0; attempt < TORIRS_MDNS_ATTEMPTS && !found; attempt++ )
    {
        if( sendto(
                sock,
                (const char*)query,
                (mdns_iolen_t)query_len,
                0,
                (const struct sockaddr*)&group,
                sizeof(group)) < 0 )
        {
            continue;
        }

        /*
         * One attempt may see several datagrams: the socket is on an ephemeral
         * port, but a responder is free to answer by multicast anyway, and
         * another host's unrelated answer can arrive in between. So the
         * timeout is a DEADLINE for the whole attempt, re-derived from the
         * clock after every packet -- not a fresh 200 ms per recvfrom, which
         * on a chatty link is a loop with no end.
         */
        deadline_ms = mdns_now_ms() + TORIRS_MDNS_TIMEOUT_MS;
        for( ;; )
        {
            fd_set readable;
            struct timeval timeout;
            int64_t remaining_ms = deadline_ms - mdns_now_ms();
            int wait_ms;
            int received;

            if( remaining_ms <= 0 )
                break;

            /* Bounded by the attempt budget above, so the narrowing is safe and
             * the field types (suseconds_t is an int on macOS, a long on
             * Windows) are reached by widening rather than by a cast that has
             * to be right on every host. */
            wait_ms = (int)remaining_ms;
            FD_ZERO(&readable);
            FD_SET(sock, &readable);
            timeout.tv_sec = wait_ms / 1000;
            timeout.tv_usec = (wait_ms % 1000) * 1000;
#ifdef _WIN32
            if( select(0, &readable, NULL, NULL, &timeout) <= 0 )
                break;
#else
            if( select((int)sock + 1, &readable, NULL, NULL, &timeout) <= 0 )
                break;
#endif

            received =
                (int)recvfrom(sock, (char*)response, (mdns_iolen_t)sizeof(response), 0, NULL, NULL);
            if( received <= 0 )
                break;
            if( mdns_response_find_a(response, received, host, out_addr_net) )
            {
                found = 1;
                break;
            }
        }
    }

    mdns_close_socket(sock);

    if( found )
    {
        const uint8_t* octets = (const uint8_t*)out_addr_net;
        TORIRS_LOG(
            "mdns: %s -> %u.%u.%u.%u\n",
            host,
            octets[0],
            octets[1],
            octets[2],
            octets[3]);
    }
    else
    {
        TORIRS_LOG("mdns: no answer for %s\n", host);
    }
    return found;
}

#else /* !TORIRS_MDNS_AVAILABLE */

int
PlatformMdns_ResolveIpv4(
    const char* host,
    uint32_t* out_addr_net)
{
    assert(host);
    assert(out_addr_net);
    /* No multicast sockets on this lane; the caller's existing resolver path is
     * the whole answer here. */
    return 0;
}

#endif /* TORIRS_MDNS_AVAILABLE */
