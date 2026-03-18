#ifndef LIB_TORI_PLATFORM_H
#define LIB_TORI_PLATFORM_H

#include "platforms/common/sockstream.h"
#include "tori_rs_net_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NetPoll return values: 0 = connected and polled; 1 = not connected (stream null or !connected);
 * -1 = fatal socket error (caller should close stream and set status to idle).
 * Performs connected check, then (1) drain shared->out_buf (message-based: DATA to stream, RECONNECT sets out_reconnect_requested);
 * (2) recv from stream and append to shared->in_buf. Does not close the stream. No callbacks.
 * If out_reconnect_requested is non-NULL and a RECONNECT message was seen, sets *out_reconnect_requested to 1. */
#define LibToriPlatform_NetPoll_Ok           0
#define LibToriPlatform_NetPoll_NotConnected 1
#define LibToriPlatform_NetPoll_Error       (-1)
int
LibToriPlatform_NetPoll(
    struct SockStream* stream,
    const LibToriRS_NetShared* shared,
    uint8_t* recv_scratch,
    int recv_scratch_size,
    int* out_reconnect_requested);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TORI_PLATFORM_H */
