/*
 * Open the cache indices this revision's profile says to open before the title
 * screen, and show the bar moving while it happens.
 *
 * This is the deob's own boot shape (Statics.method4490), which is not the
 * 2004 one wearing different names. OldSchool opens eight reference tables at
 * once and watches them complete, so its progress is a WEIGHTED SUM of how far
 * each has got rather than a position in a queue -- and the weights are wildly
 * uneven: sound effects alone are 53 of the 100, sprites 36, and the remaining
 * six share 11 between them. That is why the whole span carries one sentence,
 * "Checking for updates - N%", instead of naming each table as it lands.
 *
 * Which tables, in what order, with what weight and under what caption is the
 * profile's ([preload:] with kind=index). This knows only how to open one.
 */
#include "engine/dat2/dat2_tasks.h"

#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_login_replies.h"
#include "game/rs_preload.h"

#include "asyncio.h"
#include "boot_telemetry.h"
#include "cache/rscache_io.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2Preload
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct RS_PreloadTable const* steps;
    struct RS_LoginReplyTable const* strings;

    int at;
    /** Weight of every index already open, over the profile's stated total. */
    int weight_done;
    int weight_total;
    /*
     * The step being run, and the table it named.
     *
     * On the task rather than in the loop because this protothread SUSPENDS in
     * the middle of that loop: locals do not survive a yield, and reading one
     * afterwards reads whatever the re-entered frame happens to hold. Both are
     * set before the yields and used after them, so both have to live here.
     * The pointer is safe to hold -- it aims into the profile's own step array,
     * which outlives this task.
     */
    struct RS_PreloadStep const* step;
    enum RSCache_Dat2Table table_id;
    /* The `groups=all` fill: where the next wave starts in the reference
     * table's id list, how many siblings (index opens, then prefetch waves)
     * are still running, and the step whose caption the bar is showing. All
     * outlive yields, so all live here. */
    int fill_at;
    int pending;
    struct RS_PreloadStep const* fill_step;
    enum RSCache_Dat2Table fill_table;
    int fill_total;
    /** Groups asked for per table, added to by each wave as it lands. Per
     *  table because waves of one step are still landing while the next
     *  step's go out. */
    int fill_landed[RSCACHE_DAT2_TABLE_COUNT];
    /** Groups the producer had no bytes for, over the whole fill. */
    int fill_absent;
    /** The percentage last drawn, so a frame is spent only when it moves. */
    int fill_shown;
    /** The first index step this run opened, whose caption the index phase
     *  draws under; NULL when every table was already resident. */
    struct RS_PreloadStep const* index_step;
    int weight_shown;
    /* The table being filled. Owned by the build cache, so holding the
     * pointer across yields is safe; a local would not be. */
    struct RSCache_ReferenceTable const* fill_ref;
    /*
     * The caption, owned here.
     *
     * It has to outlive the frame it is drawn on, and a task local does not --
     * the protothread suspends and the stack is gone. The task stays queued
     * across its own render request, so this buffer is exactly as long-lived
     * as the request that points at it.
     */
    char caption[128];
};

/*
 * The profile names a table; this is the one place that turns a name into the
 * client's own id.
 *
 * By name rather than by the `id=` the profile also carries, because that id
 * is the REFERENCE's raw disk index and the two do not agree across epochs --
 * OldSchool ships client defaults at 17 and RS2 at 28. The name is the stable
 * half.
 */
/* See the comment above the `groups=all` fill in Task_Dat2Preload_Run. */
static int
preload_fill_enabled(void)
{
#if defined(TORIRS_PLATFORM_WEB)
    return 1;
#else
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_PRELOAD_FILL") != NULL;
    return enabled;
#endif
}

