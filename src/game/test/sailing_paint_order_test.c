#include "game/sailing_paint_order.u.h"
#include <stdio.h>

static bool eligible(void* user,const struct SailingPaintSpan* span,int x,int z,int plane)
{(void)user;(void)span;(void)z;(void)plane;return x!=7; /* raised shoreline */}
static struct PaintersElementCommand marker(int kind,int view)
{struct PaintersElementCommand c={0};c._bf_kind=kind;c._entity._bf_entity=view;return c;}
static struct PaintersElementCommand loc(int id)
{struct PaintersElementCommand c={0};c._bf_kind=PNTR_CMD_ELEMENT;c._element_id=id;return c;}
static struct PaintersElementCommand ground(int x,int z,int level,int id)
{struct PaintersElementCommand c={0};c._bf_kind=PNTR_CMD_TERRAIN;c._terrain._bf_terrain_x=x;
 c._terrain._bf_terrain_z=z;c._terrain._bf_terrain_y=level;c._element_id=id;return c;}
int main(void)
{
    struct PaintersElementCommand c[]={loc(100),marker(PNTR_CMD_BEGIN_WORLD,1),ground(6,6,0,200),
        marker(PNTR_CMD_BEGIN_WORLD,2),ground(6,6,0,201),marker(PNTR_CMD_END_WORLD,2),
        ground(6,6,0,202),marker(PNTR_CMD_END_WORLD,1),loc(101),
        ground(6,6,0,300),ground(7,6,0,301),ground(30,30,0,302),ground(6,6,1,303)};
    struct PaintersElementCommand original[13];memcpy(original,c,sizeof(c));
    struct PaintersBuffer buffer={.commands=c,.command_count=13};
    assert(sailing_paint_order_ground(&buffer,NULL,0,NULL,NULL)==0);
    assert(!memcmp(c,original,sizeof(c)));
    struct SailingPaintSpan spans[]={
        {.view=1,.parent=0,.level=0,.x=5,.z=5,.width=4,.height=4},
        {.view=2,.parent=1,.level=0,.x=5,.z=5,.width=4,.height=4}};
    assert(sailing_paint_order_ground(&buffer,spans,2,eligible,NULL)==2);
    assert(c[0]._element_id==100 && c[1]._element_id==300);
    assert(c[2]._bf_kind==PNTR_CMD_BEGIN_WORLD && c[2]._entity._bf_entity==1);
    assert(c[3]._element_id==200 && c[4]._element_id==202);
    assert(c[5]._bf_kind==PNTR_CMD_BEGIN_WORLD && c[5]._entity._bf_entity==2);
    assert(c[6]._element_id==201 && c[7]._bf_kind==PNTR_CMD_END_WORLD);
    assert(c[8]._bf_kind==PNTR_CMD_END_WORLD && c[8]._entity._bf_entity==1);
    assert(c[9]._element_id==101); /* shore loc still follows boat */
    assert(c[10]._element_id==301 && c[11]._element_id==302 && c[12]._element_id==303);
    assert(sailing_paint_order_ground(&buffer,spans,2,eligible,NULL)==0); /* stable replay */
    struct PaintersElementCommand layers[]={loc(100),marker(PNTR_CMD_BEGIN_WORLD,1),loc(111),
        marker(PNTR_CMD_END_WORLD,1),loc(101),marker(PNTR_CMD_BEGIN_WORLD,2),loc(222),
        marker(PNTR_CMD_END_WORLD,2),ground(6,6,0,300)};
    buffer.commands=layers;buffer.command_count=9;
    struct SailingPaintSpan layer_spans[]={
        {.view=1,.parent=0,.bounds={100,100,400,400}},
        {.view=2,.parent=0,.flat=true,.bounds={200,200,500,500}}};
    assert(sailing_paint_order_flat(&buffer,layer_spans,2)==1);
    assert(layers[0]._element_id==100 && layers[1]._entity._bf_entity==2 && layers[2]._element_id==222);
    assert(layers[3]._bf_kind==PNTR_CMD_END_WORLD && layers[4]._entity._bf_entity==1);
    assert(layers[5]._element_id==111 && layers[6]._bf_kind==PNTR_CMD_END_WORLD);
    assert(layers[7]._element_id==101 && layers[8]._element_id==300);
    assert(sailing_paint_order_flat(&buffer,layer_spans,2)==0);
    /* A non-overlapping or differently parented flat scene is never moved,
     * and a no-move call (or no spans at all) leaves the bytes untouched. */
    struct PaintersElementCommand layers_before[9];memcpy(layers_before,layers,sizeof(layers));
    layer_spans[0].flat=true;layer_spans[1].flat=false;
    layer_spans[0].bounds[0]=600;layer_spans[0].bounds[2]=700;
    assert(sailing_paint_order_flat(&buffer,layer_spans,2)==0);
    assert(!memcmp(layers,layers_before,sizeof(layers)));
    layer_spans[0].bounds[0]=100;layer_spans[0].bounds[2]=400;layer_spans[0].parent=2;
    assert(sailing_paint_order_flat(&buffer,layer_spans,2)==0);
    assert(!memcmp(layers,layers_before,sizeof(layers)));
    assert(sailing_paint_order_flat(&buffer,NULL,0)==0);
    assert(!memcmp(layers,layers_before,sizeof(layers)));
    puts("scene dependencies: flat before full, parent ground before boat; nested ownership, raised shore, loc order and zero-boat bytes PASS");
    return 0;
}
