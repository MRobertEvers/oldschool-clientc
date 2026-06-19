#ifndef TORIAUXLIBCORE_H
#define TORIAUXLIBCORE_H

#include "toriauxlib/core/toriauxlibcore_types.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriAuxLibCore*
ToriAuxLibCore_New(void);

void
ToriAuxLibCore_Free(struct ToriAuxLibCore* gamecache);

void
ToriAuxLibCore_ModelAdd(
    struct ToriAuxLibCore* gamecache,
    int model_id,
    struct ToriAuxLibCore_Model* model);

struct ToriAuxLibCore_Model*
ToriAuxLibCore_ModelGet(
    struct ToriAuxLibCore* gamecache,
    int model_id);

bool
ToriAuxLibCore_ModelHas(
    struct ToriAuxLibCore* gamecache,
    int model_id);

struct ToriAuxLibCore_Model*
ToriAuxLibCore_ModelRemove(
    struct ToriAuxLibCore* gamecache,
    int model_id);

void
ToriAuxLibCore_ModelsClearAll(struct ToriAuxLibCore* gamecache);

void
ToriAuxLibCore_TextureAdd(
    struct ToriAuxLibCore* gamecache,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture);

struct ToriAuxLibCore_Texture*
ToriAuxLibCore_TextureGet(
    struct ToriAuxLibCore* gamecache,
    int texture_id);

bool
ToriAuxLibCore_TextureHas(
    struct ToriAuxLibCore* gamecache,
    int texture_id);

void
ToriAuxLibCore_TexturesClearAll(struct ToriAuxLibCore* gamecache);

void
ToriAuxLibCore_MapTerrainAdd(
    struct ToriAuxLibCore* gamecache,
    int map_id,
    struct ToriAuxLibCore_MapTerrain* terrain);

struct ToriAuxLibCore_MapTerrain*
ToriAuxLibCore_MapTerrainGet(
    struct ToriAuxLibCore* gamecache,
    int map_id);

bool
ToriAuxLibCore_MapTerrainHas(
    struct ToriAuxLibCore* gamecache,
    int map_id);

void
ToriAuxLibCore_MapSceneryAdd(
    struct ToriAuxLibCore* gamecache,
    int map_id,
    struct ToriAuxLibCore_MapLocs* locs);

struct ToriAuxLibCore_MapLocs*
ToriAuxLibCore_MapSceneryGet(
    struct ToriAuxLibCore* gamecache,
    int map_id);

bool
ToriAuxLibCore_MapSceneryHas(
    struct ToriAuxLibCore* gamecache,
    int map_id);

void
ToriAuxLibCore_FlotypeAdd(
    struct ToriAuxLibCore* gamecache,
    int flo_id,
    struct ToriAuxLibCore_Flotype* flotype);

struct ToriAuxLibCore_Flotype*
ToriAuxLibCore_FlotypeGet(
    struct ToriAuxLibCore* gamecache,
    int flo_id);

bool
ToriAuxLibCore_FlotypeHas(
    struct ToriAuxLibCore* gamecache,
    int flo_id);

void
ToriAuxLibCore_LocationAdd(
    struct ToriAuxLibCore* gamecache,
    int loc_id,
    struct ToriAuxLibCore_Location* loc);

struct ToriAuxLibCore_Location*
ToriAuxLibCore_LocationGet(
    struct ToriAuxLibCore* gamecache,
    int loc_id);

bool
ToriAuxLibCore_LocationHas(
    struct ToriAuxLibCore* gamecache,
    int loc_id);

void
ToriAuxLibCore_SequenceAdd(
    struct ToriAuxLibCore* gamecache,
    int seq_id,
    struct ToriAuxLibCore_Sequence* seq);

struct ToriAuxLibCore_Sequence*
ToriAuxLibCore_SequenceGet(
    struct ToriAuxLibCore* gamecache,
    int seq_id);

bool
ToriAuxLibCore_SequenceHas(
    struct ToriAuxLibCore* gamecache,
    int seq_id);

struct ToriAuxLibCore_Animation*
ToriAuxLibCore_SequencePrimaryAnimation(
    struct ToriAuxLibCore* gamecache,
    int seq_id);

struct ToriAuxLibCore_Animation*
ToriAuxLibCore_SequenceResolvedAnimation(
    struct ToriAuxLibCore* gamecache,
    int seq_id);

void
ToriAuxLibCore_AnimationAdd(
    struct ToriAuxLibCore* gamecache,
    int anim_id,
    struct ToriAuxLibCore_Animation* animation);

struct ToriAuxLibCore_Animation*
ToriAuxLibCore_AnimationGet(
    struct ToriAuxLibCore* gamecache,
    int anim_id);

bool
ToriAuxLibCore_AnimationHas(
    struct ToriAuxLibCore* gamecache,
    int anim_id);

bool
ToriAuxLibCore_SequenceResolveFrame(
    struct ToriAuxLibCore* gamecache,
    int seq_id,
    int frame_index,
    const struct ToriAuxLibCore_AnimFrame** out_frame,
    const struct ToriAuxLibCore_AnimBase** out_base,
    int* out_delay);

#endif
