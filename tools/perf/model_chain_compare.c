/* Hardware-only ABBA across two real model-pipeline shared libraries. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

struct Pipeline {
    void *library, *state;
    void* (*open)(const char*);
    uint64_t (*calls)(void*), (*run)(void*), (*checksum)(void*);
    void (*close)(void*);
};
static void fail(const char* message) { perror(message); exit(1); }
static void* symbol(void* library,const char* name)
{
    void* result=dlsym(library,name);
    if( !result ) { fprintf(stderr,"%s: %s\n",name,dlerror()); exit(1); }
    return result;
}
int main(int argc,char** argv)
{
    if( argc!=5 ) { fprintf(stderr,"usage: compare CORPUS LIB_A LIB_B EVENT\n"); return 1; }
    cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(0,&mask);
    if( sched_setaffinity(0,sizeof(mask),&mask) ) fail("CPU affinity");
    CPU_ZERO(&mask);
    if( sched_getaffinity(0,sizeof(mask),&mask) ) fail("CPU affinity readback");
    printf("placement: cpu0=%d cpu1=%d\n",CPU_ISSET(0,&mask)!=0,CPU_ISSET(1,&mask)!=0);
    struct Pipeline p[2]={0};
    for( int arm=0;arm<2;arm++ )
    {
        p[arm].library=dlopen(argv[2+arm],RTLD_NOW|RTLD_LOCAL);
        if( !p[arm].library ) { fprintf(stderr,"dlopen: %s\n",dlerror()); return 1; }
        p[arm].open=symbol(p[arm].library,"model_chain_open");
        p[arm].calls=symbol(p[arm].library,"model_chain_calls");
        p[arm].run=symbol(p[arm].library,"model_chain_run");
        p[arm].checksum=symbol(p[arm].library,"model_chain_checksum");
        p[arm].close=symbol(p[arm].library,"model_chain_close");
        p[arm].state=p[arm].open(argv[1]);
    }
    uint64_t calls=p[0].calls(p[0].state);
    if( !calls || calls!=p[1].calls(p[1].state) ) fail("different work counts");
    uint64_t faces[2],checksum[2];
    for( int arm=0;arm<2;arm++ )
    {
        faces[arm]=p[arm].run(p[arm].state);
        checksum[arm]=p[arm].checksum(p[arm].state);
    }
    if( faces[0]!=faces[1] || checksum[0]!=checksum[1] ) fail("different outputs");
    printf("verified: both libraries, %" PRIu64 " calls, %" PRIu64 " faces\n",calls,faces[0]);
    fflush(stdout);
    struct perf_event_attr attr={0};
    attr.size=sizeof(attr); attr.type=PERF_TYPE_HARDWARE;
    attr.disabled=1; attr.pinned=1; attr.exclude_kernel=1; attr.exclude_hv=1;
    attr.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
    const char* event=argv[4];
    if( !strcmp(event,"cpu-cycles") ) attr.config=PERF_COUNT_HW_CPU_CYCLES;
    else if( !strcmp(event,"instructions") ) attr.config=PERF_COUNT_HW_INSTRUCTIONS;
    else if( !strcmp(event,"branch-misses") ) attr.config=PERF_COUNT_HW_BRANCH_MISSES;
    else if( !strcmp(event,"L1-dcache-load-misses") )
    { attr.type=PERF_TYPE_HW_CACHE; attr.config=PERF_COUNT_HW_CACHE_RESULT_MISS<<16; }
    else fail("unsupported event");
    int fd=(int)syscall(__NR_perf_event_open,&attr,0,-1,-1,0);
    if( fd<0 ) fail("PMU unavailable; no fallback");
    unsigned reps=(unsigned)((30000+calls-1)/calls);
    for( unsigned sample=0;sample<12;sample++ )
    {
        unsigned arm=sample%4==1 || sample%4==2;
        struct Pipeline* selected=&p[arm];
        for( unsigned warm=0;warm<reps*3;warm++ ) selected->run(selected->state);
        if( ioctl(fd,PERF_EVENT_IOC_RESET,0) || ioctl(fd,PERF_EVENT_IOC_ENABLE,0) ) fail("enable PMU");
        for( unsigned rep=0;rep<reps;rep++ ) selected->run(selected->state);
        struct { uint64_t value,enabled,running; } value;
        if( ioctl(fd,PERF_EVENT_IOC_DISABLE,0) || read(fd,&value,sizeof(value))!=sizeof(value)
            || !value.running || value.enabled!=value.running ) fail("PMU unavailable/multiplexed");
        if( selected->checksum(selected->state)!=checksum[arm] ) fail("output changed");
        printf("pmu,%s,chain-compare,%u,%" PRIu64 ",%" PRIu64 ",%.3f,arm=%u\n",
            event,sample+1,calls*reps,value.value,(double)value.value/(calls*reps),arm);
    }
    close(fd);
    for( int arm=0;arm<2;arm++ ) { p[arm].close(p[arm].state); dlclose(p[arm].library); }
    return 0;
}
