#ifndef CS2VM2_H
#define CS2VM2_H

#include "cs2_opcode.h"
#include "cs2vm2_host.h"
#include "cs2vm2_script.h"
#include "cs2vm2_strpool.h"

#include <stddef.h>
#include <stdint.h>

#define CS2VM_SCRIPT_ARG_MOUSE_X -2147483647
#define CS2VM_SCRIPT_ARG_MOUSE_Y -2147483646
#define CS2VM_SCRIPT_ARG_WIDGET_ID -2147483645
#define CS2VM_SCRIPT_ARG_OP_INDEX -2147483644
#define CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX -2147483643
#define CS2VM_SCRIPT_ARG_DRAG_TARGET_ID -2147483642
#define CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX -2147483641
#define CS2VM_SCRIPT_ARG_KEY_TYPED -2147483640
#define CS2VM_SCRIPT_ARG_KEY_PRESSED -2147483639
#define CS2VM_SCRIPT_ARG_OP_SUBINDEX -2147483638

/* On CS2VM_EXECNO_YIELD the host must not partially mutate VM state; the opcode
 * checkpoint in CS2VM2_RunScript rolls back stack, frames, and pc so RunScript can
 * be re-entered after external host work. */
#define CS2VM_EXECNO_YIELD -2
#define CS2VM_EXECNO_ERROR -1
#define CS2VM_EXECNO_OK 0
#define CS2VM_EXECNO_DONE 1

#define CS2VM_STACK_MAX 1024

struct CS2VM2;

#define CS2VM_USER(thread) ((struct CS2VM2_Thread*)(thread))->vm->user
/* Only valid where frame_sp > 0; frames below frame_sp are always allocated
 * (see the frames[] comment on struct CS2VM2_Thread). */
#define CS2VM_FRAME(thread) ((struct CS2VM2_Thread*)(thread))->frames[((struct CS2VM2_Thread*)(thread))->frame_sp - 1]
#define CS2VM_MAX_LOCALS 1024
struct CS2VM2_Frame
{
    struct CS2VM2_Script* script;
    int pc;
    /*
     * Locals, grown to what a script actually touches instead of reserved at
     * CS2VM_MAX_LOCALS. `_cap` is the allocated length, `_dirty` how far the
     * current occupant has written; NULL with both zero is the state of a frame
     * that has never run anything, and is what a fresh block starts in.
     *
     * Two invariants carry the whole scheme:
     *
     *   1. slots in [dirty, cap) are zero;
     *   2. slots at or above cap are *logically* zero — nothing is stored there,
     *      and every read of one answers 0 / NULL.
     *
     * Together they make "above dirty" the single test a read needs, so an
     * unwritten local never touches the buffer at all, and they mean a push has
     * to clear only [0, dirty) to hand the next occupant the all-zero locals it
     * is entitled to.
     *
     * The reservation this replaces was CS2VM_MAX_LOCALS ints plus
     * CS2VM_MAX_LOCALS pointers inline in the struct — 12,288 of a 12,352-byte
     * frame, up to 1.51 MB if the stack ever went its full 128 deep, against
     * scripts that declare a handful of locals each. The frame is 96 bytes now,
     * and a 2000-frame embedded-server run grows 4,032 bytes of locals in total
     * across every block it ever allocates.
     *
     * The footprint was the smaller half. The arrays were also *cleared* in full
     * on every push, and that is per-script work, not per-block: the same run
     * pushes 138,215 frames and was zeroing 1.71 GB to do it, against 3.16 MB
     * now — a mean of 22.9 bytes actually written per push. Nearly all of it was
     * rewriting zeros over zeros. Measured on that workload, interleaved against
     * a build with these buffers forced back to full size, it is 3.0% of the cs2
     * stage p50 (338.3 vs 348.7 us, lower in all five pairs).
     *
     * `dirty` tracks actual writes rather than the script's declared
     * `local_int_count`, and that is the safety of it: the opcodes that write a
     * local bound their index against CS2VM_MAX_LOCALS, not against the declared
     * count, so a script writing past what it declared would leave dirt above a
     * declared-count mark and the next occupant would read the previous
     * script's values instead of zero. Every write raises the mark and grows the
     * buffer, so no write can escape the next clear.
     *
     * Buffers stay with the block across a release/acquire round trip — that is
     * what keeps this from turning one memset into two allocations per push.
     * A warm pool reallocs only when a frame reaches deeper than any script that
     * has occupied it before.
     */
    int* int_locals;
    char** str_locals;
    int int_locals_cap;
    int str_locals_cap;
    int int_locals_dirty;
    int str_locals_dirty;

