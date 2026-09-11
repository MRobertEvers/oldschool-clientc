#ifndef TORIDRAW_RENDER_INVARIANTS_U_C
#define TORIDRAW_RENDER_INVARIANTS_U_C

/*
 * The render path's assert-only predicates.
 *
 * A whole-array check that a fast path depends on but cannot afford to make:
 * the sort loops keep their scratch arrays all-zero by re-zeroing exactly the
 * window one model dirtied, and the check that the window logic never missed a
 * bucket is the O(depth_levels) sweep the windowing exists to avoid. So it is
 * an assert and nothing else -- gone in NDEBUG, and in a debug or test build it
 * fails at the model that broke the invariant instead of at the frame that
 * renders wrong.
 *
 * NOT toridraw_debug.u.c: that is the measurement facility, gated on
 * TORIDRAW_DEBUG_STATS and absent from an ordinary debug build. These have to
 * be present wherever assert() is, so they carry the plain NDEBUG gate. The
 * gate sits on the definition and not on the call site -- assert() already
 * deletes the call; without it here the function would be an unused static.
 *
 * Included by toridraw_render.u.c, above the sort paths that assert on these.
 */

#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>

#ifndef NDEBUG
/* The CSR sort does not clear sm_depth_offset, so it depends on the array
 * arriving all-zero: every exit that reached the prefix sum restores its own
 * window through sm_depth_offset_restore. */
static bool
sm_depth_offset_all_zero(const struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(scene->sm_depth_offset);

    for( int d = 0; d <= scene->depth_levels; d++ )
    {
        if( scene->sm_depth_offset[d] != 0 )
            return false;
    }
    return true;
}
#endif

#endif /* TORIDRAW_RENDER_INVARIANTS_U_C */
