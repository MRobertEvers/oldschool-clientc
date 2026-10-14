/* torirs_mem: groups, arenas, pools, marks, scopes, moves, and the walk.
 *
 *   torirs_mem_test            every case, plus each death case in a child
 *                              process that must ABORT (a failure that
 *                              returns is the bug this allocator exists to
 *                              catch)
 *   torirs_mem_test <case>     run one death case in this process
 *
 * Run it three ways: as built, with TORIRS_ARENAS=off, and built -DNDEBUG.
 * The death cases that only abort in a debug build are skipped under NDEBUG.
 */
#include "mem/torirs_mem.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while( 0 )

static void
check_aligned(
    const void* p,
    uintptr_t align)
{
    CHECK(((uintptr_t)p & (align - 1)) == 0);
}

/* ---- Cases that must pass ------------------------------------------------ */

static void
test_heap(void)
{
    struct ToriMem_Group g;
    ToriMem_GroupInit(&g, "heap", ToriMem_Process());
    char* a = ToriMem_Alloc(&g, 100);
    int* b = ToriMem_Calloc(&g, 10, sizeof(int));
    for( int i = 0; i < 10; i++ )
        CHECK(b[i] == 0);
    char* s = ToriMem_Strndup(&g, "abcdef", 3);
    CHECK(strcmp(s, "abc") == 0);
    CHECK(g.live_blocks == 3);
    a = ToriMem_Realloc(&g, a, 5000);
    memset(a, 1, 5000);
    CHECK(g.live_blocks == 3);
    CHECK(g.live_bytes == 5000 + 40 + 4);
    CHECK(ToriMem_GroupOf(a) == &g);
    ToriMem_Free(&g, a);
    ToriMem_Free(&g, b);
    ToriMem_Free(&g, s);
    ToriMem_Free(&g, NULL);
    CHECK(g.live_blocks == 0);
    CHECK(ToriMem_Realloc(&g, ToriMem_Alloc(&g, 8), 0) == NULL);
    ToriMem_GroupEnd(&g);
}

static void
test_arena(void)
{
    struct ToriMem_Group g;
    ToriMem_ArenaInit(&g, "arena", ToriMem_Process(), 1024);
    void* a = ToriMem_Alloc(&g, 24);
    void* b = ToriMem_Alloc(&g, 40);
    void* c = ToriMem_Alloc(&g, 8);
    check_aligned(a, 16);
    check_aligned(b, 16);
    check_aligned(c, 16);
    CHECK(g.live_blocks == 3);
    ToriMem_Free(&g, b); /* not last: marked freed */
    ToriMem_Free(&g, c); /* last: space given back */
    void* d = ToriMem_Alloc(&g, 8);
    if( ToriMem_ArenasOn() )
        CHECK(d == c); /* the popped space is reused */
    ToriMem_Free(&g, d);
    ToriMem_Free(&g, a);
    CHECK(g.live_blocks == 0);

    /* A request larger than a chunk gets a chunk of its own, and a reset does
     * not keep it. */
    unsigned char* big = ToriMem_Alloc(&g, 5000);
    memset(big, 7, 5000);
    ToriMem_Free(&g, big);
    if( ToriMem_ArenasOn() )
    {
        intptr_t held = g.chunk_bytes;
        CHECK(held >= 5000);
        ToriMem_GroupReset(&g);
        CHECK(g.chunk_bytes < held);
        CHECK(g.chunk_bytes <= 1024);
    }

    /* Many small blocks span chunks. */
    void* many[200];
    for( int i = 0; i < 200; i++ )
        many[i] = ToriMem_Calloc(&g, 1, 33);
    for( int i = 0; i < 200; i++ )
        ToriMem_Free(&g, many[i]);
    CHECK(g.live_blocks == 0);
    ToriMem_GroupReset(&g);
    void* again = ToriMem_Alloc(&g, 16);
    ToriMem_Free(&g, again);
    ToriMem_GroupEnd(&g);
}

