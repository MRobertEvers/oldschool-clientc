#include "ui/tree_refresh.h"

#include <assert.h>

void
UITreeRefresh_Raise(struct UITreeRefresh* refresh)
{
    assert(refresh);
    refresh->pending = 1;
}

int
UITreeRefresh_Pending(struct UITreeRefresh const* refresh)
{
    assert(refresh);
    return refresh->pending;
}

int
UITreeRefresh_Take(struct UITreeRefresh* refresh)
{
    assert(refresh);
    if( !refresh->pending )
        return 0;
    refresh->pending = 0;
    return 1;
}
