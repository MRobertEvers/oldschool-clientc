#ifndef LIBTORIRS_PLATFORM_C_H
#define LIBTORIRS_PLATFORM_C_H

#include "platforms/common/sockstream.h"
#include "tori_rs_net_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LibToriRSPlatformC_NetPoll_Ok            0
#define LibToriRSPlatformC_NetPoll_NotConnected  1
#define LibToriRSPlatformC_NetPoll_Error        (-1)
#define LibToriRSPlatformC_NetPoll_ReconnectRequested (-2)

/**
 * Poll socket and net context: drain G2P to stream, recv from stream into P2G.
 * Updates ctx->state (NET_CONNECTED, NET_IDLE, NET_ERROR).
 * Returns LibToriRSPlatformC_NetPoll_Ok, _NotConnected, _Error, or _ReconnectRequested.
 * When _ReconnectRequested, caller should close stream and reconnect.
 */
int
LibToriRSPlatformC_NetPoll(
    struct SockStream* stream,
    LibToriRS_NetContext* ctx,
    uint8_t* recv_scratch,
    int recv_scratch_size,
    int* out_reconnect_requested);

#ifdef __cplusplus
}
#endif

#endif /* LIBTORIRS_PLATFORM_C_H */
