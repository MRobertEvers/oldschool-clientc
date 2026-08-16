#include "engine/torirs_idk_from_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriRS_Idk*
ToriRS_IdkFromRSCacheDat1(
    int idk_id,
    const struct RSCache_Dat1ConfigIdk* src)
{
    struct ToriRS_Idk* idk;

    assert(src);

    idk = calloc(1, sizeof(*idk));
    assert(idk);

    idk->id = idk_id;
    idk->body_part_id = src->type;
    idk->not_selectable = src->disable;
    memcpy(idk->recolors_from, src->recol_s, sizeof(idk->recolors_from));
    memcpy(idk->recolors_to, src->recol_d, sizeof(idk->recolors_to));
    memcpy(idk->heads, src->heads, sizeof(idk->heads));

    if( src->models_count > 0 )
    {
        idk->model_ids_count = src->models_count;
        idk->model_ids = malloc((size_t)src->models_count * sizeof(int));
        assert(idk->model_ids);
        memcpy(idk->model_ids, src->models, (size_t)src->models_count * sizeof(int));
    }

    return idk;
}

struct ToriRS_Idk*
ToriRS_IdkFromRSCacheDat2(
    int idk_id,
    const struct RSCache_Dat2ConfigIdk* src)
{
    struct ToriRS_Idk* idk;
    int recolor_limit;

    assert(src);

    idk = calloc(1, sizeof(*idk));
    assert(idk);

    idk->id = idk_id;
    idk->body_part_id = src->body_part_id;
    idk->not_selectable = src->is_not_selectable;
    memcpy(idk->heads, src->if_model_ids, sizeof(idk->heads));

    recolor_limit = src->recolor_count;
    if( recolor_limit > 10 )
        recolor_limit = 10;
    for( int i = 0; i < recolor_limit; i++ )
    {
        idk->recolors_from[i] = src->recolors_from[i];
        idk->recolors_to[i] = src->recolors_to[i];
    }

    if( src->model_ids_count > 0 )
    {
        idk->model_ids_count = src->model_ids_count;
        idk->model_ids = malloc((size_t)src->model_ids_count * sizeof(int));
        assert(idk->model_ids);
        memcpy(idk->model_ids, src->model_ids, (size_t)src->model_ids_count * sizeof(int));
    }

    return idk;
}
