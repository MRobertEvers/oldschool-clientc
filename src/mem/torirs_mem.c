#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if (defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__)
#include <execinfo.h>
#define TORIMEM_HAVE_BACKTRACE 1
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <dlfcn.h>
#define TORIMEM_HAVE_DLADDR 1
#endif

/* A block a library hook allocated has no __FILE__: its site is the code
 * address that called the hook (the library's own call, since the
 * RSCACHE_MALLOC-style macros call the hook directly), stored in `file` with
 * this line. The report symbolizes it. */
#define LIB_SITE_LINE 0xFFFFFFFFu
#define LIB_SITE() ((const char*)__builtin_return_address(0))

/*
 * The allocator's own memory (chunks, the dump's scratch) comes straight from
 * malloc: it is what the groups are built on, so it cannot belong to one.
 *
 * Checks that must hold in every build -- OPT=1 compiles out assert() -- go
 * through die() (always aborts) or violation() (aborts in a debug build or
 * under TORIRS_MEM_STRICT=1, otherwise prints a torimem: line).
 */

/* ---- Block layout -------------------------------------------------------
 *
 *   malloc'd, no prefix   [ hdr 16 ][ data ............ ]
 *   malloc'd, prefix      [ prefix 32 ][ hdr 16 ][ data ]   on its group's list
 *   in an arena chunk     [ prefix 32, debug only ][ hdr 16 ][ data, rounded to 16 ]
 *
 * Both structs are padded to a fixed size on 32-bit targets, so the data
 * keeps malloc's alignment there and a chunk's 16-byte alignment everywhere.
 */
#define F_PREFIX 0x1u
#define F_CHUNK 0x2u
#define F_FREED 0x4u

#define HDR_MAGIC 0x7A11u
#define HDR_DEAD 0xDEADu

struct ToriMem_Hdr
{
    struct ToriMem_Group* group;
    uint32_t size;
    uint16_t flags;
    uint16_t magic;
#if UINTPTR_MAX == 0xFFFFFFFFu
    uint32_t pad;
#endif
};

struct ToriMem_Prefix
{
    struct ToriMem_Prefix* prev;
    struct ToriMem_Prefix* next;
    const char* file;
    uint32_t line;
    uint32_t seq;
#if UINTPTR_MAX == 0xFFFFFFFFu
    uint32_t pad[3];
#endif
};

_Static_assert(sizeof(struct ToriMem_Hdr) == 16, "header must be 16 bytes on every target");
_Static_assert(sizeof(struct ToriMem_Prefix) == 32, "prefix must be 32 bytes on every target");

#define HDR_BYTES ((size_t)sizeof(struct ToriMem_Hdr))
#define PREFIX_BYTES ((size_t)sizeof(struct ToriMem_Prefix))

/* An arena block carries a prefix only when sites are on (a debug build, or
 * TORIRS_MEM_SITES=1): the site the walk names. With arenas off every block
 * is malloc'd and always has one. Fixed before the first allocation. */
#define CHUNK_PREFIX_BYTES (s_sites ? PREFIX_BYTES : (size_t)0)

struct ToriMem_Chunk
{
    struct ToriMem_Chunk* next;
    size_t cap;
    size_t used;
    unsigned char* data;
    int from_pool;
    int oversize; /* made for one request larger than a chunk */
};

#define FRAME_CHUNK_BYTES ((size_t)256 * 1024)
#define REPORT_SITES 16

/* ---- Process state ------------------------------------------------------ */

static int s_started;
static int s_any_alloc;
static int s_arenas_on = TORIMEM_ARENAS;
static int s_strict;
static int s_stats;
static int s_sites = TORIMEM_DEBUG;
static int s_at_exit;

static struct ToriMem_Group s_process;
static struct ToriMem_Group s_tasks;
static struct ToriMem_Group s_vms;

#define TASKS_CHUNK_BYTES ((size_t)4 * 1024)
#define VMS_CHUNK_BYTES ((size_t)16 * 1024)

static int s_registry_lock;
static struct ToriMem_Group* s_registry;

static _Thread_local char s_thread_anchor;
static _Thread_local struct ToriMem_Group* s_scope;
static _Thread_local struct ToriMem_Group s_frame;

#define THREAD_ID ((void const*)&s_thread_anchor)

/* ---- Small helpers ------------------------------------------------------ */

static void
spin_lock(int* lock)
{
    while( __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) )
    {
    }
}

