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

#endif
