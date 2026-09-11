/*
 * Bake an equipment-bonus table for the item-stats plugin to ship.
 *
 * An OldSchool obj record keeps its twelve combat bonuses in its own param
 * block, so a client running an OldSchool cache needs no table at all -- it
 * reads the record. A DAT1 cache (2004-2005 RS2, and every LostCity world this
 * client boots) states none of it: no params, no wearpos, nothing. The bonuses
 * were the SERVER's there, and the equipment screen was text the server sent.
 *
 * So for those revisions the numbers have to be shipped, exactly as RuneLite
 * ships item_stats.json -- and the honest source for them is an OldSchool
 * cache, which is the same game's own answer for an item of that name. The
 * table is keyed by NAME for that reason: an id moved between 2004 and 2024,
 * and a rune scimitar did not.
 *
 * Usage:
 *   item_bonus_bake --rev NAME <cache_dir> <out_file>
 *
 * Only records that are WORN and state at least one bonus are written; the
 * rest of the game's 30,000 objs are not equipment and would be 30,000 lines
 * of zeroes. The first record for a name wins, which is what makes the four
 * charge levels of a glory one row.
 */

#include "rscache.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cache param ids. 0..11 are the twelve bonuses in the order the equipment
 * screen lists them; 14 is the attack rate in ticks; ranged strength lives in
 * two places and no record states both (12 on ammunition and thrown weapons,
 * 189 on everything else). */
#define PARAM_BONUS_COUNT 12
#define PARAM_RANGED_STRENGTH_AMMO 12
#define PARAM_ATTACK_RATE 14
#define PARAM_RANGED_STRENGTH 189

/* Equipment slots, in the wearpos numbering an objtype states. */
#define WEARPOS_WEAPON 3
#define WEARPOS_SHIELD 5

struct BakeRow
{
    char name[64];
    int slot;
    int two_handed;
    int bonus[PARAM_BONUS_COUNT];
    int ranged_strength;
    int attack_rate;
};

static struct BakeRow* g_rows;
static int g_row_count;
static int g_row_cap;

static int
name_seen(const char* name)
{
    for( int i = 0; i < g_row_count; i++ )
        if( strcmp(g_rows[i].name, name) == 0 )
            return 1;
    return 0;
}

static void
row_add(const struct BakeRow* row)
{
    if( g_row_count == g_row_cap )
    {
        g_row_cap = g_row_cap ? g_row_cap * 2 : 256;
        g_rows = realloc(g_rows, (size_t)g_row_cap * sizeof(*g_rows));
        assert(g_rows);
    }
    g_rows[g_row_count++] = *row;
}

/* Lowercased, because the plugin's lookup is case-insensitive and doing it
 * here means it is not done 2,800 times at load. Everything else the plugin's
 * own normalizer does -- the dose suffix, the fraction word -- is left alone,
 * so that one implementation decides what a name means. */
static void
name_lower(const char* in, char* out, size_t out_size)
{
    size_t at = 0;
    for( ; in[at] && at + 1 < out_size; at++ )
        out[at] = (char)tolower((unsigned char)in[at]);
    out[at] = '\0';
}

static int
collect_from_archive(
    const struct RSCache* profile,
    const struct RSCache_RecordAddress* addr,
    struct RSCache_Dat2DiskArchive* archive)
{
    struct RSCache local = *profile;
    struct RSCache_FileList* files;
    int written = 0;

    assert(archive);
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_OBJ, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
        return 0;

    for( int i = 0; i < files->file_count; i++ )
    {
        struct RSCache_Dat2ConfigObj* obj;
        struct BakeRow row;
        int stated = 0;

        if( files->file_sizes[i] <= 0 )
            continue;
        obj = RSCache_Dat2ConfigObjNewDecodeProfile(
            &local, files->files[i], files->file_sizes[i]);
        if( !obj )
            continue;

        memset(&row, 0, sizeof(row));
        row.attack_rate = -1;
        row.slot = obj->wearpos_1;
        row.two_handed = obj->wearpos_1 == WEARPOS_WEAPON &&
                         (obj->wearpos_2 == WEARPOS_SHIELD || obj->wearpos_3 == WEARPOS_SHIELD);

        for( int p = 0; p < obj->params.count; p++ )
        {
            int key = obj->params.keys[p];
            int value;

            /* A string param's value is a char*; reading it as an int is a
             * wild dereference, not a wrong number. */
            if( (obj->params.kinds && obj->params.kinds[p] == RSCACHE_PARAM_STRING) ||
                !obj->params.values[p] )
                continue;
            value = *(const int*)obj->params.values[p];

            if( key >= 0 && key < PARAM_BONUS_COUNT )
            {
                row.bonus[key] = value;
                stated = 1;
            }
            else if( key == PARAM_ATTACK_RATE )
            {
                row.attack_rate = value;
                stated = 1;
            }
            else if( key == PARAM_RANGED_STRENGTH || key == PARAM_RANGED_STRENGTH_AMMO )
            {
                row.ranged_strength += value;
                stated = 1;
            }
        }

        if( row.slot >= 0 && stated && obj->name && obj->name[0] )
        {
            name_lower(obj->name, row.name, sizeof(row.name));
            if( !name_seen(row.name) )
            {
                row_add(&row);
                written++;
            }
        }
        RSCache_Dat2ConfigObjFree(obj);
    }

