/* The poller on Linux. See torirs_server_poll.h. */
#if defined(__linux__)

#include "torirs_server_poll.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

/*
 * epoll keeps one registration per descriptor carrying both interests, so the
 * read/write split the interface offers is folded into one mask here. The
 * owner pointer rides in `data.ptr`; the wake eventfd is registered with a
 * NULL owner and filtered out of the results.
 */
struct ToriRSServerPoll
{
    int ep;
    int wake_fd;
};

static void
poll_ctl(
    struct ToriRSServerPoll* poll,
    int op,
    int fd,
    uint32_t mask,
    void* owner)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = mask;
    event.data.ptr = owner;
    if( epoll_ctl(poll->ep, op, fd, &event) < 0 )
    {
        /* Removing a descriptor that is not registered is the one benign case. */
        if( op == EPOLL_CTL_DEL && (errno == ENOENT || errno == EBADF) )
            return;
        perror("torirsserver: epoll_ctl");
        abort();
    }
}

struct ToriRSServerPoll*
ToriRSServer_PollNew(void)
{
    struct ToriRSServerPoll* poll = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*poll));

    assert(poll);
    poll->ep = epoll_create1(EPOLL_CLOEXEC);
    if( poll->ep < 0 )
    {
        perror("torirsserver: epoll_create1");
        abort();
    }
    poll->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if( poll->wake_fd < 0 )
    {
        perror("torirsserver: eventfd");
        abort();
    }
    poll_ctl(poll, EPOLL_CTL_ADD, poll->wake_fd, EPOLLIN, NULL);
    return poll;
}

void
ToriRSServer_PollFree(struct ToriRSServerPoll* poll)
{
    if( !poll )
        return;
    close(poll->wake_fd);
    close(poll->ep);
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
    poll_ctl(poll, EPOLL_CTL_ADD, fd, EPOLLIN | EPOLLRDHUP, owner);
}

void
ToriRSServer_PollWantWrite(
    struct ToriRSServerPoll* poll,
    int fd,
    void* owner,
    int want)
{
    assert(poll);
    assert(fd >= 0);
    poll_ctl(poll, EPOLL_CTL_MOD, fd, EPOLLIN | EPOLLRDHUP | (want ? EPOLLOUT : 0u), owner);
}

void
ToriRSServer_PollRemove(
    struct ToriRSServerPoll* poll,
    int fd)
{
    assert(poll);
    assert(fd >= 0);
    poll_ctl(poll, EPOLL_CTL_DEL, fd, 0, NULL);
}

int
ToriRSServer_PollWait(
    struct ToriRSServerPoll* poll,
    long timeout_ms,
    struct ToriRSServerPollEvent* events,
    int max)
{
    struct epoll_event got[256];
    int n;
    int out = 0;

    assert(poll);
    assert(events);
    assert(max > 0);
    if( max > (int)(sizeof(got) / sizeof(got[0])) )
        max = (int)(sizeof(got) / sizeof(got[0]));
    n = epoll_wait(poll->ep, got, max, (int)timeout_ms);
    if( n < 0 )
    {
        if( errno == EINTR )
            return 0;
        perror("torirsserver: epoll_wait");
        abort();
    }
    for( int i = 0; i < n; i++ )
    {
        struct ToriRSServerPollEvent* event;

        if( !got[i].data.ptr )
        {
            uint64_t drained;

            /* The wake: empty the counter so the next wait sleeps again. */
            while( read(poll->wake_fd, &drained, sizeof(drained)) > 0 )
                ;
            continue;
        }
        event = &events[out++];
        event->owner = got[i].data.ptr;
        event->readable = (got[i].events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0;
        event->writable = (got[i].events & EPOLLOUT) != 0;
        event->closed = (got[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0;
    }
    return out;
}

void
ToriRSServer_PollWake(struct ToriRSServerPoll* poll)
{
    uint64_t one = 1;

    assert(poll);
    /* EAGAIN means the counter is already non-zero: the wake is pending. */
    if( write(poll->wake_fd, &one, sizeof(one)) < 0 && errno != EAGAIN )
        perror("torirsserver: eventfd write");
}

#endif
