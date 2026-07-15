#ifndef TORIRS_NUKLEAR_H_
#define TORIRS_NUKLEAR_H_

/* Single entry for Torirs Nuklear: applies torirs_nk_config.h before nuklear.h.
 * Nuklear is header-guarded once per translation unit; this order must happen on
 * that first include. For the implementation TU, #define NK_IMPLEMENTATION before
 * including this file (see nuklear_impl.c). */
// clang-format off
#include "nuklear/torirs_nk_config.h"
#include "nuklear.h"
// clang-format on

#endif /* TORIRS_NUKLEAR_H_ */
