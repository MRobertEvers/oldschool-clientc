#include "engine/dat1/dat1_tasks.h"

#include <assert.h>

struct ToriRS_Task*
CreateTask_Dat1ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id)
{
    (void)provider;
    (void)iface_id;
    assert(false && "dat1: component pack load unsupported");
    return NULL;
}
