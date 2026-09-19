#include "ui/uitree_canvas_measure.h"
#include "canvas_chain_format.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
static volatile unsigned sink;
static void fail(const char* message) {fprintf(stderr,"canvas replay: %s\n",message);exit(1);}
static void read_exact(FILE* f,void* p,size_t bytes) {if(fread(p,1,bytes,f)!=bytes)fail("incomplete capture");}
static __attribute__((noinline)) void query(struct UITree* t,int w,int h,int arm)
{
    __asm__ volatile("":::"memory");
    struct UITreeCanvasWidths r=arm ? UITree_CanvasMeasureCompact(t,w,h) : UITree_CanvasMeasureReference(t,w,h);
    sink+=(unsigned)(r.strip+r.core);
}
int main(int argc,char** argv)
{
    if(argc<2)fail("usage: canvas_chain_replay capture [cpu-cycles|instructions]");
    FILE* input=fopen(argv[1],"rb");if(!input)fail("open capture");
    int measure=argc>2 && strcmp(argv[2],"verify");
    int fd=-1;
#if defined(__linux__)
    if(measure) {
        struct perf_event_attr a={0};a.size=sizeof(a);a.type=PERF_TYPE_HARDWARE;
        a.config=!strcmp(argv[2],"cpu-cycles") ? PERF_COUNT_HW_CPU_CYCLES : PERF_COUNT_HW_INSTRUCTIONS;
        a.disabled=1;a.pinned=1;a.exclude_kernel=1;a.exclude_hv=1;
        a.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
        fd=syscall(__NR_perf_event_open,&a,0,-1,-1,0);if(fd<0)fail("hardware counter unavailable");
    }
#else
    if(measure)fail("hardware measurements require device");
#endif
    uint64_t totals[2]={0,0};unsigned calls[2]={0,0},records=0;
    struct UITree tree={0};
    while(1) {
        int byte=fgetc(input);if(byte==EOF)break;ungetc(byte,input);
        struct CanvasChainHeader h;read_exact(input,&h,sizeof(h));
        if(h.magic!=CANVAS_CHAIN_MAGIC || h.nodes>100000 || !h.width || !h.height)fail("invalid header");
        struct UITreeComponent* c=realloc(tree.components,(size_t)h.nodes*sizeof(*c));
        if(h.nodes && !c)fail("allocation");tree.components=c;tree.component_count=h.nodes;
        tree.generation=h.generation;tree.canvas_candidates_valid=0;
        memset(c,0,(size_t)h.nodes*sizeof(*c));
        for(uint32_t i=0;i<h.nodes;i++) {
            struct CanvasChainNode n;read_exact(input,&n,sizeof(n));
            if(n.parent>=0 && (uint32_t)n.parent>=h.nodes)fail("invalid parent");
            c[i].parent=n.parent;c[i].freed=n.freed;c[i].behavior.hide=n.hide;
            c[i].frame_hidden=n.frame_hidden;c[i].replacement_hidden=n.replacement_hidden;
            c[i].position.width_mode=n.width_mode;c[i].position.height_mode=n.height_mode;
            c[i].position.x_mode=n.x_mode;c[i].position.abs_x=n.x;c[i].position.abs_w=n.w;c[i].position.abs_h=n.h;
        }
        for(int arm=0;arm<2;arm++) {
            struct UITreeCanvasWidths r=arm ? UITree_CanvasMeasureCompact(&tree,h.width,h.height) : UITree_CanvasMeasureReference(&tree,h.width,h.height);
            if(r.strip!=h.strip || r.core!=h.core)fail("width mismatch");
        }
        /* Narrow miss-path query replay. Snapshot loading and candidate
         * construction are excluded here; app A/B includes their real cost. */
        for(int j=0;j<16;j++)query(&tree,h.width,h.height,j&1);
#if defined(__linux__)
        if(measure)for(int window=0;window<12;window++) {
            int arm=window%4==1 || window%4==2;
            if(ioctl(fd,PERF_EVENT_IOC_RESET,0) || ioctl(fd,PERF_EVENT_IOC_ENABLE,0))fail("enable counter");
            for(int j=0;j<64;j++)query(&tree,h.width,h.height,arm);
            struct {uint64_t value,enabled,running;} v;
            if(ioctl(fd,PERF_EVENT_IOC_DISABLE,0) || read(fd,&v,sizeof(v))!=sizeof(v) || !v.running || v.running!=v.enabled)fail("counter missing or multiplexed");
            totals[arm]+=v.value;calls[arm]+=64;
        }
#endif
        records++;
    }
    if(records!=24)fail("expected 24 complete snapshots");
    printf("{\"verified_snapshots\":%u,\"before_count\":%"PRIu64",\"after_count\":%"PRIu64",\"queries_per_arm\":%u}\n",records,totals[0],totals[1],calls[0]);
    free(tree.components);free(tree.canvas_candidate_ids);fclose(input);
#if defined(__linux__)
    if(fd>=0)close(fd);
#endif
    return 0;
}
