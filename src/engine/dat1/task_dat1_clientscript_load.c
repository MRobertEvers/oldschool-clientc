#include "engine/dat1/dat1_tasks.h"

#include <assert.h>

struct ToriRS_Task*
CreateTask_Dat1ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id)
{
    (void)provider;
    (void)script_id;
    assert(false && "dat1: clientscript load unsupported");
    return NULL;
}
