#ifndef GIO_ASSETS_H
#define GIO_ASSETS_H

#include "osrs/gio.h"

/**
 * Note: This should only be used as a network abstraction.
 * Only assets that are requested over a network should be here,
 * subfiles, and parts of archives should be loaded by the game
 * logic.
 */
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
    // Used in dat - jagfile
    ASSET_DAT_MAP_TERRAIN,
    ASSET_DAT_MAP_SCENERY,
    ASSET_DAT_MODELS,
    ASSET_DAT_ANIMBASEFRAMES,
    ASSET_DAT_SOUND,
    ASSET_DAT_CONFIG_TITLE_AND_FONTS,
    ASSET_DAT_CONFIG_CONFIGS,
    ASSET_DAT_CONFIG_INTERFACES,
    ASSET_DAT_CONFIG_MEDIA_2D_GRAPHICS,
    ASSET_DAT_CONFIG_VERSION_LIST,
    ASSET_DAT_CONFIG_TEXTURES,
    ASSET_DAT_CONFIG_CHAT_SYSTEM,
    ASSET_DAT_CONFIG_SOUND_EFFECTS,
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

uint32_t
gio_assets_dat_map_scenery_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_y);

uint32_t
gio_assets_dat_map_terrain_load(
    struct GIOQueue* q,
    int chunk_x,
    int chunk_z);

uint32_t
gio_assets_dat_models_load(
    struct GIOQueue* q,
    int model_id);

uint32_t
gio_assets_dat_animbaseframes_load(
    struct GIOQueue* q,
    int animbaseframes_id);

uint32_t
gio_assets_dat_sound_load(
    struct GIOQueue* q,
    int sound_id);

uint32_t
gio_assets_dat_config_texture_sprites_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_media_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_title_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_configs_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_interfaces_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_media_2d_graphics_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_version_list_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_textures_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_chat_system_load(struct GIOQueue* q);

uint32_t
gio_assets_dat_config_sound_effects_load(struct GIOQueue* q);

#endif