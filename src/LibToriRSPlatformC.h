#ifndef LIBTORIRS_PLATFORM_C_H
#define LIBTORIRS_PLATFORM_C_H

#include "platforms/common/sockstream.h"
#include "tori_rs.h"

#ifdef __cplusplus
extern "C" {
#endif

void
LibToriRSPlatformC_NetPoll(
    struct ToriRSNetSharedBuffer* sb,
    struct SockStream* stream_ptr);

#ifdef __cplusplus
}
#endif

#endif /* LIBTORIRS_PLATFORM_C_H */
