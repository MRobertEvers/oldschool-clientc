#ifndef TORIRS_PLATFORM_KIT_TRSPK_FACEBUFFER_H
#define TORIRS_PLATFORM_KIT_TRSPK_FACEBUFFER_H

#include <stdint.h>

/** Max faces for sorted-index scratch used by WebGL1 / Metal draw-model paths. */
#define TRSPK_FACEBUFFER_MAX_FACES 2500u
#define TRSPK_FACEBUFFER_INDEX_CAPACITY (TRSPK_FACEBUFFER_MAX_FACES * 3u)

typedef struct TRSPK_FaceBuffer16
{
    uint16_t indices[TRSPK_FACEBUFFER_INDEX_CAPACITY];
} TRSPK_FaceBuffer16;

typedef struct TRSPK_FaceBuffer32
{
    uint32_t indices[TRSPK_FACEBUFFER_INDEX_CAPACITY];
} TRSPK_FaceBuffer32;

#endif
