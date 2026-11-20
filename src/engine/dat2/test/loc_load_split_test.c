/*
 * The loc config load (engine/dat2/task_dat2_loc_load.c) on the time-sliced
 * background queue, against the real cache.osrs242.
 *
 * A world build fans its loc loads out onto the background queue, and the
 * first of them to land splits the loc config group (config group 6, 56,000
 * members) into its records. That split was one call: in the UBSan editor
 * session 'background task Dat2LocLoad ran 5.71 ms between two slice checks
 * (... limit 4.00 ms)'. It is now made a bounded number of entries at a time
 * (RSCache_FileListSplit, Dat2GroupCache_SplitStep) with the slice asked in
 * between, and shared by every load waiting for the group.
 *
 *   - the split, a few entries at a time, is the per-member decode byte for
 *     byte, for every member of the group;
 *   - a world build's worth of loc loads on a sliced background queue: the
 *     split spans frames, is made once, every loc is resident, and the
 *     resident group is the per-member decode byte for byte;
 *   - the longest background step (thread CPU time, every slice already over,
 *     so a step is the one unit a slice's guaranteed turn lets through) is a
 *     fraction of the uncut split in the same build.
 *
 *   make -C src test-loc-load-split   (run from src/, the cache at ..)
 */
#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_cache.h"
#include "engine/dat2/dat2_tasks.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* OSRS keeps every loc in config group 6. */
#define LOC_GROUP 6
/* Locs a test build asks for, spread over the group. */
#define LOC_LOADS 48
/* The longest background step, as a fraction of the uncut split in the same
 * build: the split is ~14 steps of DAT2_GROUP_SPLIT_ENTRIES, and a guaranteed
 * turn runs at most two of them. */
#define LONGEST_STEP_DIVISOR 4

/* A clock that moves 1 ms each time it is read: every slice check finds the
 * slice over, so the split is spread over as many frames as it has steps. */
static uint64_t g_now_us = 1000000;

static uint64_t
fake_now(void* data)
{
    (void)data;
    g_now_us += 1000;
    return g_now_us;
}

/* This thread's processor time, us. */
static uint64_t
cpu_now(void* data)
{
    struct timespec ts;

    (void)data;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

struct Lane
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct PlatformX_IO* px;
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;
    struct TaskRunner background;
};

static int
lane_open(
    struct Lane* lane,
    char const* root,
    uint64_t (*now)(void*))
{
    char path[1024];

    memset(lane, 0, sizeof(*lane));
    snprintf(path, sizeof(path), "%s/cache.osrs242", root);
    lane->disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !lane->disk )
    {
        printf("   SKIP %s: not present\n", path);
        return 0;
    }
    if( !RSCache_ProfileByName("osrs242", &lane->profile) )
        abort();
    RSCache_Dat2DiskSetProfile(lane->disk, &lane->profile);
    lane->px = PlatformX_IO_New();
    assert(lane->px);
    PlatformX_IO_InitDat2Disk(lane->px, lane->disk);
    lane->background.queue = ToriRS_TaskQueue_New();
    lane->background.io = ToriRS_IOBatch_New();
    lane->background.px = lane->px;
    lane->background.role = TASK_RUNNER_ROLE_BACKGROUND;
    lane->background.parallel = 1;
    TaskRunner_SetClock(&lane->background, now, NULL);
    /* Measures every step (longest_step_us) without naming any. */
    lane->background.slow_task_us = UINT64_MAX / 2;
    lane->bc = dat2_buildcache_new();
    lane->provider = dat2_buildcache_as_provider(lane->bc);
    CacheProvider_SetProfile(lane->provider, &lane->profile);
    return 1;
}

static void
lane_close(struct Lane* lane)
{
    dat2_buildcache_free(lane->bc);
    ToriRS_TaskQueue_Free(lane->background.queue);
    ToriRS_IOBatch_Free(lane->background.io);
    PlatformX_IO_Free(lane->px);
    RSCache_Dat2DiskFree(lane->disk);
}