static int
table_from_name(
    char const* name,
    enum RSCache_Dat2Table* out_table)
{
    static struct
    {
        char const* name;
        enum RSCache_Dat2Table table;
    } const k_tables[] = {
        { "soundeffects", RSCACHE_DAT2_TABLE_SOUND_EFFECTS },
        { "musictracks", RSCACHE_DAT2_TABLE_MUSIC_TRACKS },
        { "musicsamples", RSCACHE_DAT2_TABLE_MUSIC_SAMPLES },
        { "musicpatches", RSCACHE_DAT2_TABLE_MUSIC_PATCHES },
        { "musicjingles", RSCACHE_DAT2_TABLE_MUSIC_JINGLES },
        { "sprites", RSCACHE_DAT2_TABLE_SPRITES },
        { "textures", RSCACHE_DAT2_TABLE_TEXTURES },
        { "binary", RSCACHE_DAT2_TABLE_BINARY },
        { "fontmetrics", RSCACHE_DAT2_TABLE_FONTS },
        { "defaults", RSCACHE_DAT2_TABLE_DEFAULTS },
        { "configs", RSCACHE_DAT2_TABLE_CONFIGS },
        { "interfaces", RSCACHE_DAT2_TABLE_INTERFACES },
        { "models", RSCACHE_DAT2_TABLE_MODELS },
        { "animations", RSCACHE_DAT2_TABLE_ANIMATIONS },
        { "skeletons", RSCACHE_DAT2_TABLE_SKELETONS },
        { "maps", RSCACHE_DAT2_TABLE_MAPS },
        { "clientscript", RSCACHE_DAT2_TABLE_CLIENTSCRIPT },
        { "worldmap_geography", RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY },
        { "worldmap", RSCACHE_DAT2_TABLE_WORLDMAP },
        { "worldmap_ground", RSCACHE_DAT2_TABLE_WORLDMAP_GROUND },
    };

    assert(name);
    assert(out_table);
    for( size_t i = 0; i < sizeof(k_tables) / sizeof(k_tables[0]); i++ )
    {
        if( strcmp(k_tables[i].name, name) == 0 )
        {
            *out_table = k_tables[i].table;
            return 1;
        }
    }
    return 0;
}

/* Build the sentence this step shows: the profile's words, plus the running
 * weighted percentage where the profile states weights. The deob appends its
 * own number the same way, and reads "Checking for updates - 53%" the moment
 * sound effects alone have landed. */
static void
compose_caption(
    struct Task_Dat2Preload* task,
    struct RS_PreloadStep const* step)
{
    char const* words = NULL;

    assert(task);
    assert(step);
    if( step->say[0] && task->strings )
        words = RS_LoginReplies_String(task->strings, step->say);
    if( !words )
    {
        /* The profile declares no sentence for this step, so there is none to
         * draw. The bar still moves. */
        task->caption[0] = '\0';
        return;
    }
    if( task->weight_total > 0 )
        snprintf(
            task->caption,
            sizeof(task->caption),
            "%s - %d%%",
            words,
            task->weight_done * 100 / task->weight_total);
    else
        snprintf(task->caption, sizeof(task->caption), "%s", words);
}

/*
 * A `groups=all` fill's shape, as this task now drives it.
 *
 * The first version of the fill queued one Dat2GroupTouch task per group in
 * waves of 1024 and waited for every task of a wave to end before queueing
 * the next. Measured on the browser lane (TORIRS_IO_TRACE, 2026-09-17) that
 * had three costs on top of the download itself: the wire went idle at the
 * tail of every wave while the last few answers dribbled in and the bar
 * frame was drawn; every group cost a task, a slot, an executor round trip
 * and a promise; and every group was DECODED -- 24,000 bzip2 passes -- for
 * bytes that were freed on the spot. Six seconds of a seven second boot.
 *
 * Now a wave is ONE prefetch item (TORIRS_IOK_CACHE_PREFETCH) naming up to
 * PREFETCH_WAVE groups, nothing is decoded, and up to PREFETCH_WAVES_OUT
 * waves are outstanding at once, across step boundaries: the next wave goes
 * out as soon as the count of waves in flight drops below the ceiling, not
 * when the pipe is empty. The bar advances per wave, as before.
 */
