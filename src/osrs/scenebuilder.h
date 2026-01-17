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

struct Scene*
scenebuilder_load(struct SceneBuilder* scene_builder);

#endif