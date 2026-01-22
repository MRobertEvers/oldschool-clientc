#ifndef SCENEBUILDER_H
#define SCENEBUILDER_H

#include "configmap.h"
#include "datastruct/hmap.h"
#include "libg.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "painters.h"
#include "scene.h"

#define MAPXZ(mapx, mapz) ((mapx << 16) | mapz)

struct SceneBuilder;

struct SceneBuilder*
scenebuilder_new_painter(
    struct Painter* painter,
    int mapx_sw,
    int mapz_sw,
    int mapx_ne,
    int mapz_ne);

void
scenebuilder_free(struct SceneBuilder* scene_builder);

void
scenebuilder_cache_configmap_underlay(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_underlay_map);

void
scenebuilder_cache_configmap_overlay(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_overlay_map);

void
scenebuilder_cache_configmap_locs(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_locs_configmap);

void
scenebuilder_cache_configmap_sequences(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_sequences_configmap);

void
scenebuilder_cache_frame(
    struct SceneBuilder* scene_builder,
    // frame_file
    int frame_anim_id,
    struct CacheFrame* frame);

void
scenebuilder_cache_model(
    struct SceneBuilder* scene_builder,
    int model_id,
    struct CacheModel* model);

void
scenebuilder_cache_map_locs(
    struct SceneBuilder* scene_builder,
    int mapx,
    int mapz,
    struct CacheMapLocs* map_locs);

void
scenebuilder_cache_map_terrain(
    struct SceneBuilder* scene_builder,
    int mapx,
    int mapz,
    struct CacheMapTerrain* map_terrain);

void
scenebuilder_cache_flotype(
    struct SceneBuilder* scene_builder,
    int flotype_id,
    struct CacheConfigOverlay* flotype);

void
scenebuilder_cache_config_loc(
    struct SceneBuilder* scene_builder,
    int loc_id,
    struct CacheConfigLocation* config_loc);

void
scenebuilder_cache_animframe(
    struct SceneBuilder* scene_builder,
    int animframe_id,
    struct CacheAnimframe* animframe);

void
scenebuilder_cache_dat_sequence(
    struct SceneBuilder* scene_builder,
    int sequence_id,
    struct CacheDatSequence* sequence);

struct Scene*
scenebuilder_load(struct SceneBuilder* scene_builder);

#endif