#pragma once

#include <stdio.h>
#include <stdint.h>

#ifndef DEOB_TEXTURE_DEBUG
#define DEOB_TEXTURE_DEBUG 0 /* bench_sdl2 sets DEOB_TEXTURE_DEBUG=1 via CMake */
#endif

#if DEOB_TEXTURE_DEBUG
extern int g_deob_dbg_remaining;
extern int g_deob_dbg_frame;
#define DEOB_DBG(...)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if( g_deob_dbg_remaining > 0 )                                                                                 \
        {                                                                                                              \
            fprintf(stderr, "[DEOBDBG f=%d r=%d] ", g_deob_dbg_frame, g_deob_dbg_remaining);                           \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fputc('\n', stderr);                                                                                       \
            g_deob_dbg_remaining--;                                                                                    \
        }                                                                                                              \
    } while( 0 )
#define DEOB_DBG_ALWAYS(...)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        fprintf(stderr, "[DEOBDBG!] ");                                                                                \
        fprintf(stderr, __VA_ARGS__);                                                                                  \
        fputc('\n', stderr);                                                                                           \
    } while( 0 )
#else
#define DEOB_DBG(...) ((void)0)
#define DEOB_DBG_ALWAYS(...) ((void)0)
#endif

#define DEOB_CNT_INC(x)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        ++(x);                                                                                                         \
    } while( 0 )
