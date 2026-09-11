#include "ev_rigs.h"

#include "anim_affinity.h"
#include "tool_profile.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- the walk ------------------------------------------------------------ */

static int
npc_cmp(const void* a, const void* b)
{
    const struct EV_RigNpc* x = a;
    const struct EV_RigNpc* y = b;
    return x->npc_id - y->npc_id;
}

static double
now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

struct EV_RigIndex*
ev_rigs_build_seqs(struct Tool_Dat2Cache* cache)
{
    assert(cache);

    double t0 = now_ms();

    struct Tool_FramemapIndex fm_index;
    memset(&fm_index, 0, sizeof(fm_index));
    if( !tool_dat2_build_framemap_index(cache, &fm_index) )
        return NULL;

    int max_seq_id = 0;
    int max_framemap = 0;
    for( int i = 0; i < fm_index.count; i++ )
    {
        if( fm_index.entries[i].seq_id > max_seq_id )
            max_seq_id = fm_index.entries[i].seq_id;
        if( fm_index.entries[i].framemap_id > max_framemap )
            max_framemap = fm_index.entries[i].framemap_id;
    }

    int distinct = 0;
    int aliases = 0;
    tool_dat2_canonicalise_framemap_index(
        cache, &fm_index, max_framemap, &distinct, &aliases);

    struct EV_RigIndex* index = calloc(1, sizeof(*index));
    assert(index);
    index->distinct_rigs = distinct;
    index->alias_ids = aliases;
    index->max_seq_id = max_seq_id;
    index->max_framemap = max_framemap;
    index->seq_count = fm_index.count;
    index->seqs = calloc((size_t)(fm_index.count > 0 ? fm_index.count : 1), sizeof(*index->seqs));
    assert(index->seqs);

    /*
     * seq id -> framemap id, as a flat array.
     *
     * Every lookup after this is "what rig is this sequence built on", asked
     * once per npc seed. Against 15,000 index entries a linear scan each time,
     * over 16,000 npcs, is billions of comparisons — that is what made the
     * first version of the catalog fail to finish.
     */
    index->seq_framemap = malloc(((size_t)max_seq_id + 1) * sizeof(*index->seq_framemap));
    assert(index->seq_framemap);
    for( int i = 0; i <= max_seq_id; i++ )
        index->seq_framemap[i] = -1;

    /* Sequences per rig, so an npc's match count is a lookup rather than a
     * scan. The npc list asks for it on every row. */
    index->fm_seq_count = calloc((size_t)max_framemap + 1, sizeof(*index->fm_seq_count));
    index->fm_skeletal_count = calloc((size_t)max_framemap + 1, sizeof(*index->fm_skeletal_count));
    assert(index->fm_seq_count);
    assert(index->fm_skeletal_count);

    for( int i = 0; i < fm_index.count; i++ )
    {
        const struct Tool_FramemapIndexEntry* e = &fm_index.entries[i];
        index->seqs[i].seq_id = e->seq_id;
        index->seqs[i].framemap_id = e->framemap_id;
        index->seqs[i].frame_count = e->frame_count;
        index->seqs[i].skeletal = e->skeletal;
        index->seq_framemap[e->seq_id] = e->framemap_id;
        if( e->framemap_id >= 0 )
        {
            index->fm_seq_count[e->framemap_id]++;
            if( e->skeletal )
                index->fm_skeletal_count[e->framemap_id]++;
        }
    }
    tool_framemap_index_free(&fm_index);

    index->build_ms = (int)(now_ms() - t0);
    return index;
}

/** An npc's seeds -> its rigs and their sequence counts. The one place that
 *  arithmetic lives, so the batch pass and the on-demand path cannot drift. */
static void
fill_npc_row(
    const struct EV_RigIndex* index,
    struct Tool_Dat2Cache* cache,
    const struct RSCache_Dat2ConfigNpc* npc,
    int npc_id,
    struct EV_RigNpc* out)
{
    memset(out, 0, sizeof(*out));
    out->npc_id = npc_id;

    struct Tool_AnimSeeds seeds;
    tool_dat2_npc_anim_seeds(cache, npc, &seeds);

