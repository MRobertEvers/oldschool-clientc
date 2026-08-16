/*
 * Dump decoded dat2 NPC definitions via 3rd/rscache.
 *
 * Usage:
 *   dump_npc --rev NAME <cache_dir> --id <npc_id>
 *   dump_npc --rev NAME <cache_dir> --name <substring>
 *   dump_npc --game G --epoch E --revision N [--quirks LIST] <cache_dir> --id <npc_id>
 *
 * A cache identity must be stated via --rev or --game/--epoch/--revision (and
 * optionally --quirks). 643 (RS2) keeps NPCs in table 18, 128 files per group.
 * OSRS keeps them in config table 2 group 9. RSCache_RecordAddressFor picks the
 * layout.
 */

#include "rscache.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --rev NAME <cache_dir> --id <npc_id>\n"
        "  %s --rev NAME <cache_dir> --name <substring>\n"
        "  %s --game G --epoch E --revision N [--quirks LIST] <cache_dir> --id <npc_id>\n"
        "  %s --game G --epoch E --revision N [--quirks LIST] <cache_dir> --name <substring>\n",
        argv0,
        argv0,
        argv0,
        argv0);
}

static int
str_contains_ci(
    const char* haystack,
    const char* needle)
{
    size_t nlen;
    size_t hlen;
    size_t i;

    assert(haystack);
    assert(needle);
    nlen = strlen(needle);
    hlen = strlen(haystack);
    if( nlen == 0 )
        return 1;
    if( nlen > hlen )
        return 0;

    for( i = 0; i + nlen <= hlen; i++ )
    {
        size_t j;
        for( j = 0; j < nlen; j++ )
        {
            unsigned char a = (unsigned char)haystack[i + j];
            unsigned char b = (unsigned char)needle[j];
            if( tolower(a) != tolower(b) )
                break;
        }
        if( j == nlen )
            return 1;
    }
    return 0;
}

static int
resolve_profile(
    const char* rev_name,
    const char* game_name,
    const char* epoch_name,
    const char* revision_text,
    const char* quirks_list,
    struct RSCache* out)
{
    if( rev_name )
    {
        if( game_name || epoch_name || revision_text )
        {
            fprintf(stderr, "Use either --rev or --game/--epoch/--revision, not both\n");
            return 0;
        }
        if( !RSCache_ProfileByName(rev_name, out) )
        {
            fprintf(stderr, "Unknown revision profile: %s\n", rev_name);
            return 0;
        }
        return 1;
    }

    if( !game_name || !epoch_name || !revision_text )
    {
        fprintf(
            stderr,
            "Cache identity required: pass --rev NAME or --game/--epoch/--revision\n");
        return 0;
    }

    int game = RSCache_GameFromName(game_name);
    int epoch = RSCache_EpochFromName(epoch_name);
    if( game == RSCACHE_GAME_UNSET )
    {
        fprintf(stderr, "Unknown game: %s (expected oldschool or rs2)\n", game_name);
        return 0;
    }
    if( epoch == RSCACHE_EPOCH_UNSET )
    {
        fprintf(stderr, "Unknown epoch: %s (expected dat1 or dat2)\n", epoch_name);
        return 0;
    }

    int revision = atoi(revision_text);
    uint32_t quirks = RSCACHE_QUIRK_NONE;
    if( quirks_list )
    {
        if( !RSCache_QuirksFromList(quirks_list, &quirks) )
        {
            fprintf(stderr, "Unknown quirks: %s\n", quirks_list);
            return 0;
        }
    }

    *out = RSCache_ProfileForIdentity(game, epoch, revision, quirks);
    if( !RSCache_ProfileIsIdentified(out) )
    {
        fprintf(stderr, "Resolved profile identity is unset\n");
        return 0;
    }
    return 1;
}

static void
print_npc(
    int npc_id,
    const struct RSCache_Dat2ConfigNpc* npc,
    int file_size)
{
    printf("=== npc id=%d file_size=%d consumed=%d ===\n", npc_id, file_size, npc->_consumed);
    RSCache_Dat2ConfigNpcPrint(npc);
    printf("\n");
}

static int
file_global_id(
    const struct RSCache_RecordAddress* addr,
    int archive_id,
    int file_id)
{
    if( addr->group_shift == 0 )
        return file_id;
    return (archive_id << addr->group_shift) + file_id;
}

static int
dump_npc_from_archive(
    const struct RSCache* profile,
    const struct RSCache_RecordAddress* addr,
    struct RSCache_Dat2DiskArchive* archive,
    int want_id,
    const char* want_name,
    int* matched)
{
    struct RSCache_FileList* files;
    struct RSCache local = *profile;
    int i;
    int hits = 0;

    assert(archive);
    assert(archive->file_count > 0);

    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_NPC, archive->revision);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
        return 0;

    for( i = 0; i < files->file_count; i++ )
    {
        int file_id;
        int npc_id;
        struct RSCache_Dat2ConfigNpc* npc;

        if( files->file_sizes[i] <= 0 )
            continue;

        file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        npc_id = file_global_id(addr, archive->archive_id, file_id);

        if( want_id >= 0 && npc_id != want_id )
            continue;

        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(
            &local, files->files[i], files->file_sizes[i]);
        if( !npc )
            continue;

        if( want_name )
        {
            if( !npc->name || !str_contains_ci(npc->name, want_name) )
            {
                RSCache_Dat2ConfigNpcFree(npc);
                continue;
            }
        }

        print_npc(npc_id, npc, files->file_sizes[i]);
        hits++;
        if( matched )
            (*matched)++;
        RSCache_Dat2ConfigNpcFree(npc);

        if( want_id >= 0 )
            break;
    }

    RSCache_FileListFree(files);
    return hits;
}