static struct RSCache_Dat2DiskArchive*
loc_archive(struct Lane* lane)
{
    int const table = RSCache_Dat2DiskTableId(lane->disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(lane->disk, table, LOC_GROUP);

    assert(archive);
    /* Its member count and ids, as the IO layer gives them (platform_x_io.c). */
    if( !RSCache_Dat2DiskArchiveInitMetadataFromTable(RSCache_Dat2DiskReferenceTable(lane->disk, table), archive) )
        abort();
    assert(archive->file_count > 1);
    return archive;
}

/* Every member of `got` is `want`'s, byte for byte. */
static int
same_members(
    struct RSCache_FileList const* want,
    struct RSCache_FileList const* got)
{
    if( want->file_count != got->file_count )
        return 0;
    for( int i = 0; i < want->file_count; i++ )
        if( want->file_sizes[i] != got->file_sizes[i] ||
            memcmp(want->files[i], got->files[i], (size_t)want->file_sizes[i]) != 0 )
            return 0;
    return 1;
}

static void
test_the_split_in_steps_is_the_per_member_decode(char const* root)
{
    struct Lane lane;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* reference;
    int const step_sizes[] = { 1, 7, 4096, DAT2_GROUP_SPLIT_ENTRIES };

    printf("TEST: the loc group split a few entries at a time is the per-member decode\n");
    if( !lane_open(&lane, root, fake_now) )
        return;
    archive = loc_archive(&lane);
    reference = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    assert(reference);
    for( size_t k = 0; k < sizeof(step_sizes) / sizeof(step_sizes[0]); k++ )
    {
        struct RSCache_FileListSplit split;
        struct RSCache_FileList* list;
        char* blob = NULL;
        int steps = 0;
        int done;

        CHECK(RSCache_FileListSplitBegin(&split, archive->data, archive->data_size, archive->file_count) == 0,
            "the loc group would not begin");
        while( (done = RSCache_FileListSplitStep(&split, step_sizes[k])) == 0 )
            steps++;
        CHECK(done == 1, "the split failed at %d entries a step", step_sizes[k]);
        list = RSCache_FileListSplitTake(&split, &blob);
        CHECK(same_members(reference, list), "%d entries a step: the members differ", step_sizes[k]);
        printf("   %d entries a step: %d steps, %d members identical\n", step_sizes[k], steps + 1,
            list->file_count);
        RSCache_FileListFreeShared(list, blob);
    }
    /* And a truncated group is refused, not read past. */
    {
        struct RSCache_FileListSplit split;
        int done = 0;

        if( RSCache_FileListSplitBegin(&split, archive->data, 64, archive->file_count) == 0 )
            while( (done = RSCache_FileListSplitStep(&split, 4096)) == 0 )
                ;
        else
            done = -1;
        CHECK(done == -1, "a 64-byte loc group split");
        RSCache_FileListSplitAbandon(&split);
    }
    RSCache_FileListFree(reference);
    RSCache_Dat2DiskArchiveFree(archive);
    lane_close(&lane);
}

/* LOC_LOADS member ids, spread over the group. */
static void
loc_ids(
    struct RSCache_Dat2DiskArchive const* archive,
    int* ids)
{
    for( int i = 0; i < LOC_LOADS; i++ )
    {
        int const pos = (int)((long)i * (archive->file_count - 1) / (LOC_LOADS - 1));
        ids[i] = archive->file_ids ? archive->file_ids[pos] : pos;
    }
}

/* Add `count` loads to the lane's background queue and give it 2 ms slices
 * of the fake clock until it is empty; how many frames ended with the split
 * still in flight. */
static int
run_loads(
    struct Lane* lane,
    int const* ids,
    int count,
    int* out_frames)
{
    int frames = 0;
    int splitting_frames = 0;

    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_Task* task = CreateTask_Dat2LocLoad(lane->provider, ids[i]);

        assert(task);
        ToriRS_TaskQueue_Add(lane->background.queue, task);
    }
    while( lane->background.queue->head && frames < 100000 )
    {
        (void)TaskRunner_RunUntil(&lane->background, g_now_us + 2000);
        if( Dat2GroupCache_Splitting(lane->bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS, LOC_GROUP) )
            splitting_frames++;
        frames++;
    }
    CHECK(!lane->background.queue->head, "work left after %d frames", frames);
    *out_frames = frames;
    return splitting_frames;
}