    for( int s = 0; s < seeds.count && out->framemap_count < EV_RIG_MAX_FRAMEMAPS; s++ )
    {
        int sid = seeds.seq_ids[s];
        if( sid < 0 || sid > index->max_seq_id )
            continue;
        int fm = index->seq_framemap[sid];
        if( fm < 0 )
            continue;
        int dup = 0;
        for( int k = 0; k < out->framemap_count; k++ )
            if( out->framemaps[k] == fm )
                dup = 1;
        if( dup )
            continue;
        out->framemaps[out->framemap_count++] = fm;
        out->seq_count += index->fm_seq_count[fm];
        out->skeletal_count += index->fm_skeletal_count[fm];
    }

    tool_anim_seeds_free(&seeds);
}

struct NpcPass
{
    struct EV_RigIndex* index;
    struct Tool_Dat2Cache* cache;
    EV_RigProgressFn progress;
    void* userdata;
    int capacity;
    int seen;
    int total;
    int abandoned;
};

static int
visit_npc(int npc_id, struct RSCache_Dat2ConfigNpc* npc, void* user)
{
    struct NpcPass* pass = user;
    struct EV_RigIndex* index = pass->index;

    /* Every 64 npcs rather than every one: the callback takes a lock, and at
     * 16,000 npcs that lock would cost more than the walk. */
    if( (pass->seen & 63) == 0 && pass->progress &&
        !pass->progress(pass->userdata, EV_RIG_STAGE_NPCS, pass->seen, pass->total) )
    {
        pass->abandoned = 1;
        return 0;
    }
    pass->seen++;

    if( index->npc_count == pass->capacity )
    {
        pass->capacity = pass->capacity ? pass->capacity * 2 : 4096;
        struct EV_RigNpc* grown =
            realloc(index->npcs, (size_t)pass->capacity * sizeof(*grown));
        assert(grown);
        index->npcs = grown;
    }
    fill_npc_row(index, pass->cache, npc, npc_id, &index->npcs[index->npc_count++]);
    return 1;
}

int
ev_rigs_build_npcs(
    struct Tool_Dat2Cache* cache,
    struct EV_RigIndex* index,
    EV_RigProgressFn progress,
    void* userdata)
{
    assert(cache);
    assert(index);

    double t0 = now_ms();

    free(index->npcs);
    index->npcs = NULL;
    index->npc_count = 0;

    /*
     * How many npcs there are, for the progress fraction. The id list is cheap
     * — it comes off the archives' file tables without decoding a record — and
     * the walk that follows is what decodes them, one group archive at a time.
     */
    int* npc_ids = NULL;
    int npc_count = 0;
    tool_dat2_config_ids(
        cache, RSCACHE_TYPE_NPC, RSCACHE_DAT2_CONFIG_KIND_NPC, &npc_ids, &npc_count);
    free(npc_ids);

    struct NpcPass pass = { index, cache, progress, userdata, 0, 0, npc_count, 0 };
    tool_dat2_npc_walk_all(cache, visit_npc, &pass);
    if( pass.abandoned )
        return 0;

    /* No npc table at all is a complete answer, not a failed one: the sequence
     * half still stands, and the player and model views ask for a rig's
     * sequences without naming an npc. */
    qsort(index->npcs, (size_t)index->npc_count, sizeof(*index->npcs), npc_cmp);
    index->npcs_complete = 1;
    index->build_ms += (int)(now_ms() - t0);
    if( progress )
        progress(userdata, EV_RIG_STAGE_DONE, index->npc_count, index->npc_count);
    return 1;
}

struct EV_RigIndex*
ev_rigs_build(
    struct Tool_Dat2Cache* cache,
    EV_RigProgressFn progress,
    void* userdata)
{
    if( progress && !progress(userdata, EV_RIG_STAGE_SEQS, 0, 0) )
        return NULL;

    struct EV_RigIndex* index = ev_rigs_build_seqs(cache);
    if( !index )
        return NULL;

    if( !ev_rigs_build_npcs(cache, index, progress, userdata) )
    {
        ev_rigs_free(index);
        return NULL;
    }
    return index;
}

static void*
copy_ints(const int* src, size_t count)
{
    if( !src )
        return NULL;
    int* dst = malloc(count * sizeof(*dst));
    assert(dst);
    memcpy(dst, src, count * sizeof(*dst));
    return dst;
}