    int return_pc;
    int return_frame;

    int has_return;
    int return_int_count;
    int return_ints[8];
};

/*
 * Call depth. Recursive scripts are ordinary here — the sorts are written as
 * quicksorts that recurse once per partition (the world map's label sort 1491,
 * the spellbook's 2621) — so this is a real limit, not a paranoia bound.
 *
 * It was 50, on the belief that it matched the reference's Interpreter frame
 * array. The cache says otherwise: 2621 reaches depth 70 sorting the rev-239
 * standard spellbook, so 50 aborted it and the magic tab drew nothing but its
 * Filters button. 70 is not a ceiling either — the depth is O(spells) in the
 * worst case and the other books are larger — so this sits well above what was
 * measured rather than at it.
 *
 * Frames are fat (CS2VM_MAX_LOCALS ints plus string pointers, ~12 KB each), but
 * raising this no longer costs anything up front: the stack grows on demand, so
 * this is only the depth at which a runaway recursion is cut off. What it does
 * bound is the frame free list (CS2VM2_FRAME_POOL_MAX in cs2vm2.c).
 */
#define CS2VM_MAX_FRAMES 128
#define CS2VM_MAX_CYCLES 1000000
#define CS2VM2_MAX_ARRAYS 128
/*
 * The reference's own ceiling on a clientscript array (`define_array` throws
 * above it), and it has to be this number rather than a convenient one: 256
 * silently truncated the music tab's 852-row index array, so every
 * `cc_find($container, $rows($i))` past row 255 looked up index 0 and 596 of
 * the rows were built, never positioned, and never drawn. A cache script sized
 * by its data cannot be capped below what the reference allows.
 *
 * Cells are heap-backed and grown on demand (cs2vm2_array_reserve), so this is
 * a limit and not an allocation: the common two- and ten-element arrays still
 * cost two and ten slots.
 */
#define CS2VM2_ARRAY_CAPACITY 5000
/* Max array-cell writes a single opcode may make (the undo log only ever holds
 * the in-flight op's stores; it is reset at each op boundary). One bytecode op
 * writes at most one cell today, so this is generous headroom. */
#define CS2VM2_ARRAY_UNDO_MAX 64

/*
 * A script array. DEFINE_ARRAY carries the element type in the low half of its
 * operand, and it is not decoration: an array declared `s` lives on the STRING
 * stack, so PUSH_ARRAY_INT / POP_ARRAY_INT move string values for it. Ignoring
 * the type made every string-array write pop an int that was never pushed —
 * the music tab's list builder (script 9290) died on its first
 * `$names($i) = ""`.
 *
 * One slot is only ever one type, so the storage is a union; string cells hold
 * pool pointers (CS2VM2_StrDup), which the thread frees as a unit at script
 * start and never individually.
 */
struct CS2VM2_Array
{
    /*
     * One heap block of pointer-wide slots, viewed as either arm. Pointer-wide
     * for both is what lets cs2vm2_array_track read the int and the string cell
     * of the same index without knowing which arm is live, and it is what the
     * inline `union { int[N]; char*[N]; }` this replaced already did.
     *
     * `capacity` is what is allocated; `size` is the script-visible length and
     * is never above it. NULL cells with capacity 0 is a valid empty array —
     * every read is guarded by `defined && index < size`.
     */
    union
    {
        int* ints;
        char** strings;
    } cells;
    int capacity;
    int size;
    int defined;
    int is_string;
};

