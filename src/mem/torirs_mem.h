#ifndef TORIRS_MEM_H
#define TORIRS_MEM_H

/*
 * Every allocation belongs to a lifetime GROUP: the set of allocations that
 * die at the same event. Groups nest: a child never outlives its parent.
 * Every allocation is freed explicitly, from the group it came from. Every
 * group is reset or ended at one call site, and the reset WALKS the group to
 * prove the frees already happened.
 *
 *   PROCESS (heap) --+-- FRAME[thread] (arena)          reset: frame_loop_step end
 *   ends at exit     +-- TASKS (pool) -- task:* (arena)  end: task_free
 *                    +-- VMS   (pool) -- vm:*   (arena)  reset: script done
 *                    +-- renderer, mixer, plugin:*, iface:*, cacheprovider (heap)
 *                    +-- lib:rscache, lib:toridraw, lib:trspk, ...      end: exit
 *                    +-- SESSION (heap) --+-- SCENE (arena)  reset: region rebuild
 *                        end: App_Logout  +-- transport, the game stores
 *
 * THE RULE: a pointer may only target memory whose group resets no earlier
 * than the group holding the pointer. An ancestor always qualifies. A sibling
 * branch qualifies only where a safe point guarantees the order.
 *
 * THE DISCIPLINE: free what you allocate, from the group you allocated it in.
 * The reset is the check, not the free.
 *
 * ---- Kinds ----------------------------------------------------------------
 *
 *   HEAP    every block is its own malloc, with a header naming its group.
 *           Free really frees. End asserts the group is empty.
 *
 *   ARENA   blocks are bumped inside chunks. Free marks the block freed and
 *           counts it down; freeing the LAST block gives its space back.
 *           Mark/Rewind release everything allocated since the mark. Reset
 *           checks every block was freed, then rewinds the chunks (kept for
 *           the next cycle). End checks, then returns the chunks.
 *
 *   POOL    holds chunks for its ARENA children: a child takes chunks on first
 *           use and returns them when it ends. A pool allocates no blocks.
 *
 * ---- Failure policy -------------------------------------------------------
 *
 *   A reset or end that finds live blocks names the group (and, with sites
 *   on -- a debug build, or TORIRS_MEM_SITES=1 -- every live block's
 *   allocation site), then:
 *     debug build (no NDEBUG) ............ aborts
 *     TORIRS_MEM_STRICT=1 ................ aborts, any build
 *     release, ARENA ..................... prints a torimem: line, reclaims
 *     release, PROCESS at exit ........... prints a torimem: line
 *     release, other HEAP ................ aborts: its live blocks still name
 *                                          a group about to be destroyed, so
 *                                          a later free would be a use after
 *                                          free
 *   Allocation failure, a bad header, a double free, and misuse of a kind
 *   abort in every build.
 *
 * ---- Off switch (ASan, MEMTRACE, MEMPROF) ---------------------------------
 *
 *   -DTORIMEM_ARENAS=0 (forced by ENABLE_ASAN=1, MEMTRACE=1, MEMPROF=1), or
 *   TORIRS_ARENAS=off at runtime: every ARENA block becomes its own malloc on
 *   the group's list. Free frees, Rewind frees everything since the mark,
 *   Reset checks and frees what is left. The lifetimes do not change, so
 *   ASan reports a pointer kept past its reset as heap-use-after-free.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef TORIMEM_ARENAS
#define TORIMEM_ARENAS 1
#endif

#if !defined(NDEBUG)
#define TORIMEM_DEBUG 1
#else
#define TORIMEM_DEBUG 0
#endif

enum ToriMem_Kind
{
    TORIMEM_HEAP = 0,
    TORIMEM_ARENA = 1,
    TORIMEM_POOL = 2,
};

struct ToriMem_Chunk;
struct ToriMem_Prefix;

struct ToriMem_Group
{
    const char* name;
    struct ToriMem_Group* parent;
    int kind;
    int live; /* 1 between Init and End */

    /* Counters. Atomic: a HEAP group may be touched from the audio thread
     * under the device lock while the frame thread holds it too. */
    intptr_t live_blocks;
    intptr_t live_bytes;
    intptr_t peak_bytes;
    intptr_t total_allocs;
    intptr_t live_children;
    intptr_t chunk_bytes; /* ARENA/POOL: bytes of chunk memory held */

    /* Registry of every live group, for the stats tree. */
    struct ToriMem_Group* reg_prev;
    struct ToriMem_Group* reg_next;

    /* Blocks that are their own malloc and carry a prefix: HEAP blocks in a
     * debug build, ARENA blocks with arenas off. Newest first. */
    struct ToriMem_Prefix* list;
    uint32_t next_seq;
    int lock;

    /* ARENA */
    struct ToriMem_Chunk* chunks;
    struct ToriMem_Chunk* cur;
    struct ToriMem_Chunk* floor_chunk; /* innermost open mark */
    size_t floor_off;
    int open_marks;
    size_t chunk_size; /* ARENA without a POOL parent, and POOL */
    void const* owner_thread;

    /* POOL */
    struct ToriMem_Chunk* pool_free;

    /* Made by ToriMem_ArenaNew: the struct (and its name) are the
     * allocator's, released by ToriMem_GroupDelete. */
    int heap_struct;
};

