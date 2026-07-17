#include "engine/dat2/dat2_tasks.h"

#include <assert.h>

struct ToriRS_Task*
CreateTask_Dat2ComponentLoad(
    struct CacheProvider* provider,
    int packed_id)
{
    int iface_id;

    assert(provider);
    iface_id = packed_id >> 16;
    return CreateTask_Dat2ComponentPackLoad(provider, iface_id);
}