#define CS2VM2_CHILDREN_ITER_MAX 256

/* A yield rolls the VM back to the start of the opcode that yielded. Per the
 * CS2VM_EXECNO_YIELD contract (see above), a yielding host op must not have mutated
 * frame contents or pushed/popped frames — only operand-stack tops. So the checkpoint
 * only records the stack/frame pointers (plus the active/dot ids and, implicitly, the
 * pc which is reset on restore); the ~12 KB-per-frame `frames` array is never copied. */
struct CS2VM2_YieldCheckpoint
{
    int ints_stack_top;
    int strs_stack_top;
    int frame_sp;
    int active_component_id;
    int dot_component_id;
    int undo_log_len; /* undo_log length at op entry; on yield, roll back to here */
};

/*
 * One. Concurrency in this VM is one script per *block*, not one per thread
 * slot.
 *
 * This was 4, and the other three were dead capacity that every script paid
 * for twice. Nothing in the tree ever addressed `threads[1]` and up:
 * `CS2VM2_ThreadMain` and `CS2VM2_Run` both hand out `threads[0]`, and when a
 * script nests — awaits a load while another starts — the second one acquires
 * its own VM from the pool in cs2vm2.c, which is exactly why that pool is a
 * free list rather than a singleton. So the slots were never a scheduling
 * resource; they were three copies of a 128-array table and a string pool that
 * no code could reach.
 *
 * They were not free, because `CS2VM2_Init` and `CS2VM2_Free` both walk
 * `thread_count` and the pool parks torn-down blocks rather than warm VMs: every
 * script ran 4x the per-thread setup and teardown, three quarters of it over
 * state untouched since the identical pass before it. Measured on the rev-239
 * gameframe, the acquire/release round trip was 1,879 ns per script and 32.9% of
 * the whole CS2 stage.
 *
 * Raising it again means restoring that cost, so a real multi-thread model has
 * to make Init/Free track which slots were touched rather than walking all of
 * them. Within one thread that tracking has since been done — Free walks
 * `array_alloc` and a pooled block skips the array clear entirely — but it is
 * per-thread work, so it does not make the thread count free again.
 */
#define CS2VM2_MAX_THREADS 1
struct CS2VM2_Thread
{
    struct CS2VM2* vm;

    /*
     * Call stack, grown on demand. Held inline, this was 128 x 12,352 bytes =
     * 1.51 MB per thread and 6.03 MB of the 7.10 MB VM, reserved on every
     * acquire for a stack that is a handful of frames deep in the common case
     * (only the recursive sorts — the world map's label sort 1491, the
     * spellbook's 2621 — go deep, and 2621 peaks around 70).
     *
     * So the slots are pointers and the blocks come from a shared free list
     * (cs2vm2_frame_acquire), which means a depth-3 script touches 3 blocks
     * instead of reserving 128. frames_live is the high-water mark: slots below
     * it hold live blocks, slots at or above it are uninitialised and must not
     * be read. It is deliberately not lowered when frame_sp drops, so a
     * quicksort that recurses to depth 70 allocates each depth once rather than
     * once per partition.
     *
     * frames_live >= frame_sp always holds, so frames[0 .. frame_sp) are
     * non-NULL and the hot readers (CS2VM_FRAME and friends) need no check.
     */
    struct CS2VM2_Frame* frames[CS2VM_MAX_FRAMES];
    int frames_live;

    int ints_stack[CS2VM_STACK_MAX];
    int ints_stack_top;
    char* strs_stack[CS2VM_STACK_MAX];
    int strs_stack_top;

    int frame_sp;

    int active_component_id;
    int dot_component_id;