struct ToriMem_Mark
{
    struct ToriMem_Chunk* chunk;
    size_t off;
    uint32_t seq;
    struct ToriMem_Chunk* prev_floor_chunk;
    size_t prev_floor_off;
    int depth;
};

/* ---- Process setup ------------------------------------------------------ */

/** First thing in main(): reads TORIRS_ARENAS, TORIRS_MEM_STRICT and
 *  TORIRS_MEM_STATS and starts PROCESS. Aborts if any group allocated before
 *  it. A binary that never calls it still works: the first allocation starts
 *  PROCESS with the defaults. */
void
ToriMem_Init(void);

/** Last thing before exit: ends this thread's FRAME, then PROCESS, which
 *  reports anything still live at exit. Prints the stats tree first if
 *  TORIRS_MEM_STATS is set. */
void
ToriMem_Shutdown(void);

/** The root group. Children: FRAME, the pools, every object group. */
struct ToriMem_Group*
ToriMem_Process(void);

/** The pool every task's arena draws its chunks from (4 KB chunks). */
struct ToriMem_Group*
ToriMem_Tasks(void);

/** The pool every script VM's arena draws its chunks from (16 KB chunks). */
struct ToriMem_Group*
ToriMem_Vms(void);

/** ASSET: everything the cache provider keeps -- decoded types, models,
 *  sprites, fonts, sounds -- and every ToriRS_* destructor frees from it. A
 *  heap group under PROCESS; entries leave one at a time on eviction. */
struct ToriMem_Group*
ToriMem_Assets(void);

/* ---- Subsystem groups ----------------------------------------------------
 *
 * Every allocation in src/ belongs to exactly one of these, or to FRAME, a
 * task's arena (ToriRS_TaskMem), ASSET, or a lib:* group. Each is a HEAP
 * group under PROCESS, made on first use from any thread. Its blocks are
 * freed one at a time by the subsystem that owns them; the exit walk
 * (ToriMem_Shutdown) proves each subsystem freed everything it allocated.
 *
 *   PROCESS (heap) -- app, boot, config, prefs: lives until exit
 *   |
 *   |-- client ----------------------------------------------------------
 *   |   ui        the UI tree, interfaces, layout, chrome, revconfig
 *   |   render    renderers, painters, raster state, GPU staging
 *   |   audio     mixer, sound bank, audio backends   (audio thread too)
 *   |   net       transports, IO, protocol buffers    (IO thread too)
 *   |   session   game state from login to logout: varps, varcs, invs,
 *   |             chat, clan, loot, social, entity state
 *   |   scene     the built region: world builder output, collision,
 *   |             minimap, world entities' scene data
 *   |   plugins   the plugin host and every plugin
 *   |   script    the CS2 VM and client script queue
 *   |   editor    the map editor and its daemon
 *   |   assets    the cache provider's decoded types (ToriMem_Assets)
 *   |
 *   |-- server ----------------------------------------------------------
 *   |   server:content   packs, configs, db tables, content text
 *   |   server:world     zones, npcs, locs, objs, scene, instances
 *   |   server:players   sessions, players, banks, containers
 *   |   server:scripts   the ServerScript VM and its states
 *   |   server:net       links, pipes, websocket, js5 serving
 *   |
 *   +-- compiler  the ServerScript / CS2 compilers (build-time tools too)
 *
 * Today each ends at exit. Session and scene are the next to get their own
 * checked ends: logout and region rebuild.
 */
