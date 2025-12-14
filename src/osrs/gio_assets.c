#include "gio_assets.h"

uint32_t
gio_assets_model_load(struct GIOQueue* q, int model_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_MODELS, model_id, 0);
}

uint32_t
gio_assets_texture_load(struct GIOQueue* q, int texture_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_TEXTURES, texture_id, 0);
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

uint32_t
gio_assets_config_underlay_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_CONFIG_UNDERLAY, 0, 0);
}

uint32_t
gio_assets_config_overlay_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_CONFIG_OVERLAY, 0, 0);
}