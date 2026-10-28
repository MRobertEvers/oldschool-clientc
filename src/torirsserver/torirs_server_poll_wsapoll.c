/* The poller on Windows. See torirs_server_poll.h. */
#if defined(_WIN32)

#include "torirs_server_poll.h"

#include "mem/torirs_mem.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * WSAPoll over a flat array: O(sockets) per wait, with no FD_SETSIZE ceiling.
 * At a few thousand sockets and a wait per burst of traffic that is a few
 * microseconds of scanning; an IOCP backend can replace this file without the
 * interface changing.
 *
 * Slot 0 is the wake: a UDP socket bound to loopback and connected to itself,
 * so PollWake is one send() from any thread and the next WSAPoll returns.
 */
struct ToriRSServerPoll
{
    WSAPOLLFD* fds;
    void** owners;
    int count;
    int capacity;
    SOCKET wake;
    CRITICAL_SECTION wake_lock;
};

static int
poll_find(
    const struct ToriRSServerPoll* poll,
    int fd)
{
    for( int i = 1; i < poll->count; i++ )
        if( poll->fds[i].fd == (SOCKET)fd )
            return i;
    return -1;
}

struct ToriRSServerPoll*
ToriRSServer_PollNew(void)
{
    struct ToriRSServerPoll* poll = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*poll));
    struct sockaddr_in addr;
    int len = (int)sizeof(addr);
    u_long nonblocking = 1;

    assert(poll);
    poll->capacity = 64;
    poll->fds = ToriMem_Calloc(ToriMem_ServerNet(), (size_t)poll->capacity, sizeof(*poll->fds));
    poll->owners = ToriMem_Calloc(ToriMem_ServerNet(), (size_t)poll->capacity, sizeof(*poll->owners));
    assert(poll->fds);
    assert(poll->owners);

    poll->wake = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if( poll->wake == INVALID_SOCKET )
    {
        fprintf(stderr, "torirsserver: wake socket failed (WSA %d)\n", WSAGetLastError());
        abort();
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if( bind(poll->wake, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        getsockname(poll->wake, (struct sockaddr*)&addr, &len) != 0 ||
        connect(poll->wake, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        fprintf(stderr, "torirsserver: wake socket setup failed (WSA %d)\n", WSAGetLastError());
        abort();
    }
    ioctlsocket(poll->wake, FIONBIO, &nonblocking);
    InitializeCriticalSection(&poll->wake_lock);
    poll->fds[0].fd = poll->wake;
    poll->fds[0].events = POLLRDNORM;
    poll->count = 1;
    return poll;
}

void
ToriRSServer_PollFree(struct ToriRSServerPoll* poll)
{
    if( !poll )
        return;
    closesocket(poll->wake);
    DeleteCriticalSection(&poll->wake_lock);
    ToriMem_Free(ToriMem_ServerNet(), poll->fds);
    ToriMem_Free(ToriMem_ServerNet(), poll->owners);
    ToriMem_Free(ToriMem_ServerNet(), poll);
}

void
ToriRSServer_PollAdd(
    struct ToriRSServerPoll* poll,
    int fd,
    void* owner)
{
    assert(poll);
    assert(fd >= 0);
    assert(poll_find(poll, fd) < 0);
    if( poll->count == poll->capacity )
    {
        poll->capacity *= 2;
        poll->fds = ToriMem_Realloc(ToriMem_ServerNet(), poll->fds,
                                    (size_t)poll->capacity * sizeof(*poll->fds));
        poll->owners = ToriMem_Realloc(ToriMem_ServerNet(), poll->owners,
                                       (size_t)poll->capacity * sizeof(*poll->owners));
        assert(poll->fds);
        assert(poll->owners);
    }
    poll->fds[poll->count].fd = (SOCKET)fd;
    poll->fds[poll->count].events = POLLRDNORM;
    poll->fds[poll->count].revents = 0;
    poll->owners[poll->count] = owner;
    poll->count++;
}

void
ToriRSServer_PollWantWrite(
    struct ToriRSServerPoll* poll,
    int fd,
    void* owner,
    int want)
{
    int i;

    assert(poll);
    i = poll_find(poll, fd);
    assert(i > 0);
    poll->owners[i] = owner;
    poll->fds[i].events = (SHORT)(POLLRDNORM | (want ? POLLWRNORM : 0));
}

void
ToriRSServer_PollRemove(
    struct ToriRSServerPoll* poll,
    int fd)
{
    int i;

    assert(poll);
    i = poll_find(poll, fd);
    if( i < 0 )
        return;
    poll->count--;
    poll->fds[i] = poll->fds[poll->count];
    poll->owners[i] = poll->owners[poll->count];
}

int
ToriRSServer_PollWait(
    struct ToriRSServerPoll* poll,
    long timeout_ms,
    struct ToriRSServerPollEvent* events,
    int max)
{
    int n;
    int out = 0;

    assert(poll);
    assert(events);
    assert(max > 0);
    n = WSAPoll(poll->fds, (ULONG)poll->count, (INT)timeout_ms);
    if( n == SOCKET_ERROR )
    {
        if( WSAGetLastError() == WSAEINTR )
            return 0;
        fprintf(stderr, "torirsserver: WSAPoll failed (WSA %d)\n", WSAGetLastError());
        abort();
    }
    if( poll->fds[0].revents )
    {
        char drain[64];

        while( recv(poll->wake, drain, sizeof(drain), 0) > 0 )
            ;
        poll->fds[0].revents = 0;
    }
    for( int i = 1; i < poll->count && out < max; i++ )
    {
        SHORT revents = poll->fds[i].revents;
        struct ToriRSServerPollEvent* event;

        if( !revents )
            continue;
        poll->fds[i].revents = 0;
        event = &events[out++];
        event->owner = poll->owners[i];
        event->readable = (revents & (POLLRDNORM | POLLHUP | POLLERR)) != 0;
        event->writable = (revents & POLLWRNORM) != 0;
        event->closed = (revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
    }
    return out;
}

void
ToriRSServer_PollWake(struct ToriRSServerPoll* poll)
{
    char one = 1;

    assert(poll);
    EnterCriticalSection(&poll->wake_lock);
    send(poll->wake, &one, 1, 0);
    LeaveCriticalSection(&poll->wake_lock);
}

#endif