enum
{
    PREFETCH_WAVE = 512,
    PREFETCH_WAVES_OUT = 6,
};

/*
 * One reference table, opened and filed. A sibling of the preload task so
 * every index the profile names is asked for in the same pass rather than
 * one round trip after another: eight indices were eight serial reads, each
 * a whole frame on the browser lane.
 */
struct Task_Dat2IndexOpen
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    enum RSCache_Dat2Table table_id;
    char archive[RS_PRELOAD_NAME_LEN];
};

static int
Task_Dat2IndexOpen_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2IndexOpen* task = (struct Task_Dat2IndexOpen*)task_base;
    struct RSCache_ReferenceTable* table;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ReferenceTableLoad(io, 0, task->table_id);
    PT_YIELD(&task->pt);

    table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
    if( !table )
    {
        /* A table this cache does not carry. Not fatal: a trimmed cache
         * legitimately ships without music, and the step's weight simply
         * never lands. */
        TORIRS_ERR("preload: no reference table for '%s'\n", task->archive);
        PT_EXIT(&task->pt);
    }
    /* Never replace a table another task filed while this one was yielded:
     * a replacement frees the old one under any fill that is walking it. */
    if( dat2_buildcache_reference_table_has(task->bc, task->table_id) )
        RSCache_ReferenceTableFree(table);
    else
        dat2_buildcache_reference_table_add(task->bc, task->table_id, table);

    PT_END(&task->pt);
}

static void
Task_Dat2IndexOpen_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2IndexOpen_VTable = {
    .run = Task_Dat2IndexOpen_Run,
    .free = Task_Dat2IndexOpen_Free,
};

static struct ToriRS_Task*
CreateTask_Dat2IndexOpen(
    struct Dat2BuildCache* bc,
    enum RSCache_Dat2Table table_id,
    char const* archive)
{
    struct Task_Dat2IndexOpen* task = calloc(1, sizeof(*task));

    assert(bc);
    assert(archive);
    assert(task);
    task->task.vtable = &Task_Dat2IndexOpen_VTable;
    strcpy(task->task.name, "Dat2IndexOpen");
    task->bc = bc;
    task->table_id = table_id;
    strncpy(task->archive, archive, sizeof(task->archive) - 1);
    PT_INIT(&task->pt);
    return &task->task;
}

/*
 * One wave of a fill: a single prefetch item naming `count` groups, and a
 * count of groups added to `*landed` when it is answered. Owns its copy of
 * the ids -- the reference table's list is the build cache's, and this task
 * outlives no assumption about it.
 */
struct Task_Dat2PrefetchWave
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    enum RSCache_Dat2Table table_id;
    int* ids;
    int count;
    int* landed;
    int* absent;
};

static int
Task_Dat2PrefetchWave_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2PrefetchWave* task = (struct Task_Dat2PrefetchWave*)task_base;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueCachePrefetch(
        io, 0, 0, task->table_id, TORIRS_IO_CACHE_DAT2, task->ids, task->count);
    PT_YIELD(&task->pt);

    /* Progress counts what was ASKED: a group the cache does not hold never
     * lands, and a bar that waited for it would never reach the end. */
    *task->landed += task->count;
    *task->absent += task->count - ToriRS_IO_PrefetchLanded(io, 0);
    ToriRS_IO_ClearItem(ToriRS_IO_TaskSlot(io, 0));

    PT_END(&task->pt);
}

static void
Task_Dat2PrefetchWave_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2PrefetchWave* task = (struct Task_Dat2PrefetchWave*)task_base;
    free(task->ids);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat2PrefetchWave_VTable = {
    .run = Task_Dat2PrefetchWave_Run,
    .free = Task_Dat2PrefetchWave_Free,
};

