#ifndef TORIAUXLIBCACHE_SUBMIT_H
#define TORIAUXLIBCACHE_SUBMIT_H

#include "toriauxlib/core/toriauxlibcore_types.h"

struct ToriAuxLibCache;
struct Dat2BuildCache_InterfaceArchive;

void
ToriAuxLibCache_SubmitMapTerrainFromDat1(
    struct ToriAuxLibCache* c,
    int map_id);

void
ToriAuxLibCache_SubmitMapSceneryFromDat1(
    struct ToriAuxLibCache* c,
    int map_id);

void
ToriAuxLibCache_SubmitAnimationFromDat1(
    struct ToriAuxLibCache* c,
    int anim_id);

void
ToriAuxLibCache_SubmitModelFromDat1(
    struct ToriAuxLibCache* c,
    int model_id);

void
ToriAuxLibCache_SubmitIdkModelFromDat1(
    struct ToriAuxLibCache* c,
    int idk_id);

void
ToriAuxLibCache_SubmitObjModelFromDat1(
    struct ToriAuxLibCache* c,
    int obj_id);

void
ToriAuxLibCache_SubmitIdkModelFromDat2(
    struct ToriAuxLibCache* c,
    int idk_id);

void
ToriAuxLibCache_SubmitObjModelFromDat2(
    struct ToriAuxLibCache* c,
    int obj_id);

bool
ToriAuxLibCache_EnsureObjtype(
    struct ToriAuxLibCache* c,
    int obj_id);

bool
ToriAuxLibCache_EnsureNpctype(
    struct ToriAuxLibCache* c,
    int npc_id);

void
ToriAuxLibCache_SubmitTexture(
    struct ToriAuxLibCache* c,
    int texture_id,
    struct ToriAuxLibCore_Texture* texture);

void
ToriAuxLibCache_SubmitAllSequencesFromDat1(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitAllFlotypesFromDat1(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitAllLocationsFromDat1(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitSprite(
    struct ToriAuxLibCache* c,
    int sprite_id,
    struct ToriAuxLibCore_Sprite* sprite);

void
ToriAuxLibCache_SubmitFont(
    struct ToriAuxLibCache* c,
    int font_id,
    struct ToriAuxLibCore_Font* font);

void
ToriAuxLibCache_SubmitClientScript(
    struct ToriAuxLibCache* c,
    int script_id,
    struct ToriAuxLibCore_ClientScript* script);

struct ToriAuxLibCore_ClientScript*
ToriAuxLibCache_ClientScriptResolve(
    struct ToriAuxLibCache* c,
    int script_id);

void
ToriAuxLibCache_SubmitComponent(
    struct ToriAuxLibCache* c,
    int component_id,
    struct ToriAuxLibCore_Component* component);

void
ToriAuxLibCache_SubmitAllComponentsFromDat1(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitComponentsFromDat2(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache_InterfaceArchive* archive);

void
ToriAuxLibCache_SubmitMapTerrainFromDat2(
    struct ToriAuxLibCache* c,
    int map_id);

void
ToriAuxLibCache_SubmitMapSceneryFromDat2(
    struct ToriAuxLibCache* c,
    int map_id);

void
ToriAuxLibCache_SubmitModelFromDat2(
    struct ToriAuxLibCache* c,
    int model_id);

void
ToriAuxLibCache_SubmitAllSequencesFromDat2(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitSequenceFromDat2(
    struct ToriAuxLibCache* c,
    int seq_id);

void
ToriAuxLibCache_SubmitAllFlotypesFromDat2(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitAllUnderlaysFromDat2(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitAllLocationsFromDat2(struct ToriAuxLibCache* c);

void
ToriAuxLibCache_SubmitAnimationFromDat2(
    struct ToriAuxLibCache* c,
    int archive_id);

void
ToriAuxLibCache_SubmitSkeletalFromDat2(
    struct ToriAuxLibCache* c,
    int anim_maya_id);

#endif
