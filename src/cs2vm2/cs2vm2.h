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
    int int_locals[CS2VM_MAX_LOCALS];
    char* str_locals[CS2VM_MAX_LOCALS];

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
#define CS2VM2_ARRAY_CAPACITY 256
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
    union
    {
        int ints[CS2VM2_ARRAY_CAPACITY];
        char* strings[CS2VM2_ARRAY_CAPACITY];
    } cells;
    int size;
    int defined;
    int is_string;
};

#define CS2VM2_CHILDREN_ITER_MAX 256

/* Opcodes missing from cs2_opcode.h but used by gameframe scripts. */
#define CS2_OP_CC_CREATECHILD 106
#define CS2_OP_CC_CREATESIBLING 107
#define CS2_OP_CC_FINDROOT 202
#define CS2_OP_CC_CHILDREN_FIND 203
#define CS2_OP_CC_CHILDREN_FINDNEXTID 204
#define CS2_OP_IF_CHILDREN_FIND 205
#define CS2_OP_IF_CHILDREN_FINDNEXTID 206
/* Skill-guide Overview (9150..9199): children-find variant that also pushes
 * the match count (call-site (1i->1i) in scripts 9179/9186). */
#define CS2_OP_CC_CHILDREN_FIND_COUNT 212
/* Boolean children find-next after 212/203: set active/dot child, push 1/0.
 * Distinct from FINDNEXTID (204/206), which pushes the child sub-id. Script
 * 9179 arms Overview/Quest XP cc_setonop via this loop. */
#define CS2_OP_CC_CHILDREN_FINDNEXT 213
#define CS2_OP__213 CS2_OP_CC_CHILDREN_FINDNEXT
/* IF_CHILDREN_COLLECT (211) + CHILDREN_ARRAY (215): script 9181 gathers
 * overview_tabs child subids into an int-array handle, then walks it to
 * if_sethide the non-selected Overview content panel. */
#define CS2_OP_IF_CHILDREN_COLLECT 211
#define CS2_OP_CHILDREN_ARRAY 215
/* Array-handle family used by the Overview widget library. Names from xrsps
 * where it has them; 8012/8023 are call-site arities only. */
#define CS2_OP_ARRAY_LENGTH 8003
#define CS2_OP_ARRAY_SPLIT 8018
#define CS2_OP_ARRAY_JOIN 8019
#define CS2_OP_ARRAY_NEW 8022
#define CS2_OP_ARRAY_SETLENGTH 8023
#define CS2_OP_ARRAY_APPEND 8024
#define CS2_OP_STRING_TO_INT 4036
#define CS2_OP_CC_SETGRAPHIC2 1122
#define CS2_OP_IF_SETGRAPHIC2 2122
#define CS2_OP_CC_SETTRANSBOT 1124
#define CS2_OP_CC_SETFILLMODE 1125
#define CS2_OP_CC_SETARC 1128
#define CS2_OP_CC_SETPINCH 1004
#define CS2_OP_CC_SETPLAYERMODEL_SELF 1203
#define CS2_OP_CC_SETMODEL_PLAYERCHATHEAD 1204
#define CS2_OP_IF_SETTRANSBOT 2124
#define CS2_OP_IF_SETFILLMODE 2125
#define CS2_OP_IF_SETARC 2128
#define CS2_OP_IF_SETCLICKMASK 2308
#define CS2_OP_IF_SETPINCH 2004
#define CS2_OP_IF_SETMODEL_PLAYERCHATHEAD 2203
/* HIGHLIGHT_NPC_* entity-highlight family (7000..7004); 7001/7002 are named in
 * the generated meta, the rest are placeholders there. */
#define CS2_OP_HIGHLIGHT_NPC_SETUP 7000
#define CS2_OP_HIGHLIGHT_NPC_GET 7003
#define CS2_OP_HIGHLIGHT_NPC_CLEAR 7004
/* HIGHLIGHT_LOC_* scene-object-highlight family (7011..7014); ON/OFF are named
 * in cs2_opcode.h, GET/CLEAR are placeholders there. Same shape as the NPC
 * family but keyed on (locTypeId, coordPacked, slot, group) instead of an NPC
 * slot, so ON/OFF/GET pop 4 (NPC pops 3). */