    /* Diagnostics for the opcode that last caused CS2VM_EXECNO_ERROR. */
    int last_error_opcode;
    int last_error_pc;
    int last_error_script_id;

    /* Tracks cooperative yields (halts) for the current opcode site; error if > 1. */
    int yield_halt_frame_sp;
    int yield_halt_script_id;
    int yield_halt_pc;
    int yield_halt_count;

    /* Cache-load retry identity belongs to the executing thread, not the
     * shared game host. Several Task_CS2Run instances can be parked at once;
     * a host-global marker lets one script erase another's completed wait and
     * makes the first script yield twice at the same opcode. */
    bool has_awaited;
    enum CS2VM_HostRequestKind awaited_kind;
    int awaited_id;
    int awaited_id2;

    /* Per-op undo log for VM-field mutations (currently array stores). A yielding
     * op must leave VM state untouched so its replay-on-resume is idempotent
     * (see CS2VM_EXECNO_YIELD); the pointer-only checkpoint covers the stacks and
     * frames, but an op that also mutates a persistent field (e.g. an array cell)
     * would leave that change applied when the op re-runs. Ops opt in by routing
     * such writes through CS2VM2_ArrayStore, which records the prior value here so
     * the yield restore can undo them. Reset at the start of every op, so it only
     * ever holds the in-flight op's mutations — no per-op copying of whole arrays. */
    struct CS2VM2_ArrayUndo
    {
        short slot;
        int index;
        int old_value;
        char* old_string;
    } undo_log[CS2VM2_ARRAY_UNDO_MAX];
    int undo_log_len;

    int children_iter_indices[CS2VM2_CHILDREN_ITER_MAX];
    int children_iter_count;
    int children_iter_index;
    /* Parent uid for the active children_iter_* (set by 203/205/212). Opcode
     * 213 (boolean find-next) resolves each index under this parent. */
    int children_iter_parent;

    /* Handle from the last IF_CHILDREN_COLLECT (211); CHILDREN_ARRAY (215)
     * pushes it. Raw pointer into arrays[], or NULL if none yet this run. */
    char* children_collect_handle;

    /* Array pool. At this revision an array is a first-class object whose
     * HANDLE lives in a string local: DEFINE_ARRAY's operand names the string
     * local to store the handle in (high half) and the element type (low
     * half), and PUSH/POP_ARRAY_INT's operand is that string-local index in
     * the CURRENT frame — so passing the string local to a gosub passes the
     * array (the spellbook sort receives its array as a proc argument this
     * way; a numbered-global model sorted a different, undefined array and
     * silently no-opped). Handles are raw pointers into this pool; array_alloc
     * is the bump allocator, reset per top-level run like the string pool. */
    struct CS2VM2_Array arrays[CS2VM2_MAX_ARRAYS];
    int array_alloc;

    /* Backing storage for every string this thread makes — stack strings, frame
     * string locals, and the strings handed to host requests. Released as a unit
     * when a script starts (CS2VM2_ResetRuntime), so no string on the stack or in
     * a local is ever individually freed. Allocate through CS2VM2_StrDup and
     * friends below, never strdup/malloc. */
    struct CS2VM2_StrPool str_pool;

    /* Host-provided canvas size for GETCANVASSIZE / viewport ops. */
    int canvas_w;
    int canvas_h;

    /* Host-provided window mode for GETWINDOWMODE / GETDEFAULTWINDOWMODE, in
     * the CS2 `windowmode` domain (see CS2VM_WINDOW_MODE_*). Snapshotted per
     * thread the same way the canvas is; the SET ops write the host through a
     * host request and update this so a script that sets then reads within one
     * run sees its own write. 0 = the host never said, treated as resizable. */
    int window_mode;
    int default_window_mode;
};

struct CS2VM2
{
    struct CS2VM2_Thread threads[CS2VM2_MAX_THREADS];
    int thread_count;

    CS2VM2_HostExec_Fn host_exec;
    void* user;
};

