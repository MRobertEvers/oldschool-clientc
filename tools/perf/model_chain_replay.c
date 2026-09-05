/* Replay REAL GPU model command chains without Android UI, assets or GL.
 * Inputs are already posed; animation evaluation and driver work are excluded.
 * Calls the production stage and production U16 index packer. */
#define _GNU_SOURCE
#include "model_chain_format.h"
#include "platform/platform_renderer_gles2_dualcore_stage.h"
#include "platform/platform_renderer_gles2_indices.h"
#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_scene.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

struct Record {
    struct ModelChainHeader h;
    struct ToriRS_RenderCommand_Model command;
    int32_t *x, *y, *z, *order;
};
struct Asset { uint32_t token; struct ToriDraw_Model* model; int next, kind; };
struct Corpus {
    struct Record* records;
    size_t count;
    struct Asset* assets;
    size_t asset_count;
    uint64_t visible, sort_calls, faces, animated, dynamic, clipped, picked, prioritized, passes;
};
struct Bench {
    struct GLES2DualCoreStageContext context;
    struct GLES2DualCoreStageArena arena;
    uint16_t* indices;
    uint32_t index_count;
    struct ToriRS_RenderCommand* commands;
};
static volatile uint64_t sink;
static void fail(const char* text) { fprintf(stderr, "%s\n", text); exit(1); }
static void* read_array(FILE* f, size_t bytes)
{
    void* p = malloc(bytes ? bytes : 1);
    if( !p || (bytes && fread(p, 1, bytes, f) != bytes) ) fail("truncated capture/allocation failure");
    return p;
}
static int same_model(const struct ToriDraw_Model* a, const struct ToriDraw_Model* b)
{
    int n=a->face_count, v=a->vertex_count;
    return n==b->face_count && v==b->vertex_count && a->flags==b->flags &&
        a->tile_sort_kernel==b->tile_sort_kernel && a->textured_face_count==b->textured_face_count &&
        a->has_bounds_cylinder==b->has_bounds_cylinder &&
        !memcmp(&a->bounds_cylinder,&b->bounds_cylinder,sizeof(a->bounds_cylinder)) &&
        !memcmp(a->vertices_x,b->vertices_x,(size_t)v*2) &&
        !memcmp(a->vertices_y,b->vertices_y,(size_t)v*2) &&
        !memcmp(a->vertices_z,b->vertices_z,(size_t)v*2) &&
        !memcmp(a->face_indices_a,b->face_indices_a,(size_t)n*2) &&
        !memcmp(a->face_indices_b,b->face_indices_b,(size_t)n*2) &&
        !memcmp(a->face_indices_c,b->face_indices_c,(size_t)n*2) &&
        (!a->face_priorities == !b->face_priorities) &&
        (!a->face_priorities || !memcmp(a->face_priorities,b->face_priorities,((size_t)n+1)/2)) &&
        (!a->face_colors_c == !b->face_colors_c) &&
        (!a->face_colors_c || !memcmp(a->face_colors_c,b->face_colors_c,(size_t)n*2));
}
static struct Corpus load_corpus(const char* path)
{
    struct Corpus c={0};
    int buckets[4096];
    memset(buckets,0xff,sizeof(buckets));
    FILE* f=fopen(path,"rb");
    if( !f ) { perror(path); exit(1); }
    int completed=0;
    for( ;; )
    {
        struct Record r={0};
        struct ModelChainHeader* h=&r.h;
        size_t n=fread(h,1,sizeof(*h),f);
        if( !n && feof(f) ) break;
        if( n==sizeof(*h) && h->magic==MODEL_CHAIN_END )
        {
            if( h->version!=MODEL_CHAIN_VERSION || h->ordinal!=c.count ||
                h->pass!=c.passes || fgetc(f)!=EOF ) fail("invalid capture footer");
            completed=1;
            break;
        }
        if( n!=sizeof(*h) || h->magic!=MODEL_CHAIN_MAGIC || h->version!=MODEL_CHAIN_VERSION ||
            h->vertices<0 || h->vertices>32767 || h->faces<0 || h->faces>32767 ||
            h->sorted_count<0 || h->sorted_count>h->faces || h->textures<0 ||
            h->tile_kernel<0 || h->tile_kernel>4 || h->depth_levels!=16384 ||
            h->ordinal!=c.count || (h->options & ~0xff00001fu) || (h->pick_flags & ~15u) ||
            (h->tile_kernel && (h->vertices!=4 || h->faces!=2)) )
            fail("invalid chain header");
        int kind=(int)(h->options >> MODEL_CHAIN_KIND_SHIFT);
        if( !ToriDraw_ModelKindIsFull(kind) ) fail("invalid model kind");
        if( c.count && (h->max_faces!=c.records[0].h.max_faces ||
                       h->max_vertices!=c.records[0].h.max_vertices) )
            fail("mixed scratch capacities");
        struct ToriDraw_Model* m=ToriDraw_ModelNew(h->vertices,h->faces,0);
        if( !m ) fail("model allocation failed");
        if( kind!=TORIDRAWMK_MODEL )
        {
            size_t size=kind==TORIDRAWMK_MODEL_SHARED?sizeof(struct ToriDraw_SharedModel):
                kind==TORIDRAWMK_MODEL_LENT_FACES?sizeof(struct ToriDraw_ModelLentFaces):sizeof(struct ToriDraw_ModelHD);
            void* wrapper=calloc(1,size);
            if( !wrapper ) fail("model wrapper allocation failed");
            memcpy(wrapper,m,sizeof(*m)); free(m); m=wrapper;
        }
        m->flags=h->model_flags; m->tile_sort_kernel=h->tile_kernel;
        m->has_bounds_cylinder=!!(h->options&MODEL_CHAIN_BOUNDS); m->bounds_cylinder=h->bounds;
        m->textured_face_count=h->textures;
        m->vertices_x=read_array(f,(size_t)h->vertices*2);
        m->vertices_y=read_array(f,(size_t)h->vertices*2);
        m->vertices_z=read_array(f,(size_t)h->vertices*2);
        m->face_indices_a=read_array(f,(size_t)h->faces*2);
        m->face_indices_b=read_array(f,(size_t)h->faces*2);
        m->face_indices_c=read_array(f,(size_t)h->faces*2);
        if( h->options&MODEL_CHAIN_PRIORITIES ) m->face_priorities=read_array(f,((size_t)h->faces+1)/2);
        if( h->options&MODEL_CHAIN_COLOUR_C ) m->face_colors_c=read_array(f,(size_t)h->faces*2);
        for( int i=0;i<h->faces;i++ )
            if( (unsigned)m->face_indices_a[i]>=(unsigned)h->vertices ||
                (unsigned)m->face_indices_b[i]>=(unsigned)h->vertices ||
                (unsigned)m->face_indices_c[i]>=(unsigned)h->vertices ||
                (m->face_priorities && ((m->face_priorities[i/2]>>((i&1)*4))&15)>11) )
                fail("invalid face index/priority");
        uint32_t token=(uint32_t)h->model_token;
        unsigned bucket=((token>>4)*2654435761u)>>20;
        int found=-1;
        for( int i=buckets[bucket]; i>=0; i=c.assets[i].next )
            if( c.assets[i].token==token && c.assets[i].kind==kind && same_model(c.assets[i].model,m) ) { found=i; break; }
        if( found>=0 ) { ToriDraw_ModelFree(m); m=c.assets[found].model; }
        else
        {
            struct Asset* grown=realloc(c.assets,(c.asset_count+1)*sizeof(*c.assets));
            if( !grown ) fail("asset allocation failed");
            c.assets=grown;
            c.assets[c.asset_count]=(struct Asset){token,m,buckets[bucket],kind};
            buckets[bucket]=(int)c.asset_count++;
        }
        r.command.model.kind=kind;
        if( kind==TORIDRAWMK_MODEL_SHARED ) r.command.model.u.shared=(struct ToriDraw_SharedModel*)m;
        else if( kind==TORIDRAWMK_MODEL_LENT_FACES ) r.command.model.u.lent=(struct ToriDraw_ModelLentFaces*)m;
        else r.command.model.u.model.model=m;
        r.command.position=h->position;
        r.command.element_id=h->element_id;
        r.command.dynamic=!!(h->options&MODEL_CHAIN_DYNAMIC);
        r.command.pickable=!!(h->pick_flags&MODEL_CHAIN_PICKABLE);
        r.command.pick_aabb=!!(h->pick_flags&MODEL_CHAIN_PICK_AABB);
        r.command.pick_terrain=!!(h->pick_flags&MODEL_CHAIN_PICK_TERRAIN);
        r.command.pick_only=!!(h->pick_flags&MODEL_CHAIN_PICK_ONLY);
        if( h->cull==TORIDRAW_CULL_VISIBLE )
        {
            r.x=read_array(f,(size_t)h->vertices*4);
            r.y=read_array(f,(size_t)h->vertices*4);
            r.z=read_array(f,(size_t)h->vertices*4);
            r.order=read_array(f,(size_t)h->sorted_count*4);
            c.visible++; c.faces+=h->sorted_count;
            c.sort_calls+=!(h->pick_flags&MODEL_CHAIN_PICK_ONLY);
            c.clipped+=!!h->near_clipped; c.picked+=!!h->pick_hit;
        }
        if( !c.count || h->pass!=c.records[c.count-1].h.pass ) c.passes++;
        c.animated+=!!(h->options&MODEL_CHAIN_ANIMATED);
        c.dynamic+=!!(h->options&MODEL_CHAIN_DYNAMIC);
        c.prioritized+=!!(h->options&MODEL_CHAIN_PRIORITIES);
        struct Record* grown=realloc(c.records,(c.count+1)*sizeof(*c.records));
        if( !grown ) fail("record allocation failed");
        c.records=grown; c.records[c.count++]=r;
    }
    fclose(f);
    if( !c.count || !completed ) fail("empty or incomplete capture");
    return c;
}
static void mismatch(size_t i, const char* what)
{ fprintf(stderr,"record %zu: %s mismatch\n",i,what); exit(1); }