#define CS2_OP_HIGHLIGHT_LOC_GET 7013
#define CS2_OP_HIGHLIGHT_LOC_CLEAR 7014
/* HIGHLIGHT_OBJ_* ground-item-highlight family (7021..7025); ON/OFF are named in
 * cs2_opcode.h, GET/SETUP are placeholders there. OBJ ON/OFF/GET pop 4 like the
 * LOC family; 7025 is the OBJTYPE setup (pop 5). */
#define CS2_OP_HIGHLIGHT_OBJ_GET 7023
#define CS2_OP_HIGHLIGHT_OBJTYPE_SETUP 7025
/* MINIMENU_* mouseover / right-click-menu queries (7100..7110). All are no-arg
 * getters reading the current hovered target + open menu state; the host answers
 * them (CS2VM_HOST_REQUEST_MINIMENU). cs2_opcode.h has these as _71xx. */
#define CS2_OP_MINIMENU_TYPE 7100
#define CS2_OP_MINIMENU_ENTRY 7101
#define CS2_OP_MINIMENU_FINDNPC 7102
#define CS2_OP_MINIMENU_FINDLOC 7103
#define CS2_OP_MINIMENU_FINDOBJ 7104
#define CS2_OP_MINIMENU_FINDPLAYER 7105
#define CS2_OP_MINIMENU_ISOPEN 7108
#define CS2_OP_MINIMENU_FINDCOMPONENT 7109
#define CS2_OP_MINIMENU_NUMOPS 7110
/* Client / game / device option get/set (3209..3217). Unlike the direct volume
 * setters (3203..3208, which carry just a value), these are keyed by an option
 * id: SET pops (id, value), GET pops (id) -> value, GETRANGE pops (id) -> min,
 * max. The host answers them (CS2VM_HOST_REQUEST_CLIENT_OPTION). The volume ops
 * are already named in cs2_opcode.h; these _32xx are placeholders there. */
#define CS2_OP_CLIENTOPTION_SET 3209
#define CS2_OP_CLIENTOPTION_GET 3210
#define CS2_OP_DEVICEOPTION_SET 3212
#define CS2_OP_GAMEOPTION_SET 3213
#define CS2_OP_DEVICEOPTION_GET 3214
#define CS2_OP_GAMEOPTION_GET 3215
#define CS2_OP_DEVICEOPTION_GETRANGE 3217
/* Minimap zoom controls (7250..7254). Setters pop one value; GETZOOM pushes the
 * current zoom (2..8). The host owns the zoom (CS2VM_HOST_REQUEST_MINIMAP) so a
 * SETZOOM round-trips through GETZOOM. 7250 is also known as SETMINIMAPLOCK in
 * cs2_opcode.h (same opcode, both just pop a flag); 7253/7254 are past the meta
 * table there and decode via its default entry. */
#define CS2_OP_MINIMAP_SETZOOMABLE 7250
#define CS2_OP_MINIMAP_SETZOOM 7252
#define CS2_OP_MINIMAP_GETZOOM 7253
#define CS2_OP_MINIMAP_SETICONZOOMLIMIT 7254
/* UI zoom controls (6210..6214; 6213 unconfirmed, left out). SET/GET/RESET are
 * host-owned state (like MINIMAP zoom); GETDEFAULT answers a fixed constant
 * without touching that state. cs2_opcode.h has 6210/6212 as untyped
 * placeholders and no entry at all for 6211/6214. */
#define CS2_OP_UIZOOM_SET 6210
#define CS2_OP_UIZOOM_GET 6211
#define CS2_OP_UIZOOM_RESET 6212
#define CS2_OP_UIZOOM_GETDEFAULT 6214
/* Safe-area bounds (6220..6223, plus the 6231 alternate MAXY). Desktop client,
 * no notch/home-indicator: MINX/MINY are 0, MAXX/MAXY are the live canvas size
 * (same source as GETCANVASSIZE / VIEWPORT_GETEFFECTIVESIZE). cs2_opcode.h has
 * 6220..6223 as untyped placeholders and no entry at all for 6231. */
#define CS2_OP_SAFEAREA_GETMINX 6220
#define CS2_OP_SAFEAREA_GETMINY 6221
#define CS2_OP_SAFEAREA_GETMAXX 6222
#define CS2_OP_SAFEAREA_GETMAXY 6223
#define CS2_OP_SAFEAREA_GETMAXY_ALT 6231
/* Camera yaw getter. No setter in this range and no live link from RS_CS2Host
 * to the render-side camera yet (same situation as CAM_*FOLLOWHEIGHT), so this
 * is a host-owned value with no current writer — see RS_CS2Host.cam_yaw. Not in
 * cs2_opcode.h at all (no placeholder define existed for 6232). */
