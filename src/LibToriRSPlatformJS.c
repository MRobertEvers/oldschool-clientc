#if defined(__EMSCRIPTEN__)

#include <emscripten.h>
#include "tori_rs_net_shared.h"

/* Pointers are to the start of the ring buffer (8-byte header + payload). */

EMSCRIPTEN_KEEPALIVE
uint8_t*
LibToriRSPlatformJS_GetP2GDataPtr(LibToriRS_NetContext* ctx)
{
    return ctx ? ctx->p2g_buf : NULL;
}

EMSCRIPTEN_KEEPALIVE
uint8_t*
LibToriRSPlatformJS_GetG2PDataPtr(LibToriRS_NetContext* ctx)
{
    return ctx ? ctx->g2p_buf : NULL;
}

EMSCRIPTEN_KEEPALIVE
uint32_t
LibToriRSPlatformJS_GetP2GWritePos(LibToriRS_NetContext* ctx)
{
    if( !ctx || !ctx->p2g_buf || ctx->p2g_cap < 9 )
        return 0;
    return (uint32_t)(ctx->p2g_buf[0]) | ((uint32_t)(ctx->p2g_buf[1]) << 8) |
           ((uint32_t)(ctx->p2g_buf[2]) << 16) | ((uint32_t)(ctx->p2g_buf[3]) << 24);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriRSPlatformJS_SetP2GWritePos(LibToriRS_NetContext* ctx, uint32_t pos)
{
    if( !ctx || !ctx->p2g_buf )
        return;
    const int payload_cap = ctx->p2g_cap - 8;
    if( payload_cap <= 0 )
        return;
    pos = pos % (uint32_t)payload_cap;
    ctx->p2g_buf[0] = (uint8_t)(pos);
    ctx->p2g_buf[1] = (uint8_t)(pos >> 8);
    ctx->p2g_buf[2] = (uint8_t)(pos >> 16);
    ctx->p2g_buf[3] = (uint8_t)(pos >> 24);
}

EMSCRIPTEN_KEEPALIVE
uint32_t
LibToriRSPlatformJS_GetG2PReadPos(LibToriRS_NetContext* ctx)
{
    if( !ctx || !ctx->g2p_buf || ctx->g2p_cap < 9 )
        return 0;
    return (uint32_t)(ctx->g2p_buf[4]) | ((uint32_t)(ctx->g2p_buf[5]) << 8) |
           ((uint32_t)(ctx->g2p_buf[6]) << 16) | ((uint32_t)(ctx->g2p_buf[7]) << 24);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriRSPlatformJS_SetG2PReadPos(LibToriRS_NetContext* ctx, uint32_t pos)
{
    if( !ctx || !ctx->g2p_buf )
        return;
    const int payload_cap = ctx->g2p_cap - 8;
    if( payload_cap <= 0 )
        return;
    pos = pos % (uint32_t)payload_cap;
    ctx->g2p_buf[4] = (uint8_t)(pos);
    ctx->g2p_buf[5] = (uint8_t)(pos >> 8);
    ctx->g2p_buf[6] = (uint8_t)(pos >> 16);
    ctx->g2p_buf[7] = (uint8_t)(pos >> 24);
}

EMSCRIPTEN_KEEPALIVE
uint32_t
LibToriRSPlatformJS_GetG2PWritePos(LibToriRS_NetContext* ctx)
{
    if( !ctx || !ctx->g2p_buf || ctx->g2p_cap < 9 )
        return 0;
    return (uint32_t)(ctx->g2p_buf[0]) | ((uint32_t)(ctx->g2p_buf[1]) << 8) |
           ((uint32_t)(ctx->g2p_buf[2]) << 16) | ((uint32_t)(ctx->g2p_buf[3]) << 24);
}

EMSCRIPTEN_KEEPALIVE
void
LibToriRSPlatformJS_SetState(LibToriRS_NetContext* ctx, int status)
{
    if( ctx )
        ctx->state = status;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriRSPlatformJS_GetP2GCapacity(LibToriRS_NetContext* ctx)
{
    return ctx ? ctx->p2g_cap : 0;
}

EMSCRIPTEN_KEEPALIVE
int
LibToriRSPlatformJS_GetG2PCapacity(LibToriRS_NetContext* ctx)
{
    return ctx ? ctx->g2p_cap : 0;
}

#endif /* __EMSCRIPTEN__ */
