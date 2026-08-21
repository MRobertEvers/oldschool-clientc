/*
 * The plugin layer's trips across the platform seam: the script manifest, the
 * scripts themselves, and the settings file.
 *
 * Tasks rather than fopen calls for the reason every other loader here is one:
 * the platform owns the disk, the IO queue is how anything asks it for work,
 * and the browser lane has no synchronous read at all -- PlatformX_IO_LoadItem
 * fails loudly there rather than pretending. A plugin system built on fopen
 * would be a native-only plugin system.
 *
 * Boot order matters and is the whole reason this is one task rather than
 * three:
 *
 *   1. the manifest names the scripts,
 *   2. each script is read and registered as a plugin,
 *   3. only then is the settings file decoded -- a section whose plugin has
 *      not registered yet would be applied to nothing, and a script would
 *      spend its first frames on defaults before jumping when its saved
 *      values arrived,
 *   4. and only then does anything start.
 */

#include "plugin/task_plugin_io.h"

#include "asyncio.h"
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_lua.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One slot, one item, no overlap: this task issues a single request at a time
 * and parks on it, the way CreateTask_PrefsLoad does. */
#define PLUGIN_IO_SLOT 0

#define PLUGIN_BOOT_MAX_SCRIPTS 32
#define PLUGIN_BOOT_NAME_MAX 64

char const*
PluginPrefs_Path(void)
{
    char const* configured = getenv("TORIRS_PLUGIN_PREFS");

    if( !configured )
        return PLUGIN_PREFS_DEFAULT_PATH;
    /* Empty means "do not touch the filesystem", matching TORIRS_PREFS: a
     * headless run must not inherit the last one's plugin state, and must not
     * leave a file behind either. */
    return *configured ? configured : NULL;
}

char const*
PluginManifest_Path(void)
{
    char const* configured = getenv("TORIRS_PLUGIN_MANIFEST");

    if( !configured )
        return PLUGIN_MANIFEST_DEFAULT_PATH;
    return *configured ? configured : NULL;
}

struct PluginBootEntry
{
    char name[PLUGIN_BOOT_NAME_MAX];
    char source[TORIRS_IOITEM_MAX_PATH];
    int enabled;
};

struct Task_PluginBoot
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_PluginHost* host;
    char manifest_path[TORIRS_IOITEM_MAX_PATH];
    char prefs_path[TORIRS_IOITEM_MAX_PATH];
    struct PluginBootEntry entries[PLUGIN_BOOT_MAX_SCRIPTS];
    int entry_count;
    int at;
};

/*
 * Parse the manifest: [plugin:name] sections carrying `source=` and an
 * optional `enabled=`.
 *
 * Hand-written rather than routed through 3rd/ini for the same reason the
 * config store is: this runs against a memory image the IO queue delivered,
 * not a path, and it has to tolerate keys it does not recognise rather than
 * failing the boot over one.
 */
static void
plugin_manifest_parse(struct Task_PluginBoot* task, char const* data, int size)
{
    char const* p = data;
    char const* end = data + size;
    int current = -1;

    assert(task);
    assert(data);

    while( p < end && task->entry_count < PLUGIN_BOOT_MAX_SCRIPTS )
    {
        char const* line = p;
        char const* line_end;

        while( p < end && *p != '\n' )
            p++;
        line_end = p;
        if( p < end )
            p++;

        while( line < line_end && (*line == ' ' || *line == '\t' || *line == '\r') )
            line++;
        while( line_end > line &&
               (line_end[-1] == ' ' || line_end[-1] == '\t' || line_end[-1] == '\r') )
            line_end--;
        if( line == line_end || *line == ';' || *line == '#' )
            continue;

        if( *line == '[' )
        {
            char const* close = line;
            char const* name;
            int len;

            while( close < line_end && *close != ']' )
                close++;
            if( close >= line_end )
                continue;

            name = line + 1;
            for( char const* c = name; c < close; c++ )
            {
                if( *c == ':' )
                {
                    name = c + 1;
                    break;
                }
            }
            len = (int)(close - name);
            if( len <= 0 )
                continue;
            if( len >= PLUGIN_BOOT_NAME_MAX )
                len = PLUGIN_BOOT_NAME_MAX - 1;

            current = task->entry_count++;
            memset(&task->entries[current], 0, sizeof(task->entries[current]));
            memcpy(task->entries[current].name, name, (size_t)len);
            task->entries[current].name[len] = '\0';
            task->entries[current].enabled = 1;
            continue;
        }

        if( current < 0 )
            continue;

        {
            char const* eq = line;
            char const* val;
            char key[32];
            int klen;
            int vlen;

            while( eq < line_end && *eq != '=' )
                eq++;
            if( eq >= line_end )
                continue;

            klen = (int)(eq - line);
            while( klen > 0 && (line[klen - 1] == ' ' || line[klen - 1] == '\t') )
                klen--;
            if( klen >= (int)sizeof(key) )
                klen = (int)sizeof(key) - 1;
            memcpy(key, line, (size_t)klen);
            key[klen] = '\0';

            val = eq + 1;
            while( val < line_end && (*val == ' ' || *val == '\t') )
                val++;
            vlen = (int)(line_end - val);
            if( vlen < 0 )
                vlen = 0;

            if( strcmp(key, "source") == 0 )
            {
                if( vlen >= TORIRS_IOITEM_MAX_PATH )
                    vlen = TORIRS_IOITEM_MAX_PATH - 1;
                memcpy(task->entries[current].source, val, (size_t)vlen);
                task->entries[current].source[vlen] = '\0';
            }
            else if( strcmp(key, "enabled") == 0 )
            {
                task->entries[current].enabled = (vlen > 0 && val[0] != '0');
            }
        }
    }
}

