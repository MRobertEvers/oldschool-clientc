#ifndef TASK_CS2_SCRIPT_EXEC_H
#define TASK_CS2_SCRIPT_EXEC_H

#include "asyncio.h"
#include "engine/cache_provider.h"

#include <hmap.h>

struct CS2VM2_Script;

struct ScriptEntry
{
    int script_id;
    struct CS2VM2_Script* script;
};

struct HMap*
ScriptCache_New(void);

void
ScriptCache_Free(struct HMap* scripts);

struct ScriptEntry*
ScriptCache_Get(
    struct HMap* scripts,
    int script_id);

struct ScriptEntry*
ScriptCache_StoreCopy(
    struct HMap* scripts,
    int script_id,
    const struct CS2VM2_Script* vm_script);

struct ToriRS_Task*
Task_CS2ScriptExec_New(
    struct CS2VM2_Script* script,
    struct CacheProvider* provider);

#endif
