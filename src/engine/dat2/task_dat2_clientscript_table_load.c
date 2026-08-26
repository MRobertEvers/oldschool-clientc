#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * The clientscript index's reference table.
 *
 * Loaded for one reason: every group in it carries an IDENTIFIER, and the
 * cache addresses its client-trigger scripts through that and nothing else.
 * See game/rs_client_trigger.h.
 *
 * One archive read, once, at boot. The alternative is resolving a name on
 * demand, which would make the first npc of every type on screen wait a frame
 * for a table read -- and the trigger it is waiting for is the one that draws
 * its indicator.
 */

struct Task_Dat2ClientScriptTableLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
};

static int
Task_Dat2ClientScriptTableLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ClientScriptTableLoad* task =
        (struct Task_Dat2ClientScriptTableLoad*)task_base;
    struct RSCache_ReferenceTable* table = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_TABLE_CLIENTSCRIPT);
    PT_YIELD(&task->pt);

    table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
    if( !table )
    {
        TORIRS_ERR("Failed to load clientscript reference table\n");
        PT_EXIT(&task->pt);
    }
    dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_TABLE_CLIENTSCRIPT, table);

    PT_END(&task->pt);
}

static void
Task_Dat2ClientScriptTableLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ClientScriptTableLoad_VTable = {
    .run = Task_Dat2ClientScriptTableLoad_Run,
    .free = Task_Dat2ClientScriptTableLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ClientScriptTableLoad(struct CacheProvider* provider)
{
    struct Dat2BuildCache* bc;
    struct Task_Dat2ClientScriptTableLoad* task;

    assert(provider);

    bc = (struct Dat2BuildCache*)provider;
    if( dat2_buildcache_reference_table_has(bc, RSCACHE_DAT2_TABLE_CLIENTSCRIPT) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ClientScriptTableLoad_VTable;
    strcpy(task->task.name, "Dat2ClientScriptTableLoad");
    task->bc = bc;
    PT_INIT(&task->pt);
    return &task->task;
}

/*
 * name hash -> group id.
 *
 * A linear scan of the table would be 9,725 comparisons, and the trigger
 * lookup runs three of them per npc that walks on screen and per loc the scene
 * builder places -- tens of thousands of times during one region load. So the
 * pairs are sorted once, on the first lookup after the table lands, and
 * answered by binary search after that. The table is immutable once decoded,
 * so there is nothing to keep in step.
 */

static int
dat2_clientscript_name_cmp(void const* a, void const* b)
{
    int const x = ((int const*)a)[0];
    int const y = ((int const*)b)[0];
    return x < y ? -1 : (x > y ? 1 : 0);
}

int
dat2_clientscript_id_by_name_hash(struct CacheProvider* provider, int name_hash)
{
    struct Dat2BuildCache* bc;
    struct RSCache_ReferenceTable* table;

    assert(provider);

    bc = (struct Dat2BuildCache*)provider;
    table = dat2_buildcache_reference_table_get(bc, RSCACHE_DAT2_TABLE_CLIENTSCRIPT);
    if( !table )
        return -1;

    if( !bc->clientscript_names )
    {
        bc->clientscript_names = malloc(sizeof(*bc->clientscript_names) * 2 *
                                        (size_t)(table->archive_count > 0 ? table->archive_count : 1));
        assert(bc->clientscript_names);
        for( int i = 0; i < table->archive_count; i++ )
        {
            bc->clientscript_names[i * 2 + 0] = RSCache_ReferenceTableIdentifier(table, i);
            bc->clientscript_names[i * 2 + 1] =
                RSCache_ReferenceTableHasArchive(table, i) ? i : -1;
        }
        bc->clientscript_name_count = table->archive_count;
        qsort(
            bc->clientscript_names,
            (size_t)bc->clientscript_name_count,
            sizeof(int) * 2,
            dat2_clientscript_name_cmp);
    }

    {
        int const key[2] = { name_hash, 0 };
        int const* found = bsearch(
            key,
            bc->clientscript_names,
            (size_t)bc->clientscript_name_count,
            sizeof(int) * 2,
            dat2_clientscript_name_cmp);
        return found ? found[1] : -1;
    }
}
