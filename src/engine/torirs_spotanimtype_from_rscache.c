#include "mem/torirs_mem.h"
#include "engine/torirs_spotanimtype_from_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(
    TORIRS_SPOTANIM_COLOUR_SLOTS == RSCACHE_SPOTANIM_COLOUR_SLOTS,
    "client spotanim colour slots must match the decoder's");

struct ToriRS_Spotanimtype*
ToriRS_SpotanimtypeFromRSCacheDat1(
    int spotanim_id,
    const struct RSCache_Dat1ConfigSpotanim* src)
{
    struct ToriRS_Spotanimtype* dst;

    assert(src);
    dst = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*dst));
    assert(dst);

    dst->id = spotanim_id;
    dst->model = src->model;
    dst->seq = src->anim;
    dst->resizeh = src->resizeh;
    dst->resizev = src->resizev;
    dst->angle = src->angle;
    dst->ambient = src->ambient;
    dst->contrast = src->contrast;
    memcpy(dst->recol_s, src->recol_s, sizeof(dst->recol_s));
    memcpy(dst->recol_d, src->recol_d, sizeof(dst->recol_d));
    /* The 2004 SpotType has six slots and recolours all six only when the
     * first source is set; dat1 has no retexture opcodes. */
    dst->recol_count = src->recol_s[0] != 0 ? 6 : 0;
    dst->retex_count = 0;

    return dst;
}

struct ToriRS_Spotanimtype*
ToriRS_SpotanimtypeFromRSCacheDat2(
    int spotanim_id,
    const struct RSCache_Dat2ConfigSpotanim* src)
{
    struct ToriRS_Spotanimtype* dst;

    assert(src);
    dst = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*dst));
    assert(dst);

    dst->id = spotanim_id;
    dst->model = src->model;
    dst->seq = src->anim;
    dst->resizeh = src->resizeh;
    dst->resizev = src->resizev;
    dst->angle = src->angle;
    dst->ambient = src->ambient;
    dst->contrast = src->contrast;
    memcpy(dst->recol_s, src->recol_s, sizeof(dst->recol_s));
    memcpy(dst->recol_d, src->recol_d, sizeof(dst->recol_d));
    memcpy(dst->retex_s, src->retex_s, sizeof(dst->retex_s));
    memcpy(dst->retex_d, src->retex_d, sizeof(dst->retex_d));
    assert(src->recol_count >= 0);
    assert(src->recol_count <= TORIRS_SPOTANIM_COLOUR_SLOTS);
    assert(src->retex_count >= 0);
    assert(src->retex_count <= TORIRS_SPOTANIM_COLOUR_SLOTS);
    dst->recol_count = src->recol_count;
    dst->retex_count = src->retex_count;

    return dst;
}
