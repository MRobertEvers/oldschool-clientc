#ifndef TORIRS_PLATFORM_KIT_D3D8_INTERNAL_H
#define TORIRS_PLATFORM_KIT_D3D8_INTERNAL_H

#include "trspk_d3d8.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void*
trspk_d3d8_internal_device(TRSPK_D3D8Renderer* r);

void*
trspk_d3d8_internal_d3d(TRSPK_D3D8Renderer* r);

#ifdef __cplusplus
}
#endif

#endif
