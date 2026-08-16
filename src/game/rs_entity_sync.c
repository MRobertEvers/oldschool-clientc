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
    if( sync->local_pid >= 0 )
        RS_EntitySync_RemovePlayer(sync, world, sync->local_pid);
    sync->active_player_count = 0;
    sync->active_npc_count = 0;
}
