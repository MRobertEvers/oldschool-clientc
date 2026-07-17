#ifndef DAT1_TASKS_H
#define DAT1_TASKS_H

#include "engine/cache_provider.h"

struct ToriRS_Task*
CreateTask_Dat1ModelLoad(
    struct CacheProvider* provider,
    int model_id);

struct ToriRS_Task*
CreateTask_Dat1ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id);

struct ToriRS_Task*
CreateTask_Dat1ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id);

struct ToriRS_Task*
CreateTask_Dat1ObjLoad(
    struct CacheProvider* provider,
    int obj_id);

struct ToriRS_Task*
CreateTask_Dat1NpcLoad(
    struct CacheProvider* provider,
    int npc_id);

struct ToriRS_Task*
CreateTask_Dat1IdkLoad(
    struct CacheProvider* provider,
    int idk_id);

struct ToriRS_Task*
CreateTask_Dat1SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id);

struct ToriRS_Task*
CreateTask_Dat1FontLoad(
    struct CacheProvider* provider,
    int font_id);

struct ToriRS_Task*
CreateTask_Dat1ComponentLoad(
    struct CacheProvider* provider,
    int packed_component_id);

#endif
