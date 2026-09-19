/* Replay captured retained-pose lookup chains, with actual TRSPK metadata
 * types and resolver code. No geometry fabrication, GL, or timed profile. */
#define _GNU_SOURCE
#include "3rd/trspk/trspk_unity.c"
#include "placement_chain_format.h"
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
static volatile uint64_t placement_sink;
static void
fail(const char* text)
{
    fprintf(stderr, "placement replay: %s\n", text);
    exit(1);
}
static void
read_exact(
    FILE* f,
    void* p,
    size_t n)
{
    if( n && fread(p, 1, n, f) != n )
        fail("truncated corpus");
}
static void*
allocate(size_t n)
{
    void* p = calloc(1, n ? n : 1);
    if( !p )
        fail("allocation failed");
    return p;
}
static uint32_t
word(FILE* f)
{
    uint32_t v;
    read_exact(f, &v, 4);
    return v;
}
struct Replay
{
    struct ToriRS_GLES2* renderer;
    struct PlacementChainCall* calls;
    struct GLES2StaticPrimary* output;
    uint32_t count, queries;
};
static struct Replay
load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if( !f )
        fail("cannot open corpus");
    struct PlacementChainHeader h;
    read_exact(f, &h, sizeof(h));
    if( h.magic != PLACEMENT_CHAIN_MAGIC || (h.version != 1 && h.version != 2) ||
        h.elements > 1048576 || h.batches > 65536 || h.pages > 65536 )
        fail("invalid header");
    struct Replay replay = { 0 };
    struct ToriRS_GLES2* r = allocate(sizeof(*r));
    replay.renderer = r;
    r->has_3d = true;
    r->batch_poses.element_count = r->batch_poses.element_cap = h.elements;
    r->batch_poses.elements = allocate((size_t)h.elements * sizeof(*r->batch_poses.elements));
    for( uint32_t e = 0; e < h.elements; e++ )
        for( unsigned a = 0; a < TRSPK_POSE_TRACK_COUNT; a++ )
        {
            struct TRSPK_PoseTrack* t = &r->batch_poses.elements[e].tracks[a];
            t->pose_count = word(f);
            t->pose_cap = word(f);
            if( t->pose_count > t->pose_cap || t->pose_cap > 1048576 )
                fail("invalid pose track");
            t->vertex_base = allocate((size_t)t->pose_cap * 4);
            read_exact(f, t->vertex_base, (size_t)t->pose_count * 4);
        }
    r->static_batch_count = h.batches;
    r->static_batches = allocate((size_t)h.batches * sizeof(*r->static_batches));
    for( uint32_t i = 0; i < h.batches; i++ )
    {
        struct PlacementChainBatch bh;
        read_exact(f, &bh, sizeof(bh));
        if( bh.active > 1 || bh.has_cpu > 1 || bh.entries > 1048576 || bh.page_capacity > 65536 ||
            (!bh.has_cpu && bh.entries) )
            fail("invalid batch");
        struct GLES2StaticBatch* b = &r->static_batches[i];
        b->active = bh.active;
        if( bh.has_cpu )
        {
            b->cpu = trspk_batch16_create(TRSPK_VERTEX_FORMAT_GLES2);
            if( !b->cpu )
                fail("batch allocation failed");
            free(b->cpu->entries);
            b->cpu->entries = allocate((size_t)bh.entries * sizeof(*b->cpu->entries));
            b->cpu->entry_count = b->cpu->entry_capacity = bh.entries;
            read_exact(f, b->cpu->entries, (size_t)bh.entries * sizeof(*b->cpu->entries));
        }
        b->page_id_capacity = bh.page_capacity;
        b->page_ids = allocate((size_t)bh.page_capacity * 4);
        read_exact(f, b->page_ids, (size_t)bh.page_capacity * 4);
    }
    r->static_page_count = h.pages;
    r->static_pages = allocate((size_t)h.pages * sizeof(*r->static_pages));
    for( uint32_t i = 0; i < h.pages; i++ )
    {
        struct PlacementChainPage page;
        read_exact(f, &page, sizeof(page));
        if( page.valid > 1 )
            fail("invalid page");
        r->static_pages[i].valid = page.valid;
        r->static_pages[i].gpu_offset = page.offset;
    }
    for( ;; )
    {
        uint32_t tag = word(f);
        if( tag == PLACEMENT_CHAIN_END )
        {
            if( word(f) != replay.count || fgetc(f) != EOF )
                fail("invalid footer");
            break;
        }
        if( (tag != PLACEMENT_CHAIN_CALL && (tag != PLACEMENT_CHAIN_PREFETCH || h.version < 2)) ||
            replay.count >= 1048576 )
            fail("invalid query tag");
        struct PlacementChainCall call = { .magic = tag };
        read_exact(f, (char*)&call + 4, sizeof(call) - 4);
        if( call.result < -1 || call.result > 1 )
            fail("invalid query result");
        void* grown = realloc(replay.calls, (size_t)(replay.count + 1) * sizeof(call));
        if( !grown )
            fail("query allocation failed");
        replay.calls = grown;
        replay.calls[replay.count++] = call;
        replay.queries += (tag == PLACEMENT_CHAIN_CALL);
    }
    fclose(f);
    if( !replay.count || !replay.queries )
        fail("empty corpus");
    replay.output = allocate((size_t)replay.count * sizeof(*replay.output));
    r->static_primary_enabled = true;
    gles2_static_primary_rebuild(r);
    printf(
        "graph: %u elements, %u batches, %u pages; descriptor bytes=%zu\n",
        h.elements,
        h.batches,
        h.pages,
        (size_t)r->static_primary_capacity * sizeof(*r->static_primary));
    return replay;
}
__attribute__((noinline)) static void
run(struct Replay* replay,
    int verify)
{
    __asm__ volatile("" ::: "memory");
    uint64_t sum = 0;
    for( uint32_t i = 0; i < replay->count; i++ )
    {
        const struct PlacementChainCall* c = &replay->calls[i];
        if( c->magic == PLACEMENT_CHAIN_PREFETCH )
        {
            gles2_static_prefetch_ids(replay->renderer, c->element_id, c->anim_index, c->pose_id);
            continue;
        }
        struct GLES2StaticPrimary* p = &replay->output[i];
        int result =
            gles2_static_resolve(replay->renderer, c->element_id, c->anim_index, c->pose_id, p);
        if( verify &&
            (result != c->result || (result == 1 && memcmp(p, &c->placement, sizeof(*p)))) )
        {
            fprintf(stderr, "query %u mismatch\n", i);
            exit(1);
        }
        sum += (uint32_t)result;
        if( result == 1 )
            sum += p->vertex_base + p->vertex_count + p->page_id;
    }
    placement_sink = sum;
}
int
main(
    int argc,
    char** argv)
{
    if( argc != 3 )
        fail("usage: placement_chain_replay CORPUS EVENT|verify");
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    if( sched_setaffinity(0, sizeof(mask), &mask) )
        fail("CPU affinity");
    struct Replay replay = load(argv[1]);
    for( int arm = 0; arm < 2; arm++ )
    {
        replay.renderer->static_primary_enabled = arm;
        run(&replay, 1);
    }
    printf(
        "verified: %u real placement queries, %u ordered records, both arms\n",
        replay.queries,
        replay.count);
    fflush(stdout);
    if( !strcmp(argv[2], "verify") )
        return 0;
    struct perf_event_attr attr = { 0 };
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_HARDWARE;
    attr.disabled = 1;
    attr.pinned = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    if( !strcmp(argv[2], "cpu-cycles") )
        attr.config = PERF_COUNT_HW_CPU_CYCLES;
    else if( !strcmp(argv[2], "instructions") )
        attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    else if( !strcmp(argv[2], "branch-misses") )
        attr.config = PERF_COUNT_HW_BRANCH_MISSES;
    else if( !strcmp(argv[2], "L1-dcache-load-misses") )
    {
        attr.type = PERF_TYPE_HW_CACHE;
        attr.config = PERF_COUNT_HW_CACHE_RESULT_MISS << 16;
    }
    else
        fail("unknown event");
    int fd = (int)syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if( fd < 0 )
    {
        perror("PMU unavailable; no fallback");
        return 1;
    }
    uint32_t reps = (300000 + replay.queries - 1) / replay.queries;
    for( int sample = 0; sample < 12; sample++ )
    {
        int arm = sample % 4 == 1 || sample % 4 == 2;
        replay.renderer->static_primary_enabled = arm;
        for( unsigned i = 0; i < reps; i++ )
            run(&replay, 0);
        if( ioctl(fd, PERF_EVENT_IOC_RESET, 0) || ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) )
            fail("counter enable");
        for( unsigned i = 0; i < reps; i++ )
            run(&replay, 0);
        struct
        {
            uint64_t value, enabled, running;
        } count;
        if( ioctl(fd, PERF_EVENT_IOC_DISABLE, 0) ||
            read(fd, &count, sizeof(count)) != sizeof(count) || !count.running ||
            count.running != count.enabled )
            fail("counter unavailable/multiplexed");
        uint64_t queries = (uint64_t)reps * replay.queries;
        printf(
            "pmu,%s,placement,%d,%" PRIu64 ",%" PRIu64 ",%.3f,arm=%d\n",
            argv[2],
            sample + 1,
            queries,
            count.value,
            (double)count.value / queries,
            arm);
    }
    close(fd);
    return 0;
}
