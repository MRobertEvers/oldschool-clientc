/*
 * NPC metadata from the cache: names, and the menu ops the server may act on.
 *
 * Same recipe as mock230_objinfo.c — profile, CONFIGS table, KIND_NPC archive,
 * file list, decode each — and worth keeping separate for the same reason: it
 * is the only place that knows the cache's npc layout.
 *
 * The immediate consumer is npc_say. NPCs have no overhead text in this client
 * (World_NpcSetChat writes a field nothing reads), so the mock renders npc
 * speech as a chatbox line, which needs the speaker's name.
 */

#include "mock230.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Mock230NpcInfo* g_npcs;
static int g_npc_count;

static const struct Mock230NpcInfo k_unknown = { NULL, -1, 0 };

int
mock230_npcinfo_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int highest = -1;

    mock230_npcinfo_free();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = 230;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Run from src/ as well as from the repo root, like objinfo. */
        char fallback[512];

        snprintf(fallback, sizeof(fallback), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(fallback);
    }
    if( !disk )
    {
        fprintf(stderr, "mock230: no npc metadata (cache '%s' not found)\n", cache_dir);
        return 0;
    }

    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_NPC);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        fprintf(stderr, "mock230: no npc config archive in '%s'\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_NPC, archive->revision);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    /* File ids are sparse, so the table is sized from the largest id rather
     * than from the file count. */
    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] > highest )
            highest = archive->file_ids[i];
    }
    if( highest < 0 )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    g_npc_count = highest + 1;
    g_npcs = (struct Mock230NpcInfo*)calloc((size_t)g_npc_count, sizeof(*g_npcs));
    if( !g_npcs )
    {
        g_npc_count = 0;
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    for( int i = 0; i < g_npc_count; i++ )
    {
        g_npcs[i].name = NULL;
        g_npcs[i].combat_level = -1;
        g_npcs[i].size = 1;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigNpc* npc;

        if( id < 0 || id >= g_npc_count )
            continue;
        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(
            &profile, files->files[i], files->file_sizes[i]);
        if( !npc )
            continue;

        if( npc->name && strcmp(npc->name, "null") != 0 )
            g_npcs[id].name = strdup(npc->name);
        g_npcs[id].combat_level = npc->combat_level;
        g_npcs[id].size = npc->size > 0 ? npc->size : 1;
        RSCache_Dat2ConfigNpcFree(npc);
    }

    /* Read the count before the free, not after: the archive owns it. The
     * first version of this printed "0 records" from freed memory while the
     * table itself was fine. */
    {
        int loaded = archive->file_count;

        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);

        fprintf(stderr, "mock230: npc metadata loaded (%d records from %s)\n", loaded,
                cache_dir);
    }
    return 1;
}

void
mock230_npcinfo_free(void)
{
    if( g_npcs )
    {
        for( int i = 0; i < g_npc_count; i++ )
            free((void*)g_npcs[i].name);
        free(g_npcs);
    }
    g_npcs = NULL;
    g_npc_count = 0;
}

const struct Mock230NpcInfo*
mock230_npcinfo(int npc_id)
{
    static struct Mock230NpcInfo placeholder;

    if( g_npcs && npc_id >= 0 && npc_id < g_npc_count && g_npcs[npc_id].name )
        return &g_npcs[npc_id];

    /* Never NULL: a name is used in player-facing text, so an unknown id has to
     * render as something rather than crash the message that would have told
     * you which id it was. */
    placeholder = k_unknown;
    placeholder.name = "Someone";
    return &placeholder;
}
