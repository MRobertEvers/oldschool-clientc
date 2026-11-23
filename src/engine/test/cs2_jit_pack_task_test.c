/*
 * test-cs2-jit-pack-tasks: the CS2 jit's platform cache end to end on the
 * desktop executor (docs/CS2_JIT.md section 6), the host-level path a client
 * session takes:
 *
 *   boot      Task_CS2JitPackLoad on a parallel role-less runner, DERIVED_READ
 *   load      every script through CacheProvider_ClientScriptJit, the load
 *             task's one call (a provider with only the jit fields set)
 *   frame     CS2JitCache_MaybeStore -> Task_CS2JitPackStore on a background
 *             role-less runner, DERIVED_WRITE through a real PlatformX_IO
 *
 * over several "boots" on one scratch cache directory:
 *
 *   1  cold: no pack (the derived directory does not exist yet), all built,
 *      the store creates the directory and writes the pack;
 *   2  warm: the second session attaches every script from the pack and
 *      builds nothing (counted), and stores nothing;
 *   3  a script changed (wrong fingerprint): that one is stale, rebuilt, and
 *      the store replaces it; boot 4 attaches all again;
 *   5  corrupt bytes in one blob on disk: that script is a checksum miss and
 *      is replaced; corrupt bytes in the index: the whole pack is absent and
 *      is rewritten; boot 7 attaches all again;
 *   8  a stale version: a pack written under another jit format version is
 *      refused whole, and a blob of another version inside a good pack is a
 *      version miss; both are replaced by the next store.
 *
 * Throughout, neither task is render-blocking and neither runner ever has
 * frame_settle_pending set: the pack never holds a frame or the settle gate.
 *
 *   cs2_jit_pack_task_test <scratch dir>     (emptied and recreated)
 */

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/cs2_jit_cache.h"
#include "engine/task_cs2_jit_pack.h"
#include "engine/test/cs2_jit_test_scripts.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char g_root[1024];
static char g_cache_dir[1100];
static char g_pack_path[1300];

static int const k_ids[] = { 5, 12, 40, 41, 300, 7001, 7002, 9100 };
#define K_IDS ((int)(sizeof(k_ids) / sizeof(k_ids[0])))

struct Session
{
    struct PlatformX_IO* px;
    struct TaskRunner frame;
    struct TaskRunner background;
    struct CS2JitCache* cache;
    struct CacheProvider provider;
    struct ct_set scripts;
};

static void
runner_init(
    struct TaskRunner* runner,
    struct PlatformX_IO* px,
    int parallel)
{
    memset(runner, 0, sizeof(*runner));
    runner->queue = ToriRS_TaskQueue_New();
    runner->io = ToriRS_IOBatch_New();
    runner->px = px;
    runner->parallel = parallel;
}

static void
runner_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IOBatch_Free(runner->io);
}

static void
check_never_settle(
    struct Session* s,
    char const* when)
{
    CT_CHECK(s->frame.frame_settle_pending == 0, "%s: the frame queue's settle flag stays 0", when);
    CT_CHECK(s->background.frame_settle_pending == 0, "%s: the background queue's settle flag stays 0", when);
    CT_CHECK(!TaskRunner_RenderBlockingRemains(&s->frame), "%s: nothing render-blocking (frame)", when);
    CT_CHECK(!TaskRunner_RenderBlockingRemains(&s->background), "%s: nothing render-blocking (background)", when);
    for( struct ToriRS_Task* t = s->frame.queue->head; t; t = t->next )
        CT_CHECK(!t->render_blocking, "%s: %s is an ordinary task", when, t->name);
    for( struct ToriRS_Task* t = s->background.queue->head; t; t = t->next )
        CT_CHECK(!t->render_blocking, "%s: %s is an ordinary task", when, t->name);
}

static void
drain(
    struct Session* s,
    struct TaskRunner* runner)
{
    int guard = 0;

    while( runner->queue->head && guard++ < 64 )
    {
        check_never_settle(s, "while a pack task is queued");
        TaskRunner_Step(runner);
    }
    CT_CHECK(runner->queue->head == NULL, "the queue drained");
}

/*
 * One client session: boot (pack read), load every script, store if one is
 * due, end. `variant_id` gets content `variant` (other ids get 0).
 */