struct ToriMem_Group* ToriMem_Ui(void);
struct ToriMem_Group* ToriMem_Render(void);
struct ToriMem_Group* ToriMem_Audio(void);
struct ToriMem_Group* ToriMem_Net(void);
struct ToriMem_Group* ToriMem_Session(void);
struct ToriMem_Group* ToriMem_Scene(void);
struct ToriMem_Group* ToriMem_Plugins(void);
struct ToriMem_Group* ToriMem_Script(void);
struct ToriMem_Group* ToriMem_Editor(void);
struct ToriMem_Group* ToriMem_ServerContent(void);
struct ToriMem_Group* ToriMem_ServerWorld(void);
struct ToriMem_Group* ToriMem_ServerPlayers(void);
struct ToriMem_Group* ToriMem_ServerScripts(void);
struct ToriMem_Group* ToriMem_ServerNet(void);
struct ToriMem_Group* ToriMem_Compiler(void);

int
ToriMem_ArenasOn(void);

/* ---- Groups ------------------------------------------------------------- */

void
ToriMem_GroupInit(
    struct ToriMem_Group* group,
    const char* name,
    struct ToriMem_Group* parent);

/** An ARENA. `chunk_bytes` 0 takes the chunk size of a POOL parent. */
void
ToriMem_ArenaInit(
    struct ToriMem_Group* group,
    const char* name,
    struct ToriMem_Group* parent,
    size_t chunk_bytes);

void
ToriMem_PoolInit(
    struct ToriMem_Group* group,
    const char* name,
    struct ToriMem_Group* parent,
    size_t chunk_bytes);

/** The one place a group's blocks are released, for a group that is used
 *  again (FRAME, SCENE, CONTENT, vm:*). Checks every block was freed and that
 *  no child group is live, then (ARENA) rewinds the chunks. */
void
ToriMem_GroupReset(struct ToriMem_Group* group);

/** Reset, then retire the group: chunks back to the parent pool or malloc,
 *  out of the registry. The struct may be Init'ed again afterwards. */
void
ToriMem_GroupEnd(struct ToriMem_Group* group);

/** An ARENA whose struct the allocator owns, for an object that is itself
 *  freed before its group can be checked (a task's shell is freed by its own
 *  vtable). The name is copied. */
struct ToriMem_Group*
ToriMem_ArenaNew(
    const char* name,
    struct ToriMem_Group* parent,
    size_t chunk_bytes);

/** End a group made by ToriMem_ArenaNew and release its struct. NULL ok. */
void
ToriMem_GroupDelete(struct ToriMem_Group* group);

/* ---- Blocks ------------------------------------------------------------- */

/* Always passed; stored only when sites are on (a debug build, or
 * TORIRS_MEM_SITES=1 in any build). OPT=1 is the default lane here, so the
 * walk naming sites cannot be a debug-only feature. */
#define TORIMEM_SITE __FILE__, __LINE__

void*
ToriMem_AllocAt(
    struct ToriMem_Group* group,
    size_t size,
    const char* file,
    int line);

void*
ToriMem_CallocAt(
    struct ToriMem_Group* group,
    size_t count,
    size_t size,
    const char* file,
    int line);

void*
ToriMem_ReallocAt(
    struct ToriMem_Group* group,
    void* ptr,
    size_t size,
    const char* file,
    int line);

char*
ToriMem_StrdupAt(
    struct ToriMem_Group* group,
    const char* str,
    const char* file,
    int line);

char*
ToriMem_StrndupAt(
    struct ToriMem_Group* group,
    const char* str,
    size_t max_len,
    const char* file,
    int line);

void*
ToriMem_MemdupAt(
    struct ToriMem_Group* group,
    const void* src,
    size_t size,
    const char* file,
    int line);

/** Never returns NULL. Size 0 still returns a distinct block. */
#define ToriMem_Alloc(group, size) ToriMem_AllocAt((group), (size), TORIMEM_SITE)
#define ToriMem_Calloc(group, count, size)                                                         \
    ToriMem_CallocAt((group), (count), (size), TORIMEM_SITE)
/** `ptr` may be NULL. Size 0 frees and returns NULL. */
#define ToriMem_Realloc(group, ptr, size) ToriMem_ReallocAt((group), (ptr), (size), TORIMEM_SITE)
#define ToriMem_Strdup(group, str) ToriMem_StrdupAt((group), (str), TORIMEM_SITE)
#define ToriMem_Strndup(group, str, max_len)                                                       \
    ToriMem_StrndupAt((group), (str), (max_len), TORIMEM_SITE)
#define ToriMem_Memdup(group, src, size) ToriMem_MemdupAt((group), (src), (size), TORIMEM_SITE)

/** NULL is accepted. Aborts if the block belongs to another group. */
void
ToriMem_Free(
    struct ToriMem_Group* group,
    void* ptr);

/** Hand a block to a longer-lived group. Returns the block's new address:
 *  an ARENA block is copied. Aborts if `to` is a descendant of `from`. */