static int
Task_PluginBoot_Run(struct ToriRS_Task* task_base, struct ToriRS_IO* io)
{
    struct Task_PluginBoot* task = (struct Task_PluginBoot*)task_base;
    struct ToriRS_IOItem* item;

    PT_BEGIN(&task->pt);

    /* 1. The manifest. Absent is the ordinary case -- a client with no scripts
     *    installed -- so it is not reported. */
    if( task->manifest_path[0] )
    {
        ToriRS_IO_QueueScript(io, PLUGIN_IO_SLOT, task->manifest_path);
        PT_YIELD(&task->pt);

        item = &io->io_slots[PLUGIN_IO_SLOT];
        if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
            plugin_manifest_parse(
                task, (char const*)IOITEM_DATA(item), IOITEM_DATA_SIZE(item));
        ToriRS_IO_ClearItem(item);
    }

    /* 2. Each script, in manifest order. A script that fails to read or fails
     *    to compile is reported and skipped: one broken plugin must not stop
     *    the others, or the client, from starting. */
    for( task->at = 0; task->at < task->entry_count; task->at++ )
    {
        if( !task->entries[task->at].source[0] )
        {
            fprintf(
                stderr,
                "plugin: manifest entry '%s' has no source=, skipped\n",
                task->entries[task->at].name);
            continue;
        }

        ToriRS_IO_QueueScript(io, PLUGIN_IO_SLOT, task->entries[task->at].source);
        PT_YIELD(&task->pt);

        item = &io->io_slots[PLUGIN_IO_SLOT];
        if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
        {
            int const index = PluginLua_AddScript(
                task->host,
                task->entries[task->at].name,
                (char const*)IOITEM_DATA(item),
                IOITEM_DATA_SIZE(item));
            /* The manifest's own enabled=0 is applied here rather than left to
             * the settings file: it is the author's switch, and it has to work
             * for a script that has never been run and so has no saved state
             * at all. */
            if( index >= 0 && !task->entries[task->at].enabled )
                PluginHost_SetEnabled(task->host, index, false);
        }
        else
        {
            fprintf(
                stderr,
                "plugin: could not read script '%s' for '%s'\n",
                task->entries[task->at].source,
                task->entries[task->at].name);
        }
        ToriRS_IO_ClearItem(item);
    }

    /* 3. Settings, now that every plugin that owns a section exists. */
    if( task->prefs_path[0] )
    {
        ToriRS_IO_QueueFileRead(io, PLUGIN_IO_SLOT, task->prefs_path);
        PT_YIELD(&task->pt);

        item = &io->io_slots[PLUGIN_IO_SLOT];
        if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
            PluginHost_ConfigDecode(task->host, IOITEM_DATA(item), IOITEM_DATA_SIZE(item));
        /* No file is a first launch, not an error. */
        ToriRS_IO_ClearItem(item);
    }

    /* 4. Start. Decoding above may have disabled some, which Start honours. */
    PluginHost_Start(task->host);
    /* Nothing has changed relative to what is on disk, and the decode itself
     * must not be what triggers a rewrite. */
    PluginHost_ConfigClearDirty(task->host);

    /*
     * One line saying what is actually running.
     *
     * A plugin layer that loads silently is one where "my script does nothing"
     * and "my script never loaded" look identical from the outside, which is
     * the first thing anyone writing one needs to tell apart. Disabled
     * plugins are listed too, with their reason, for the same reason.
     */
    {
        int const count = PluginHost_Count(task->host);
        int enabled = 0;

        for( int i = 0; i < count; i++ )
            enabled += PluginHost_IsEnabled(task->host, i) ? 1 : 0;

        fprintf(stderr, "plugin: %d of %d enabled", enabled, count);
        for( int i = 0; i < count; i++ )
        {
            char const* error = PluginHost_Error(task->host, i);
            fprintf(
                stderr,
                "%s%s%s",
                i == 0 ? " -- " : ", ",
                PluginHost_Name(task->host, i),
                PluginHost_IsEnabled(task->host, i) ? "" : " (off)");
            if( error )
                fprintf(stderr, " [%s]", error);
        }
        fputc('\n', stderr);
    }

    PT_END(&task->pt);
}

