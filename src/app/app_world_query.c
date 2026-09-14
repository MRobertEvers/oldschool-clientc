/*
 * Small answers about the world: the local player, its plane, terrain height,
 * and which view a world belongs to.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Server-synced local player entity (esync pid), or NULL (offline / not yet
 * spawned). Shared by camera follow, minimap centering, and roof check. */
struct WorldEntity_Player*
app_local_player(struct App* app)
{
    int world_idx;
    if( !app->world )
        return NULL;
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return NULL;
    return World_EntityPoolGet(&app->world->entities.player, world_idx);
}

int
app_world_local_plane(void* userdata)
{
    struct App* app = (struct App*)userdata;

    assert(app);
    return app_cinema_level(app);
}

/* World_HeightFn: projectiles/movers track terrain height (world units). */
int
app_world_height(
    void* userdata,
    int world_x,
    int world_z,
    int level)
{
    struct App* app = (struct App*)userdata;

    assert(app);
    return World_HeightAt(app->world, world_x, world_z, level);
}

/** The registry slot whose World is `world`, or -1. Views are 16 and the
 * lookup runs once per painter pass, so a scan beats a back-pointer. */
int
app_worldview_id_of(
    struct App* app,
    const struct World* world)
{
    assert(app);
    assert(world);
    for( int i = 0; i < WORLDVIEW_MAX; i++ )
    {
        if( WorldviewRegistry_IsLive(&app->worldviews, i) &&
            WorldviewRegistry_Get(&app->worldviews, i)->world == world )
            return i;
    }
    return -1;
}
