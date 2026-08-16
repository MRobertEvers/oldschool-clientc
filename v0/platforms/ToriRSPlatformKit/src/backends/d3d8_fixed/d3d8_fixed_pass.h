#ifndef TORIRS_PLATFORM_KIT_D3D8_FIXED_PASS_H
#define TORIRS_PLATFORM_KIT_D3D8_FIXED_PASS_H

#include "d3d8_fixed_internal.h"

/**
 * Submit the accumulated 3D pass: one ib_ring DISCARD lock, merged DIPs.
 * Clears pass_state after submission. No-op if pass_state is empty.
 */
void
d3d8_fixed_submit_pass(D3D8FixedInternal* p, IDirect3DDevice8* dev);

/** Clear pass_state buffers without submitting (called at begin-3D reset). */
void
d3d8_fixed_reset_pass(D3D8FixedInternal* p);

#endif