static struct CS2JitCacheStats
session(
    char const* name,
    int variant_id,
    int variant,
    int* out_stored)
{
    struct Session s;
    struct CS2JitCacheStats st;
    bool spawned;

    memset(&s, 0, sizeof(s));
    s.px = PlatformX_IO_New();
    PlatformX_IO_InitCacheId(s.px, 0, 0, 0, 0, g_cache_dir);
    runner_init(&s.frame, s.px, 1);
    runner_init(&s.background, s.px, 0);
    s.cache = CS2JitCache_New(NULL);
    /* The provider's jit fields as CacheProvider_SetCS2Jit leaves them. */
    s.provider.cs2jit_enabled = 1;
    s.provider.cs2jit = s.cache;

    ToriRS_TaskQueue_Add(s.frame.queue, CreateTask_CS2JitPackLoad(s.cache));
    check_never_settle(&s, name);
    drain(&s, &s.frame);
    CT_CHECK(CS2JitCache_Resolved(s.cache), "%s: the read resolved", name);

    for( int i = 0; i < K_IDS; i++ )
    {
        struct CS2VM2_Script* script = ct_add(&s.scripts, k_ids[i], k_ids[i] == variant_id ? variant : 0);
        CacheProvider_ClientScriptJit(&s.provider, script);
        CT_CHECK(script->jit != NULL, "%s: script %d runs jitted", name, k_ids[i]);
    }

    /* Frames: the first sees what is new, STORE_MS later it is stored. */
    spawned = CS2JitCache_MaybeStore(s.cache, 1000, s.background.queue, ct_lookup, &s.scripts);
    CT_CHECK(!spawned, "%s: not before STORE_MS", name);
    spawned = CS2JitCache_MaybeStore(s.cache, 1000 + CS2JIT_STORE_MS_DEFAULT, s.background.queue, ct_lookup,
        &s.scripts);
    check_never_settle(&s, name);
    drain(&s, &s.background);
    CT_CHECK(!CS2JitCache_StoreInFlight(s.cache), "%s: the store ended", name);

    CS2JitCache_Stats(s.cache, &st);
    *out_stored = spawned && st.stores == 1;
    printf("%s: ", name);
    fflush(stdout);
    CS2JitCache_Report(s.cache, name);

    runner_free(&s.frame);
    runner_free(&s.background);
    CS2JitCache_Free(s.cache);
    ct_set_free(&s.scripts);
    PlatformX_IO_Free(s.px);
    return st;
}

static uint8_t*
read_pack(size_t* size)
{
    FILE* f = fopen(g_pack_path, "rb");
    uint8_t* bytes;
    long n;

    *size = 0;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bytes = ToriMem_Alloc(ToriMem_Script(), (size_t)n);
    if( fread(bytes, 1, (size_t)n, f) != (size_t)n )
        n = 0;
    fclose(f);
    *size = (size_t)n;
    return bytes;
}

static void
write_pack(
    uint8_t const* bytes,
    size_t size)
{
    FILE* f = fopen(g_pack_path, "wb");
    assert(f);
    assert(fwrite(bytes, 1, size, f) == size);
    fclose(f);
}

/* The pack a session finds on disk, edited by `edit`. */
static void
corrupt(void (*edit)(uint8_t* pack, size_t size))
{
    size_t size;
    uint8_t* pack = read_pack(&size);
    assert(pack);
    edit(pack, size);
    write_pack(pack, size);
    ToriMem_Free(ToriMem_Script(), pack);
}

/* Entry 2's blob (id 40): one code byte flipped. Its blob checksum fails. */
static void
edit_blob_byte(
    uint8_t* pack,
    size_t size)
{
    uint32_t const off = ct_rd32(pack + 40 + (2 * 24) + 16);
    assert(off + CS2VM2_JIT_BLOB_HEADER_SIZE < size);
    pack[off + CS2VM2_JIT_BLOB_HEADER_SIZE] ^= 0x40;
}

/* An index fingerprint byte: the index checksum fails, the pack is absent. */
static void
edit_index_byte(
    uint8_t* pack,
    size_t size)
{
    (void)size;
    pack[40 + 8] ^= 0x01;
}

/* The whole pack claims another jit format version. */
static void
edit_pack_version(
    uint8_t* pack,
    size_t size)
{
    (void)size;
    ct_wr32(pack + 8, CS2VM2_JIT_FORMAT_VERSION + 1);
}

/* Entry 4's blob (id 300) claims another jit format version. */
static void
edit_blob_version(
    uint8_t* pack,
    size_t size)
{
    uint32_t const off = ct_rd32(pack + 40 + (4 * 24) + 16);
    (void)size;
    pack[off + 4] = (uint8_t)(CS2VM2_JIT_FORMAT_VERSION + 1);
}