#define CS2_OP_CAM_GETYAW 6232
/* OC_* obj-config getters with no cs2_opcode.h define at all (the 4213..4221
 * gap, plus 4222). Same INT8-baked-operand convention as the rest of the OC_
 * family (4200..4212, all CS2_OPERAND_INT8 in the generated meta). None of these
 * have backing data on ToriRS_Objtype yet except EXAMINE (desc field, already
 * real) — see the host handlers for what each stubs to. */
#define CS2_OP_OC_SHIFTCLICKIOP 4213
#define CS2_OP_OC_WEARPOS 4214
#define CS2_OP_OC_WEARPOS2 4215
#define CS2_OP_OC_WEARPOS3 4216
#define CS2_OP_OC_WEIGHT 4217
#define CS2_OP_OC_EXAMINE 4218
/* oc_isubop(obj, opIndex, subIndex) -> string. No sub-menu nesting exists on
 * ToriRS_Objtype (only flat inv_actions/ground_actions), so this stubs to "". */
#define CS2_OP_OC_ISUBOP 4222
/* CLIENTOP_* (6700..6709): enhanced client-side context-menu hooks that install
 * or remove transient client-owned ops (Tag, Lookup, Mark tile, etc.) on an
 * entity/tile slot. SET pops (slot, scriptId) + string label; DEL pops slot.
 * The host stubs them for now (no enhanced menu entries yet) so scripts that
 * wire interface state do not abort. cs2_opcode.h has these as _67xx. */
#define CS2_OP_CLIENTOP_NPC_SET 6700
#define CS2_OP_CLIENTOP_NPC_DEL 6701
#define CS2_OP_CLIENTOP_LOC_SET 6702
#define CS2_OP_CLIENTOP_LOC_DEL 6703
#define CS2_OP_CLIENTOP_OBJ_SET 6704
#define CS2_OP_CLIENTOP_OBJ_DEL 6705
#define CS2_OP_CLIENTOP_PLAYER_SET 6706
#define CS2_OP_CLIENTOP_PLAYER_DEL 6707
#define CS2_OP_CLIENTOP_TILE_SET 6708
#define CS2_OP_CLIENTOP_TILE_DEL 6709
/*
 * Input-field (widget type 16) configuration. These were previously numbered
 * 7200-7212, which is outside the real opcode space -- no cache script could
 * ever reach those handlers, while the real opcodes sat in the table as
 * "_unknown" with {0,0,0,0} stack metadata and desynced the operand stack.
 * SETACCEPTMODE was also missing entirely, which shifted every entry after
 * SETSELECTCOLOUR by one. Numbering per the reference (Opcodes.ts:119-132).
 */
#define CS2_OP_CC_INPUT_SETSUBMITMODE 1133
#define CS2_OP_CC_INPUT_SETSELECTCOLOUR 1134
#define CS2_OP_CC_INPUT_SETACCEPTMODE 1135
#define CS2_OP_CC_INPUT_SETWRAPMODE 1136
#define CS2_OP_CC_INPUT_SETLINEWRAPPINGWIDTH 1137
#define CS2_OP_CC_INPUT_SETSELECTBGCOLOUR 1138
#define CS2_OP_CC_INPUT_SETLINECOUNTLIMIT 1139
#define CS2_OP_CC_INPUT_SETCURSORCOLOUR 1140
#define CS2_OP_CC_INPUT_SETCURSORTRANS 1141
#define CS2_OP_CC_INPUT_SETCURSORWIDTH 1142
#define CS2_OP_CC_INPUT_SETCURSORHEIGHT 1143
#define CS2_OP_CC_INPUT_SETCURSOROFFSET 1144
#define CS2_OP_CC_INPUT_SETLINEWIDTHLIMIT 1145
#define CS2_OP_CC_INPUT_SETCHARFILTER 1146

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

#define CS2VM2_MAX_THREADS 4
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
 * Acquire returns an Init'd VM; Release runs the same CS2VM2_Free teardown and
 * parks the block. Recycling a block is exactly as safe as a fresh malloc:
 * cs2vm2_thread_init is written against uninitialised memory (see its comment).
 * Drain releases the parked blocks and the pooled call frames — call it at
 * shutdown or when trimming. */
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
