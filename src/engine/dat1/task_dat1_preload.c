/*
 * Fetch the jag archives this revision's profile says to fetch before the
 * title screen, and show the bar moving while it happens.
 *
 * This is Client-TS's boot (Client.ts `load()`), and it is a different shape
 * from the modern one: nine archives pulled one at a time, each NAMED on
 * screen as it is requested, so the bar is a position in a queue rather than a
 * weighted sum. On a `source=ondemand` world every one of these is a real HTTP
 * round trip and this is where the boot's whole wall time goes, which is
 * exactly why the reference bothers to announce each one.
 *
 * The archives are only the front half of that boot. Once the version list is
 * in, the reference turns to the on-demand cache and prefetches it -- every
 * animation, every model the version list flags for preload, and both halves
 * of every map square -- all before the login screen is offered
 * (Client.ts load(), "Requesting animations/models/maps"). The profile
 * transcribes those passes as [preload:] steps with kind=ondemand, and this
 * task performs them too, for the same reason in the same place: on an
 * on-demand world they are the loading, and after login is too late for a
 * loading screen. A disk world skips them -- there is no wire to warm, the
 * files are already local.
 *
 * Which archives and passes, in what order, at what percentage and under what
 * caption is the profile's ([preload:] with kind=jagfile / kind=ondemand).
 * This knows only how to fetch one and where to put it.
 */
#include "engine/dat1/dat1_tasks.h"

#include "engine/dat1/dat1_buildcache.h"
#include "game/rs_login_replies.h"
#include "game/rs_preload.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "datatypes/dat1_version_list.h"
#include "datatypes/mapsquares.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Where each archive lives once it is decoded.
 *
 * The buildcache keeps a named slot per archive rather than a table, so the
 * mapping is a pair of function pointers. `get` is how this task knows an
 * archive is already resident and skips it; an archive whose slot has no
 * getter is fetched once and handed over, which is safe because this task runs
 * before anything that could be holding a pointer into the old one.
 */
struct Dat1PreloadSlot
{
    char const* name;
    int archive;
    struct RSCache_FileListDat* (*get)(struct Dat1BuildCache*);
    void (*set)(struct Dat1BuildCache*, struct RSCache_FileListDat*);
};

static struct Dat1PreloadSlot const k_slots[] = {
    { "title",
      RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS,
      dat1_buildcache_get_title_fonts_jagfile,
      dat1_buildcache_set_title_fonts_jagfile },
    { "config",
      RSCACHE_DAT1_CONFIG_CONFIGS,
      dat1_buildcache_get_config_jagfile,
      dat1_buildcache_set_config_jagfile },
    { "media",
      RSCACHE_DAT1_CONFIG_MEDIA_2D,
      dat1_buildcache_get_media_2d_graphics_jagfile,
      dat1_buildcache_set_media_2d_graphics_jagfile },
    { "textures",
      RSCACHE_DAT1_CONFIG_TEXTURES,
      dat1_buildcache_get_textures_jagfile,
      dat1_buildcache_set_textures_jagfile },
    { "versionlist",
      RSCACHE_DAT1_CONFIG_VERSION_LIST,
      dat1_buildcache_get_versionlist_jagfile,
      dat1_buildcache_set_versionlist_jagfile },
    /* The sound bank is derived from this one on demand, so there is no
     * jagfile getter to test -- it is fetched once and handed over. */
    { "sounds", RSCACHE_DAT1_CONFIG_SOUND_EFFECTS, NULL, dat1_buildcache_set_sounds_jagfile },
};

static struct Dat1PreloadSlot const*
slot_for(char const* name)
{
    assert(name);
    for( size_t i = 0; i < sizeof(k_slots) / sizeof(k_slots[0]); i++ )
    {
        if( strcmp(k_slots[i].name, name) == 0 )
            return &k_slots[i];
    }
    return NULL;
}

/*
 * Which on-demand pass a kind=ondemand step names.
 *
 * By the step's archive name, matching the vocabulary of the reference's own
 * passes. "prefetch" is the web lane's bulk /ondemand.zip download into
 * IndexedDB (OnDemand.prefetchAll) -- a native client warms its write-back
 * cache through the per-type passes below instead, so the step is announced
 * as recognised and performed by them.
 */