static void
test_arena_realloc(void)
{
    struct ToriMem_Group g;
    ToriMem_ArenaInit(&g, "vec", ToriMem_Process(), 4096);
    int* v = NULL;
    int n = 0;
    int cap = 0;
    for( int i = 0; i < 1000; i++ )
    {
        if( n == cap )
        {
            cap = cap ? cap * 2 : 4;
            v = ToriMem_Realloc(&g, v, (size_t)cap * sizeof(int));
        }
        v[n++] = i;
    }
    for( int i = 0; i < 1000; i++ )
        CHECK(v[i] == i);
    CHECK(g.live_blocks == 1);
    /* A block that is not last moves. */
    void* other = ToriMem_Alloc(&g, 8);
    v = ToriMem_Realloc(&g, v, 2000 * sizeof(int));
    for( int i = 0; i < 1000; i++ )
        CHECK(v[i] == i);
    ToriMem_Free(&g, other);
    ToriMem_Free(&g, v);
    ToriMem_GroupEnd(&g);
}

static void
test_marks(void)
{
    struct ToriMem_Group g;
    ToriMem_ArenaInit(&g, "scratch", ToriMem_Process(), 512);
    void* keep = ToriMem_Alloc(&g, 32);
    struct ToriMem_Mark outer = ToriMem_Mark(&g);
    for( int i = 0; i < 50; i++ )
        ToriMem_Alloc(&g, 40); /* released by the rewind, not freed one by one */
    struct ToriMem_Mark inner = ToriMem_Mark(&g);
    void* x = ToriMem_Alloc(&g, 16);
    ToriMem_Free(&g, x);
    void* y = ToriMem_Alloc(&g, 16);
    ToriMem_Free(&g, y);
    ToriMem_CheckedRewind(&g, inner); /* everything since inner was freed */
    ToriMem_Rewind(&g, outer);
    CHECK(g.live_blocks == 1);
    ToriMem_Free(&g, keep);
    ToriMem_GroupReset(&g);
    ToriMem_GroupEnd(&g);
}

static void
test_pool(void)
{
    struct ToriMem_Group pool;
    ToriMem_PoolInit(&pool, "tasks", ToriMem_Process(), 1024);
    struct ToriMem_Group t1;
    ToriMem_ArenaInit(&t1, "task:a", &pool, 0);
    void* a = ToriMem_Alloc(&t1, 100);
    CHECK(pool.live_children == 1);
    ToriMem_Free(&t1, a);
    ToriMem_GroupEnd(&t1);
    CHECK(pool.live_children == 0);
    if( ToriMem_ArenasOn() )
        CHECK(pool.chunk_bytes == 1024); /* the chunk came back to the pool */

    struct ToriMem_Group t2;
    ToriMem_ArenaInit(&t2, "task:b", &pool, 0);
    void* b = ToriMem_Alloc(&t2, 100);
    if( ToriMem_ArenasOn() )
    {
        CHECK(pool.chunk_bytes == 0); /* and was reused */
        CHECK(b == a);
    }
    ToriMem_Free(&t2, b);
    ToriMem_GroupEnd(&t2);
    ToriMem_GroupEnd(&pool);
}

static void
test_scope_and_lib(void)
{
    struct ToriMem_Group lib;
    struct ToriMem_Group task;
    ToriMem_GroupInit(&lib, "lib:test", ToriMem_Process());
    ToriMem_ArenaInit(&task, "task", ToriMem_Process(), 1024);

    void* outside = ToriMem_LibMalloc(16, &lib);
    CHECK(ToriMem_GroupOf(outside) == &lib);

    struct ToriMem_Scope s = ToriMem_ScopeEnter(&task);
    void* inside = ToriMem_LibCalloc(4, 4, &lib);
    void* grown = ToriMem_LibRealloc(ToriMem_LibMalloc(8, &lib), 64, &lib);
    ToriMem_ScopeLeave(s);
    CHECK(ToriMem_GroupOf(inside) == &task);
    CHECK(ToriMem_GroupOf(grown) == &task);

    /* A library frees outside the scope: the header names the group. */
    ToriMem_LibFree(inside, &lib);
    ToriMem_LibFree(grown, &lib);
    ToriMem_LibFree(outside, &lib);
    ToriMem_LibFree(NULL, &lib);
    CHECK(task.live_blocks == 0);
    CHECK(lib.live_blocks == 0);
    ToriMem_GroupEnd(&task);
    ToriMem_GroupEnd(&lib);
}

