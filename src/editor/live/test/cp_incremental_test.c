/**
 * The incremental CLIENT pack (`cachepack pack --incremental`,
 * 3rd/rscache/tools/cachepack/cp_incremental.h) on a copied miniature tree:
 * meta.ini, content.ini, fields/, pack/ and three config types (underlay,
 * overlay, varp) plus the raw config groups, packed into a cache of its own.
 *
 *   - a first incremental build over a cache a full pack wrote repacks every
 *     type, finds every archive unchanged, and writes nothing;
 *   - with nothing edited every type is reused;
 *   - one varp edited: only varp repacks, only its archive (2/16) is
 *     written, client.changed names it, the config table's reference table
 *     is bumped (its version, and that archive's version and crc) and every
 *     other archive keeps its version and crc;
 *   - the archive it wrote holds exactly the files a FULL pack of the edited
 *     tree writes;
 *   - an edit that changes no record (a comment) repacks the type and writes
 *     nothing;
 *   - --incremental refuses the asset flags;
 *   - src/maped/live-editor.mk's maped-client-cache runs it, and refuses a
 *     live cache inside the content tree.
 *
 *   cp_incremental_test <scratch> <cachepack> <make> <src-dir-of-repo>
 *
 * <scratch>/src is the miniature tree (the recipe copies it); everything is
 * written under <scratch>.
 */

#include "archive.h"
#include "editor/test/editor_test_text.h"
#include "dat2disk.h"
#include "filelist.h"
#include "mem/torirs_mem.h"
#include "reference_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static const char* g_scratch;
static const char* g_cachepack;