static void
spin_unlock(int* lock)
{
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

static size_t
round16(size_t n)
{
    return (n + 15u) & ~(size_t)15u;
}

/* Name the call that broke the rule: a finding without the caller is a hunt. */
static void
print_stack(void)
{
#ifdef TORIMEM_HAVE_BACKTRACE
    void* frames[24];
    int n = backtrace(frames, 24);
    fprintf(stderr, "torimem: stack:\n");
    fflush(stderr);
    backtrace_symbols_fd(frames, n, 2);
#endif
}

static void
die(
    const char* what,
    const struct ToriMem_Group* group)
{
    fprintf(stderr, "torimem: FATAL: %s (group %s)\n", what, group && group->name ? group->name : "-");
    print_stack();
    fflush(stderr);
    abort();
}

static void
violation_end(void)
{
    /* At exit nothing can act on a finding any more except the reader: the
     * report is the work list, and only TORIRS_MEM_STRICT makes it fatal. */
    if( s_strict || (TORIMEM_DEBUG && !s_at_exit) )
    {
        print_stack();
        fflush(stderr);
        abort();
    }
}

static void
counters_alloc(
    struct ToriMem_Group* g,
    size_t size)
{
    __atomic_add_fetch(&g->live_blocks, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g->total_allocs, 1, __ATOMIC_RELAXED);
    intptr_t live = __atomic_add_fetch(&g->live_bytes, (intptr_t)size, __ATOMIC_RELAXED);
    intptr_t peak = __atomic_load_n(&g->peak_bytes, __ATOMIC_RELAXED);
    while( live > peak &&
           !__atomic_compare_exchange_n(
               &g->peak_bytes, &peak, live, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED) )
    {
    }
}

static void
counters_free(
    struct ToriMem_Group* g,
    size_t size)
{
    __atomic_sub_fetch(&g->live_blocks, 1, __ATOMIC_RELAXED);
    __atomic_sub_fetch(&g->live_bytes, (intptr_t)size, __ATOMIC_RELAXED);
}

static void
counters_resize(
    struct ToriMem_Group* g,
    size_t old_size,
    size_t new_size)
{
    intptr_t live = __atomic_add_fetch(
        &g->live_bytes, (intptr_t)new_size - (intptr_t)old_size, __ATOMIC_RELAXED);
    intptr_t peak = __atomic_load_n(&g->peak_bytes, __ATOMIC_RELAXED);
    while( live > peak &&
           !__atomic_compare_exchange_n(
               &g->peak_bytes, &peak, live, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED) )
    {
    }
}

static struct ToriMem_Hdr*
hdr_of(const void* ptr)
{
    struct ToriMem_Hdr* h = (struct ToriMem_Hdr*)((unsigned char*)ptr - HDR_BYTES);
    if( h->magic == HDR_DEAD )
        die("double free, or a block used after it was freed", NULL);
    if( h->magic != HDR_MAGIC )
        die("pointer did not come from a ToriMem group (libc, SDL, stb, Lua, or a library with no hooks installed?)",
            NULL);
    return h;
}

static struct ToriMem_Prefix*
prefix_of(struct ToriMem_Hdr* h)
{
    return (struct ToriMem_Prefix*)((unsigned char*)h - PREFIX_BYTES);
}

static void
check_live(const struct ToriMem_Group* g)
{
    if( !g )
        die("NULL group", NULL);
    if( !g->live )
        die("group used before Init or after End", g);
}

static void
check_owner(const struct ToriMem_Group* g)
{
#if TORIMEM_DEBUG
    if( g->kind == TORIMEM_ARENA && g->owner_thread != THREAD_ID )
        die("an arena is single-threaded; this thread does not own it", g);
#else
    (void)g;
#endif
}

static int
group_uses_list(const struct ToriMem_Group* g)
{
    if( g->kind == TORIMEM_HEAP )
        return s_sites;
    return g->kind == TORIMEM_ARENA && !s_arenas_on;
}

static int
group_uses_chunks(const struct ToriMem_Group* g)
{
    return g->kind == TORIMEM_ARENA && s_arenas_on;
}

static void
group_path(
    const struct ToriMem_Group* g,
    char* out,
    size_t out_size)
{
    const struct ToriMem_Group* chain[16];
    int n = 0;
    for( const struct ToriMem_Group* p = g; p && n < 16; p = p->parent )
        chain[n++] = p;
    size_t at = 0;
    out[0] = '\0';
    for( int i = n - 1; i >= 0; i-- )
    {
        int w = snprintf(
            out + at, out_size - at, "%s%s", i == n - 1 ? "" : "/", chain[i]->name ? chain[i]->name : "?");
        if( w < 0 || (size_t)w >= out_size - at )
            break;
        at += (size_t)w;
    }
}

/* ---- Process start ------------------------------------------------------ */

static int
env_on(const char* name)
{
    const char* v = getenv(name);
    return v && v[0] && v[0] != '0';
}

static void
registry_add(struct ToriMem_Group* g)
{
    spin_lock(&s_registry_lock);
    g->reg_prev = NULL;
    g->reg_next = s_registry;
    if( s_registry )
        s_registry->reg_prev = g;
    s_registry = g;
    spin_unlock(&s_registry_lock);
}

static void
registry_remove(struct ToriMem_Group* g)
{
    spin_lock(&s_registry_lock);
    if( g->reg_prev )
        g->reg_prev->reg_next = g->reg_next;
    else
        s_registry = g->reg_next;
    if( g->reg_next )
        g->reg_next->reg_prev = g->reg_prev;
    g->reg_prev = NULL;
    g->reg_next = NULL;
    spin_unlock(&s_registry_lock);
}

static void
start_process(void)
{
    if( s_started )
        return;
    const char* arenas = getenv("TORIRS_ARENAS");
    s_arenas_on = TORIMEM_ARENAS;
    if( arenas && (strcmp(arenas, "off") == 0 || strcmp(arenas, "0") == 0) )
        s_arenas_on = 0;
    s_strict = env_on("TORIRS_MEM_STRICT");
    s_stats = env_on("TORIRS_MEM_STATS");
    s_sites = TORIMEM_DEBUG || env_on("TORIRS_MEM_SITES");

    memset(&s_process, 0, sizeof(s_process));
    s_process.name = "process";
    s_process.kind = TORIMEM_HEAP;
    s_process.live = 1;
    s_process.owner_thread = THREAD_ID;
    registry_add(&s_process);
    s_started = 1;
}

void
ToriMem_Init(void)
{
    /* The arena mode decides every block's layout; it cannot change under
     * blocks that already exist. */
    if( s_any_alloc )
        die("ToriMem_Init after the first allocation", NULL);
    if( s_started && (s_registry != &s_process || s_process.reg_next) )
        die("ToriMem_Init after a group was made: call it first thing in main()", NULL);
    if( s_started )
    {
        registry_remove(&s_process);
        s_started = 0;
    }
    start_process();
}

struct ToriMem_Group*
ToriMem_Process(void)
{
    start_process();
    return &s_process;
}

struct ToriMem_Group*
ToriMem_Tasks(void)
{
    if( !s_tasks.live )
        ToriMem_PoolInit(&s_tasks, "tasks", ToriMem_Process(), TASKS_CHUNK_BYTES);
    return &s_tasks;
}

struct ToriMem_Group*
ToriMem_Vms(void)
{
    if( !s_vms.live )
        ToriMem_PoolInit(&s_vms, "vms", ToriMem_Process(), VMS_CHUNK_BYTES);
    return &s_vms;
}

/* A subsystem's heap group under PROCESS, made on first use from any thread
 * (the audio callback and the IO and render workers reach theirs first
 * sometimes), ended by ToriMem_Shutdown's exit walk. */
static int s_lazy_lock;

static struct ToriMem_Group*
lazy_heap(
    struct ToriMem_Group* g,
    const char* name)
{
    if( __atomic_load_n(&g->live, __ATOMIC_ACQUIRE) )
        return g;
    struct ToriMem_Group* process = ToriMem_Process();
    spin_lock(&s_lazy_lock);
    if( !g->live )
        ToriMem_GroupInit(g, name, process);
    spin_unlock(&s_lazy_lock);
    return g;
}

#define TORIMEM_SUBSYSTEM(fn, name)                                                                \
    struct ToriMem_Group* fn(void)                                                                 \
    {                                                                                              \
        static struct ToriMem_Group g;                                                             \
        return lazy_heap(&g, name);                                                                \
    }

TORIMEM_SUBSYSTEM(ToriMem_Assets, "assets")
TORIMEM_SUBSYSTEM(ToriMem_Ui, "ui")
TORIMEM_SUBSYSTEM(ToriMem_Render, "render")
TORIMEM_SUBSYSTEM(ToriMem_Audio, "audio")
TORIMEM_SUBSYSTEM(ToriMem_Net, "net")
TORIMEM_SUBSYSTEM(ToriMem_Session, "session")
TORIMEM_SUBSYSTEM(ToriMem_Scene, "scene")
TORIMEM_SUBSYSTEM(ToriMem_Plugins, "plugins")
TORIMEM_SUBSYSTEM(ToriMem_Script, "script")
TORIMEM_SUBSYSTEM(ToriMem_Editor, "editor")
TORIMEM_SUBSYSTEM(ToriMem_ServerContent, "server:content")
TORIMEM_SUBSYSTEM(ToriMem_ServerWorld, "server:world")
TORIMEM_SUBSYSTEM(ToriMem_ServerPlayers, "server:players")
TORIMEM_SUBSYSTEM(ToriMem_ServerScripts, "server:scripts")
TORIMEM_SUBSYSTEM(ToriMem_ServerNet, "server:net")
TORIMEM_SUBSYSTEM(ToriMem_Compiler, "compiler")

/* The running renderer, under render: one at a time, begun and ended by the
 * host's renderer start and stop. A renderer's worker thread (the GLES2 dual
 * core lane) allocates here too, between the same two calls. */
static struct ToriMem_Group s_renderer;

struct ToriMem_Group*
ToriMem_Renderer(void)
{
    if( !__atomic_load_n(&s_renderer.live, __ATOMIC_ACQUIRE) )
        die("renderer memory asked for with no renderer running", ToriMem_Render());
    return &s_renderer;
}

void
ToriMem_RendererBegin(void)
{
    if( s_renderer.live )
        die("a renderer began while another was running", &s_renderer);
    ToriMem_GroupInit(&s_renderer, "renderer", ToriMem_Render());
}

void
ToriMem_RendererEnd(void)
{
    if( !s_renderer.live )
        die("a renderer ended that never began", ToriMem_Render());
    ToriMem_GroupEnd(&s_renderer);
}

int
ToriMem_ArenasOn(void)
{
    start_process();
    return s_arenas_on;
}

/* ---- Groups ------------------------------------------------------------- */

static void
group_init(
    struct ToriMem_Group* g,
    const char* name,
    struct ToriMem_Group* parent,
    int kind,
    size_t chunk_size)
{
    start_process();
    if( !g )
        die("NULL group", NULL);
    if( !name )
        die("group needs a name", NULL);
    if( !parent )
        die("group needs a parent (ToriMem_Process() at the root)", NULL);
    check_live(parent);
    if( parent->kind == TORIMEM_POOL && kind != TORIMEM_ARENA )
        die("a pool's children are arenas", parent);

    memset(g, 0, sizeof(*g));
    g->name = name;
    g->parent = parent;
    g->kind = kind;
    g->live = 1;
    g->chunk_size = chunk_size;
    g->owner_thread = THREAD_ID;
    __atomic_add_fetch(&parent->live_children, 1, __ATOMIC_RELAXED);
    registry_add(g);
}

void
ToriMem_GroupInit(
    struct ToriMem_Group* group,
    const char* name,
    struct ToriMem_Group* parent)
{
    group_init(group, name, parent, TORIMEM_HEAP, 0);
}

void
ToriMem_ArenaInit(
    struct ToriMem_Group* group,
    const char* name,
    struct ToriMem_Group* parent,
    size_t chunk_bytes)
{
    if( parent && parent->kind == TORIMEM_POOL )
    {
        /* Pooled chunks are handed between children, so every child of a pool
         * uses the pool's size: a smaller one would be handed a request it
         * cannot hold. */
        if( chunk_bytes != 0 && chunk_bytes != parent->chunk_size )
            die("an arena under a pool uses the pool's chunk size (pass 0)", parent);
        chunk_bytes = parent->chunk_size;
    }
    else if( chunk_bytes == 0 )
    {
        die("an arena outside a pool needs a chunk size", parent);
    }
    if( chunk_bytes < 256 )
        die("arena chunk size below 256 bytes", parent);
    group_init(group, name, parent, TORIMEM_ARENA, chunk_bytes);
}

void
ToriMem_PoolInit(
    struct ToriMem_Group* group,
    const char* name,
    struct ToriMem_Group* parent,
    size_t chunk_bytes)
{
    if( chunk_bytes < 256 )
        die("pool chunk size below 256 bytes", parent);
    group_init(group, name, parent, TORIMEM_POOL, chunk_bytes);
}

/* ---- Chunks ------------------------------------------------------------- */

static struct ToriMem_Chunk*
chunk_new(size_t cap)
{
    /* +15: a 32-bit malloc only promises 8, and the data must start at 16. */
    unsigned char* raw = malloc(sizeof(struct ToriMem_Chunk) + 15 + cap);
    if( !raw )
        die("out of memory allocating an arena chunk", NULL);
    struct ToriMem_Chunk* c = (struct ToriMem_Chunk*)raw;
    uintptr_t data = (uintptr_t)(raw + sizeof(*c));
    data = (data + 15u) & ~(uintptr_t)15u;
    c->next = NULL;
    c->cap = cap;
    c->used = 0;
    c->data = (unsigned char*)data;
    c->from_pool = 0;
    c->oversize = 0;
    return c;
}

static struct ToriMem_Chunk*
arena_take_chunk(
    struct ToriMem_Group* g,
    size_t need)
{
    struct ToriMem_Group* pool =
        (g->parent && g->parent->kind == TORIMEM_POOL) ? g->parent : NULL;
    struct ToriMem_Chunk* c = NULL;
    if( need <= g->chunk_size )
    {
        if( pool )
        {
            spin_lock(&pool->lock);
            c = pool->pool_free;
            if( c )
            {
                pool->pool_free = c->next;
                pool->chunk_bytes -= (intptr_t)c->cap;
            }
            spin_unlock(&pool->lock);
        }
        if( !c )
            c = chunk_new(g->chunk_size);
        c->from_pool = pool != NULL;
    }
    else
    {
        /* Larger than a chunk: a chunk of its own, never pooled, and not kept
         * past the next reset. */
        c = chunk_new(need);
        c->oversize = 1;
    }
    c->next = NULL;
    c->used = 0;
    g->chunk_bytes += (intptr_t)c->cap;
    return c;
}

static void
arena_release_chunks(struct ToriMem_Group* g)
{
    struct ToriMem_Group* pool =
        (g->parent && g->parent->kind == TORIMEM_POOL) ? g->parent : NULL;
    struct ToriMem_Chunk* c = g->chunks;
    while( c )
    {
        struct ToriMem_Chunk* next = c->next;
        if( c->from_pool && pool )
        {
            c->used = 0;
            spin_lock(&pool->lock);
            c->next = pool->pool_free;
            pool->pool_free = c;
            pool->chunk_bytes += (intptr_t)c->cap;
            spin_unlock(&pool->lock);
        }
        else
        {
            free(c);
        }
        c = next;
    }
    g->chunks = NULL;
    g->cur = NULL;
    g->chunk_bytes = 0;
}

/* Visit every block in chunk order, starting at (chunk, off); NULL chunk is
 * the very start. Chunks after `cur` are always empty. */
typedef void (*block_visit_fn)(struct ToriMem_Hdr* h, void* ctx);

static void
arena_walk(
    struct ToriMem_Group* g,
    struct ToriMem_Chunk* start_chunk,
    size_t start_off,
    block_visit_fn visit,
    void* ctx)
{
    size_t p = CHUNK_PREFIX_BYTES;
    for( struct ToriMem_Chunk* c = start_chunk ? start_chunk : g->chunks; c; c = c->next )
    {
        size_t pos = (c == start_chunk) ? start_off : 0;
        while( pos < c->used )
        {
            struct ToriMem_Hdr* h = (struct ToriMem_Hdr*)(c->data + pos + p);
            if( h->magic != HDR_MAGIC )
                die("arena chunk corrupted: a block header was overwritten", g);
            size_t step = p + HDR_BYTES + round16(h->size ? h->size : 1);
            visit(h, ctx);
            pos += step;
        }
    }
}

/* ---- Live-block reports ------------------------------------------------- */

/* "file:line", or for a library hook's block the function that called the
 * hook ("RSCache_BandRecordAppendTuple+0x58 (librscache)"). */
static void
site_name(
    const char* file,
    uint32_t line,
    char* out,
    size_t cap)
{
    if( !file )
    {
        snprintf(out, cap, "%s", s_sites ? "(no site)" : "(run with TORIRS_MEM_SITES=1)");
        return;
    }
    if( line != LIB_SITE_LINE )
    {
        snprintf(out, cap, "%s:%u", file, line);
        return;
    }
#ifdef TORIMEM_HAVE_DLADDR
    Dl_info info;
    if( dladdr(file, &info) && info.dli_sname )
    {
        const char* obj = info.dli_fname ? strrchr(info.dli_fname, '/') : NULL;
        snprintf(
            out,
            cap,
            "%s+0x%lx (library hook, %s)",
            info.dli_sname,
            (unsigned long)((uintptr_t)file - (uintptr_t)info.dli_saddr),
            obj ? obj + 1 : "?");
        return;
    }
#endif
    snprintf(out, cap, "%p (library hook)", (const void*)file);
}

struct site_tally
{
    const char* file;
    uint32_t line;
    intptr_t blocks;
    intptr_t bytes;
};

struct report_ctx
{
    struct site_tally sites[64];
    int n_sites;
    intptr_t blocks;
    intptr_t bytes;
};

static void
tally(
    struct report_ctx* r,
    const char* file,
    uint32_t line,
    size_t size)
{
    r->blocks++;
    r->bytes += (intptr_t)size;
    for( int i = 0; i < r->n_sites; i++ )
    {
        if( r->sites[i].file == file && r->sites[i].line == line )
        {
            r->sites[i].blocks++;
            r->sites[i].bytes += (intptr_t)size;
            return;
        }
    }
    if( r->n_sites < 64 )
    {
        struct site_tally* s = &r->sites[r->n_sites++];
        s->file = file;
        s->line = line;
        s->blocks = 1;
        s->bytes = (intptr_t)size;
    }
}

static void
tally_chunk_block(
    struct ToriMem_Hdr* h,
    void* ctx)
{
    if( h->flags & F_FREED )
        return;
    const char* file = NULL;
    uint32_t line = 0;
    if( h->flags & F_PREFIX )
    {
        struct ToriMem_Prefix* pre = prefix_of(h);
        file = pre->file;
        line = pre->line;
    }
    tally((struct report_ctx*)ctx, file, line, h->size);
}

static int
site_cmp(
    const void* a,
    const void* b)
{
    const struct site_tally* x = a;
    const struct site_tally* y = b;
    return (y->bytes > x->bytes) - (y->bytes < x->bytes);
}

/* Print the live blocks of `g` (all of them, or those after a mark). Returns
 * how many there were. */
static intptr_t
report_live(
    struct ToriMem_Group* g,
    const char* event,
    struct ToriMem_Chunk* from_chunk,
    size_t from_off,
    uint32_t from_seq,
    int whole_group)
{
    struct report_ctx r;
    memset(&r, 0, sizeof(r));

    if( group_uses_chunks(g) )
    {
        if( whole_group )
            arena_walk(g, NULL, 0, tally_chunk_block, &r);
        else
            arena_walk(g, from_chunk, from_off, tally_chunk_block, &r);
    }
    else if( group_uses_list(g) )
    {
        spin_lock(&g->lock);
        for( struct ToriMem_Prefix* pre = g->list; pre; pre = pre->next )
        {
            if( !whole_group && pre->seq < from_seq )
                break;
            struct ToriMem_Hdr* h = (struct ToriMem_Hdr*)((unsigned char*)pre + PREFIX_BYTES);
            tally(&r, pre->file, pre->line, h->size);
        }
        spin_unlock(&g->lock);
    }
    else if( whole_group )
    {
        /* A release HEAP group keeps no list: the counter is the answer. */
        r.blocks = __atomic_load_n(&g->live_blocks, __ATOMIC_RELAXED);
        r.bytes = __atomic_load_n(&g->live_bytes, __ATOMIC_RELAXED);
    }

    if( r.blocks == 0 )
        return 0;

    char path[256];
    group_path(g, path, sizeof(path));
    fprintf(
        stderr,
        "torimem: group %s %s with %ld live block(s), %ld bytes -- each should have been freed\n",
        path,
        event,
        (long)r.blocks,
        (long)r.bytes);
    qsort(r.sites, (size_t)r.n_sites, sizeof(r.sites[0]), site_cmp);
    for( int i = 0; i < r.n_sites && i < REPORT_SITES; i++ )
    {
        char where[320];
        site_name(r.sites[i].file, r.sites[i].line, where, sizeof(where));
        fprintf(
            stderr,
            "torimem:   %ld block(s), %ld bytes  allocated at %s\n",
            (long)r.sites[i].blocks,
            (long)r.sites[i].bytes,
            where);
    }
    if( r.n_sites > REPORT_SITES )
        fprintf(stderr, "torimem:   ... %d more site(s)\n", r.n_sites - REPORT_SITES);
    return r.blocks;
}

static void
report_live_children(struct ToriMem_Group* g)
{
    char path[256];
    group_path(g, path, sizeof(path));
    fprintf(
        stderr,
        "torimem: group %s reset with %ld live child group(s) -- children end first:\n",
        path,
        (long)__atomic_load_n(&g->live_children, __ATOMIC_RELAXED));
    spin_lock(&s_registry_lock);
    int shown = 0;
    for( struct ToriMem_Group* c = s_registry; c && shown < REPORT_SITES; c = c->reg_next )
    {
        if( c->parent == g )
        {
            fprintf(stderr, "torimem:   %s\n", c->name);
            shown++;
        }
    }
    spin_unlock(&s_registry_lock);
}

/* ---- Allocation --------------------------------------------------------- */

static void
check_size(
    size_t size,
    const struct ToriMem_Group* g)
{
    if( size > (size_t)UINT32_MAX - 64u )
        die("single allocation over 4 GB", g);
}

static void*
plain_alloc(
    struct ToriMem_Group* g,
    size_t size,
    int zero)
{
    struct ToriMem_Hdr* h = zero ? calloc(1, HDR_BYTES + size) : malloc(HDR_BYTES + size);
    if( !h )
        die("out of memory", g);
    h->group = g;
    h->size = (uint32_t)size;
    h->flags = 0;
    h->magic = HDR_MAGIC;
    counters_alloc(g, size);
    return h + 1;
}

static void*
list_alloc(
    struct ToriMem_Group* g,
    size_t size,
    int zero,
    const char* file,
    int line)
{
    size_t total = PREFIX_BYTES + HDR_BYTES + size;
    struct ToriMem_Prefix* pre = zero ? calloc(1, total) : malloc(total);
    if( !pre )
        die("out of memory", g);
    struct ToriMem_Hdr* h = (struct ToriMem_Hdr*)((unsigned char*)pre + PREFIX_BYTES);
    pre->file = file;
    pre->line = (uint32_t)line;
    h->group = g;
    h->size = (uint32_t)size;
    h->flags = F_PREFIX;
    h->magic = HDR_MAGIC;

    spin_lock(&g->lock);
    pre->seq = g->next_seq++;
    pre->prev = NULL;
    pre->next = g->list;
    if( g->list )
        g->list->prev = pre;
    g->list = pre;
    spin_unlock(&g->lock);

    counters_alloc(g, size);
    return h + 1;
}

static void
list_unlink(
    struct ToriMem_Group* g,
    struct ToriMem_Prefix* pre)
{
    if( pre->prev )
        pre->prev->next = pre->next;
    else
        g->list = pre->next;
    if( pre->next )
        pre->next->prev = pre->prev;
}

static void*
chunk_alloc(
    struct ToriMem_Group* g,
    size_t size,
    int zero,
    const char* file,
    int line)
{
    size_t p = CHUNK_PREFIX_BYTES;
    size_t need = p + HDR_BYTES + round16(size ? size : 1);
    struct ToriMem_Chunk* c = g->cur;
    if( !c || c->cap - c->used < need )
    {
        /* Chunks after `cur` are empty, kept from an earlier cycle. Skip any
         * too small for this request; append a new one if none fits. */
        struct ToriMem_Chunk* next = c ? c->next : g->chunks;
        while( next && next->cap < need )
            next = next->next;
        if( !next )
        {
            next = arena_take_chunk(g, need);
            if( !g->chunks )
            {
                g->chunks = next;
            }
            else
            {
                struct ToriMem_Chunk* last = g->chunks;
                while( last->next )
                    last = last->next;
                last->next = next;
            }
        }
        g->cur = next;
        c = next;
    }

    unsigned char* pos = c->data + c->used;
    c->used += need;
    if( p )
    {
        struct ToriMem_Prefix* pre = (struct ToriMem_Prefix*)pos;
        pre->prev = NULL;
        pre->next = NULL;
        pre->file = file;
        pre->line = (uint32_t)line;
        pre->seq = g->next_seq++;
    }
    struct ToriMem_Hdr* h = (struct ToriMem_Hdr*)(pos + p);
    h->group = g;
    h->size = (uint32_t)size;
    h->flags = (uint16_t)(F_CHUNK | (p ? F_PREFIX : 0u));
    h->magic = HDR_MAGIC;
    if( zero )
        memset(h + 1, 0, size);
    counters_alloc(g, size);
    return h + 1;
}

static void*
alloc_impl(
    struct ToriMem_Group* g,
    size_t size,
    int zero,
    const char* file,
    int line)
{
    check_live(g);
    check_size(size, g);
    if( g->kind == TORIMEM_POOL )
        die("a pool hands out chunks to its arena children; allocate from a child", g);
    s_any_alloc = 1;
    if( group_uses_chunks(g) )
    {
        check_owner(g);
        return chunk_alloc(g, size, zero, file, line);
    }
    if( group_uses_list(g) )
        return list_alloc(g, size, zero, file, line);
    return plain_alloc(g, size, zero);
}

void*
ToriMem_AllocAt(
    struct ToriMem_Group* group,
    size_t size,
    const char* file,
    int line)
{
    return alloc_impl(group, size, 0, file, line);
}

void*
ToriMem_CallocAt(
    struct ToriMem_Group* group,
    size_t count,
    size_t size,
    const char* file,
    int line)
{
    if( size != 0 && count > SIZE_MAX / size )
        die("calloc size overflows", group);
    return alloc_impl(group, count * size, 1, file, line);
}

char*
ToriMem_StrdupAt(
    struct ToriMem_Group* group,
    const char* str,
    const char* file,
    int line)
{
    if( !str )
        die("strdup of NULL", group);
    size_t len = strlen(str);
    char* out = alloc_impl(group, len + 1, 0, file, line);
    memcpy(out, str, len + 1);
    return out;
}

char*
ToriMem_StrndupAt(
    struct ToriMem_Group* group,
    const char* str,
    size_t max_len,
    const char* file,
    int line)
{
    if( !str )
        die("strndup of NULL", group);
    size_t len = 0;
    while( len < max_len && str[len] )
        len++;
    char* out = alloc_impl(group, len + 1, 0, file, line);
    memcpy(out, str, len);
    out[len] = '\0';
    return out;
}

void*
ToriMem_MemdupAt(
    struct ToriMem_Group* group,
    const void* src,
    size_t size,
    const char* file,
    int line)
{
    if( !src && size )
        die("memdup of NULL", group);
    void* out = alloc_impl(group, size, 0, file, line);
    if( size )
        memcpy(out, src, size);
    return out;
}

/* ---- Free ---------------------------------------------------------------- */

static int
chunk_block_is_last(
    struct ToriMem_Group* g,
    struct ToriMem_Hdr* h)
{
    struct ToriMem_Chunk* c = g->cur;
    if( !c )
        return 0;
    unsigned char* end = (unsigned char*)(h + 1) + round16(h->size ? h->size : 1);
    return end == c->data + c->used;
}

/* Freeing the last block in the current chunk gives its space back, unless an
 * open mark sits above it: the mark's rewind owns that space. */
static void
chunk_try_pop(
    struct ToriMem_Group* g,
    struct ToriMem_Hdr* h)
{
    if( !chunk_block_is_last(g, h) )
        return;
    struct ToriMem_Chunk* c = g->cur;
    unsigned char* start = (unsigned char*)h - CHUNK_PREFIX_BYTES;
    if( g->open_marks > 0 && g->floor_chunk == c && start < c->data + g->floor_off )
        return;
    c->used = (size_t)(start - c->data);
}

static void
free_block(struct ToriMem_Hdr* h)
{
    struct ToriMem_Group* g = h->group;
    check_live(g);
    if( h->flags & F_CHUNK )
    {
        check_owner(g);
        if( h->flags & F_FREED )
            die("double free of an arena block", g);
        h->flags = (uint16_t)(h->flags | F_FREED);
        counters_free(g, h->size);
#if TORIMEM_DEBUG
        memset(h + 1, 0xDD, h->size);
#endif
        chunk_try_pop(g, h);
        return;
    }

    counters_free(g, h->size);
    void* base = h;
    if( h->flags & F_PREFIX )
    {
        struct ToriMem_Prefix* pre = prefix_of(h);
        spin_lock(&g->lock);
        list_unlink(g, pre);
        spin_unlock(&g->lock);
        base = pre;
    }
    h->magic = HDR_DEAD;
    free(base);
}

void
ToriMem_Free(
    struct ToriMem_Group* group,
    void* ptr)
{
    if( !ptr )
        return;
    struct ToriMem_Hdr* h = hdr_of(ptr);
    if( h->group != group )
    {
        char want[256];
        char have[256];
        group_path(group, want, sizeof(want));
        group_path(h->group, have, sizeof(have));
        fprintf(
            stderr,
            "torimem: block of group %s freed through group %s -- a missing ToriMem_Move, or the wrong owner freeing it\n",
            have,
            want);
        violation_end();
    }
    free_block(h);
}

/* ---- Realloc ------------------------------------------------------------- */

void*
ToriMem_ReallocAt(
    struct ToriMem_Group* group,
    void* ptr,
    size_t size,
    const char* file,
    int line)
{
    if( !ptr )
        return alloc_impl(group, size, 0, file, line);
    if( size == 0 )
    {
        ToriMem_Free(group, ptr);
        return NULL;
    }
    struct ToriMem_Hdr* h = hdr_of(ptr);
    if( h->group != group )
    {
        char want[256];
        char have[256];
        group_path(group, want, sizeof(want));
        group_path(h->group, have, sizeof(have));
        fprintf(stderr, "torimem: block of group %s realloc'd through group %s\n", have, want);
        violation_end();
    }
    struct ToriMem_Group* g = h->group;
    check_live(g);
    check_size(size, g);
    size_t old = h->size;

    if( h->flags & F_CHUNK )
    {
        check_owner(g);
        /* The last block grows in place; anything else moves. A doubling
         * vector wastes at most one copy per cycle; the reset reclaims it. */
        if( chunk_block_is_last(g, h) )
        {
            struct ToriMem_Chunk* c = g->cur;
            size_t old_span = round16(old ? old : 1);
            size_t new_span = round16(size);
            if( new_span <= old_span || c->cap - c->used >= new_span - old_span )
            {
                c->used = c->used - old_span + new_span;
                h->size = (uint32_t)size;
                counters_resize(g, old, size);
                return ptr;
            }
        }
        void* np = chunk_alloc(g, size, 0, file, line);
        memcpy(np, ptr, old < size ? old : size);
        free_block(h);
        return np;
    }

    if( h->flags & F_PREFIX )
    {
        /* Keep the block's place on the list: a rewind of an arena-off
         * group depends on the list staying in allocation order. */
        spin_lock(&g->lock);
        struct ToriMem_Prefix* pre = prefix_of(h);
        struct ToriMem_Prefix* prev = pre->prev;
        struct ToriMem_Prefix* next = pre->next;
        struct ToriMem_Prefix* np = realloc(pre, PREFIX_BYTES + HDR_BYTES + size);
        if( !np )
            die("out of memory", g);
        if( prev )
            prev->next = np;
        else
            g->list = np;
        if( next )
            next->prev = np;
        spin_unlock(&g->lock);
        struct ToriMem_Hdr* nh = (struct ToriMem_Hdr*)((unsigned char*)np + PREFIX_BYTES);
        nh->size = (uint32_t)size;
        counters_resize(g, old, size);
        return nh + 1;
    }

    struct ToriMem_Hdr* nh = realloc(h, HDR_BYTES + size);
    if( !nh )
        die("out of memory", g);
    nh->size = (uint32_t)size;
    counters_resize(g, old, size);
    return nh + 1;
}

/* ---- Move ----------------------------------------------------------------- */

static int
is_descendant(
    const struct ToriMem_Group* g,
    const struct ToriMem_Group* ancestor)
{
    for( const struct ToriMem_Group* p = g->parent; p; p = p->parent )
        if( p == ancestor )
            return 1;
    return 0;
}

void*
ToriMem_Move(
    void* ptr,
    struct ToriMem_Group* from,
    struct ToriMem_Group* to)
{
    if( !ptr )
        die("Move of NULL", from);
    check_live(from);
    check_live(to);
    if( to->kind == TORIMEM_POOL )
        die("cannot Move a block into a pool", to);
    struct ToriMem_Hdr* h = hdr_of(ptr);
    if( h->group != from )
    {
        fprintf(stderr, "torimem: Move from %s of a block that belongs to %s\n", from->name, h->group->name);
        violation_end();
        from = h->group;
    }
    if( from == to )
        return ptr;
    if( is_descendant(to, from) )
    {
        fprintf(
            stderr,
            "torimem: Move from %s into its own descendant %s -- memory moves to a longer-lived group\n",
            from->name,
            to->name);
        violation_end();
    }

    /* A block can change groups in place only if its layout fits the new
     * group: not inside a chunk, and with a prefix exactly when the
     * destination keeps a list. Otherwise it is copied. */
    int has_prefix = (h->flags & F_PREFIX) != 0;
    if( (h->flags & F_CHUNK) || group_uses_chunks(to) || has_prefix != group_uses_list(to) )
    {
        const char* file = NULL;
        int line = 0;
        if( has_prefix )
        {
            file = prefix_of(h)->file;
            line = (int)prefix_of(h)->line;
        }
        void* np = alloc_impl(to, h->size, 0, file, line);
        memcpy(np, ptr, h->size);
        free_block(h);
        return np;
    }

    counters_free(from, h->size);
    if( has_prefix )
    {
        struct ToriMem_Prefix* pre = prefix_of(h);
        spin_lock(&from->lock);
        list_unlink(from, pre);
        spin_unlock(&from->lock);
        spin_lock(&to->lock);
        pre->seq = to->next_seq++;
        pre->prev = NULL;
        pre->next = to->list;
        if( to->list )
            to->list->prev = pre;
        to->list = pre;
        spin_unlock(&to->lock);
    }
    h->group = to;
    counters_alloc(to, h->size);
    __atomic_sub_fetch(&to->total_allocs, 1, __ATOMIC_RELAXED);
    return ptr;
}

void*
ToriMem_Adopt(
    void* ptr,
    struct ToriMem_Group* to)
{
    if( !ptr )
        die("Adopt of NULL", to);
    return ToriMem_Move(ptr, hdr_of(ptr)->group, to);
}

struct ToriMem_Group*
ToriMem_GroupOf(const void* ptr)
{
    if( !ptr )
        die("GroupOf NULL", NULL);
    return hdr_of(ptr)->group;
}

/* ---- Mark / rewind ------------------------------------------------------- */

struct ToriMem_Mark
ToriMem_Mark(struct ToriMem_Group* group)
{
    check_live(group);
    if( group->kind != TORIMEM_ARENA )
        die("Mark is for arenas", group);
    check_owner(group);
    struct ToriMem_Mark m;
    memset(&m, 0, sizeof(m));
    m.chunk = group->cur;
    m.off = group->cur ? group->cur->used : 0;
    m.seq = group->next_seq;
    m.prev_floor_chunk = group->floor_chunk;
    m.prev_floor_off = group->floor_off;
    group->floor_chunk = m.chunk;
    group->floor_off = m.off;
    group->open_marks++;
    m.depth = group->open_marks;
    return m;
}

static void
release_chunk_block(
    struct ToriMem_Hdr* h,
    void* ctx)
{
    if( h->flags & F_FREED )
        return;
    h->flags = (uint16_t)(h->flags | F_FREED);
    counters_free((struct ToriMem_Group*)ctx, h->size);
}

static void
rewind_impl(
    struct ToriMem_Group* g,
    struct ToriMem_Mark m,
    int checked)
{
    check_live(g);
    if( g->kind != TORIMEM_ARENA )
        die("Rewind is for arenas", g);
    check_owner(g);
    if( m.depth != g->open_marks )
        die("marks rewound out of order: rewind the innermost mark first", g);

    if( checked && report_live(g, "rewound", m.chunk, m.off, m.seq, 0) )
        violation_end();

    if( group_uses_chunks(g) )
    {
        arena_walk(g, m.chunk, m.off, release_chunk_block, g);
        for( struct ToriMem_Chunk* c = m.chunk ? m.chunk->next : g->chunks; c; c = c->next )
        {
#if TORIMEM_DEBUG
            memset(c->data, 0xDD, c->used);
#endif
            c->used = 0;
        }
        if( m.chunk )
        {
#if TORIMEM_DEBUG
            memset(m.chunk->data + m.off, 0xDD, m.chunk->used - m.off);
#endif
            m.chunk->used = m.off;
        }
        g->cur = m.chunk;
    }
    else
    {
        spin_lock(&g->lock);
        while( g->list && g->list->seq >= m.seq )
        {
            struct ToriMem_Prefix* pre = g->list;
            struct ToriMem_Hdr* h = (struct ToriMem_Hdr*)((unsigned char*)pre + PREFIX_BYTES);
            list_unlink(g, pre);
            counters_free(g, h->size);
            h->magic = HDR_DEAD;
            free(pre);
        }
        spin_unlock(&g->lock);
    }

    g->floor_chunk = m.prev_floor_chunk;
    g->floor_off = m.prev_floor_off;
    g->open_marks--;
}

void
ToriMem_Rewind(
    struct ToriMem_Group* group,
    struct ToriMem_Mark mark)
{
    rewind_impl(group, mark, 0);
}

void
ToriMem_CheckedRewind(
    struct ToriMem_Group* group,
    struct ToriMem_Mark mark)
{
    rewind_impl(group, mark, 1);
}

/* ---- Reset / end ---------------------------------------------------------- */

static void
reset_impl(
    struct ToriMem_Group* g,
    const char* event)
{
    check_live(g);
    if( __atomic_load_n(&g->live_children, __ATOMIC_RELAXED) != 0 )
    {
        report_live_children(g);
        violation_end();
    }
    if( g->kind == TORIMEM_POOL )
        return;
    if( g->kind == TORIMEM_ARENA )
    {
        check_owner(g);
        if( g->open_marks != 0 )
        {
            fprintf(stderr, "torimem: group %s %s with %d open mark(s)\n", g->name, event, g->open_marks);
            violation_end();
        }
    }

    if( report_live(g, event, NULL, 0, 0, 1) )
    {
        /* An arena reclaims its live blocks below, so carrying on is safe. A
         * heap group's live blocks would keep naming this group after it is
         * gone; at exit nothing can free them any more. */
        if( g->kind == TORIMEM_HEAP && !s_at_exit )
            die("heap group reset or ended with live blocks (see above)", g);
        violation_end();
    }

    if( g->kind != TORIMEM_ARENA )
        return;

    if( group_uses_chunks(g) )
    {
        /* Keep the regular chunks for the next cycle. An oversize chunk was
         * made for one request; keeping it would pin that high-water mark. */
        struct ToriMem_Chunk** link = &g->chunks;
        while( *link )
        {
            struct ToriMem_Chunk* c = *link;
            if( c->oversize )
            {
                *link = c->next;
                g->chunk_bytes -= (intptr_t)c->cap;
                free(c);
                continue;
            }
#if TORIMEM_DEBUG
            memset(c->data, 0xDD, c->used);
#endif
            c->used = 0;
            link = &c->next;
        }
        g->cur = NULL;
    }
    else
    {
        spin_lock(&g->lock);
        while( g->list )
        {
            struct ToriMem_Prefix* pre = g->list;
            struct ToriMem_Hdr* h = (struct ToriMem_Hdr*)((unsigned char*)pre + PREFIX_BYTES);
            list_unlink(g, pre);
            h->magic = HDR_DEAD;
            free(pre);
        }
        spin_unlock(&g->lock);
    }
    g->live_blocks = 0;
    g->live_bytes = 0;
    g->floor_chunk = NULL;
    g->floor_off = 0;
}

void
ToriMem_GroupReset(struct ToriMem_Group* group)
{
    reset_impl(group, "reset");
}

void
ToriMem_GroupEnd(struct ToriMem_Group* group)
{
    reset_impl(group, "ended");
    if( group->kind == TORIMEM_ARENA )
        arena_release_chunks(group);
    if( group->kind == TORIMEM_POOL )
    {
        struct ToriMem_Chunk* c = group->pool_free;
        while( c )
        {
            struct ToriMem_Chunk* next = c->next;
            free(c);
            c = next;
        }
        group->pool_free = NULL;
        group->chunk_bytes = 0;
    }
    registry_remove(group);
    if( group->parent )
        __atomic_sub_fetch(&group->parent->live_children, 1, __ATOMIC_RELAXED);
    group->live = 0;
}

struct ToriMem_Group*
ToriMem_ArenaNew(
    const char* name,
    struct ToriMem_Group* parent,
    size_t chunk_bytes)
{
    if( !name )
        die("group needs a name", parent);
    size_t len = strlen(name);
    /* The allocator's own memory: the struct is not a block of any group. */
    struct ToriMem_Group* g = malloc(sizeof(*g) + len + 1);
    if( !g )
        die("out of memory allocating a group", parent);
    char* copy = (char*)(g + 1);
    memcpy(copy, name, len + 1);
    ToriMem_ArenaInit(g, copy, parent, chunk_bytes);
    g->heap_struct = 1;
    return g;
}

void
ToriMem_GroupDelete(struct ToriMem_Group* group)
{
    if( !group )
        return;
    if( !group->heap_struct )
        die("GroupDelete of a group not made by ToriMem_ArenaNew", group);
    ToriMem_GroupEnd(group);
    free(group);
}

/* ---- FRAME ---------------------------------------------------------------- */

struct ToriMem_Group*
ToriMem_Frame(void)
{
    if( !s_frame.live )
        ToriMem_ArenaInit(&s_frame, "frame", ToriMem_Process(), FRAME_CHUNK_BYTES);
    return &s_frame;
}

void
ToriMem_FrameEnd(void)
{
    if( s_frame.live )
        ToriMem_GroupReset(&s_frame);
}

void
ToriMem_ThreadExit(void)
{
    if( s_frame.live )
        ToriMem_GroupEnd(&s_frame);
}

/* ---- Library hooks --------------------------------------------------------- */

struct ToriMem_Group*
ToriMem_LibRSCache(void)
{
    static struct ToriMem_Group g;
    if( !g.live )
        ToriMem_GroupInit(&g, "lib:rscache", ToriMem_Process());
    return &g;
}

struct ToriMem_Group*
ToriMem_LibToriDraw(void)
{
    static struct ToriMem_Group g;
    if( !g.live )
        ToriMem_GroupInit(&g, "lib:toridraw", ToriMem_Process());
    return &g;
}

struct ToriMem_Group*
ToriMem_LibTRSPK(void)
{
    static struct ToriMem_Group g;
    if( !g.live )
        ToriMem_GroupInit(&g, "lib:trspk", ToriMem_Process());
    return &g;
}

struct ToriMem_Group*
ToriMem_LibHMap(void)
{
    static struct ToriMem_Group g;
    if( !g.live )
        ToriMem_GroupInit(&g, "lib:hmap", ToriMem_Process());
    return &g;
}

struct ToriMem_Group*
ToriMem_LibRSProt(void)
{
    static struct ToriMem_Group g;
    if( !g.live )
        ToriMem_GroupInit(&g, "lib:rsprot", ToriMem_Process());
    return &g;
}

struct ToriMem_Group*
ToriMem_LibRSAreaBuf(void)
{
    static struct ToriMem_Group g;
    if( !g.live )
        ToriMem_GroupInit(&g, "lib:rsareabuf", ToriMem_Process());
    return &g;
}

struct ToriMem_Group*
ToriMem_LibBZip(void)
{
    static struct ToriMem_Group g;
    if( !g.live )
        ToriMem_GroupInit(&g, "lib:bzip", ToriMem_Process());
    return &g;
}


struct ToriMem_Scope
ToriMem_ScopeEnter(struct ToriMem_Group* group)
{
    check_live(group);
    if( group->kind == TORIMEM_POOL )
        die("scope a call to a group that allocates, not a pool", group);
    struct ToriMem_Scope s;
    s.prev = s_scope;
    s_scope = group;
    return s;
}

void
ToriMem_ScopeLeave(struct ToriMem_Scope scope)
{
    s_scope = scope.prev;
}

void*
ToriMem_LibMalloc(
    size_t size,
    void* user)
{
    struct ToriMem_Group* g = s_scope ? s_scope : (struct ToriMem_Group*)user;
    return alloc_impl(g, size, 0, LIB_SITE(), (int)LIB_SITE_LINE);
}

void*
ToriMem_LibCalloc(
    size_t count,
    size_t size,
    void* user)
{
    struct ToriMem_Group* g = s_scope ? s_scope : (struct ToriMem_Group*)user;
    if( size != 0 && count > SIZE_MAX / size )
        die("calloc size overflows", g);
    return alloc_impl(g, count * size, 1, LIB_SITE(), (int)LIB_SITE_LINE);
}

void*
ToriMem_LibRealloc(
    void* ptr,
    size_t size,
    void* user)
{
    if( !ptr )
    {
        struct ToriMem_Group* g = s_scope ? s_scope : (struct ToriMem_Group*)user;
        return alloc_impl(g, size, 0, LIB_SITE(), (int)LIB_SITE_LINE);
    }
    struct ToriMem_Hdr* h = hdr_of(ptr);
    if( size == 0 )
    {
        free_block(h);
        return NULL;
    }
    return ToriMem_ReallocAt(h->group, ptr, size, LIB_SITE(), (int)LIB_SITE_LINE);
}

void
ToriMem_LibFree(
    void* ptr,
    void* user)
{
    (void)user;
    if( !ptr )
        return;
    free_block(hdr_of(ptr));
}

/* ---- Shutdown and the stats tree ------------------------------------------ */

struct dump_node
{
    struct ToriMem_Group* g;
    int parent;
    intptr_t sub_live;
    intptr_t sub_blocks;
    intptr_t sub_chunks;
    int printed;
};

static void
dump_print(
    FILE* out,
    struct dump_node* nodes,
    int n,
    int idx,
    int depth)
{
    for( int i = 0; i < n; i++ )
    {
        if( nodes[i].parent != idx || nodes[i].printed )
            continue;
        /* Fold siblings with the same name: four hundred tasks are one row. */
        intptr_t live = 0;
        intptr_t blocks = 0;
        intptr_t chunks = 0;
        intptr_t peak = 0;
        int count = 0;
        for( int j = i; j < n; j++ )
        {
            if( nodes[j].parent != idx || nodes[j].printed )
                continue;
            if( strcmp(nodes[j].g->name, nodes[i].g->name) != 0 )
                continue;
            live += nodes[j].sub_live;
            blocks += nodes[j].sub_blocks;
            chunks += nodes[j].sub_chunks;
            intptr_t pk = __atomic_load_n(&nodes[j].g->peak_bytes, __ATOMIC_RELAXED);
            if( pk > peak )
                peak = pk;
            if( j != i )
                nodes[j].printed = 1;
            count++;
        }
        nodes[i].printed = 1;
        char label[96];
        if( count > 1 )
            snprintf(label, sizeof(label), "%*s%s x%d", depth * 2, "", nodes[i].g->name, count);
        else
            snprintf(label, sizeof(label), "%*s%s", depth * 2, "", nodes[i].g->name);
        fprintf(
            out,
            "torimem: %-40s %12ld %9ld %12ld %12ld\n",
            label,
            (long)live,
            (long)blocks,
            (long)chunks,
            (long)peak);
        if( count == 1 )
            dump_print(out, nodes, n, i, depth + 1);
    }
}

void
ToriMem_DumpTree(FILE* out)
{
    if( !out )
        die("DumpTree to NULL", NULL);
    spin_lock(&s_registry_lock);
    int n = 0;
    for( struct ToriMem_Group* g = s_registry; g; g = g->reg_next )
        n++;
    struct dump_node* nodes = calloc((size_t)(n ? n : 1), sizeof(*nodes));
    if( !nodes )
        die("out of memory for the stats dump", NULL);
    int i = 0;
    for( struct ToriMem_Group* g = s_registry; g; g = g->reg_next )
    {
        nodes[i].g = g;
        nodes[i].parent = -1;
        i++;
    }
    for( i = 0; i < n; i++ )
    {
        for( int j = 0; j < n; j++ )
            if( nodes[i].g->parent == nodes[j].g )
                nodes[i].parent = j;
        /* Roll this group's own numbers into it and every ancestor. */
        intptr_t live = __atomic_load_n(&nodes[i].g->live_bytes, __ATOMIC_RELAXED);
        intptr_t blocks = __atomic_load_n(&nodes[i].g->live_blocks, __ATOMIC_RELAXED);
        intptr_t chunks = nodes[i].g->chunk_bytes;
        for( const struct ToriMem_Group* p = nodes[i].g; p; p = p->parent )
        {
            for( int j = 0; j < n; j++ )
            {
                if( nodes[j].g == p )
                {
                    nodes[j].sub_live += live;
                    nodes[j].sub_blocks += blocks;
                    nodes[j].sub_chunks += chunks;
                }
            }
        }
    }
    fprintf(
        out,
        "torimem: %-40s %12s %9s %12s %12s  (arenas %s)\n",
        "group",
        "live_bytes",
        "blocks",
        "chunk_bytes",
        "own_peak",
        s_arenas_on ? "on" : "off");
    dump_print(out, nodes, n, -1, 0);
    spin_unlock(&s_registry_lock);
    free(nodes);
}

void
ToriMem_Shutdown(void)
{
    start_process();
    if( s_stats )
        ToriMem_DumpTree(stderr);
    /* From here a heap group's live blocks can never be freed again, so they
     * are reported, not fatal, unless TORIRS_MEM_STRICT is set. */
    s_at_exit = 1;
    if( s_frame.live )
        ToriMem_GroupEnd(&s_frame);

    /* End whatever is still live, leaves first, so its findings are named by
     * group rather than folded into PROCESS. The library groups end here.
     * Another thread's FRAME cannot be ended from this one: it is named. */
    for( ;; )
    {
        struct ToriMem_Group* leaf = NULL;
        spin_lock(&s_registry_lock);
        for( struct ToriMem_Group* g = s_registry; g; g = g->reg_next )
        {
            if( g == &s_process || __atomic_load_n(&g->live_children, __ATOMIC_RELAXED) != 0 )
                continue;
            if( g->kind == TORIMEM_ARENA && g->owner_thread != THREAD_ID )
                continue;
            leaf = g;
            break;
        }
        spin_unlock(&s_registry_lock);
        if( !leaf )
            break;
        ToriMem_GroupEnd(leaf);
    }
    reset_impl(&s_process, "ended at exit");
}
