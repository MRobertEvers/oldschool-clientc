#ifndef TORIDRAW_SCENE_H
#define TORIDRAW_SCENE_H

#include "toridraw_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool
ToriDraw_SceneGraphInit(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneGraphShutdown(struct ToriDraw_Scene* scene);

struct ToriDraw_Scene*
ToriDraw_SceneNew(uint32_t flags);

void
ToriDraw_SceneFree(struct ToriDraw_Scene* scene);

size_t
ToriDraw_SceneSize(uint32_t flags);

void
ToriDraw_ScenePrintSize(uint32_t flags);

struct ToriDraw_TextureState*
ToriDraw_SceneTexState(struct ToriDraw_Scene* scene);

/* Asset registry: models */

void
ToriDraw_SceneModelAdd(
    struct ToriDraw_Scene* scene,
    int model_id,
    struct ToriDraw_ModelHandle model);

struct ToriDraw_ModelHandle
ToriDraw_SceneModelGet(
    struct ToriDraw_Scene* scene,
    int model_id);

bool
ToriDraw_SceneModelHas(
    struct ToriDraw_Scene* scene,
    int model_id);

struct ToriDraw_ModelHandle
ToriDraw_SceneModelRemove(
    struct ToriDraw_Scene* scene,
    int model_id);

void
ToriDraw_SceneModelsClearAll(struct ToriDraw_Scene* scene);

/* Asset registry: animations */

void
ToriDraw_SceneAnimationAdd(
    struct ToriDraw_Scene* scene,
    int anim_id,
    struct ToriDraw_Animation* animation);

struct ToriDraw_Animation*
ToriDraw_SceneAnimationGet(
    struct ToriDraw_Scene* scene,
    int anim_id);

bool
ToriDraw_SceneAnimationHas(
    struct ToriDraw_Scene* scene,
    int anim_id);

/* Asset registry: textures */

void
ToriDraw_SceneSetTexture(
    struct ToriDraw_Scene* scene,
    int id,
    struct ToriDraw_Texture* texture);

/* Asset registry: sprites */

void
ToriDraw_SceneSpriteAdd(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_Sprite** sprites,
    int count);

struct ToriDraw_Sprite**
ToriDraw_SceneSpriteGet(
    struct ToriDraw_Scene* scene,
    int element_id,
    int* out_count);

bool
ToriDraw_SceneSpriteHas(
    struct ToriDraw_Scene* scene,
    int element_id);

/* Asset registry: fonts */

void
ToriDraw_SceneFontAdd(
    struct ToriDraw_Scene* scene,
    int font_id,
    struct ToriDraw_Font* font);

struct ToriDraw_Font*
ToriDraw_SceneFontGet(
    struct ToriDraw_Scene* scene,
    int font_id);

bool
ToriDraw_SceneFontHas(
    struct ToriDraw_Scene* scene,
    int font_id);

/* Scene elements */

void
ToriDraw_SceneClear(struct ToriDraw_Scene* scene);

int
ToriDraw_SceneElementAdd(struct ToriDraw_Scene* scene);

int
ToriDraw_SceneElementRemove(
    struct ToriDraw_Scene* scene,
    int element_id);

struct ToriDraw_SceneElement*
ToriDraw_SceneElementGet(
    struct ToriDraw_Scene* scene,
    int element_id);

bool
ToriDraw_SceneElementIsLive(
    struct ToriDraw_Scene* scene,
    int element_id);

int
ToriDraw_SceneElementSlotCount(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneElementSetModel(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model);

void
ToriDraw_SceneElementSetAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary);

void
ToriDraw_SceneElementSetAnimationSeq(
    struct ToriDraw_Scene* scene,
    int element_id,
    int seq_id);

void
ToriDraw_SceneElementApplyAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    bool primary,
    int frame);

void
ToriDraw_SceneElementSetPosition(
    struct ToriDraw_Scene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int yaw);

void
ToriDraw_SceneElementSetPositionPitchYaw(
    struct ToriDraw_Scene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int pitch,
    int yaw);

/* Batch building */

void
ToriDraw_SceneBatchBegin(struct ToriDraw_Scene* scene);

struct ToriDraw_SceneBatchElementHandle
ToriDraw_SceneBatchAddElement(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneBatchEnd(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneBatchClear(
    struct ToriDraw_Scene* scene,
    int batch_id);

void
ToriDraw_SceneBatchElementAddPose(
    struct ToriDraw_Scene* scene,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle baked);

/* Events and frame lifecycle */

void
ToriDraw_SceneSpritesReemitLoads(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneFontsReemitLoads(struct ToriDraw_Scene* scene);

struct ToriDraw_EventQueue*
ToriDraw_SceneEvents(struct ToriDraw_Scene* scene);

void
ToriDraw_SceneFrameEnd(struct ToriDraw_Scene* scene);

#endif
