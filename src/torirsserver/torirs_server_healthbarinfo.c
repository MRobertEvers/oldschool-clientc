/*
 * Healthbar widths from the cache (config group 33).
 *
 * One question, and it is the server's rather than the client's: when a HEADBAR
 * block says "fill", what is that a fraction OF? The answer is the healthbar
 * type's own opcode 14, and the client divides by it -- so a server that picks
 * a bar and then scales the fill by some other number draws a bar that is right
 * in width and wrong in length.
 *
 * That is not hypothetical: the fill used to be scaled by the content constant
 * `^healthbar_standard_width` (30) for every npc, which was correct only
 * because every npc was also being given the 30-wide standard bar. Choosing the
 * bar by size (see `npc_headbar_id`) makes the two numbers different, and this
 * table is what keeps them in step.
 *
 * WHY THE CACHE AND NOT CONTENT. The widths are cache facts -- the same bytes
 * `src/game/rs_healthbar.h` reads at the other end -- and there are now five of
 * them rather than one. Restating them in the content tree would be five
 * numbers that can silently disagree with the records they describe, and the
 * failure would look like a bar that never quite fills. The one constant that
 * did (`^healthbar_standard_width`) is retired for exactly that reason; see
 * server/scripts/player/configs/healthbar.constant.
 *
 * Only the width is kept. Sprites, padding, persistence and the fade are all
 * questions about DRAWING, which is the client's business; the server's only
 * stake in this record is the denominator.
 */

#include "mock230.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int* g_healthbar_widths;
static int g_healthbar_count;

void
mock230_healthbarinfo_free(void)
{
    free(g_healthbar_widths);
    g_healthbar_widths = NULL;
    g_healthbar_count = 0;
}

int
mock230_healthbar_width(int id)
{
    /* class381's constructor default. A cache with no group 33, an id no record
     * covers, or a record with no opcode 14 all mean the same thing to the
     * client, so they mean the same thing here. */
    if( !g_healthbar_widths || id < 0 || id >= g_healthbar_count )
        return MOCK230_HEALTHBAR_DEFAULT_WIDTH;
    return g_healthbar_widths[id];
}

int
mock230_healthbarinfo_count(void)
{
    return g_healthbar_count;
}

int
mock230_healthbarinfo_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int highest = -1;

    mock230_healthbarinfo_free();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = MOCK230_CACHE_REVISION;

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
        fprintf(stderr, "mock230: no healthbar metadata (cache '%s' not found)\n", cache_dir);
        return 0;
    }

    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_HEALTHBAR);
    if( !archive )
    {
        /* Not an error: the type is OldSchool-only, and every id then reports
         * the constructor default, which is what the server sent before this
         * table existed. */
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

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

    g_healthbar_count = highest + 1;
    g_healthbar_widths = (int*)malloc((size_t)g_healthbar_count * sizeof(*g_healthbar_widths));
    assert(g_healthbar_widths);
    for( int i = 0; i < g_healthbar_count; i++ )
        g_healthbar_widths[i] = MOCK230_HEALTHBAR_DEFAULT_WIDTH;

    for( int i = 0; i < archive->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigHealthbar entry;

        if( id < 0 || id >= g_healthbar_count || files->file_sizes[i] <= 0 )
            continue;
        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigHealthbarDecodeInplace(&entry, files->files[i], files->file_sizes[i]);
        /* Presence, not value: an absent opcode 14 must keep the default, and
         * the decoder leaves the field at 0 either way. */
        if( entry.has_width && entry.width > 0 )
            g_healthbar_widths[id] = entry.width;
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return g_healthbar_count;
}
