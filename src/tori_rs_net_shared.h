#ifndef TORI_RS_NET_SHARED_H
#define TORI_RS_NET_SHARED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Outbound message types: first byte of each message in out_buf.
 * Message layout: [type:1][len_lo:1][len_hi:1][payload]. Platform must interpret type using these.
 */
#define LIBTORIRS_NET_MSG_DATA 0
#define LIBTORIRS_NET_MSG_RECONNECT 1
#define LIBTORIRS_NET_MSG_HEADER_SIZE 3

#define LIBTORIRS_NET_DEFAULT_RING_SIZE 32768

typedef struct LibToriRS_NetShared
{
    uint8_t* out_buf; /* outbound ring (8-byte header + payload); game writes, platform drains */
    int out_cap;       /* size in bytes of outbound region */
    uint8_t* in_buf;   /* inbound ring (8-byte header + payload); platform writes, net reads */
    int in_cap;        /* size in bytes of inbound region */
} LibToriRS_NetShared;

void
LibToriRS_NetSharedFromBuffer(
    LibToriRS_NetShared* out,
    uint8_t* buf,
    int capacity);

struct GGame;

/* Context owning P2G and G2P ring buffers and connection state. Platform and game share this. */
struct LibToriRS_NetContext
{
    struct GGame* game;
    uint8_t* p2g_buf;
    int p2g_cap;
    uint8_t* g2p_buf;
    int g2p_cap;
    int state; /* NetStatus */
    LibToriRS_NetShared shared_view;
};
typedef struct LibToriRS_NetContext LibToriRS_NetContext;

LibToriRS_NetContext*
LibToriRS_NetNewContext(void);

void
LibToriRS_NetFreeContext(LibToriRS_NetContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* TORI_RS_NET_SHARED_H */