static int
dump_by_id(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    int npc_id)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, RSCACHE_TYPE_NPC);
    struct RSCache_Dat2DiskArchive* archive;
    int table;
    int archive_id;
    int matched = 0;

    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = RSCACHE_DAT2_CONFIG_KIND_NPC;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(disk, addr.table);
        archive_id = npc_id >> addr.group_shift;
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, archive_id);
    if( !archive )
    {
        fprintf(
            stderr,
            "Failed to load npc archive table=%d archive=%d for id=%d\n",
            table,
            archive_id,
            npc_id);
        return 1;
    }
    if( !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
    {
        fprintf(
            stderr,
            "NPC archive table=%d archive=%d has no files for id=%d\n",
            table,
            archive_id,
            npc_id);
        RSCache_Dat2DiskArchiveFree(archive);
        return 1;
    }

    dump_npc_from_archive(profile, &addr, archive, npc_id, NULL, &matched);
    RSCache_Dat2DiskArchiveFree(archive);

    if( matched == 0 )
    {
        fprintf(stderr, "NPC id %d not found (or empty record)\n", npc_id);
        return 1;
    }
    return 0;
}

static int
dump_by_name(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    const char* name)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, RSCACHE_TYPE_NPC);
    int matched = 0;

    if( addr.group_shift == 0 )
    {
        struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(
            disk,
            RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS),
            RSCACHE_DAT2_CONFIG_KIND_NPC);
        if( !archive )
        {
            fprintf(stderr, "Failed to load OSRS npc config group\n");
            return 1;
        }
        RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
        dump_npc_from_archive(profile, &addr, archive, -1, name, &matched);
        RSCache_Dat2DiskArchiveFree(archive);
    }
    else
    {
        int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
        struct RSCache_ReferenceTable* table =
            table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL : disk->tables[table_id];
        int i;

        if( !table )
        {
            fprintf(stderr, "Missing reference table %d for NPCs\n", table_id);
            return 1;
        }

        for( i = 0; i < table->id_count; i++ )
        {
            int archive_id = table->ids[i];
            struct RSCache_Dat2DiskArchive* archive =
                RSCache_Dat2DiskArchiveNewLoad(disk, table_id, archive_id);
            if( !archive )
                continue;
            if( !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
            {
                RSCache_Dat2DiskArchiveFree(archive);
                continue;
            }
            dump_npc_from_archive(profile, &addr, archive, -1, name, &matched);
            RSCache_Dat2DiskArchiveFree(archive);
        }
    }

    if( matched == 0 )
    {
        fprintf(stderr, "No NPC name containing \"%s\"\n", name);
        return 1;
    }
    printf("Matched %d NPC(s)\n", matched);
    return 0;
}

int
main(int argc, char** argv)
{
    const char* rev_name = NULL;
    const char* game_name = NULL;
    const char* epoch_name = NULL;
    const char* revision_text = NULL;
    const char* quirks_list = NULL;
    const char* cache_dir = NULL;
    int npc_id = -1;
    const char* name = NULL;
    int i;
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    int rc;

    for( i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 )
        {
            if( i + 1 >= argc )
            {
                usage(argv[0]);
                return 1;
            }
            rev_name = argv[++i];
        }
        else if( strcmp(argv[i], "--game") == 0 )
        {
            if( i + 1 >= argc )
            {
                usage(argv[0]);
                return 1;
            }
            game_name = argv[++i];
        }
        else if( strcmp(argv[i], "--epoch") == 0 )
        {
            if( i + 1 >= argc )
            {
                usage(argv[0]);
                return 1;
            }
            epoch_name = argv[++i];
        }
        else if( strcmp(argv[i], "--revision") == 0 )
        {
            if( i + 1 >= argc )
            {
                usage(argv[0]);
                return 1;
            }
            revision_text = argv[++i];
        }
        else if( strcmp(argv[i], "--quirks") == 0 )
        {
            if( i + 1 >= argc )
            {
                usage(argv[0]);
                return 1;
            }
            quirks_list = argv[++i];
        }
        else if( strcmp(argv[i], "--id") == 0 )
        {
            if( i + 1 >= argc )
            {
                usage(argv[0]);
                return 1;
            }
            npc_id = atoi(argv[++i]);
        }
        else if( strcmp(argv[i], "--name") == 0 )
        {
            if( i + 1 >= argc )
            {
                usage(argv[0]);
                return 1;
            }
            name = argv[++i];
        }
        else if( strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 )
        {
            usage(argv[0]);
            return 0;
        }
        else if( argv[i][0] == '-' )
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        else if( !cache_dir )
        {
            cache_dir = argv[i];
        }
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    if( !cache_dir || (npc_id < 0 && !name) || (npc_id >= 0 && name) )
    {
        usage(argv[0]);
        return 1;
    }

    if( !resolve_profile(rev_name, game_name, epoch_name, revision_text, quirks_list, &profile) )
        return 1;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "Failed to open cache: %s\n", cache_dir);
        return 1;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    {
        char quirks_buf[32];
        RSCache_QuirksName(profile.quirks, quirks_buf, sizeof(quirks_buf));
        printf(
            "# cache=%s game=%s epoch=%s revision=%d quirks=%s npc_layout=%s\n",
            cache_dir,
            RSCache_GameName(profile.game),
            RSCache_EpochName(profile.epoch),
            profile.revision,
            quirks_buf,
            RSCache_RecordAddressFor(&profile, RSCACHE_TYPE_NPC).group_shift ? "sharded-table"
                                                                             : "config-group");
    }

    if( npc_id >= 0 )
        rc = dump_by_id(disk, &profile, npc_id);
    else
        rc = dump_by_name(disk, &profile, name);

    RSCache_Dat2DiskFree(disk);
    return rc;
}
