#include "engine/torirs_spotanimtype_from_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriRS_Spotanimtype*
ToriRS_SpotanimtypeFromRSCacheDat1(
    int spotanim_id,
    const struct RSCache_Dat1ConfigSpotanim* src)
{
    struct ToriRS_Spotanimtype* dst;

    assert(src);
    dst = calloc(1, sizeof(*dst));
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
    /* dat1 spotanim has no retexture opcodes. */

    return dst;
}

struct ToriRS_Spotanimtype*
ToriRS_SpotanimtypeFromRSCacheDat2(
    int spotanim_id,
    const struct RSCache_Dat2ConfigSpotanim* src)
{
    struct ToriRS_Spotanimtype* dst;

    assert(src);
    dst = calloc(1, sizeof(*dst));
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

    return dst;
}