enum CS2VM2_ThreadStatus
{
    CS2VM2_THREAD_DONE,    /* script finished (maps EXECNO_DONE/OK) */
    CS2VM2_THREAD_YIELDED, /* host work pending (maps EXECNO_YIELD) */
    CS2VM2_THREAD_ERROR,   /* runtime error (maps EXECNO_ERROR/other) */
};

struct CS2VM2_ThreadError
{
    int opcode;
    int pc;
    int script_id;
};

/* Trace controls (0=off, 1=targeting ops, 2=all). */
extern int g_cs2_trace_mode;
extern char g_cs2_trace_extra[512];

/**
 * One executed instruction, for comparing this VM against another.
 *
 * The stderr trace above is for reading; this is for diffing. The fields are
 * the ones the official client's interpreter can also be made to report
 * (Deobfuscator/instr/src/CS2Trace.java) and the ones tools/cs2_parity's
 * artifact schema already names, so a record here lines up with a record there
 * without either side being reinterpreted.
 *
 * State is sampled *after* the instruction runs, which is what makes a
 * divergence point at the instruction that caused it rather than the one after.
 */
struct CS2VM2_TraceRecord
{
    int script_id;
    int pc;
    int opcode;
    int operand;
    int ints_top;
    int strs_top;
    int top_int;
};

/**
 * Record the next `capacity` instructions into `out`.
 *
 * Independent of `g_cs2_trace_mode`: capturing is for a harness, printing is
 * for a person, and a harness that had to turn on the stderr trace to collect
 * anything would drown in it. Passing NULL stops capture.
 */
void
CS2VM2_TraceCaptureBegin(struct CS2VM2_TraceRecord* out, int capacity);

/** How many records were written; capture stops. */
int
CS2VM2_TraceCaptureEnd(void);

void
CS2VM2_Init(struct CS2VM2* vm);

void
CS2VM2_Free(struct CS2VM2* vm);

/* Heap-allocated VMs come from a free list rather than malloc: struct CS2VM2 is
 * ~1.07 MB (4 threads x arrays[128] plus the stacks; measured, do not trust this
 * number after changing the limits above), and the client starts a script often
 * enough that malloc/free of that block was the single largest source of
 * allocator traffic in a boot — a gigabyte of it, interleaved with the small
 * long-lived allocations that fragment a wasm heap.
 *
 * Acquire returns an Init'd VM; Release runs the CS2VM2_Free teardown and parks
 * the block. Recycling a block is exactly as safe as a fresh malloc:
 * cs2vm2_thread_init is written against uninitialised memory (see its comment).
 * A block that comes back out of the pool skips that wide clear, because Free
 * left the array table clean — see cs2vm2_init_warm. Drain releases the parked
 * blocks and the pooled call frames — call it at shutdown or when trimming. */
struct CS2VM2*
CS2VM2_Acquire(void);

void
CS2VM2_Release(struct CS2VM2* vm);

void
CS2VM2_PoolDrain(void);

void
CS2VM2_BindHost(
    struct CS2VM2* vm,
    void* user,
    CS2VM2_HostExec_Fn host_exec);

void
CS2VM2_Run(struct CS2VM2* vm);

int
CS2VM2_DotOrActiveComponentId(
    struct CS2VM2_Thread* thread,
    int operand);

void
CS2VM2_SetTargetComponentId(
    struct CS2VM2_Thread* thread,
    int operand,
    int component_id);

void
CS2VM2_SetTraceExtra(
    char const* fmt,
    ...);

void
CS2VM2_ResetChildrenIter(struct CS2VM2_Thread* thread);

int
CS2VM2_PopInt(
    struct CS2VM2_Thread* thread,
    int* operand);

int
CS2VM2_PushInt(
    struct CS2VM2_Thread* thread,
    int operand);

int
CS2VM2_PopStr(
    struct CS2VM2_Thread* thread,
    char** operand);