static uint64_t run_corpus(struct Bench* b, const struct Corpus* c, int verify, int sort_only)
{
    struct ToriDraw_Scene* s=b->context.scene;
    uint64_t drawn=0;
    uint32_t pass=UINT32_MAX;
    for( size_t i=0;i<c->count;i++ )
    {
        const struct Record* r=&c->records[i];
        const struct ModelChainHeader* h=&r->h;
        if( pass!=h->pass )
        {
            if( b->context.in_pass ) GLES2DualCoreStage_EndPass(&b->context);
            GLES2DualCoreStage_BeginPass(&b->context,&h->begin);
            b->arena.result_count=0; b->arena.order_count=0; b->index_count=0;
            pass=h->pass;
        }
        b->context.pick_enabled=h->pick_enabled;
        b->context.pick_mouse_x=h->pick_x; b->context.pick_mouse_y=h->pick_y;
        int count=0;
        const int* order=NULL;
        if( sort_only )
        {
            if( h->cull!=TORIDRAW_CULL_VISIBLE || (h->pick_flags&MODEL_CHAIN_PICK_ONLY) ) continue;
            memcpy(s->screen_vertices_x,r->x,(size_t)h->vertices*4);
            memcpy(s->screen_vertices_y,r->y,(size_t)h->vertices*4);
            memcpy(s->screen_vertices_z,r->z,(size_t)h->vertices*4);
            memcpy(s->projected_box,h->projected_box,sizeof(h->projected_box));
            s->near_clipped=h->near_clipped;
            count=ToriDraw_RenderModel2SortFacesWithTable(r->command.model,s,b->context.kernel);
            order=ToriDraw_FaceOrder(s);
        }
        else
        {
            if( !GLES2DualCoreStage_ComputeModel(&b->context,&b->arena,&r->command) ) fail("stage exhausted");
            const struct GLES2DualCoreStageResult* result=&b->arena.results[b->arena.result_count-1];
            if( verify && result->cull!=h->cull ) mismatch(i,"cull");
            if( result->cull!=TORIDRAW_CULL_VISIBLE ) continue;
            if( verify && (result->pick_hit!=h->pick_hit || result->projected_depth!=h->projected_depth) )
                mismatch(i,"pick/depth");
            if( verify && (s->near_clipped!=h->near_clipped ||
                memcmp(s->screen_vertices_x,r->x,(size_t)h->vertices*4) ||
                memcmp(s->screen_vertices_y,r->y,(size_t)h->vertices*4) ||
                memcmp(s->screen_vertices_z,r->z,(size_t)h->vertices*4) ||
                memcmp(s->projected_box,h->projected_box,sizeof(h->projected_box))) )
                mismatch(i,"projection");
            count=result->sorted_face_count;
            order=b->arena.orders+result->order_offset;
        }
        if( verify && (count!=h->sorted_count || memcmp(order,r->order,(size_t)count*4)) )
            mismatch(i,"face order");
        if( count && !(h->options&MODEL_CHAIN_DYNAMIC) && h->element_id>=0 )
        {
            uint16_t* indices=b->indices+b->index_count;
            gles2_painter_write_indices(indices,0,(uint32_t)h->faces,order,(uint32_t)count,true);
            if( verify )
                for( int f=0;f<count;f++ )
                    for( int corner=0;corner<3;corner++ )
                        if( indices[f*3+corner]!=(uint16_t)(r->order[f]*3+corner) )
                            mismatch(i,"indices");
            b->index_count+=(uint32_t)count*3;
        }
        drawn+=count;
    }
    GLES2DualCoreStage_EndPass(&b->context);
    return drawn;
}
static void consume(struct Bench* b)
{
    uint64_t sum=0;
    for( unsigned i=0;i<b->index_count;i++ ) sum=sum*31+b->indices[i];
    sink=sum;
}

