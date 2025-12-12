#include "gio_assets.h"

uint32_t
gio_assets_model_load(struct GIOQueue* q, int model_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_MODELS, model_id, 0);
}

uint32_t
gio_assets_map_scenery_load(struct GIOQueue* q, int chunk_x, int chunk_y)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_MAP_SCENERY, chunk_x, chunk_y);
}

uint32_t
gio_assets_config_scenery_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_CONFIG_SCENERY, 0, 0);
}