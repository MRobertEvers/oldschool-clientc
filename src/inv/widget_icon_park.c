#include "inv/widget_icon_park.h"

#include "mem/torirs_mem.h"

#include <assert.h>

struct WidgetIconPark*
WidgetIconPark_New(void)
{
    struct WidgetIconPark* park = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*park));

    assert(park);
    return park;
}

void
WidgetIconPark_Free(struct WidgetIconPark* park)
{
    if( !park )
        return;
    ToriMem_Free(ToriMem_Process(), park->icons);
    ToriMem_Free(ToriMem_Process(), park);
}

struct WidgetIconRequest*
WidgetIconPark_Row(
    struct WidgetIconPark* park,
    int component_id,
    bool* found)
{
    struct WidgetIconRequest* row = NULL;

    assert(park);
    assert(found);
    for( int i = 0; i < park->count; i++ )
        if( park->icons[i].com_id == component_id )
        {
            row = &park->icons[i];
            break;
        }
    *found = row != NULL;
    if( !row )
    {
        if( park->count == park->cap )
        {
            int cap = park->cap ? park->cap * 2 : 32;
            park->icons = ToriMem_Realloc(ToriMem_Process(), park->icons, (size_t)cap * sizeof(*park->icons));
            assert(park->icons);
            park->cap = cap;
        }
        row = &park->icons[park->count++];
        row->com_id = component_id;
    }
    return row;
}

int
WidgetIconPark_Count(struct WidgetIconPark const* park)
{
    assert(park);
    return park->count;
}

int
WidgetIconPark_TakeDue(
    struct WidgetIconPark* park,
    uint64_t now_ms,
    int max,
    struct WidgetIconRequest* out)
{
    int taken = 0;
    int kept = 0;

    assert(park);
    assert(max >= 0);
    assert(out);
    for( int i = 0; i < park->count; i++ )
    {
        struct WidgetIconRequest const row = park->icons[i];

        if( taken < max && row.due_ms <= now_ms )
        {
            out[taken++] = row;
            continue;
        }
        park->icons[kept++] = row;
    }
    park->count = kept;
    return taken;
}