static void
expect_warm(
    struct CS2JitCacheStats st,
    int stored,
    char const* name)
{
    CT_CHECK(st.read_state == CS2JIT_PACK_LANDED, "%s: the pack landed (%s)", name,
        CS2JitCache_PackStateName(st.read_state));
    CT_CHECK(st.attached == K_IDS && st.built == 0, "%s: every script from the cache, none built (attached %d built %d)",
        name, st.attached, st.built);
    CT_CHECK(!stored && st.stores == 0, "%s: nothing to store", name);
}

int
main(
    int argc,
    char** argv)
{
    struct CS2JitCacheStats st;
    struct stat sb;
    char derived_dir[1200];
    char cmd[2400];
    int stored;

    if( argc < 2 )
    {
        printf("usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    unsetenv("TORIRS_DERIVED_DIR");
    snprintf(g_root, sizeof(g_root), "%s", argv[1]);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    (void)system(cmd);
    mkdir(g_root, 0755);
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/cache.jittest", g_root);
    mkdir(g_cache_dir, 0755);
    snprintf(derived_dir, sizeof(derived_dir), "%s.derived", g_cache_dir);
    {
        struct CS2JitCache* probe = CS2JitCache_New(NULL);
        snprintf(g_pack_path, sizeof(g_pack_path), "%s/%s", derived_dir, CS2JitCache_PackName(probe));
        CS2JitCache_Free(probe);
    }

    /* 1: cold. The derived directory does not exist; the store makes it. */
    CT_CHECK(stat(derived_dir, &sb) != 0, "no derived directory before the first store");
    st = session("boot1 cold", -1, 0, &stored);
    CT_CHECK(st.read_state == CS2JIT_PACK_ABSENT, "boot1: no pack (%s)", CS2JitCache_PackStateName(st.read_state));
    CT_CHECK(st.built == K_IDS && st.attached == 0, "boot1: all built");
    CT_CHECK(stored && st.store_entries_new == K_IDS, "boot1: stored all");
    CT_CHECK(stat(g_pack_path, &sb) == 0 && sb.st_size == st.store_bytes, "boot1: the pack is on disk (%s)",
        g_pack_path);

    /* 2: warm. */
    st = session("boot2 warm", -1, 0, &stored);
    expect_warm(st, stored, "boot2");
    CT_CHECK(st.fingerprinted == K_IDS, "boot2: one fingerprint per script");

    /* 3: script 41 changed: wrong fingerprint. */
    st = session("boot3 changed", 41, 3, &stored);
    CT_CHECK(st.rejected[CS2VM2_JIT_REJECT_STALE] == 1 && st.built == 1 && st.attached == K_IDS - 1,
        "boot3: one stale, rebuilt");
    CT_CHECK(stored && st.store_entries_new == 1 && st.store_entries_carried == K_IDS - 1, "boot3: replaced");
    st = session("boot4 warm", 41, 3, &stored);
    expect_warm(st, stored, "boot4");

    /* 5: corrupt bytes in one blob. */
    corrupt(edit_blob_byte);
    st = session("boot5 blob", 41, 3, &stored);
    CT_CHECK(st.rejected[CS2VM2_JIT_REJECT_CHECKSUM] == 1 && st.built == 1, "boot5: the corrupt blob is a miss");
    CT_CHECK(stored && st.store_entries_new == 1, "boot5: and is replaced");
    /* 6: corrupt bytes in the index. */
    corrupt(edit_index_byte);
    st = session("boot6 index", 41, 3, &stored);
    CT_CHECK(st.read_state == CS2JIT_PACK_BAD && st.read_reject == CS2JIT_PACK_REJECT_CHECKSUM,
        "boot6: the pack is refused whole");
    CT_CHECK(st.built == K_IDS && stored && st.store_entries_new == K_IDS, "boot6: all built and rewritten");
    st = session("boot7 warm", 41, 3, &stored);
    expect_warm(st, stored, "boot7");

    /* 8: stale versions. */
    corrupt(edit_pack_version);
    st = session("boot8 pack version", 41, 3, &stored);
    CT_CHECK(st.read_state == CS2JIT_PACK_BAD && st.read_reject == CS2JIT_PACK_REJECT_FORMAT,
        "boot8: another format version is refused");
    CT_CHECK(st.built == K_IDS && stored, "boot8: rebuilt and rewritten");
    corrupt(edit_blob_version);
    st = session("boot9 blob version", 41, 3, &stored);
    CT_CHECK(st.rejected[CS2VM2_JIT_REJECT_VERSION] == 1 && st.built == 1 && stored, "boot9: a blob of another version");
    st = session("boot10 warm", 41, 3, &stored);
    expect_warm(st, stored, "boot10");

    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    (void)system(cmd);
    return ct_finish("cs2_jit_pack_task_test");
}
