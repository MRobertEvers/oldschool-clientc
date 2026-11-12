/*
 * Editor lanes (torirsmaped/torirs_maped_lanes.h) over a small content tree
 * written under the scratch dir: the mirror's rules (links, copies, linked
 * asset dirs, skipped pack outputs and locks), create / lookup / list, the
 * change scan (modified, added, deleted; copies compared by content;
 * temporaries ignored), publish into a new lane, into another lane, and into
 * the main tree with reset, the lock refusal and force, discard, and
 * following the base (new files and directories, the base's deletions, a
 * copy taken / kept / refused against its ancestor, a re-linked copy).
 *
 *   ./lanes_test <scratch>
 */
#include "torirsmaped/torirs_maped_lanes.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
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

/* A link as the lanes code means it: on Windows a hard link or a junction can
 * stand for a symbolic link (an account without Developer Mode). */
static int
is_link(const char* path)
{
    return ToriRSMapEdLanes_IsLink(path);
}

static int
is_regular(const char* path)
{
    struct stat st;
    return !is_link(path) && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int
exists(const char* path)
{
    struct stat st;
    return is_link(path) || stat(path, &st) == 0;
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

/** A file link at `link_path` to the file `target`, the way a mirror makes one
 *  on this account: a hard link on Windows (no Developer Mode needed), a
 *  symbolic link elsewhere. 1 on success. */
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

/** An explode's temporary for the lane directory `rel`:
 *  "<content>/<parent>/.<name>.explode.tmp". */
static void
explode_temp(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* out,
    size_t out_size)
{
    const char* const slash = strrchr(rel, '/');
    if( slash )
        snprintf(out, out_size, "%s/%.*s/.%s.explode.tmp", lane->content, (int)(slash - rel), rel, slash + 1);
    else
        snprintf(out, out_size, "%s/.%s.explode.tmp", lane->content, rel);
}

/** `path`'s (a file's or a directory's) modification time set to `when`. 1 on
 *  success. */
static int
set_mtime(
    const char* path,
    time_t when)
{
#ifdef _WIN32
    /* utime() cannot open a directory here; a handle with backup semantics
     * can. */
    unsigned long long const ticks = ((unsigned long long)when + 11644473600ULL) * 10000000ULL;
    FILETIME stamp;
    HANDLE handle;
    BOOL ok;

    stamp.dwLowDateTime = (DWORD)ticks;
    stamp.dwHighDateTime = (DWORD)(ticks >> 32);
    handle = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if( handle == INVALID_HANDLE_VALUE )
        return 0;
    ok = SetFileTime(handle, NULL, &stamp, &stamp);
    CloseHandle(handle);
    return ok != 0;
#else
    struct utimbuf times;
    times.actime = when;
    times.modtime = when;
    return utime(path, &times) == 0;
#endif
}

/* ---- sparse lanes against legacy mirrors ------------------------------------ */

/** A link's own name removed, never anything through it: unlink for a file
 *  link or a POSIX symlink, rmdir for a Windows junction (rmdir of a REAL
 *  directory with entries fails, so this cannot delete a tree). */
static void
remove_link(const char* path)
{
    if( unlink(path) != 0 )
        rmdir(path);
}

/** What an editor save does: make the path writable, replace the entry. */
static int
save_text(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    const char* text)
{
    char path[2048];
    char why[512];

    if( !ToriRSMapEdLanes_MakeWritable(lane, rel, why, sizeof(why)) )
    {
        fprintf(stderr, "  make writable %s/%s: %s\n", lane->name, rel, why);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s", lane->content, rel);
    remove_link(path);
    write_text(path, text);
    return 1;
}

static int
delete_rel(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel)
{
    char path[2048];
    char why[512];

    if( !ToriRSMapEdLanes_MakeWritable(lane, rel, why, sizeof(why)) )
    {
        fprintf(stderr, "  make writable %s/%s: %s\n", lane->name, rel, why);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s", lane->content, rel);
    return unlink(path) == 0;
}

enum
{
    VIEW_MAX = 256,
    VIEW_ROW = 320,
};

/** Every file a tree SHOWS (links followed), as "rel=contents" rows, sorted:
 *  what a build tool or the game server reads through a lane. Temporaries,
 *  locks and the pack outputs are left out, as a lane leaves them out. */
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
    const char* root,
    const char* rel,
    struct view* view)
{
    char dir_path[2048];
    DIR* dir;
    struct dirent* entry;

    snprintf(dir_path, sizeof(dir_path), "%s%s%s", root, rel[0] ? "/" : "", rel);
    dir = opendir(dir_path);
    if( !dir )
        return;
    while( (entry = readdir(dir)) )
    {
        char sub_rel[1024];
        char path[2048];
        size_t const length = strlen(entry->d_name);
        struct stat st;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        if( (length >= 4 && strcmp(entry->d_name + length - 4, ".tmp") == 0)
            || (length >= 5 && strcmp(entry->d_name + length - 5, ".lock") == 0) )
            continue;
        snprintf(sub_rel, sizeof(sub_rel), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);
        if( strcmp(sub_rel, "server/pack") == 0 || strncmp(sub_rel, "server/scripts/build", 20) == 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if( stat(path, &st) != 0 )
            continue;
        if( S_ISDIR(st.st_mode) )
        {
            view_walk(root, sub_rel, view);
            continue;
        }
        if( view->count < VIEW_MAX )
        {
            char text[200];
            size_t n = 0;
            FILE* file = fopen(path, "rb");
            if( file )
            {
                n = fread(text, 1, sizeof(text) - 1, file);
                fclose(file);
            }
            text[n] = '\0';
            snprintf(view->rows[view->count++], VIEW_ROW, "%s=%s", sub_rel, text);
        }
    }
    closedir(dir);
}

static void
view_take(
    const char* root,
    struct view* view)
{
    view->count = 0;
    view_walk(root, "", view);
    qsort(view->rows, (size_t)view->count, VIEW_ROW, view_compare);
}

static int
view_same(
    const struct view* a,
    const struct view* b,
    const char* what)
{
    int same = a->count == b->count;
    for( int i = 0; same && i < a->count; i++ )
        same = strcmp(a->rows[i], b->rows[i]) == 0;
    if( !same )
    {
        fprintf(stderr, "  %s: %d rows against %d\n", what, a->count, b->count);
        for( int i = 0; i < a->count || i < b->count; i++ )
        {
            const char* const ra = i < a->count ? a->rows[i] : "";
            const char* const rb = i < b->count ? b->rows[i] : "";
            if( strcmp(ra, rb) != 0 )
                fprintf(stderr, "    [%d] '%s' | '%s'\n", i, ra, rb);
        }
    }
    return same;
}

/** The two lanes' change scans give the same rows and counts (`want` rows,
 *  -1 any). */
static int
changes_same(
    const struct ToriRSMapEdLaneDef* a,
    const struct ToriRSMapEdLaneDef* b,
    int want)
{
    static struct ToriRSMapEdLaneChange rows_a[64];
    static struct ToriRSMapEdLaneChange rows_b[64];
    struct ToriRSMapEdLaneCounts ca;
    struct ToriRSMapEdLaneCounts cb;
    int const na = ToriRSMapEdLanes_Changes(a, rows_a, 64, &ca);
    int const nb = ToriRSMapEdLanes_Changes(b, rows_b, 64, &cb);
    int same = na == nb && (want < 0 || na == want) && ca.modified == cb.modified && ca.added == cb.added
               && ca.deleted == cb.deleted;

    for( int i = 0; same && i < na && i < 64; i++ )
        same = rows_a[i].kind == rows_b[i].kind && rows_a[i].size == rows_b[i].size
               && strcmp(rows_a[i].path, rows_b[i].path) == 0;
    if( !same )
    {
        fprintf(stderr, "  changes %s: %d (want %d)\n", a->name, na, want);
        for( int i = 0; i < na && i < 64; i++ )
            fprintf(stderr, "    %d %s %u\n", rows_a[i].kind, rows_a[i].path, rows_a[i].size);
        fprintf(stderr, "  changes %s: %d\n", b->name, nb);
        for( int i = 0; i < nb && i < 64; i++ )
            fprintf(stderr, "    %d %s %u\n", rows_b[i].kind, rows_b[i].path, rows_b[i].size);
    }
    return same;
}

/** The same small tree at `tree`. */
static void
write_tree(const char* tree)
{
    static const char* const files[][2] = {
        { "maps/m1.jm2", "square 1\n" },
        { "maps/m2.jm2", "square 2\n" },
        { "areas/x/configs/x.npc", "[x]\n" },
        { "areas/x/configs/old.npc", "[old]\n" },
        { "areas/x/configs/keep.npc", "[keep]\n" },
        { "quests/q1/a.rs2", "[proc,a]\n" },
        { "quests/q1/b.rs2", "[proc,b]\n" },
        { "server/scripts/hello.rs2", "[proc,hello]\n" },
        { "pack/npc.alloc", "x=1\n" },
        { "models/1.dat", "model\n" },
        { "meta.ini", "game = 1\n" },
        { "deep/top.txt", "top\n" },
        { "deep/a/sib1/s.txt", "sib1\n" },
        { "deep/a/b/sib2.txt", "sib2\n" },
        { "deep/a/b/c/file.txt", "file\n" },
        { "deep2/f.txt", "f\n" },
        { "quests2/q.rs2", "[proc,q2]\n" },
        { "quests2/sub/s.rs2", "[proc,s2]\n" },
        { "quests3/q.rs2", "[proc,q3]\n" },
        { "quests4/q.rs2", "[proc,q4]\n" },
        { "quests4/sub/s.rs2", "[proc,s4]\n" },
        /* A temporary a crashed save left in the base: never mirrored. */
        { "maps/m2.jm2.tmp", "half written\n" },
        { "sprites/s.dat", "sprite\n" },
        { "deep3/d.txt", "d\n" },
        { "gone1/g.txt", "g1\n" },
        { "gone2/g.txt", "g2\n" },
        { "gone2/sub/h.txt", "h2\n" },
        { "quests6/q.rs2", "[proc,q6]\n" },
    };
    char path[2048];

    for( size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++ )
    {
        snprintf(path, sizeof(path), "%s/%s", tree, files[i][0]);
        write_text(path, files[i][1]);
        /* A base tree is older than the lanes made of it: an hour here. The
         * lanes tell a file they wrote from one that only fell behind the
         * base by its time against the lane's (torirs_maped_lanes.c, "what
         * the lane wrote"), and a tree written in the lane's own second
         * would make every link's file look like the lane's. */
        {
            int const dated = set_mtime(path, time(NULL) - 3600);
            assert(dated);
            (void)dated;
        }
    }
}

/** A LEGACY lane, as Create made them before sparse lanes: the full mirror
 *  (ToriRSMapEdLanes_Mirror), the copied tree's ancestor, a lane.ini with
 *  `base = `. */
static int
make_legacy(
    const char* repo,
    const char* tree,
    const char* name,
    struct ToriRSMapEdLaneDef* out)
{
    char path[2048];
    char text[2048];
    char why[512];

    snprintf(path, sizeof(path), "%s/build/lanes/%s/live", repo, name);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/build/lanes/%s/content", repo, name);
    if( ToriRSMapEdLanes_Mirror(tree, path, why, sizeof(why)) < 0 )
    {
        fprintf(stderr, "  mirror: %s\n", why);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/build/lanes/%s/ancestor/pack/npc.alloc", repo, name);
    write_text(path, "x=1\n");
    snprintf(path, sizeof(path), "%s/build/lanes/%s/lane.ini", repo, name);
    snprintf(text, sizeof(text), "base = %s\ncreated = %lld\nmirrored_files = 1\n", tree, (long long)time(NULL));
    write_text(path, text);
    return ToriRSMapEdLanes_Lookup(repo, tree, name, out);
}

/* ---- a lane behind its base: what fell behind is not the lane's change ------ */

#ifdef _WIN32
/** check() with a formatted message. */
static void
checkf(
    int condition,
    const char* format,
    ...)
{
    char what[512];
    va_list args;

    va_start(args, format);
    vsnprintf(what, sizeof(what), format, args);
    va_end(args);
    check(condition, what);
}

/** The base's file replaced by a NEW file, as a pull, a publish or an editor
 *  save replaces one: the lanes' hard links keep the old file. */
static void
replace_text(
    const char* path,
    const char* text)
{
    remove(path);
    write_text(path, text);
}

static int
file_names(
    const char* path,
    const char* line)
{
    char text[4096];
    size_t n;
    FILE* file = fopen(path, "rb");

    if( !file )
        return 0;
    n = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[n] = '\0';
    return strstr(text, line) != NULL;
}

static void
behind_one(
    const char* repo,
    const char* tree,
    struct ToriRSMapEdLaneDef* lane,
    const char* kind)
{
    struct ToriRSMapEdLaneChange rows[16];
    struct ToriRSMapEdLaneCounts counts;
    char path[2048];
    char why[512];
    int count;
    time_t const now = time(NULL);

    /* The lane's two edits: one file changed, one new. */
    checkf(save_text(lane, "maps/m1.jm2", "mine\n") && save_text(lane, "maps/m9.jm2", "new square\n"),
        "behind (%s): the lane edits one square and adds one", kind);
    /* The base moves on: files replaced, one removed, the edited one too. */
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", tree);
    replace_text(path, "square 2b\n");
    snprintf(path, sizeof(path), "%s/maps/m1.jm2", tree);
    replace_text(path, "square 1b\n");
    snprintf(path, sizeof(path), "%s/areas/x/configs/x.npc", tree);
    replace_text(path, "[x2]\n");
    snprintf(path, sizeof(path), "%s/quests/q1/b.rs2", tree);
    remove(path);
    snprintf(path, sizeof(path), "%s/pack/obj.alloc", tree);
    replace_text(path, "a=1\nb=2\n");

    count = ToriRSMapEdLanes_Changes(lane, rows, 16, &counts);
    checkf(count == 2 && find_change(rows, count, "maps/m1.jm2", TORIRSMAPED_LANE_CHANGE_MODIFIED)
              && find_change(rows, count, "maps/m9.jm2", TORIRSMAPED_LANE_CHANGE_ADDED),
        "behind (%s): the changes are the lane's two edits, not the files the base replaced (%d)", kind, count);
    for( int i = 0; i < count && i < 16 && count != 2; i++ )
        fprintf(stderr, "  %d %s\n", (int)rows[i].kind, rows[i].path);

    checkf(ToriRSMapEdLanes_Follow(lane, &counts, why, sizeof(why)), "behind (%s): the follow is not refused: %s", kind,
        why);
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", lane->content);
    checkf(text_is(path, "square 2b\n") && is_link(path), "behind (%s): a replaced square is the base's again", kind);
    snprintf(path, sizeof(path), "%s/areas/x/configs/x.npc", lane->content);
    checkf(text_is(path, "[x2]\n"), "behind (%s): a replaced config shows the base's", kind);
    snprintf(path, sizeof(path), "%s/quests/q1/b.rs2", lane->content);
    checkf(!exists(path), "behind (%s): a file the base removed is gone from the lane", kind);
    snprintf(path, sizeof(path), "%s/pack/obj.alloc", lane->content);
    checkf(text_is(path, "a=1\nb=2\n") && is_regular(path), "behind (%s): an untouched copy takes the base's", kind);
    snprintf(path, sizeof(path), "%s/maps/m1.jm2", lane->content);
    checkf(text_is(path, "mine\n"), "behind (%s): the lane's edit is kept", kind);
    snprintf(path, sizeof(path), "%s/maps/m9.jm2", lane->content);
    checkf(text_is(path, "new square\n"), "behind (%s): the lane's new square is kept", kind);
    snprintf(path, sizeof(path), "%s/build/lanes/%s/owned.ini", repo, lane->name);
    checkf(file_names(path, "maps/m1.jm2\n") && file_names(path, "maps/m9.jm2\n") && !file_names(path, "m2.jm2"),
        "behind (%s): the follow listed the two files the lane wrote", kind);

    /* The look has moved past the lane's edits: only the list says they are
     * the lane's now. Their times are put well before the follow to say so. */
    snprintf(path, sizeof(path), "%s/maps/m1.jm2", lane->content);
    checkf(set_mtime(path, now - 600), "behind (%s): the edit dated before the follow", kind);
    snprintf(path, sizeof(path), "%s/maps/m9.jm2", lane->content);
    checkf(set_mtime(path, now - 600), "behind (%s): the new square dated before the follow", kind);
    /* And the base replaces a square again (its file dated before the
     * follow, as one the lane linked then is). */
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", tree);
    checkf(set_mtime(path, now - 300), "behind (%s): the linked square dated before the follow", kind);
    replace_text(path, "square 2c\n");
    count = ToriRSMapEdLanes_Changes(lane, rows, 16, &counts);
    checkf(count == 2 && find_change(rows, count, "maps/m1.jm2", TORIRSMAPED_LANE_CHANGE_MODIFIED)
              && find_change(rows, count, "maps/m9.jm2", TORIRSMAPED_LANE_CHANGE_ADDED),
        "behind (%s): after the follow the changes are still the lane's two (%d)", kind, count);
    checkf(ToriRSMapEdLanes_Follow(lane, &counts, why, sizeof(why)), "behind (%s): a second follow: %s", kind, why);
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", lane->content);
    checkf(text_is(path, "square 2c\n"), "behind (%s): the square replaced again is the base's again", kind);
    snprintf(path, sizeof(path), "%s/maps/m1.jm2", lane->content);
    checkf(text_is(path, "mine\n"), "behind (%s): the second follow kept the lane's edit", kind);
    snprintf(path, sizeof(path), "%s/maps/m9.jm2", lane->content);
    checkf(text_is(path, "new square\n"), "behind (%s): the second follow kept the lane's new square", kind);

    /* A discard links the lane's files again and takes them off the list. */
    checkf(ToriRSMapEdLanes_Discard(lane, &counts, why, sizeof(why)), "behind (%s): discard: %s", kind, why);
    checkf(ToriRSMapEdLanes_Changes(lane, rows, 16, &counts) == 0, "behind (%s): nothing changed after the discard", kind);
    snprintf(path, sizeof(path), "%s/build/lanes/%s/owned.ini", repo, lane->name);
    checkf(!file_names(path, "maps/m1.jm2") && !file_names(path, "maps/m9.jm2"),
        "behind (%s): the discard took its files off the list", kind);
}

/** A folder the base has not got (a new quest's, made by Triggers > New
 *  quest): no file in it was ever a link's, so whatever their time its files
 *  are the lane's -- in Changes, and on the list a follow writes. They read
 *  as fallen behind once the look passed them (2026-11-11): a new quest's
 *  files went from the Changes list after the first RUN's follow. */
static void
behind_new_folder(const char* scratch)
{
    static const char* const k_rels[2] = { "quests/qnew/scripts/qnew.rs2", "quests/qnew/configs/qnew.varp" };
    struct ToriRSMapEdLaneChange rows[16];
    struct ToriRSMapEdLaneCounts counts;
    struct ToriRSMapEdLaneDef lane;
    char repo[1024];
    char tree[1024];
    char path[2048];
    char why[512];
    int count;

    snprintf(repo, sizeof(repo), "%s/behind-folder", scratch);
    snprintf(tree, sizeof(tree), "%s/OSRS-Content/osrs-content", repo);
    write_tree(tree);
    checkf(ToriRSMapEdLanes_Create(repo, tree, "fold", &lane, why, sizeof(why)) && lane.sparse, "folder: a lane");
    for( int i = 0; i < 2; i++ )
    {
        checkf(save_text(&lane, k_rels[i], "made by the lane\n"), "folder: %s written", k_rels[i]);
        snprintf(path, sizeof(path), "%s/%s", lane.content, k_rels[i]);
        /* Before the folder's look, as a file written ahead of a follow is
         * once the follow has moved the look past it. */
        checkf(set_mtime(path, time(NULL) - 600), "folder: %s dated before the look", k_rels[i]);
    }
    count = ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts);
    checkf(count == 2 && find_change(rows, count, k_rels[0], TORIRSMAPED_LANE_CHANGE_ADDED)
              && find_change(rows, count, k_rels[1], TORIRSMAPED_LANE_CHANGE_ADDED),
        "folder: both files are the lane's additions (%d)", count);
    checkf(ToriRSMapEdLanes_Follow(&lane, &counts, why, sizeof(why)), "folder: a follow: %s", why);
    snprintf(path, sizeof(path), "%s/build/lanes/fold/owned.ini", repo);
    checkf(file_names(path, "quests/qnew/scripts/qnew.rs2\n") && file_names(path, "quests/qnew/configs/qnew.varp\n"),
        "folder: the follow listed both files");
    for( int i = 0; i < 2; i++ )
    {
        snprintf(path, sizeof(path), "%s/%s", lane.content, k_rels[i]);
        checkf(text_is(path, "made by the lane\n"), "folder: the follow kept %s", k_rels[i]);
    }
    checkf(ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == 2 && counts.added == 2,
        "folder: after the follow both are still the lane's additions");
}

static void
fell_behind(const char* scratch)
{
    static const char* const kinds[2] = { "legacy", "sparse" };
    struct ToriRSMapEdLaneChange rows[16];
    struct ToriRSMapEdLaneCounts counts;
    char why[512];

    behind_new_folder(scratch);

    for( int k = 0; k < 2; k++ )
    {
        char repo[1024];
        char tree[1024];
        char path[2048];
        struct ToriRSMapEdLaneDef lane;

        snprintf(repo, sizeof(repo), "%s/behind-%s", scratch, kinds[k]);
        snprintf(tree, sizeof(tree), "%s/OSRS-Content/osrs-content", repo);
        write_tree(tree);
        /* A copied file the legacy lane holds no ancestor of. */
        snprintf(path, sizeof(path), "%s/pack/obj.alloc", tree);
        write_text(path, "a=1\n");
        checkf(set_mtime(path, time(NULL) - 3600), "behind (%s): the base's copy is an hour old", kinds[k]);
        if( k == 0 )
        {
            checkf(make_legacy(repo, tree, "old", &lane), "behind (legacy): a lane");
            /* A mirror's copies are made before its lane.ini is written. */
            snprintf(path, sizeof(path), "%s/pack/obj.alloc", lane.content);
            checkf(set_mtime(path, time(NULL) - 60), "behind (legacy): its copy is a minute older than lane.ini");
        }
        else
            checkf(ToriRSMapEdLanes_Create(repo, tree, "new", &lane, why, sizeof(why)) && lane.sparse,
                "behind (sparse): a lane");
        behind_one(repo, tree, &lane, kinds[k]);
    }

    /* A lane a build before the list followed: nothing in it is said to have
     * fallen behind until a follow here writes its list. */
    {
        char repo[1024];
        char tree[1024];
        char path[2048];
        char text[2048];
        struct ToriRSMapEdLaneDef lane;

        snprintf(repo, sizeof(repo), "%s/behind-prior", scratch);
        snprintf(tree, sizeof(tree), "%s/OSRS-Content/osrs-content", repo);
        write_tree(tree);
        checkf(make_legacy(repo, tree, "prior", &lane), "behind (prior): a lane");
        snprintf(path, sizeof(path), "%s/build/lanes/prior/lane.ini", repo);
        snprintf(text, sizeof(text), "base = %s\ncreated = %lld\nmirrored_files = 1\nfollowed = %lld\n", tree,
            (long long)time(NULL) - 20, (long long)time(NULL) - 5);
        write_text(path, text);
        checkf(ToriRSMapEdLanes_Lookup(repo, tree, "prior", &lane), "behind (prior): read again with its follow");
        /* Its edit, written before that follow. */
        checkf(save_text(&lane, "maps/m1.jm2", "before the list\n"), "behind (prior): an edit");
        snprintf(path, sizeof(path), "%s/maps/m1.jm2", lane.content);
        checkf(set_mtime(path, time(NULL) - 10), "behind (prior): dated before its follow");
        checkf(ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == 1
                  && find_change(rows, 1, "maps/m1.jm2", TORIRSMAPED_LANE_CHANGE_MODIFIED),
            "behind (prior): the edit is still the lane's change");
        checkf(ToriRSMapEdLanes_Follow(&lane, &counts, why, sizeof(why)), "behind (prior): a follow: %s", why);
        checkf(text_is(path, "before the list\n"), "behind (prior): the follow kept the edit");
        snprintf(path, sizeof(path), "%s/build/lanes/prior/owned.ini", repo);
        checkf(file_names(path, "maps/m1.jm2\n"), "behind (prior): and listed it");
    }
}
#else
static void
fell_behind(const char* scratch)
{
    (void)scratch;
    printf("lanes_test: fell-behind checks are for hard-link lanes (Windows without Developer Mode): not run\n");
}
#endif

static void
sparse_against_legacy(const char* scratch)
{
    static struct view view_a;
    static struct view view_b;
    static struct view base_before;
    static struct view base_after;
    char repo[1024];
    char tree_s[1024];
    char tree_l[1024];
    char path[2048];
    char path2[2048];
    char why[512];
    struct ToriRSMapEdLaneDef sparse;
    struct ToriRSMapEdLaneDef legacy;
    struct ToriRSMapEdLaneDef target_s;
    struct ToriRSMapEdLaneDef target_l;
    struct ToriRSMapEdLaneDef helper;
    struct ToriRSMapEdLaneCounts cs;
    struct ToriRSMapEdLaneCounts cl;
    int ok_s;
    int ok_l;

    /* Two copies of one tree, a sparse lane on one and a legacy mirror on
     * the other. Not one tree for both: on Windows without Developer Mode a
     * file link is a hard link, told from a file by its link count, and two
     * lanes on one base would each hold up the other's count. */
    snprintf(repo, sizeof(repo), "%s/repo2", scratch);
    snprintf(tree_s, sizeof(tree_s), "%s/repo2/base_s", scratch);
    snprintf(tree_l, sizeof(tree_l), "%s/repo2/base_l", scratch);
    write_tree(tree_s);
    write_tree(tree_l);

    check(ToriRSMapEdLanes_Create(repo, tree_s, "sparse", &sparse, why, sizeof(why)) && sparse.sparse,
        "pair: a sparse lane");
    check(make_legacy(repo, tree_l, "legacy", &legacy) && !legacy.sparse && strcmp(legacy.base, tree_l) == 0,
        "pair: a legacy lane opens as legacy, with its base");
    {
        struct ToriRSMapEdLaneDef again;
        check(ToriRSMapEdLanes_Open(repo, tree_l, "legacy", &again, why, sizeof(why)) && !again.sparse
                  && strcmp(again.content, legacy.content) == 0,
            "pair: Open finds the legacy lane, not a new one");
        check(ToriRSMapEdLanes_Lookup(repo, tree_s, "sparse", &again) && again.sparse
                  && strcmp(again.base, tree_s) == 0 && again.created == sparse.created,
            "pair: Lookup reads a sparse lane.ini");
    }

    /* The sparse skeleton. */
    snprintf(path, sizeof(path), "%s/maps", sparse.content);
    check(exists(path) && !is_link(path), "sparse create: maps/ is real (exploded)");
    snprintf(path, sizeof(path), "%s/maps/m1.jm2", sparse.content);
    check(is_link(path), "sparse create: a square is a link");
    snprintf(path, sizeof(path), "%s/quests", sparse.content);
    check(is_link(path), "sparse create: another directory is one link");
    snprintf(path, sizeof(path), "%s/models", sparse.content);
    check(is_link(path), "sparse create: an asset tree is one link");
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", sparse.content);
    check(is_regular(path), "sparse create: pack/ is copied");
    snprintf(path, sizeof(path), "%s/ancestor/pack/npc.alloc", sparse.dir);
    check(is_regular(path), "sparse create: pack's ancestor is seeded");
    snprintf(path, sizeof(path), "%s/server/scripts/hello.rs2", sparse.content);
    check(is_link(path), "sparse create: a script is a link in a real server/scripts");
    snprintf(path, sizeof(path), "%s/quests", legacy.content);
    check(exists(path) && !is_link(path), "legacy: a directory is real");
    /* A base temporary linked into a lane is the base's file under the name
     * a save opens to write: neither layout carries one. */
    snprintf(path, sizeof(path), "%s/maps/m2.jm2.tmp", sparse.content);
    snprintf(path2, sizeof(path2), "%s/maps/m2.jm2.tmp", legacy.content);
    check(!exists(path) && !exists(path2), "create: a base temporary is not linked into either layout");

    view_take(sparse.content, &view_a);
    view_take(legacy.content, &view_b);
    check(view_same(&view_a, &view_b, "fresh views"), "pair: a fresh sparse lane shows what a full mirror shows");
    check(changes_same(&sparse, &legacy, 0), "pair: no changes, either layout");

    /* ---- the same edits to both --------------------------------------- */
    view_take(tree_s, &base_before);
    snprintf(path, sizeof(path), "%s/areas", sparse.content);
    /* The guard's reason, without writing into the base to show it: the
     * directory the save lands in is a link to the base's right now. */
    check(is_link(path), "edits: areas/ is a link before the save");
    ok_s = save_text(&sparse, "maps/m1.jm2", "square 1 edited\n")
           && save_text(&sparse, "areas/x/configs/x.npc", "[x edited]\n")
           && save_text(&sparse, "areas/y/new.npc", "[new]\n") && delete_rel(&sparse, "quests/q1/a.rs2")
           && save_text(&sparse, "server/scripts/hello.rs2", "[proc,hello2]\n");
    ok_l = save_text(&legacy, "maps/m1.jm2", "square 1 edited\n")
           && save_text(&legacy, "areas/x/configs/x.npc", "[x edited]\n")
           && save_text(&legacy, "areas/y/new.npc", "[new]\n") && delete_rel(&legacy, "quests/q1/a.rs2")
           && save_text(&legacy, "server/scripts/hello.rs2", "[proc,hello2]\n");
    check(ok_s && ok_l, "edits: saved in both");
    view_take(tree_s, &base_after);
    check(view_same(&base_before, &base_after, "base after edits"), "edits: the sparse lane's base is untouched");
    snprintf(path, sizeof(path), "%s/areas/x/configs", sparse.content);
    check(exists(path) && !is_link(path), "edits: the save's directory was exploded");
    snprintf(path, sizeof(path), "%s/areas/x/configs/keep.npc", sparse.content);
    check(is_link(path), "edits: its untouched sibling is a link");
    snprintf(path, sizeof(path), "%s/quests/q1/b.rs2", sparse.content);
    check(is_link(path), "edits: a deletion's sibling is a link");
    snprintf(path, sizeof(path), "%s/deep", sparse.content);
    check(is_link(path), "edits: a directory nothing wrote into is still one link");
    check(changes_same(&sparse, &legacy, 5), "edits: the same 5 changes, either layout");
    view_take(sparse.content, &view_a);
    view_take(legacy.content, &view_b);
    check(view_same(&view_a, &view_b, "views after edits"), "edits: both show the same tree");

    /* ---- the base moves under both ---------------------------------------- */
    sleep(1);
    {
        const char* const trees[2] = { tree_s, tree_l };
        for( int t = 0; t < 2; t++ )
        {
            snprintf(path, sizeof(path), "%s/areas/x/configs/later.npc", trees[t]);
            write_text(path, "[later]\n"); /* new, in a directory the sparse lane exploded */
            snprintf(path, sizeof(path), "%s/deep/a/later.txt", trees[t]);
            write_text(path, "later\n"); /* new, in a directory the sparse lane links */
            snprintf(path, sizeof(path), "%s/areas/z/configs/z.npc", trees[t]);
            write_text(path, "[z]\n"); /* a new directory in an exploded one */
            snprintf(path, sizeof(path), "%s/newroot/f.txt", trees[t]);
            write_text(path, "newroot\n"); /* a new directory at the root */
            snprintf(path, sizeof(path), "%s/areas/x/configs/old.npc", trees[t]);
            unlink(path); /* gone from the base */
        }
    }
    /* Not a count: old.npc's link in each lane is the file's last other name
     * on Windows (a hard link, then a plain file: an addition), a dangling
     * symlink elsewhere (nothing). Either way the same in both layouts. */
    check(changes_same(&sparse, &legacy, -1), "base moved: still the same changes before a follow");
    ok_s = ToriRSMapEdLanes_Follow(&sparse, &cs, why, sizeof(why));
    if( !ok_s )
        fprintf(stderr, "  follow sparse: %s\n", why);
    ok_l = ToriRSMapEdLanes_Follow(&legacy, &cl, why, sizeof(why));
    if( !ok_l )
        fprintf(stderr, "  follow legacy: %s\n", why);
    check(ok_s && ok_l, "follow: both follow");
    /* The counts differ by design: the sparse lane's link to deep/ followed
     * on its own (nothing to do, nothing counted) and a new directory is one
     * link there, a mirror of its files here. */
    check(cs.written == 3 && cl.written == 4 && cs.removed == cl.removed,
        "follow: sparse links 3 (a file, two directories), legacy 4 files; the same removed");
    if( cs.written != 3 || cl.written != 4 || cs.removed != cl.removed )
        fprintf(stderr, "  sparse %d/%d legacy %d/%d\n", cs.written, cs.removed, cl.written, cl.removed);
    snprintf(path, sizeof(path), "%s/areas/z", sparse.content);
    check(is_link(path), "follow: a new base directory is one link in the sparse lane");
    snprintf(path, sizeof(path), "%s/newroot", sparse.content);
    check(is_link(path), "follow: a new root directory is one link in the sparse lane");
    snprintf(path, sizeof(path), "%s/deep", sparse.content);
    check(is_link(path), "follow: a linked directory is not mirrored by the follow");
    snprintf(path, sizeof(path), "%s/deep/a/later.txt", sparse.content);
    check(text_is(path, "later\n"), "follow: a new file in a linked directory shows through it");
    view_take(sparse.content, &view_a);
    view_take(legacy.content, &view_b);
    check(view_same(&view_a, &view_b, "views after follow"), "follow: both show the same tree");
    check(changes_same(&sparse, &legacy, -1), "follow: the same changes, either layout");

    /* ---- publish each into a fresh lane of its own kind ---------------------- */
    check(ToriRSMapEdLanes_Create(repo, tree_s, "target-s", &target_s, why, sizeof(why)) && target_s.sparse,
        "publish: a sparse target");
    check(make_legacy(repo, tree_l, "target-l", &target_l), "publish: a legacy target");
    snprintf(path, sizeof(path), "%s/areas", target_s.content);
    check(is_link(path), "publish: the sparse target's areas/ is a link before");
    /* A link to a base file sitting at the name the publish writes its
     * temporary under (as a mirror made before temporaries were left out
     * linked them): the copy must not be written through it. */
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", tree_s);
    snprintf(path2, sizeof(path2), "%s/maps/m1.jm2.tmp", target_s.content);
    check(link_file(path, path2), "publish: a link at the target's temporary name");
    view_take(tree_s, &base_before);
    ok_s = ToriRSMapEdLanes_Publish(&sparse, &target_s, 0, &cs, why, sizeof(why));
    if( !ok_s )
        fprintf(stderr, "  publish sparse: %s\n", why);
    ok_l = ToriRSMapEdLanes_Publish(&legacy, &target_l, 0, &cl, why, sizeof(why));
    if( !ok_l )
        fprintf(stderr, "  publish legacy: %s\n", why);
    check(ok_s && ok_l && cs.written == cl.written && cs.removed == cl.removed && cs.written >= 4 && cs.removed == 1,
        "publish: the same written and removed, either layout");
    view_take(tree_s, &base_after);
    check(view_same(&base_before, &base_after, "base after publish"),
        "publish into a sparse lane: nothing lands in its base");
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", tree_s);
    snprintf(path2, sizeof(path2), "%s/maps/m1.jm2", target_s.content);
    check(text_is(path, "square 2\n") && is_regular(path2) && text_is(path2, "square 1 edited\n"),
        "publish: the copy went to a new temporary, not through the link at its name");
    snprintf(path, sizeof(path), "%s/areas/x/configs/x.npc", target_s.content);
    check(is_regular(path) && text_is(path, "[x edited]\n"), "publish: the save is a real file in the target");
    snprintf(path, sizeof(path), "%s/areas/x/configs/x.npc", tree_s);
    check(text_is(path, "[x]\n"), "publish: the target's base keeps its own");
    snprintf(path, sizeof(path), "%s/quests/q1/a.rs2", target_s.content);
    snprintf(path2, sizeof(path2), "%s/quests/q1/a.rs2", tree_s);
    check(!exists(path) && text_is(path2, "[proc,a]\n"), "publish: the deletion is the target's, not its base's");
    check(changes_same(&target_s, &target_l, -1) && changes_same(&target_s, &sparse, -1),
        "publish: both targets carry the source's changes");
    view_take(target_s.content, &view_a);
    view_take(target_l.content, &view_b);
    check(view_same(&view_a, &view_b, "targets"), "publish: both targets show the same tree");

    /* ---- discard both ------------------------------------------------------- */
    ok_s = ToriRSMapEdLanes_Discard(&sparse, &cs, why, sizeof(why));
    ok_l = ToriRSMapEdLanes_Discard(&legacy, &cl, why, sizeof(why));
    check(ok_s && ok_l && cs.written == cl.written && cs.removed == cl.removed && cs.written == 4,
        "discard: the same re-linked and removed, either layout");
    check(changes_same(&sparse, &legacy, 0), "discard: both clean");
    view_take(sparse.content, &view_a);
    view_take(tree_s, &view_b);
    check(view_same(&view_a, &view_b, "sparse after discard"), "discard: the sparse lane shows its base again");
    view_take(legacy.content, &view_a);
    view_take(tree_l, &view_b);
    check(view_same(&view_a, &view_b, "legacy after discard"), "discard: the legacy lane shows its base again");
    snprintf(path, sizeof(path), "%s/areas/x/configs", sparse.content);
    check(exists(path) && !is_link(path), "discard: an exploded directory stays exploded");

    /* ---- MakeWritable on a deep path ------------------------------------------ */
    view_take(tree_s, &base_before);
    snprintf(path, sizeof(path), "%s/deep", sparse.content);
    check(is_link(path), "deep: deep/ is a link before");
    check(ToriRSMapEdLanes_MakeWritable(&sparse, "deep/a/b/c/new.txt", why, sizeof(why)),
        "deep: MakeWritable on a four-deep path");
    {
        static const char* const real_dirs[] = { "deep", "deep/a", "deep/a/b", "deep/a/b/c" };
        static const char* const links[] = { "deep/top.txt", "deep/a/sib1", "deep/a/b/sib2.txt",
            "deep/a/b/c/file.txt", "deep/a/later.txt" };
        int real = 1;
        int linked = 1;
        int temps = 0;
        for( int i = 0; i < 4; i++ )
        {
            snprintf(path, sizeof(path), "%s/%s", sparse.content, real_dirs[i]);
            real = real && exists(path) && !is_link(path);
            explode_temp(&sparse, real_dirs[i], path, sizeof(path));
            temps += exists(path);
        }
        for( int i = 0; i < 5; i++ )
        {
            snprintf(path, sizeof(path), "%s/%s", sparse.content, links[i]);
            linked = linked && is_link(path);
        }
        check(real, "deep: every ancestor is real");
        check(linked, "deep: every sibling is a link");
        check(temps == 0, "deep: no explode temporary left");
    }
    snprintf(path, sizeof(path), "%s/deep/a/b/c/new.txt", sparse.content);
    check(!exists(path), "deep: the path itself is not made");
    write_text(path, "new\n");
    snprintf(path, sizeof(path), "%s/deep/a/mine.txt", sparse.content);
    write_text(path, "mine\n");
    /* Asked again: what is real stays real (nothing exploded twice). */
    check(ToriRSMapEdLanes_MakeWritable(&sparse, "deep/a/b/c/other.txt", why, sizeof(why)), "deep: asked again");
    snprintf(path, sizeof(path), "%s/deep/a/mine.txt", sparse.content);
    snprintf(path2, sizeof(path2), "%s/deep/a/b/c/new.txt", sparse.content);
    check(is_regular(path) && text_is(path2, "new\n"), "deep: each ancestor was exploded once");
    view_take(tree_s, &base_after);
    check(view_same(&base_before, &base_after, "base after deep"), "deep: the base's listing is unchanged");
    snprintf(path, sizeof(path), "%s/deep/a/b/c/new.txt", tree_s);
    check(!exists(path), "deep: the write after MakeWritable is not in the base");
    {
        struct ToriRSMapEdLaneCounts counts;
        check(ToriRSMapEdLanes_Changes(&sparse, NULL, 0, &counts) == 2 && counts.added == 2,
            "deep: the two new files are the lane's additions");
    }
    check(!ToriRSMapEdLanes_MakeWritable(&sparse, "deep/../../x", why, sizeof(why)) && strstr(why, "not a path"),
        "deep: a path out of the lane is refused");
    check(!ToriRSMapEdLanes_MakeWritable(&sparse, "meta.ini/x", why, sizeof(why)) && strstr(why, "not a directory"),
        "deep: an ancestor that is a file is refused");
    check(ToriRSMapEdLanes_MakeWritable(&sparse, "meta.ini", why, sizeof(why)), "deep: a root file needs nothing");

    /* ---- a base deletion inside a linked directory ---------------------------- */
    snprintf(path, sizeof(path), "%s/deep2/f.txt", tree_s);
    unlink(path);
    snprintf(path, sizeof(path), "%s/deep2/f.txt", sparse.content);
    check(!exists(path), "linked: a base deletion inside a linked directory is the lane's at once");
    check(ToriRSMapEdLanes_Follow(&sparse, &cs, why, sizeof(why)) && cs.removed == 0,
        "linked: the follow has nothing to drop for it");

    /* ---- crash recovery ---------------------------------------------------- */
    /* (a) An explode that stopped after removing the link: the complete
     * temporary is all there is. Built with a helper lane's own explode. */
    check(ToriRSMapEdLanes_Create(repo, tree_s, "helper", &helper, why, sizeof(why)), "recover: a helper lane");
    check(ToriRSMapEdLanes_MakeWritable(&helper, "quests2/x", why, sizeof(why))
              && ToriRSMapEdLanes_MakeWritable(&helper, "quests3/x", why, sizeof(why)),
        "recover: the helper explodes quests2 and quests3");
    view_take(tree_s, &base_before);
    {
        static const char* const names[] = { "quests2", "quests3" };
        int moved = 1;
        for( int i = 0; i < 2; i++ )
        {
            snprintf(path, sizeof(path), "%s/%s", sparse.content, names[i]);
            moved = moved && is_link(path);
            remove_link(path);
            moved = moved && !exists(path);
            snprintf(path, sizeof(path), "%s/%s", helper.content, names[i]);
            explode_temp(&sparse, names[i], path2, sizeof(path2));
            moved = moved && rename(path, path2) == 0;
        }
        check(moved, "recover: two lanes left mid-explode (link gone, temporary complete)");
    }
    check(ToriRSMapEdLanes_Changes(&sparse, NULL, 0, NULL) == 2, "recover: a scan ignores the temporaries");
    check(ToriRSMapEdLanes_MakeWritable(&sparse, "quests2/sub/new.rs2", why, sizeof(why)),
        "recover: the next MakeWritable completes it");
    explode_temp(&sparse, "quests2", path, sizeof(path));
    snprintf(path2, sizeof(path2), "%s/quests2/q.rs2", sparse.content);
    check(!exists(path) && is_link(path2), "recover: quests2 is in place, its file a link");
    snprintf(path, sizeof(path), "%s/quests2/sub", sparse.content);
    check(exists(path) && !is_link(path), "recover: and the explode carried on below it");
    check(ToriRSMapEdLanes_Follow(&sparse, &cs, why, sizeof(why)), "recover: a follow");
    explode_temp(&sparse, "quests3", path, sizeof(path));
    snprintf(path2, sizeof(path2), "%s/quests3/q.rs2", sparse.content);
    check(!exists(path) && is_link(path2), "recover: the follow completes the other one");
    /* (b) An explode that stopped part way: a temporary beside the link,
     * holding a directory link INTO the base and a stray file. It is
     * removed without following the link, and the explode is done again. */
    explode_temp(&sparse, "quests4", path, sizeof(path));
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/quests4/sub", tree_s);
    explode_temp(&sparse, "quests4", path2, sizeof(path2));
    strcat(path2, "/sub");
    check(ToriRSMapEdLanes_LinkDirectory(path, path2), "recover: a partial temporary with a link into the base");
    explode_temp(&sparse, "quests4", path, sizeof(path));
    strcat(path, "/stray.txt");
    write_text(path, "stray\n");
    check(ToriRSMapEdLanes_MakeWritable(&sparse, "quests4/new.rs2", why, sizeof(why)),
        "recover: MakeWritable beside a partial temporary");
    if( why[0] )
        fprintf(stderr, "  why: %s\n", why);
    snprintf(path, sizeof(path), "%s/quests4/sub/s.rs2", tree_s);
    check(text_is(path, "[proc,s4]\n"), "recover: the base's files behind the temporary's link survive");
    explode_temp(&sparse, "quests4", path, sizeof(path));
    snprintf(path2, sizeof(path2), "%s/quests4/stray.txt", sparse.content);
    check(!exists(path) && !exists(path2), "recover: the partial temporary is gone, its stray with it");
    snprintf(path, sizeof(path), "%s/quests4/sub", sparse.content);
    snprintf(path2, sizeof(path2), "%s/quests4/q.rs2", sparse.content);
    check(is_link(path) && is_link(path2), "recover: quests4 is exploded properly");
    view_take(tree_s, &base_after);
    check(view_same(&base_before, &base_after, "base after recovery"), "recover: the base is untouched");

    /* ---- a legacy lane's linked asset tree ------------------------------------ */
    view_take(tree_l, &base_before);
    snprintf(path, sizeof(path), "%s/models", legacy.content);
    check(is_link(path), "legacy: models/ is one link");
    check(save_text(&legacy, "models/2.dat", "model 2\n"), "legacy: a save into models/ through MakeWritable");
    check(exists(path) && !is_link(path), "legacy: models/ was exploded");
    snprintf(path, sizeof(path), "%s/models/1.dat", legacy.content);
    check(is_link(path), "legacy: its file is a link");
    view_take(tree_l, &base_after);
    check(view_same(&base_before, &base_after, "legacy base"), "legacy: the base is untouched");
    {
        struct ToriRSMapEdLaneChange rows[8];
        struct ToriRSMapEdLaneCounts counts;
        int const count = ToriRSMapEdLanes_Changes(&legacy, rows, 8, &counts);
        check(count == 1 && find_change(rows, count, "models/2.dat", TORIRSMAPED_LANE_CHANGE_ADDED),
            "legacy: the walks go into an exploded asset tree");
    }
    check(ToriRSMapEdLanes_Discard(&legacy, &cs, why, sizeof(why)) && cs.removed == 1, "legacy: discard it");

    /* ---- a legacy asset tree an explode left absent ----------------------------- */
    /* The link went, the complete temporary was not yet renamed into place:
     * the legacy walks pass over an ABSENT asset tree, and the temporary is
     * a dot-name .tmp, yet the follow must still put it in place. */
    snprintf(path, sizeof(path), "%s/sprites", legacy.content);
    check(is_link(path), "legacy recover: sprites/ is one link");
    remove_link(path);
    explode_temp(&legacy, "sprites", path2, sizeof(path2));
    mkdirs(path2);
    strcat(path2, "/s.dat");
    snprintf(path, sizeof(path), "%s/sprites/s.dat", tree_l);
    check(link_file(path, path2), "legacy recover: its complete temporary, the link gone");
    ok_l = ToriRSMapEdLanes_Follow(&legacy, &cl, why, sizeof(why));
    if( !ok_l )
        fprintf(stderr, "  follow legacy: %s\n", why);
    check(ok_l, "legacy recover: a follow");
    snprintf(path, sizeof(path), "%s/sprites/s.dat", legacy.content);
    explode_temp(&legacy, "sprites", path2, sizeof(path2));
    check(is_link(path) && text_is(path, "sprite\n") && !exists(path2),
        "legacy recover: the follow puts the asset tree in place");

    /* ---- a temporary beside a link that is still there ------------------------- */
    /* An explode that died filling, or whose unlink of the link failed: the
     * temporary holds a second copy of the directory. Follow removes it. */
    explode_temp(&sparse, "deep3", path, sizeof(path));
    mkdirs(path);
    strcat(path, "/d.txt");
    write_text(path, "a second copy\n");
    ok_s = ToriRSMapEdLanes_Follow(&sparse, &cs, why, sizeof(why));
    if( !ok_s )
        fprintf(stderr, "  follow sparse: %s\n", why);
    explode_temp(&sparse, "deep3", path, sizeof(path));
    snprintf(path2, sizeof(path2), "%s/deep3", sparse.content);
    check(ok_s && !exists(path) && is_link(path2), "leftover: the follow removes a temporary beside its link");
    snprintf(path, sizeof(path), "%s/deep3/d.txt", tree_s);
    check(text_is(path, "d\n"), "leftover: the base behind the link is untouched");

    /* ---- one explode at a time ---------------------------------------------------- */
    {
        char lock[1100];
        char text[64];
        static char wait_short[] = "TORIRSMAPED_LANES_LOCK_WAIT_MS=300";
        static char wait_default[] = "TORIRSMAPED_LANES_LOCK_WAIT_MS=";

        snprintf(lock, sizeof(lock), "%s/explode.lock", sparse.dir);
        snprintf(text, sizeof(text), "pid %d\n", (int)getpid());
        write_text(lock, text);
        putenv(wait_short);
        /* Another process mid-explode: its lock, its half-built temporary. */
        explode_temp(&sparse, "quests6", path, sizeof(path));
        mkdirs(path);
        strcat(path, "/half.rs2");
        write_text(path, "half\n");
        check(!ToriRSMapEdLanes_MakeWritable(&sparse, "quests6/x.rs2", why, sizeof(why)) && strstr(why, "held by pid"),
            "lock: a live holder's explode is waited for, then refused");
        snprintf(path2, sizeof(path2), "%s/quests6", sparse.content);
        check(is_link(path2) && exists(path), "lock: the holder's link and its temporary are left alone");
        write_text(lock, "pid 4194000\n");
        check(ToriRSMapEdLanes_MakeWritable(&sparse, "quests6/x.rs2", why, sizeof(why)),
            "lock: a dead holder's lock is broken");
        if( why[0] )
            fprintf(stderr, "  why: %s\n", why);
        explode_temp(&sparse, "quests6", path, sizeof(path));
        snprintf(path2, sizeof(path2), "%s/quests6/q.rs2", sparse.content);
        check(!exists(lock) && !exists(path) && is_link(path2), "lock: released, the dead one's temporary rebuilt");
        putenv(wait_default);
    }

    /* ---- a deletion of a file the base gained after the lane ---------------------- */
    {
        struct ToriRSMapEdLaneDef looks;
        struct ToriRSMapEdLaneChange rows[16];
        int count;

        check(ToriRSMapEdLanes_Create(repo, tree_s, "looks", &looks, why, sizeof(why)), "looks: a new sparse lane");
        sleep(1);
        snprintf(path, sizeof(path), "%s/quests/q1/fresh.rs2", tree_s);
        write_text(path, "[proc,fresh]\n");
        sleep(1);
        snprintf(path, sizeof(path), "%s/quests/q1/fresh.rs2", looks.content);
        check(text_is(path, "[proc,fresh]\n"), "looks: a file the base gained later shows through the link");
        check(delete_rel(&looks, "quests/q1/fresh.rs2"), "looks: the lane deletes it");
        count = ToriRSMapEdLanes_Changes(&looks, rows, 16, NULL);
        check(count == 1 && find_change(rows, count, "quests/q1/fresh.rs2", TORIRSMAPED_LANE_CHANGE_DELETED),
            "looks: the deletion is the lane's change");
        check(ToriRSMapEdLanes_Follow(&looks, &cs, why, sizeof(why)) && cs.written == 0, "looks: a follow");
        check(!exists(path), "looks: the follow keeps it deleted");
        count = ToriRSMapEdLanes_Changes(&looks, rows, 16, NULL);
        check(count == 1 && find_change(rows, count, "quests/q1/fresh.rs2", TORIRSMAPED_LANE_CHANGE_DELETED),
            "looks: still the lane's one change after the follow");
    }

    /* ---- a directory the base has under an old time, which the lane lacks ---------- */
    {
        struct ToriRSMapEdLaneChange rows[64];
        int count;

        snprintf(path, sizeof(path), "%s/areas/old/f.npc", tree_s);
        write_text(path, "[old]\n");
        check(set_mtime(path, time(NULL) - 3600), "old dir: a file with an old time");
        snprintf(path, sizeof(path), "%s/areas/old", tree_s);
        check(set_mtime(path, time(NULL) - 3600), "old dir: its directory with an old time (as a rename keeps it)");
        ok_s = ToriRSMapEdLanes_Follow(&sparse, &cs, why, sizeof(why));
        snprintf(path, sizeof(path), "%s/areas/old", sparse.content);
        check(ok_s && !exists(path), "old dir: a follow does not link a directory older than the lane's look");
        check(save_text(&sparse, "areas/old/new.npc", "[new]\n"), "old dir: a save into it");
        snprintf(path, sizeof(path), "%s/areas/old/f.npc", sparse.content);
        check(is_link(path) && text_is(path, "[old]\n"), "old dir: filled from the base, not made empty");
        count = ToriRSMapEdLanes_Changes(&sparse, rows, 64, NULL);
        check(!find_change(rows, count, "areas/old/f.npc", TORIRSMAPED_LANE_CHANGE_DELETED)
                  && find_change(rows, count, "areas/old/new.npc", TORIRSMAPED_LANE_CHANGE_ADDED),
            "old dir: no base file in it reads as deleted");
    }

    /* ---- the base removes directories the sparse lane links -------------------------- */
    {
        int before;

        before = ToriRSMapEdLanes_Changes(&sparse, NULL, 0, NULL);
        snprintf(path, sizeof(path), "%s/gone1/g.txt", tree_s);
        unlink(path);
        snprintf(path, sizeof(path), "%s/gone1", tree_s);
        rmdir(path);
        snprintf(path, sizeof(path), "%s/gone2/sub/h.txt", tree_s);
        unlink(path);
        snprintf(path, sizeof(path), "%s/gone2/sub", tree_s);
        rmdir(path);
        snprintf(path, sizeof(path), "%s/gone2/g.txt", tree_s);
        unlink(path);
        snprintf(path, sizeof(path), "%s/gone2", tree_s);
        rmdir(path);
        check(!exists(path), "gone: the base removed gone1/ and gone2/");
        snprintf(path, sizeof(path), "%s/gone1", sparse.content);
        check(is_link(path), "gone: gone1 is still a (dangling) link in the lane");
        check(ToriRSMapEdLanes_Changes(&sparse, NULL, 0, NULL) == before, "gone: no change from a dangling link");
        check(ToriRSMapEdLanes_MakeWritable(&sparse, "gone1/x.txt", why, sizeof(why)),
            "gone: MakeWritable under a dangling link");
        check(exists(path) && !is_link(path), "gone: it is an empty real directory");
        ok_s = ToriRSMapEdLanes_Follow(&sparse, &cs, why, sizeof(why));
        if( !ok_s || cs.removed != 1 )
            fprintf(stderr, "  follow sparse: %s (written %d, removed %d)\n", why, cs.written, cs.removed);
        check(ok_s && cs.removed == 1, "gone: the follow drops the dangling link, counted");
        snprintf(path, sizeof(path), "%s/gone2", sparse.content);
        check(!exists(path), "gone: gone2 is gone from the lane");
        check(ToriRSMapEdLanes_Changes(&sparse, NULL, 0, NULL) == before, "gone: still no change");
        check(ToriRSMapEdLanes_MakeWritable(&sparse, "gone2/sub/y.txt", why, sizeof(why)),
            "gone: MakeWritable under a directory neither side has");
        snprintf(path, sizeof(path), "%s/gone2/sub", sparse.content);
        check(exists(path) && !is_link(path), "gone: real directories, made empty");
    }

    /* ---- a new directory under the copied tree ------------------------------------ */
    {
        const char* const trees[2] = { tree_s, tree_l };
        const struct ToriRSMapEdLaneDef* const lanes[2] = { &sparse, &legacy };

        sleep(1);
        for( int t = 0; t < 2; t++ )
        {
            snprintf(path, sizeof(path), "%s/pack/sub/new.alloc", trees[t]);
            write_text(path, "n=1\n");
        }
        ok_s = ToriRSMapEdLanes_Follow(&sparse, &cs, why, sizeof(why));
        ok_l = ToriRSMapEdLanes_Follow(&legacy, &cl, why, sizeof(why));
        check(ok_s && ok_l && cs.written == 1 && cl.written == 1, "pack: both follow the new copied directory");
        for( int t = 0; t < 2; t++ )
        {
            snprintf(path, sizeof(path), "%s/pack/sub/new.alloc", lanes[t]->content);
            snprintf(path2, sizeof(path2), "%s/ancestor/pack/sub/new.alloc", lanes[t]->dir);
            check(is_regular(path) && text_is(path, "n=1\n") && is_regular(path2),
                t == 0 ? "pack: the sparse lane copies it, with its ancestor"
                       : "pack: the legacy lane copies it, with its ancestor");
        }
    }
}

/** A plain write into `path` IN PLACE, the way a text editor saves (VS Code,
 *  Notepad): the file that is there is opened and written, never replaced. */
static void
write_in_place(
    const char* path,
    const char* text)
{
    FILE* file = fopen(path, "r+b");
    assert(file);
    fputs(text, file);
    fclose(file);
}

/** Handing a path to a program outside the editor (ToriRSMapEdLanes_HandOut):
 *  a linked file becomes the lane's own, an in-place write to it stays in the
 *  lane, a file made by hand in a handed-out folder is the lane's; a hand-out
 *  nobody wrote is no change, follows the base as a copy, and is never a link
 *  again -- after a discard or a publish with reset either. */
static void
hand_out(const char* scratch)
{
    char repo[1024];
    char tree[1024];
    char path[2048];
    char base[2048];
    char why[1024];
    struct ToriRSMapEdLaneDef lane;
    struct ToriRSMapEdLaneDef main_lane;
    struct ToriRSMapEdLaneChange rows[16];
    struct ToriRSMapEdLaneCounts counts;
    int count;

    snprintf(repo, sizeof(repo), "%s/handout", scratch);
    snprintf(tree, sizeof(tree), "%s/handout/OSRS-Content/osrs-content", scratch);
    write_tree(tree);
    check(ToriRSMapEdLanes_Create(repo, tree, "q", &lane, why, sizeof(why)), "hand-out: a lane");
    ToriRSMapEdLanes_Main(repo, tree, &main_lane);

    /* Before: the folder is one link, so its file is the base's file. */
    snprintf(path, sizeof(path), "%s/quests", lane.content);
    check(is_link(path), "hand-out: quests starts as one link to the base's");
    check(ToriRSMapEdLanes_HandOut(&main_lane, "quests/q1/a.rs2", why, sizeof(why)), "hand-out: main needs nothing");

    /* ---- a file: real, with the same bytes, and no change ---------------- */
    check(ToriRSMapEdLanes_HandOut(&lane, "quests/q1/a.rs2", why, sizeof(why)), "hand-out: a linked file");
    if( why[0] )
        fprintf(stderr, "  why: %s\n", why);
    snprintf(path, sizeof(path), "%s/quests/q1", lane.content);
    check(!is_link(path) && exists(path), "hand-out: its folder is real");
    snprintf(path, sizeof(path), "%s/quests/q1/a.rs2", lane.content);
    check(is_regular(path) && text_is(path, "[proc,a]\n"), "hand-out: the file is the lane's own, same bytes");
    snprintf(path, sizeof(path), "%s/quests/q1/b.rs2", lane.content);
    check(is_link(path), "hand-out: its neighbour is a link still");
    snprintf(path, sizeof(path), "%s/ancestor/quests/q1/a.rs2", lane.dir);
    check(text_is(path, "[proc,a]\n"), "hand-out: the bytes handed are recorded");
    check(ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == 0, "hand-out: a file nobody wrote is no change");
    check(ToriRSMapEdLanes_HandOut(&lane, "quests/q1/a.rs2", why, sizeof(why)), "hand-out: again is a no-op");

    /* ---- an editor saves it in place: the lane's, never the base's ------- */
    snprintf(path, sizeof(path), "%s/quests/q1/a.rs2", lane.content);
    write_in_place(path, "[proc,a]\n// mine\n");
    snprintf(base, sizeof(base), "%s/quests/q1/a.rs2", tree);
    check(text_is(base, "[proc,a]\n"), "hand-out: an in-place save does not reach the base");
    count = ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts);
    check(count == 1 && find_change(rows, count, "quests/q1/a.rs2", TORIRSMAPED_LANE_CHANGE_MODIFIED),
        "hand-out: the save is the lane's one change");
    check(ToriRSMapEdLanes_Follow(&lane, &counts, why, sizeof(why)) && text_is(path, "[proc,a]\n// mine\n"),
        "hand-out: a follow keeps the lane's edit");

    /* ---- a folder: a file made in it by hand is the lane's -------------- */
    check(ToriRSMapEdLanes_HandOut(&lane, "quests2", why, sizeof(why)), "hand-out: a linked folder");
    snprintf(path, sizeof(path), "%s/quests2", lane.content);
    check(!is_link(path) && exists(path), "hand-out: the folder is real");
    snprintf(path, sizeof(path), "%s/quests2/q.rs2", lane.content);
    check(is_link(path), "hand-out: the folder's files are links until each is asked for");
    snprintf(path, sizeof(path), "%s/quests2/new.rs2", lane.content);
    write_text(path, "[proc,new]\n");
    snprintf(base, sizeof(base), "%s/quests2/new.rs2", tree);
    check(!exists(base), "hand-out: a file made by hand in it is not in the base");
    count = ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts);
    check(count == 2 && find_change(rows, count, "quests2/new.rs2", TORIRSMAPED_LANE_CHANGE_ADDED),
        "hand-out: ...and is the lane's added file");

    /* ---- a path that is not there yet: its folders are made real ---------- */
    check(ToriRSMapEdLanes_HandOut(&lane, "quests3/fresh/x.rs2", why, sizeof(why)), "hand-out: a file yet to be made");
    snprintf(path, sizeof(path), "%s/quests3", lane.content);
    check(!is_link(path) && exists(path), "hand-out: the folder above it is real");
    snprintf(path, sizeof(path), "%s/quests3/fresh", lane.content);
    check(!is_link(path) && exists(path), "hand-out: the folder it goes in is made");
    snprintf(base, sizeof(base), "%s/quests3/fresh", tree);
    check(!exists(base), "hand-out: ...in the lane, not in the base");
    check(!ToriRSMapEdLanes_HandOut(&lane, "../escape.rs2", why, sizeof(why)) && why[0],
        "hand-out: a path that leaves the lane is refused, with why");

    /* ---- the base moves under a hand-out nobody wrote -------------------- */
    check(ToriRSMapEdLanes_HandOut(&lane, "quests4/q.rs2", why, sizeof(why)), "hand-out: a second file");
    snprintf(base, sizeof(base), "%s/quests4/q.rs2", tree);
    remove(base);
    write_text(base, "[proc,q4]\n// the base moved\n");
    check(ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == 2,
        "hand-out: the base's newer bytes are not the lane's change");
    check(ToriRSMapEdLanes_Follow(&lane, &counts, why, sizeof(why)), "hand-out: follow");
    snprintf(path, sizeof(path), "%s/quests4/q.rs2", lane.content);
    check(is_regular(path) && text_is(path, "[proc,q4]\n// the base moved\n"),
        "hand-out: the follow gives it the base's bytes as a COPY, not a link");
    write_in_place(path, "[proc,q4]\n// the base moved\n// then me\n");
    check(text_is(base, "[proc,q4]\n// the base moved\n"),
        "hand-out: an in-place save after the follow still stays in the lane");

    /* ---- discard: the base's bytes again, and still not a link ----------- */
    check(ToriRSMapEdLanes_Discard(&lane, &counts, why, sizeof(why)), "hand-out: discard");
    snprintf(path, sizeof(path), "%s/quests/q1/a.rs2", lane.content);
    check(is_regular(path) && text_is(path, "[proc,a]\n"),
        "hand-out: a discarded hand-out holds the base's bytes and is a real file still");
    write_in_place(path, "[proc,a]\n// again\n");
    snprintf(base, sizeof(base), "%s/quests/q1/a.rs2", tree);
    check(text_is(base, "[proc,a]\n"), "hand-out: ...so an editor that still has it open cannot write the base");
    check(ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == 1, "hand-out: and that save is a change again");

    /* ---- publish with reset: published, and still not a link ------------- */
    check(ToriRSMapEdLanes_Publish(&lane, &main_lane, TORIRSMAPED_LANE_PUBLISH_RESET, &counts, why, sizeof(why)),
        "hand-out: publish to the base with reset");
    check(text_is(base, "[proc,a]\n// again\n"), "hand-out: the base took the edit");
    check(is_regular(path) && ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == 0,
        "hand-out: the lane is clean and the file is its own still");

    /* ---- the base's file goes: the untouched copy goes with it ----------- */
    remove(base);
    check(ToriRSMapEdLanes_Follow(&lane, &counts, why, sizeof(why)) && !exists(path),
        "hand-out: a hand-out nobody wrote goes when the base's file goes");

    /* ---- a folder shown in a file manager: its files, every level -------- */
    {
        static const char* const k_deep[] = { "deep/top.txt", "deep/a/sib1/s.txt", "deep/a/b/sib2.txt",
            "deep/a/b/c/file.txt" };
        int const before = ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts);
        int files = 0;
        int real = 0;

        check(ToriRSMapEdLanes_HandOutFolder(&main_lane, "deep", 16, &files, why, sizeof(why)) && files == 0,
            "hand-out folder: main needs nothing");
        check(ToriRSMapEdLanes_HandOutFolder(&lane, "deep", 3, &files, why, sizeof(why)) && files == -1,
            "hand-out folder: more files than the cap is said (-1)");
        snprintf(path, sizeof(path), "%s/deep", lane.content);
        check(!is_link(path) && exists(path), "hand-out folder: past the cap the folder itself is real");
        snprintf(path, sizeof(path), "%s/deep/top.txt", lane.content);
        check(is_link(path), "hand-out folder: ...and its files are links still");
        checkf(ToriRSMapEdLanes_HandOutFolder(&lane, "deep", 16, &files, why, sizeof(why)) && files == 4,
            "hand-out folder: the four files under it (%d)", files);
        for( int i = 0; i < 4; i++ )
        {
            snprintf(path, sizeof(path), "%s/%s", lane.content, k_deep[i]);
            real += is_regular(path) && !is_link(path);
        }
        checkf(real == 4, "hand-out folder: every file, every level down, is the lane's own (%d of 4)", real);
        check(ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == before,
            "hand-out folder: files nobody wrote are no change");
        snprintf(path, sizeof(path), "%s/deep/a/b/sib2.txt", lane.content);
        write_in_place(path, "sib2\n// mine\n");
        snprintf(base, sizeof(base), "%s/deep/a/b/sib2.txt", tree);
        check(text_is(base, "sib2\n"), "hand-out folder: a neighbour saved in place does not reach the base");
        check(ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts) == before + 1,
            "hand-out folder: ...and is the lane's change");
    }
}

