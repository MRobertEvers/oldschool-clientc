#include "gio_assets.h"

uint32_t
gio_assets_model_load(
    struct GIOQueue* q,
    int model_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_MODELS, model_id, 0);
}

uint32_t
gio_assets_texture_definitions_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_TEXTURES, 0, 0);
}

uint32_t
gio_assets_spritepack_load(
    struct GIOQueue* q,
    int spritepack_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_SPRITEPACKS, spritepack_id, 0);
}

uint32_t
gio_assets_map_scenery_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_y)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_MAP_SCENERY, chunk_x, chunk_y);
}

uint32_t
gio_assets_map_terrain_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_z)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_MAP_TERRAIN, chunk_x, chunk_z);
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

uint32_t
gio_assets_config_sequences_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_CONFIG_SEQUENCES, 0, 0);
}

uint32_t
gio_assets_animation_load(
    struct GIOQueue* q,
    int animation_aka_archive_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_ANIMATION, animation_aka_archive_id, 0);
}

uint32_t
gio_assets_framemap_load(
    struct GIOQueue* q,
    int framemap_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_FRAMEMAP, framemap_id, 0);
}

uint32_t
gio_assets_dat_map_scenery_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_y)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_DAT_MAP_SCENERY, chunk_x, chunk_y);
}

uint32_t
gio_assets_dat_map_terrain_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_z)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_DAT_MAP_TERRAIN, chunk_x, chunk_z);
}

uint32_t
gio_assets_dat_models_load(
    struct GIOQueue* q,
    int model_id)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_DAT_MODELS, model_id, 0);
}

uint32_t
gio_assets_dat_config_texture_sprites_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_DAT_CONFIG_TEXTURE_SPRITES, 0, 0);
}

uint32_t
gio_assets_dat_config_flotype_file_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_DAT_CONFIG_FILE_FLOORTYPE, 0, 0);
}

uint32_t
gio_assets_dat_config_scenery_fileidx_load(struct GIOQueue* q)
{
    return gioq_submit(q, GIO_REQ_ASSET, ASSET_DAT_CONFIG_FILEIDX_SCENERY, 0, 0);
}