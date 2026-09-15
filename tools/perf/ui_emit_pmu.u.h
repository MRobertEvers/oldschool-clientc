/* Android-only real UI update/emission A/B. No wall-clock profiling, sampling
 * fallback, worker totals or GL synchronization. IO is outside counted work. */
#include "ui/uitree_emit.h"
#include "log/torirs_log.h"
#include <stdlib.h>
#include <string.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <inttypes.h>
static struct {
    int initialized, enabled, fd, measuring;
    unsigned warmup, block, sample, arm, reused, commands;
    uint64_t total;
    const char* event;
} ui_emit_pmu;
static void ui_emit_pmu_fail(const char* what)
{
    TORIRS_REPORT("ui-emit-pmu FAILED: %s\n", what);
    abort();
}
static void ui_emit_pmu_begin(void)
{
    if( !ui_emit_pmu.initialized )
    {
        ui_emit_pmu.initialized=1;
        ui_emit_pmu.event=getenv("TORIRS_UI_EMIT_PMU");
        if( !ui_emit_pmu.event ) return;
        ui_emit_pmu.enabled=1; ui_emit_pmu.warmup=600;
        struct perf_event_attr a={0};
        a.size=sizeof(a); a.disabled=1; a.pinned=1; a.exclude_kernel=1; a.exclude_hv=1;
        a.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
        if( !strcmp(ui_emit_pmu.event,"cpu-cycles") ) {a.type=PERF_TYPE_HARDWARE; a.config=PERF_COUNT_HW_CPU_CYCLES;}
        else if( !strcmp(ui_emit_pmu.event,"instructions") ) {a.type=PERF_TYPE_HARDWARE; a.config=PERF_COUNT_HW_INSTRUCTIONS;}
        else if( !strcmp(ui_emit_pmu.event,"L1-dcache-load-misses") )
        {a.type=PERF_TYPE_HW_CACHE; a.config=PERF_COUNT_HW_CACHE_RESULT_MISS<<16;}
        else ui_emit_pmu_fail("unknown hardware event");
        ui_emit_pmu.fd=(int)syscall(__NR_perf_event_open,&a,0,-1,-1,0);
        if( ui_emit_pmu.fd<0 ) ui_emit_pmu_fail("hardware counter unavailable");
    }
    ui_emit_pmu.measuring=0;
    if( !ui_emit_pmu.enabled || ui_emit_pmu.sample>=12 ) return;
    ui_emit_pmu.arm=ui_emit_pmu.sample%4==1 || ui_emit_pmu.sample%4==2;
    UITree_EmitSetOverlayRetain((int)ui_emit_pmu.arm);
    ui_emit_pmu.measuring=!ui_emit_pmu.warmup && ui_emit_pmu.block>=6;
    if( ui_emit_pmu.measuring &&
        (ioctl(ui_emit_pmu.fd,PERF_EVENT_IOC_RESET,0) || ioctl(ui_emit_pmu.fd,PERF_EVENT_IOC_ENABLE,0)) )
        ui_emit_pmu_fail("enable counter");
}
static void ui_emit_pmu_end(int reused, unsigned commands)
{
    if( !ui_emit_pmu.enabled || ui_emit_pmu.sample>=12 ) return;
    if( ui_emit_pmu.warmup ) {--ui_emit_pmu.warmup; return;}
    if( ui_emit_pmu.measuring )
    {
        struct {uint64_t value,enabled,running;} v;
        if( ioctl(ui_emit_pmu.fd,PERF_EVENT_IOC_DISABLE,0) || read(ui_emit_pmu.fd,&v,sizeof(v))!=sizeof(v) ||
            !v.running || v.enabled!=v.running ) ui_emit_pmu_fail("counter missing or multiplexed");
        ui_emit_pmu.total+=v.value; ui_emit_pmu.reused+=reused!=0; ui_emit_pmu.commands+=commands;
    }
    if( ++ui_emit_pmu.block<186 ) return;
    TORIRS_REPORT("ui-emit-pmu,%s,%u,%u,180,%" PRIu64 ",%u,%u\n",ui_emit_pmu.event,
        ui_emit_pmu.sample,ui_emit_pmu.arm,ui_emit_pmu.total,ui_emit_pmu.reused,ui_emit_pmu.commands);
    ui_emit_pmu.block=ui_emit_pmu.reused=ui_emit_pmu.commands=0; ui_emit_pmu.total=0;
    if( ++ui_emit_pmu.sample==12 ) {close(ui_emit_pmu.fd); TORIRS_REPORT("ui-emit-pmu complete\n");}
}