static void
test_a_world_builds_loc_loads_split_the_group_in_slices(char const* root)
{
    struct Lane lane;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* reference;
    struct Dat2Group const* resident;
    int ids[LOC_LOADS];
    int frames = 0;
    int splitting_frames;
    int resident_count = 0;

    printf("TEST: loc loads on a sliced background queue split the group over frames, once\n");
    if( !lane_open(&lane, root, fake_now) )
        return;
    archive = loc_archive(&lane);
    reference = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    assert(reference);
    loc_ids(archive, ids);

    /* One load alone: a slice already over gives it one guaranteed turn a
     * frame, so the split is spread over frames. */
    splitting_frames = run_loads(&lane, ids, 1, &frames);
    CHECK(splitting_frames > 1, "one load: the split was in flight for %d frame(s): not sliced",
        splitting_frames);
    CHECK(CacheProvider_LocationHas(lane.provider, ids[0]), "the first loc is not resident");
    printf("   one load: %d frame(s), the split in flight across %d\n", frames, splitting_frames);

    /* A world build's worth after it, and a group dropped (a live reload's
     * Evict) to be split again by all of them together: every task steps the
     * one split, which lands once. */
    Dat2GroupCache_Evict(lane.bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS, LOC_GROUP);
    splitting_frames = run_loads(&lane, ids + 1, LOC_LOADS - 1, &frames);
    for( int i = 0; i < LOC_LOADS; i++ )
        resident_count += CacheProvider_LocationHas(lane.provider, ids[i]) ? 1 : 0;
    CHECK(resident_count == LOC_LOADS, "%d of %d locs resident", resident_count, LOC_LOADS);
    CHECK(!Dat2GroupCache_Splitting(lane.bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS, LOC_GROUP),
        "a split is still in flight");
    resident = Dat2GroupCache_Get(lane.bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS, LOC_GROUP);
    CHECK(resident != NULL, "the loc group is not resident");
    if( resident )
        CHECK(same_members(reference, resident->filelist),
            "the resident loc group is not the per-member decode");
    printf("   %d more loads: %d frame(s), the split in flight across %d; %d locs resident; group "
           "identical\n",
        LOC_LOADS - 1, frames, splitting_frames, resident_count);
    RSCache_FileListFree(reference);
    RSCache_Dat2DiskArchiveFree(archive);
    lane_close(&lane);
}

static void
test_the_longest_step(char const* root)
{
    struct Lane lane;
    struct RSCache_Dat2DiskArchive* archive;
    int ids[LOC_LOADS];
    int frames = 0;
    uint64_t whole_us = UINT64_MAX;

    printf("TEST: the longest background step of a world build's loc loads (thread CPU time)\n");
    if( !lane_open(&lane, root, cpu_now) )
        return;
    archive = loc_archive(&lane);
    /* What the uncut split costs in this build: the least of three. */
    for( int i = 0; i < 3; i++ )
    {
        char* blob = NULL;
        uint64_t const start = cpu_now(NULL);
        struct RSCache_FileList* list =
            RSCache_FileListNewFromDecodeShared(archive->data, archive->data_size, archive->file_count, &blob);
        uint64_t const spent = cpu_now(NULL) - start;

        assert(list);
        RSCache_FileListFreeShared(list, blob);
        if( spent < whole_us )
            whole_us = spent;
    }
    loc_ids(archive, ids);
    for( int i = 0; i < LOC_LOADS; i++ )
        ToriRS_TaskQueue_Add(lane.background.queue, CreateTask_Dat2LocLoad(lane.provider, ids[i]));
    /* Every slice already over when given: a step is a guaranteed turn. */
    while( lane.background.queue->head && frames < 1000000 )
    {
        (void)TaskRunner_RunUntil(&lane.background, cpu_now(NULL));
        frames++;
    }
    CHECK(!lane.background.queue->head, "work left after %d frames", frames);
    printf("LONGEST STEP: %.3f ms (%s), %d frames; UNCUT SPLIT %.3f ms in this build, bound %.3f ms\n",
        (double)lane.background.longest_step_us / 1000.0, lane.background.longest_step_name, frames,
        (double)whole_us / 1000.0, (double)(whole_us / LONGEST_STEP_DIVISOR) / 1000.0);
    CHECK(lane.background.longest_step_us < whole_us / LONGEST_STEP_DIVISOR,
        "a background step took %.3f ms (bound %.3f ms, the uncut split / %d)",
        (double)lane.background.longest_step_us / 1000.0,
        (double)(whole_us / LONGEST_STEP_DIVISOR) / 1000.0, LONGEST_STEP_DIVISOR);
    RSCache_Dat2DiskArchiveFree(archive);
    lane_close(&lane);
}

int
main(
    int argc,
    char** argv)
{
    char const* root = argc > 1 ? argv[1] : "..";

    test_the_split_in_steps_is_the_per_member_decode(root);
    test_a_world_builds_loc_loads_split_the_group_in_slices(root);
    test_the_longest_step(root);
    if( g_failures )
    {
        printf("loc_load_split_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("loc_load_split_test: ok\n");
    return 0;
}
