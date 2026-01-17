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
    ASSET_CONFIG_SEQUENCES,
    ASSET_ANIMATION,
    ASSET_FRAMEMAP,
    ASSET_TABLE_COUNT
};

uint32_t
gio_assets_model_load(
    struct GIOQueue* q,
    int model_id);

uint32_t
gio_assets_texture_definitions_load(struct GIOQueue* q);

uint32_t
gio_assets_spritepack_load(
    struct GIOQueue* q,
    int spritepack_id);

uint32_t
gio_assets_map_scenery_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_y);

uint32_t
gio_assets_map_terrain_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_z);

uint32_t
gio_assets_config_scenery_load(struct GIOQueue* q);

uint32_t
gio_assets_config_underlay_load(struct GIOQueue* q);

uint32_t
gio_assets_config_overlay_load(struct GIOQueue* q);

uint32_t
gio_assets_config_sequences_load(struct GIOQueue* q);

/**
 * A single "frame" archive actually is a file-list of "frame" objects.
 *
 * An "animation" if you will.
 */
uint32_t
gio_assets_animation_load(
    struct GIOQueue* q,
    int animation_aka_archive_id);

uint32_t
gio_assets_framemap_load(
    struct GIOQueue* q,
    int framemap_id);

#endif