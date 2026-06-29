#ifndef LIBTORIRS_SCRIPTAPI_H
#define LIBTORIRS_SCRIPTAPI_H

#include "../games/game_handle.h"
#include "../libtorirs.h"
#include "libtorirs_scripting.h"

#include <stdbool.h>
#include <stdint.h>

void
LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

void
LibToriRS_ScriptAPI_Dat1_TexturesFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat1_TexturesLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat2_TexturesLoad(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat2_SubmitTextures(struct LibToriRS_Instance* instance);

const char*
LibToriRS_ScriptAPI_GetCacheMode(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_ModelFetch(
    struct LibToriRS_Instance* instance,
    int model_id,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat1_ModelLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

void
LibToriRS_ScriptAPI_Dat1_MapChunkTerrainFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue);

void
LibToriRS_ScriptAPI_Dat1_MapChunkSceneryFetch(
    struct LibToriRS_Instance* instance,
    int mapx,
    int mapz,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat1_MapChunkTerrainLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat1_MapChunkSceneryLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

void
LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(
    struct LibToriRS_Instance* instance,
    int model_id);

void
LibToriRS_ScriptAPI_Game_ModelViewer_Init(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Game_Runescape_Init(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Game_Runescape_BuildWorld(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Game_Runescape_BuildWorldCenterzone(
    struct LibToriRS_Instance* instance,
    int center_x,
    int center_z,
    int scene_size);

void
LibToriRS_ScriptAPI_Game_Runescape_BuildWorldChunkList(
    struct LibToriRS_Instance* instance,
    int* chunks_xz,
    int count);

struct GameHandle*
LibToriRS_ScriptAPI_Game_ModelViewer_GetGameHandle(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_CoreTask_Dat1LoadModel(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game,
    int model_id);

void
LibToriRS_ScriptAPI_CoreTask_RevConfigQueue(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game,
    const char* filename);

void
LibToriRS_ScriptAPI_CoreTask_RevConfigLoad(
    struct LibToriRS_Instance* instance,
    struct GameHandle* game);

bool
LibToriRS_ScriptAPI_RunTasks(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel(
    struct LibToriRS_Instance* instance,
    int model_id);

void
LibToriRS_ScriptAPI_Dat1_ModelCleanup(
    struct LibToriRS_Instance* instance,
    int model_id);

void
LibToriRS_ScriptAPI_Dat1_TexturesCleanup(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SubmitTextures(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_GameCache_ModelsClearAll(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_VersionListFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat1_VersionListLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

void
LibToriRS_ScriptAPI_Dat1_AnimationsFetch(
    struct LibToriRS_Instance* instance,
    int archive_id,
    struct LibToriRS_IOQueue* io_queue);

bool
LibToriRS_ScriptAPI_Dat1_AnimationsLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue);

void
LibToriRS_ScriptAPI_Dat1_SequencesInitFromConfigJagfile(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_FloortypesInitFromConfigJagfile(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SceneryConfigsInitFromConfigJagfile(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SubmitSequences(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SubmitFloortypes(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SubmitSceneryConfigs(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SubmitAnimations(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SequencesCleanup(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_FloortypesCleanup(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_SceneryConfigsCleanup(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_Dat1_AnimationsCleanup(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_GameCache_SequencesClearAll(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_GameCache_FloortypesClearAll(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_GameCache_SceneryConfigsClearAll(struct LibToriRS_Instance* instance);

void
LibToriRS_ScriptAPI_GameCache_AnimationsClearAll(struct LibToriRS_Instance* instance);

#endif
