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
    /* The `groups=all` fill of the step's archive: where the next wave starts
     * in the reference table's id list, and how many siblings of the current
     * wave are still running. Both outlive yields, so both live here. */
    int fill_at;
    int fill_end;
    int pending;
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

static int
Task_Dat2Preload_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2Preload* task = (struct Task_Dat2Preload*)task_base;
    struct RSCache_ReferenceTable* table = NULL;

    PT_BEGIN(&task->pt);

    for( task->at = 0; task->at < task->steps->count; task->at++ )
    {
        task->step = RS_Preload_At(task->steps, task->at);
        assert(task->step);

        /* Only the index steps are this task's. The rest of the list describes
         * work other machinery does, and skipping it here is not a failure --
         * a step this lane does not perform simply is not performed. */
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

        if( !dat2_buildcache_reference_table_has(task->bc, task->table_id) )
        {
            /* Announce BEFORE the work, so the sentence on screen names what
             * is happening rather than what just finished -- and only when
             * there IS work: a table already resident (the post-login rebake
             * reaches this task again) must not replay its caption. */
            compose_caption(task, task->step);
            if( task->step->render )
                TASK_YIELD_TO_RENDER(
                    &task->task,
                    &task->pt,
                    TORIRS_RENDER_BOOT_BAR,
                    task->step->percent,
                    task->caption);

            RSCache_IO_Dat2ReferenceTableLoad(io, 0, task->table_id);
            PT_YIELD(&task->pt);

            table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
            if( !table )
            {
                /* A table this cache does not carry. Not fatal: a trimmed
                 * cache legitimately ships without music, and the step's
                 * weight simply never lands. */
                TORIRS_ERR("preload: no reference table for '%s'\n", task->step->archive);
                continue;
            }
            /* Never replace a table another task filed while this one was
             * yielded: a replacement frees the old one under any fill that
             * is walking it (the boot joins on its preload now, so two of
             * these no longer overlap -- this is the belt to that brace). */
            if( dat2_buildcache_reference_table_has(task->bc, task->table_id) )
                RSCache_ReferenceTableFree(table);
            else
                dat2_buildcache_reference_table_add(task->bc, task->table_id, table);
        }

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
         * Fetched in joined waves of siblings on the asset queue, so this
         * task never resumes past a wave with a group half done, and the
         * boot bar can advance between waves.
         *
         * The revision's list (revconfig) says WHICH archives; whether to
         * act on it is the lane's: the point of the fetch is to make the
         * group resident in the client's store, and on a cache read from
         * local disk every group already is, so the desktop client would
         * spend a boot re-reading and decompressing ~15 MB for nothing. The
         * browser has no local disk (TORIRS_PLATFORM_WEB), and
         * TORIRS_PRELOAD_FILL=1 forces it elsewhere -- a native client on a
         * streamed cache, or a test of this path.
         */
        if( task->step->groups_all && task->bc->base.asset_queue &&
            !task->bc->reference_table_filled[task->table_id] && preload_fill_enabled() )
        {
            /* Groups in flight per wave. The server pipelines happily and
             * the store writes in batches; what bounds this is that the bar
             * only advances between waves, and ten steps across the largest
             * archive (clientscript, 9725 groups) reads as movement. */
            enum
            {
                WAVE = 1024
            };
            /* Marked before the waves rather than after: a second preload
             * task queued while this one is mid-fill must not start a second
             * fill of the same table. */
            task->bc->reference_table_filled[task->table_id] = 1;
            task->fill_ref = dat2_buildcache_reference_table_get(task->bc, task->table_id);
            if( !task->fill_ref )
            {
                TORIRS_ERR(
                    "preload: '%s' has no reference table to fill from\n", task->step->archive);
                continue;
            }
            for( task->fill_at = 0; task->fill_at < task->fill_ref->id_count;
                 task->fill_at += WAVE )
            {
                task->fill_end = task->fill_at + WAVE < task->fill_ref->id_count
                                     ? task->fill_at + WAVE
                                     : task->fill_ref->id_count;
                for( int k = task->fill_at; k < task->fill_end; k++ )
                    ToriRS_TaskQueue_AddJoined(
                        task->bc->base.asset_queue,
                        CreateTask_Dat2GroupTouch(
                            &task->bc->base, task->table_id, task->fill_ref->ids[k]),
                        &task->pending);
                while( task->pending > 0 )
                {
                    task->task.blocked = 1;
                    PT_YIELD(&task->pt);
                }
                if( task->step->render )
                {
                    /* The deob's own shape for this phase: "Loading sprites -
                     * 37%". Its caption, not the index phase's, and one
                     * percentage -- the fill's, not the weighted sum's. */
                    char const* name = task->step->fill_say[0] ? task->step->fill_say
                                                               : task->step->say;
                    char const* words = name[0] && task->strings
                                            ? RS_LoginReplies_String(task->strings, name)
                                            : NULL;
                    snprintf(
                        task->caption,
                        sizeof(task->caption),
                        "%s - %d%%",
                        words ? words : task->step->archive,
                        task->fill_end * 100 / task->fill_ref->id_count);
                    TASK_YIELD_TO_RENDER(
                        &task->task,
                        &task->pt,
                        TORIRS_RENDER_BOOT_BAR,
                        task->step->percent,
                        task->caption);
                }
            }
            fprintf(
                stderr,
                "preload: %s (table %d) filled, %d groups, ids %d..%d, bar \"%s\" at %d%%\n",
                task->step->archive,
                (int)task->table_id,
                task->fill_ref->id_count,
                task->fill_ref->id_count ? task->fill_ref->ids[0] : -1,
                task->fill_ref->id_count ? task->fill_ref->ids[task->fill_ref->id_count - 1] : -1,
                task->caption,
                task->step->percent);
        }

        task->weight_done += task->step->weight;
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