void*
ToriMem_Move(
    void* ptr,
    struct ToriMem_Group* from,
    struct ToriMem_Group* to);

/** `to` takes ownership of a block, from whichever group holds it now: the
 *  one statement at a steal ("this decoded array is the cache's now"). Same
 *  rules and return value as ToriMem_Move. */
void*
ToriMem_Adopt(
    void* ptr,
    struct ToriMem_Group* to);

/** The group a block belongs to. Aborts on a block not from a group. */
struct ToriMem_Group*
ToriMem_GroupOf(const void* ptr);

/* ---- Scratch inside an ARENA -------------------------------------------- */

/** Marks nest; rewind them innermost first. */
struct ToriMem_Mark
ToriMem_Mark(struct ToriMem_Group* group);

/** Release everything allocated since the mark: the rewind IS the free. */
void
ToriMem_Rewind(
    struct ToriMem_Group* group,
    struct ToriMem_Mark mark);

/** A nested reset: check everything allocated since the mark was freed,
 *  then rewind. A server tick inside the client's frame ends with this. */
void
ToriMem_CheckedRewind(
    struct ToriMem_Group* group,
    struct ToriMem_Mark mark);

/* ---- FRAME -------------------------------------------------------------- */

/** This thread's FRAME arena, created on first use. */
struct ToriMem_Group*
ToriMem_Frame(void);

/** Reset this thread's FRAME. Called at the end of one loop iteration:
 *  frame_loop_step (client), the host loop (standalone server). */
void
ToriMem_FrameEnd(void);

/** A thread other than main that used FRAME ends it before it exits. */
void
ToriMem_ThreadExit(void);

/* ---- Library allocations ------------------------------------------------ */

/*
 * One group per library, under PROCESS, ended at exit. A library's allocation
 * lands here unless the calling thread is inside a scope. Host code that frees
 * library memory names the library's group; host code that builds memory a
 * library will free (a sprite's pixels, a model's arrays) allocates it here,
 * in its final owner's group. src/mem/torirs_mem_libs.h installs the hooks.
 */
struct ToriMem_Group*
ToriMem_LibRSCache(void);

struct ToriMem_Group*
ToriMem_LibToriDraw(void);

struct ToriMem_Group*
ToriMem_LibTRSPK(void);

struct ToriMem_Group*
ToriMem_LibHMap(void);

struct ToriMem_Group*
ToriMem_LibRSProt(void);

struct ToriMem_Group*
ToriMem_LibRSAreaBuf(void);

struct ToriMem_Group*
ToriMem_LibBZip(void);


struct ToriMem_Scope
{
    struct ToriMem_Group* prev;
};

/** Route this thread's library allocations to `group` until the matching
 *  Leave. Scopes nest. Outside any scope a library allocates from the group
 *  it was installed with (its lib:* group). */
struct ToriMem_Scope
ToriMem_ScopeEnter(struct ToriMem_Group* group);

void
ToriMem_ScopeLeave(struct ToriMem_Scope scope);

/* The hook signature every library's SetAllocator takes; `user` is the
 * library's lib:* group. A library's free and realloc credit the group in
 * the block's header, whichever scope they run in. */
void*
ToriMem_LibMalloc(
    size_t size,
    void* user);

void*
ToriMem_LibCalloc(
    size_t count,
    size_t size,
    void* user);

void*
ToriMem_LibRealloc(
    void* ptr,
    size_t size,
    void* user);

void
ToriMem_LibFree(
    void* ptr,
    void* user);

/* ---- Audit -------------------------------------------------------------- */

/** The group tree with live and peak bytes per node, children rolled into
 *  their parents. Siblings with the same name are folded ("task:x  x12"). */
void
ToriMem_DumpTree(FILE* out);


/* Point a library's own allocator interface (struct <Type>_Allocator, set by
 * its <set_fn>) at a lib:* group. The libraries know nothing of ToriMem: each
 * defaults to malloc/free, and a binary that never installs keeps them that
 * way. See mem/torirs_mem_libs.h. */
#define TORIMEM_INSTALL_LIB(Type, set_fn, group_fn)                                                \
    do                                                                                             \
    {                                                                                              \
        struct Type##_Allocator a = {                                                              \
            ToriMem_LibMalloc, ToriMem_LibCalloc, ToriMem_LibRealloc, ToriMem_LibFree, group_fn(), \
        };                                                                                         \
        set_fn(&a);                                                                                \
    } while( 0 )

#endif /* TORIRS_MEM_H */
