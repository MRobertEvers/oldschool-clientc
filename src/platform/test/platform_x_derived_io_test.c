/*
 * test-platform-derived-io: the desktop executor's derived records
 * (asyncio.h TORIRS_IOK_DERIVED_READ / _WRITE; docs/CS2_JIT.md 4.5) against a
 * real PlatformX_IO on a scratch directory.
 *
 *   platform_x_derived_io_test <scratch dir>     (emptied and recreated)
 *
 * An absent record is -1 with no data and no log line; a write then a read
 * round-trips; the record lives at <cache dir>.derived/<name>, beside the
 * cache and never in it, and no temporary file is left behind; a missing
 * directory is created on demand; TORIRS_DERIVED_DIR moves the location; bad
 * names, an empty or "." cache directory and a read-only location fail
 * without crashing, and every one is counted by PlatformX_IO_DerivedStats.
 */

#include "asyncio.h"
#include "platform/platform_x_io.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail;
static int g_ok;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            g_ok++;                                                                                \
        else                                                                                       \
        {                                                                                          \
            g_fail++;                                                                              \
            printf("FAIL: %s:%d: %s -- ", __FILE__, __LINE__, #cond);                              \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

static char g_root[1024];

static void
rm_rf(char const* path)
{
    char cmd[2048];
    /* The scratch directory the make target names, under its own objdir. */
    snprintf(cmd, sizeof(cmd), "chmod -R u+w '%s' 2>/dev/null; rm -rf '%s'", path, path);
    (void)system(cmd);
}

static int
file_size(char const* path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (int)st.st_size : -1;
}

static int
dir_has_tmp(char const* dir)
{
    DIR* d = opendir(dir);
    struct dirent* e;
    int found = 0;

    if( !d )
        return 0;
    while( (e = readdir(d)) )
    {
        size_t const n = strlen(e->d_name);
        if( n > 4 && strcmp(e->d_name + n - 4, ".tmp") == 0 )
            found = 1;
    }
    closedir(d);
    return found;
}

/* One item through Process, as the runner would hand it over. */
struct Rig
{
    struct ToriRS_Task task;
    struct ToriRS_IOBatch* io;
};

static void
rig_init(struct Rig* rig)
{
    memset(rig, 0, sizeof(*rig));
    rig->io = ToriRS_IOBatch_New();
    rig->io->task = &rig->task;
}

static int
rig_read(
    struct Rig* rig,
    struct PlatformX_IO* px,
    char const* name,
    void** out,
    int* out_size)
{
    struct ToriRS_IOItem* item;
    int err;

    ToriRS_IO_QueueDerivedRead(rig->io, 0, name);
    PlatformX_IO_Process(px, rig->io);
    item = ToriRS_IO_TaskSlot(rig->io, 0);
    CHECK(item->pending == 0, "the desktop answers inside Process");
    err = item->error_code;
    *out = NULL;
    *out_size = item->data_size;
    if( item->data )
    {
        *out = malloc((size_t)item->data_size + 1);
        memcpy(*out, item->data, (size_t)item->data_size);
    }
    ToriRS_IO_ClearItem(item);
    return err;
}

static int
rig_write(
    struct Rig* rig,
    struct PlatformX_IO* px,
    char const* name,
    void* data,
    int size)
{
    struct ToriRS_IOItem* item;
    int err;

    ToriRS_IO_QueueDerivedWrite(rig->io, 0, name, data, size);
    PlatformX_IO_Process(px, rig->io);
    item = ToriRS_IO_TaskSlot(rig->io, 0);
    err = item->error_code;
    CHECK(item->data == data, "a write's lent bytes stay the caller's");
    ToriRS_IO_ClearItem(item);
    return err;
}

/* A hand-made item, for names the queue itself would refuse. */
static int
raw_load(
    struct PlatformX_IO* px,
    int kind,
    char const* name)
{
    struct ToriRS_IOItem item;
    static char bytes[4] = "abc";

    memset(&item, 0, sizeof(item));
    item.kind = (enum ToriRS_IOKind)kind;
    if( name )
        snprintf(item.u.derived.name, sizeof(item.u.derived.name), "%s", name);
    else
        memset(item.u.derived.name, 'a', sizeof(item.u.derived.name)); /* no terminator */
    if( kind == TORIRS_IOK_DERIVED_WRITE )
    {
        item.data = bytes;
        item.data_size = 3;
    }
    PlatformX_IO_LoadItem(px, &item);
    CHECK(kind != TORIRS_IOK_DERIVED_READ || item.data == NULL, "a refused read carries no data");
    return item.error_code;
}

int
main(int argc, char** argv)
{
    struct Rig rig;
    struct PlatformX_IO* px;
    struct PlatformX_IO_DerivedStats st;
    char cache_dir[1100];
    char path[1300];
    char expect[1300];
    char log_path[1100];
    unsigned char payload[3000];
    void* got;
    int got_size;
    int saved_err;
    int log_fd;

    if( argc < 2 )
    {
        printf("usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    snprintf(g_root, sizeof(g_root), "%s", argv[1]);
    rm_rf(g_root);
    mkdir(g_root, 0755);
    unsetenv("TORIRS_DERIVED_DIR");
    for( size_t i = 0; i < sizeof(payload); i++ )
        payload[i] = (unsigned char)(i * 31u + 7u);
    rig_init(&rig);
    px = PlatformX_IO_New();

    /* ---- location -------------------------------------------------------- */
    CHECK(PlatformX_IO_DerivedDir(px) == NULL, "no location before InitCacheId");
    snprintf(cache_dir, sizeof(cache_dir), "%s/cache.test", g_root);
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, cache_dir);
    snprintf(expect, sizeof(expect), "%s/cache.test.derived", g_root);
    CHECK(PlatformX_IO_DerivedDir(px) && strcmp(PlatformX_IO_DerivedDir(px), expect) == 0,
        "the sibling: %s", PlatformX_IO_DerivedDir(px) ? PlatformX_IO_DerivedDir(px) : "(null)");
    snprintf(path, sizeof(path), "%s/cache.test//", g_root);
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, path);
    CHECK(PlatformX_IO_DerivedDir(px) && strcmp(PlatformX_IO_DerivedDir(px), expect) == 0,
        "trailing separators stripped: %s", PlatformX_IO_DerivedDir(px));

    /* ---- absent: -1, no data, no log line --------------------------------- */
    snprintf(log_path, sizeof(log_path), "%s/stderr.txt", g_root);
    fflush(stderr);
    saved_err = dup(2);
    log_fd = open(log_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    dup2(log_fd, 2);
    close(log_fd);
    CHECK(rig_read(&rig, px, "cs2jit-00000000-v1.pack", &got, &got_size) == -1, "absent reads -1");
    fflush(stderr);
    dup2(saved_err, 2);
    close(saved_err);
    CHECK(got == NULL && got_size == 0, "absent carries no data");
    CHECK(file_size(log_path) == 0, "absent says nothing (%d bytes logged)", file_size(log_path));
    CHECK(file_size(expect) < 0, "a read creates no directory");

    /* ---- write, read back; the directory is made on demand ------------------ */
    CHECK(rig_write(&rig, px, "cs2jit-00000000-v1.pack", payload, (int)sizeof(payload)) == 0, "write ok");
    snprintf(path, sizeof(path), "%s/cs2jit-00000000-v1.pack", expect);
    CHECK(file_size(path) == (int)sizeof(payload), "the record is at %s", path);
    CHECK(!dir_has_tmp(expect), "no temporary left");
    snprintf(path, sizeof(path), "%s/cache.test", g_root);
    CHECK(file_size(path) < 0, "nothing inside (or as) the cache directory");
    CHECK(rig_read(&rig, px, "cs2jit-00000000-v1.pack", &got, &got_size) == 0, "read back");
    CHECK(got && got_size == (int)sizeof(payload) && memcmp(got, payload, sizeof(payload)) == 0, "same bytes");
    free(got);
    /* Replaced whole. */
    CHECK(rig_write(&rig, px, "cs2jit-00000000-v1.pack", payload + 100, 50) == 0, "rewrite ok");
    CHECK(rig_read(&rig, px, "cs2jit-00000000-v1.pack", &got, &got_size) == 0 && got_size == 50 &&
              memcmp(got, payload + 100, 50) == 0,
        "the rewrite replaced it (%d bytes)", got_size);
    free(got);
    /* An empty record is a record. */
    CHECK(rig_write(&rig, px, "empty.bin", payload, 0) == 0, "an empty write");
    CHECK(rig_read(&rig, px, "empty.bin", &got, &got_size) == 0 && got_size == 0, "reads back empty");
    free(got);

    /* A cache directory nested under directories that do not exist yet. */
    snprintf(cache_dir, sizeof(cache_dir), "%s/deep/a/b/cache.deep", g_root);
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, cache_dir);
    CHECK(rig_write(&rig, px, "x.pack", payload, 10) == 0, "missing directories created on demand");
    snprintf(path, sizeof(path), "%s/deep/a/b/cache.deep.derived/x.pack", g_root);
    CHECK(file_size(path) == 10, "at %s", path);

    /* ---- TORIRS_DERIVED_DIR -------------------------------------------------- */
    snprintf(path, sizeof(path), "%s/moved", g_root);
    setenv("TORIRS_DERIVED_DIR", path, 1);
    snprintf(cache_dir, sizeof(cache_dir), "%s/cache.test/", g_root);
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, cache_dir);
    snprintf(expect, sizeof(expect), "%s/moved/cache.test", g_root);
    CHECK(PlatformX_IO_DerivedDir(px) && strcmp(PlatformX_IO_DerivedDir(px), expect) == 0,
        "moved: %s", PlatformX_IO_DerivedDir(px));
    CHECK(rig_read(&rig, px, "cs2jit-00000000-v1.pack", &got, &got_size) == -1, "the old record is not seen there");
    CHECK(rig_write(&rig, px, "cs2jit-00000000-v1.pack", payload, 20) == 0, "write under the override");
    snprintf(path, sizeof(path), "%s/cs2jit-00000000-v1.pack", expect);
    CHECK(file_size(path) == 20, "at %s", path);
    unsetenv("TORIRS_DERIVED_DIR");

    /* ---- bad names: refused by the executor too ----------------------------- */
    {
        char long_name[80];
        memset(long_name, 'a', 64);
        long_name[64] = '\0';
        CHECK(!ToriRS_IO_DerivedNameValid(long_name), "64 bytes is not a name");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_READ, "../escape") == -1, "'..' refused");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_WRITE, "../escape") == -1, "'..' write refused");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_WRITE, "a/b") == -1, "a separator refused");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_WRITE, ".hidden") == -1, "a leading dot refused");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_WRITE, "") == -1, "an empty name refused");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_WRITE, "sp ace") == -1, "a space refused");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_WRITE, NULL) == -1, "an unterminated name refused");
        CHECK(raw_load(px, TORIRS_IOK_DERIVED_READ, NULL) == -1, "an unterminated name refused (read)");
        long_name[63] = '\0';
        CHECK(ToriRS_IO_DerivedNameValid(long_name), "63 bytes is a name");
        snprintf(path, sizeof(path), "%s/escape", g_root);
        CHECK(file_size(path) < 0, "nothing written outside the directory");
    }

    /* ---- no location: "" and "." --------------------------------------------- */
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, "");
    CHECK(PlatformX_IO_DerivedDir(px) == NULL, "an empty dir has no location");
    CHECK(rig_read(&rig, px, "x.pack", &got, &got_size) == -1, "no location: absent");
    CHECK(rig_write(&rig, px, "x.pack", payload, 4) == -1, "no location: write fails");
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, ".");
    CHECK(PlatformX_IO_DerivedDir(px) == NULL, "'.' has no location");
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, NULL);
    CHECK(PlatformX_IO_DerivedDir(px) == NULL, "NULL has no location");

    /* ---- a read-only location: the write fails, nothing crashes ------------- */
    snprintf(path, sizeof(path), "%s/ro", g_root);
    mkdir(path, 0755);
    snprintf(cache_dir, sizeof(cache_dir), "%s/ro/cache.ro", g_root);
    chmod(path, 0555);
    PlatformX_IO_InitCacheId(px, 0, 0, 0, 0, cache_dir);
    if( access(path, W_OK) == 0 )
        printf("note: running with write access to a 0555 directory (root?); read-only case skipped\n");
    else
    {
        CHECK(rig_write(&rig, px, "x.pack", payload, 4) == -1, "a read-only location fails the write");
        CHECK(rig_read(&rig, px, "x.pack", &got, &got_size) == -1, "and reads absent");
    }
    chmod(path, 0755);

    PlatformX_IO_DerivedStats(px, &st);
    printf("stats: reads=%d misses=%d read_bytes=%lld writes=%d failures=%d write_bytes=%lld\n", st.reads,
        st.read_misses, (long long)st.read_bytes, st.writes, st.write_failures, (long long)st.write_bytes);
    CHECK(st.reads == 9 && st.read_misses == 6, "reads counted");
    CHECK(st.writes == 13 && st.write_failures == 8, "writes and failures counted");
    CHECK(st.read_bytes == (int64_t)sizeof(payload) + 50, "read bytes counted (%lld)", (long long)st.read_bytes);

    PlatformX_IO_Free(px);
    ToriRS_IOBatch_Free(rig.io);
    rm_rf(g_root);
    printf("platform_x_derived_io_test: %d ok, %d FAIL\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