/*
 * Push a string the thread's pool allocated (CS2VM2_StrDup / _StrFmt / _StrAlloc
 * below, or one that came off this thread's stack). Ownership stays with the
 * pool: the push does not take it, and popping a string does not hand it over —
 * a popped string must not be freed, and may be pushed again as-is. Everything
 * the thread allocated dies together when the next script starts.
 */
int
CS2VM2_PushStr(
    struct CS2VM2_Thread* thread,
    char* operand);

/*
 * Thread string allocation. Use these instead of strdup/malloc for any string
 * that reaches the operand stack, a frame local, or a host request; see
 * cs2vm2_strpool.h for the lifetime rules.
 */

/* Writable buffer for `len` characters, NUL-terminated at [len]; contents
 * uninitialised (the caller fills them). */
char*
CS2VM2_StrAlloc(
    struct CS2VM2_Thread* thread,
    size_t len);

/* Pool copy of `text`; NULL in, NULL out. */
char*
CS2VM2_StrDup(
    struct CS2VM2_Thread* thread,
    char const* text);

/* Pool copy of exactly `len` bytes of `text`, then NUL. */
char*
CS2VM2_StrDupLen(
    struct CS2VM2_Thread* thread,
    char const* text,
    size_t len);

/* printf into the pool, sized to fit. */
char*
CS2VM2_StrFmt(
    struct CS2VM2_Thread* thread,
    char const* fmt,
    ...);

/* A fresh, writable "" (distinct storage per call — in-place opcodes such as
 * UPPERCASE mean no two stack slots may alias). */
char*
CS2VM2_StrEmpty(struct CS2VM2_Thread* thread);

int
CS2VM2_PushCallScript(
    struct CS2VM2_Thread* thread,
    struct CS2VM2_Script* script);

int
CS2VM2_SetActiveAndDotComponentId(
    struct CS2VM2_Thread* thread,
    int component_id);

int
CS2VM2_SetIntCurrentFrameLocal(
    struct CS2VM2_Thread* thread,
    int local,
    int value);

int
CS2VM2_SetStringCurrentFrameLocal(
    struct CS2VM2_Thread* thread,
    int local,
    char const* value);

int
CS2VM2_RunScript(struct CS2VM2_Thread* thread);

void
CS2VM2_ResetRuntime(struct CS2VM2_Thread* thread);

void
CS2VM2_ClearYieldHalt(struct CS2VM2_Thread* thread);

/* Tracked array store — writes arrays[slot][index] = value and records the prior
 * value in the per-op undo log so a yield restore undoes it (see undo_log). */
void
CS2VM2_ArrayStore(
    struct CS2VM2_Thread* thread, struct CS2VM2_Array* array, int index, int value);

/** The same, for an array declared with string elements. `value` is a pool
 *  pointer (CS2VM2_StrDup); the cell borrows it and never frees. */
void
CS2VM2_ArrayStoreStr(
    struct CS2VM2_Thread* thread, struct CS2VM2_Array* array, int index, char* value);

struct CS2VM2_Thread*
CS2VM2_ThreadMain(struct CS2VM2* vm);

void
CS2VM2_ThreadSetCanvas(
    struct CS2VM2_Thread* thread,
    int w,
    int h);

void
CS2VM2_ThreadSetWindowMode(
    struct CS2VM2_Thread* thread,
    int mode,
    int default_mode);

int
CS2VM2_ThreadStart(
    struct CS2VM2_Thread* thread,
    struct CS2VM2_Script* script);

enum CS2VM2_ThreadStatus
CS2VM2_ThreadResume(
    struct CS2VM2_Thread* thread,
    struct CS2VM2_ThreadError* err_out);

enum CS2VM2_ThreadStatus
CS2VM2_ThreadRun(
    struct CS2VM2_Thread* thread,
    struct CS2VM2_ThreadError* err_out);

#endif /* CS2VM2_H */