enum Dat1OndemandPass
{
    DAT1_ONDEMAND_PASS_NONE = 0,
    DAT1_ONDEMAND_PASS_PREFETCH,
    DAT1_ONDEMAND_PASS_ANIMS,
    DAT1_ONDEMAND_PASS_MODELS,
    DAT1_ONDEMAND_PASS_MAPS,
};

static enum Dat1OndemandPass
ondemand_pass_for(char const* archive)
{
    assert(archive);
    if( archive[0] == '\0' )
        return DAT1_ONDEMAND_PASS_PREFETCH;
    if( strcmp(archive, "anims") == 0 )
        return DAT1_ONDEMAND_PASS_ANIMS;
    if( strcmp(archive, "models") == 0 )
        return DAT1_ONDEMAND_PASS_MODELS;
    if( strcmp(archive, "maps") == 0 )
        return DAT1_ONDEMAND_PASS_MAPS;
    return DAT1_ONDEMAND_PASS_NONE;
}

/* A prefetch wants the bytes cached, not decoded: take the arrived archive
 * off the slot and free it. The fetch itself is what warmed the on-demand
 * source's write-back cache. */
static void
io_slot_discard(struct ToriRS_IO* io)
{
    struct ToriRS_IOItem* item = ToriRS_IO_TaskSlot(io, 0);
    struct RSCache_Dat1DiskArchive* archive;

    assert(io);
    assert(item->kind == TORIRS_IOK_CACHE);
    archive = item->data;
    item->data = NULL;
    item->data_size = 0;
    ToriRS_IO_ClearItem(item);
    if( archive )
        RSCache_Dat1DiskArchiveFree(archive);
}

struct Task_Dat1Preload
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    struct RS_PreloadTable const* steps;
    struct RS_LoginReplyTable const* strings;
    /** Non-zero on a source=ondemand world; the kind=ondemand passes run only
     *  there. */
    int on_demand;

    int at;
    /*
     * The step being run and where its archive goes.
     *
     * On the task rather than in the loop because this protothread SUSPENDS
     * mid-loop, and a local does not survive that -- the resumed frame reads
     * whatever the stack happens to hold. Both are set before the yields and
     * read after them.
     */
    struct RS_PreloadStep const* step;
    struct Dat1PreloadSlot const* slot;

    /*
     * The on-demand pass in flight: which pass, the file cursor, how many
     * fetches it will make, how many have been made, and the last percentage
     * put on screen (so the render yield fires once per percent, not once per
     * file). All on the task for the same locals-do-not-survive reason.
     */
    enum Dat1OndemandPass pass;
    struct RSCache_Dat1VersionList* vl;
    int i;
    int total;
    int done;
    int shown_percent;
    /** The composed "words - N%" caption. It must outlive the frame it is
     *  drawn on, and a task local does not -- this buffer lives exactly as
     *  long as the render request that points at it. */
    char caption[128];
};

/* How many fetches a pass will make, so its caption can carry a running
 * percentage the way the reference's does ("Loading models - 41%"). */
static int
ondemand_pass_total(
    struct Task_Dat1Preload const* task,
    enum Dat1OndemandPass pass)
{
    assert(task);
    assert(task->vl);
    switch( pass )
    {
    case DAT1_ONDEMAND_PASS_ANIMS:
        return task->vl->anim_version_count;
    case DAT1_ONDEMAND_PASS_MODELS:
    {
        int total = 0;
        for( int i = 0; i < task->vl->model_index_count; i++ )
        {
            if( task->vl->model_index[i] & 0x1 )
                total++;
        }
        return total;
    }
    case DAT1_ONDEMAND_PASS_MAPS:
        /* Both halves of every square: terrain and locs. */
        return task->vl->map_squares ? task->vl->map_squares->squares_count * 2 : 0;
    default:
        return 0;
    }
}

static void
compose_caption(
    struct Task_Dat1Preload* task)
{
    char const* words = NULL;

