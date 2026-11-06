/*
 * Editor lanes (torirsmaped/torirs_maped_lanes.h) over a small content tree
 * written under the scratch dir: the mirror's rules (links, copies, linked
 * asset dirs, skipped pack outputs and locks), create / lookup / list, the
 * change scan (modified, added, deleted; copies compared by content;
 * temporaries ignored), publish into a new lane, into another lane, and into
 * the main tree with reset, the lock refusal and force, and discard.
 *
 *   ./lanes_test <scratch>
 */
#include "torirsmaped/torirs_maped_lanes.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
is_link(const char* path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

static int
is_regular(const char* path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int
exists(const char* path)
{
    struct stat st;
    return lstat(path, &st) == 0;
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
    char repo[1024];
    char tree[1024];
    char path[2048];
    char why[512];
    struct ToriRSMapEdLaneDef work;
    struct ToriRSMapEdLaneDef other;
    struct ToriRSMapEdLaneDef fresh;
    struct ToriRSMapEdLaneDef main_lane;
    struct ToriRSMapEdLaneDef list[8];
    struct ToriRSMapEdLaneChange rows[16];
    struct ToriRSMapEdLaneCounts counts;
    int count;

    if( argc != 2 )
    {
        fprintf(stderr, "lanes_test <scratch>\n");
        return 2;
    }
    snprintf(repo, sizeof(repo), "%s/repo", argv[1]);
    snprintf(tree, sizeof(tree), "%s/repo/OSRS-Content/osrs-content", argv[1]);
    mkdirs(repo);

    /* ---- a small tree: the shapes the mirror rules care about ------------ */
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", tree);
    write_text(path, "square 50 50\n");
    snprintf(path, sizeof(path), "%s/maps/m51_50.jm2", tree);
    write_text(path, "square 51 50\n");
    snprintf(path, sizeof(path), "%s/areas/lumbridge/configs/lumbridge.npc", tree);
    write_text(path, "[hans]\nname=Hans\n");
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", tree);
    write_text(path, "hans=1\n");
    snprintf(path, sizeof(path), "%s/models/1.dat", tree);
    write_text(path, "model\n");
    snprintf(path, sizeof(path), "%s/server/pack/pack.manifest", tree);
    write_text(path, "pack\n");
    snprintf(path, sizeof(path), "%s/server/scripts/build/script.dat", tree);
    write_text(path, "scripts\n");
    snprintf(path, sizeof(path), "%s/server/scripts/build_summoning_curses/script.dat", tree);
    write_text(path, "lanes\n");
    snprintf(path, sizeof(path), "%s/server/scripts/hello.rs2", tree);
    write_text(path, "[proc,hello]\n");
    snprintf(path, sizeof(path), "%s/.editor-session.lock", tree);
    write_text(path, "pid 4194000\nsince 0\ncmd torirs\n");
    snprintf(path, sizeof(path), "%s/meta.ini", tree);
    write_text(path, "game = 1\n");

    /* ---- names, main --------------------------------------------------- */
    check(ToriRSMapEdLanes_NameValid("working") && ToriRSMapEdLanes_NameValid("lane-2_b"), "names: plain ones");
    check(!ToriRSMapEdLanes_NameValid("main") && !ToriRSMapEdLanes_NameValid("Working")
              && !ToriRSMapEdLanes_NameValid("a b") && !ToriRSMapEdLanes_NameValid(""),
        "names: main, capitals, spaces and empty are refused");
    ToriRSMapEdLanes_Main(repo, tree, &main_lane);
    check(main_lane.is_main && strcmp(main_lane.content, tree) == 0 && strstr(main_lane.live, "/build/live/default"),
        "main: the tree itself and build/live/default");
    check(ToriRSMapEdLanes_Changes(&main_lane, rows, 16, &counts) == 0, "main: no changes");
    check(!ToriRSMapEdLanes_Lookup(repo, tree, "working", &work), "lookup: no lane yet");

    /* ---- create: the mirror ------------------------------------------- */
    check(ToriRSMapEdLanes_Open(repo, tree, "working", &work, why, sizeof(why)), "open: creates the lane");
    if( why[0] )
        fprintf(stderr, "  why: %s\n", why);
    check(!work.is_main && strstr(work.dir, "/build/lanes/working") && strcmp(work.base, tree) == 0,
        "create: dir, base");
    snprintf(path, sizeof(path), "%s/lane.ini", work.dir);
    check(is_regular(path), "create: lane.ini");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", work.content);
    check(is_link(path), "mirror: a file is a link");
    snprintf(path, sizeof(path), "%s/maps", work.content);
    check(!is_link(path) && exists(path), "mirror: a directory is real");
    snprintf(path, sizeof(path), "%s/models", work.content);
    check(is_link(path), "mirror: an asset tree is one directory link");
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", work.content);
    check(is_regular(path), "mirror: pack/ is copied");
    snprintf(path, sizeof(path), "%s/server/pack", work.content);
    check(!exists(path), "mirror: server/pack is left out");
    snprintf(path, sizeof(path), "%s/server/scripts/build", work.content);
    check(!exists(path), "mirror: server/scripts/build is left out");
    snprintf(path, sizeof(path), "%s/server/scripts/build_summoning_curses", work.content);
    check(!exists(path), "mirror: server/scripts/build_* is left out");
    snprintf(path, sizeof(path), "%s/server/scripts/hello.rs2", work.content);
    check(is_link(path), "mirror: a script is a link");
    snprintf(path, sizeof(path), "%s/.editor-session.lock", work.content);
    check(!exists(path), "mirror: the tree's lock stays behind");
    snprintf(path, sizeof(path), "%s/meta.ini", work.content);
    check(is_link(path), "mirror: a root file is a link");
    check(exists(work.live), "create: the live dir");
    check(!ToriRSMapEdLanes_Create(repo, tree, "working", &other, why, sizeof(why)) && strstr(why, "exists"),
        "create: twice is refused");
    check(ToriRSMapEdLanes_Lookup(repo, tree, "working", &other) && strcmp(other.content, work.content) == 0,
        "lookup: finds it, same paths");
    check(ToriRSMapEdLanes_Open(repo, tree, "working", &other, why, sizeof(why)), "open: finds it");
    count = ToriRSMapEdLanes_List(repo, tree, list, 8);
    check(count == 2 && list[0].is_main && strcmp(list[1].name, "working") == 0, "list: main, working");
    check(ToriRSMapEdLanes_Changes(&work, rows, 16, &counts) == 0, "a fresh lane has no changes");
    /* A file the base gains later (a log beside the tree, another session's
     * new square) is new in the base, not deleted by the lane. */
    sleep(1);
    snprintf(path, sizeof(path), "%s/maps/m99_99.jm2", tree);
    write_text(path, "square 99 99 later\n");
    check(ToriRSMapEdLanes_Changes(&work, rows, 16, &counts) == 0, "a base file younger than the lane is not a deletion");

    /* ---- edits: a save, an addition, a deletion, a ledger rewrite -------- */
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", work.content);
    unlink(path);
    write_text(path, "square 50 50 edited\n");
    snprintf(path, sizeof(path), "%s/maps/m52_50.jm2", work.content);
    write_text(path, "square 52 50 new\n");
    snprintf(path, sizeof(path), "%s/maps/m51_50.jm2", work.content);
    unlink(path);
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", work.content);
    write_text(path, "hans=1\nbob=2\n");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2.tmp", work.content);
    write_text(path, "half written\n");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", tree);
    check(text_is(path, "square 50 50\n"), "the base is untouched by the lane's edits");
    count = ToriRSMapEdLanes_Changes(&work, rows, 16, &counts);
    check(count == 4 && counts.modified == 2 && counts.added == 1 && counts.deleted == 1,
        "changes: 2 modified, 1 added, 1 deleted");
    if( count != 4 )
        fprintf(stderr, "  got %d: %d/%d/%d\n", count, counts.modified, counts.added, counts.deleted);
    check(find_change(rows, count, "maps/m50_50.jm2", TORIRSMAPED_LANE_CHANGE_MODIFIED), "changes: the save");
    check(find_change(rows, count, "maps/m52_50.jm2", TORIRSMAPED_LANE_CHANGE_ADDED), "changes: the addition");
    check(find_change(rows, count, "maps/m51_50.jm2", TORIRSMAPED_LANE_CHANGE_DELETED), "changes: the deletion");
    check(find_change(rows, count, "pack/npc.alloc", TORIRSMAPED_LANE_CHANGE_MODIFIED), "changes: the ledger");
    check(!find_change(rows, count, "maps/m50_50.jm2.tmp", TORIRSMAPED_LANE_CHANGE_ADDED),
        "changes: a temporary is not one");
    check(count > 1 && strcmp(rows[0].path, "maps/m50_50.jm2") == 0 && strcmp(rows[3].path, "pack/npc.alloc") == 0,
        "changes: sorted by path");
    check(ToriRSMapEdLanes_Changes(&work, NULL, 0, &counts) == 4, "changes: a count-only scan agrees");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2.tmp", work.content);
    unlink(path);

    /* ---- publish into a new lane -------------------------------------- */
    check(ToriRSMapEdLanes_Create(repo, tree, "review", &fresh, why, sizeof(why)), "a new lane to publish into");
    check(ToriRSMapEdLanes_Publish(&work, &fresh, 0, &counts, why, sizeof(why)) && counts.written == 3
              && counts.removed == 1,
        "publish to a lane: 3 written, 1 removed");
    if( why[0] )
        fprintf(stderr, "  why: %s\n", why);
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", fresh.content);
    check(is_regular(path) && text_is(path, "square 50 50 edited\n"), "published: the save is a real file there");
    snprintf(path, sizeof(path), "%s/maps/m52_50.jm2", fresh.content);
    check(is_regular(path), "published: the addition");
    snprintf(path, sizeof(path), "%s/maps/m51_50.jm2", fresh.content);
    check(!exists(path), "published: the deletion");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", work.content);
    check(is_regular(path), "publish to a lane keeps the working lane's changes");
    check(ToriRSMapEdLanes_Changes(&fresh, rows, 16, &counts) == 4, "the target lane now carries the 4 changes");
    check(!ToriRSMapEdLanes_Publish(&work, &work, 0, &counts, why, sizeof(why)) && strstr(why, "itself"),
        "publish into itself is refused");
    check(!ToriRSMapEdLanes_Publish(&main_lane, &work, 0, &counts, why, sizeof(why)), "main publishes nowhere");

    /* ---- the main tree's lock ------------------------------------------- */
    snprintf(path, sizeof(path), "%s/.editor-session.lock", tree);
    {
        char lock[64];
        snprintf(lock, sizeof(lock), "pid %d\nsince 0\ncmd this test\n", (int)getpid());
        write_text(path, lock);
    }
    check(ToriRSMapEdLanes_TreeHeld(tree, why, sizeof(why)) && strstr(why, "this test"), "the tree reads held");
    check(!ToriRSMapEdLanes_Publish(&work, &main_lane, TORIRSMAPED_LANE_PUBLISH_RESET, &counts, why, sizeof(why))
              && strstr(why, "being edited"),
        "publish to a held main tree is refused");
    write_text(path, "pid 4194000\nsince 0\ncmd torirs\n");
    check(!ToriRSMapEdLanes_TreeHeld(tree, why, sizeof(why)), "a dead holder does not hold");

    /* ---- publish into main with reset ---------------------------------- */
    check(ToriRSMapEdLanes_Publish(&work, &main_lane, TORIRSMAPED_LANE_PUBLISH_RESET, &counts, why, sizeof(why))
              && counts.written == 3 && counts.removed == 1,
        "publish to main: 3 written, 1 removed");
    if( why[0] )
        fprintf(stderr, "  why: %s\n", why);
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", tree);
    check(text_is(path, "square 50 50 edited\n"), "main: the save landed");
    snprintf(path, sizeof(path), "%s/maps/m52_50.jm2", tree);
    check(text_is(path, "square 52 50 new\n"), "main: the addition landed");
    snprintf(path, sizeof(path), "%s/maps/m51_50.jm2", tree);
    check(!exists(path), "main: the deletion landed");
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", tree);
    check(text_is(path, "hans=1\nbob=2\n"), "main: the ledger landed");
    snprintf(path, sizeof(path), "%s/maps/m50_50.jm2", work.content);
    check(is_link(path), "reset: the published save is a link again");
    snprintf(path, sizeof(path), "%s/maps/m52_50.jm2", work.content);
    check(is_link(path), "reset: the published addition is a link");
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", work.content);
    check(is_link(path), "reset: the published ledger is a link (a copy again at the next mirror)");
    check(ToriRSMapEdLanes_Changes(&work, rows, 16, &counts) == 0, "reset: the working lane is clean");

    /* ---- discard ------------------------------------------------------- */
    snprintf(path, sizeof(path), "%s/areas/lumbridge/configs/lumbridge.npc", work.content);
    unlink(path);
    write_text(path, "[hans]\nname=Hans the Edited\n");
    snprintf(path, sizeof(path), "%s/areas/lumbridge/configs/new.npc", work.content);
    write_text(path, "[bob]\n");
    snprintf(path, sizeof(path), "%s/meta.ini", work.content);
    unlink(path);
    check(ToriRSMapEdLanes_Changes(&work, rows, 16, &counts) == 3, "discard: 3 changes before");
    check(ToriRSMapEdLanes_Discard(&work, &counts, why, sizeof(why)) && counts.written == 2 && counts.removed == 1,
        "discard: 2 re-linked, 1 removed");
    snprintf(path, sizeof(path), "%s/areas/lumbridge/configs/lumbridge.npc", work.content);
    check(is_link(path), "discard: the edit is a link again");
    snprintf(path, sizeof(path), "%s/areas/lumbridge/configs/new.npc", work.content);
    check(!exists(path), "discard: the addition is gone");
    snprintf(path, sizeof(path), "%s/meta.ini", work.content);
    check(is_link(path), "discard: the deleted file is back as a link");
    snprintf(path, sizeof(path), "%s/areas/lumbridge/configs/lumbridge.npc", tree);
    check(text_is(path, "[hans]\nname=Hans\n"), "discard: the base never changed");
    check(ToriRSMapEdLanes_Changes(&work, rows, 16, &counts) == 0, "discard: clean");
    check(!ToriRSMapEdLanes_Discard(&main_lane, &counts, why, sizeof(why)), "discard: main has no base");

    /* ---- the next boot's lane (workbench 6.5) ---------------------------- */
    {
        char lane_name[64];
        char note[300];
        char name[64];
        check(!ToriRSMapEdLanes_BootName(repo, name, sizeof(name)) && name[0] == '\0', "boot: no lane.ini, no name");
        check(!ToriRSMapEdLanes_BootLane(repo, tree, "working", lane_name, sizeof(lane_name), note, sizeof(note))
                  && strcmp(lane_name, "working") == 0 && note[0] == '\0',
            "boot: without the file the boot's own lane is served, silently");
        check(!ToriRSMapEdLanes_BootLane(repo, tree, NULL, lane_name, sizeof(lane_name), note, sizeof(note))
                  && strcmp(lane_name, "main") == 0,
            "boot: no lane asked is main");
        check(!ToriRSMapEdLanes_SetBoot(repo, tree, "nowhere", why, sizeof(why)) && strstr(why, "no lane 'nowhere'"),
            "switch: refused for a lane that does not exist");
        check(!ToriRSMapEdLanes_SetBoot(repo, tree, "Bad Name", why, sizeof(why)), "switch: refused for a bad name");
        check(ToriRSMapEdLanes_SetBoot(repo, tree, "review", why, sizeof(why)), "switch: to review");
        snprintf(path, sizeof(path), "%s/build/lanes/lane.ini", repo);
        check(is_regular(path), "switch: build/lanes/lane.ini written");
        snprintf(path, sizeof(path), "%s/build/lanes/lane.ini.tmp", repo);
        check(!exists(path), "switch: no temporary left behind");
        check(ToriRSMapEdLanes_BootName(repo, name, sizeof(name)) && strcmp(name, "review") == 0,
            "boot: the file names review");
        check(ToriRSMapEdLanes_BootLane(repo, tree, "working", lane_name, sizeof(lane_name), note, sizeof(note))
                  && strcmp(lane_name, "review") == 0 && strstr(note, "switched in the editor")
                  && strstr(note, "'working'"),
            "boot: the switched lane wins over the boot's, and says so");
        count = ToriRSMapEdLanes_List(repo, tree, list, 8);
        check(count == 3, "list: the lanes directory's own lane.ini is no lane");
        check(ToriRSMapEdLanes_SetBoot(repo, tree, "main", why, sizeof(why))
                  && ToriRSMapEdLanes_BootLane(repo, tree, "working", lane_name, sizeof(lane_name), note, sizeof(note))
                  && strcmp(lane_name, "main") == 0,
            "switch: back to main");
        /* A lane the file names that has gone: the boot's own, said. */
        snprintf(path, sizeof(path), "%s/build/lanes/lane.ini", repo);
        write_text(path, "lane = gone\n");
        check(!ToriRSMapEdLanes_BootLane(repo, tree, "working", lane_name, sizeof(lane_name), note, sizeof(note))
                  && strcmp(lane_name, "working") == 0 && strstr(note, "does not exist"),
            "boot: a vanished lane falls back to the boot's, and says so");
        unlink(path);
    }

    printf("lanes_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
