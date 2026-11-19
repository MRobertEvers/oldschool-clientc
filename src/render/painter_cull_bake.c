#include "render/painter_cull_bake.h"

#include <assert.h>

bool
PainterCullBake_NeedsBake(
    struct PainterCullBake const* bake,
    int vw,
    int vh)
{
    assert(bake);
    if( bake->w > 0 && bake->h > 0 )
    {
        int dw = vw - bake->w;
        int dh = vh - bake->h;
        if( dw < 0 )
            dw = -dw;
        if( dh < 0 )
            dh = -dh;
        if( dw < 8 && dh < 8 )
            return false;
    }
    return true;
}

void
PainterCullBake_Note(
    struct PainterCullBake* bake,
    int vw,
    int vh)
{
    assert(bake);
    bake->w = vw;
    bake->h = vh;
}