    RSCache_FileListFree(files);
    return written;
}

int
main(int argc, char** argv)
{
    const char* rev_name = NULL;
    const char* cache_dir = NULL;
    const char* out_path = NULL;
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_RecordAddress addr;
    int table;
    FILE* out;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev_name = argv[++i];
        else if( !cache_dir )
            cache_dir = argv[i];
        else if( !out_path )
            out_path = argv[i];
    }
    if( !rev_name || !cache_dir || !out_path )
    {
        fprintf(stderr, "Usage: %s --rev NAME <cache_dir> <out_file>\n", argv[0]);
        return 1;
    }
    if( !RSCache_ProfileByName(rev_name, &profile) )
    {
        fprintf(stderr, "Unknown revision profile: %s\n", rev_name);
        return 1;
    }
    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "Failed to open cache: %s\n", cache_dir);
        return 1;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    addr = RSCache_RecordAddressFor(&profile, RSCACHE_TYPE_OBJ);
    table = RSCache_Dat2DiskTableId(
        disk, addr.group_shift ? addr.table : RSCACHE_DAT2_TABLE_CONFIGS);

    if( addr.group_shift == 0 )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
        if( archive && RSCache_Dat2DiskArchiveInitMetadata(disk, archive) &&
            archive->file_count > 0 )
            collect_from_archive(&profile, &addr, archive);
        RSCache_Dat2DiskArchiveFree(archive);
    }
    else
    {
        struct RSCache_ReferenceTable* ref = disk->tables[table];
        if( !ref )
        {
            fprintf(stderr, "Missing reference table %d for objs\n", table);
            return 1;
        }
        for( int i = 0; i < ref->id_count; i++ )
        {
            struct RSCache_Dat2DiskArchive* archive =
                RSCache_Dat2DiskArchiveNewLoad(disk, table, ref->ids[i]);
            if( archive && RSCache_Dat2DiskArchiveInitMetadata(disk, archive) &&
                archive->file_count > 0 )
                collect_from_archive(&profile, &addr, archive);
            RSCache_Dat2DiskArchiveFree(archive);
        }
    }

    out = fopen(out_path, "wb");
    if( !out )
    {
        fprintf(stderr, "Failed to write %s\n", out_path);
        return 1;
    }
    fprintf(
        out,
        "; GENERATED by tools/item_bonus_bake -- do not edit.\n"
        ";\n"
        "; Equipment bonuses for revisions whose cache states none: every dat1\n"
        "; world, where the numbers were the server's. Read by the item-stats\n"
        "; plugin ONLY when the open cache carries no bonus params, so an\n"
        "; OldSchool session still answers out of its own record.\n"
        ";\n"
        "; One row per item NAME, lowercased:\n"
        ";   <name>=<slot> <2h> <astab> <aslash> <acrush> <amagic> <arange>\n"
        ";                 <dstab> <dslash> <dcrush> <dmagic> <drange>\n"
        ";                 <str> <prayer> <rstr> <speed>\n"
        ";\n"
        "; `speed` is the attack rate in ticks, or -1 where the record states\n"
        "; none. The first record for a name wins, which is what makes the four\n"
        "; charge levels of an amulet of glory one row.\n"
        ";\n"
        "; The source is an OldSchool cache, which is this game's own answer for\n"
        "; an item of that name -- not the 2004 server's, which nothing here has.\n"
        "; A handful of items were rebalanced between the two, so a 2004 world\n"
        "; reads a number that is right for the item and may be a point off what\n"
        "; that server would roll with.\n"
        ";\n"
        "; source: %s (profile %s)\n"
        "count=%d\n",
        cache_dir,
        rev_name,
        g_row_count);

    for( int i = 0; i < g_row_count; i++ )
    {
        struct BakeRow const* row = &g_rows[i];
        fprintf(out, "%s=%d %d", row->name, row->slot, row->two_handed);
        for( int b = 0; b < PARAM_BONUS_COUNT; b++ )
            fprintf(out, " %d", row->bonus[b]);
        fprintf(out, " %d %d\n", row->ranged_strength, row->attack_rate);
    }
    fclose(out);
    printf("wrote %s (%d items)\n", out_path, g_row_count);
    return 0;
}