/* Producer-ahead protocol probe over actual replay results and commands.
 * This isolates acquire overhead; it does not simulate a thread schedule. */
static void run_acquires(struct Bench* b, int arm)
{
    struct GLES2DualCoreStageArena* a=&b->arena;
    uint32_t frontier=0;
    uint64_t sum=0;
    a->feed_acquired=0;
    a->cache_acquires=arm!=0;
    for( uint32_t i=0;i<a->result_count;i++ )
    {
        const struct GLES2DualCoreStageResult* r;
        if( arm ) r=GLES2DualCoreStageArena_TryAcquireResult(a,i,&frontier);
        else
        {
            if( atomic_load_explicit(&a->claims[i],memory_order_acquire)==GLES2_DUALCORE_CLAIM_CONSUMER )
                fail("unexpected consumer claim");
            if( i>=atomic_load_explicit(&a->ready,memory_order_relaxed) ) fail("missing result");
            atomic_thread_fence(memory_order_acquire);
            r=&a->results[i];
        }
        const struct ToriRS_RenderCommand* command;
        if( !r || GLES2DualCoreStageArena_FeedTake(a,i,&command)!=GLES2_DUALCORE_FEED_READY )
            fail("missing acquired payload");
        sum+=(uint32_t)r->sorted_face_count+(uint32_t)command->u.model.model.kind;
    }
    sink=sum;
}