    assert(task);
    assert(task->step);
    if( task->step->say[0] && task->strings )
        words = RS_LoginReplies_String(task->strings, task->step->say);
    if( !words )
    {
        task->caption[0] = '\0';
        return;
    }
    if( task->total > 0 )
        snprintf(
            task->caption,
            sizeof(task->caption),
            "%s - %d%%",
            words,
            task->done * 100 / task->total);
    else
        snprintf(task->caption, sizeof(task->caption), "%s", words);
}

static int
Task_Dat1Preload_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1Preload* task = (struct Task_Dat1Preload*)task_base;
    struct RSCache_FileListDat* jagfile = NULL;

    PT_BEGIN(&task->pt);

    for( task->at = 0; task->at < task->steps->count; task->at++ )
    {
        task->step = RS_Preload_At(task->steps, task->at);
        assert(task->step);

        if( task->step->kind == RS_PRELOAD_KIND_JAGFILE )
        {
            task->slot = slot_for(task->step->archive);
            if( !task->slot )
            {
                /* An archive the client keeps no slot for. The chat filter and
                 * the interface pack are both like this -- they are read
                 * straight through by their own loaders and never held whole --
                 * so there is nothing for a preload to warm, and saying so
                 * beats pretending the step ran. */
                TORIRS_LOG(
                    "preload: [preload:%s] names '%s', which this client does not hold whole\n",
                    task->step->name,
                    task->step->archive);
                continue;
            }

            if( task->slot->get && task->slot->get(task->bc) )
                continue;

            /*
             * Announce BEFORE the fetch: on an on-demand world this is a
             * network round trip, and the sentence has to name what is being
             * waited on rather than what already arrived.
             */
            task->total = 0;
            task->done = 0;
            compose_caption(task);
            if( task->step->render )
                TASK_YIELD_TO_RENDER(
                    &task->task,
                    &task->pt,
                    TORIRS_RENDER_BOOT_BAR,
                    task->step->percent,
                    task->caption);

            RSCache_IO_Dat1JagfileLoad(io, 0, task->slot->archive);
            PT_YIELD(&task->pt);

            jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, task->slot->archive);
            if( !jagfile )
            {
                /* A cache that does not carry this archive. Not fatal here:
                 * the loader that actually needs it will fail with a message
                 * about what it wanted, which is the more useful place to hear
                 * it. */
                TORIRS_ERR("preload: no jagfile for '%s'\n", task->step->archive);
                continue;
            }
            task->slot->set(task->bc, jagfile);
            continue;
        }

        if( task->step->kind != RS_PRELOAD_KIND_ONDEMAND )
            continue;

        /* The unpack and prepare steps the profile also lists are other
         * machinery's work; only the fetch passes are this task's. */

        /* A disk world has nothing to prefetch: every one of these files is
         * already a local read, and warming a cache with its own contents is
         * work with no product. The step simply is not performed. */
        if( !task->on_demand )
            continue;

        task->pass = ondemand_pass_for(task->step->archive);
        if( task->pass == DAT1_ONDEMAND_PASS_NONE )
        {
            TORIRS_ERR(
                "preload: [preload:%s] names no on-demand pass this client knows ('%s')\n",
                task->step->name,
                task->step->archive);
            continue;
        }
        if( task->pass == DAT1_ONDEMAND_PASS_PREFETCH )
        {
            /* The web lane's bulk zip. The per-type passes below are the
             * native shape of the same warm-up. */
            continue;
        }

        /* Every pass reads the version list the jagfile steps fetched above.
         * Decoded once, on first use, and kept for the passes that follow. */
        if( !task->vl )
        {
            struct RSCache_FileListDat* vljag =
                dat1_buildcache_get_versionlist_jagfile(task->bc);
            if( !vljag )
            {
                TORIRS_ERR(
                    "preload: [preload:%s] runs before any versionlist step fetched one\n",
                    task->step->name);
                continue;
            }
            task->vl = RSCache_Dat1VersionListNewFromJagfile(vljag);
            if( !task->vl )
            {
                TORIRS_ERR("preload: the versionlist jagfile did not decode\n");
                continue;
            }
        }

        task->total = ondemand_pass_total(task, task->pass);
        task->done = 0;
        task->shown_percent = 0;
        if( task->total == 0 )
            continue;

        /* Announce BEFORE the first fetch, at 0: the sentence on screen has
         * to name what is being waited on rather than what just arrived. */
        compose_caption(task);
        if( task->step->render )
            TASK_YIELD_TO_RENDER(
                &task->task,
                &task->pt,
                TORIRS_RENDER_BOOT_BAR,
                task->step->percent,
                task->caption);

        for( task->i = 0;; task->i++ )
        {
            /* Which file this iteration fetches, per pass. A pass that has
             * walked off its list ends here. */
            if( task->pass == DAT1_ONDEMAND_PASS_ANIMS )
            {
                if( task->i >= task->vl->anim_version_count )
                    break;
                RSCache_IO_Dat1AnimBaseFramesLoad(io, 0, task->i);
            }
            else if( task->pass == DAT1_ONDEMAND_PASS_MODELS )
            {
                if( task->i >= task->vl->model_index_count )
                    break;
                if( !(task->vl->model_index[task->i] & 0x1) )
                    continue;
                RSCache_IO_Dat1ModelLoad(io, 0, task->i);
            }
            else
            {
                struct RSCache_MapSquareCoord coord;

                assert(task->pass == DAT1_ONDEMAND_PASS_MAPS);
                /* Two fetches per square: even i is the terrain half, odd i
                 * the locs half of square i/2. */
                if( task->i >= task->vl->map_squares->squares_count * 2 )
                    break;
                RSCache_MapSquareCoord(
                    &coord, task->vl->map_squares->squares[task->i / 2].map_id);
                if( (task->i & 1) == 0 )
                    RSCache_IO_Dat1MapTerrainLoad(io, 0, coord.map_x, coord.map_z);
                else
                    RSCache_IO_Dat1MapSceneryLoad(io, 0, coord.map_x, coord.map_z);
            }

            PT_YIELD(&task->pt);
            io_slot_discard(io);
            task->done++;

            /* The reference names the pass once and then counts it up on
             * screen ("Loading models - 41%"). Once per percent, not once per
             * file: the render yield suspends until a frame has drawn, and a
             * thousand-file pass must not cost a thousand frames. */
            if( task->step->render &&
                task->done * 100 / task->total != task->shown_percent )
            {
                task->shown_percent = task->done * 100 / task->total;
                compose_caption(task);
                TASK_YIELD_TO_RENDER(
                    &task->task,
                    &task->pt,
                    TORIRS_RENDER_BOOT_BAR,
                    task->step->percent,
                    task->caption);
            }
        }
    }

    PT_END(&task->pt);
}

