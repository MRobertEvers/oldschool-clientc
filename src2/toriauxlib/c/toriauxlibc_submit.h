#ifndef TORIAUXLIBC_SUBMIT_H
#define TORIAUXLIBC_SUBMIT_H

#include "toriauxlib/core/toriauxlibcore_types.h"

struct ToriAuxLibC;

void
ToriAuxLibC_SubmitMapTerrainFromDat1(
    struct ToriAuxLibC* c,
    int map_id);

void
ToriAuxLibC_SubmitMapSceneryFromDat1(
    struct ToriAuxLibC* c,
    int map_id);

void
ToriAuxLibC_SubmitAnimationFromDat1(
    struct ToriAuxLibC* c,
    int anim_id);

void
ToriAuxLibC_SubmitModelFromDat1(
    struct ToriAuxLibC* c,
    int model_id);

void
ToriAuxLibC_SubmitTexture(
    struct ToriAuxLibC* c,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture);

void
ToriAuxLibC_SubmitAllSequencesFromDat1(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitAllFlotypesFromDat1(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitAllLocationsFromDat1(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitSpriteFromDat1(
    struct ToriAuxLibC* c,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite);

void
ToriAuxLibC_SubmitFontFromDat1(
    struct ToriAuxLibC* c,
    int font_id,
    struct ToriAuxLibCore_Font* font);

void
ToriAuxLibC_SubmitAllComponentsFromDat1(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitMapTerrainFromDat2(
    struct ToriAuxLibC* c,
    int map_id);

void
ToriAuxLibC_SubmitMapSceneryFromDat2(
    struct ToriAuxLibC* c,
    int map_id);

void
ToriAuxLibC_SubmitModelFromDat2(
    struct ToriAuxLibC* c,
    int model_id);

void
ToriAuxLibC_SubmitAllSequencesFromDat2(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitAllFlotypesFromDat2(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitAllUnderlaysFromDat2(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitAllLocationsFromDat2(struct ToriAuxLibC* c);

void
ToriAuxLibC_SubmitAnimationFromDat2(
    struct ToriAuxLibC* c,
    int archive_id);

void
ToriAuxLibC_SubmitSkeletalFromDat2(
    struct ToriAuxLibC* c,
    int anim_maya_id);

#endif