static char*
slurp(
    const char* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    *out_size = 0;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    data = malloc((size_t)size + 1);
    if( !data || (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

static uint64_t
file_hash(const char* path)
{
    size_t size;
    char* data = slurp(path, &size);
    uint64_t h = 1469598103934665603ull;
    if( !data )
        return 0;
    for( size_t i = 0; i < size; i++ )
        h = (h ^ (uint8_t)data[i]) * 1099511628211ull;
    free(data);
    return h ^ size;
}

/** `command`, a POSIX shell line (quoting, cd, redirections), as system()
 *  runs it on Linux and macOS. Windows' system() is cmd.exe, so there the
 *  line goes to Git's sh.exe, which the make recipes run under and which is
 *  on PATH whenever the suite runs. Returns the exit status (0 = success). */
static int
run_shell(const char* command)
{
#ifdef _WIN32
    char wrapped[8192];
    size_t at = (size_t)snprintf(wrapped, sizeof(wrapped), "sh -c \"");

    for( const char* c = command; *c && at + 4 < sizeof(wrapped); c++ )
    {
        if( *c == '"' || *c == '\\' )
            wrapped[at++] = '\\';
        wrapped[at++] = *c;
    }
    snprintf(wrapped + at, sizeof(wrapped) - at, "\"");
    return system(wrapped);
#else
    return system(command);
#endif
}

/** Run cachepack in the scratch dir; its output lands in <scratch>/<log>. */
static int
cachepack(
    const char* args,
    const char* log)
{
    char command[4096];
    snprintf(command, sizeof(command), "cd '%s' && '%s' %s > '%s' 2>&1", g_scratch, g_cachepack, args, log);
    return run_shell(command);
}

static int
log_has(
    const char* log,
    const char* needle)
{
    char path[2048];
    size_t size;
    char* text;
    int found;
    snprintf(path, sizeof(path), "%s/%s", g_scratch, log);
    text = slurp(path, &size);
    found = text && strstr(text, needle) != NULL;
    if( !found )
        fprintf(stderr, "  (%s has no \"%s\")\n", log, needle);
    free(text);
    return found;
}

/** The config table's reference data: the table version, and per archive its
 *  version and crc. */
struct snapshot
{
    int ok;
    int table_version;
    int archive_count;
    int versions[128];
    int crcs[128];
    uint64_t dat2;
};

static void
snap(
    const char* cache,
    struct snapshot* out)
{
    char dir[2048];
    struct RSCache_Dat2Disk* disk;
    struct RSCache_ReferenceTable* table;

    memset(out, 0, sizeof(*out));
    snprintf(dir, sizeof(dir), "%s/%s", g_scratch, cache);
    disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(dir);
    if( !disk )
        return;
    table = disk->tables[2];
    if( table )
    {
        out->ok = 1;
        out->table_version = table->version;
        out->archive_count = table->archive_count < 128 ? table->archive_count : 128;
        for( int i = 0; i < out->archive_count; i++ )
        {
            out->versions[i] = table->archives[i].version;
            out->crcs[i] = table->archives[i].crc;
        }
    }
    RSCache_Dat2DiskFree(disk);
    snprintf(dir, sizeof(dir), "%s/%s/main_file_cache.dat2", g_scratch, cache);
    out->dat2 = file_hash(dir);
}

/** 1 when archive (2, archive) holds the same files in both caches. */
static int
same_archive(
    const char* cache_a,
    const char* cache_b,
    int archive_id)
{
    char dir[2048];
    struct RSCache_Dat2Disk* disks[2];
    struct RSCache_Dat2DiskArchive* archives[2] = { NULL, NULL };
    struct RSCache_FileList* lists[2] = { NULL, NULL };
    int same = 1;
    const char* names[2] = { cache_a, cache_b };

    for( int i = 0; i < 2; i++ )
    {
        snprintf(dir, sizeof(dir), "%s/%s", g_scratch, names[i]);
        disks[i] = RSCache_Dat2DiskNewReadOnlyFromDirectory(dir);
        if( !disks[i] )
            return 0;
        archives[i] = RSCache_Dat2DiskArchiveNewLoad(disks[i], 2, archive_id);
        if( !archives[i] || !RSCache_Dat2DiskArchiveInitMetadata(disks[i], archives[i]) )
            same = 0;
        else
            lists[i] = RSCache_FileListNewFromDecode(
                archives[i]->data, archives[i]->data_size, archives[i]->file_count);
        if( !lists[i] )
            same = 0;
    }
    if( same && archives[0]->file_count != archives[1]->file_count )
        same = 0;
    for( int f = 0; same && f < archives[0]->file_count; f++ )
    {
        if( archives[0]->file_ids[f] != archives[1]->file_ids[f]
            || lists[0]->file_sizes[f] != lists[1]->file_sizes[f]
            || memcmp(lists[0]->files[f], lists[1]->files[f], (size_t)lists[0]->file_sizes[f]) != 0 )
            same = 0;
    }
    for( int i = 0; i < 2; i++ )
    {
        if( lists[i] )
            RSCache_FileListFree(lists[i]);
        if( archives[i] )
            RSCache_Dat2DiskArchiveFree(archives[i]);
        RSCache_Dat2DiskFree(disks[i]);
    }
    return same;
}

static int
read_changed(char* out, size_t out_size)
{
    char path[2048];
    size_t size;
    char* text;
    snprintf(path, sizeof(path), "%s/live/client.changed", g_scratch);
    text = slurp(path, &size);
    out[0] = '\0';
    if( !text )
        return 0;
    snprintf(out, out_size, "%s", text);
    free(text);
    return 1;
}

static void
edit_file(
    const char* rel,
    const char* find,
    const char* replace)
{
    char path[2048];
    size_t size;
    char* text;
    char* at;
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", g_scratch, rel);
    text = slurp(path, &size);
    if( !text )
    {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    /* A CRLF checkout of the content (Git for Windows) edits as LF: the
     * needles are LF, and the packer reads either. */
    editor_test_text_lf(text, &size);
    f = fopen(path, "wb");
    at = find ? strstr(text, find) : NULL;
    if( find && !at )
    {
        fprintf(stderr, "%s has no \"%s\"\n", path, find);
        exit(2);
    }
    if( at )
    {
        fwrite(text, 1, (size_t)(at - text), f);
        fputs(replace, f);
        fputs(at + strlen(find), f);
    }
    else
    {
        fwrite(text, 1, size, f);
        fputs(replace, f);
    }
    fclose(f);
    free(text);
}

int
main(
    int argc,
    char** argv)
{
    const char* make;
    const char* src_dir;
    struct snapshot full;
    struct snapshot s1;
    struct snapshot s3;
    struct snapshot s4;
    struct snapshot s5;
    char changed[4096];
    char command[8192];
    int varp_archive = 16;
    int others_kept = 1;

    if( argc != 5 )
    {
        fprintf(stderr, "usage: cp_incremental_test <scratch> <cachepack> <make> <repo-src>\n");
        return 2;
    }
    g_scratch = argv[1];
    g_cachepack = argv[2];
    make = argv[3];
    src_dir = argv[4];
    ToriMem_Init();

    check(cachepack("pack --src src --out live", "full.log") == 0, "full pack of the miniature tree");
    snap("live", &full);
    check(full.ok && full.archive_count > varp_archive, "the full pack has a config table with varp's archive");

    check(cachepack("pack --incremental --src src --out live", "inc1.log") == 0, "first incremental build");
    check(log_has("inc1.log", "20 type(s) repacked, 0 reused"), "first incremental: no state, every type repacks");
    check(log_has("inc1.log", "0 archive(s) changed"), "first incremental: every archive is unchanged");
    snap("live", &s1);
    check(s1.dat2 == full.dat2 && s1.table_version == full.table_version,
        "first incremental: nothing written, the reference table untouched");
    check(read_changed(changed, sizeof(changed)) && changed[0] == '\0', "first incremental: client.changed is empty");

    check(cachepack("pack --incremental --src src --out live", "inc2.log") == 0, "second incremental build");
    check(log_has("inc2.log", "0 type(s) repacked, 20 reused"), "nothing edited: every type reused");

    edit_file("src/configs/all.varp", "[varp1_mcannonmulti]\nclientcode=default",
        "[varp1_mcannonmulti]\nclientcode=7");
    check(cachepack("pack --incremental --src src --out live", "inc3.log") == 0, "incremental after a varp edit");
    check(log_has("inc3.log", "1 type(s) repacked, 19 reused") && log_has("inc3.log", "1 archive(s) changed"),
        "varp edit: one type repacked, one archive changed");
    check(log_has("inc3.log", "repacked varp — its files changed"), "varp edit: varp repacked because its file changed");
    check(read_changed(changed, sizeof(changed)) && strcmp(changed, "archive 2 16 varp\n") == 0,
        "varp edit: client.changed names archive 2/16");
    snap("live", &s3);
    check(s3.table_version == s1.table_version + 1, "varp edit: the config table's reference table version bumped");
    check(s3.versions[varp_archive] == s1.versions[varp_archive] + 1 && s3.crcs[varp_archive] != s1.crcs[varp_archive],
        "varp edit: archive 16's version bumped and crc changed");
    for( int i = 0; i < s3.archive_count; i++ )
    {
        if( i != varp_archive && (s3.versions[i] != s1.versions[i] || s3.crcs[i] != s1.crcs[i]) )
            others_kept = 0;
    }
    check(others_kept, "varp edit: every other archive keeps its version and crc");

    check(cachepack("pack --src src --out full2", "full2.log") == 0, "a full pack of the edited tree");
    check(same_archive("live", "full2", varp_archive), "the incremental varp archive equals the full pack's");
    check(same_archive("live", "full2", 1), "the untouched underlay archive equals the full pack's");

    check(cachepack("pack --incremental --src src --out live", "inc4.log") == 0, "incremental again");
    snap("live", &s4);
    check(log_has("inc4.log", "0 type(s) repacked, 20 reused") && s4.dat2 == s3.dat2
              && s4.table_version == s3.table_version,
        "no edit since: all reused, nothing written");

    edit_file("src/configs/all.underlay", NULL, "\n// a comment changes no record\n");
    check(cachepack("pack --incremental --src src --out live", "inc5.log") == 0, "incremental after a comment edit");
    snap("live", &s5);
    check(log_has("inc5.log", "repacked underlay — its files changed; its archive is unchanged"),
        "comment edit: underlay repacked, its archive unchanged");
    check(s5.dat2 == s4.dat2 && s5.table_version == s4.table_version, "comment edit: nothing written");
    check(read_changed(changed, sizeof(changed)) && changed[0] == '\0', "comment edit: client.changed is empty");

    check(cachepack("pack --incremental --assets --src src --out live", "refuse.log") != 0,
        "--incremental refuses --assets");

    /* The make target the native runner spawns. */
    snprintf(command, sizeof(command),
        "'%s' --no-print-directory -C '%s' maped-client-cache TORIRSSERVER_CONTENT_DIR='%s/src' "
        "MAPED_LIVE_CACHE='%s/live' TORIRSSERVER_CACHE_BASE='%s/no_base' > '%s/make.log' 2>&1",
        make, src_dir, g_scratch, g_scratch, g_scratch, g_scratch);
    check(run_shell(command) == 0, "make maped-client-cache runs the incremental pack");
    /* The tree is named by an absolute path now, a global input: every type
     * repacks, and still nothing differs. */
    check(log_has("make.log", "0 archive(s) changed"), "make maped-client-cache: nothing differs, nothing written");
    check(run_shell(command) == 0 && log_has("make.log", "0 type(s) repacked, 20 reused"),
        "make maped-client-cache again: all reused");
    snprintf(command, sizeof(command),
        "'%s' --no-print-directory -C '%s' maped-client-cache TORIRSSERVER_CONTENT_DIR='%s/src' "
        "MAPED_LIVE_CACHE='%s/src/cache_in_tree' > '%s/make_refuse.log' 2>&1",
        make, src_dir, g_scratch, g_scratch, g_scratch);
    check(run_shell(command) != 0 && log_has("make_refuse.log", "is inside the content tree"),
        "make maped-client-cache refuses a live cache inside the content tree");
    {
        struct stat info;
        char path[2048];
        snprintf(path, sizeof(path), "%s/src/cache_in_tree", g_scratch);
        check(stat(path, &info) != 0, "make maped-client-cache wrote nothing into the tree");
    }

    printf("cp_incremental_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