static void
Task_Dat1Preload_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat1Preload* task = (struct Task_Dat1Preload*)task_base;
    if( task->vl )
        RSCache_Dat1VersionListFree(task->vl);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat1Preload_VTable = {
    .run = Task_Dat1Preload_Run,
    .free = Task_Dat1Preload_Free,
};

struct ToriRS_Task*
CreateTask_Dat1Preload(
    struct CacheProvider* provider,
    struct RS_PreloadTable const* steps,
    struct RS_LoginReplyTable const* strings,
    int on_demand)
{
    struct Task_Dat1Preload* task;
    int work = 0;

    assert(provider);
    assert(steps);

    /* A profile listing nothing this task performs has nothing to do, and a
     * task that ends on its first step still costs a scheduling pass. */
    for( int i = 0; i < steps->count; i++ )
    {
        if( steps->steps[i].kind == RS_PRELOAD_KIND_JAGFILE &&
            slot_for(steps->steps[i].archive) )
            work++;
        if( steps->steps[i].kind == RS_PRELOAD_KIND_ONDEMAND && on_demand )
            work++;
    }
    if( work == 0 )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1Preload_VTable;
    strcpy(task->task.name, "Dat1Preload");
    task->bc = (struct Dat1BuildCache*)provider;
    task->steps = steps;
    task->strings = strings;
    task->on_demand = on_demand;
    return &task->task;
}
