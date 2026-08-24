#include "worldview.h"

#include "world.h"

#include "engine/world_builder/world_builder.h"

#include <assert.h>
#include <string.h>

void
WorldviewRegistry_Init(struct WorldviewRegistry* reg)
{
    assert(reg);
    memset(reg, 0, sizeof(*reg));
    for( int i = 0; i < WORLDVIEW_MAX; i++ )
    {
        reg->views[i].id = i;
        reg->views[i].parent_id = WORLDVIEW_PARENT_NONE;
    }
}

void
WorldviewRegistry_Free(struct WorldviewRegistry* reg)
{
    if( !reg )
        return;
    for( int i = WORLDVIEW_MAX - 1; i > WORLDVIEW_ROOT; i-- )
    {
        if( reg->views[i].live )
            WorldviewRegistry_Release(reg, i);
    }
    /* The root's pair is borrowed — just drop the reference. */
    reg->views[WORLDVIEW_ROOT].world = NULL;
    reg->views[WORLDVIEW_ROOT].builder = NULL;
    reg->views[WORLDVIEW_ROOT].live = false;
}

struct Worldview*
WorldviewRegistry_RegisterRoot(
    struct WorldviewRegistry* reg,
    struct World* world,
    struct WorldBuilder* builder)
{
    struct Worldview* view;

    assert(reg);
    assert(world);
    assert(builder);
    view = &reg->views[WORLDVIEW_ROOT];
    assert(!view->live);
    view->world = world;
    view->builder = builder;
    view->base_x = 0;
    view->base_z = 0;
    view->size_x_tiles = 0;
    view->size_z_tiles = 0;
    /* The root rides in nothing, so it has no level within a carrier. */
    view->parent_level = 0;
    view->parent_id = WORLDVIEW_PARENT_NONE;
    view->live = true;
    view->owns = false;
    return view;
}

struct Worldview*
WorldviewRegistry_Register(
    struct WorldviewRegistry* reg,
    int id,
    struct World* world,
    struct WorldBuilder* builder,
    int base_x,
    int base_z,
    int size_x_tiles,
    int size_z_tiles,
    int parent_id)
{
    struct Worldview* view;

    assert(reg);
    /* id 0 goes through RegisterRoot — its ownership rule is different. */
    assert(id > WORLDVIEW_ROOT);
    assert(id < WORLDVIEW_MAX);
    assert(world);
    assert(builder);
    /* Sub-world sizes come off the wire as zone nibbles × 8 tiles. */
    assert(size_x_tiles > 0);
    assert(size_z_tiles > 0);
    assert(parent_id >= 0);
    assert(parent_id < WORLDVIEW_MAX);
    assert(reg->views[parent_id].live);
    view = &reg->views[id];
    assert(!view->live);
    view->world = world;
    view->builder = builder;
    view->base_x = base_x;
    view->base_z = base_z;
    view->size_x_tiles = size_x_tiles;
    view->size_z_tiles = size_z_tiles;
    /* The spawn record carries no level; SET_ACTIVE_WORLD does, and it
     * arrives with the rebuild. Surface plane until then. */
    view->parent_level = 0;
    view->parent_id = parent_id;
    view->live = true;
    view->owns = true;
    return view;
}

void
WorldviewRegistry_Release(
    struct WorldviewRegistry* reg,
    int id)
{
    struct Worldview* view;

    assert(reg);
    assert(id > WORLDVIEW_ROOT);
    assert(id < WORLDVIEW_MAX);
    view = &reg->views[id];
    assert(view->live);
    assert(view->owns);
    /* Builder before world, same order the App tears its pair down: the
     * builder references the world it built. */
    WorldBuilder_Free(view->builder);
    World_Free(view->world);
    view->world = NULL;
    view->builder = NULL;
    view->base_x = 0;
    view->base_z = 0;
    view->size_x_tiles = 0;
    view->size_z_tiles = 0;
    view->parent_level = 0;
    view->parent_id = WORLDVIEW_PARENT_NONE;
    view->live = false;
    view->owns = false;
}

struct Worldview*
WorldviewRegistry_Get(
    struct WorldviewRegistry* reg,
    int id)
{
    assert(reg);
    assert(id >= 0);
    assert(id < WORLDVIEW_MAX);
    assert(reg->views[id].live);
    return &reg->views[id];
}

bool
WorldviewRegistry_IsLive(
    struct WorldviewRegistry const* reg,
    int id)
{
    assert(reg);
    assert(id >= 0);
    assert(id < WORLDVIEW_MAX);
    return reg->views[id].live;
}
