/* Cross-scene ground dependency for the painter renderer. Native world-entity
 * insertion uses radius60; this engine's terrain traversal otherwise allows a
 * neighboring ocean tile to overpaint the hull's overhanging geometry. Move
 * only eligible parent ground before that view's marker. Parent actors/locs,
 * child scene contents and all markers retain their relative order. */
#include "painters/painters.h"
#include "world/worldview.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Spans are indexed by Worldview id and the painter's per-view tables by the
 * command stream's entity id; the two id spaces must be the same size. */
_Static_assert(WORLDVIEW_MAX == PAINTER_MAX_WORLD_VIEWS,
               "worldview and painter view id spaces must agree");

/* Per-frame scratch, grown on demand and kept: the ground pass runs every
 * frame a boat is live and must not allocate in steady state. */
static int* sailing_paint_scratch_ints;
static int sailing_paint_scratch_int_cap;
static struct PaintersElementCommand* sailing_paint_scratch_commands;
static int sailing_paint_scratch_command_cap;

static int*
sailing_paint_scratch_int_buffers(int n, int buffers)
{
    assert(n > 0);
    assert(buffers > 0);
    if( sailing_paint_scratch_int_cap < n * buffers )
    {
        sailing_paint_scratch_int_cap = n * buffers;
        sailing_paint_scratch_ints = realloc(
            sailing_paint_scratch_ints,
            (size_t)sailing_paint_scratch_int_cap * sizeof(*sailing_paint_scratch_ints));
        assert(sailing_paint_scratch_ints);
    }
    return sailing_paint_scratch_ints;
}

static struct PaintersElementCommand*
sailing_paint_scratch_command_buffer(int n)
{
    assert(n > 0);
    if( sailing_paint_scratch_command_cap < n )
    {
        sailing_paint_scratch_command_cap = n;
        sailing_paint_scratch_commands = realloc(
            sailing_paint_scratch_commands,
            (size_t)n * sizeof(*sailing_paint_scratch_commands));
        assert(sailing_paint_scratch_commands);
    }
    return sailing_paint_scratch_commands;
}

struct SailingPaintSpan
{
    int view, parent, level;
    int x, z, width, height;
    int surface_y;
    bool flat;
    int bounds[4]; /* native fine AABB, separate from terrain dependency span */
};
typedef bool (*SailingPaintGroundFn)(void*, const struct SailingPaintSpan*, int, int, int);

/* Flat geometry lies just above the parent surface, so an overlapping full
 * scene must composite over it. Relocate complete sibling marker subtrees;
 * all parent loc/actor commands and full view subtrees keep their order. */
static int
sailing_paint_order_flat(struct PaintersBuffer* buffer,
                        const struct SailingPaintSpan* spans,int count)
{
    assert(buffer);
    int has_flat=0;
    for(int i=0;i<count;++i)has_flat|=spans[i].flat;
    if(!has_flat || !buffer->command_count)return 0;
    int begin[PAINTER_MAX_WORLD_VIEWS],end[PAINTER_MAX_WORLD_VIEWS],target[PAINTER_MAX_WORLD_VIEWS];
    for(int id=0;id<PAINTER_MAX_WORLD_VIEWS;++id)begin[id]=end[id]=target[id]=-1;
    for(int i=0;i<buffer->command_count;++i)
    {
        const struct PaintersElementCommand* c=&buffer->commands[i];
        if(c->_bf_kind!=PNTR_CMD_BEGIN_WORLD)continue;
        int id=(int)c->_entity._bf_entity;
        assert(id>0 && id<PAINTER_MAX_WORLD_VIEWS);
        if(begin[id]>=0)continue;
        begin[id]=i;int depth=1;
        for(int j=i+1;j<buffer->command_count;++j)
        {
            if(buffer->commands[j]._bf_kind==PNTR_CMD_BEGIN_WORLD)++depth;
            if(buffer->commands[j]._bf_kind==PNTR_CMD_END_WORLD)--depth;
            if(depth==0){end[id]=j;break;}
        }
        assert(end[id]>=i);
    }
    int moved=0;
    for(int f=0;f<count;++f)
    {
        const struct SailingPaintSpan* flat=&spans[f];
        assert(flat->view>0);
        assert(flat->view<PAINTER_MAX_WORLD_VIEWS);
        if(!flat->flat || begin[flat->view]<0)continue;
        for(int p=0;p<count;++p)
        {
            const struct SailingPaintSpan* full=&spans[p];
            assert(full->view>0);
            assert(full->view<PAINTER_MAX_WORLD_VIEWS);
            int before=begin[full->view];
            if(full->flat || full->parent!=flat->parent || before<0 ||
               before>=begin[flat->view] || (target[flat->view]>=0 && target[flat->view]<=before))continue;
            if(flat->bounds[0]<=full->bounds[2] && flat->bounds[2]>=full->bounds[0] &&
               flat->bounds[1]<=full->bounds[3] && flat->bounds[3]>=full->bounds[1])
                target[flat->view]=before;
        }
        moved+=target[flat->view]>=0;
    }
    if(!moved)return 0;
    struct PaintersElementCommand* copy=sailing_paint_scratch_command_buffer(buffer->command_count);
    int order[PAINTER_MAX_WORLD_VIEWS],ordered=0;
    for(int id=1;id<PAINTER_MAX_WORLD_VIEWS;++id)if(target[id]>=0)
    {
        int j=ordered;
        while(j>0 && begin[order[j-1]]>begin[id]){order[j]=order[j-1];--j;}
        order[j]=id;++ordered;
    }
    int used=0;
    for(int i=0;i<buffer->command_count;)
    {
        /* Original ordering among equally placed flat siblings is retained. */
        for(int j=0;j<ordered;++j)
        {
            int id=order[j];if(target[id]!=i)continue;
            int n=end[id]-begin[id]+1;
            memcpy(copy+used,buffer->commands+begin[id],(size_t)n*sizeof(*copy));used+=n;
        }
        const struct PaintersElementCommand* c=&buffer->commands[i];
        int id=(int)c->_entity._bf_entity;
        if(c->_bf_kind==PNTR_CMD_BEGIN_WORLD && begin[id]==i && target[id]>=0)i=end[id]+1;
        else copy[used++]=buffer->commands[i++];
    }
    assert(used==buffer->command_count);
    memcpy(buffer->commands,copy,(size_t)used*sizeof(*copy));
    return moved;
}

