/*
 * The flatten strategy table (wev_flat.h): which one, and its state.
 *
 * @see wev_flat.h
 */
#include "render/wev_flat.h"

#include "log/torirs_log.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WevFlatOps const*
WevFlat_OpsByName(char const* name)
{
    if( !name || !name[0] )
        return &g_wev_flat_outline;
    if( strcmp(name, g_wev_flat_live.name) == 0 )
        return &g_wev_flat_live;
    if( strcmp(name, g_wev_flat_outline.name) == 0 )
        return &g_wev_flat_outline;
    TORIRS_ERR("TORIRS_WEV_FLAT=%s: expected \"live\" or \"outline\"\n", name);
    assert(!"unknown TORIRS_WEV_FLAT strategy");
    abort();
}

void
WevFlat_Init(
    struct WevFlat* flat,
    struct WevFlatOps const* ops)
{
    assert(flat);
    assert(ops);
    flat->ops = ops;
    flat->state = ops->create();
}

void
WevFlat_Free(
    struct WevFlat* flat,
    struct ToriDraw_Scene* scene)
{
    assert(flat);
    if( !flat->ops )
        return;
    flat->ops->destroy(flat->state, scene);
    flat->ops = NULL;
    flat->state = NULL;
}
