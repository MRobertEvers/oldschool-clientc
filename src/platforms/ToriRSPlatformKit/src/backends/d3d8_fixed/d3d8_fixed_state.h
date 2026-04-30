#ifndef TORIRS_PLATFORM_KIT_D3D8_FIXED_STATE_H
#define TORIRS_PLATFORM_KIT_D3D8_FIXED_STATE_H

#include "d3d8_fixed_internal.h"

void
d3d8_fixed_ensure_pass(D3D8FixedInternal* priv, IDirect3DDevice8* dev, PassKind want);

bool
d3d8_fixed_should_log_cmd(D3D8FixedInternal* p, int kind);

void
d3d8_fixed_mark_cmd_logged(D3D8FixedInternal* p, int kind);

void
d3d8_fixed_destroy_internal(D3D8FixedInternal* p);

bool
d3d8_fixed_reset_device(TRSPK_D3D8FixedRenderer* ren, D3D8FixedInternal* p, HWND hwnd);

bool
d3d8_fixed_create_device(TRSPK_D3D8FixedRenderer* ren, D3D8FixedInternal* p, HWND hwnd);

#endif