static int
sailing_paint_order_ground(struct PaintersBuffer* buffer,
                          const struct SailingPaintSpan* spans, int count,
                          SailingPaintGroundFn eligible, void* user)
{
    assert(buffer);
    if( count==0 || buffer->command_count==0 ) return 0;
    assert(spans);
    assert(eligible);
    int n=buffer->command_count;
    int* scratch=sailing_paint_scratch_int_buffers(n,5);
    int* owner=scratch;
    int* target=scratch+n;
    int* head=scratch+2*n;
    int* tail=scratch+3*n;
    int* next=scratch+4*n;
    int begin[PAINTER_MAX_WORLD_VIEWS], stack[PAINTER_MAX_WORLD_VIEWS+1]={0}, depth=0;
    for(int i=0;i<PAINTER_MAX_WORLD_VIEWS;++i)begin[i]=-1;
    for(int i=0;i<n;++i)
    {
        const struct PaintersElementCommand* c=&buffer->commands[i];
        owner[i]=stack[depth];target[i]=head[i]=tail[i]=next[i]=-1;
        int id=(int)c->_entity._bf_entity;
        if(c->_bf_kind==PNTR_CMD_BEGIN_WORLD)
        {
            assert(id>0 && id<PAINTER_MAX_WORLD_VIEWS);
            assert(depth<PAINTER_MAX_WORLD_VIEWS);
            if(begin[id]<0)begin[id]=i;
            stack[++depth]=id;
        }
        else if(c->_bf_kind==PNTR_CMD_END_WORLD)
        {assert(depth>0 && stack[depth]==id);--depth;}
    }
    assert(depth==0);
    int moved=0;
    for(int i=0;i<n;++i)
    {
        const struct PaintersElementCommand* c=&buffer->commands[i];
        if(c->_bf_kind!=PNTR_CMD_TERRAIN && c->_bf_kind!=PNTR_CMD_TERRAIN_PICK_ONLY)continue;
        int x=(int)c->_terrain._bf_terrain_x,z=(int)c->_terrain._bf_terrain_z;
        int level=(int)c->_terrain._bf_terrain_y;
        for(int j=0;j<count;++j)
        {
            const struct SailingPaintSpan* span=&spans[j];
            assert(span->view>0);
            assert(span->view<PAINTER_MAX_WORLD_VIEWS);
            int before=begin[span->view];
            if(span->parent!=owner[i] || before<0 || before>=i ||
               level!=span->level || x<span->x || z<span->z ||
               x>=span->x+span->width || z>=span->z+span->height ||
               (target[i]>=0 && target[i]<=before))continue;
            if(eligible(user,span,x,z,level))target[i]=before;
        }
        if(target[i]>=0)
        {
            int before=target[i];
            if(tail[before]>=0)next[tail[before]]=i;else head[before]=i;
            tail[before]=i;++moved;
        }
    }
    if(moved)
    {
        struct PaintersElementCommand* copy=sailing_paint_scratch_command_buffer(n);
        int used=0;
        for(int i=0;i<n;++i)
        {
            for(int j=head[i];j>=0;j=next[j])copy[used++]=buffer->commands[j];
            if(target[i]<0)copy[used++]=buffer->commands[i];
        }
        assert(used==n);memcpy(buffer->commands,copy,(size_t)n*sizeof(*copy));
    }
    return moved;
}
