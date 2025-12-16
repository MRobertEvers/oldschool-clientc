#ifndef GIO_ASSETS_H
#define GIO_ASSETS_H

#include "osrs/gio.h"

enum AssetKind
{
    ASSET_BAD = 0,
    ASSET_MODELS,
    ASSET_TEXTURES,
    ASSET_SPRITEPACKS,
    ASSET_MAP_SCENERY,
    ASSET_MAP_TERRAIN,
    ASSET_CONFIG_SCENERY,
    ASSET_CONFIG_UNDERLAY,
    ASSET_CONFIG_OVERLAY,
    ASSET_TABLE_COUNT
};

uint32_t gio_assets_model_load(struct GIOQueue* q, int model_id);
uint32_t gio_assets_texture_definitions_load(struct GIOQueue* q);
uint32_t gio_assets_spritepack_load(struct GIOQueue* q, int spritepack_id);
uint32_t gio_assets_map_scenery_load(struct GIOQueue* q, int chunk_x, int chunk_y);
uint32_t gio_assets_map_terrain_load(struct GIOQueue* q, int chunk_x, int chunk_z);
uint32_t gio_assets_config_scenery_load(struct GIOQueue* q);
uint32_t gio_assets_config_underlay_load(struct GIOQueue* q);
uint32_t gio_assets_config_overlay_load(struct GIOQueue* q);

#endif