static void run_publish(struct Bench* b, int arm)
{
    struct GLES2DualCoreStageArena* a=&b->arena;
    a->feed_count=a->feed_flushed=0;
    a->feed_batch_mask=arm?7:0;
    atomic_store_explicit(&a->feed_state,GLES2_DUALCORE_FEED_OPEN,memory_order_relaxed);
    atomic_store_explicit(&a->feed_published,0,memory_order_relaxed);
    for( uint32_t i=0;i<a->result_count;i++ )
    {
        struct ToriRS_RenderCommand* slot=GLES2DualCoreStageArena_FeedReserve(a);
        if( !slot ) fail("publish overflow");
        *slot=b->commands[i];
        GLES2DualCoreStageArena_FeedCommitBatched(a);
    }
    GLES2DualCoreStageArena_FeedClose(a);
    sink=atomic_load_explicit(&a->feed_published,memory_order_relaxed);
}

#if defined(MODEL_CHAIN_LIBRARY)
/* Shared-library entry points let one PMU process compare compiler targets.
 * Each library owns its scene, assets and kernel tables; -Bsymbolic prevents
 * interposition between the two copies of toridraw. */
struct LibraryBench { struct Corpus corpus; struct Bench bench; };
void* model_chain_open(const char* path)
{
    ToriDraw_Init();
    struct LibraryBench* state=calloc(1,sizeof(*state));
    if( !state ) fail("library state allocation failed");
    state->corpus=load_corpus(path);
    struct Corpus* c=&state->corpus;
    struct Bench* b=&state->bench;
    for( int tier=TORIDRAW_SCRATCH_BUFFER_LOW_2K;tier<TORIDRAW_SCRATCH_BUFFER_SIZE_COUNT;tier++ )
    {
        b->context.scene=ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL|TORIDRAW_SCENE_DEPTH_16K,tier);
        if( b->context.scene->max_faces==c->records[0].h.max_faces &&
            b->context.scene->max_vertices==c->records[0].h.max_vertices ) break;
        ToriDraw_SceneFree(b->context.scene); b->context.scene=NULL;
    }
    if( !b->context.scene ) fail("unsupported captured scratch capacity");
    b->context.kernel=ToriDraw_KernelGetGpu();
    GLES2DualCoreStageArena_Init(&b->arena);
    b->arena.result_capacity=(uint32_t)c->count;
    b->arena.results=calloc(c->count,sizeof(*b->arena.results));
    b->arena.order_capacity=(uint32_t)(c->faces?c->faces:1);
    b->arena.orders=malloc((size_t)b->arena.order_capacity*4);
    b->indices=malloc((size_t)b->arena.order_capacity*3*2);
    if( !b->arena.results || !b->arena.orders || !b->indices ) fail("scratch allocation failed");
    run_corpus(b,c,1,0);
    struct ToriDraw_Kernel bucket=*b->context.kernel;
    bucket.face_sort=ToriDraw_FaceCullSortKernelGetBucket();
    b->context.kernel=&bucket;
    run_corpus(b,c,1,0);
    b->context.kernel=ToriDraw_KernelGetGpu();
    return state;
}
uint64_t model_chain_calls(void* opaque)
{ return ((struct LibraryBench*)opaque)->corpus.count; }
uint64_t model_chain_run(void* opaque)
{
    struct LibraryBench* state=opaque;
    return run_corpus(&state->bench,&state->corpus,0,0);
}
uint64_t model_chain_checksum(void* opaque)
{
    consume(&((struct LibraryBench*)opaque)->bench);
    return sink;
}
void model_chain_close(void* opaque)
{
    struct LibraryBench* state=opaque;
    struct Corpus* c=&state->corpus;
    for( size_t i=0;i<c->count;i++ )
    { free(c->records[i].x); free(c->records[i].y); free(c->records[i].z); free(c->records[i].order); }
    for( size_t i=0;i<c->asset_count;i++ ) ToriDraw_ModelFree(c->assets[i].model);
    free(c->assets); free(c->records); free(state->bench.indices);
    GLES2DualCoreStageArena_Free(&state->bench.arena);
    ToriDraw_SceneFree(state->bench.context.scene);
    free(state);
}
#else
int main(int argc,char** argv)
{
    if( argc<2 || argc>5 )
        fail("usage: model_chain_replay CAPTURE [verify|cpu-cycles|instructions|branch-misses|L1-dcache-load-misses|sample] [chain|sort|acquire|publish|chain-ab] [repetitions]");
    const char* event=argc>2?argv[2]:"verify";
    int sort_only=argc>3 && !strcmp(argv[3],"sort");
    int chain_ab=argc>3 && !strcmp(argv[3],"chain-ab");
    int publish=argc>3 && !strcmp(argv[3],"publish");
    int acquire=argc>3 && (!strcmp(argv[3],"acquire") || publish);
    if( argc>3 && strcmp(argv[3],"sort") && strcmp(argv[3],"chain") && !acquire && !chain_ab ) fail("unknown chain mode");
    int reps=argc>4?atoi(argv[4]):0;
    if( reps<0 || reps>10000 ) fail("invalid repetition count");
    uint16_t endian=1;
    if( *(uint8_t*)&endian!=1 ) fail("capture requires little-endian host");
#if defined(__linux__)
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(0,&set);
    if( sched_setaffinity(0,sizeof(set),&set) ) { perror("pin CPU0"); return 1; }
    cpu_set_t actual; CPU_ZERO(&actual);
    if( sched_getaffinity(0,sizeof(actual),&actual) ) { perror("read CPU affinity"); return 1; }
    printf("placement: requested cpu0, effective cpu0=%d cpu1=%d\n",
        CPU_ISSET(0,&actual)!=0,CPU_ISSET(1,&actual)!=0);
#endif
    ToriDraw_Init();
    struct Corpus c=load_corpus(argv[1]);
    if( !reps )
    {
        uint64_t calls=sort_only?c.sort_calls:c.count;
        if( !calls ) fail("no calls in selected chain mode");
        reps=(int)((30000+calls-1)/calls);
    }
    struct Bench b={0};
    enum ToriDraw_ScratchBufferSize tier;
    for( tier=TORIDRAW_SCRATCH_BUFFER_LOW_2K;tier<TORIDRAW_SCRATCH_BUFFER_SIZE_COUNT;tier++ )
    {
        b.context.scene=ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL|TORIDRAW_SCENE_DEPTH_16K,tier);
        if( b.context.scene->max_faces==c.records[0].h.max_faces &&
            b.context.scene->max_vertices==c.records[0].h.max_vertices ) break;
        ToriDraw_SceneFree(b.context.scene); b.context.scene=NULL;
    }
    if( !b.context.scene ) fail("unsupported captured scratch capacity");
    b.context.kernel=ToriDraw_KernelGetGpu();
    GLES2DualCoreStageArena_Init(&b.arena);
    b.arena.result_capacity=(uint32_t)c.count;
    b.arena.results=calloc(c.count,sizeof(*b.arena.results));
    b.arena.order_capacity=(uint32_t)(c.faces?c.faces:1);
    b.arena.orders=malloc((size_t)b.arena.order_capacity*4);
    b.indices=malloc((size_t)b.arena.order_capacity*3*2);
    if( !b.arena.results || !b.arena.orders || !b.indices ) fail("scratch allocation failed");
    run_corpus(&b,&c,1,0);
    struct ToriDraw_Kernel bucket=*b.context.kernel;
    bucket.face_sort=ToriDraw_FaceCullSortKernelGetBucket();
    b.context.kernel=&bucket;
    run_corpus(&b,&c,1,0);
    b.context.kernel=ToriDraw_KernelGetGpu();
    if( sort_only ) run_corpus(&b,&c,1,1);
    if( chain_ab ) { ToriDraw_FaceSortSetCompact4(1); run_corpus(&b,&c,1,0); ToriDraw_FaceSortSetCompact4(0); }
    printf("verified: %zu chains, %" PRIu64 " passes, %zu distinct posed assets, %" PRIu64
           " visible, %" PRIu64 " faces, %" PRIu64 " clipped, %" PRIu64 " pick hits, %"
           PRIu64 " prioritized, %" PRIu64 " animated snapshots, %" PRIu64 " dynamic\n",
           c.count,c.passes,c.asset_count,c.visible,c.faces,c.clipped,c.picked,c.prioritized,c.animated,c.dynamic);
    fflush(stdout);
    if( acquire )
    {
        b.arena.claims=calloc(b.arena.result_count,sizeof(*b.arena.claims));
        b.arena.feed=calloc(b.arena.result_count,sizeof(*b.arena.feed));
        if( !b.arena.claims || !b.arena.feed ) fail("protocol allocation failed");
        b.arena.feed_capacity=b.arena.result_count;
        atomic_store(&b.arena.feed_state,GLES2_DUALCORE_FEED_OPEN);
        for( uint32_t i=0;i<b.arena.result_count;i++ )
        {
            atomic_init(&b.arena.claims[i],GLES2_DUALCORE_CLAIM_PRODUCER);
            struct ToriRS_RenderCommand command={0};
            command.kind=TORIRSRC_DRAW_MODEL;
            command.u.model=c.records[c.count-b.arena.result_count+i].command;
            if( !GLES2DualCoreStageArena_FeedPush(&b.arena,&command) ) fail("feed full");
        }
        GLES2DualCoreStageArena_FeedClose(&b.arena);
        b.commands=malloc(b.arena.result_count*sizeof(*b.commands));
        if( !b.commands ) fail("command allocation failed");
        memcpy(b.commands,b.arena.feed,b.arena.result_count*sizeof(*b.commands));
        if( argc<=4 || !atoi(argv[4]) ) reps=(int)((300000+b.arena.result_count-1)/b.arena.result_count);
        run_acquires(&b,0); uint64_t reference=sink;
        run_acquires(&b,1); if( sink!=reference ) fail("acquire checksum differs");
        if( publish ) for( int arm=0;arm<2;arm++ )
        {
            run_publish(&b,arm);
            if( sink!=b.arena.result_count || memcmp(b.commands,b.arena.feed,
                b.arena.result_count*sizeof(*b.commands)) ) fail("published payload differs");
        }
    }
    if( strcmp(event,"verify") && !acquire )
        for( int i=0;i<reps*10;i++ ) run_corpus(&b,&c,0,sort_only);
    if( !strcmp(event,"sample") )
    {
        for( int i=0;i<reps;i++ )
            if( publish ) run_publish(&b,1); else if( acquire ) run_acquires(&b,1);
            else run_corpus(&b,&c,0,sort_only);
        consume(&b);
    }
    else if( strcmp(event,"verify") )
    {
#if defined(__linux__)
        struct perf_event_attr attr={0};
        attr.size=sizeof(attr); attr.type=PERF_TYPE_HARDWARE;
        attr.disabled=1; attr.pinned=1; attr.exclude_kernel=1; attr.exclude_hv=1;
        attr.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
        if( !strcmp(event,"cpu-cycles") ) attr.config=PERF_COUNT_HW_CPU_CYCLES;
        else if( !strcmp(event,"instructions") ) attr.config=PERF_COUNT_HW_INSTRUCTIONS;
        else if( !strcmp(event,"branch-misses") ) attr.config=PERF_COUNT_HW_BRANCH_MISSES;
        else if( !strcmp(event,"L1-dcache-load-misses") )
        { attr.type=PERF_TYPE_HW_CACHE; attr.config=PERF_COUNT_HW_CACHE_RESULT_MISS<<16; }
        else fail("unsupported hardware event");
        int fd=(int)syscall(__NR_perf_event_open,&attr,0,-1,-1,0);
        if( fd<0 ) { perror("PMU unavailable (no fallback)"); return 1; }
        for( int sample=0;sample<((acquire || chain_ab)?12:9);sample++ )
        {
            int arm=(sample%4==1 || sample%4==2);
            if( chain_ab ) { ToriDraw_FaceSortSetCompact4(arm); run_corpus(&b,&c,0,0); }
            if( acquire ) for( int warm=0;warm<reps;warm++ )
                if( publish ) run_publish(&b,arm); else run_acquires(&b,arm);
            struct { uint64_t value,enabled,running; } value;
            if( ioctl(fd,PERF_EVENT_IOC_RESET,0) || ioctl(fd,PERF_EVENT_IOC_ENABLE,0) )
                fail("PMU enable failed");
            for( int rep=0;rep<reps;rep++ )
                if( publish ) run_publish(&b,arm); else if( acquire ) run_acquires(&b,arm); else run_corpus(&b,&c,0,sort_only);
            if( ioctl(fd,PERF_EVENT_IOC_DISABLE,0) ||
                read(fd,&value,sizeof(value))!=sizeof(value) ||
                !value.running || value.running!=value.enabled )
                fail("PMU unavailable/multiplexed (no estimate)");
            consume(&b);
            uint64_t calls=(acquire ? b.arena.result_count : sort_only ? c.sort_calls : c.count)*(uint64_t)reps;
            if( !calls ) fail("no calls in selected chain mode");
            printf("pmu,%s,%s,%d,%" PRIu64 ",%" PRIu64 ",%.3f",event,chain_ab?"chain-ab":publish?"publish":acquire?"acquire":sort_only?"sort":"chain",
                   sample+1,calls,value.value,(double)value.value/calls);
            if( acquire || chain_ab ) printf(",arm=%d",arm);
            putchar('\n');
        }
        close(fd);
#else
        fail("PMU measurement requires Linux/Android; this host supports verify only");
#endif
    }
    for( size_t i=0;i<c.count;i++ )
    { free(c.records[i].x); free(c.records[i].y); free(c.records[i].z); free(c.records[i].order); }
    for( size_t i=0;i<c.asset_count;i++ ) ToriDraw_ModelFree(c.assets[i].model);
    free(c.assets); free(c.records); free(b.indices); free(b.commands);
    GLES2DualCoreStageArena_Free(&b.arena);
    ToriDraw_SceneFree(b.context.scene);
    return 0;
}

#endif /* MODEL_CHAIN_LIBRARY */
