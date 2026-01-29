#ifndef SCENEBUILDER_H
#define SCENEBUILDER_H

#include "configmap.h"
#include "datastruct/hmap.h"
#include "osrs/game.h"
#include "osrs/minimap.h"
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
    struct Minimap* minimap);

void
scenebuilder_free(struct SceneBuilder* scene_builder);

struct Scene*
scenebuilder_load_from_buildcachedat(
    struct SceneBuilder* scene_builder,
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z,
    struct BuildCacheDat* buildcachedat);

struct Scene*
scenebuilder_load_from_buildcache(
    struct SceneBuilder* scene_builder,
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z,
    struct BuildCache* buildcache);

struct SceneAnimation*
scenebuilder_new_animation(
    struct SceneBuilder* scene_builder,
    int sequence_id);

void
scenebuilder_push_dynamic_element(
    struct SceneBuilder* scene_builder,
    struct Scene* scene,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    struct SceneElement* element);

void
scenebuilder_reset_dynamic_elements(
    struct SceneBuilder* scene_builder,
    struct Scene* scene);

#endif