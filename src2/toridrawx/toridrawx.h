#ifndef TORIDRAWX_H
#define TORIDRAWX_H

#include "gamecache/gamecache_l.h"
#include "toridraw/toridraw_gccontext.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>

struct GameCache_Animation;
struct GameCache_Model;
struct GameCache_Texture;
struct ToriDraw_Animation;
struct ToriDraw_Model;
struct ToriDraw_Texture;

struct ToriDrawX*
toridrawx_new(
    struct GameCacheL* gamecache_l,
    struct ToriDraw_Context* context);

void
toridrawx_free(struct ToriDrawX* tdx);

struct GameCacheL*
toridrawx_gamecache_l(struct ToriDrawX* tdx);

struct GameCache*
toridrawx_gamecache(struct ToriDrawX* tdx);

struct ToriDraw_Context*
toridrawx_context(struct ToriDrawX* tdx);

struct ToriDraw_Model*
toridrawx_model_new_from_gamecache(const struct GameCache_Model* src);

struct ToriDraw_Animation*
toridrawx_animation_new_from_gamecache(const struct GameCache_Animation* src);

struct ToriDraw_Texture*
toridrawx_texture_new_from_gamecache(const struct GameCache_Texture* src);

struct ToriDraw_ModelHandle
toridrawx_model(
    struct ToriDrawX* tdx,
    int model_id);

struct ToriDraw_Animation*
toridrawx_animation(
    struct ToriDrawX* tdx,
    int anim_id);

struct ToriDraw_Animation*
toridrawx_sequence_animation(
    struct ToriDrawX* tdx,
    int seq_id);

struct ToriDraw_Texture*
toridrawx_texture(
    struct ToriDrawX* tdx,
    int texture_id);

bool
toridrawx_model_ready(
    struct ToriDrawX* tdx,
    int model_id);

bool
toridrawx_submit_model_from_dat1(
    struct ToriDrawX* tdx,
    int model_id);

int
toridrawx_element_add_model(
    struct ToriDrawX* tdx,
    int model_id);

bool
toridrawx_element_set_model_id(
    struct ToriDrawX* tdx,
    int element_id,
    int model_id);

bool
toridrawx_element_set_sequence_id(
    struct ToriDrawX* tdx,
    int element_id,
    int seq_id);

#endif
