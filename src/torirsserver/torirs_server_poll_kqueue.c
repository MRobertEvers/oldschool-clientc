/* The poller on macOS and the BSDs. See torirs_server_poll.h. */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include "torirs_server_poll.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

/* The EVFILT_USER ident PollWake triggers. Descriptor idents and user idents
 * live in separate filters, so any value is free. */
#define POLL_WAKE_IDENT 1

struct ToriRSServerPoll
{
    int kq;
};

static void
poll_change(
    struct ToriRSServerPoll* poll,
    int fd,
    int16_t filter,
    uint16_t flags,
    void* owner)
{
    struct kevent change;

    EV_SET(&change, (uintptr_t)fd, filter, flags, 0, 0, owner);
    /* A delete of a filter that was never added is ENOENT; that is the only
     * failure the callers can cause, and it means "already not watched". */
    if( kevent(poll->kq, &change, 1, NULL, 0, NULL) < 0 && errno != ENOENT )
    {
        perror("torirsserver: kevent change");
        abort();
    }
}

struct ToriRSServerPoll*
ToriRSServer_PollNew(void)
{
    struct ToriRSServerPoll* poll = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*poll));
    struct kevent wake;

    assert(poll);
    poll->kq = kqueue();
    if( poll->kq < 0 )
    {
        perror("torirsserver: kqueue");
        abort();
    }
    EV_SET(&wake, POLL_WAKE_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if( kevent(poll->kq, &wake, 1, NULL, 0, NULL) < 0 )
    {
        perror("torirsserver: kevent wake");
        abort();
    }
    return poll;
}

void
ToriRSServer_PollFree(struct ToriRSServerPoll* poll)
{
    if( !poll )
        return;
    close(poll->kq);
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
    poll_change(poll, fd, EVFILT_READ, EV_ADD, owner);
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
    poll_change(poll, fd, EVFILT_WRITE, want ? EV_ADD : EV_DELETE, owner);
}

void
ToriRSServer_PollRemove(
    struct ToriRSServerPoll* poll,
    int fd)
{
    assert(poll);
    assert(fd >= 0);
    poll_change(poll, fd, EVFILT_READ, EV_DELETE, NULL);
    poll_change(poll, fd, EVFILT_WRITE, EV_DELETE, NULL);
}

int
ToriRSServer_PollWait(
    struct ToriRSServerPoll* poll,
    long timeout_ms,
    struct ToriRSServerPollEvent* events,
    int max)
{
    struct kevent got[256];
    struct timespec timeout;
    int n;
    int out = 0;

    assert(poll);
    assert(events);
    assert(max > 0);
    if( max > (int)(sizeof(got) / sizeof(got[0])) )
        max = (int)(sizeof(got) / sizeof(got[0]));
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;
    n = kevent(poll->kq, NULL, 0, got, max, &timeout);
    if( n < 0 )
    {
        if( errno == EINTR )
            return 0;
        perror("torirsserver: kevent wait");
        abort();
    }
    for( int i = 0; i < n; i++ )
    {
        struct ToriRSServerPollEvent* event;

        if( got[i].filter == EVFILT_USER )
            continue;
        event = &events[out++];
        event->owner = got[i].udata;
        event->readable = got[i].filter == EVFILT_READ;
        event->writable = got[i].filter == EVFILT_WRITE;
        event->closed = (got[i].flags & (EV_EOF | EV_ERROR)) != 0;
    }
    return out;
}

void
ToriRSServer_PollWake(struct ToriRSServerPoll* poll)
{
    struct kevent wake;

    assert(poll);
    EV_SET(&wake, POLL_WAKE_IDENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    kevent(poll->kq, &wake, 1, NULL, 0, NULL);
}

#endif
