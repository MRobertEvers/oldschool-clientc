/* Compile-time-only Android diagnostic. Each thread counts its own renderer
 * work using one pinned hardware event. No software timing or multiplexing.
 * Include in dualcore.c after the lane definition. All arm changes occur
 * between joined frames. Worker state is read only after the join. */
#include "platform/platform_renderer_gles2_placement.h"
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <inttypes.h>

struct PipelinePmuThread { int fd; uint64_t last; };
static struct {
    bool initialized, enabled, measuring;
    const char *event, *target;
    unsigned warmup, frames, sample, block, arm;
    uint64_t draw_sum, worker_sum, models, worker_models, faces;
    struct PipelinePmuThread draw, worker;
} pipeline_pmu;

static void pipeline_pmu_error(const char* what)
{
    TORIRS_ERR("pipeline-pmu: %s: %s (no fallback)\n", what, strerror(errno));
    abort();
}
static void pipeline_pmu_begin_thread(struct PipelinePmuThread* thread)
{
    if( !pipeline_pmu.measuring ) return;
    if( !thread->fd )
    {
        struct perf_event_attr a = {0};
        a.size=sizeof(a); a.type=PERF_TYPE_HARDWARE;
        a.disabled=1; a.pinned=1; a.exclude_kernel=1; a.exclude_hv=1;
        a.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
        if( !strcmp(pipeline_pmu.event,"cpu-cycles") ) a.config=PERF_COUNT_HW_CPU_CYCLES;
        else if( !strcmp(pipeline_pmu.event,"instructions") ) a.config=PERF_COUNT_HW_INSTRUCTIONS;
        else if( !strcmp(pipeline_pmu.event,"branch-misses") ) a.config=PERF_COUNT_HW_BRANCH_MISSES;
        else if( !strcmp(pipeline_pmu.event,"L1-dcache-load-misses") )
        { a.type=PERF_TYPE_HW_CACHE; a.config=PERF_COUNT_HW_CACHE_RESULT_MISS<<16; }
        else pipeline_pmu_error("unknown event");
        thread->fd=(int)syscall(__NR_perf_event_open,&a,0,-1,-1,0);
        if( thread->fd<0 ) pipeline_pmu_error("open counter");
    }
    if( ioctl(thread->fd,PERF_EVENT_IOC_RESET,0) || ioctl(thread->fd,PERF_EVENT_IOC_ENABLE,0) )
        pipeline_pmu_error("enable counter");
}
static void pipeline_pmu_end_thread(struct PipelinePmuThread* thread)
{
    if( !pipeline_pmu.measuring ) return;
    struct { uint64_t value, enabled, running; } count;
    if( ioctl(thread->fd,PERF_EVENT_IOC_DISABLE,0) || read(thread->fd,&count,sizeof(count))!=sizeof(count)
        || !count.running || count.running!=count.enabled ) pipeline_pmu_error("counter unavailable/multiplexed");
    thread->last=count.value;
}
static void pipeline_pmu_frame_begin(struct ToriRS_GLES2* renderer)
{
    if( !pipeline_pmu.initialized )
    {
        pipeline_pmu.initialized=true;
        pipeline_pmu.event=getenv("TORIRS_PIPELINE_PMU");
        if( !pipeline_pmu.event ) return;
        pipeline_pmu.enabled=true;
        pipeline_pmu.target=getenv("TORIRS_PIPELINE_AB");
        if( !pipeline_pmu.target ) pipeline_pmu.target="both";
        pipeline_pmu.warmup=300;
        pipeline_pmu.frames=90;
        const char* v=getenv("TORIRS_PIPELINE_WARMUP");
        if( v ) pipeline_pmu.warmup=(unsigned)atoi(v);
        v=getenv("TORIRS_PIPELINE_FRAMES");
        if( v ) pipeline_pmu.frames=(unsigned)atoi(v);
        if( !pipeline_pmu.frames ) pipeline_pmu_error("zero frame window");
    }
    if( !pipeline_pmu.enabled || pipeline_pmu.sample>=12 ) return;
    pipeline_pmu.arm=(pipeline_pmu.sample%4==1 || pipeline_pmu.sample%4==2);
    GLES2DualCoreStage_SetDirectOrder((!strcmp(pipeline_pmu.target,"acquire") || !strcmp(pipeline_pmu.target,"feed")) ? 1 : (int)pipeline_pmu.arm);
    GLES2DualCoreStage_SetAcquireCache(!strcmp(pipeline_pmu.target,"direct") ? 0 : !strcmp(pipeline_pmu.target,"feed") ? 1 : (int)pipeline_pmu.arm);
    GLES2DualCoreStage_SetFeedBatch((!strcmp(pipeline_pmu.target,"feed") || !strcmp(pipeline_pmu.target,"all")) ? (int)pipeline_pmu.arm : 0);
    if( !strcmp(pipeline_pmu.target,"compact") )
    {
        GLES2DualCoreStage_SetDirectOrder(1);
        GLES2DualCoreStage_SetAcquireCache(1);
        GLES2DualCoreStage_SetFeedBatch(1);
        ToriDraw_FaceSortSetCompact4((int)pipeline_pmu.arm);
    }
    if( !strcmp(pipeline_pmu.target,"placement") )
    {
        GLES2DualCoreStage_SetDirectOrder(1);
        GLES2DualCoreStage_SetAcquireCache(1);
        GLES2DualCoreStage_SetFeedBatch(1);
        ToriDraw_FaceSortSetCompact4(1);
        bool enabled=pipeline_pmu.warmup || pipeline_pmu.arm;
        if( enabled && !renderer->static_primary_enabled )
        {
            renderer->static_primary_enabled=true;
            gles2_static_primary_rebuild(renderer);
        }
        renderer->static_primary_enabled=enabled;
    }
    if( !strcmp(pipeline_pmu.target,"pose") )
    {
        GLES2DualCoreStage_SetDirectOrder(1);
        GLES2DualCoreStage_SetAcquireCache(1);
        GLES2DualCoreStage_SetFeedBatch(1);
        ToriDraw_FaceSortSetCompact4(1);
        renderer->pose_reuse_enabled=pipeline_pmu.arm!=0;
    }
    if( !strcmp(pipeline_pmu.target,"bake") )
    {
        GLES2DualCoreStage_SetDirectOrder(1);
        GLES2DualCoreStage_SetAcquireCache(1);
        GLES2DualCoreStage_SetFeedBatch(1);
        ToriDraw_FaceSortSetCompact4(1);
        renderer->pose_reuse_enabled=true;
        renderer->actor_world_cache_enabled=pipeline_pmu.arm!=0;
    }
    if( !strcmp(pipeline_pmu.target,"complete") )
    {
        bool enabled=pipeline_pmu.warmup || pipeline_pmu.arm;
        GLES2DualCoreStage_SetDirectOrder(enabled);
        GLES2DualCoreStage_SetAcquireCache(enabled);
        GLES2DualCoreStage_SetFeedBatch(enabled);
        ToriDraw_FaceSortSetCompact4(enabled);
        if( enabled && !renderer->static_primary_enabled )
        {
            renderer->static_primary_enabled=true;
            gles2_static_primary_rebuild(renderer);
        }
        renderer->static_primary_enabled=enabled;
        renderer->pose_reuse_enabled=enabled;
        renderer->actor_world_cache_enabled=enabled;
        renderer->world_fast_shader=enabled;
    }
    pipeline_pmu.measuring=!pipeline_pmu.warmup && pipeline_pmu.block>=6;
    pipeline_pmu.worker.last=0;
    pipeline_pmu_begin_thread(&pipeline_pmu.draw);
}
static void pipeline_pmu_frame_end(struct ToriRS_GLES2DualCore* lane)
{
    if( !pipeline_pmu.enabled || pipeline_pmu.sample>=12 ) return;
    if( pipeline_pmu.warmup ) { pipeline_pmu.warmup--; return; }
    if( pipeline_pmu.measuring )
    {
        pipeline_pmu_end_thread(&pipeline_pmu.draw);
        pipeline_pmu.draw_sum+=pipeline_pmu.draw.last;
        pipeline_pmu.worker_sum+=pipeline_pmu.worker.last;
        pipeline_pmu.models+=lane->take_index;
        pipeline_pmu.faces+=lane->arena.order_count;
        for( uint32_t i=0;i<lane->arena.result_count;i++ )
            pipeline_pmu.worker_models+=!lane->arena.results[i].taken_by_draw;
    }
    if( ++pipeline_pmu.block < pipeline_pmu.frames+6 ) return;
    TORIRS_ERR("pipeline,%s,%s,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
        pipeline_pmu.event,pipeline_pmu.target,pipeline_pmu.sample,pipeline_pmu.arm,pipeline_pmu.frames,
        pipeline_pmu.draw_sum,pipeline_pmu.worker_sum,pipeline_pmu.models,pipeline_pmu.worker_models,pipeline_pmu.faces);
    pipeline_pmu.sample++; pipeline_pmu.block=0;
    pipeline_pmu.draw_sum=pipeline_pmu.worker_sum=pipeline_pmu.models=pipeline_pmu.worker_models=pipeline_pmu.faces=0;
    pipeline_pmu.measuring=false;
}
