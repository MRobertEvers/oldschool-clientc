#include "net/net_keepalive.h"

#include <assert.h>
#include <string.h>

void
NetKeepalive_Reset(struct NetKeepalive* keepalive)
{
    assert(keepalive);
    memset(keepalive, 0, sizeof(*keepalive));
}

void
NetKeepalive_NoteSend(
    struct NetKeepalive* keepalive,
    uint64_t now_ms)
{
    assert(keepalive);
    keepalive->last_send_ms = now_ms;
}

bool
NetKeepalive_Due(
    struct NetKeepalive const* keepalive,
    uint64_t now_ms,
    uint64_t idle_ms)
{
    assert(keepalive);
    return now_ms - keepalive->last_send_ms >= idle_ms;
}