static struct ToriRS_TaskVTable Task_PluginBoot_VTable = {
    .run = Task_PluginBoot_Run,
    .free = NULL,
};

struct ToriRS_Task*
CreateTask_PluginBoot(
    struct ToriRS_PluginHost* host,
    char const* manifest_path,
    char const* prefs_path)
{
    struct Task_PluginBoot* task;

    assert(host);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PluginBoot_VTable;
    strcpy(task->task.name, "PluginBoot");
    task->host = host;
    if( manifest_path )
        snprintf(task->manifest_path, sizeof(task->manifest_path), "%s", manifest_path);
    if( prefs_path )
        snprintf(task->prefs_path, sizeof(task->prefs_path), "%s", prefs_path);
    PT_INIT(&task->pt);
    return &task->task;
}

/* -------------------------------------------------------------- the assets */

/*
 * A plugin's own file, read or written.
 *
 * Two IO kinds and not one, because the two roots are genuinely different
 * things to the platform: the shipped copy is part of the install and is
 * resolved under the script directory (which the browser lane serves over the
 * network), while the saved copy is the player's and is named verbatim. A read
 * tries the saved copy first so that a plugin can ship a default and then
 * overwrite it without the name changing underneath it.
 */

struct Task_PluginAssetRead
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_PluginHost* host;
    char plugin[PLUGIN_BOOT_NAME_MAX];
    char asset[TORIRS_IOITEM_MAX_PATH];
    char saved_path[TORIRS_IOITEM_MAX_PATH];
    char shipped_path[TORIRS_IOITEM_MAX_PATH];
};

/* Hand the item's buffer to the host and detach it, so the ClearItem below
 * does not free bytes the host now owns. */
static void
plugin_asset_take(struct Task_PluginAssetRead* task, struct ToriRS_IOItem* item)
{
    assert(task);
    assert(item);
    PluginHost_AssetDeliver(
        task->host, task->plugin, task->asset, IOITEM_DATA(item), IOITEM_DATA_SIZE(item));
    IOITEM_DATA(item) = NULL;
    IOITEM_DATA_SIZE(item) = 0;
}

static int
Task_PluginAssetRead_Run(struct ToriRS_Task* task_base, struct ToriRS_IO* io)
{
    struct Task_PluginAssetRead* task = (struct Task_PluginAssetRead*)task_base;
    struct ToriRS_IOItem* item;

    PT_BEGIN(&task->pt);

    if( task->saved_path[0] )
    {
        ToriRS_IO_QueueFileRead(io, PLUGIN_IO_SLOT, task->saved_path);
        PT_YIELD(&task->pt);

        item = &io->io_slots[PLUGIN_IO_SLOT];
        if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
        {
            plugin_asset_take(task, item);
            ToriRS_IO_ClearItem(item);
            PT_EXIT(&task->pt);
        }
        /* No saved copy is the ordinary case on a first run, not an error. */
        ToriRS_IO_ClearItem(item);
    }

    ToriRS_IO_QueueScript(io, PLUGIN_IO_SLOT, task->shipped_path);
    PT_YIELD(&task->pt);

    item = &io->io_slots[PLUGIN_IO_SLOT];
    if( IOITEM_ERROR_CODE(item) == 0 && IOITEM_DATA(item) )
        plugin_asset_take(task, item);
    else
    {
        /*
         * Reported as a failure rather than left silent. A plugin that asked
         * for an asset is waiting on the event, and "the file is not there" is
         * the answer it needs -- an asset_load that simply never comes back is
         * indistinguishable from one still in flight.
         */
        fprintf(
            stderr,
            "plugin: %s asset '%s' not found (%s, %s)\n",
            task->plugin,
            task->asset,
            task->saved_path[0] ? task->saved_path : "(no saved path)",
            task->shipped_path);
        PluginHost_AssetDeliver(task->host, task->plugin, task->asset, NULL, 0);
    }
    ToriRS_IO_ClearItem(item);

    PT_END(&task->pt);
}

static struct ToriRS_TaskVTable Task_PluginAssetRead_VTable = {
    .run = Task_PluginAssetRead_Run,
    .free = NULL,
};