static struct ToriRS_Task*
CreateTask_Dat2PrefetchWave(
    struct Dat2BuildCache* bc,
    enum RSCache_Dat2Table table_id,
    int const* ids,
    int count,
    int* landed,
    int* absent)
{
    struct Task_Dat2PrefetchWave* task = calloc(1, sizeof(*task));

    assert(bc);
    assert(ids);
    assert(count > 0);
    assert(landed);
    assert(absent);
    assert(task);
    task->task.vtable = &Task_Dat2PrefetchWave_VTable;
    strcpy(task->task.name, "Dat2PrefetchWave");
    task->bc = bc;
    task->table_id = table_id;
    task->ids = malloc((size_t)count * sizeof(int));
    assert(task->ids);
    memcpy(task->ids, ids, (size_t)count * sizeof(int));
    task->count = count;
    task->landed = landed;
    task->absent = absent;
    PT_INIT(&task->pt);
    return &task->task;
}

/* The weight of every index step whose table is open, over the profile's
 * total: the deob's "Checking for updates - N%". */
static int
preload_index_weight_done(struct Task_Dat2Preload* task)
{
    int done = 0;

    assert(task);
    for( int i = 0; i < task->steps->count; i++ )
    {
        struct RS_PreloadStep const* step = RS_Preload_At(task->steps, i);
        enum RSCache_Dat2Table table_id;

        if( step->kind != RS_PRELOAD_KIND_INDEX )
            continue;
        if( !table_from_name(step->archive, &table_id) )
            continue;
        if( dat2_buildcache_reference_table_has(task->bc, table_id) )
            done += step->weight;
    }
    return done;
}

/*
 * The fill caption for the step being filled, "Loading sprites - 37%".
 * Returns 1 when it differs from the one last drawn, which is when a frame
 * is worth spending on it.
 */
static int
preload_fill_caption(struct Task_Dat2Preload* task)
{
    struct RS_PreloadStep const* step = task->fill_step;
    char const* name;
    char const* words;
    int percent;

    assert(task);
    assert(step);
    percent = task->fill_total > 0 ? task->fill_landed[task->fill_table] * 100 / task->fill_total
                                   : 100;
    if( percent > 100 )
        percent = 100;
    if( percent == task->fill_shown )
        return 0;
    task->fill_shown = percent;
    name = step->fill_say[0] ? step->fill_say : step->say;
    words = name[0] && task->strings ? RS_LoginReplies_String(task->strings, name) : NULL;
    snprintf(
        task->caption,
        sizeof(task->caption),
        "%s - %d%%",
        words ? words : step->archive,
        percent);
    return 1;
}

/*
 * One pass of waiting on the fill: draw the bar when it moved, otherwise
 * park until a wave lands. A macro because the yield has to be in the
 * protothread's own function -- and ONE yield, because a protothread's
 * resume point is the line number, so two yields on one line is one case
 * label twice.
 */
#define PRELOAD_FILL_WAIT(task)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( (task)->fill_step && (task)->fill_step->render && preload_fill_caption(task) )         \
        {                                                                                          \
            (task)->task.wants_render = 1;                                                         \
            (task)->task.render.intent = TORIRS_RENDER_BOOT_BAR;                                   \
            (task)->task.render.percent = (task)->fill_step->percent;                              \
            (task)->task.render.caption = (task)->caption;                                         \
        }                                                                                          \
        else                                                                                       \
            (task)->task.blocked = 1;                                                              \
        PT_YIELD(&(task)->pt);                                                                     \
    } while( 0 )

