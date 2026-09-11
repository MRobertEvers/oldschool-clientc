/* Device-wide SP counters, fixed rendered-frame windows, no time estimate. */
#include "kgsl_counters.h"
#include <inttypes.h>
static struct { int fd;bool probed,enabled;unsigned warm,frames,sample;uint64_t start_cycles,start_alu; } gpu_probe;
static void gpu_counter_frame_begin(void)
{
    if(!gpu_probe.probed){gpu_probe.probed=true;gpu_probe.fd=-1;gpu_probe.enabled=getenv("TORIRS_GPU_COUNTERS")!=NULL;gpu_probe.warm=300;}
    if(!gpu_probe.enabled||gpu_probe.sample>=4||gpu_probe.warm)return;
    if(gpu_probe.frames==0){
        if(gpu_probe.fd<0){gpu_probe.fd=krait_kgsl_open();if(gpu_probe.fd<0){fprintf(stderr,"GPU counters unavailable; no fallback\n");abort();}}
        glFinish();
        if(krait_kgsl_read(gpu_probe.fd,&gpu_probe.start_cycles,&gpu_probe.start_alu)){fprintf(stderr,"GPU counter read failed\n");abort();}
    }
}
static void gpu_counter_frame_end(void)
{
    if(!gpu_probe.enabled||gpu_probe.sample>=4)return;
    if(gpu_probe.warm){gpu_probe.warm--;return;}
    if(++gpu_probe.frames<120)return;
    uint64_t cycles,alu;glFinish();
    if(krait_kgsl_read(gpu_probe.fd,&cycles,&alu)||cycles<gpu_probe.start_cycles||alu<gpu_probe.start_alu){fprintf(stderr,"GPU counter reset/read failure; no estimate\n");abort();}
    fprintf(stderr,"gpu-counters,%u,%u,%" PRIu64 ",%" PRIu64 "\n",gpu_probe.sample,gpu_probe.frames,cycles-gpu_probe.start_cycles,alu-gpu_probe.start_alu);
    gpu_probe.frames=0;
    if(++gpu_probe.sample==4){close(gpu_probe.fd);gpu_probe.fd=-1;}
}
