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
 * Which archives, in what order, at what percentage and under what caption is
 * the profile's ([preload:] with kind=jagfile). This knows only how to fetch
 * one and where to put it.
 */
#include "engine/dat1/dat1_tasks.h"

#include "engine/dat1/dat1_buildcache.h"
#include "game/rs_login_replies.h"
#include "game/rs_preload.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
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

struct Task_Dat1Preload
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    struct RS_PreloadTable const* steps;
    struct RS_LoginReplyTable const* strings;

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
};

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

        /* Only the archive steps are this task's; the unpack and on-demand
         * passes the profile also lists are other machinery's work. */
        if( task->step->kind != RS_PRELOAD_KIND_JAGFILE )
            continue;

        task->slot = slot_for(task->step->archive);
        if( !task->slot )
        {
            /* An archive the client keeps no slot for. The chat filter and the
             * interface pack are both like this -- they are read straight
             * through by their own loaders and never held whole -- so there is
             * nothing for a preload to warm, and saying so beats pretending
             * the step ran. */
            TORIRS_LOG(
                "preload: [preload:%s] names '%s', which this client does not hold whole\n",
                task->step->name,
                task->step->archive);
            continue;
        }

        if( task->slot->get && task->slot->get(task->bc) )
            continue;

        /*
         * Announce BEFORE the fetch: on an on-demand world this is a network
         * round trip, and the sentence has to name what is being waited on
         * rather than what already arrived.
         *
         * The caption is the string table's own storage, which outlives every
         * frame it is drawn on -- no copy needed, and no buffer to size.
         */
        if( task->step->render )
        {
            char const* words = NULL;

            if( task->step->say[0] && task->strings )
                words = RS_LoginReplies_String(task->strings, task->step->say);
            TASK_YIELD_TO_RENDER(
                &task->task,
                &task->pt,
                TORIRS_RENDER_BOOT_BAR,
                task->step->percent,
                words);
        }

        RSCache_IO_Dat1JagfileLoad(io, 0, task->slot->archive);
        PT_YIELD(&task->pt);

        jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, task->slot->archive);
        if( !jagfile )
        {
            /* A cache that does not carry this archive. Not fatal here: the
             * loader that actually needs it will fail with a message about
             * what it wanted, which is the more useful place to hear it. */
            TORIRS_ERR("preload: no jagfile for '%s'\n", task->step->archive);
            continue;
        }
        task->slot->set(task->bc, jagfile);
    }

    PT_END(&task->pt);
}

static void
Task_Dat1Preload_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1Preload_VTable = {
    .run = Task_Dat1Preload_Run,
    .free = Task_Dat1Preload_Free,
};

struct ToriRS_Task*
CreateTask_Dat1Preload(
    struct CacheProvider* provider,
    struct RS_PreloadTable const* steps,
    struct RS_LoginReplyTable const* strings)
{
    struct Task_Dat1Preload* task;
    int archives = 0;

    assert(provider);
    assert(steps);

    /* A profile listing no archives has nothing for this task to do, and a
     * task that ends on its first step still costs a scheduling pass. */
    for( int i = 0; i < steps->count; i++ )
    {
        if( steps->steps[i].kind == RS_PRELOAD_KIND_JAGFILE &&
            slot_for(steps->steps[i].archive) )
            archives++;
    }
    if( archives == 0 )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1Preload_VTable;
    strcpy(task->task.name, "Dat1Preload");
    task->bc = (struct Dat1BuildCache*)provider;
    task->steps = steps;
    task->strings = strings;
    return &task->task;
}