static void
test_move(void)
{
    struct ToriMem_Group cache;
    struct ToriMem_Group other;
    struct ToriMem_Group task;
    ToriMem_GroupInit(&cache, "cache", ToriMem_Process());
    ToriMem_GroupInit(&other, "other", ToriMem_Process());
    ToriMem_ArenaInit(&task, "task", ToriMem_Process(), 1024);

    /* heap -> heap: same address */
    char* h = ToriMem_Strdup(&other, "hello");
    char* moved = ToriMem_Move(h, &other, &cache);
    CHECK(moved == h);
    CHECK(ToriMem_GroupOf(moved) == &cache);
    CHECK(other.live_blocks == 0);
    CHECK(cache.live_blocks == 1);

    /* arena -> heap: copied */
    char* t = ToriMem_Strdup(&task, "decoded");
    char* kept = ToriMem_Move(t, &task, &cache);
    CHECK(strcmp(kept, "decoded") == 0);
    CHECK(ToriMem_GroupOf(kept) == &cache);
    CHECK(task.live_blocks == 0);

    /* adopt: the source group is read from the block */
    char* stolen = ToriMem_Adopt(ToriMem_Strdup(&other, "stolen"), &cache);
    CHECK(ToriMem_GroupOf(stolen) == &cache);
    ToriMem_Free(&cache, stolen);

    ToriMem_Free(&cache, moved);
    ToriMem_Free(&cache, kept);
    ToriMem_GroupEnd(&task);
    ToriMem_GroupEnd(&other);
    ToriMem_GroupEnd(&cache);
}

static void
test_frame(void)
{
    struct ToriMem_Group* f = ToriMem_Frame();
    for( int frame = 0; frame < 3; frame++ )
    {
        struct ToriMem_Mark m = ToriMem_Mark(f);
        int* route = ToriMem_Alloc(f, 104 * 104 * sizeof(int));
        route[0] = 1;
        ToriMem_Rewind(f, m);
        char* s = ToriMem_Strdup(f, "per-op string");
        ToriMem_Free(f, s);
        ToriMem_FrameEnd();
    }
}

static void
test_nested_groups(void)
{
    struct ToriMem_Group session;
    struct ToriMem_Group scene;
    ToriMem_GroupInit(&session, "session", ToriMem_Process());
    ToriMem_ArenaInit(&scene, "scene", &session, 4096);
    void* tile = ToriMem_Alloc(&scene, 64);
    void* store = ToriMem_Alloc(&session, 64);
    ToriMem_Free(&scene, tile);
    ToriMem_GroupReset(&scene); /* region rebuild */
    ToriMem_GroupEnd(&scene);   /* logout: scene first */
    ToriMem_Free(&session, store);
    ToriMem_GroupEnd(&session);
}

/* ---- Cases that must abort ----------------------------------------------- */

static void
die_heap_leak(void)
{
    struct ToriMem_Group g;
    ToriMem_GroupInit(&g, "leaky", ToriMem_Process());
    ToriMem_Alloc(&g, 10);
    ToriMem_GroupEnd(&g);
}

static void
die_arena_leak(void)
{
    struct ToriMem_Group g;
    ToriMem_ArenaInit(&g, "leaky_arena", ToriMem_Process(), 1024);
    ToriMem_Alloc(&g, 10);
    ToriMem_GroupReset(&g);
}

static void
die_wrong_group(void)
{
    struct ToriMem_Group a;
    struct ToriMem_Group b;
    ToriMem_GroupInit(&a, "a", ToriMem_Process());
    ToriMem_GroupInit(&b, "b", ToriMem_Process());
    ToriMem_Free(&b, ToriMem_Alloc(&a, 10));
}

static void
die_double_free(void)
{
    struct ToriMem_Group g;
    ToriMem_ArenaInit(&g, "df", ToriMem_Process(), 1024);
    void* p = ToriMem_Alloc(&g, 10);
    ToriMem_Alloc(&g, 10); /* keep p from being popped */
    ToriMem_Free(&g, p);
    ToriMem_Free(&g, p);
}

static void
die_foreign_pointer(void)
{
    struct ToriMem_Group g;
    ToriMem_GroupInit(&g, "g", ToriMem_Process());
    unsigned char* raw = calloc(1, 64);
    ToriMem_Free(&g, raw + 32);
}

