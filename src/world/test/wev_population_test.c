#include "world/world.h"
#include "painters/painters.h"
#include "toridraw_types.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int borrowed_calls;
static void borrowed(void* user,struct World* w)
{(void)user;++borrowed_calls;painter_add_normal_scenery(w->painter,6,6,0,905,1,1,0);}
static int has(struct PaintersBuffer* b,int id)
{for(int i=0;i<b->command_count;++i)if(b->commands[i]._bf_kind==PNTR_CMD_ELEMENT &&
  painter_command_element_id(&b->commands[i])==id)return 1;return 0;}
int main(void)
{
    ToriDraw_InitSinTable();ToriDraw_InitCosTable();
    struct World* world=World_New();World_ResetScene(world,50,50,16);World_SetLoadComplete(world,true);
    struct WorldEntityFacet_IdleAnimations idle={0};
    World_PlayerSpawn(world,901,0,3,3,idle);
    World_NpcSpawn(world,902,123,0,4,4,1,idle);
    World_SpotanimSpawn(world,903,0,5,5,0,0,0,30);
    char actions[5][32]={{0}};
    World_SceneryRegister(world,904,4321,7,7,0,1,1,10,0,0,"Mast",(const char(*)[32])actions,1);
    struct WorldEntity_Scenery* loc=World_SceneryGetByElementId(world,904);assert(loc);
    loc->runtime_spawn=true;loc->painter_wall_ab=-1;
    World_SetForeignActorRegisterFn(world,borrowed,NULL);
    struct PaintersBuffer b={.commands=calloc(128,sizeof(*b.commands)),.command_capacity=128};assert(b.commands);
    painter_set_draw_distance(world->painter,16);
    World_CycleRegisterDynamics(world);painter_paint_bucket(world->painter,&b,0,0,0);
    assert(has(&b,901) && has(&b,902) && has(&b,903) && has(&b,904) && has(&b,905));
    assert(borrowed_calls==1);
    world->suppress_dynamic_population=true;
    World_CycleRegisterDynamics(world);painter_paint_bucket(world->painter,&b,0,0,0);
    assert(!has(&b,901) && !has(&b,902) && !has(&b,903) && !has(&b,905));
    assert(has(&b,904) && borrowed_calls==1); /* runtime scenery stays live */
    world->suppress_dynamic_population=false;
    World_CycleRegisterDynamics(world);painter_paint_bucket(world->painter,&b,0,0,0);
    assert(has(&b,901) && has(&b,902) && has(&b,903) && has(&b,904) && has(&b,905));
    assert(borrowed_calls==2);
    free(b.commands);World_Free(world);
    puts("flat view population: own/borrowed actors and graphics removed; runtime scenery retained; next-frame restoration PASS");
    return 0;
}