struct EV_RigIndex*
ev_rigs_clone(const struct EV_RigIndex* index)
{
    assert(index);

    struct EV_RigIndex* copy = malloc(sizeof(*copy));
    assert(copy);
    *copy = *index;

    copy->seqs = malloc((size_t)(index->seq_count > 0 ? index->seq_count : 1) * sizeof(*copy->seqs));
    assert(copy->seqs);
    memcpy(copy->seqs, index->seqs, (size_t)index->seq_count * sizeof(*copy->seqs));

    if( index->npc_count > 0 )
    {
        copy->npcs = malloc((size_t)index->npc_count * sizeof(*copy->npcs));
        assert(copy->npcs);
        memcpy(copy->npcs, index->npcs, (size_t)index->npc_count * sizeof(*copy->npcs));
    }
    else
    {
        copy->npcs = NULL;
        copy->npc_count = 0;
    }

    copy->seq_framemap = copy_ints(index->seq_framemap, (size_t)index->max_seq_id + 1);
    copy->fm_seq_count = copy_ints(index->fm_seq_count, (size_t)index->max_framemap + 1);
    copy->fm_skeletal_count = copy_ints(index->fm_skeletal_count, (size_t)index->max_framemap + 1);
    return copy;
}

void
ev_rigs_free(struct EV_RigIndex* index)
{
    /* A deallocator: NULL is the ordinary "there was no index" case, which is
     * every cache switch before the first walk finishes. */
    if( !index )
        return;
    free(index->seqs);
    free(index->npcs);
    free(index->seq_framemap);
    free(index->fm_seq_count);
    free(index->fm_skeletal_count);
    free(index);
}

