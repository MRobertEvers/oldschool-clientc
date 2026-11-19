#include "ui/uitree_kept_mounts.h"

#include <assert.h>
#include <string.h>

void
UITreeKeptMounts_Clear(struct UITreeKeptMounts* kept)
{
    assert(kept);
    kept->count = 0;
}

int
UITreeKeptMounts_Count(struct UITreeKeptMounts const* kept)
{
    assert(kept);
    return kept->count;
}

struct UITreeInterfaceParent const*
UITreeKeptMounts_At(
    struct UITreeKeptMounts const* kept,
    int index)
{
    assert(kept);
    assert(index >= 0);
    assert(index < kept->count);
    return &kept->items[index];
}

bool
UITreeKeptMounts_Add(
    struct UITreeKeptMounts* kept,
    int container_uid,
    int group_id,
    int type)
{
    assert(kept);
    if( kept->count >= UITREE_INTERFACE_PARENT_MAX )
        return false;
    kept->items[kept->count].container_uid = container_uid;
    kept->items[kept->count].group_id = group_id;
    kept->items[kept->count].type = type;
    kept->count++;
    return true;
}

struct UITreeInterfaceParent
UITreeKeptMounts_Take(
    struct UITreeKeptMounts* kept,
    int container_uid)
{
    struct UITreeInterfaceParent taken = { container_uid, 0, 0 };

    assert(kept);
    for( int i = 0; i < kept->count; i++ )
    {
        if( kept->items[i].container_uid != container_uid )
            continue;
        taken = kept->items[i];
        kept->items[i] = kept->items[--kept->count];
        break;
    }
    return taken;
}

static bool
kept_mounts_names_slot(
    struct UITreeInterfaceParent const* records,
    int count,
    int container_uid)
{
    for( int i = 0; i < count; i++ )
        if( records[i].container_uid == container_uid )
            return true;
    return false;
}

int
UITreeKeptMounts_Detach(
    struct UITreeKeptMounts* kept,
    struct UITreeInterfaceParent const* live,
    int live_count,
    struct UITreeInterfaceParent* out_forgotten)
{
    struct UITreeInterfaceParent old[UITREE_INTERFACE_PARENT_MAX];
    int old_count = 0;
    int forgotten = 0;
    int count = live_count;

    assert(kept);
    assert(live);
    assert(out_forgotten);
    assert(live_count >= 0);
    assert(live_count <= UITREE_INTERFACE_PARENT_MAX);
    for( int i = 0; i < kept->count; i++ )
        if( !kept_mounts_names_slot(live, live_count, kept->items[i].container_uid) )
            old[old_count++] = kept->items[i];
    memcpy(kept->items, live, (size_t)live_count * sizeof(kept->items[0]));
    for( int i = 0; i < old_count; i++ )
    {
        if( count >= UITREE_INTERFACE_PARENT_MAX )
        {
            out_forgotten[forgotten++] = old[i];
            continue;
        }
        kept->items[count++] = old[i];
    }
    kept->count = count;
    return forgotten;
}
