/*
 * N3: a player whose appearance carries the hidden flag (deob class60
 * .field551, WorldEntity_Player.hidden) is not put in the scene: deob
 * Statics.method10053 returns before the tile claim and the scene add, for
 * every player the local one included. So it is not painted, and it claims no
 * tile, so a visible player standing on its square is painted. And the pass
 * stamps each player it does put in the scene (field1463), which the overhead
 * name reads.
 */
#include "mem/torirs_mem.h"
#include "world/world.h"
#include "world/entity_player.h"
#include "painters/painters.h"
#include "toridraw_types.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int
painted(
    struct PaintersBuffer* b,
    int id)
{
    for( int i = 0; i < b->command_count; ++i )
        if( b->commands[i]._bf_kind == PNTR_CMD_ELEMENT &&
            painter_command_element_id(&b->commands[i]) == id )
            return 1;
    return 0;
}

static struct WorldEntity_Player*
spawn(
    struct World* world,
    int element_id,
    int pid,
    int x,
    int z)
{
    struct WorldEntityFacet_IdleAnimations idle = { 0 };
    int idx = World_PlayerSpawn(world, element_id, 0, x, z, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);

    assert(player);
    player->server_pid = pid;
    return player;
}

static void
paint(
    struct World* world,
    struct PaintersBuffer* b)
{
    b->command_count = 0;
    World_CycleRegisterDynamics(world);
    painter_paint_bucket(world->painter, b, 0, 0, 0);
}

int
main(void)
{
    struct PaintersBuffer b = { .commands = ToriMem_Calloc(ToriMem_Render(), 256, sizeof(*b.commands)),
                                .command_capacity = 256 };
    struct World* world;
    struct WorldEntity_Player* local;
    struct WorldEntity_Player* hidden;
    int failures = 0;

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    assert(b.commands);

    world = World_New();
    World_ResetScene(world, 50, 50, 16);
    World_SetLoadComplete(world, true);
    painter_set_draw_distance(world->painter, 16);
    painter_set_level_mask(world->painter, 0xF);
    local = spawn(world, 930, 7, 5, 5);
    world->local_pid = 7;
    (void)spawn(world, 931, 8, 7, 5);
    hidden = spawn(world, 932, 9, 9, 5);
    /* A visible player on the hidden one's square: the hidden one claims no
     * tile, so this one is not deduped away behind it. */
    (void)spawn(world, 933, 10, 9, 5);
    hidden->hidden = 1;

    paint(world, &b);
    if( !painted(&b, 930) || !painted(&b, 931) )
    {
        fprintf(stderr, "FAIL: a visible player was not painted\n");
        failures++;
    }
    if( painted(&b, 932) )
    {
        fprintf(stderr, "FAIL: a hidden player was painted\n");
        failures++;
    }
    if( !painted(&b, 933) )
    {
        fprintf(stderr, "FAIL: the visible player on the hidden one's square was not painted\n");
        failures++;
    }

    /* N2: the pass stamps who it put in the scene (deob field1463), which is
     * what an overhead name asks. Two visible players on one square: the
     * first claims it, the second is not put in the scene. */
    {
        struct WorldEntity_Player* first = spawn(world, 934, 11, 11, 5);
        struct WorldEntity_Player* second = spawn(world, 935, 12, 11, 5);

        paint(world, &b);
        if( local->drawn_scene_cycle != world->scene_cycle || first->drawn_scene_cycle != world->scene_cycle )
        {
            fprintf(stderr, "FAIL: a drawn player is not stamped with the pass\n");
            failures++;
        }
        if( second->drawn_scene_cycle == world->scene_cycle || painted(&b, 935) )
        {
            fprintf(stderr, "FAIL: the second player on a claimed square counts as drawn\n");
            failures++;
        }
        if( hidden->drawn_scene_cycle == world->scene_cycle )
        {
            fprintf(stderr, "FAIL: a hidden player is stamped as drawn\n");
            failures++;
        }
    }

    local->hidden = 1;
    paint(world, &b);
    if( painted(&b, 930) )
    {
        fprintf(stderr, "FAIL: a hidden local player was painted\n");
        failures++;
    }
    if( !painted(&b, 931) )
    {
        fprintf(stderr, "FAIL: the others are painted while the local player is hidden\n");
        failures++;
    }

    ToriMem_Free(ToriMem_Render(), b.commands); /* the painter grows it in RENDER */
    World_Free(world);
    if( failures )
        return 1;
    puts("hidden players: not painted, the local player included, and they claim no tile PASS");
    return 0;
}
