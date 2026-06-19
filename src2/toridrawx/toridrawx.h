#ifndef TORIDRAWX_H
#define TORIDRAWX_H

#include "gamecache/gamecache_l.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>

struct GameCache_Animation;
struct GameCache_Model;
struct GameCache_Texture;
struct ToriDraw_Animation;
struct ToriDraw_Model;
struct ToriDraw_Texture;

struct ToriDrawX*
ToriDrawX_New(
    struct GameCacheL* gamecache_l,
    struct ToriDraw_Scene* scene);

void
ToriDrawX_Free(struct ToriDrawX* tdx);

struct GameCacheL*
ToriDrawX_GamecacheL(struct ToriDrawX* tdx);

struct GameCache*
ToriDrawX_Gamecache(struct ToriDrawX* tdx);

struct ToriDraw_Scene*
ToriDrawX_Scene(struct ToriDrawX* tdx);

struct ToriDraw_Model*
ToriDrawX_ModelNewFromGamecache(const struct GameCache_Model* src);

struct ToriDraw_Animation*
ToriDrawX_AnimationNewFromGamecache(const struct GameCache_Animation* src);

struct ToriDraw_Texture*
ToriDrawX_TextureNewFromGamecache(const struct GameCache_Texture* src);

struct ToriDraw_ModelHandle
ToriDrawX_Model(
    struct ToriDrawX* tdx,
    int model_id);

struct ToriDraw_Animation*
ToriDrawX_Animation(
    struct ToriDrawX* tdx,
    int anim_id);

struct ToriDraw_Animation*
ToriDrawX_SequenceAnimation(
    struct ToriDrawX* tdx,
    int seq_id);

struct ToriDraw_Texture*
ToriDrawX_Texture(
    struct ToriDrawX* tdx,
    int texture_id);

bool
ToriDrawX_ModelReady(
    struct ToriDrawX* tdx,
    int model_id);

bool
ToriDrawX_SubmitModelFromDat1(
    struct ToriDrawX* tdx,
    int model_id);

int
ToriDrawX_ElementAddModel(
    struct ToriDrawX* tdx,
    int model_id);

bool
ToriDrawX_ElementSetModelId(
    struct ToriDrawX* tdx,
    int element_id,
    int model_id);

bool
ToriDrawX_ElementSetSequenceId(
    struct ToriDrawX* tdx,
    int element_id,
    int seq_id);

#endif
