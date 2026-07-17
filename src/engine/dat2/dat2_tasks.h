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

#endif