struct ToriRS_Task*
CreateTask_PluginAssetRead(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* asset_name,
    char const* saved_path)
{
    struct Task_PluginAssetRead* task;

    assert(host);
    assert(plugin_name);
    assert(asset_name);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PluginAssetRead_VTable;
    strcpy(task->task.name, "PluginAssetRead");
    task->host = host;
    snprintf(task->plugin, sizeof(task->plugin), "%s", plugin_name);
    snprintf(task->asset, sizeof(task->asset), "%s", asset_name);
    if( saved_path )
        snprintf(task->saved_path, sizeof(task->saved_path), "%s", saved_path);
    snprintf(
        task->shipped_path,
        sizeof(task->shipped_path),
        "%s/%s/%s",
        PLUGIN_ASSET_SHIPPED_DIR,
        plugin_name,
        asset_name);
    PT_INIT(&task->pt);
    return &task->task;
}

struct Task_PluginAssetWrite
{
    struct ToriRS_Task task;
    struct pt pt;
    char path[TORIRS_IOITEM_MAX_PATH];
    /* Owned, for the reason Task_PluginSave's copy is: the IO item borrows it
     * across the yield, and the plugin's buffer is not ours to keep. */
    void* data;
    int size;
};

static int
Task_PluginAssetWrite_Run(struct ToriRS_Task* task_base, struct ToriRS_IO* io)
{
    struct Task_PluginAssetWrite* task = (struct Task_PluginAssetWrite*)task_base;
    struct ToriRS_IOItem* item;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileWrite(io, PLUGIN_IO_SLOT, task->path, task->data, task->size);
    PT_YIELD(&task->pt);

    item = &io->io_slots[PLUGIN_IO_SLOT];
    if( IOITEM_ERROR_CODE(item) != 0 )
        fprintf(stderr, "plugin: could not write asset to %s\n", task->path);
    ToriRS_IO_ClearItem(item);

    PT_END(&task->pt);
}

static void
Task_PluginAssetWrite_Free(struct ToriRS_Task* task_base)
{
    struct Task_PluginAssetWrite* task = (struct Task_PluginAssetWrite*)task_base;
    free(task->data);
    task->data = NULL;
}

static struct ToriRS_TaskVTable Task_PluginAssetWrite_VTable = {
    .run = Task_PluginAssetWrite_Run,
    .free = Task_PluginAssetWrite_Free,
};

struct ToriRS_Task*
CreateTask_PluginAssetWrite(
    char const* saved_path,
    void const* data,
    int size)
{
    struct Task_PluginAssetWrite* task;

    assert(saved_path && *saved_path);
    assert(data || size == 0);
    assert(size >= 0);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PluginAssetWrite_VTable;
    strcpy(task->task.name, "PluginAssetWrite");
    snprintf(task->path, sizeof(task->path), "%s", saved_path);
    task->size = size;
    if( size > 0 )
    {
        task->data = malloc((size_t)size);
        assert(task->data);
        memcpy(task->data, data, (size_t)size);
    }
    PT_INIT(&task->pt);
    return &task->task;
}

/* --------------------------------------------------------------- the save */

struct Task_PluginSave
{
    struct ToriRS_Task task;
    struct pt pt;
    char path[TORIRS_IOITEM_MAX_PATH];
    /* The encoded image, snapshotted at CREATION time rather than at the
     * yield: the store keeps changing while a slider is being dragged, and the
     * bytes the platform is handed have to be the ones this save was asked
     * for. Borrowed by the IO item, so it must outlive the yield -- which is
     * exactly why it is owned here and freed in _Free. */
    void* data;
    int size;
};

static int
Task_PluginSave_Run(struct ToriRS_Task* task_base, struct ToriRS_IO* io)
{
    struct Task_PluginSave* task = (struct Task_PluginSave*)task_base;
    struct ToriRS_IOItem* item;

    PT_BEGIN(&task->pt);

    ToriRS_IO_QueueFileWrite(io, PLUGIN_IO_SLOT, task->path, task->data, task->size);
    PT_YIELD(&task->pt);

    item = &io->io_slots[PLUGIN_IO_SLOT];
    if( IOITEM_ERROR_CODE(item) != 0 )
        fprintf(stderr, "plugin: could not write settings to %s\n", task->path);
    ToriRS_IO_ClearItem(item);

    PT_END(&task->pt);
}

static void
Task_PluginSave_Free(struct ToriRS_Task* task_base)
{
    struct Task_PluginSave* task = (struct Task_PluginSave*)task_base;
    free(task->data);
    task->data = NULL;
}

static struct ToriRS_TaskVTable Task_PluginSave_VTable = {
    .run = Task_PluginSave_Run,
    .free = Task_PluginSave_Free,
};

struct ToriRS_Task*
CreateTask_PluginSave(struct ToriRS_PluginHost const* host, char const* path)
{
    struct Task_PluginSave* task;

    assert(host);
    assert(path && *path);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PluginSave_VTable;
    strcpy(task->task.name, "PluginSave");
    snprintf(task->path, sizeof(task->path), "%s", path);
    PluginHost_ConfigEncode(host, &task->data, &task->size);
    PT_INIT(&task->pt);
    return &task->task;
}
