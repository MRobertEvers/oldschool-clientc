#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_POLL_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_POLL_H
/*
 * The core loop's one wait: which sockets can make progress, and whether
 * another thread has finished work for us.
 *
 * Replaces `select()`, which cannot describe more than FD_SETSIZE descriptors
 * (1024 on macOS and Linux, 64 on Windows) and costs O(descriptors) per call
 * whether or not they are ready. Three backends, one per platform, each in its
 * own file and compiled to nothing elsewhere:
 *
 *   torirs_server_poll_kqueue.c   macOS, BSD   kqueue + EVFILT_USER wake
 *   torirs_server_poll_epoll.c    Linux        epoll + eventfd wake
 *   torirs_server_poll_wsapoll.c  Windows      WSAPoll + loopback UDP wake
 *
 * Level-triggered everywhere: a socket stays reported until the reader drains
 * it, which is what the connection code (one recv per pump) was written for.
 *
 * An `owner` is an opaque pointer handed back with the event -- the core loop
 * stores the coroutine that owns the socket, so an event goes straight to the
 * one piece of work that cares about it.
 */
#include <stdint.h>

struct ToriRSServerPoll;

struct ToriRSServerPollEvent
{
    void* owner;
    uint8_t readable;
    uint8_t writable;
    /** Hung up or errored: the owner's next read will say so. */
    uint8_t closed;
};

/** A new poller. Aborts if the platform refuses one: a server with no way to
 *  wait for its sockets cannot run. */
struct ToriRSServerPoll*
ToriRSServer_PollNew(void);

/** Deallocator; NULL accepted. Sockets still registered are not closed. */
void
ToriRSServer_PollFree(struct ToriRSServerPoll* poll);

/** Watch `fd` for reading on behalf of `owner`. */
void
ToriRSServer_PollAdd(
    struct ToriRSServerPoll* poll,
    int fd,
    void* owner);

/** Also watch for writability, or stop. Only while there is something to
 *  write: a writable socket is reported on every wait, so leaving it on with
 *  an empty queue turns the loop into a spin. */
void
ToriRSServer_PollWantWrite(
    struct ToriRSServerPoll* poll,
    int fd,
    void* owner,
    int want);

/** Stop watching `fd`. Call before closing it. */
void
ToriRSServer_PollRemove(
    struct ToriRSServerPoll* poll,
    int fd);

/**
 * Wait up to `timeout_ms` (0 = just look) for events, filling at most `max`.
 * Returns how many; 0 on timeout, on a wake, or on a signal (the caller loops
 * and checks its shutdown flag either way).
 */
int
ToriRSServer_PollWait(
    struct ToriRSServerPoll* poll,
    long timeout_ms,
    struct ToriRSServerPollEvent* events,
    int max);

/** End a PollWait in progress, from any thread. The one call here that is
 *  thread-safe: it is how the blocking-call pool says a job finished. */
void
ToriRSServer_PollWake(struct ToriRSServerPoll* poll);

#endif
