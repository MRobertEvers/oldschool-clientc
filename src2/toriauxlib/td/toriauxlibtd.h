#ifndef TORIAUXLIBTD_H
#define TORIAUXLIBTD_H

#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>

struct RSCacheDat1A_AnimBaseFrames;
struct ToriAuxLibCore_Animation;
struct ToriAuxLibCore_Model;
struct ToriAuxLibCore_Texture;
struct ToriDraw_Animation;
struct ToriDraw_Model;
struct ToriDraw_Texture;

struct ToriAuxLibTD*
ToriAuxLibTD_New(
    struct ToriAuxLibCore* core,
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene);

void
ToriAuxLibTD_Free(struct ToriAuxLibTD* td);

struct ToriAuxLibCore*
ToriAuxLibTD_Core(struct ToriAuxLibTD* td);

struct ToriAuxLibCache*
ToriAuxLibTD_C(struct ToriAuxLibTD* td);

struct ToriDraw_Scene*
ToriAuxLibTD_Scene(struct ToriAuxLibTD* td);

struct ToriDraw_Model*
ToriAuxLibTD_ModelNewFromCore(const struct ToriAuxLibCore_Model* src);

/** Copy core geometry into a new TD model without inserting into the scene cache. */
struct ToriDraw_Model*
ToriAuxLibTD_ModelNewInstanceFromCore(
    struct ToriAuxLibTD* td,
    int model_id);

/**
 * Move compatible arrays from an exclusively-owned core model into a new TD model.
 * Leaves src hollow (free shell with ToriAuxLibCore_ModelFree).
 */
struct ToriDraw_Model*
ToriAuxLibTD_ModelStealFromCore(struct ToriAuxLibCore_Model* src);

struct ToriDraw_Animation*
ToriAuxLibTD_AnimationNewFromCore(const struct ToriAuxLibCore_Animation* src);

struct ToriDraw_Animation*
ToriAuxLibTD_AnimationNewFromCacheDatAnimbaseframes(struct RSCacheDat1A_AnimBaseFrames* abf);

struct ToriDraw_Texture*
ToriAuxLibTD_TextureNewFromCore(const struct ToriAuxLibCore_Texture* src);

struct ToriDraw_ModelHandle
ToriAuxLibTD_Model(
    struct ToriAuxLibTD* td,
    int model_id);

struct ToriDraw_Animation*
ToriAuxLibTD_Animation(
    struct ToriAuxLibTD* td,
    int anim_id);

struct ToriDraw_Animation*
ToriAuxLibTD_SequenceAnimation(
    struct ToriAuxLibTD* td,
    int seq_id);

struct ToriDraw_Texture*
ToriAuxLibTD_Texture(
    struct ToriAuxLibTD* td,
    int texture_id);

bool
ToriAuxLibTD_ModelReady(
    struct ToriAuxLibTD* td,
    int model_id);

bool
ToriAuxLibTD_SubmitModelFromDat1(
    struct ToriAuxLibTD* td,
    int model_id);

int
ToriAuxLibTD_ElementAddModel(
    struct ToriAuxLibTD* td,
    int model_id);

bool
ToriAuxLibTD_ElementSetModelId(
    struct ToriAuxLibTD* td,
    int element_id,
    int model_id);

bool
ToriAuxLibTD_ElementSetSequenceId(
    struct ToriAuxLibTD* td,
    int element_id,
    int seq_id);

struct ToriDraw_SkeletalAnim*
ToriAuxLibTD_SkeletalAnimation(
    struct ToriAuxLibTD* td,
    int anim_maya_id);

#endif
