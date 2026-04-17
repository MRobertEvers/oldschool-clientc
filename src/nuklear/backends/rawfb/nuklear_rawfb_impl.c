/* Nuklear rawfb backend compiled as C (MinGW g++ cannot compile NK_RAWFB_IMPLEMENTATION in .cpp). */
#include "nuklear/torirs_nuklear.h"

#include <assert.h>
#include <math.h>

#ifndef NK_ASSERT
#define NK_ASSERT(expr) assert(expr)
#endif

#define NK_RAWFB_IMPLEMENTATION
#include "nuklear/backends/rawfb/nuklear_rawfb.h"

struct nk_context*
torirs_rawfb_get_nk_context(struct rawfb_context* rawfb)
{
    return rawfb ? &rawfb->ctx : NULL;
}
