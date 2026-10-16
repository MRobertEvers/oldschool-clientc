#include "rs_entity_sync.h"

#include "world/world.h"

#include <assert.h>
#include <string.h>

void
RS_EntitySync_Init(struct RS_EntitySync* sync)
{
    assert(sync);
    memset(sync, 0, sizeof(*sync));
    sync->local_pid = -1;
    World_EntityRegistryInit(&sync->registry, WORLD_ENTITY_REGISTRY_INITIAL_CAP);
}

void
RS_EntitySync_NpcList(
    struct RS_EntitySync* sync,
    int view,
    int** out_list,
    int** out_count,
    int* out_cap)
{
    assert(sync);
    assert(out_list);
    assert(out_count);
    assert(out_cap);
    assert(view >= 0);
    assert(view < WORLDVIEW_MAX);
    if( view == WORLDVIEW_ROOT )
    {
        *out_list = sync->active_npcs;
        *out_count = &sync->active_npc_count;
        *out_cap = RS_ENTITY_SYNC_MAX_NPCS;
        return;
    }
    *out_list = sync->view_npcs[view];
    *out_count = &sync->view_npc_count[view];
    *out_cap = RS_ENTITY_SYNC_VIEW_NPCS;
}

void
RS_EntitySync_ClearViewNpcs(
    struct RS_EntitySync* sync,
    struct World* world,
    int view)
{
    assert(sync);
    assert(view > WORLDVIEW_ROOT);
    assert(view < WORLDVIEW_MAX);
    for( int i = 0; i < sync->view_npc_count[view]; i++ )
        RS_EntitySync_RemoveNpc(sync, world, sync->view_npcs[view][i]);
    sync->view_npc_count[view] = 0;
}

void
RS_EntitySync_Free(struct RS_EntitySync* sync)
{
    if( !sync )
        return;
    World_EntityRegistryFree(&sync->registry);
}

static void
registry_remove(
    struct World_EntityRegistry* reg,
    int entity_id)
{
    for( int i = 0; i < reg->count; i++ )
    {
        if( reg->records[i].entity_id == entity_id )
        {
            reg->records[i] = reg->records[reg->count - 1];
            reg->count--;
            return;
        }
    }
}

int
RS_EntitySync_FindPlayer(
    struct RS_EntitySync* sync,
    int server_pid,
    int* out_world_idx,
    int* out_element_id)
{
    struct World_EntityRecord* rec;
    assert(sync);
    rec = World_EntityRegistryFind(
        &sync->registry, WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, server_pid));
    if( !rec )
        return 0;
    if( out_world_idx )
        *out_world_idx = rec->world_index;
    if( out_element_id )
        *out_element_id = rec->element_id;
    return 1;
}

int
RS_EntitySync_FindNpc(
    struct RS_EntitySync* sync,
    int server_slot,
    int* out_world_idx,
    int* out_element_id)
{
    struct World_EntityRecord* rec;
    assert(sync);
    rec = World_EntityRegistryFind(
        &sync->registry, WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, server_slot));
    if( !rec )
        return 0;
    if( out_world_idx )
        *out_world_idx = rec->world_index;
    if( out_element_id )
        *out_element_id = rec->element_id;
    return 1;
}

void
RS_EntitySync_RegisterPlayer(
    struct RS_EntitySync* sync,
    int server_pid,
    int element_id,
    int world_idx)
{
    assert(sync);
    World_EntityRegistryRegister(
        &sync->registry,
        WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, server_pid),
        element_id,
        world_idx);
}

void
RS_EntitySync_RegisterNpc(
    struct RS_EntitySync* sync,
    int server_slot,
    int element_id,
    int world_idx)
{
    assert(sync);
    World_EntityRegistryRegister(
        &sync->registry, WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, server_slot), element_id, world_idx);
}

void
RS_EntitySync_RemovePlayer(
    struct RS_EntitySync* sync,
    struct World* world,
    int server_pid)
{
    int world_idx;
    assert(sync);
    if( RS_EntitySync_FindPlayer(sync, server_pid, &world_idx, NULL) )
    {
        if( world )
            World_PlayerDespawn(world, world_idx);
        registry_remove(&sync->registry, WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, server_pid));
    }
}

void
RS_EntitySync_RemoveNpc(
    struct RS_EntitySync* sync,
    struct World* world,
    int server_slot)
{
    int world_idx;
    assert(sync);
    if( RS_EntitySync_FindNpc(sync, server_slot, &world_idx, NULL) )
    {
        if( world )
            World_NpcDespawn(world, world_idx);
        registry_remove(&sync->registry, WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, server_slot));
    }
}

void
RS_EntitySync_Clear(
    struct RS_EntitySync* sync,
    struct World* world)
{
    assert(sync);
    for( int i = 0; i < sync->active_player_count; i++ )
        RS_EntitySync_RemovePlayer(sync, world, sync->active_players[i]);
    for( int i = 0; i < sync->active_npc_count; i++ )
        RS_EntitySync_RemoveNpc(sync, world, sync->active_npcs[i]);
    for( int v = 1; v < WORLDVIEW_MAX; v++ )
        RS_EntitySync_ClearViewNpcs(sync, world, v);
    if( sync->local_pid >= 0 )
        RS_EntitySync_RemovePlayer(sync, world, sync->local_pid);
    sync->active_player_count = 0;
    sync->active_npc_count = 0;
}