static int
Task_Dat2Preload_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2Preload* task = (struct Task_Dat2Preload*)task_base;

    (void)io;
    PT_BEGIN(&task->pt);

    /*
     * Every index the profile names, opened TOGETHER.
     *
     * The deob opens its eight at once and watches them complete; this used
     * to open them one after another, a round trip and a frame apiece.
     */
    for( task->at = 0; task->at < task->steps->count; task->at++ )
    {
        task->step = RS_Preload_At(task->steps, task->at);
        assert(task->step);
        if( task->step->kind != RS_PRELOAD_KIND_INDEX )
            continue;
        if( !table_from_name(task->step->archive, &task->table_id) )
        {
            TORIRS_ERR(
                "preload: [preload:%s] names no table this client knows ('%s')\n",
                task->step->name,
                task->step->archive);
            continue;
        }
        /* A table already resident (the post-login rebake reaches this task
         * again) is neither re-read nor re-announced. */
        if( dat2_buildcache_reference_table_has(task->bc, task->table_id) )
            continue;
        if( !task->index_step )
            task->index_step = task->step;
        if( task->bc->base.asset_queue )
            ToriRS_TaskQueue_AddJoined(
                task->bc->base.asset_queue,
                CreateTask_Dat2IndexOpen(task->bc, task->table_id, task->step->archive),
                &task->pending);
        else
        {
            /* No asset queue to fan out on (a test harness): open inline. */
            RSCache_IO_Dat2ReferenceTableLoad(io, 0, task->table_id);
            PT_YIELD(&task->pt);
            {
                struct RSCache_ReferenceTable* table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
                if( !table )
                    TORIRS_ERR("preload: no reference table for '%s'\n", task->step->archive);
                else if( dat2_buildcache_reference_table_has(task->bc, task->table_id) )
                    RSCache_ReferenceTableFree(table);
                else
                    dat2_buildcache_reference_table_add(task->bc, task->table_id, table);
            }
        }
    }
    if( task->index_step )
        ToriRS_BootTelemetry_Mark("preload:indices");

    /* The bar moves as they land: the weighted sum, under the one sentence
     * the whole span carries. */
    task->weight_shown = -1;
    for( ;; )
    {
        task->weight_done = preload_index_weight_done(task);
        if( task->index_step && task->index_step->render && task->weight_done != task->weight_shown )
        {
            task->weight_shown = task->weight_done;
            compose_caption(task, task->index_step);
            TASK_YIELD_TO_RENDER(
                &task->task,
                &task->pt,
                TORIRS_RENDER_BOOT_BAR,
                task->index_step->percent,
                task->caption);
            continue;
        }
        if( task->pending <= 0 )
            break;
        task->task.blocked = 1;
        PT_YIELD(&task->pt);
    }
    if( task->index_step )
        ToriRS_BootTelemetry_Mark("preload:indices:open");

    /*
     * `groups=all`: the whole archive, not just its index.
     *
     * What the deob's loading screen does for interfaces, scripts and
     * sprites, and why its client is never caught fetching a script in
     * the middle of an interface hook. Without it a browser boot of
     * osrs239 paid 1149 network round trips after login -- 682 of them a
     * single CS2 script or sprite an interface open resolved on demand,
     * one after another -- and the game froze until they were in.
     *
     * The revision's list (revconfig) says WHICH archives; whether to
     * act on it is the lane's: the point of the fetch is to make the
     * group resident in the client's store, and on a cache read from
     * local disk every group already is, so the desktop client would
     * spend a boot re-reading ~15 MB for nothing. The browser has no
     * local disk (TORIRS_PLATFORM_WEB), and TORIRS_PRELOAD_FILL=1 forces
     * it elsewhere -- a native client on a streamed cache, or a test of
     * this path.
     *
     * Waves of PREFETCH_WAVE groups, PREFETCH_WAVES_OUT of them in flight,
     * and the window slides across step boundaries -- see the note above
     * PREFETCH_WAVE.
     */
    task->fill_shown = -1;
    for( task->at = 0; task->at < task->steps->count; task->at++ )
    {
        task->step = RS_Preload_At(task->steps, task->at);
        assert(task->step);
        if( task->step->kind != RS_PRELOAD_KIND_INDEX || !task->step->groups_all )
            continue;
        if( !table_from_name(task->step->archive, &task->table_id) )
            continue;
        if( !task->bc->base.asset_queue || !preload_fill_enabled() )
            continue;
        if( task->bc->reference_table_filled[task->table_id] )
            continue;
        if( !dat2_buildcache_reference_table_has(task->bc, task->table_id) )
        {
            TORIRS_ERR(
                "preload: '%s' has no reference table to fill from\n", task->step->archive);
            continue;
        }
        /* Marked before the waves rather than after: a second preload task
         * queued while this one is mid-fill must not start a second fill of
         * the same table. */
        task->bc->reference_table_filled[task->table_id] = 1;
        task->fill_ref = dat2_buildcache_reference_table_get(task->bc, task->table_id);
        assert(task->fill_ref);
        task->fill_step = task->step;
        task->fill_table = task->table_id;
        task->fill_total = task->fill_ref->id_count;
        task->fill_shown = -1;
        ToriRS_BootTelemetry_Markf("preload:%s", task->step->archive);

        for( task->fill_at = 0; task->fill_at < task->fill_ref->id_count;
             task->fill_at += PREFETCH_WAVE )
        {
            int n = task->fill_ref->id_count - task->fill_at;
            if( n > PREFETCH_WAVE )
                n = PREFETCH_WAVE;
            ToriRS_TaskQueue_AddJoined(
                task->bc->base.asset_queue,
                CreateTask_Dat2PrefetchWave(
                    task->bc,
                    task->table_id,
                    task->fill_ref->ids + task->fill_at,
                    n,
                    &task->fill_landed[task->table_id],
                    &task->fill_absent),
                &task->pending);
            /* The window: never more than this many waves on the wire, and
             * never an empty wire while there is a wave left to send. */
            while( task->pending >= PREFETCH_WAVES_OUT )
                PRELOAD_FILL_WAIT(task);
        }
        fprintf(
            stderr,
            "preload: %s (table %d) fill queued, %d groups, ids %d..%d\n",
            task->step->archive,
            (int)task->table_id,
            task->fill_ref->id_count,
            task->fill_ref->id_count ? task->fill_ref->ids[0] : -1,
            task->fill_ref->id_count ? task->fill_ref->ids[task->fill_ref->id_count - 1] : -1);
    }
    while( task->pending > 0 )
        PRELOAD_FILL_WAIT(task);
    if( task->fill_step )
    {
        int landed = 0;
        for( int t = 0; t < RSCACHE_DAT2_TABLE_COUNT; t++ )
            landed += task->fill_landed[t];
        fprintf(
            stderr,
            "preload: fills landed, %d groups asked, %d absent\n",
            landed,
            task->fill_absent);
        ToriRS_BootTelemetry_Mark("preload:fills");
    }

    PT_END(&task->pt);
}

static void
Task_Dat2Preload_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2Preload_VTable = {
    .run = Task_Dat2Preload_Run,
    .free = Task_Dat2Preload_Free,
};

struct ToriRS_Task*
CreateTask_Dat2Preload(
    struct CacheProvider* provider,
    struct RS_PreloadTable const* steps,
    struct RS_LoginReplyTable const* strings)
{
    struct Task_Dat2Preload* task;
    int indices = 0;

    assert(provider);
    assert(steps);

    /* A profile that lists no indices has nothing for this task to do, and
     * queueing one that immediately ends would put a frame's worth of
     * scheduling in the boot for nothing. */
    for( int i = 0; i < steps->count; i++ )
    {
        if( steps->steps[i].kind == RS_PRELOAD_KIND_INDEX )
            indices++;
    }
    if( indices == 0 )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2Preload_VTable;
    strcpy(task->task.name, "Dat2Preload");
    task->bc = (struct Dat2BuildCache*)provider;
    task->steps = steps;
    task->strings = strings;
    task->weight_total = RS_Preload_TotalWeight(steps);
    return &task->task;
}