const struct EV_RigNpc*
ev_rigs_npc(const struct EV_RigIndex* index, int npc_id)
{
    if( !index )
        return NULL;
    int lo = 0;
    int hi = index->npc_count - 1;
    while( lo <= hi )
    {
        int mid = lo + (hi - lo) / 2;
        if( index->npcs[mid].npc_id == npc_id )
            return &index->npcs[mid];
        if( index->npcs[mid].npc_id < npc_id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

int
ev_rigs_npc_lookup(
    const struct EV_RigIndex* index,
    struct Tool_Dat2Cache* cache,
    int npc_id,
    struct EV_RigNpc* out)
{
    assert(out);
    if( !index )
        return 0;

    const struct EV_RigNpc* row = ev_rigs_npc(index, npc_id);
    if( row )
    {
        *out = *row;
        return 1;
    }
    if( !cache )
        return 0;

    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(cache, npc_id);
    if( !npc )
        return 0;
    fill_npc_row(index, cache, npc, npc_id, out);
    RSCache_Dat2ConfigNpcFree(npc);
    return 1;
}

int
ev_rigs_seq_framemap(const struct EV_RigIndex* index, int seq_id)
{
    if( !index || seq_id < 0 || seq_id > index->max_seq_id )
        return -1;
    return index->seq_framemap[seq_id];
}

/* ---- the background build ------------------------------------------------ */

/*
 * One walk at a time, identified by a generation number.
 *
 * The generation is what makes abandonment safe without killing a thread: a
 * worker publishes only if it is still the current generation, and otherwise
 * frees its own work and exits. Nothing is ever freed out from under the
 * server, because the promotion — pending becomes current, and the index it
 * replaces is released — happens on the main thread inside ev_rigs_collect.
 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_generation = 0;
static struct EV_RigIndex* g_current = NULL;
static struct EV_RigIndex* g_pending = NULL;
static struct EV_RigStatus g_status = { EV_RIG_IDLE, EV_RIG_STAGE_SEQS, 0, 0, 0 };

struct RigJob
{
    char cache_dir[1024];
    char rev[64];
    unsigned generation;
};

/** Hand an index to the main thread, or drop it if this walk was abandoned.
 *  Takes ownership either way. Returns 0 when the walk should stop. */
static int
publish(const struct RigJob* job, struct EV_RigIndex* index, enum EV_RigState state)
{
    pthread_mutex_lock(&g_lock);
    if( job->generation != g_generation )
    {
        pthread_mutex_unlock(&g_lock);
        ev_rigs_free(index);
        return 0;
    }
    ev_rigs_free(g_pending); /* an earlier publish the main thread never collected */
    g_pending = index;
    g_status.state = state;
    if( index )
        g_status.ms = index->build_ms;
    pthread_mutex_unlock(&g_lock);
    return 1;
}

static int
worker_progress(void* userdata, enum EV_RigStage stage, int done, int total)
{
    const struct RigJob* job = userdata;
    int live;
    pthread_mutex_lock(&g_lock);
    live = (job->generation == g_generation);
    if( live )
    {
        g_status.stage = stage;
        g_status.done = done;
        g_status.total = total;
    }
    pthread_mutex_unlock(&g_lock);
    return live;
}

static void*
worker_main(void* arg)
{
    struct RigJob* job = arg;

    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    if( !tool_resolve_profile(job->rev, NULL, NULL, NULL, NULL, &profile) ||
        !tool_dat2_open(job->cache_dir, &profile, &cache) )
    {
        publish(job, NULL, EV_RIG_FAILED);
        fprintf(stderr, "rigs: cannot read %s as %s\n", job->cache_dir, job->rev);
        free(job);
        return NULL;
    }

    struct EV_RigIndex* index = ev_rigs_build_seqs(&cache);
    if( !index )
    {
        publish(job, NULL, EV_RIG_FAILED);
        fprintf(stderr, "rigs: no sequence table in %s\n", job->cache_dir);
        tool_dat2_close(&cache);
        free(job);
        return NULL;
    }

    fprintf(
        stderr,
        "rigs: %d sequences on %d distinct rigs (%d ids unified) [%d ms]\n",
        index->seq_count,
        index->distinct_rigs,
        index->alias_ids,
        index->build_ms);

    /* The sequence half is enough to answer any single npc, so hand it over
     * now: the npc pass is most of the walk and nobody should wait for it to
     * look at one creature. */
    if( !publish(job, ev_rigs_clone(index), EV_RIG_BUILDING) )
    {
        ev_rigs_free(index);
        tool_dat2_close(&cache);
        free(job);
        return NULL;
    }

    if( ev_rigs_build_npcs(&cache, index, worker_progress, job) )
    {
        fprintf(
            stderr,
            "rigs: %d npcs walked [%d ms total]\n",
            index->npc_count,
            index->build_ms);
        publish(job, index, EV_RIG_READY);
    }
    else
    {
        ev_rigs_free(index); /* abandoned: another cache is open now */
    }

    tool_dat2_close(&cache);
    free(job);
    return NULL;
}

void
ev_rigs_start(const char* cache_dir, const char* rev)
{
    assert(cache_dir);
    assert(rev);

    struct RigJob* job = calloc(1, sizeof(*job));
    assert(job);
    snprintf(job->cache_dir, sizeof(job->cache_dir), "%s", cache_dir);
    snprintf(job->rev, sizeof(job->rev), "%s", rev);

    pthread_mutex_lock(&g_lock);
    g_generation++;
    job->generation = g_generation;
    /* Both describe the cache that WAS open. Detached from the globals here and
     * freed after the lock, so a worker publishing at this instant cannot see
     * them and the free is not holding up the walk. */
    struct EV_RigIndex* stale_current = g_current;
    struct EV_RigIndex* stale_pending = g_pending;
    g_current = NULL;
    g_pending = NULL;
    g_status.state = EV_RIG_BUILDING;
    g_status.stage = EV_RIG_STAGE_SEQS;
    g_status.done = 0;
    g_status.total = 0;
    g_status.ms = 0;
    pthread_mutex_unlock(&g_lock);

    ev_rigs_free(stale_current);
    ev_rigs_free(stale_pending);

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int err = pthread_create(&thread, &attr, worker_main, job);
    pthread_attr_destroy(&attr);

    if( err != 0 )
    {
        /* No thread means no animations at all, which is the state this exists
         * to end — so do the walk here rather than pretend one is coming. */
        fprintf(stderr, "rigs: no worker thread (%s); walking inline\n", strerror(err));
        worker_main(job);
    }
}

void
ev_rigs_collect(void)
{
    struct EV_RigIndex* retired = NULL;

    pthread_mutex_lock(&g_lock);
    if( g_pending )
    {
        retired = g_current;
        g_current = g_pending;
        g_pending = NULL;
    }
    pthread_mutex_unlock(&g_lock);

    ev_rigs_free(retired);
}

const struct EV_RigIndex*
ev_rigs_current(void)
{
    pthread_mutex_lock(&g_lock);
    const struct EV_RigIndex* current = g_current;
    pthread_mutex_unlock(&g_lock);
    return current;
}

void
ev_rigs_status(struct EV_RigStatus* out)
{
    assert(out);
    pthread_mutex_lock(&g_lock);
    *out = g_status;
    pthread_mutex_unlock(&g_lock);
}