/** A lane's life-cycle (torirs_maped_lanes.h, 2026-11-11): the boot lane
 *  file's place and a run that ignores it, one change discarded, the base's
 *  copy for a diff, what a publish would overwrite, and deleting a lane --
 *  with the base tree, and the lane beside it, the same before and after. */
static void
life_cycle(const char* scratch)
{
    static struct view base_before;
    static struct view base_after;
    static struct view keep_before;
    static struct view keep_after;
    char repo[1024];
    char tree[1024];
    char path[2048];
    char boot[2048];
    char why[1024];
    char name[64];
    char overlap[4][TORIRSMAPED_LANE_PATH];
    struct ToriRSMapEdLaneDef lane;
    struct ToriRSMapEdLaneDef keep;
    struct ToriRSMapEdLaneDef gone;
    struct ToriRSMapEdLaneDef list[8];
    struct ToriRSMapEdLaneChange rows[16];
    struct ToriRSMapEdLaneCounts counts;
    struct ToriRSMapEdLaneDeleted deleted;
    int count;
    int ours = -1;
    int theirs = -1;

    snprintf(repo, sizeof(repo), "%s/life", scratch);
    snprintf(tree, sizeof(tree), "%s/life/OSRS-Content/osrs-content", scratch);
    write_tree(tree);
    check(ToriRSMapEdLanes_Create(repo, tree, "scratch1", &lane, why, sizeof(why)), "life: a scratch lane");
    check(ToriRSMapEdLanes_Create(repo, tree, "keep", &keep, why, sizeof(why)), "life: a lane beside it");

    /* ---- the boot lane file: where it is, and a run that ignores it ---- */
    ToriRSMapEdLanes_BootFilePath(repo, path, sizeof(path));
    check(strlen(path) > 20 && strcmp(path + strlen(path) - 21, "/build/lanes/lane.ini") == 0,
        "boot file: build/lanes/lane.ini by default");
    snprintf(boot, sizeof(boot), "%s/life/own_boot.ini", scratch);
    setenv("TORIRSMAPED_BOOT_LANE_FILE", boot, 1);
    ToriRSMapEdLanes_BootFilePath(repo, path, sizeof(path));
    check(strcmp(path, boot) == 0, "boot file: TORIRSMAPED_BOOT_LANE_FILE names another");
    check(ToriRSMapEdLanes_SetBoot(repo, tree, "keep", why, sizeof(why)) && is_regular(boot),
        "boot file: a switch writes the named file");
    snprintf(path, sizeof(path), "%s/build/lanes/lane.ini", repo);
    check(!exists(path), "boot file: ...and the repo's own is not created");
    check(ToriRSMapEdLanes_BootName(repo, name, sizeof(name)) && strcmp(name, "keep") == 0,
        "boot file: the named file is the one read");
    setenv("TORIRSMAPED_IGNORE_BOOT_LANE", "1", 1);
    check(ToriRSMapEdLanes_BootIgnored(), "boot file: TORIRSMAPED_IGNORE_BOOT_LANE=1 is a run that ignores it");
    check(!ToriRSMapEdLanes_SetBoot(repo, tree, "scratch1", why, sizeof(why)) && strstr(why, "was not recorded")
              && strstr(why, "lane=scratch1"),
        "boot file: such a run's switch is refused, and says how to open the lane");
    check(text_is(boot, "; The lane every editor boot on this repo serves (torirsmaped/torirs_maped_lanes.h),\n"
                        "; whatever its manifest or --lane asks: written by the Lanes panel's Switch and\n"
                        "; Scratch lane (LANE_SWITCH). Delete this file to boot as the manifest says.\n"
                        "lane = keep\n"),
        "boot file: ...with the file as it was");
    setenv("TORIRSMAPED_IGNORE_BOOT_LANE", "0", 1);
    check(!ToriRSMapEdLanes_BootIgnored(), "boot file: =0 reads and writes it as ever");
    unsetenv("TORIRSMAPED_IGNORE_BOOT_LANE");
    check(!ToriRSMapEdLanes_BootIgnored(), "boot file: unset too");

    /* ---- three changes; one of them put back ---- */
    check(save_text(&lane, "maps/m1.jm2", "square 1 edited\n") && save_text(&lane, "maps/m2.jm2", "square 2 edited\n")
              && save_text(&lane, "quests/q1/new.rs2", "[proc,new]\n") && delete_rel(&lane, "deep/top.txt"),
        "life: two edits, an added file and a deletion");
    count = ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts);
    check(count == 4 && counts.modified == 2 && counts.added == 1 && counts.deleted == 1, "life: four changes");
    check(!ToriRSMapEdLanes_DiscardOne(&lane, "areas/x/configs/x.npc", &counts, why, sizeof(why))
              && strstr(why, "is not a change"),
        "discard one: a path that is no change is refused");
    check(ToriRSMapEdLanes_DiscardOne(&lane, "maps/m1.jm2", &counts, why, sizeof(why)) && counts.written == 1
              && counts.removed == 0,
        "discard one: a modified file goes back");
    snprintf(path, sizeof(path), "%s/maps/m1.jm2", lane.content);
    check(is_link(path) && text_is(path, "square 1\n"), "discard one: it is the base's again, a link");
    count = ToriRSMapEdLanes_Changes(&lane, rows, 16, &counts);
    check(count == 3 && find_change(rows, count, "maps/m2.jm2", TORIRSMAPED_LANE_CHANGE_MODIFIED)
              && find_change(rows, count, "quests/q1/new.rs2", TORIRSMAPED_LANE_CHANGE_ADDED)
              && find_change(rows, count, "deep/top.txt", TORIRSMAPED_LANE_CHANGE_DELETED),
        "discard one: the other three stay");

    /* ---- the base's copy for a diff ---- */
    check(ToriRSMapEdLanes_DiffBase(&lane, "maps/m2.jm2", path, sizeof(path), why, sizeof(why)),
        "diff: a modified file has a base to compare");
    check(strlen(path) > 22 && strcmp(path + strlen(path) - 22, "/diff/maps/m2.base.jm2") == 0
              && strncmp(path, lane.dir, strlen(lane.dir)) == 0,
        "diff: the copy is <lane dir>/diff/maps/m2.base.jm2");
    check(is_regular(path) && text_is(path, "square 2\n"), "diff: a real file holding the base's bytes");
    write_text(path, "scribbled on by a diff tool\n");
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", tree);
    check(text_is(path, "square 2\n"), "diff: writing the copy does not write the base");
    check(ToriRSMapEdLanes_Changes(&lane, NULL, 0, NULL) == 3, "diff: the copy is no change of the lane's");
    check(!ToriRSMapEdLanes_DiffBase(&lane, "quests/q1/new.rs2", path, sizeof(path), why, sizeof(why))
              && strstr(why, "is new in lane"),
        "diff: an added file has no base, said");
    check(!ToriRSMapEdLanes_DiffBase(&lane, "deep/top.txt", path, sizeof(path), why, sizeof(why))
              && strstr(why, "was deleted by lane"),
        "diff: a deleted file has no lane file, said");
    check(!ToriRSMapEdLanes_DiffBase(&lane, "../life/x", path, sizeof(path), why, sizeof(why))
              && !ToriRSMapEdLanes_DiffBase(&lane, "/etc/passwd", path, sizeof(path), why, sizeof(why)),
        "diff: a path outside the lane is refused");

    /* ---- what a publish into another lane would overwrite ---- */
    check(save_text(&keep, "maps/m2.jm2", "square 2 by keep\n") && save_text(&keep, "areas/x/configs/x.npc", "[kept]\n")
              && save_text(&keep, "quests/q1/new.rs2", "[proc,new]\n") && delete_rel(&keep, "deep/top.txt"),
        "life: the lane beside it changes one file differently, one of its own, and two the very same way");
    count = ToriRSMapEdLanes_PublishOverlap(&lane, &keep, overlap, 4, &ours, &theirs);
    check(count == 1 && strcmp(overlap[0], "maps/m2.jm2") == 0 && ours == 3 && theirs == 4,
        "overlap: the one file both changed DIFFERENTLY (the same bytes and the same deletion are nothing lost)");
    ToriRSMapEdLanes_Main(repo, tree, &gone);
    check(ToriRSMapEdLanes_PublishOverlap(&lane, &gone, NULL, 0, NULL, NULL) == 0, "overlap: the main tree has none");

    /* ---- deleting a lane ---- */
    check(!ToriRSMapEdLanes_Delete(repo, tree, "main", &deleted, why, sizeof(why)) && strstr(why, "base tree"),
        "delete: main is refused");
    check(!ToriRSMapEdLanes_Delete(repo, tree, "nowhere", &deleted, why, sizeof(why)) && strstr(why, "no lane 'nowhere'"),
        "delete: a lane that does not exist is refused");
    check(!ToriRSMapEdLanes_Delete(repo, tree, "Bad Name", &deleted, why, sizeof(why)), "delete: a bad name is refused");
    check(!ToriRSMapEdLanes_Delete(repo, tree, "keep", &deleted, why, sizeof(why)) && strstr(why, "every launch opens"),
        "delete: the lane the boot lane file names is refused");
    snprintf(path, sizeof(path), "%s/lane.ini", keep.dir);
    check(is_regular(path), "delete: ...and is all there");
    unlink(boot);
    unsetenv("TORIRSMAPED_BOOT_LANE_FILE");
    snprintf(path, sizeof(path), "%s/.editor-session.lock", lane.content);
    snprintf(why, sizeof(why), "pid %ld\nsince 0\ncmd lanes_test\n", (long)getpid());
    write_text(path, why);
    check(!ToriRSMapEdLanes_Delete(repo, tree, "scratch1", &deleted, why, sizeof(why))
              && strstr(why, "open in an editor"),
        "delete: a lane an editor holds is refused");
    unlink(path);
    snprintf(path, sizeof(path), "%s/maps/m2.jm2", lane.content);
    check(text_is(path, "square 2 edited\n"), "delete: ...with its edits where they were");

    view_take(tree, &base_before);
    view_take(keep.content, &keep_before);
    check(ToriRSMapEdLanes_Delete(repo, tree, "scratch1", &deleted, why, sizeof(why)), "delete: the scratch lane goes");
    if( why[0] )
        fprintf(stderr, "  delete: %s\n", why);
    checkf(deleted.links > 0 && deleted.files >= 3 && deleted.dirs > 0,
        "delete: links dropped by name, the lane's own files removed (%d links, %d files, %d directories)",
        deleted.links, deleted.files, deleted.dirs);
    check(!exists(lane.dir), "delete: build/lanes/scratch1 is gone");
    snprintf(path, sizeof(path), "%s/build/lanes/.scratch1.deleting", repo);
    check(!exists(path), "delete: nothing left aside");
    check(!ToriRSMapEdLanes_Lookup(repo, tree, "scratch1", &gone), "delete: it is no lane");
    count = ToriRSMapEdLanes_List(repo, tree, list, 8);
    check(count == 2 && strcmp(list[1].name, "keep") == 0, "delete: the list is main and the lane beside it");
    view_take(tree, &base_after);
    view_take(keep.content, &keep_after);
    check(view_same(&base_before, &base_after, "base"), "delete: every file of the base tree is as it was");
    check(view_same(&keep_before, &keep_after, "keep"), "delete: and every file the other lane shows");
    snprintf(path, sizeof(path), "%s/maps/m1.jm2", tree);
    /* (The other lane links it too, so by link count it reads as a link.) */
    check(exists(path) && text_is(path, "square 1\n"), "delete: a file the lane linked is still the base's");
    check(!ToriRSMapEdLanes_Delete(repo, tree, "scratch1", &deleted, why, sizeof(why)) && strstr(why, "no lane 'scratch1'"),
        "delete: a second delete finds nothing");

    /* A delete that stopped part-way left its directory aside: the next
     * delete of the name finishes it. */
    snprintf(path, sizeof(path), "%s/build/lanes/.halfway.deleting/content/maps/left.jm2", repo);
    write_text(path, "left behind\n");
    check(ToriRSMapEdLanes_Delete(repo, tree, "halfway", &deleted, why, sizeof(why)) && deleted.files == 1,
        "delete: what an interrupted delete left aside is finished");
    snprintf(path, sizeof(path), "%s/build/lanes/.halfway.deleting", repo);
    check(!exists(path), "delete: ...and gone");
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
    /* The sparse layout (2026-11-10): whole directories are links until a
     * write explodes them; the ones the tools write into are real. */
    check(work.sparse, "create: the lane is sparse");
    snprintf(path, sizeof(path), "%s/areas", work.content);
    check(is_link(path), "sparse: a directory nothing wrote into is one link");
    snprintf(path, sizeof(path), "%s/server", work.content);
    check(exists(path) && !is_link(path), "sparse: server/ is real (its pack outputs stay out)");
    snprintf(path, sizeof(path), "%s/server/scripts", work.content);
    check(exists(path) && !is_link(path), "sparse: server/scripts/ is real");
    snprintf(path, sizeof(path), "%s/pack", work.content);
    check(exists(path) && !is_link(path), "sparse: pack/ is real");
    snprintf(path, sizeof(path), "%s/lane.ini", work.dir);
    {
        char ini[1024];
        size_t n;
        FILE* file = fopen(path, "rb");
        assert(file);
        n = fread(ini, 1, sizeof(ini) - 1, file);
        fclose(file);
        ini[n] = '\0';
        check(strstr(ini, "\nlayout = sparse\n") && strstr(ini, "\nsparse_base = "), "sparse: lane.ini says so");
        /* An editor that predates sparse lanes reads `base = ` at a line's
         * start and refuses a lane without one. */
        check(strncmp(ini, "base = ", 7) != 0 && !strstr(ini, "\nbase = "), "sparse: lane.ini has no `base = `");
    }
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
    check(is_link(path), "reset: the published ledger is a link (a copy again at the next follow)");
    check(ToriRSMapEdLanes_Changes(&work, rows, 16, &counts) == 0, "reset: the working lane is clean");

    /* ---- discard ------------------------------------------------------- */
    /* areas/ is a link in the sparse lane: the editor's rule, before any
     * write into a lane, is MakeWritable. */
    snprintf(path, sizeof(path), "%s/areas", work.content);
    check(is_link(path), "discard: areas/ is still a link before the edit");
    check(ToriRSMapEdLanes_MakeWritable(&work, "areas/lumbridge/configs/lumbridge.npc", why, sizeof(why))
              && ToriRSMapEdLanes_MakeWritable(&work, "areas/lumbridge/configs/new.npc", why, sizeof(why)),
        "discard: the edits' directories made writable");
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

    /* ---- follow: the mirror catches up with its base -------------------- */
    snprintf(path, sizeof(path), "%s/ancestor/pack/npc.alloc", fresh.dir);
    check(is_regular(path), "create: the copied tree's ancestor");
    snprintf(path, sizeof(path), "%s/meta.ini", work.content);
    unlink(path);
    sleep(1);
    snprintf(path, sizeof(path), "%s/server/scripts/later.rs2", tree);
    write_text(path, "[proc,later]\n");
    snprintf(path, sizeof(path), "%s/areas/varrock/configs/varrock.npc", tree);
    write_text(path, "[zaff]\n");
    snprintf(path, sizeof(path), "%s/pack/npc.server", tree);
    write_text(path, "hans\n");
    snprintf(path, sizeof(path), "%s/server/scripts/hello.rs2", tree);
    unlink(path);
    check(ToriRSMapEdLanes_Follow(&work, &counts, why, sizeof(why)) && counts.written == 4 && counts.removed == 1,
        "follow: 4 linked or copied, 1 dropped");
    if( why[0] || counts.written != 4 || counts.removed != 1 )
        fprintf(stderr, "  why: %s (written %d, removed %d)\n", why, counts.written, counts.removed);
    snprintf(path, sizeof(path), "%s/maps/m99_99.jm2", work.content);
    check(is_link(path), "follow: a base file younger than the lane is linked");
    snprintf(path, sizeof(path), "%s/server/scripts/later.rs2", work.content);
    check(is_link(path), "follow: a new script is linked");
    snprintf(path, sizeof(path), "%s/areas/varrock", work.content);
    check(is_link(path), "follow: a new directory is one directory link (sparse)");
    snprintf(path, sizeof(path), "%s/areas/varrock/configs/varrock.npc", work.content);
    check(text_is(path, "[zaff]\n"), "follow: the new directory's file reads through it");
    snprintf(path, sizeof(path), "%s/pack/npc.server", work.content);
    check(is_regular(path) && text_is(path, "hans\n"), "follow: a new copied file is a copy");
    snprintf(path, sizeof(path), "%s/server/scripts/hello.rs2", work.content);
    check(!exists(path), "follow: a link the base's deletion left dangling is dropped");
    snprintf(path, sizeof(path), "%s/meta.ini", work.content);
    check(!exists(path), "follow: the lane's own deletion stays deleted");
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", work.content);
    check(is_regular(path), "follow: a re-linked copy is a copy again");
    snprintf(path, sizeof(path), "%s/lane.ini", work.dir);
    {
        struct ToriRSMapEdLaneDef again;
        check(ToriRSMapEdLanes_Lookup(repo, tree, "working", &again) && again.created == work.created,
            "follow: lane.ini's followed is the mirror's new last look");
    }
    check(ToriRSMapEdLanes_Changes(&work, rows, 16, &counts) == 1 && counts.deleted == 1,
        "follow: the lane's one change is its deletion");

    /* The copies against their ancestor. */
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", tree);
    write_text(path, "hans=1\nbob=2\nzaff=3\n");
    snprintf(path, sizeof(path), "%s/pack/npc.server", work.content);
    write_text(path, "hans\nbob\n");
    check(ToriRSMapEdLanes_Follow(&work, &counts, why, sizeof(why)) && counts.written == 1,
        "follow: one copy refreshed");
    snprintf(path, sizeof(path), "%s/pack/npc.alloc", work.content);
    check(text_is(path, "hans=1\nbob=2\nzaff=3\n"), "follow: a copy the lane never changed takes the base's");
    snprintf(path, sizeof(path), "%s/pack/npc.server", work.content);
    check(text_is(path, "hans\nbob\n"), "follow: the lane's own edit of a copy is kept");
    snprintf(path, sizeof(path), "%s/pack/npc.server", tree);
    write_text(path, "hans\nzaff\n");
    check(!ToriRSMapEdLanes_Follow(&work, &counts, why, sizeof(why)) && strstr(why, "pack/npc.server")
              && strstr(why, "AND in the main tree"),
        "follow: a copy both sides changed is refused, named");
    snprintf(path, sizeof(path), "%s/pack/npc.server", work.content);
    check(text_is(path, "hans\nbob\n"), "follow: a refused copy is left as the lane had it");
    write_text(path, "hans\nzaff\n");
    check(ToriRSMapEdLanes_Follow(&work, &counts, why, sizeof(why)), "follow: agreeing again, it follows");
    snprintf(path, sizeof(path), "%s/meta.ini", work.content);
    check(ToriRSMapEdLanes_Discard(&work, &counts, why, sizeof(why)) && is_link(path), "follow: discard after it");
    check(ToriRSMapEdLanes_Follow(&main_lane, &counts, why, sizeof(why)) && counts.written == 0,
        "follow: main has nothing to follow");

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

    sparse_against_legacy(argv[1]);
    fell_behind(argv[1]);
    life_cycle(argv[1]);
    hand_out(argv[1]);

    printf("lanes_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