static void
die_live_child(void)
{
    struct ToriMem_Group parent;
    struct ToriMem_Group child;
    ToriMem_GroupInit(&parent, "parent", ToriMem_Process());
    ToriMem_GroupInit(&child, "child", &parent);
    ToriMem_GroupEnd(&parent);
}

static void
die_checked_rewind(void)
{
    struct ToriMem_Group* f = ToriMem_Frame();
    struct ToriMem_Mark tick = ToriMem_Mark(f);
    ToriMem_Alloc(f, 32); /* the tick forgot to free this */
    ToriMem_CheckedRewind(f, tick);
}

static void
die_move_down(void)
{
    struct ToriMem_Group parent;
    struct ToriMem_Group child;
    ToriMem_GroupInit(&parent, "parent", ToriMem_Process());
    ToriMem_GroupInit(&child, "child", &parent);
    ToriMem_Move(ToriMem_Alloc(&parent, 8), &parent, &child);
}

static void
die_pool_alloc(void)
{
    struct ToriMem_Group pool;
    ToriMem_PoolInit(&pool, "pool", ToriMem_Process(), 1024);
    ToriMem_Alloc(&pool, 8);
}

static void
die_pool_chunk_size(void)
{
    struct ToriMem_Group pool;
    struct ToriMem_Group child;
    ToriMem_PoolInit(&pool, "pool", ToriMem_Process(), 4096);
    ToriMem_ArenaInit(&child, "child", &pool, 1024);
}

static void
die_strict_exit(void)
{
    /* Run with TORIRS_MEM_STRICT=1: an exit finding is fatal. */
    struct ToriMem_Group g;
    ToriMem_GroupInit(&g, "left_at_exit", ToriMem_Process());
    ToriMem_Alloc(&g, 10);
    ToriMem_Shutdown();
}

struct death_case
{
    const char* name;
    void (*fn)(void);
    int every_build; /* aborts under NDEBUG too */
    const char* env;
};

static const struct death_case DEATHS[] = {
    { "heap_leak", die_heap_leak, 1, NULL },
    { "arena_leak", die_arena_leak, 0, NULL },
    { "wrong_group", die_wrong_group, 0, NULL },
    { "double_free", die_double_free, 1, NULL },
    { "foreign_pointer", die_foreign_pointer, 1, NULL },
    { "live_child", die_live_child, 0, NULL },
    { "checked_rewind", die_checked_rewind, 0, NULL },
    { "move_down", die_move_down, 0, NULL },
    { "pool_alloc", die_pool_alloc, 1, NULL },
    { "pool_chunk_size", die_pool_chunk_size, 1, NULL },
    { "strict_exit", die_strict_exit, 1, "TORIRS_MEM_STRICT=1" },
};

static int
run_death(
    const char* self,
    const struct death_case* d)
{
    pid_t pid = fork();
    if( pid == 0 )
    {
        if( d->env )
            putenv((char*)d->env);
        execl(self, self, d->name, (char*)NULL);
        _exit(99);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

int
main(
    int argc,
    char** argv)
{
    ToriMem_Init();
    if( argc > 1 )
    {
        for( size_t i = 0; i < sizeof(DEATHS) / sizeof(DEATHS[0]); i++ )
        {
            if( strcmp(argv[1], DEATHS[i].name) == 0 )
            {
                DEATHS[i].fn();
                fprintf(stderr, "death case %s returned\n", argv[1]);
                return 0;
            }
        }
        fprintf(stderr, "unknown case %s\n", argv[1]);
        return 2;
    }

    test_heap();
    test_arena();
    test_arena_realloc();
    test_marks();
    test_pool();
    test_scope_and_lib();
    test_move();
    test_frame();
    test_nested_groups();

    int failed = 0;
    for( size_t i = 0; i < sizeof(DEATHS) / sizeof(DEATHS[0]); i++ )
    {
        if( !TORIMEM_DEBUG && !DEATHS[i].every_build )
            continue;
        if( !run_death(argv[0], &DEATHS[i]) )
        {
            fprintf(stderr, "FAIL: death case %s did not abort\n", DEATHS[i].name);
            failed = 1;
        }
    }
    if( failed )
        return 1;

    ToriMem_Shutdown();
    printf(
        "torirs_mem_test: OK (%s, arenas %s)\n",
        TORIMEM_DEBUG ? "debug" : "release",
        ToriMem_ArenasOn() ? "on" : "off");
    return 0;
}
