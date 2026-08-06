#ifndef SRC_JS5_SERVER_JS5_SERVER_SESSION_H
#define SRC_JS5_SERVER_JS5_SERVER_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Js5ServerCache;
struct Js5ServerSession;

enum Js5ServerSessionState
{
    JS5_SERVER_SESSION_HANDSHAKE = 0,
    JS5_SERVER_SESSION_ACTIVE,
    JS5_SERVER_SESSION_CLOSING,
    JS5_SERVER_SESSION_CLOSED,
    JS5_SERVER_SESSION_FAILED,
};

enum Js5ServerSessionError
{
    JS5_SERVER_SESSION_ERROR_NONE = 0,
    JS5_SERVER_SESSION_ERROR_PROTOCOL,
    JS5_SERVER_SESSION_ERROR_QUEUE_FULL,
    JS5_SERVER_SESSION_ERROR_CACHE_MISS,
    JS5_SERVER_SESSION_ERROR_CACHE,
    JS5_SERVER_SESSION_ERROR_TIMEOUT,
    JS5_SERVER_SESSION_ERROR_NO_MEMORY,
};

struct Js5ServerSessionConfig
{
    uint32_t revision;
    uint32_t max_pending_per_lane;
    uint32_t handshake_timeout_ms;
    uint32_t idle_timeout_ms;
    uint32_t output_timeout_ms;
    bool server_full;
};

struct Js5ServerSessionStats
{
    enum Js5ServerSessionState state;
    enum Js5ServerSessionError error;
    uint32_t client_revision;
    uint32_t queued_urgent;
    uint32_t queued_normal;
    uint64_t requests_received;
    uint64_t responses_served;
    uint64_t bytes_sent;
    uint64_t cache_misses;
    uint8_t xor_key;
    bool logged_in;
};

void
Js5ServerSessionConfigInit(struct Js5ServerSessionConfig* config);

struct Js5ServerSession*
Js5ServerSessionNew(
    struct Js5ServerCache* cache,
    const struct Js5ServerSessionConfig* config,
    uint64_t now_ms);

void
Js5ServerSessionFree(struct Js5ServerSession* session);

/* Feed plaintext client bytes. Returns 0 on success and -1 on terminal error. */
int
Js5ServerSessionFeed(
    struct Js5ServerSession* session,
    const uint8_t* data,
    size_t size,
    uint64_t now_ms);

/*
 * Return the next contiguous response bytes. The view remains valid until
 * ConsumeOutput or Free. Returns 1 for bytes, 0 when idle, and -1 on failure.
 */
int
Js5ServerSessionPeekOutput(
    struct Js5ServerSession* session,
    const uint8_t** data,
    size_t* size);

int
Js5ServerSessionConsumeOutput(
    struct Js5ServerSession* session,
    size_t size,
    uint64_t now_ms);

void
Js5ServerSessionTick(
    struct Js5ServerSession* session,
    uint64_t now_ms);

bool
Js5ServerSessionWantsClose(const struct Js5ServerSession* session);

void
Js5ServerSessionGetStats(
    const struct Js5ServerSession* session,
    struct Js5ServerSessionStats* stats);

#ifdef __cplusplus
}
#endif

#endif
