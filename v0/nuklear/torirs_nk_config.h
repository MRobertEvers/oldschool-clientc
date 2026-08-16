/* Prefer #include "nuklear/torirs_nuklear.h" in app code so config is applied before
 * nuklear.h on the first parse. If you include "nuklear.h" directly, include this file
 * immediately before it (must match nuklear_impl.c). */
#ifndef TORIRS_NK_CONFIG_H
#define TORIRS_NK_CONFIG_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

/* Backends compiled with NK_*_IMPLEMENTATION in .cpp need memset before nk_memset exists. */
#include <string.h>
#ifndef NK_MEMSET
#define NK_MEMSET memset
#endif

#endif
