/*
 * The editor server's saves on a SPARSE lane: the real local host
 * (editor/editor_host_local.c) over a lane ToriRSMapEdLanes_Create made, its
 * write guard set to ToriRSMapEdLanes_MakeWritable as ToriRSMapEd_OpenLane
 * sets it. A square, a spawn file, a server-overlay record, a script in a new
 * quest directory, an npc ledger and a new editor/ file are saved and a
 * script deleted, each inside a directory that starts as a link to the base;
 * afterwards the base tree (files and directories) is what it was, the lane
 * holds the edits, and the change scan lists exactly them. A guard that refuses fails the save, and (in a build without
 * asserts, where the check refuses instead of aborting) a guard that does
 * nothing is caught before anything is written through the link.
 *
 *   ./lanes_host_test <scratch>
 */
#include "editor/editor_host.h"
#include "torirsmaped/torirs_maped_lanes.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

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

static void
mkdirs(const char* path)
{
    char copy[2048];
    snprintf(copy, sizeof(copy), "%s", path);
    for( char* p = copy + 1; *p; p++ )
    {
        if( *p == '/' )
        {
            *p = '\0';
            mkdir(copy, 0755);
            *p = '/';
        }
    }
    mkdir(copy, 0755);
}

static void
write_text(
    const char* path,
    const char* text)
{
    char dir[2048];
    const char* slash = strrchr(path, '/');
    FILE* file;
    if( slash )
    {
        snprintf(dir, sizeof(dir), "%.*s", (int)(slash - path), path);
        mkdirs(dir);
    }
    file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static int
text_is(
    const char* path,
    const char* want)
{
    char got[512];
    size_t n;
    FILE* file = fopen(path, "rb");
    if( !file )
        return 0;
    n = fread(got, 1, sizeof(got) - 1, file);
    fclose(file);
    got[n] = '\0';
    return strcmp(got, want) == 0;
}

static int
exists(const char* path)
{
    struct stat st;
    return ToriRSMapEdLanes_IsLink(path) || stat(path, &st) == 0;
}

/** A file link the way a mirror makes one on this account: a hard link on
 *  Windows, a symbolic link elsewhere. 1 on success. */
static int
link_file(
    const char* target,
    const char* link_path)
{
#ifdef _WIN32
    return CreateHardLinkA(link_path, target, NULL) != 0;
#else
    return symlink(target, link_path) == 0;
#endif
}

/* ---- the base, as files ------------------------------------------------------- */

enum
{
    VIEW_MAX = 64,
    VIEW_ROW = 320,
};

/** Every file under a tree as "rel=contents" and every directory as "rel/",
 *  sorted: the base before and after must be the same rows (a mkdir that
 *  went through a link shows as a new directory row). */
struct view
{
    int count;
    char rows[VIEW_MAX][VIEW_ROW];
};

static int
view_compare(
    const void* a,
    const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

static void
view_walk(
    struct view* view,
    const char* root,
    const char* rel)
{
    char dir_path[2048];
    DIR* dir;
    struct dirent* entry;

    snprintf(dir_path, sizeof(dir_path), "%s%s%s", root, rel[0] ? "/" : "", rel);
    dir = opendir(dir_path);
    assert(dir);
    while( (entry = readdir(dir)) != NULL )
    {
        char child[1024];
        char path[2048];
        struct stat st;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if( stat(path, &st) != 0 )
            continue;
        assert(view->count < VIEW_MAX);
        if( S_ISDIR(st.st_mode) )
        {
            snprintf(view->rows[view->count++], VIEW_ROW, "%s/", child);
            view_walk(view, root, child);
            continue;
        }
        {
            char text[256] = "";
            FILE* file = fopen(path, "rb");
            size_t n;
            assert(file);
            n = fread(text, 1, sizeof(text) - 1, file);
            fclose(file);
            text[n] = '\0';
            snprintf(view->rows[view->count++], VIEW_ROW, "%s=%s", child, text);
        }
    }
    closedir(dir);
}

static void
view_take(
    struct view* view,
    const char* root)
{
    memset(view, 0, sizeof(*view));
    view_walk(view, root, "");
    qsort(view->rows, (size_t)view->count, VIEW_ROW, view_compare);
}

static int
view_equal(
    const struct view* a,
    const struct view* b)
{
    if( a->count != b->count )
        return 0;
    for( int i = 0; i < a->count; i++ )
    {
        if( strcmp(a->rows[i], b->rows[i]) != 0 )
        {
            fprintf(stderr, "  base row %d: %s -> %s\n", i, a->rows[i], b->rows[i]);
            return 0;
        }
    }
    return 1;
}

/* ---- the guard ---------------------------------------------------------------- */

struct guard
{
    const struct ToriRSMapEdLaneDef* lane;
    int calls;
    /** 1: refuse every write (with a reason). */
    int refuse;
    /** 1: say yes and do nothing -- a guard that MISSED its lane. */
    int idle;
};

static int
guard_fn(
    void* ctx,
    const char* rel,
    char* why,
    size_t why_size)
{
    struct guard* guard = ctx;

    guard->calls++;
    if( guard->refuse )
    {
        snprintf(why, why_size, "refused by the test (%s)", rel);
        return 0;
    }
    if( guard->idle )
        return 1;
    return ToriRSMapEdLanes_MakeWritable(guard->lane, rel, why, why_size);
}

static int
find_change(
    const struct ToriRSMapEdLaneChange* rows,
    int count,
    const char* path,
    enum ToriRSMapEdLaneChangeKind kind)
{
    for( int i = 0; i < count; i++ )
    {
        if( strcmp(rows[i].path, path) == 0 && rows[i].kind == kind )
            return 1;
    }
    return 0;
}

int
main(
    int argc,
    char** argv)
{
    char base[1024];
    char path[2048];
    char why[512];
    struct ToriRSMapEdLaneDef lane;
    struct view before;
    struct view after;
    struct EditorHost host;
    struct guard guard;
    const struct EditorHost_VTable* files;
    int calls;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <scratch>\n", argv[0]);
        return 2;
    }
    snprintf(base, sizeof(base), "%s/base", argv[1]);
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", base);
    write_text(path, "jm2 base\n");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jl2", base);
    write_text(path, "jl2 base\n");
    snprintf(path, sizeof(path), "%s/configs/all.loc", base);
    write_text(path, "[a]\nx=1\n");
    snprintf(path, sizeof(path), "%s/server/scripts/areas/lumbridge/configs/lumbridge.npc", base);
    write_text(path, "[goblin]\nhp=5\n");
    snprintf(path, sizeof(path), "%s/server/scripts/areas/lumbridge/scripts/old.rs2", base);
    write_text(path, "old\n");
    snprintf(path, sizeof(path), "%s/server/scripts/quests/q1/q1.rs2", base);
    write_text(path, "q1\n");
    snprintf(path, sizeof(path), "%s/npc_stats/g/goblin.stats", base);
    write_text(path, "base\n");
    snprintf(path, sizeof(path), "%s/worldmap/areas/w.wma", base);
    write_text(path, "w\n");
    snprintf(path, sizeof(path), "%s/pack/loc.alloc", base);
    write_text(path, "a\n");
    view_take(&before, base);

    check(ToriRSMapEdLanes_Create(argv[1], base, "ovlhost", &lane, why, sizeof(why)), "the sparse lane is made");
    if( !lane.sparse )
    {
        fprintf(stderr, "FAIL: not a sparse lane (%s)\n", why);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/server/scripts/areas", lane.content);
    check(ToriRSMapEdLanes_IsLink(path), "server/scripts/areas starts as a link");
    snprintf(path, sizeof(path), "%s/npc_stats", lane.content);
    check(ToriRSMapEdLanes_IsLink(path), "npc_stats starts as a link");

    Editor_HostOpenLocal(&host, lane.content, NULL);
    files = host.vtable;
    check(files->session(host.user_data, 1) == EDITOR_HOST_OK, "the lane's lock is taken");
    memset(&guard, 0, sizeof(guard));
    guard.lane = &lane;
    Editor_HostLocalSetWriteGuard(&host, guard_fn, &guard);

    /* ---- saves into linked directories ------------------------------------- */

    /* A link to a base file at the name the square's save writes its
     * temporary under (a mirror made before temporaries were left out of
     * mirrors linked them): the save must not write through it. */
    {
        char target[2048];
        snprintf(target, sizeof(target), "%s/maps/m50_50.jl2", base);
        snprintf(path, sizeof(path), "%s/maps/m50_50.jm2.tmp", lane.content);
        check(link_file(target, path), "a link to the base's .jl2 at the .jm2's temporary name");
    }
    calls = guard.calls;
    check(files->square_save(host.user_data, 50, 50, "jm2 lane\n", 9, NULL, 0) == EDITOR_HOST_OK, "a square saves");
    check(guard.calls > calls, "the square asked the guard");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", lane.content);
    check(text_is(path, "jm2 lane\n") && !ToriRSMapEdLanes_IsLink(path), "the square is the lane's own file");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jl2", base);
    check(text_is(path, "jl2 base\n"), "the link at the temporary's name was not written through");

    calls = guard.calls;
    check(files->spawn_save(host.user_data, 50, 50, "spawn\n", 6) == EDITOR_HOST_OK, "a spawn file saves");
    check(guard.calls > calls, "the spawn asked the guard");
    snprintf(path, sizeof(path), "%s/server/scripts/areas", lane.content);
    check(!ToriRSMapEdLanes_IsLink(path), "server/scripts/areas was exploded for it");
    snprintf(path, sizeof(path), "%s/server/scripts/areas/edited/configs/m50_50.spawn", lane.content);
    check(text_is(path, "spawn\n"), "the spawn file is in the lane");

    calls = guard.calls;
    check(files->doc_save(host.user_data, EDITOR_DOC_CFG, "npc:goblin@areas/lumbridge/configs/lumbridge.npc", 0,
              "[goblin]\nhp=9\n", 14)
              == EDITOR_HOST_OK,
        "a server-overlay record saves");
    check(guard.calls > calls, "the record asked the guard");
    snprintf(path, sizeof(path), "%s/server/scripts/areas/lumbridge/configs/lumbridge.npc", lane.content);
    check(text_is(path, "[goblin]\nhp=9\n"), "the record's file is the lane's, edited");

    calls = guard.calls;
    check(files->doc_save(host.user_data, EDITOR_DOC_TEXT, "rs2:areas/lumbridge/scripts/old.rs2", 0, NULL, 0)
              == EDITOR_HOST_OK,
        "a script is deleted");
    check(guard.calls > calls, "the delete asked the guard");
    snprintf(path, sizeof(path), "%s/server/scripts/areas/lumbridge/scripts/old.rs2", lane.content);
    check(!exists(path), "the script is gone from the lane");

    calls = guard.calls;
    check(files->doc_save(host.user_data, EDITOR_DOC_TEXT, "rs2:areas/varrock/none.rs2", 0, NULL, 0) == EDITOR_HOST_OK,
        "deleting a script nobody has is fine");
    check(guard.calls == calls, "and asks nothing of the guard");
    snprintf(path, sizeof(path), "%s/server/scripts/areas/varrock", lane.content);
    check(!exists(path), "and makes no directory");

    /* A new directory inside a linked one: its mkdir must come after the
     * guard, or it is made in the base. */
    calls = guard.calls;
    check(files->doc_save(host.user_data, EDITOR_DOC_TEXT, "rs2:quests/q2/new.rs2", 0, "q2\n", 3) == EDITOR_HOST_OK,
        "a script in a new quest directory saves");
    check(guard.calls > calls, "the new script asked the guard");
    snprintf(path, sizeof(path), "%s/server/scripts/quests/q2/new.rs2", lane.content);
    check(text_is(path, "q2\n"), "the new script is in the lane");

    calls = guard.calls;
    check(files->doc_save(host.user_data, EDITOR_DOC_TEXT, "stats:g/goblin", 0, "lane\n", 5) == EDITOR_HOST_OK,
        "an npc ledger saves");
    check(guard.calls > calls, "the ledger asked the guard");
    snprintf(path, sizeof(path), "%s/npc_stats/g/goblin.stats", lane.content);
    check(text_is(path, "lane\n"), "the ledger is the lane's");

    calls = guard.calls;
    check(files->doc_save(host.user_data, EDITOR_DOC_AREAS, "areas", 0, "areas\n", 6) == EDITOR_HOST_OK,
        "editor/areas.txt saves (a directory the base lacks)");
    check(guard.calls > calls, "editor/areas.txt asked the guard");
    snprintf(path, sizeof(path), "%s/editor/areas.txt", lane.content);
    check(text_is(path, "areas\n"), "editor/areas.txt is in the lane");

    /* ---- a guard that refuses ------------------------------------------------ */

    guard.refuse = 1;
    check(files->doc_save(host.user_data, EDITOR_DOC_TEXT, "wma:w2", 0, "x\n", 2) == EDITOR_HOST_IO_ERROR,
        "a refused write fails the save");
    snprintf(path, sizeof(path), "%s/worldmap", lane.content);
    check(ToriRSMapEdLanes_IsLink(path), "and leaves worldmap a link");
    guard.refuse = 0;

    /* ---- a guard that missed: the check before the write ------------------- */

#ifdef NDEBUG
    guard.idle = 1;
    check(files->doc_save(host.user_data, EDITOR_DOC_TEXT, "wma:w3", 0, "x\n", 2) == EDITOR_HOST_IO_ERROR,
        "a write under a link the guard missed is refused");
    check(files->doc_save(host.user_data, EDITOR_DOC_TEXT, "wma:w", 0, NULL, 0) == EDITOR_HOST_IO_ERROR,
        "and so is a delete under one");
    guard.idle = 0;
#else
    printf("lanes_host_test: the missed-guard case aborts by assert in this build; not run\n");
#endif

    /* ---- the base is untouched, the lane holds exactly the edits ------------ */

    view_take(&after, base);
    check(view_equal(&before, &after), "the base tree is byte for byte what it was");
    {
        struct ToriRSMapEdLaneChange rows[32];
        struct ToriRSMapEdLaneCounts counts;
        int const count = ToriRSMapEdLanes_Changes(&lane, rows, 32, &counts);

        check(count == 7, "seven changes");
        check(find_change(rows, count, "maps/m50_50.jm2", TORIRSMAPED_LANE_CHANGE_MODIFIED), "the square, modified");
        check(find_change(rows, count, "server/scripts/areas/lumbridge/configs/lumbridge.npc",
                  TORIRSMAPED_LANE_CHANGE_MODIFIED),
            "the record's file, modified");
        check(find_change(rows, count, "npc_stats/g/goblin.stats", TORIRSMAPED_LANE_CHANGE_MODIFIED),
            "the ledger, modified");
        check(find_change(rows, count, "server/scripts/areas/edited/configs/m50_50.spawn",
                  TORIRSMAPED_LANE_CHANGE_ADDED),
            "the spawn file, added");
        check(find_change(rows, count, "editor/areas.txt", TORIRSMAPED_LANE_CHANGE_ADDED), "the areas, added");
        check(find_change(rows, count, "server/scripts/quests/q2/new.rs2", TORIRSMAPED_LANE_CHANGE_ADDED),
            "the new script, added");
        check(find_change(rows, count, "server/scripts/areas/lumbridge/scripts/old.rs2",
                  TORIRSMAPED_LANE_CHANGE_DELETED),
            "the script, deleted");
        if( count != 7 )
        {
            for( int i = 0; i < count && i < 32; i++ )
                fprintf(stderr, "  change %d %s\n", (int)rows[i].kind, rows[i].path);
        }
    }

    files->session(host.user_data, 0);
    Editor_HostClose(&host);
    printf("lanes_host_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
