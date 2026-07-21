#ifndef DAT2_TASKS_H
#define DAT2_TASKS_H

#include "engine/cache_provider.h"

struct ToriRS_Task*
CreateTask_Dat2ModelLoad(
    struct CacheProvider* provider,
    int model_id);

struct ToriRS_Task*
CreateTask_Dat2ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id);

struct ToriRS_Task*
CreateTask_Dat2ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id);

struct ToriRS_Task*
CreateTask_Dat2ObjLoad(
    struct CacheProvider* provider,
    int obj_id);

struct ToriRS_Task*
CreateTask_Dat2NpcLoad(
    struct CacheProvider* provider,
    int npc_id);

struct ToriRS_Task*
CreateTask_Dat2IdkLoad(
    struct CacheProvider* provider,
    int idk_id);

struct ToriRS_Task*
CreateTask_Dat2MapTerrainLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z);

struct ToriRS_Task*
CreateTask_Dat2MapSceneryLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z);

struct ToriRS_Task*
CreateTask_Dat2LocLoad(
    struct CacheProvider* provider,
    int loc_id);

struct ToriRS_Task*
CreateTask_Dat2FlotypeLoad(
    struct CacheProvider* provider,
    int flo_id);

struct ToriRS_Task*
CreateTask_Dat2UnderlayLoad(
    struct CacheProvider* provider,
    int underlay_id);

struct ToriRS_Task*
CreateTask_Dat2TextureLoad(
    struct CacheProvider* provider,
    int texture_id);

struct ToriRS_Task*
CreateTask_Dat2SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id);

struct ToriRS_Task*
CreateTask_Dat2SpriteLoadByName(
    struct CacheProvider* provider,
    char const* archive_name);

struct ToriRS_Task*
CreateTask_Dat2FontLoad(
    struct CacheProvider* provider,
    int font_id);

struct ToriRS_Task*
CreateTask_Dat2EnumLoad(
    struct CacheProvider* provider,
    int enum_id);

struct ToriRS_Task*
CreateTask_Dat2StructLoad(
    struct CacheProvider* provider,
    int struct_id);

struct ToriRS_Task*
CreateTask_Dat2ParamLoad(
    struct CacheProvider* provider,
    int param_id);

struct ToriRS_Task*
CreateTask_Dat2ComponentLoad(
    struct CacheProvider* provider,
    int packed_component_id);

#endif
