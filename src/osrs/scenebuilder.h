#ifndef SCENEBUILDER_H
#define SCENEBUILDER_H

#include "configmap.h"
#include "datastruct/hmap.h"
#include "libg.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/maps.h"
#include "osrs/tables/model.h"
#include "painters.h"
#include "scene.h"

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
    struct ConfigMap* config_underlay_map);

void
scenebuilder_cache_configmap_overlay(
    struct SceneBuilder* scene_builder,
    struct ConfigMap* config_overlay_map);

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
scenebuilder_cache_config_loc(
    struct SceneBuilder* scene_builder,
    int config_loc_id,
    struct CacheConfigLocation* config_loc);

struct Scene*
scenebuilder_load(struct SceneBuilder* scene_builder);

#endif