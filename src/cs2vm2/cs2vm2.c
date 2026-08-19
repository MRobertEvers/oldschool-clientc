/*
 * CS2VM2 — yield-capable ClientScript 2 bytecode interpreter.
 * Ported from v1/vm/cs2vmx (CS2VMX_*). Host ops go through CS2VM2_HostExec_Fn.
 */

#include "cs2vm2.h"

#include "cs2_opcode.h"
#include "cs2_opcode_meta.h"
#include "cs2vm2_opcode_stack.gen.h"
#include "perf/torirs_perf.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CS2VM2_DEBUG_OPS 0

int g_cs2_trace_mode = 0;
char g_cs2_trace_extra[512];

/* --- Call frame pool ----------------------------------------------------- */
/*
 * Call frames (12 KB each) are uniform blocks with no identity, so the threads
 * share one free list instead of each reserving CS2VM_MAX_FRAMES of them inline.
 * A thread pops blocks as its stack grows and returns them all on teardown.
 *
 * The cap bounds what is retained across teardowns. One full stack's worth is
 * the useful size: it covers the deepest recursion the cache actually contains
 * (script 2621 sorting the standard spellbook, ~70) without re-mallocing, and
 * costs the same 1.51 MB that a single thread used to reserve unconditionally.
 */
#define CS2VM2_FRAME_POOL_MAX CS2VM_MAX_FRAMES

static struct CS2VM2_Frame* g_frame_pool[CS2VM2_FRAME_POOL_MAX];
static int g_frame_pool_count;

/* Contents are left as-is; every caller memsets the frame before use. */
static struct CS2VM2_Frame*
cs2vm2_frame_acquire(void)
{
    if( g_frame_pool_count > 0 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_FRAME_POOL_HIT, 1);
        return g_frame_pool[--g_frame_pool_count];
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_FRAME_POOL_MISS, 1);
    return (struct CS2VM2_Frame*)malloc(sizeof(struct CS2VM2_Frame));
}

static void
cs2vm2_frame_release(struct CS2VM2_Frame* frame)
{
    assert(frame);
    if( g_frame_pool_count < CS2VM2_FRAME_POOL_MAX )
        g_frame_pool[g_frame_pool_count++] = frame;
    else
        free(frame);
}

/*
 * The frame at `depth`, allocated if the stack has not reached that far yet.
 * Only ever called with depth == frames_live (push is one at a time), so this
 * grows by one; the loop is there so the invariant does not depend on that.
 */
static struct CS2VM2_Frame*
cs2vm2_thread_frame_grow(
    struct CS2VM2_Thread* thread,
    int depth)
{
    assert(thread);
    assert(depth >= 0);
    assert(depth < CS2VM_MAX_FRAMES);

    while( thread->frames_live <= depth )
    {
        struct CS2VM2_Frame* frame = cs2vm2_frame_acquire();
        if( !frame )
            return NULL;
        thread->frames[thread->frames_live++] = frame;
    }
    return thread->frames[depth];
}

/* Hand every block this thread holds back to the free list. Safe to repeat. */
static void
cs2vm2_thread_frames_release(struct CS2VM2_Thread* thread)
{
    assert(thread);
    for( int i = 0; i < thread->frames_live; i++ )
        cs2vm2_frame_release(thread->frames[i]);
    thread->frames_live = 0;
}

static void
CS2VM2_ClearTraceExtra(void)
{
    g_cs2_trace_extra[0] = '\0';
}

void
CS2VM2_SetTraceExtra(
    char const* fmt,
    ...)
{
    assert(fmt);
    if( !g_cs2_trace_mode )
        return;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_cs2_trace_extra, sizeof(g_cs2_trace_extra), fmt, ap);
    va_end(ap);
}

static void
CS2VM2_SaveYieldCheckpoint(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_YieldCheckpoint* cp)
{
    assert(vm);
    assert(cp);

    cp->ints_stack_top = vm->ints_stack_top;
    cp->strs_stack_top = vm->strs_stack_top;
    cp->frame_sp = vm->frame_sp;
    cp->active_component_id = vm->active_component_id;
    cp->dot_component_id = vm->dot_component_id;
    cp->undo_log_len = vm->undo_log_len;
    /* No frame copy: a yielding op leaves frame contents untouched (see the
     * CS2VM_EXECNO_YIELD contract), so restore is a pure pointer rollback. */
}

static void
CS2VM2_RestoreYieldCheckpoint(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_YieldCheckpoint const* cp,
    int op_pc)
{
    assert(vm);
    assert(cp);
    assert(cp->frame_sp > 0);

    /* Defensive: if a (contract-violating) op grew the frame stack before yielding,
     * clear the now-abandoned frames. Under the invariant frame_sp is unchanged and
     * this loop does nothing. Frame contents at/below cp->frame_sp are untouched by a
     * yielding op, so no frame copy is needed — only the pointers and pc are rolled back. */
    assert(vm->frame_sp <= vm->frames_live);
    for( int i = cp->frame_sp; i < vm->frame_sp; i++ )
        memset(vm->frames[i], 0, sizeof(struct CS2VM2_Frame));

    vm->ints_stack_top = cp->ints_stack_top;
    vm->strs_stack_top = cp->strs_stack_top;
    vm->frame_sp = cp->frame_sp;
    vm->active_component_id = cp->active_component_id;
    vm->dot_component_id = cp->dot_component_id;
    vm->frames[vm->frame_sp - 1]->pc = op_pc;

    /* Undo any opt-in VM-field mutations the yielding op applied before it
     * yielded, newest first, so the op re-runs from the same clean state as the
     * stacks/frames. Ops that never mutate persistent fields append nothing and
     * pay nothing here. */
    while( vm->undo_log_len > cp->undo_log_len )
    {
        struct CS2VM2_ArrayUndo const* undo = &vm->undo_log[--vm->undo_log_len];
        if( undo->slot < 0 || undo->slot >= CS2VM2_MAX_ARRAYS || undo->index < 0 ||
            undo->index >= vm->arrays[undo->slot].capacity )
            continue;
        if( vm->arrays[undo->slot].is_string )
            vm->arrays[undo->slot].cells.strings[undo->index] = undo->old_string;
        else
            vm->arrays[undo->slot].cells.ints[undo->index] = undo->old_value;
    }
}

/*
 * Make sure `array` can hold `count` cells, growing geometrically and keeping
 * whatever it already had. Slots are pointer-wide whichever arm is live (see
 * struct CS2VM2_Array), so one size serves both.
 *
 * Returns 0 when the block could not be grown; every caller then leaves the
 * array at size 0, which reads as an empty array rather than a wild write.
 * Blocks are kept across script runs — a rebuild that defines the same-sized
 * array every time allocates once.
 */
static int
cs2vm2_array_reserve(
    struct CS2VM2_Array* array,
    int count)
{
    if( count <= array->capacity )
        return 1;
    if( count > CS2VM2_ARRAY_CAPACITY )
        return 0;

    int cap = array->capacity > 0 ? array->capacity : 16;
    while( cap < count )
        cap <<= 1;
    if( cap > CS2VM2_ARRAY_CAPACITY )
        cap = CS2VM2_ARRAY_CAPACITY;

    void* cells = realloc(array->cells.strings, (size_t)cap * sizeof(char*));
    assert(cells);
    array->cells.strings = (char**)cells;
    array->capacity = cap;
    return 1;
}

/* Take a pool slot and give it `size` usable cells. Returns 0 (with the array
 * left defined but empty) when the storage could not be had. */
static int
cs2vm2_array_begin(
    struct CS2VM2_Array* array,
    int size,
    int is_string)
{
    array->defined = 1;
    array->is_string = is_string;
    array->size = 0;
    if( size < 0 )
        size = 0;
    if( size > CS2VM2_ARRAY_CAPACITY )
        size = CS2VM2_ARRAY_CAPACITY;
    if( !cs2vm2_array_reserve(array, size) )
        return 0;
    array->size = size;
    return 1;
}

/* An array HANDLE is a raw pointer into vm->arrays carried in a string local
 * (and across the string stack / gosub args, which copy the char* verbatim).
 * Resolution validates rather than trusts: anything that is not a live,
 * aligned pool pointer — a real string, NULL, a stale local — resolves to no
 * array, and the op degrades to the same read-0/drop-write the old
 * out-of-range path had. */
static struct CS2VM2_Array*
cs2vm2_array_from_handle(struct CS2VM2_Thread* vm, char* handle)
{
    ptrdiff_t off = handle - (char*)vm->arrays;
    if( off < 0 || off >= (ptrdiff_t)sizeof(vm->arrays) )
        return NULL;
    assert(handle);
    if( off % (ptrdiff_t)sizeof(struct CS2VM2_Array) != 0 )
        return NULL;
    struct CS2VM2_Array* array = (struct CS2VM2_Array*)handle;
    return array->defined ? array : NULL;
}

/* PUSH/POP_ARRAY_INT's operand: the string-local slot of the CURRENT frame
 * holding the handle. */
static struct CS2VM2_Array*
cs2vm2_array_local(struct CS2VM2_Thread* vm, struct CS2VM2_Frame* frame, int slot)
{
    if( slot < 0 || slot >= CS2VM_MAX_LOCALS )
        return NULL;
    return cs2vm2_array_from_handle(vm, frame->str_locals[slot]);
}

/* Opt-in tracked array store: records the prior value so a yield restore can undo
 * it (see the undo_log contract in CS2VM2_Thread). Used by array-writing opcodes
 * whose replay-on-yield would otherwise double-apply. */
static int
cs2vm2_array_track(struct CS2VM2_Thread* vm, struct CS2VM2_Array* array, int index)
{
    assert(array);
    if( !array->defined || index < 0 || index >= array->size )
        return 0;
    if( vm->undo_log_len < CS2VM2_ARRAY_UNDO_MAX )
    {
        struct CS2VM2_ArrayUndo* undo = &vm->undo_log[vm->undo_log_len++];
        undo->slot = (short)(array - vm->arrays);
        undo->index = index;
        undo->old_value = array->cells.ints[index];
        undo->old_string = array->cells.strings[index];
    }
    return 1;
}

void
CS2VM2_ArrayStore(struct CS2VM2_Thread* vm, struct CS2VM2_Array* array, int index, int value)
{
    assert(vm);
    if( !cs2vm2_array_track(vm, array, index) )
        return;
    array->cells.ints[index] = value;
}

/* The string twin. The pointer is pool-owned (CS2VM2_StrDup); storing it here
 * transfers no ownership and the overwritten cell is never freed. */
void
CS2VM2_ArrayStoreStr(struct CS2VM2_Thread* vm, struct CS2VM2_Array* array, int index, char* value)
{
    assert(vm);
    if( !cs2vm2_array_track(vm, array, index) )
        return;
    array->cells.strings[index] = value;
}

void
CS2VM2_ClearYieldHalt(struct CS2VM2_Thread* vm)
{
    assert(vm);
    vm->yield_halt_frame_sp = 0;
    vm->yield_halt_script_id = 0;
    vm->yield_halt_pc = -1;
    vm->yield_halt_count = 0;
}

/**
 * One opcode, one yield: the host must load everything a request needs in that
 * single round-trip and complete on the retry (with defaults if a load failed).
 * A second yield at the same site means a host handler broke that contract.
 */
static bool
CS2VM2_CheckYieldHalt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int op_pc,
    int opcode)
{
    assert(vm);
    assert(frame);

    int script_id = frame->script->script_id;

    if( vm->yield_halt_frame_sp == vm->frame_sp && vm->yield_halt_script_id == script_id &&
        vm->yield_halt_pc == op_pc )
        vm->yield_halt_count++;
    else
    {
        vm->yield_halt_frame_sp = vm->frame_sp;
        vm->yield_halt_script_id = script_id;
        vm->yield_halt_pc = op_pc;
        vm->yield_halt_count = 1;
    }

    if( vm->yield_halt_count > 1 )
    {
        fprintf(
            stderr,
            "CS2VM: opcode %d yielded more than once at script=%d pc=%d frame=%d\n",
            opcode,
            script_id,
            op_pc,
            vm->frame_sp);
        vm->last_error_opcode = opcode;
        vm->last_error_pc = op_pc;
        vm->last_error_script_id = script_id;
        assert(0 && "host yielded twice for one opcode");
        return false;
    }
    return true;
}

static bool
CS2VM2_IsTargetingOpcode(int opcode)
{
    switch( opcode )
    {
    case CS2_OP_IF_FIND:
    case CS2_OP_CC_FIND:
    case CS2_OP_CC_FINDROOT:
    case CS2_OP_CC_CHILDREN_FIND:
    case CS2_OP_CC_CHILDREN_FIND_COUNT:
    case CS2_OP_CC_CHILDREN_FINDNEXTID:
    case CS2_OP__213:
    case CS2_OP_IF_CHILDREN_FIND:
    case CS2_OP_IF_CHILDREN_FINDNEXTID:
    case CS2_OP_IF_CHILDREN_COLLECT:
    case CS2_OP_CC_CREATE:
    case CS2_OP_CC_COPY:
    case CS2_OP_CC_CREATECHILD:
    case CS2_OP_CC_CREATESIBLING:
    case CS2_OP_IF_SETOP:
    case CS2_OP_IF_SETOPBASE:
    case CS2_OP_CC_SETOP:
        return true;
    default:
        return false;
    }
}

static struct CS2VM2_TraceRecord* g_cs2_trace_capture = NULL;
static int g_cs2_trace_capacity = 0;
static int g_cs2_trace_count = 0;

void
CS2VM2_TraceCaptureBegin(struct CS2VM2_TraceRecord* out, int capacity)
{
    g_cs2_trace_capture = out;
    g_cs2_trace_capacity = out ? capacity : 0;
    g_cs2_trace_count = 0;
}

int
CS2VM2_TraceCaptureEnd(void)
{
    int count = g_cs2_trace_count;
    g_cs2_trace_capture = NULL;
    g_cs2_trace_capacity = 0;
    g_cs2_trace_count = 0;
    return count;
}

static void
CS2VM2_TraceOpcode(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int op_pc,
    int opcode,
    int operand,
    int result)
{
    if( g_cs2_trace_capture && g_cs2_trace_count < g_cs2_trace_capacity )
    {
        struct CS2VM2_TraceRecord* record = &g_cs2_trace_capture[g_cs2_trace_count++];
        record->script_id = frame->script ? frame->script->script_id : -1;
        record->pc = op_pc;
        record->opcode = opcode;
        record->operand = operand;
        record->ints_top = vm->ints_stack_top;
        record->strs_top = vm->strs_stack_top;
        record->top_int = vm->ints_stack_top > 0 ? vm->ints_stack[vm->ints_stack_top - 1] : 0;
    }

    if( !g_cs2_trace_mode )
        return;
    /* TORIRS_CS2_TRACE_SCRIPT=<id>: trace ONE script. A whole-VM trace costs
     * enough wall clock that a headless run's scripted commands land before
     * login finishes, so "trace the boss HUD" was not reachable without it. */
    {
        static char const* only = NULL;
        static int only_id = -2;
        if( only_id == -2 )
        {
            only = getenv("TORIRS_CS2_TRACE_SCRIPT");
            only_id = only ? (int)strtol(only, NULL, 0) : -1;
        }
        if( only_id >= 0 && frame->script &&
            frame->script->script_id != only_id )
            return;
    }
#if !CS2VM2_DEBUG_OPS
    if( g_cs2_trace_mode == 1 && !CS2VM2_IsTargetingOpcode(opcode) && result == CS2VM_EXECNO_OK )
        return;
#endif

    char const* op_name = CS2_OpCode_String(opcode);
    fprintf(
        stderr,
        "CS2TRACE script=%d pc=%d op=%s(%d) intOp=%d istack=%d sstack=%d aw=0x%08x dw=0x%08x",
        frame->script->script_id,
        op_pc,
        op_name ? op_name : "_unknown",
        opcode,
        operand,
        vm->ints_stack_top,
        vm->strs_stack_top,
        (unsigned)vm->active_component_id,
        (unsigned)vm->dot_component_id);
    if( g_cs2_trace_extra[0] != '\0' )
        fprintf(stderr, " %s", g_cs2_trace_extra);
    /* TEMP DEBUG: top-of-stack values for parity diffing (mode 2 only). */
    if( g_cs2_trace_mode == 2 )
    {
        if( vm->ints_stack_top > 0 )
            fprintf(stderr, " itop=%d", vm->ints_stack[vm->ints_stack_top - 1]);
        if( vm->strs_stack_top > 0 )
            fprintf(
                stderr,
                " stop=\"%s\"",
                vm->strs_stack[vm->strs_stack_top - 1] ? vm->strs_stack[vm->strs_stack_top - 1]
                                                       : "(null)");
    }
    if( result == CS2VM_EXECNO_YIELD )
        fprintf(stderr, " result=yield");
    else if( result != CS2VM_EXECNO_OK )
        fprintf(stderr, " result=error");
    fprintf(stderr, "\n");
    if( result != CS2VM_EXECNO_YIELD )
        CS2VM2_ClearTraceExtra();
}

void
CS2VM2_BindHost(
    struct CS2VM2* vm,
    void* user,
    CS2VM2_HostExec_Fn host_exec)
{
    assert(vm);
    assert(host_exec);
    vm->user = user;
    vm->host_exec = host_exec;
}

int
CS2VM2_DotOrActiveComponentId(
    struct CS2VM2_Thread* vm,
    int operand)
{
    assert(vm);
    return operand == 1 ? vm->dot_component_id : vm->active_component_id;
}

void
CS2VM2_SetTargetComponentId(
    struct CS2VM2_Thread* vm,
    int operand,
    int component_id)
{
    assert(vm);
    if( operand == 1 )
        vm->dot_component_id = component_id;
    else
        vm->active_component_id = component_id;
}

void
CS2VM2_ResetChildrenIter(struct CS2VM2_Thread* vm)
{
    assert(vm);
    vm->children_iter_count = 0;
    vm->children_iter_index = 0;
    vm->children_iter_parent = -1;
}

static inline int
CS2VM2_JumpRelative(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    frame->pc += operand;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VM2_PopFrame(struct CS2VM2_Thread* vm)
{
    assert(vm);
    vm->frame_sp--;
    return CS2VM_EXECNO_OK;
}

int
CS2VM2_PopInt(
    struct CS2VM2_Thread* vm,
    int* operand)
{
    assert(vm);
    if( vm->ints_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *operand = vm->ints_stack[--vm->ints_stack_top];
    return CS2VM_EXECNO_OK;
}

int
CS2VM2_PopIntFrameLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    if( vm->ints_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;

    frame->int_locals[operand] = vm->ints_stack[--vm->ints_stack_top];

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_PushInt(
    struct CS2VM2_Thread* vm,
    int value)
{
    assert(vm);

    if( vm->ints_stack_top >= CS2VM_STACK_MAX )
        return CS2VM_EXECNO_ERROR;

    vm->ints_stack[vm->ints_stack_top++] = value;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VM2_SetIntFrameLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int idx,
    int value)
{
    assert(vm);
    assert(frame);
    frame->int_locals[idx] = value;
    return CS2VM_EXECNO_OK;
}

int
CS2VM2_SetIntCurrentFrameLocal(
    struct CS2VM2_Thread* vm,
    int idx,
    int value)
{
    assert(vm);
    return CS2VM2_SetIntFrameLocal(vm, CS2VM_FRAME(vm), idx, value);
}

int
CS2VM2_SetStringCurrentFrameLocal(
    struct CS2VM2_Thread* vm,
    int idx,
    char const* value)
{
    struct CS2VM2_Frame* frame;
    assert(vm);
    if( idx < 0 || idx >= CS2VM_MAX_LOCALS )
        return CS2VM_EXECNO_ERROR;
    frame = CS2VM_FRAME(vm);
    frame->str_locals[idx] = CS2VM2_StrDup(vm, value);
    return CS2VM_EXECNO_OK;
}

int
CS2VM2_PushIntFrameLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);

    return CS2VM2_PushInt(vm, frame->int_locals[idx]);
}

int
CS2VM2_PushIntCurrentFrameLocal(
    struct CS2VM2_Thread* vm,
    int idx)
{
    assert(vm);

    struct CS2VM2_Frame* frame = CS2VM_FRAME(vm);
    return CS2VM2_PushIntFrameLocal(vm, frame, idx);
}

char*
CS2VM2_StrAlloc(
    struct CS2VM2_Thread* vm,
    size_t len)
{
    assert(vm);
    return CS2VM2_StrPool_Alloc(&vm->str_pool, len);
}

char*
CS2VM2_StrDup(
    struct CS2VM2_Thread* vm,
    char const* text)
{
    assert(vm);
    return CS2VM2_StrPool_Dup(&vm->str_pool, text);
}

char*
CS2VM2_StrDupLen(
    struct CS2VM2_Thread* vm,
    char const* text,
    size_t len)
{
    assert(vm);
    return CS2VM2_StrPool_DupLen(&vm->str_pool, text, len);
}

char*
CS2VM2_StrFmt(
    struct CS2VM2_Thread* vm,
    char const* fmt,
    ...)
{
    assert(vm);

    va_list args;
    va_start(args, fmt);
    char* out = CS2VM2_StrPool_VFmt(&vm->str_pool, fmt, args);
    va_end(args);
    return out;
}

char*
CS2VM2_StrEmpty(struct CS2VM2_Thread* vm)
{
    assert(vm);
    return CS2VM2_StrPool_Empty(&vm->str_pool);
}

int
CS2VM2_PopStr(
    struct CS2VM2_Thread* vm,
    char** value)
{
    assert(vm);
    if( vm->strs_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *value = vm->strs_stack[--vm->strs_stack_top];
    return CS2VM_EXECNO_OK;
}

int
CS2VM2_PopStrFrameLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    if( vm->strs_stack_top <= 0 )
        return CS2VM_EXECNO_OK;

    frame->str_locals[operand] = vm->strs_stack[--vm->strs_stack_top];

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_PushStr(
    struct CS2VM2_Thread* vm,
    char* value)
{
    assert(vm);

    if( vm->strs_stack_top >= CS2VM_STACK_MAX )
        return CS2VM_EXECNO_ERROR;

    vm->strs_stack[vm->strs_stack_top++] = value;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_PushStrFrameLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);

    /* A string local that was never assigned. The reference's frame is a
     * `String[]` of nulls and pushing one is legal there — a script that
     * declares `def_string $s;` and pushes it before any write is ordinary
     * cache code, not a contract violation, so it cannot reach StrPool_Dup's
     * assert. "" is the value every reader downstream already handles. */
    if( !frame->str_locals[idx] )
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));

    /* An array HANDLE rides string locals and the string stack as a raw
     * pointer — strdup'ing it would copy the struct's leading bytes as "text"
     * and the callee's array ops would resolve nothing (the spellbook sort
     * received exactly that and silently no-opped). Pass it through. */
    if( cs2vm2_array_from_handle(vm, frame->str_locals[idx]) )
        return CS2VM2_PushStr(vm, frame->str_locals[idx]);

    /* Push a copy, not the local's pointer: the in-place string opcodes
     * (UPPERCASE / LOWERCASE) rewrite the buffer their operand points at, which
     * would otherwise silently rewrite the local too. */
    return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, frame->str_locals[idx]));
}

int
CS2VM2_PushCallScript(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Script* script);

int
CS2VM2_Op_PushVar(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR;
    request.u.vars_read_varp.varp_id = operand;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_PushVarbit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARBIT;
    request.u.vars_read_varbit.varbit_id = operand;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/* KEYHELD / KEYPRESSED: pop an OSRS internal key code, host pushes 0/1. */
static int
CS2VM2_Op_KeyQuery(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    enum CS2VM_HostRequestKind kind)
{
    assert(vm);
    assert(frame);

    int key_code;
    if( CS2VM2_PopInt(vm, &key_code) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.key_query.key_code = key_code;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_PushVarcInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARC_INT;
    request.u.vars_read_varc_int.varc_id = operand;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_PopVarcInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT;
    request.u.vars_write_varc_int.varc_id = operand;
    request.u.vars_write_varc_int.value = value;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_PushVarcString(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING;
    request.u.vars_read_varc_string.varc_id = operand;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_PopVarcString(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    char* value;
    if( CS2VM2_PopStr(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING;
    request.u.vars_write_varc_string.varc_id = operand;
    request.u.vars_write_varc_string.value = value;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_PushConstantString(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    char const* value)
{
    assert(vm);
    (void)frame;
    /* A copy, not the script's operand: the operand outlives this run and the
     * in-place string opcodes would rewrite it for every later execution. */
    return CS2VM2_PushStr(vm, value ? CS2VM2_StrDup(vm, value) : CS2VM2_StrEmpty(vm));
}

int
CS2VM2_Op_PushConstantInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int value)
{
    assert(vm);
    return CS2VM2_PushInt(vm, value);
}

int
CS2VM2_Op_PushIntLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VM2_PushIntFrameLocal(vm, frame, operand);
}

int
CS2VM2_Op_PushStrLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);
    return CS2VM2_PushStrFrameLocal(vm, frame, idx);
}

int
CS2VM2_Op_PopIntLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VM2_PopIntFrameLocal(vm, frame, operand);
}

int
CS2VM2_Op_PopStrLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VM2_PopStrFrameLocal(vm, frame, operand);
}

int
CS2VM2_Op_JoinString(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int count = operand;
    if( count <= 0 )
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));

    if( count == 1 )
    {
        char* s;
        if( CS2VM2_PopStr(vm, &s) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushStr(vm, s);
    }

    char** parts = calloc((size_t)count, sizeof(char*));
    assert(parts);

    for( int i = count - 1; i >= 0; i-- )
    {
        if( CS2VM2_PopStr(vm, &parts[i]) != CS2VM_EXECNO_OK )
        {
            free(parts);
            return CS2VM_EXECNO_ERROR;
        }
    }

    size_t total = 0;
    for( int i = 0; i < count; i++ )
        total += parts[i] ? strlen(parts[i]) : 0;

    char* joined = CS2VM2_StrAlloc(vm, total);

    char* out = joined;
    for( int i = 0; i < count; i++ )
    {
        if( parts[i] )
        {
            size_t len = strlen(parts[i]);
            memcpy(out, parts[i], len);
            out += len;
        }
    }
    *out = '\0';
    free(parts);

    return CS2VM2_PushStr(vm, joined);
}

int
CS2VM2_Op_ToString(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushStr(vm, CS2VM2_StrFmt(vm, "%d", value));
}

/*
 * ESCAPE — neutralise a string's markup so it renders literally.
 *
 * The renderer treats '<' as the start of a tag (<col=...>, <br>, <img=...>),
 * so unescaped text can inject formatting — this is what scripts call before
 * putting player-supplied text into a widget. `<lt>` / `<gt>` are the escapes
 * ToriDraw_Font decodes back to '<' / '>' (see toridraw_font.c), so escaping to
 * those round-trips to the original characters on screen.
 *
 * NOTE: the xrsps reference escapes to the HTML entities `&lt;` / `&gt;` — that
 * is right for a DOM renderer and wrong for us; our font would draw them
 * literally. Match the renderer, not the reference, for this one.
 */
static int
CS2VM2_Op_Escape(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    char* text;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    assert(text);

    /* Worst case every character is '<' or '>', each growing to four bytes. */
    size_t len = strlen(text);
    char* out = CS2VM2_StrAlloc(vm, len * 4u);

    size_t out_len = 0;
    for( size_t i = 0; i < len; i++ )
    {
        if( text[i] == '<' )
        {
            memcpy(out + out_len, "<lt>", 4);
            out_len += 4;
        }
        else if( text[i] == '>' )
        {
            memcpy(out + out_len, "<gt>", 4);
            out_len += 4;
        }
        else
        {
            out[out_len++] = text[i];
        }
    }
    out[out_len] = '\0';

    return CS2VM2_PushStr(vm, out);
}

/*
 * CHAR_ISPRINTABLE — can this character code be drawn?
 *
 * The client's font covers printable ASCII, the Latin-1 supplement, and a handful
 * of stragglers that cp1252 places in the 0x80..0x9F control range and so map to
 * scattered Unicode points (euro, OE/oe ligatures, em dash, Y-diaeresis). Used to
 * filter keyboard input, so being wrong here silently eats keystrokes.
 */
static int
CS2VM2_Op_CharIsPrintable(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    int chr;
    if( CS2VM2_PopInt(vm, &chr) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int printable = (chr >= 32 && chr <= 126) || (chr >= 160 && chr <= 255) ||
                    chr == 0x20AC || chr == 0x0152 || chr == 0x2014 || chr == 0x0153 ||
                    chr == 0x0178;
    return CS2VM2_PushInt(vm, printable ? 1 : 0);
}

/* CHAR_ISALPHANUMERIC / CHAR_ISALPHA / CHAR_ISNUMERIC — ASCII-only predicates. */
static int
CS2VM2_Op_CharClass(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    assert(vm);

    int chr;
    if( CS2VM2_PopInt(vm, &chr) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int is_alpha = (chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z');
    int is_digit = (chr >= '0' && chr <= '9');
    int match = 0;

    switch( opcode )
    {
    case CS2_OP_CHAR_ISALPHANUMERIC:
        match = is_alpha || is_digit;
        break;
    case CS2_OP_CHAR_ISALPHA:
        match = is_alpha;
        break;
    case CS2_OP_CHAR_ISNUMERIC:
        match = is_digit;
        break;
    default:
        break;
    }
    return CS2VM2_PushInt(vm, match ? 1 : 0);
}

/* UPPERCASE — ASCII-uppercase a string (locale-independent, like the client). */
static int
CS2VM2_Op_Uppercase(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    char* text;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    assert(text);

    for( char* ch = text; *ch; ch++ )
    {
        if( *ch >= 'a' && *ch <= 'z' )
            *ch = (char)(*ch - 'a' + 'A');
    }
    /* Rewritten in place: the popped buffer is pool memory nothing else aliases
     * (every producer hands out a fresh copy), so it can be pushed straight back. */
    return CS2VM2_PushStr(vm, text);
}

/* LOWERCASE — ASCII-lowercase a string (locale-independent, like the client). */
static int
CS2VM2_Op_Lowercase(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    char* text;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    assert(text);

    for( char* ch = text; *ch; ch++ )
    {
        if( *ch >= 'A' && *ch <= 'Z' )
            *ch = (char)(*ch - 'A' + 'a');
    }
    /* In place, as UPPERCASE above. */
    return CS2VM2_PushStr(vm, text);
}

/* REMOVETAGS — strip every <...> run, leaving the plain text. */
static int
CS2VM2_Op_RemoveTags(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    char* text;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    assert(text);

    size_t len = strlen(text);
    char* out = CS2VM2_StrAlloc(vm, len);

    size_t out_len = 0;
    bool in_tag = false;
    for( size_t i = 0; i < len; i++ )
    {
        if( text[i] == '<' )
            in_tag = true;
        else if( in_tag && text[i] == '>' )
            in_tag = false;
        else if( !in_tag )
            out[out_len++] = text[i];
    }
    out[out_len] = '\0';

    return CS2VM2_PushStr(vm, out);
}

int
CS2VM2_Op_Append(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char *src, *dest;
    if( CS2VM2_PopStr(vm, &src) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &dest) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    /* Sized to fit. These used to format into a char[512] and truncate, which
     * quietly clipped long concatenations (the reference's StringBuilder has no
     * such limit). */
    return CS2VM2_PushStr(vm, CS2VM2_StrFmt(vm, "%s%s", dest ? dest : "", src ? src : ""));
}

int
CS2VM2_Op_AppendNum(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int num;
    char* str;
    if( CS2VM2_PopInt(vm, &num) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &str) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushStr(vm, CS2VM2_StrFmt(vm, "%s%d", str ? str : "", num));
}

int
CS2VM2_Op_AppendSignNum(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int num;
    char* str;
    if( CS2VM2_PopInt(vm, &num) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &str) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushStr(
        vm, CS2VM2_StrFmt(vm, "%s%s%d", str ? str : "", num >= 0 ? "+" : "", num));
}

int
CS2VM2_Op_AppendChar(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int chr;
    char* str;
    if( CS2VM2_PopInt(vm, &chr) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &str) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    size_t len = str ? strlen(str) : 0;
    char* result = CS2VM2_StrAlloc(vm, len + 1u);
    if( len > 0 )
        memcpy(result, str, len);
    result[len] = (char)chr;

    return CS2VM2_PushStr(vm, result);
}

int
CS2VM2_Op_StringLength(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char* str;
    if( CS2VM2_PopStr(vm, &str) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, str ? (int)strlen(str) : 0);
}

int
CS2VM2_Op_ParaHeight(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int font_id;
    int max_width;
    char* text;

    if( CS2VM2_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &max_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_PARAHEIGHT;
    request.u.para_height.font_id = font_id;
    request.u.para_height.max_width = max_width;
    request.u.para_height.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_ParaWidth(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int font_id;
    int max_width;
    char* text;

    if( CS2VM2_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &max_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_PARAWIDTH;
    request.u.para_height.font_id = font_id;
    request.u.para_height.max_width = max_width;
    request.u.para_height.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_GosubWithParams(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    assert(vm->vm->host_exec);

    if( vm->frame_sp >= CS2VM_MAX_FRAMES )
    {
        /* Say so. A blown call stack otherwise surfaces as "script N failed at
         * opcode 40", which reads like a bad gosub target and sent this hunt
         * looking at the callee rather than the depth. */
        fprintf(
            stderr,
            "CS2VM2: call depth %d exhausted calling script %d from script %d "
            "(raise CS2VM_MAX_FRAMES)\n",
            CS2VM_MAX_FRAMES,
            operand,
            frame->script ? frame->script->script_id : -1);
        return CS2VM_EXECNO_ERROR;
    }

    struct CS2VM2_Frame* caller = frame;
    caller->return_pc = caller->pc;
    caller->return_frame = vm->frame_sp - 1;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
    request.u.push_script.script_id = operand;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    struct CS2VM2_Frame* callee = CS2VM_FRAME(vm);
    int argc = callee->script->int_argument_count + callee->script->string_argument_count;
    int str_args = callee->script->string_argument_count;
    int int_args = callee->script->int_argument_count;

    for( int i = str_args - 1; i >= 0; i-- )
    {
        char* value = NULL;
        if( CS2VM2_PopStr(vm, &value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        callee->str_locals[i] = value;
    }
    for( int i = int_args - 1; i >= 0; i-- )
    {
        if( CS2VM2_PopIntFrameLocal(vm, callee, i) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_DeleteAll(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame)
{
    assert(vm);
    assert(frame);

    int component_id;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_DELETEALL;
    request.u.cc_delete_all.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

// CC_COPY: clone dynamic child src_sub of parent into slot dst_sub.
// Stack (bottom->top): [parent_id, src_sub, dst_sub]. Sets active/dot to the copy.
int
CS2VM2_Op_CC_Copy(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int dst_sub, src_sub, parent_id;

    if( CS2VM2_PopInt(vm, &dst_sub) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &src_sub) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &parent_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_COPY;
    request.u.cc_copy.parent_id = parent_id;
    request.u.cc_copy.src_sub_id = src_sub;
    request.u.cc_copy.dst_sub_id = dst_sub;
    request.u.cc_copy.dot_operand = operand;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_Find(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int sub, parent;

    if( CS2VM2_PopInt(vm, &sub) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &parent) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_FIND;
    request.u.cc_find.parent_id = parent;
    request.u.cc_find.sub_id = sub;
    request.u.cc_find.dot_operand = operand;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

// CC_CREATE: When intOp=1 (dot variant .cc_create), sets dotWidget instead of activeWidget
//
// PARITY: Argument count depends on client revision, NOT stack contents.
// - Older revisions (< 200): 3 args [parentUid, type, childIndex] (aka Kronos)
// - Modern revisions (>= 200): 4 args [parentUid, type, childIndex, isNested]
int
CS2VM2_Op_CC_Create(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int is_nested = 0, child_index, type, parent_id;

    /* RS2 (634) pushes only (parent, type, index) — Class66.method710 opcode 100
     * pops three. The nested flag is a later OldSchool addition; popping it under
     * RS2 steals the parent id and the create fails on a garbage uid. */
    if( !(frame->script && frame->script->rs2_dialect) )
    {
        if( CS2VM2_PopInt(vm, &is_nested) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    if( CS2VM2_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &parent_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    request.u.cc_create.parent_id = parent_id;
    request.u.cc_create.component_type = type;
    request.u.cc_create.child_index = child_index;
    request.u.cc_create.is_nested = is_nested;
    request.u.cc_create.dot_operand = operand;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

// === Position and Size ===
// 4 args read as array
int
CS2VM2_Op_CC_SetPosition(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int x, y, xmode, ymode;

    if( CS2VM2_PopInt(vm, &ymode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &xmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETPOSITION;
    request.u.cc_set_position.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_position.x = x;
    request.u.cc_set_position.y = y;
    request.u.cc_set_position.xmode = xmode;
    request.u.cc_set_position.ymode = ymode;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetSize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int w, h, wmode, hmode;

    if( CS2VM2_PopInt(vm, &hmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &wmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &h) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &w) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETSIZE;
    request.u.cc_set_size.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_size.width = w;
    request.u.cc_set_size.height = h;
    request.u.cc_set_size.wmode = wmode;
    request.u.cc_set_size.hmode = hmode;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetGraphic(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int graphic_id;

    if( CS2VM2_PopInt(vm, &graphic_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
    request.u.cc_set_graphic.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_graphic.graphic_id = graphic_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetGraphic2(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int graphic_id;

    if( CS2VM2_PopInt(vm, &graphic_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC2;
    request.u.cc_set_graphic2.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_graphic2.graphic_id = graphic_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetTiling(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int tiling;
    if( CS2VM2_PopInt(vm, &tiling) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTILING;
    request.u.cc_set_tiling.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_tiling.tiling = tiling;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetOutline(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int outline;
    if( CS2VM2_PopInt(vm, &outline) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETOUTLINE;
    request.u.cc_set_outline.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_outline.outline = outline;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetGraphicShadow(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int shadow;
    if( CS2VM2_PopInt(vm, &shadow) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW;
    request.u.cc_set_graphic_shadow.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_graphic_shadow.shadow = shadow;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetTiling(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, tiling;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &tiling) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTILING;
    request.u.cc_set_tiling.component_id = component_id;
    request.u.cc_set_tiling.tiling = tiling;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetGraphicShadow(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, shadow;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &shadow) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW;
    request.u.cc_set_graphic_shadow.component_id = component_id;
    request.u.cc_set_graphic_shadow.shadow = shadow;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetColour(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int colour;
    if( CS2VM2_PopInt(vm, &colour) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETCOLOUR;
    request.u.cc_set_colour.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_colour.colour = colour;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetColour(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, colour;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &colour) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETCOLOUR;
    request.u.cc_set_colour.component_id = component_id;
    request.u.cc_set_colour.colour = colour;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetFill(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int filled;
    if( CS2VM2_PopInt(vm, &filled) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETFILL;
    request.u.cc_set_fill.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_fill.filled = filled;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetFill(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, filled;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &filled) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETFILL;
    request.u.cc_set_fill.component_id = component_id;
    request.u.cc_set_fill.filled = filled;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetTrans(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int trans;
    if( CS2VM2_PopInt(vm, &trans) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTRANS;
    request.u.cc_set_trans.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_trans.trans = trans;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/*
 * CC_TRIGGEROP(op) — run the dot/active component's on_op listener now, as if
 * op index `op` had been picked from its menu.
 *
 * One int in, nothing out (local_commands.py witness table: `cc_triggerop:
 * (["INT"], [], False)`). script6014, the shift-click-inventory-option
 * handler, is the call site:
 *
 *     if( cc_find($component0, $comsubid2) = ^true ) {
 *         ...
 *         cc_triggerop(enum(int, int, enum_4303, oc_shiftclickiop($int1)));
 *     }
 *
 * i.e. cc_find locates the target component and cc_triggerop fires its op
 * handler directly, without a real click. Queued rather than run in place —
 * same reason as IF_CALLONRESIZE: this is reached from inside a running CS2
 * script and the host has no runner to nest a second one on.
 */
/*
 * The four sound opcodes.
 *
 * Arguments are popped last-to-first, which is why each of these reads its
 * fields in reverse of the source-level call. Getting that backwards is silent
 * -- sound_synth(synth, 1, 0) with the order reversed plays synth 0 with 1
 * repeat at a delay of `synth`, which is a real sound at a real time, just the
 * wrong one -- so the pop order is written out per field rather than inferred.
 */
static int
sound_request(
    struct CS2VM2_Thread* vm,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_Sound* sound)
{
    struct CS2VM_HostRequest request;

    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.sound = *sound;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_SoundSynth(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_Sound sound;

    (void)frame;
    (void)operand;
    memset(&sound, 0, sizeof(sound));
    /* sound_synth(synth, loops, delay) */
    if( CS2VM2_PopInt(vm, &sound.delay) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.loops) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return sound_request(vm, CS2VM_HOST_REQUEST_SOUND_SYNTH, &sound);
}

int
CS2VM2_Op_SoundSong(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_Sound sound;

    (void)frame;
    (void)operand;
    memset(&sound, 0, sizeof(sound));
    /* sound_song(id, fadeOutDelay, fadeOutSpeed, fadeInDelay, fadeInSpeed) */
    if( CS2VM2_PopInt(vm, &sound.fade_in_speed) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.fade_in_delay) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.fade_out_speed) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.fade_out_delay) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return sound_request(vm, CS2VM_HOST_REQUEST_SOUND_SONG, &sound);
}

int
CS2VM2_Op_SoundJingle(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_Sound sound;

    (void)frame;
    (void)operand;
    memset(&sound, 0, sizeof(sound));
    /* sound_jingle(id, delay) */
    if( CS2VM2_PopInt(vm, &sound.delay) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return sound_request(vm, CS2VM_HOST_REQUEST_SOUND_JINGLE, &sound);
}

int
CS2VM2_Op_SoundSongWithSecondary(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_Sound sound;

    (void)frame;
    (void)operand;
    memset(&sound, 0, sizeof(sound));
    /* _3221(primary, secondary, fadeOutDelay, fadeOutSpeed, fadeInDelay,
     *       fadeInSpeed) -- script9630 passes the same four fade arguments it
     *       would have passed to sound_song, with the secondary spliced in. */
    if( CS2VM2_PopInt(vm, &sound.fade_in_speed) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.fade_in_delay) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.fade_out_speed) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.fade_out_delay) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.secondary_id) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &sound.id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return sound_request(vm, CS2VM_HOST_REQUEST_SOUND_SONG_WITHSECONDARY, &sound);
}

int
CS2VM2_Op_CC_TriggerOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int op_index;
    if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_TRIGGEROP;
    request.u.cc_trigger_op.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_trigger_op.op_index = op_index;

    return vm->vm->host_exec(vm, &request);
}

/*
 * IF_TRIGGEROPLOCAL(crc, component, childIndex, typed..., signature) —
 * notify the server of a synthetic button click with typed args.
 *
 * Stack shape from local_commands.py (`"_2929": (["INT","INT","INT","INT",
 * "STRING"], [], False)` for the common "i" signature) and xrsps's
 * forwardIfTriggerOpLocal: pop signature, then typed args per char, then
 * childIndex / component / crc (top to bottom). crc is the IF_SCRIPT_TRIGGER
 * script key on real rev-239 wire; ignored here. sub for the packet is
 * childIndex when set, else the first typed int (Quest XP View-journal).
 */
int
CS2VM2_Op_IF_TriggerOpLocal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char* signature = NULL;
    if( CS2VM2_PopStr(vm, &signature) != CS2VM_EXECNO_OK || !signature )
        return CS2VM_EXECNO_ERROR;

    int typed0 = -1;
    int argc = (int)strlen(signature);
    for( int i = argc - 1; i >= 0; i-- )
    {
        if( signature[i] == 'i' )
        {
            int v;
            if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
            if( i == 0 )
                typed0 = v;
        }
        else
        {
            char* ignored = NULL;
            if( CS2VM2_PopStr(vm, &ignored) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
        }
    }

    int child_index;
    int component_id;
    int crc;
    if( CS2VM2_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &crc) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    (void)crc;

    int sub = (child_index != -1) ? child_index : typed0;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_TRIGGEROPLOCAL;
    request.u.if_triggeroplocal.component_id = component_id;
    request.u.if_triggeroplocal.sub = sub;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_SetTrans(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, trans;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &trans) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTRANS;
    request.u.cc_set_trans.component_id = component_id;
    request.u.cc_set_trans.trans = trans;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetNoClickThrough(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int enabled;
    if( CS2VM2_PopInt(vm, &enabled) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH;
    request.u.cc_set_no_click_through.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_no_click_through.enabled = enabled;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetText(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    char* text;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXT;
    request.u.cc_set_text.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetTextFont(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int font_id;
    if( CS2VM2_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
    request.u.cc_set_text_font.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text_font.font_id = font_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetTextFont(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, font_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
    request.u.cc_set_text_font.component_id = component_id;
    request.u.cc_set_text_font.font_id = font_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetTextAlign(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int line_height, y_align, x_align;
    if( CS2VM2_PopInt(vm, &line_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &y_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &x_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTALIGN;
    request.u.cc_set_text_align.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text_align.x_align = x_align;
    request.u.cc_set_text_align.y_align = y_align;
    request.u.cc_set_text_align.line_height = line_height;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetTextAlign(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, line_height, y_align, x_align;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &line_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &y_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &x_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTALIGN;
    request.u.cc_set_text_align.component_id = component_id;
    request.u.cc_set_text_align.x_align = x_align;
    request.u.cc_set_text_align.y_align = y_align;
    request.u.cc_set_text_align.line_height = line_height;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetTextShadow(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int shadowed;
    if( CS2VM2_PopInt(vm, &shadowed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW;
    request.u.cc_set_text_shadow.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text_shadow.shadowed = shadowed;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetTextShadow(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, shadowed;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &shadowed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW;
    request.u.cc_set_text_shadow.component_id = component_id;
    request.u.cc_set_text_shadow.shadowed = shadowed;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetDraggable(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int child_index;
    int parent_uid;

    if( CS2VM2_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &parent_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGGABLE;
    request.u.cc_set_draggable.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_draggable.parent_uid = parent_uid;
    request.u.cc_set_draggable.child_index = child_index;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetDraggableBehavior(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int behavior;
    if( CS2VM2_PopInt(vm, &behavior) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR;
    request.u.cc_set_draggable_behavior.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_draggable_behavior.behavior = behavior;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetDragDeadZone(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int zone;
    if( CS2VM2_PopInt(vm, &zone) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE;
    request.u.cc_set_drag_dead_zone.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_drag_dead_zone.zone = zone;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetDragDeadTime(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int time;
    if( CS2VM2_PopInt(vm, &time) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME;
    request.u.cc_set_drag_dead_time.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_drag_dead_time.time = time;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetObject(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    int num_mode)
{
    assert(vm);
    assert(frame);

    int count, obj_id;

    if( CS2VM2_PopInt(vm, &count) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &obj_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
    request.u.cc_set_object.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_object.obj_id = obj_id;
    request.u.cc_set_object.count = count;
    request.u.cc_set_object.num_mode = num_mode;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetObject(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    int num_mode)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, count, obj_id;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &count) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &obj_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOBJECT;
    request.u.if_set_object.component_id = component_id;
    request.u.if_set_object.obj_id = obj_id;
    request.u.if_set_object.count = count;
    request.u.if_set_object.num_mode = num_mode;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetGraphic(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int graphic_id, component_id;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &graphic_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETGRAPHIC;
    request.u.if_set_graphic.component_id = component_id;
    request.u.if_set_graphic.graphic_id = graphic_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/* Absolute (IF) form of CC_SETGRAPHIC2 (1122): sets a widget's secondary /
 * alternate sprite (OSRS Widget.spriteId). Kronos pops the component first, then
 * the sprite id (stack top = component, under it = sprite). Both forms feed the
 * same component-addressed CC_SETGRAPHIC2 host request, which writes
 * scene_id_active and marks the node dirty — the difference is only that the CC
 * form takes the component from the dot/active register, this one off the stack. */
int
CS2VM2_Op_IF_SetGraphic2(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int graphic_id, component_id;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &graphic_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC2;
    request.u.cc_set_graphic2.component_id = component_id;
    request.u.cc_set_graphic2.graphic_id = graphic_id;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_SetText(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    char* text;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETTEXT;
    request.u.if_set_text.component_id = component_id;
    request.u.if_set_text.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int index;
    char* text;

    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETOP;
    request.u.if_set_op.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.if_set_op.index = index;
    request.u.if_set_op.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_SetOpBase(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    char* text;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOPBASE;
    request.u.if_set_op_base.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.if_set_op_base.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

static int
CS2VM2_Op_CC_SetTargetVerb(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    char* text;

    assert(vm);
    assert(frame);
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request = { 0 };
    request.kind = CS2VM_HOST_REQUEST_IF_SETTARGETVERB;
    request.u.if_set_target_verb.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.if_set_target_verb.text = text;
    return vm->vm->host_exec(vm, &request);
}

/*
 * CC/IF_SETOPKEY family. Stack layouts (bottom -> top), from the reference
 * WidgetOps.ts:2864-3060:
 *
 *   CC_SETOPKEY   opindex, then 5 (keychar, keycode) pairs
 *   CC_SETOPTKEY  keychar, keycode                      (op slot 10 implied)
 *   IF_SETOPKEY   opindex, keychar, keycode, component  (component on top)
 *   IF_SETOPTKEY  keychar, keycode, component
 *
 * The CC form always reserves all five pairs on the stack but stops reading at
 * the first negative keychar; the IF form carries only one pair.
 */
static int
CS2VM2_Op_SetOpKey(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    int is_if,
    int is_typed)
{
    struct CS2VM_HostRequest request;
    int raw[2 * CS2VM_OPKEY_PAIR_MAX];
    int available_pairs = (!is_if && !is_typed) ? CS2VM_OPKEY_PAIR_MAX : 1;
    int component_id = 0;
    int op_index = CS2VM_OPKEY_TYPED_SLOT;
    int pair_count = 0;

    assert(vm);
    assert(frame);

    if( is_if )
    {
        if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    /* Pop the pair block top-down so raw[] ends up in push order. */
    for( int i = (2 * available_pairs) - 1; i >= 0; i-- )
        if( CS2VM2_PopInt(vm, &raw[i]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;

    if( !is_typed )
    {
        if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    for( int i = 0; i < available_pairs; i++ )
    {
        if( raw[i * 2] < 0 )
            break;
        pair_count++;
    }

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY;
    request.u.widget_set_opkey.component_id =
        is_if ? component_id : CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_opkey.op_index = op_index;
    request.u.widget_set_opkey.pair_count = pair_count;
    for( int i = 0; i < pair_count; i++ )
    {
        request.u.widget_set_opkey.key_chars[i] = raw[i * 2];
        request.u.widget_set_opkey.key_codes[i] = raw[(i * 2) + 1];
    }

    return vm->vm->host_exec(vm, &request);
}

/*
 * CC/IF_SETOPKEYRATE and SETOPKEYIGNOREHELD.
 *
 *   CC_SETOPKEYRATE          opindex, keyrate, tickrate
 *   CC_SETOPTKEYRATE         tickrate, keyrate      (reversed in the reference;
 *                                                    values are unused, so only
 *                                                    the count matters)
 *   CC_SETOPKEYIGNOREHELD    opindex
 *   CC_SETOPTKEYIGNOREHELD   -
 * IF variants append the component uid on top.
 */
static int
CS2VM2_Op_SetOpKeyRate(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    int is_if,
    int is_typed,
    int is_ignore_held)
{
    struct CS2VM_HostRequest request;
    int component_id = 0;
    int op_index = CS2VM_OPKEY_TYPED_SLOT;
    int rate = 0;
    int tick_rate = 0;

    assert(vm);
    assert(frame);

    if( is_if )
    {
        if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    if( !is_ignore_held )
    {
        if( is_typed )
        {
            if( CS2VM2_PopInt(vm, &rate) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
            if( CS2VM2_PopInt(vm, &tick_rate) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
        }
        else
        {
            if( CS2VM2_PopInt(vm, &tick_rate) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
            if( CS2VM2_PopInt(vm, &rate) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
        }
    }

    if( !is_typed )
    {
        if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY_RATE;
    request.u.widget_set_opkey_rate.component_id =
        is_if ? component_id : CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_opkey_rate.op_index = op_index;
    request.u.widget_set_opkey_rate.rate = rate;
    request.u.widget_set_opkey_rate.enabled = tick_rate != 0;
    request.u.widget_set_opkey_rate.ignore_held = is_ignore_held ? 1 : 0;

    return vm->vm->host_exec(vm, &request);
}

/*
 * CC_DELETE — remove the component the VM is currently pointed at.
 *
 * `cc_deleteall` takes a parent and clears its children; this takes no operand
 * at all and deletes the *active* (or dot) component, which is whatever the
 * preceding `cc_find` selected. Every list that removes one row rather than
 * rebuilding uses it.
 *
 * It was the last unimplemented opcode in the XP-drop panel's path
 * (`script1006` is a two-line `if (cc_find(...)) cc_delete;`), and an
 * unimplemented opcode is an abort here rather than a no-op — deliberately, see
 * StackMetaStub — so the whole client went down the first time a drop expired.
 */
int
CS2VM2_Op_CC_Delete(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_DELETE;
    request.u.cc_delete.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_ClearOps(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CLEAROPS;
    request.u.if_clear_ops.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_ClearOpSubmenu(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int op_index;
    if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CLEAROPSUBMENU;
    request.u.if_clear_op_submenu.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.if_clear_op_submenu.op_index = op_index;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_SetOpSubmenu(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int sub_index, op_index;
    char* text;
    if( CS2VM2_PopInt(vm, &sub_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOPSUBMENU;
    request.u.if_set_op_submenu.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.if_set_op_submenu.sub_index = sub_index;
    request.u.if_set_op_submenu.op_index = op_index;
    request.u.if_set_op_submenu.text = text;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_SetTargetPriority(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int priority;
    if( CS2VM2_PopInt(vm, &priority) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY;
    request.u.if_set_target_priority.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.if_set_target_priority.priority = priority;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_SetHide(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int hide;
    if( CS2VM2_PopInt(vm, &hide) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETHIDE;
    request.u.if_set_hide.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.if_set_hide.hidden = hide != 0;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}
static int
cs2vm2_op_cc_get_int(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_HostRequestKind kind)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.cc_gettext.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    return vm->vm->host_exec(vm, &request);
}

static int
cs2vm2_op_if_get_int(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_HostRequestKind kind)
{
    assert(vm);
    assert(frame);
    (void)operand;
    (void)frame;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.if_gettext.component_id = component_id;
    return vm->vm->host_exec(vm, &request);
}


int
CS2VM2_Op_CC_GetId(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETID;
    request.u.cc_get_id.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_GetX(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETX;
    request.u.cc_get_id.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_GetY(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETY;
    request.u.cc_get_id.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_GetWidth(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETWIDTH;
    request.u.cc_get_id.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_GetHeight(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETHEIGHT;
    request.u.cc_get_id.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_CC_GetHide(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETHIDE;
    request.u.cc_get_id.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

static int
CS2VM2_Op_CC_GetOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    int op_index;
    struct CS2VM_HostRequest request;
    assert(vm);
    assert(frame);
    (void)frame;

    if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETOP;
    request.u.widget_get_op.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_get_op.op_index = op_index;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_GetWidth(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETWIDTH;
    request.u.if_get_width.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_GetHeight(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETHEIGHT;
    request.u.if_get_height.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

static int
CS2VM2_Op_IF_GetOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    int component_id, op_index;
    struct CS2VM_HostRequest request;
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    /* Official rev-239 method2 resolves the explicit component first, then
     * pops and normalizes the one-based operation index. */
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETOP;
    request.u.widget_get_op.component_id = component_id;
    request.u.widget_get_op.op_index = op_index;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_GetHide(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETHIDE;
    request.u.if_get_width.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/* IF_HASSUB(component) -> 1 if a sub-interface is mounted into `component`, else 0.
 * The gameframe tab-visibility proc (script 908) uses this to decide which tab
 * slot to reveal; without it every slot reads as empty and no tab ever shows. */
int
CS2VM2_Op_IF_HasSub(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_HASSUB;
    request.u.if_get_width.component_id = component_id;

    return vm->vm->host_exec(vm, &request);
}

/* IF_SETPARAM (2704): write a runtime param onto a named component.
 * Stack top-first: type, child_index, component_uid, then typed value, then
 * param_id. child_index of -1 means the component itself (Overview sites).
 * xrsps WidgetOps IF_SETPARAM; call-site arity (5i->()) against script 9176. */
static int
CS2VM2_Op_IF_SetParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    int type;
    int child_index;
    int component_id;
    int param_id;
    int value = 0;
    char* str_value = NULL;

    if( CS2VM2_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( type == 2 || type == 115 || type == CS2_CC_COMPONENTPARAM_KIND_STRING )
    {
        if( CS2VM2_PopStr(vm, &str_value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        type = CS2_CC_COMPONENTPARAM_KIND_STRING;
    }
    else if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    /* child_index != -1 would mean a dynamic child under component_id; Overview
     * always passes -1. A non-(-1) child without a resolve helper is left as the
     * parent uid — better than inventing a lookup we have not verified. */
    (void)child_index;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM;
    request.u.cc_component_param.component_id = component_id;
    request.u.cc_component_param.param_id = param_id;
    request.u.cc_component_param.value = value;
    request.u.cc_component_param.str_value = str_value;
    request.u.cc_component_param.kind = type;

    return vm->vm->host_exec(vm, &request);
}

/* IF_HASCHILD_OVERLAY (2705): pop widget + parent group, push 1 iff mounted.
 * Rev-634 name; 2704 was reclaimed as IF_SETPARAM at this revision. */
static int
CS2VM2_Op_IF_HasChild(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int group_id;
    int component_id;
    if( CS2VM2_PopInt(vm, &group_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_HASCHILD;
    request.u.if_has_child.component_id = component_id;
    request.u.if_has_child.group_id = group_id;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_GetY(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETY;
    request.u.if_get_width.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_GetLayer(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETLAYER;
    request.u.if_get_layer.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_GetTop(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETTOP;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_GetScrollX(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETSCROLLX;
    request.u.if_get_scroll_x.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_GetScrollY(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETSCROLLY;
    request.u.if_get_scroll_y.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_GetScrollHeight(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT;
    request.u.if_get_scroll_height.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetScrollPos(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int scroll_x;
    int scroll_y;

    /* Stack (bottom to top): [scroll_x, scroll_y, uid] — pop in reverse. */
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &scroll_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &scroll_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETSCROLLPOS;
    request.u.if_set_scroll_pos.component_id = component_id;
    request.u.if_set_scroll_pos.scroll_x = scroll_x;
    request.u.if_set_scroll_pos.scroll_y = scroll_y;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetScrollSize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int scroll_width;
    int scroll_height;

    /* Stack (bottom to top): [width, height, uid] — pop in reverse. */
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &scroll_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &scroll_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE;
    request.u.if_set_scroll_size.component_id = component_id;
    request.u.if_set_scroll_size.scroll_width = scroll_width;
    request.u.if_set_scroll_size.scroll_height = scroll_height;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetPosition(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int x, y, xmode, ymode;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &ymode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &xmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETPOSITION;
    request.u.cc_set_position.component_id = component_id;
    request.u.cc_set_position.x = x;
    request.u.cc_set_position.y = y;
    request.u.cc_set_position.xmode = xmode;
    request.u.cc_set_position.ymode = ymode;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetOutline(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, outline;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &outline) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOUTLINE;
    request.u.if_set_outline.component_id = component_id;
    request.u.if_set_outline.outline = outline;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetSize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int w, h, wmode, hmode;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &hmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &wmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &h) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &w) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETSIZE;
    request.u.cc_set_size.component_id = component_id;
    request.u.cc_set_size.width = w;
    request.u.cc_set_size.height = h;
    request.u.cc_set_size.wmode = wmode;
    request.u.cc_set_size.hmode = hmode;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetHide(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id, hide;

    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &hide) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    bool hidden = hide != 0;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETHIDE;
    request.u.if_set_hide.component_id = component_id;
    request.u.if_set_hide.hidden = hidden;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/**
 * Set event handler by widget UID (IF_SETON* opcodes)
 *
 * OSRS stack layout for IF_SETON* trigger hooks (bottom to top):
 * - int stack: [scriptId, intArgs..., widgetUid]  <- UID is at TOP
 * - string stack: [stringArgs..., signature]
 *
 * The widget UID is pushed LAST (so it's at the top), then the signature.
 * We pop: UID first, then signature, then args (reverse order), then scriptId.

  // Parse trigger args (pops signature, args, scriptId, and transmit triggers if 'Y' suffix)
        const parsed = this.parseTriggerArgs();

 */
static int
CS2VM2_Op_IF_SetOnEventHandler(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_IF_SetOnOp* out_request)
{
    assert(vm);
    assert(frame);
    assert(out_request);

    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VM2_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VM2_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VM2_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            assert(trigger_ids);

            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VM2_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    {
        int int_args[CS2VM_SETON_INT_ARG_MAX] = { 0 };
        int int_arg_count = 0;
        char* str_by_pos[CS2VM_SETON_INT_ARG_MAX] = { 0 };
        uint64_t str_arg_mask = 0;

        if( signature && signature_parse_len > 0 )
        {
            for( int i = signature_parse_len - 1; i >= 0; i-- )
            {
                char c = signature[i];
                if( c == 's' || c == 'W' || c == 'X' )
                {
                    char* v = NULL;
                    if( CS2VM2_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                    {
                        free(trigger_ids);
                        return CS2VM_EXECNO_ERROR;
                    }
                    /* Past str_by_pos' 32 slots the value is dropped; the pool owns it. */
                    if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                    {
                        str_by_pos[i] = v;
                        str_arg_mask |= (uint64_t)1 << i;
                        if( i + 1 > int_arg_count )
                            int_arg_count = i + 1;
                    }
                }
                else
                {
                    int v = 0;
                    if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                    {
                        free(trigger_ids);
                        return CS2VM_EXECNO_ERROR;
                    }
                    if( i < (int)(sizeof(int_args) / sizeof(int_args[0])) )
                    {
                        int_args[i] = v;
                        if( i + 1 > int_arg_count )
                            int_arg_count = i + 1;
                    }
                }
            }
        }

        if( CS2VM2_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
        {
            free(trigger_ids);
            return CS2VM_EXECNO_ERROR;
        }

        /*
         * A null script is a DISARM, not a no-op.
         *
         * `if_setontimer(null, com)` is how a script stops its own timer —
         * script1005 (the XP-drop row) ends with exactly that, and script997
         * uses it to turn the XP panel's auto-hide off. Returning here left the
         * previous hook installed, so a listener that had asked to be removed
         * kept firing for the rest of the session. It reaches the host with
         * script_id <= 0, and UITree_HookSet already treats that as "clear the
         * slot"; the transmit registries clear their entry rather than acquire
         * one (see rs_cs2_acquire_*_transmit_hook's `create`).
         *
         * The pops above must happen either way — the stack is unwound by the
         * signature, not by whether a handler is being installed.
         */
        memset(out_request, 0, sizeof(*out_request));
        out_request->component_id = widget_uid;
        out_request->script_id = script_id;
        out_request->signature = signature;
        out_request->trigger_ids = trigger_ids;
        out_request->trigger_count = trigger_count;
        out_request->int_arg_count = int_arg_count;
        if( int_arg_count > 0 )
            memcpy(out_request->int_args, int_args, (size_t)int_arg_count * sizeof(int));
        out_request->str_arg_mask = str_arg_mask;
        for( int i = 0; i < CS2VM_SETON_INT_ARG_MAX; i++ )
        {
            if( !(str_arg_mask & ((uint64_t)1 << i)) )
                continue;
            if( out_request->str_arg_count < CS2VM_SETON_STR_ARG_MAX )
            {
                char* dst = out_request->str_args[out_request->str_arg_count];
                strncpy(dst, str_by_pos[i] ? str_by_pos[i] : "", CS2VM_SETON_STR_ARG_LEN - 1);
                dst[CS2VM_SETON_STR_ARG_LEN - 1] = '\0';
            }
            out_request->str_arg_count++;
        }
        if( out_request->str_arg_count > CS2VM_SETON_STR_ARG_MAX )
            out_request->str_arg_count = CS2VM_SETON_STR_ARG_MAX;
    }

    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = kind;
        request.u.if_set_on_op = *out_request;

        int result = vm->vm->host_exec(vm, &request);
        free(trigger_ids);
        out_request->trigger_ids = NULL;
        if( result != CS2VM_EXECNO_OK )
            return result;

        return CS2VM_EXECNO_OK;
    }
}

/**
 * Set event handler on active/dot child (CC_SETON* opcodes).
 *
 * OSRS stack layout (bottom to top):
 * - int stack: [scriptId, intArgs...]
 * - string stack: [stringArgs..., signature]
 *
 * Target component comes from operand (0 = active, 1 = dot), not the stack.
 */
static int
CS2VM2_Op_CC_SetOnEventHandler(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_CC_SetOnOp* out_request)
{
    assert(vm);
    assert(frame);
    assert(out_request);

    int script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VM2_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VM2_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            assert(trigger_ids);

            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VM2_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    {
        int int_args[CS2VM_SETON_INT_ARG_MAX] = { 0 };
        int int_arg_count = 0;
        char* str_by_pos[CS2VM_SETON_INT_ARG_MAX] = { 0 };
        uint64_t str_arg_mask = 0;

        if( signature && signature_parse_len > 0 )
        {
            for( int i = signature_parse_len - 1; i >= 0; i-- )
            {
                char c = signature[i];
                if( c == 's' || c == 'W' || c == 'X' )
                {
                    char* v = NULL;
                    if( CS2VM2_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                    {
                        free(trigger_ids);
                        return CS2VM_EXECNO_ERROR;
                    }
                    /* Past str_by_pos' 32 slots the value is dropped; the pool owns it. */
                    if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                    {
                        str_by_pos[i] = v;
                        str_arg_mask |= (uint64_t)1 << i;
                        if( i + 1 > int_arg_count )
                            int_arg_count = i + 1;
                    }
                }
                else
                {
                    int v = 0;
                    if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                    {
                        free(trigger_ids);
                        return CS2VM_EXECNO_ERROR;
                    }
                    if( i < (int)(sizeof(int_args) / sizeof(int_args[0])) )
                    {
                        int_args[i] = v;
                        if( i + 1 > int_arg_count )
                            int_arg_count = i + 1;
                    }
                }
            }
        }

        if( CS2VM2_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
        {
            free(trigger_ids);
            return CS2VM_EXECNO_ERROR;
        }

        /* script_id -1 still reaches the host: it clears the stored hook
         * (reference: setting a null handler removes the listener). */
        memset(out_request, 0, sizeof(*out_request));
        out_request->component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
        out_request->script_id = script_id;
        out_request->signature = signature;
        out_request->trigger_ids = trigger_ids;
        out_request->trigger_count = trigger_count;
        out_request->int_arg_count = int_arg_count;
        if( int_arg_count > 0 )
            memcpy(out_request->int_args, int_args, (size_t)int_arg_count * sizeof(int));
        out_request->str_arg_mask = str_arg_mask;
        for( int i = 0; i < CS2VM_SETON_INT_ARG_MAX; i++ )
        {
            if( !(str_arg_mask & ((uint64_t)1 << i)) )
                continue;
            if( out_request->str_arg_count < CS2VM_SETON_STR_ARG_MAX )
            {
                char* dst = out_request->str_args[out_request->str_arg_count];
                strncpy(dst, str_by_pos[i] ? str_by_pos[i] : "", CS2VM_SETON_STR_ARG_LEN - 1);
                dst[CS2VM_SETON_STR_ARG_LEN - 1] = '\0';
            }
            out_request->str_arg_count++;
        }
        if( out_request->str_arg_count > CS2VM_SETON_STR_ARG_MAX )
            out_request->str_arg_count = CS2VM_SETON_STR_ARG_MAX;
    }

    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = kind;
        request.u.cc_set_on_op = *out_request;

        int result = vm->vm->host_exec(vm, &request);
        free(trigger_ids);
        out_request->trigger_ids = NULL;
        if( result != CS2VM_EXECNO_OK )
            return result;

        return CS2VM_EXECNO_OK;
    }
}

int
CS2VM2_Op_CC_SetOnClick(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONCLICK, &request);
}

int
CS2VM2_Op_CC_SetOnHold(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONHOLD, &request);
}

int
CS2VM2_Op_CC_SetOnMouseOver(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER, &request);
}

int
CS2VM2_Op_CC_SetOnMouseLeave(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE, &request);
}

static int
CS2VM2_Op_CC_SetOnTargetEnter(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONTARGETENTER, &request);
}

static int
CS2VM2_Op_CC_SetOnTargetLeave(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONTARGETLEAVE, &request);
}

static int
CS2VM2_Op_CC_SetOnClickRepeat(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONCLICKREPEAT, &request);
}

static int
CS2VM2_Op_CC_SetOnRelease(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONRELEASE, &request);
}

static int
CS2VM2_Op_CC_SetOnDialogAbort(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONDIALOGABORT, &request);
}

static int
CS2VM2_Op_CC_SetOnFriendTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONFRIENDTRANSMIT, &request);
}

int
CS2VM2_Op_CC_SetOnMouseRepeat(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT, &request);
}

int
CS2VM2_Op_CC_SetOnDrag(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONDRAG, &request);
}

int
CS2VM2_Op_CC_SetOnScrollWheel(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL, &request);
}

int
CS2VM2_Op_CC_SetOnKey(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONKEY, &request);
}

/* CC_SETONKEYDOWN / CC_SETONKEYUP (1430/1431) — see
 * CS2VM_HOST_REQUEST_CC_SETONKEYDOWN for why the vendor names are wrong. */
int
CS2VM2_Op_CC_SetOnKeyDown(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONKEYDOWN, &request);
}

int
CS2VM2_Op_CC_SetOnKeyUp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONKEYUP, &request);
}

int
CS2VM2_Op_CC_SetOnOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONOP, &request);
}

int
CS2VM2_Op_CC_SetOnDragComplete(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE, &request);
}

int
CS2VM2_Op_CC_SetOnResize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONRESIZE, &request);
}

int
CS2VM2_Op_CC_SetOnSubChange(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE, &request);
}

int
CS2VM2_Op_IF_SetDraggable(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int child_index;
    int parent_uid;
    int component_id;

    /* Stack (bottom to top): [parentUid, childIndex, uid] — pop in reverse. */
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &parent_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETDRAGGABLE;
    request.u.cc_set_draggable.component_id = component_id;
    request.u.cc_set_draggable.parent_uid = parent_uid;
    request.u.cc_set_draggable.child_index = child_index;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_SetDraggableBehavior(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int behavior;
    int component_id;
    if( CS2VM2_PopInt(vm, &behavior) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETDRAGGABLEBEHAVIOR;
    request.u.cc_set_draggable_behavior.component_id = component_id;
    request.u.cc_set_draggable_behavior.behavior = behavior;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_DragPickup(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_HostRequestKind kind)
{
    int pickup_x;
    int pickup_y;
    int component_id;

    assert(vm);
    assert(frame);
    (void)frame;

    /* IF_DRAGPICKUP stack (bottom→top): [x, y, component]
     * CC_DRAGPICKUP stack (bottom→top): [x, y] — target is active/dot.
     * Reference ScriptRunner 3108/3109. */
    if( kind == CS2VM_HOST_REQUEST_IF_DRAGPICKUP )
    {
        if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    else
        component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    if( CS2VM2_PopInt(vm, &pickup_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &pickup_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.drag_pickup.component_id = component_id;
    request.u.drag_pickup.pickup_x = pickup_x;
    request.u.drag_pickup.pickup_y = pickup_y;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_SetAntiDrag(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_SETANTIDRAG;
    request.u.widget_set_int.value = value;
    return vm->vm->host_exec(vm, &request);
}

static int
cs2vm2_op_if_set_on_transmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_HostRequestKind kind)
{
    assert(vm);
    assert(frame);

    /*
     * `trigger_count = 0` is load-bearing, not tidiness.
     *
     * Only a signature ending in 'Y' carries a trigger list; without one this
     * variable is never written, and it used to be handed to the host anyway —
     * stack garbage as a count, alongside a NULL `trigger_ids`. The host clamps
     * the count to its own ceiling and leaves the ids at zero, so the hook ends
     * up filtered to "stat/varp id 0 only".
     *
     * What that cost: the XP-drop listener (`script1003`'s
     * `if_setonstattransmit`, which carries NO trigger list) matched stat 0 —
     * attack. Combat that trained attack still drew drops; cooking, prayer,
     * every non-attack skill was silently filtered out of the dispatch, and
     * because the value was uninitialised the same session could behave
     * differently from one login to the next.
     */
    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;

    char* signature = NULL;

    if( CS2VM2_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VM2_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VM2_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            assert(trigger_ids);

            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VM2_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    int int_args[CS2VM_SETON_INT_ARG_MAX] = { 0 };
    int int_arg_count = 0;
    char* str_by_pos[CS2VM_SETON_INT_ARG_MAX] = { 0 };
    uint64_t str_arg_mask = 0;

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VM2_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                /* Past str_by_pos' 32 slots the value is dropped; the pool owns it. */
                if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                {
                    str_by_pos[i] = v;
                    str_arg_mask |= (uint64_t)1 << i;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
            }
            else
            {
                int v = 0;
                if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                if( i < (int)(sizeof(int_args) / sizeof(int_args[0])) )
                {
                    int_args[i] = v;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
            }
        }
    }

    if( CS2VM2_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        if( getenv("TORIRS_VAR_HOOK_DEBUG") )
            fprintf(
                stderr,
                "VARHOOKOP kind=%d com=0x%08x bail=script_id\n",
                (int)kind,
                (unsigned)widget_uid);
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    /* script -1 is a disarm, not a no-op — see CS2VM2_Op_IF_SetOnEventHandler.
     * The host clears the registry entry instead of acquiring one. */

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.if_set_on_var_transmit.component_id = widget_uid;
    request.u.if_set_on_var_transmit.script_id = script_id;
    request.u.if_set_on_var_transmit.signature = signature;
    request.u.if_set_on_var_transmit.trigger_ids = trigger_ids;
    request.u.if_set_on_var_transmit.trigger_count = trigger_count;
    memcpy(
        request.u.if_set_on_var_transmit.int_args,
        int_args,
        sizeof(request.u.if_set_on_var_transmit.int_args));
    request.u.if_set_on_var_transmit.int_arg_count = int_arg_count;
    request.u.if_set_on_var_transmit.str_arg_mask = str_arg_mask;
    for( int i = 0; i < CS2VM_SETON_INT_ARG_MAX; i++ )
    {
        if( !(str_arg_mask & ((uint64_t)1 << i)) )
            continue;
        if( request.u.if_set_on_var_transmit.str_arg_count < CS2VM_SETON_STR_ARG_MAX )
        {
            char* dst = request.u.if_set_on_var_transmit
                            .str_args[request.u.if_set_on_var_transmit.str_arg_count];
            strncpy(dst, str_by_pos[i] ? str_by_pos[i] : "", CS2VM_SETON_STR_ARG_LEN - 1);
            dst[CS2VM_SETON_STR_ARG_LEN - 1] = '\0';
        }
        request.u.if_set_on_var_transmit.str_arg_count++;
    }
    if( request.u.if_set_on_var_transmit.str_arg_count > CS2VM_SETON_STR_ARG_MAX )
        request.u.if_set_on_var_transmit.str_arg_count = CS2VM_SETON_STR_ARG_MAX;

    int result = vm->vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetOnVarTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    return cs2vm2_op_if_set_on_transmit(
        vm, frame, operand, CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT);
}

int
CS2VM2_Op_IF_SetOnStatTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    return cs2vm2_op_if_set_on_transmit(
        vm, frame, operand, CS2VM_HOST_REQUEST_IF_SETONSTATTRANSMIT);
}

int
CS2VM2_Op_IF_SetOnInvTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VM2_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VM2_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            assert(trigger_ids);

            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VM2_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    int int_args[CS2VM_SETON_INT_ARG_MAX] = { 0 };
    int int_arg_count = 0;
    char* str_by_pos[CS2VM_SETON_INT_ARG_MAX] = { 0 };
    uint64_t str_arg_mask = 0;

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VM2_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                /* Past str_by_pos' 32 slots the value is dropped; the pool owns it. */
                if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                {
                    str_by_pos[i] = v;
                    str_arg_mask |= (uint64_t)1 << i;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
            }
            else
            {
                int v = 0;
                if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                if( i < (int)(sizeof(int_args) / sizeof(int_args[0])) )
                {
                    int_args[i] = v;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
            }
        }
    }

    if( CS2VM2_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    /* script -1 is a disarm, not a no-op — see CS2VM2_Op_IF_SetOnEventHandler.
     * The host clears the registry entry instead of acquiring one. */

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT;
    request.u.if_set_on_inv_transmit.component_id = widget_uid;
    request.u.if_set_on_inv_transmit.script_id = script_id;
    request.u.if_set_on_inv_transmit.signature = signature;
    request.u.if_set_on_inv_transmit.trigger_ids = trigger_ids;
    request.u.if_set_on_inv_transmit.trigger_count = trigger_count;
    memcpy(
        request.u.if_set_on_inv_transmit.int_args,
        int_args,
        sizeof(request.u.if_set_on_inv_transmit.int_args));
    request.u.if_set_on_inv_transmit.int_arg_count = int_arg_count;
    request.u.if_set_on_inv_transmit.str_arg_mask = str_arg_mask;
    for( int i = 0; i < CS2VM_SETON_INT_ARG_MAX; i++ )
    {
        if( !(str_arg_mask & ((uint64_t)1 << i)) )
            continue;
        if( request.u.if_set_on_inv_transmit.str_arg_count < CS2VM_SETON_STR_ARG_MAX )
        {
            char* dst = request.u.if_set_on_inv_transmit
                            .str_args[request.u.if_set_on_inv_transmit.str_arg_count];
            strncpy(dst, str_by_pos[i] ? str_by_pos[i] : "", CS2VM_SETON_STR_ARG_LEN - 1);
            dst[CS2VM_SETON_STR_ARG_LEN - 1] = '\0';
        }
        request.u.if_set_on_inv_transmit.str_arg_count++;
    }
    if( request.u.if_set_on_inv_transmit.str_arg_count > CS2VM_SETON_STR_ARG_MAX )
        request.u.if_set_on_inv_transmit.str_arg_count = CS2VM_SETON_STR_ARG_MAX;

    int result = vm->vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetOnOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONOP, &request);
}

int
CS2VM2_Op_IF_SetOnClick(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONCLICK, &request);
}

int
CS2VM2_Op_IF_SetOnHold(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONHOLD, &request);
}

int
CS2VM2_Op_IF_SetOnMouseOver(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER, &request);
}

int
CS2VM2_Op_IF_SetOnMouseLeave(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE, &request);
}

static int
CS2VM2_Op_IF_SetOnTargetEnter(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_IF_SetOnOp request;
    (void)operand;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONTARGETENTER, &request);
}

static int
CS2VM2_Op_IF_SetOnTargetLeave(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_IF_SetOnOp request;
    (void)operand;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONTARGETLEAVE, &request);
}

static int
CS2VM2_Op_IF_SetOnClickRepeat(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_IF_SetOnOp request;
    (void)operand;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONCLICKREPEAT, &request);
}

static int
CS2VM2_Op_IF_SetOnRelease(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_IF_SetOnOp request;
    (void)operand;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONRELEASE, &request);
}

static int
CS2VM2_Op_IF_SetOnDialogAbort(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_IF_SetOnOp request;
    (void)operand;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONDIALOGABORT, &request);
}

int
CS2VM2_Op_IF_SetOnDrag(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONDRAG, &request);
}

int
CS2VM2_Op_IF_SetOnDragComplete(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE, &request);
}

int
CS2VM2_Op_IF_SetOnResize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONRESIZE, &request);
}

int
CS2VM2_Op_IF_SetOnSubChange(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE, &request);
}

int
CS2VM2_Op_IF_SetOnMouseRepeat(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT, &request);
}

int
CS2VM2_Op_IF_SetOnTimer(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(vm, frame, CS2VM_HOST_REQUEST_IF_SETONTIMER, &request);
}

int
CS2VM2_Op_IF_SetOnScrollWheel(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL, &request);
}

int
CS2VM2_Op_IF_SetOnKey(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(vm, frame, CS2VM_HOST_REQUEST_IF_SETONKEY, &request);
}

/* IF_SETONKEYDOWN / IF_SETONKEYUP (2430/2431), the IF twins of the CC pair. */
int
CS2VM2_Op_IF_SetOnKeyDown(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONKEYDOWN, &request);
}

int
CS2VM2_Op_IF_SetOnKeyUp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONKEYUP, &request);
}

int
CS2VM2_Op_IF_SetOnMiscTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT, &request);
}

/*
 * IF_SETONFRIENDTRANSMIT (2420). Structurally identical to the misc-transmit
 * registration above and for the same reason: the op takes no trigger list, so
 * one dirty flag re-runs every registered hook.
 *
 * This is the *whole* reactive path for the friends and ignore panels. Their
 * onloads (scripts 123 and 127) end with exactly two registrations — this one
 * and an if_setonvartransmit on varp 1737 — and there is no varbit, no
 * RUNCLIENTSCRIPT and no server repaint packet behind either panel. Discarding
 * this registration (which is what happened before) meant the list painted once
 * at mount, against whatever the store held at that instant, and never again.
 */
int
CS2VM2_Op_IF_SetOnFriendTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VM2_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONFRIENDTRANSMIT, &request);
}

int
CS2VM2_Op_IF_SetOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int widget, index;
    char* text;

    if( CS2VM2_PopInt(vm, &widget) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOP;
    request.u.if_set_op.component_id = widget;
    request.u.if_set_op.index = index;
    request.u.if_set_op.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetOpBase(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    char* text;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOPBASE;
    request.u.if_set_op_base.component_id = component_id;
    request.u.if_set_op_base.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

static int
CS2VM2_Op_IF_SetTargetVerb(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    int component_id;
    char* text;

    assert(vm);
    assert(frame);
    (void)operand;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK ||
        CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request = { 0 };
    request.kind = CS2VM_HOST_REQUEST_IF_SETTARGETVERB;
    request.u.if_set_target_verb.component_id = component_id;
    request.u.if_set_target_verb.text = text;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_SetOpSubmenu(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, sub_index, op_index;
    char* text;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &sub_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOPSUBMENU;
    request.u.if_set_op_submenu.component_id = component_id;
    request.u.if_set_op_submenu.sub_index = sub_index;
    request.u.if_set_op_submenu.op_index = op_index;
    request.u.if_set_op_submenu.text = text;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetTargetPriority(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int priority, component_id;
    if( CS2VM2_PopInt(vm, &priority) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY;
    request.u.if_set_target_priority.component_id = component_id;
    request.u.if_set_target_priority.priority = priority;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_ClearOps(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CLEAROPS;
    request.u.if_clear_ops.component_id = component_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/*
 * IF_CALLONRESIZE(component) — run that component's on-resize listener.
 *
 * One int in, nothing out, read off the bytecode rather than inferred:
 *
 *     pc=23 PUSH_INT_LOCAL 2        <- the component
 *     pc=24 IF_CALLONRESIZE
 *     pc=25 RETURN
 *
 * (script 1911, the skill guide's window setup). All seventeen call sites in
 * cache.osrs239 have the same shape.
 */
int
CS2VM2_Op_IF_CallOnResize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CALLONRESIZE;
    request.u.if_call_on_resize.component_id = component_id;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_Add(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, intpop_a + intpop_b);
}

int
CS2VM2_Op_Sub(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, intpop_a - intpop_b);
}

int
CS2VM2_Op_Mul(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, intpop_a * intpop_b);
}

/* Bitwise / arithmetic ops mirroring the reference client (xrsps MathOps). Each
 * pops its operands (top-of-stack listed first) and pushes one int. */

int
CS2VM2_Op_And(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int a, b;
    if( CS2VM2_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, a & b);
}

int
CS2VM2_Op_Min(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int a, b;
    if( CS2VM2_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, a < b ? a : b);
}

int
CS2VM2_Op_Max(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int a, b;
    if( CS2VM2_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, a > b ? a : b);
}

/* value + value * percent / 100. The multiply is done in 64-bit to match the
 * reference's exact (non-overflowing) arithmetic; C truncation toward zero on
 * the divide matches the reference truncation. */
int
CS2VM2_Op_AddPercent(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int value, percent;
    if( CS2VM2_PopInt(vm, &percent) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, value + (int)(((int64_t)value * percent) / 100));
}

int
CS2VM2_Op_BitCount(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, __builtin_popcount((unsigned)value));
}

/**
 * ABS (4035): pop one int, push its magnitude.
 *
 * A pure-VM op with no host state, and it had no case here at all — it went to
 * StackMetaStub, which before the cs2_command.gen.h bridge asserted and after
 * it would have pushed 0 for every input. The one path in cache.osrs239 that
 * reaches it is [proc,script5380], the XP-tracker's actions-to-goal estimator
 * (5448 -> 5449 -> 5362 -> 5366 -> 5375 -> 5380), where "abs(x) is always 0"
 * would read as a plausible-but-wrong number on screen rather than a crash.
 *
 * INT_MIN has no positive counterpart in two's complement; negating it is UB in
 * C. The Java client this is ported from has the same wrap (`-INT_MIN ==
 * INT_MIN`), so reproduce it explicitly rather than invoking UB.
 */
static int
CS2VM2_Op_Abs(struct CS2VM2_Thread* vm)
{
    assert(vm);

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( value < 0 )
        value = (int)(0u - (unsigned)value);
    return CS2VM2_PushInt(vm, value);
}

int
CS2VM2_Op_ToggleBit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int bit, value;
    if( CS2VM2_PopInt(vm, &bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, value ^ (1 << bit));
}

/* Contiguous-bit-range mask helper: bits [low, high] inclusive. */
static inline int
cs2_bit_range_mask(
    int low,
    int high)
{
    return ((1 << (high - low + 1)) - 1) << low;
}

int
CS2VM2_Op_SetBitRange(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int low, high, value;
    if( CS2VM2_PopInt(vm, &high) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &low) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, value | cs2_bit_range_mask(low, high));
}

int
CS2VM2_Op_ClearBitRange(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int low, high, value;
    if( CS2VM2_PopInt(vm, &high) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &low) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, value & ~cs2_bit_range_mask(low, high));
}

int
CS2VM2_Op_GetBitRange(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int low, high, value;
    if( CS2VM2_PopInt(vm, &high) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &low) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, (value >> low) & ((1 << (high - low + 1)) - 1));
}

/* Clear bits [low, high] of value, then write newBits (clamped to the range
 * width) shifted into that position. Stack (top first): high, low, newBits, value. */
int
CS2VM2_Op_SetBitRangeValue(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int high, low, new_bits, value;
    if( CS2VM2_PopInt(vm, &high) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &low) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &new_bits) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int max_value = (1 << (high - low + 1)) - 1;
    int clamped = new_bits > max_value ? max_value : new_bits;
    return CS2VM2_PushInt(vm, (value & ~cs2_bit_range_mask(low, high)) | (clamped << low));
}

int
CS2VM2_Op_Div(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int intpop_b, intpop_a;

    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_b == 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, intpop_a / intpop_b);
}

int
CS2VM2_Op_Mod(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;
    int intpop_b, intpop_a;

    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_b == 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, intpop_a % intpop_b);
}

int
CS2VM2_Op_Scale(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int c, b, a;

    if( CS2VM2_PopInt(vm, &c) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = 0;
    if( b != 0 )
        result = (int)(((int64_t)c * (int64_t)a) / (int64_t)b);

    return CS2VM2_PushInt(vm, result);
}

int
CS2VM2_Op_Pow(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int intpop_b, intpop_a;

    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, (int)pow((double)intpop_a, (double)intpop_b));
}

int
CS2VM2_Op_PopIntDiscard(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    /* One value, and the operand byte is NOT a count.
     *
     * 38/39 are two of the three opcodes (with RETURN) the loader reads a
     * single BYTE operand for rather than an int, and the reference
     * interpreter's whole body for them is `--isp` / `--ssp`. Reading that byte
     * as a repeat count made the op a no-op on every call site in every cache
     * here — all 1,588 discards in cache.osrs239, all 172 in cache.osrs184,
     * carry operand 0 — so every proc whose return values a caller discarded
     * leaked them. The generated stack table has said (1 int in, 0 out) all
     * along; this is the handler catching up to it.
     *
     * It surfaces as a *full* operand stack far from the leak: the skill guide's
     * Overview builds one text widget per word (script 9187) and discards two
     * ints per word, so ~500 words of the Crafting page filled all 1024 slots
     * and the next push failed inside script 8303.
     */
    (void)operand;

    int intpop;
    return CS2VM2_PopInt(vm, &intpop);
}

int
CS2VM2_Op_PopStrDiscard(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    /* One value; see CS2VM2_Op_PopIntDiscard for why the operand is not a
     * count. */
    (void)operand;

    char* strpop;
    return CS2VM2_PopStr(vm, &strpop);
}

int
CS2VM2_Op_Enum(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int key, enum_id, output_type, input_type;

    if( CS2VM2_PopInt(vm, &key) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &enum_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &output_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &input_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP;
    request.u.enum_lookup.input_type = input_type;
    request.u.enum_lookup.output_type = output_type;
    request.u.enum_lookup.enum_id = enum_id;
    request.u.enum_lookup.key = key;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;


    return CS2VM_EXECNO_OK;
}

/* enum_string(enum, key)(string) — key then enum_id on int stack (top = key). */
int
CS2VM2_Op_EnumString(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int key, enum_id;
    if( CS2VM2_PopInt(vm, &key) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &enum_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP;
    request.u.enum_lookup.input_type = (int)'i';
    request.u.enum_lookup.output_type = (int)'s';
    request.u.enum_lookup.enum_id = enum_id;
    request.u.enum_lookup.key = key;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_EnumGetOutputCount(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int enum_id;
    if( CS2VM2_PopInt(vm, &enum_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT;
    request.u.enum_get_output_count.enum_id = enum_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/* DB_* opcodes (7500..7510). The host owns all stack manipulation: DB_FIND must
 * defer popping its search value until the table index reveals the value's type,
 * so the VM op just forwards the opcode and lets exec_db drive the stack. */
int
CS2VM2_Op_Db(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    struct CS2VM_HostRequest request;

    assert(vm);

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_DB;
    request.u.db.opcode = opcode;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IsMapMembers(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // Hardcode yes

    return CS2VM2_PushInt(vm, 1);
}

int
CS2VM2_Op_OnMobile(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VM2_PushInt(vm, 0);
}

/* Rev 634 opcode 6910: push Class24.anInt359 (signed 24-bit login / packet-54
 * field). No login session here, so push the static default of 0. */
static int
CS2VM2_Op_LoginInt24(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VM2_PushInt(vm, 0);
}

int
CS2VM2_Op_GetCanvasSize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( CS2VM2_PushInt(vm, vm->canvas_w) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, vm->canvas_h);
}

int
CS2VM2_Op_GetWindowMode(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    /* Host state, snapshotted per thread. It used to push the literal 2, which
     * made [clientscript,settings_client_mode] believe the client was already
     * resizable no matter what SETWINDOWMODE had done — the script's own
     * "already in this mode, nothing to do" branch. 0 = the host never set it;
     * resizable is the boot mode this client actually comes up in. */
    return CS2VM2_PushInt(
        vm, vm->window_mode > 0 ? vm->window_mode : CS2VM_WINDOW_MODE_RESIZABLE);
}

int
CS2VM2_Op_GetDefaultWindowMode(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VM2_PushInt(
        vm,
        vm->default_window_mode > 0 ? vm->default_window_mode : CS2VM_WINDOW_MODE_RESIZABLE);
}

/* SETWINDOWMODE (5307) / SETDEFAULTWINDOWMODE (5309). Pop the mode, tell the
 * host (which owns the canvas and the window), and mirror it into this thread
 * so a script that sets then re-reads inside one run — 3998 does exactly that
 * across setwindowmode/getdefaultwindowmode — sees its own write. */
static int
CS2VM2_Op_SetWindowMode(
    struct CS2VM2_Thread* vm,
    bool is_default)
{
    struct CS2VM_HostRequest request;
    int mode;

    assert(vm);
    if( CS2VM2_PopInt(vm, &mode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( is_default )
        vm->default_window_mode = mode;
    else
        vm->window_mode = mode;

    memset(&request, 0, sizeof(request));
    request.kind = is_default ? CS2VM_HOST_REQUEST_SET_DEFAULT_WINDOW_MODE
                              : CS2VM_HOST_REQUEST_SET_WINDOW_MODE;
    request.u.window_mode.mode = mode;
    return vm->vm->host_exec(vm, &request);
}

/* COORD returns the local player's packed world coordinate; it does not pop from the
 * stack. This offline renderer has no player position, so it pushes a fixed dummy coord
 * (plane 0, x 0, y 0) — good enough to keep script-local stack balance correct. */
int
CS2VM2_Op_Coord(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest request;

    assert(vm);
    assert(frame);
    (void)operand;

    /*
     * THE LOCAL PLAYER'S OWN TILE, packed the way `coordx`/`coordy`/`coordz`
     * unpack it: plane in bits 28-29, x in 14-27, z in 0-13.
     *
     * This used to push a literal 0, and a zero coord is not a harmless
     * placeholder — it is a real tile, in the corner of the map, that no script
     * comparing against a region ever matches. Every cache script that decides
     * what to show from where the player is standing therefore took its "not
     * here" branch.
     *
     * The Theatre of Blood's HUD is the case that found it. Script 2297 reads
     * `coord`, compares it against the raid's regions and `if_sethide`s the
     * boss health bar when it does not match — so the bar was mounted, its
     * listener armed, its updater running on every varbit change, and hidden on
     * every tick, in every room of the raid.
     */
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_COORD;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CoordX(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int packed;
    if( CS2VM2_PopInt(vm, &packed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, (packed >> 14) & 0x3fff);
}

int
CS2VM2_Op_CoordY(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    /* "y" is the plane, not the second tile axis — RuneScript names the axes
     * x/y/z with y as the level. Script 1715 (world map coord search) walks
     * planes with coordy()/movecoord() and only terminates with this reading. */
    int packed;
    if( CS2VM2_PopInt(vm, &packed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, (packed >> 28) & 0x3);
}

int
CS2VM2_Op_CoordZ(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int packed;
    if( CS2VM2_PopInt(vm, &packed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, packed & 0x3fff);
}

int
CS2VM2_Op_MoveCoord(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int packed;
    int off_x;
    int off_plane;
    int off_z;

    /* Args are (x, y, z) in RuneScript order, where y is the plane. Offsets add
     * in packed space, so a tile offset carries into the field above it exactly
     * as the reference client's arithmetic does. */
    if( CS2VM2_PopInt(vm, &off_z) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &off_plane) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &off_x) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &packed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM2_PushInt(vm, packed + ((off_plane << 28) | (off_x << 14) | off_z));
}

int
CS2VM2_Op_ClientType(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VM2_PushInt(vm, 10);
}

int
CS2VM2_Op_RunWeightVisible(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VM2_PushInt(vm, 0);
}

// IF_ICMPGT
int
CS2VM2_Op_BranchGreaterThan(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a > b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a > intpop_b )
        return CS2VM2_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_BranchLessThan(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a < b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a < intpop_b )
        return CS2VM2_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_BranchLessThanOrEquals(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int intpop_b, intpop_a;
    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a <= intpop_b )
        return CS2VM2_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_BranchGreaterThanOrEquals(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int intpop_b, intpop_a;
    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a >= intpop_b )
        return CS2VM2_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_BranchEquals(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a === b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a == intpop_b )
        return CS2VM2_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_BranchNotEquals(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const cond = ctx.intStack[--ctx.intStackSize];
    // if( cond === 0 )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VM2_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a != intpop_b )
        return CS2VM2_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_Branch(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VM2_JumpRelative(vm, frame, operand);
}

/* RS2-era (rev 634) opcode 86: pop int, branch by operand if value == 1. */
static int
CS2VM2_Op_BranchIfOne(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( value == 1 )
        return CS2VM2_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_Switch(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const key = this.intStack[--this.intStackSize];
    // const table = switches ? switches[intOp] : undefined;
    // if (table && table.has(key)) {
    //     pc += table.get(key)!;
    // }

    int key;
    if( CS2VM2_PopInt(vm, &key) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( operand < 0 || operand >= frame->script->switch_table_count )
        return CS2VM_EXECNO_OK;

    struct CS2VM2_ScriptSwitch const* sw = &frame->script->switch_tables[operand];
    for( int i = 0; i < sw->case_count; i++ )
    {
        if( sw->cases[i].key == key )
            return CS2VM2_JumpRelative(vm, frame, sw->cases[i].target_pc);
    }

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_TestBit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const bit = ctx.intStack[--ctx.intStackSize];
    // const value = ctx.intStack[--ctx.intStackSize];
    // ctx.pushInt((value & (1 << bit)) !== 0 ? 1 : 0);

    int intpop_bit, intpop_value;
    if( CS2VM2_PopInt(vm, &intpop_bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &intpop_value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, (intpop_value & (1 << intpop_bit)) != 0 ? 1 : 0);
}

static int
CS2VM2_ArrayDefineSlot(int operand)
{
    return operand >> 16;
}

int
CS2VM2_Op_PopVar(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_WRITE_VARP_AKA_POP_VAR;
    request.u.vars_write_varp.varp_id = operand;
    request.u.vars_write_varp.value = value;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_PopVarbit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_WRITE_VARBIT;
    request.u.vars_write_varbit.varbit_id = operand;
    request.u.vars_write_varbit.value = value;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_DefineArray(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int size;
    if( CS2VM2_PopInt(vm, &size) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    /* High half of the operand is the STRING LOCAL that receives the array's
     * handle — not a global array id. Allocate from the pool and park the
     * pointer in that local; every later PUSH/POP_ARRAY_INT resolves its own
     * frame's local, which is what lets a proc receive an array as an
     * ordinary string argument (the spellbook sort does). */
    int const str_slot = CS2VM2_ArrayDefineSlot(operand);
    if( str_slot < 0 || str_slot >= CS2VM_MAX_LOCALS )
        return CS2VM_EXECNO_OK;

    if( vm->array_alloc >= CS2VM2_MAX_ARRAYS )
    {
        fprintf(
            stderr,
            "CS2VM2: array pool exhausted (%d) in script %d\n",
            CS2VM2_MAX_ARRAYS,
            frame->script ? frame->script->script_id : -1);
        vm->array_alloc = CS2VM2_MAX_ARRAYS - 1; /* fail soft: reuse the last */
    }
    struct CS2VM2_Array* array = &vm->arrays[vm->array_alloc++];

    /* Low half of the operand is the RuneScript element-type char; 's' is the
     * only one that lives on the string stack. The rest are the RuneScript base
     * types (`i` 105 int, `Ð` 208 dbrow, …) and all share the int stack.
     *
     * A fresh array's cells are **-1, not 0**, and that is not cosmetic. -1 is
     * `null` for every reference-typed RuneScript base type, so a script that
     * fills an array and guards each slot with `= null` is testing a sentinel —
     * and 0 is a perfectly good dbrow / component / obj id.
     *
     * `[clientscript,script1090]`, the builder behind all three of Slayer
     * Rewards' catalogue tabs, is the witness, and it fails closed:
     *
     *     def_dbrow $rows($n);
     *     while ($row ! null) {
     *         if ($rows($i) = null) { $rows($i) = $row; ... }
     *         else { ~error("Multiple overlapping reward ids …"); return; }
     *     }
     *
     * With zeroed cells the very first slot compares unequal to null, the
     * script takes the error branch on iteration zero and returns, and three
     * tabs of a panel that mounted perfectly draw nothing at all — with nothing
     * logged, because `db_find` had already answered with its 24 rows.
     *
     * Strings stay NULL rather than "": the reference fills them with the empty
     * string, but no script in this cache reads an unwritten string cell, so
     * there is nothing here to verify the change against and every reader
     * already treats NULL as "". */
    if( cs2vm2_array_begin(array, size, (operand & 0xffff) == 's') )
    {
        if( array->is_string )
            memset(array->cells.strings, 0, (size_t)array->size * sizeof(char*));
        else
            for( int i = 0; i < array->size; i++ )
                array->cells.ints[i] = -1;
    }
    frame->str_locals[str_slot] = (char*)array;
    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_PushArrayInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int index;
    if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM2_Array* array = cs2vm2_array_local(vm, frame, operand);
    int const in_range = array && index >= 0 && index < array->size;

    /* A string array reads onto the string stack — same opcode, other stack. */
    if( array && array->is_string )
    {
        char* value = in_range ? array->cells.strings[index] : NULL;
        return CS2VM2_PushStr(vm, value ? value : CS2VM2_StrEmpty(vm));
    }

    return CS2VM2_PushInt(vm, in_range ? array->cells.ints[index] : 0);
}

int
CS2VM2_Op_PopArrayInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    /* `$array($index) = $value` compiles to: push index, push value, POP_ARRAY_INT
     * — so the VALUE is on top and the INDEX sits beneath it. Popping these in the
     * wrong order transposes them, which is invisible whenever index == value (the
     * spellbook's `$visible_indices($n) = $n` fill) and silently scrambles every
     * asymmetric write (the spellbook sort's in-place swaps). */
    int index;
    int value;

    struct CS2VM2_Array* array = cs2vm2_array_local(vm, frame, operand);

    /* A string array's value comes off the STRING stack; popping an int for it
     * underflows the int stack and desyncs everything after. */
    if( array && array->is_string )
    {
        char* text = NULL;
        if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        CS2VM2_ArrayStoreStr(vm, array, index, text);
        return CS2VM_EXECNO_OK;
    }

    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    /* Tracked store: records the prior cell value in the per-op undo log so a
     * yield inside this op cannot leave the write half-applied on replay. */
    CS2VM2_ArrayStore(vm, array, index, value);

    return CS2VM_EXECNO_OK;
}

/*
 * Opcode 8000 — sort two paired arrays by the first.
 *
 * Pops two array HANDLES off the string stack: secondary on top, primary
 * beneath (the questlist pushes names then ids; xrsps VarOps 8000 is
 * `primary.sortAllWith(secondary)`). Sorts primary ascending — strcmp for a
 * string array, numeric otherwise — and applies the same permutation to
 * secondary so the pairing survives. The older deob signature recorded in the
 * decompiler's tables (one int, numbered-global arrays) predates handles and
 * is wrong for this revision. No host call can occur mid-op, so the writes
 * need no undo tracking. Insertion sort: the biggest caller is the 213-row
 * questlist.
 */
int
CS2VM2_Op_ArraySortAll(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    char* secondary_handle = NULL;
    char* primary_handle = NULL;
    if( CS2VM2_PopStr(vm, &secondary_handle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &primary_handle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM2_Array* primary = cs2vm2_array_from_handle(vm, primary_handle);
    struct CS2VM2_Array* secondary = cs2vm2_array_from_handle(vm, secondary_handle);
    if( !primary )
        return CS2VM_EXECNO_OK;
    int const paired = secondary && secondary->size >= primary->size;

    for( int i = 1; i < primary->size; i++ )
    {
        for( int j = i; j > 0; j-- )
        {
            int before;
            if( primary->is_string )
            {
                char const* a = primary->cells.strings[j - 1];
                char const* b = primary->cells.strings[j];
                before = strcmp(a ? a : "", b ? b : "") <= 0;
            }
            else
                before = primary->cells.ints[j - 1] <= primary->cells.ints[j];
            if( before )
                break;
            /* cells is a union — swap through the ACTIVE member only, a
             * char*-wide swap on an int array would clobber two int cells. */
            if( primary->is_string )
            {
                char* tmp = primary->cells.strings[j - 1];
                primary->cells.strings[j - 1] = primary->cells.strings[j];
                primary->cells.strings[j] = tmp;
            }
            else
            {
                int tmp = primary->cells.ints[j - 1];
                primary->cells.ints[j - 1] = primary->cells.ints[j];
                primary->cells.ints[j] = tmp;
            }
            if( paired )
            {
                if( secondary->is_string )
                {
                    char* tmp = secondary->cells.strings[j - 1];
                    secondary->cells.strings[j - 1] = secondary->cells.strings[j];
                    secondary->cells.strings[j] = tmp;
                }
                else
                {
                    int tmp = secondary->cells.ints[j - 1];
                    secondary->cells.ints[j - 1] = secondary->cells.ints[j];
                    secondary->cells.ints[j] = tmp;
                }
            }
        }
    }

    return CS2VM_EXECNO_OK;
}

/*
 * Opcode 8007 — count the cells in [start, end) equal to a value.
 *
 * Stack, top first: valueType, end, start; then the search value, taken from
 * the STRING stack when valueType names a string (2 or 115) and from the int
 * stack when it names an int (0, 49, 105); then the array HANDLE off the
 * string stack. `end` < 0 — or past the end — means "to the end", and
 * `start` < 0 clamps to 0. A null handle pushes 0 rather than failing.
 *
 * The type code is on the stack rather than in the operand because one opcode
 * serves both array flavours; reading it as the value (or popping the value
 * from the wrong stack) desynchronises both stacks and surfaces much later.
 * Reference: xrsps VarOps ARRAY_COUNT_MATCHES + Cs2ArrayObject.countMatches.
 *
 * This was the one unimplemented opcode the boot actually reaches — 341 times
 * per run, silently, because the stub returned OK for anything past the
 * generated table. See CS2VM2_Op_StackMetaStub.
 */
int
CS2VM2_Op_ArrayCountMatches(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    int start;
    int end;
    int value_type;
    if( CS2VM2_PopInt(vm, &value_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &end) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int want_int = 0;
    int want_str = 0;
    if( value_type == 2 || value_type == 115 )
        want_str = 1;
    else if( value_type == 0 || value_type == 49 || value_type == 105 )
        want_int = 1;
    /* value_type -1 is "no value": nothing is popped and nothing can match. */

    int search_int = 0;
    char* search_str = NULL;
    if( want_int && CS2VM2_PopInt(vm, &search_int) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( want_str && CS2VM2_PopStr(vm, &search_str) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    char* handle = NULL;
    if( CS2VM2_PopStr(vm, &handle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM2_Array* array = cs2vm2_array_from_handle(vm, handle);
    if( !array )
        return CS2VM2_PushInt(vm, 0);

    int first = start < 0 ? 0 : start;
    int last = (end < 0 || end > array->size) ? array->size : end;
    int matches = 0;

    for( int i = first; i < last; i++ )
    {
        if( array->is_string )
        {
            char const* cell = array->cells.strings[i];
            if( want_str && strcmp(cell ? cell : "", search_str ? search_str : "") == 0 )
                matches++;
        }
        else if( want_int && array->cells.ints[i] == search_int )
        {
            matches++;
        }
    }

    return CS2VM2_PushInt(vm, matches);
}

/*
 * Opcode 8003 — ARRAY_LENGTH. Handle on the string stack -> element count.
 * Null handle pushes 0 (xrsps VarOps ARRAY_LENGTH).
 */
int
CS2VM2_Op_ArrayLength(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    char* handle = NULL;
    if( CS2VM2_PopStr(vm, &handle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    struct CS2VM2_Array* array = cs2vm2_array_from_handle(vm, handle);
    return CS2VM2_PushInt(vm, array ? array->size : 0);
}

/*
 * Opcode 8019 — ARRAY_JOIN. (handle, separator) -> joined string.
 * Reference: xrsps VarOps ARRAY_JOIN. Call-site correction: local_commands
 * previously recorded no string out; script 9153's gosub 9182 needs the push.
 */
int
CS2VM2_Op_ArrayJoin(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    char* separator = NULL;
    char* handle = NULL;
    if( CS2VM2_PopStr(vm, &separator) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &handle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM2_Array* array = cs2vm2_array_from_handle(vm, handle);
    if( !array )
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));

    char const* sep = separator ? separator : "";
    size_t sep_len = strlen(sep);
    size_t total = 1; /* NUL */
    for( int i = 0; i < array->size; i++ )
    {
        if( i > 0 )
            total += sep_len;
        if( array->is_string )
        {
            char const* cell = array->cells.strings[i];
            total += strlen(cell ? cell : "");
        }
        else
        {
            char buf[16];
            total += (size_t)snprintf(buf, sizeof(buf), "%d", array->cells.ints[i]);
        }
    }

    char* out = CS2VM2_StrAlloc(vm, total);
    if( !out )
        return CS2VM_EXECNO_ERROR;
    size_t off = 0;
    for( int i = 0; i < array->size; i++ )
    {
        if( i > 0 && sep_len > 0 )
        {
            memcpy(out + off, sep, sep_len);
            off += sep_len;
        }
        if( array->is_string )
        {
            char const* cell = array->cells.strings[i];
            size_t n = strlen(cell ? cell : "");
            if( n )
            {
                memcpy(out + off, cell ? cell : "", n);
                off += n;
            }
        }
        else
        {
            int n = snprintf(out + off, total - off, "%d", array->cells.ints[i]);
            if( n > 0 )
                off += (size_t)n;
        }
    }
    out[off] = '\0';
    return CS2VM2_PushStr(vm, out);
}

/*
 * Opcode 8018 — split a string on a separator into a new string-array handle.
 * Script 9183: push $s; push "||"; 8018; length.
 */
int
CS2VM2_Op_ArraySplit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char* separator = NULL;
    char* text = NULL;
    if( CS2VM2_PopStr(vm, &separator) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( vm->array_alloc >= CS2VM2_MAX_ARRAYS )
    {
        fprintf(
            stderr,
            "CS2VM2: array pool exhausted (%d) in script %d (ARRAY_SPLIT)\n",
            CS2VM2_MAX_ARRAYS,
            frame->script ? frame->script->script_id : -1);
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
    }

    struct CS2VM2_Array* array = &vm->arrays[vm->array_alloc++];
    array->defined = 1;
    array->is_string = 1;
    array->size = 0;

    char const* sep = separator ? separator : "";
    size_t sep_len = strlen(sep);
    char const* cursor = text ? text : "";

    if( sep_len == 0 )
    {
        /* Empty separator: one cell holding the whole string. */
        if( cs2vm2_array_reserve(array, 1) )
            array->cells.strings[array->size++] = CS2VM2_StrDup(vm, cursor);
    }
    else
    {
        for( ;; )
        {
            /* Unknown length up front, so the block grows a part at a time. */
            if( !cs2vm2_array_reserve(array, array->size + 1) )
                break;
            char const* found = strstr(cursor, sep);
            if( !found )
            {
                array->cells.strings[array->size++] = CS2VM2_StrDup(vm, cursor);
                break;
            }
            size_t part_len = (size_t)(found - cursor);
            array->cells.strings[array->size++] =
                CS2VM2_StrDupLen(vm, cursor, part_len);
            cursor = found + sep_len;
        }
    }

    return CS2VM2_PushStr(vm, (char*)array);
}

/*
 * Opcode 8022 — ARRAY_NEW(typeCode, length, capacity) -> handle.
 * typeCode 115 ('s') / 2 => string cells; else int cells filled with -1.
 * Reference: xrsps createTypedArrayFromCode.
 */
int
CS2VM2_Op_ArrayNew(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int capacity;
    int length;
    int type_code;
    if( CS2VM2_PopInt(vm, &capacity) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &length) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &type_code) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( capacity < length )
        capacity = length;
    if( length < 0 )
        length = 0;
    if( capacity < 0 )
        capacity = 0;
    if( capacity > CS2VM2_ARRAY_CAPACITY )
        capacity = CS2VM2_ARRAY_CAPACITY;
    if( length > capacity )
        length = capacity;

    if( vm->array_alloc >= CS2VM2_MAX_ARRAYS )
    {
        fprintf(
            stderr,
            "CS2VM2: array pool exhausted (%d) in script %d (ARRAY_NEW)\n",
            CS2VM2_MAX_ARRAYS,
            frame->script ? frame->script->script_id : -1);
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
    }

    struct CS2VM2_Array* array = &vm->arrays[vm->array_alloc++];
    if( cs2vm2_array_begin(array, length, type_code == 2 || type_code == 115) )
    {
        if( array->is_string )
            for( int i = 0; i < array->size; i++ )
                array->cells.strings[i] = CS2VM2_StrEmpty(vm);
        else
            for( int i = 0; i < array->size; i++ )
                array->cells.ints[i] = -1;
    }
    (void)capacity; /* capacity is reserved length; we store `size` as length. */
    return CS2VM2_PushStr(vm, (char*)array);
}

/*
 * Opcode 8023 — set an array handle's length (Overview: prepare N slots, then
 * fill with pop_array_int). Grows with -1/"" defaults; shrinks by truncating.
 */
int
CS2VM2_Op_ArraySetLength(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    int length;
    char* handle = NULL;
    if( CS2VM2_PopInt(vm, &length) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &handle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM2_Array* array = cs2vm2_array_from_handle(vm, handle);
    if( !array )
        return CS2VM_EXECNO_OK;
    if( length < 0 )
        length = 0;
    if( length > CS2VM2_ARRAY_CAPACITY )
        length = CS2VM2_ARRAY_CAPACITY;
    if( length > array->size )
    {
        if( !cs2vm2_array_reserve(array, length) )
            return CS2VM_EXECNO_OK;
        /* Cells past the old size are whatever the block last held, so both
         * arms initialise here — "already -1 from allocation" stopped being
         * true once blocks started being reused across runs. */
        if( array->is_string )
            for( int i = array->size; i < length; i++ )
                array->cells.strings[i] = CS2VM2_StrEmpty(vm);
        else
            for( int i = array->size; i < length; i++ )
                array->cells.ints[i] = -1;
    }
    array->size = length;
    return CS2VM_EXECNO_OK;
}

/*
 * Opcode 8024 — append a typed value onto an array handle.
 * Stack (top first): typeCode, then the value (int or string by type), then
 * the handle. Overview sites use type 0 (int) with no index — append at end.
 * Same type-code convention as ARRAY_COUNT_MATCHES (8007).
 */
int
CS2VM2_Op_ArrayAppend(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    int value_type;
    if( CS2VM2_PopInt(vm, &value_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int want_str = (value_type == 2 || value_type == 115);
    int want_int = (value_type == 0 || value_type == 49 || value_type == 105);

    int value_int = 0;
    char* value_str = NULL;
    if( want_int && CS2VM2_PopInt(vm, &value_int) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( want_str && CS2VM2_PopStr(vm, &value_str) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    char* handle = NULL;
    if( CS2VM2_PopStr(vm, &handle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM2_Array* array = cs2vm2_array_from_handle(vm, handle);
    if( !array || !cs2vm2_array_reserve(array, array->size + 1) )
        return CS2VM_EXECNO_OK;

    int index = array->size;
    if( array->is_string || want_str )
    {
        array->is_string = 1;
        array->cells.strings[index] =
            value_str ? CS2VM2_StrDup(vm, value_str) : CS2VM2_StrEmpty(vm);
    }
    else
    {
        array->cells.ints[index] = value_int;
    }
    array->size = index + 1;
    return CS2VM_EXECNO_OK;
}

/*
 * Opcode 4036 — STRING_TO_INT. Parse decimal; -1 on failure (xrsps MathOps).
 */
int
CS2VM2_Op_StringToInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;
    (void)operand;

    char* text = NULL;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( !text || !*text )
        return CS2VM2_PushInt(vm, -1);
    char* end = NULL;
    long value = strtol(text, &end, 10);
    if( end == text || *end != '\0' )
        return CS2VM2_PushInt(vm, -1);
    return CS2VM2_PushInt(vm, (int)value);
}

/*
 * Opcode 212 — children-find on the active component that also pushes the
 * match count (scripts discard it). Same host request as CC_CHILDREN_FIND.
 */
int
CS2VM2_Op_CC_ChildrenFindCount(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int start_index;
    if( CS2VM2_PopInt(vm, &start_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_CHILDREN_FIND;
    request.u.cc_children_find.parent_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_children_find.start_index = start_index;

    int rc = vm->vm->host_exec(vm, &request);
    if( rc != CS2VM_EXECNO_OK )
        return rc;
    return CS2VM2_PushInt(vm, vm->children_iter_count);
}

/*
 * Opcode 211 — IF_CHILDREN_COLLECT(unused, componentUid, startIndex) -> count.
 * Call sites push in that order (script 9181: `211(1, $tabs, -1)`), so start
 * is on top of the stack — same as IF_CHILDREN_FIND's start-then-uid pop
 * after the leading unused. Fills a new int-array with child subids from
 * startIndex and stashes the handle for CHILDREN_ARRAY (215). Script 9181
 * walks overview_tabs and if_sethide's the non-selected content panel; a
 * reversed pop made start=1 and skipped tab 0, so Overview never hid when
 * switching to Quest XP.
 */
int
CS2VM2_Op_IF_ChildrenCollect(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int start_index;
    int uid;
    int unused;
    if( CS2VM2_PopInt(vm, &start_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &unused) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    (void)unused;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CHILDREN_FIND;
    request.u.if_children_find.uid = uid;
    request.u.if_children_find.start_index = start_index;
    request.u.if_children_find.dot_operand = operand;

    int rc = vm->vm->host_exec(vm, &request);
    if( rc != CS2VM_EXECNO_OK )
        return rc;

    int count = vm->children_iter_count;
    if( count < 0 )
        count = 0;
    if( count > CS2VM2_ARRAY_CAPACITY )
        count = CS2VM2_ARRAY_CAPACITY;

    if( vm->array_alloc >= CS2VM2_MAX_ARRAYS )
    {
        fprintf(
            stderr,
            "CS2VM2: array pool exhausted (%d) in script %d (IF_CHILDREN_COLLECT)\n",
            CS2VM2_MAX_ARRAYS,
            frame->script ? frame->script->script_id : -1);
        vm->children_collect_handle = NULL;
        return CS2VM2_PushInt(vm, 0);
    }

    struct CS2VM2_Array* array = &vm->arrays[vm->array_alloc++];
    if( !cs2vm2_array_begin(array, count, 0) )
    {
        vm->children_collect_handle = NULL;
        return CS2VM2_PushInt(vm, 0);
    }
    for( int i = 0; i < array->size; i++ )
        array->cells.ints[i] = vm->children_iter_indices[i];

    vm->children_collect_handle = (char*)array;
    return CS2VM2_PushInt(vm, count);
}

/*
 * Opcode 215 — CHILDREN_ARRAY() -> string handle.
 * Pushes the int-array stashed by the last IF_CHILDREN_COLLECT (211). If none
 * ran, allocates a length-0 int array so ARRAY_LENGTH / indexing stay safe.
 */
int
CS2VM2_Op_ChildrenArray(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( vm->children_collect_handle )
        return CS2VM2_PushStr(vm, vm->children_collect_handle);

    if( vm->array_alloc >= CS2VM2_MAX_ARRAYS )
    {
        fprintf(
            stderr,
            "CS2VM2: array pool exhausted (%d) in script %d (CHILDREN_ARRAY)\n",
            CS2VM2_MAX_ARRAYS,
            frame->script ? frame->script->script_id : -1);
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
    }

    struct CS2VM2_Array* array = &vm->arrays[vm->array_alloc++];
    array->defined = 1;
    array->size = 0;
    array->is_string = 0;
    for( int i = 0; i < CS2VM2_ARRAY_CAPACITY; i++ )
        array->cells.ints[i] = -1;
    vm->children_collect_handle = (char*)array;
    return CS2VM2_PushStr(vm, (char*)array);
}

int
CS2VM2_Op_SetBit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int bit;
    int value;
    if( CS2VM2_PopInt(vm, &bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, value | (1 << bit));
}

int
CS2VM2_Op_ClearBit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int bit;
    int value;
    if( CS2VM2_PopInt(vm, &bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, value & ~(1 << bit));
}

int
CS2VM2_Op_Or(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int a;
    int b;
    if( CS2VM2_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_PushInt(vm, a | b);
}

int
CS2VM2_Op_InvPow(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int exponent;
    int base;
    if( CS2VM2_PopInt(vm, &exponent) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &base) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    /* Inverse power: the exponent-th integer root of base (NOT base^exponent).
     * Mirrors the reference client (xrsps MathOps.INVPOW). */
    int result;
    if( base == 0 )
        result = 0;
    else
        switch( exponent )
        {
        case 0:
            result = 2147483647; /* Integer.MAX_VALUE */
            break;
        case 1:
            result = base;
            break;
        case 2:
            result = (int)sqrt((double)base);
            break;
        case 3:
            result = (int)cbrt((double)base);
            break;
        case 4:
            result = (int)sqrt(sqrt((double)base));
            break;
        default:
            result = (int)pow((double)base, 1.0 / exponent);
            break;
        }
    return CS2VM2_PushInt(vm, result);
}

int
CS2VM2_Op_Random(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    /*
     * `random($max)` is 0 .. $max-1, and it POPS $max. This used to push a raw
     * rand() and pop nothing, which fails twice over: the argument stays on the
     * int stack, and the result is unbounded.
     *
     * Neither failure is loud, which is why it stood. A leftover int is
     * tolerated at script end, and an out-of-range value is almost always used
     * as an array index, where CS2VM2_Op_PushArrayInt answers 0 for anything
     * out of range. The bank PIN keypad is where it showed: script 653 shuffles
     * its ten digit buttons with `random(9)` twenty times, and every one of
     * those swaps read past the end of the array — so the keypad drew its
     * digits in plain 0-to-9 order, with the last button blank, and the whole
     * anti-shoulder-surfing property of the screen was inert.
     */
    int max;
    if( CS2VM2_PopInt(vm, &max) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( max <= 0 )
        return CS2VM2_PushInt(vm, 0);

    return CS2VM2_PushInt(vm, (int)(rand() % (unsigned)max));
}

int
CS2VM2_Op_RandomInc(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int max;
    if( CS2VM2_PopInt(vm, &max) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( max < 0 )
        return CS2VM2_PushInt(vm, 0);

    return CS2VM2_PushInt(vm, (int)(rand() % ((unsigned)(max + 1))));
}

int
CS2VM2_Op_Interpolate(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int e;
    int d;
    int c;
    int b;
    int a;
    if( CS2VM2_PopInt(vm, &e) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &d) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &c) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int denom = d - c;
    if( denom == 0 )
        return CS2VM2_PushInt(vm, a);

    int mul = (b - a) * (e - c);
    int div = mul / denom;
    return CS2VM2_PushInt(vm, a + div);
}

int
CS2VM2_Op_Compare(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char* b;
    char* a;
    if( CS2VM2_PopStr(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int cmp = 0;
    if( a && b )
        cmp = strcmp(a, b);
    else if( a && !b )
        cmp = 1;
    else if( !a && b )
        cmp = -1;

    if( cmp < 0 )
        return CS2VM2_PushInt(vm, -1);
    if( cmp > 0 )
        return CS2VM2_PushInt(vm, 1);
    return CS2VM2_PushInt(vm, 0);
}

int
CS2VM2_Op_Substring(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int end;
    int start;
    char* text;
    if( CS2VM2_PopInt(vm, &end) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    assert(text);

    int len = (int)strlen(text);
    if( start < 0 )
        start = 0;
    if( end > len )
        end = len;
    if( start > end )
        start = end;

    int out_len = end - start;
    return CS2VM2_PushStr(vm, CS2VM2_StrDupLen(vm, text + start, (size_t)out_len));
}

int
CS2VM2_Op_StringIndexOfString(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int start;
    char* needle;
    char* haystack;
    if( CS2VM2_PopInt(vm, &start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &needle) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &haystack) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = -1;
    if( haystack && needle && needle[0] != '\0' )
    {
        int len = (int)strlen(haystack);
        if( start < 0 )
            start = 0;
        if( start <= len )
        {
            char const* found = strstr(haystack + start, needle);
            if( found )
                result = (int)(found - haystack);
        }
    }

    return CS2VM2_PushInt(vm, result);
}

int
CS2VM2_Op_StringIndexOfChar(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    /* Bytecode / LostCity / SSVM: (string, char) -> index. No start index —
     * STRING_INDEXOF_STRING is the one that takes a start. */
    int ch;
    char* haystack;
    if( CS2VM2_PopInt(vm, &ch) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &haystack) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = -1;
    if( haystack )
    {
        int len = (int)strlen(haystack);
        for( int i = 0; i < len; i++ )
        {
            if( (unsigned char)haystack[i] == (unsigned char)ch )
            {
                result = i;
                break;
            }
        }
    }

    return CS2VM2_PushInt(vm, result);
}

int
CS2VM2_Op_StructParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_GetParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_GetText(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_FindRoot(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_ChildrenFind(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_ChildrenFindNextId(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_ChildrenFindNext(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_IF_ChildrenFind(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_IF_ChildrenFindNextId(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_CreateChild(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_CreateSibling(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_GetTrans(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_GetComponentParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_IF_GetComponentParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_CC_SetComponentParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_IF_Find(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_IF_GetX(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_IF_GetText(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_IF_GetScrollWidth(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand);

int
CS2VM2_Op_OC_IntParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_OC_IntField field);

static int
CS2VM2_Op_IF_SetOnEventDiscard(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    (void)operand;
    struct CS2VM_HostRequest_IF_SetOnOp req;
    return CS2VM2_Op_IF_SetOnEventHandler(vm, frame, CS2VM_HOST_REQUEST_IF_SETON_DISCARD, &req);
}

static int
CS2VM2_Op_CC_SetOnEventDiscard(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp req;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETON_DISCARD, &req);
}

static int
CS2VM2_Op_CC_SetOnTimer(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp req;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONTIMER, &req);
}

static int
CS2VM2_Op_CC_SetOnVarTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp req;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT, &req);
}

static int
CS2VM2_Op_CC_SetOnInvTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp req;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT, &req);
}

static int
CS2VM2_Op_CC_SetOnStatTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp req;
    return CS2VM2_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONSTATTRANSMIT, &req);
}

/* Print the calling script and a window of ops around the current pc when an
 * unimplemented opcode is hit, so its stack signature can be read off the
 * surrounding pushes/pops (how the MANUAL_STACK entries in gen_opcode_stack.py
 * were derived) before adding it there. */
static void
CS2VM2_ReportUnimplementedOpcode(
    struct CS2VM2_Frame const* frame,
    int opcode)
{
    struct CS2VM2_Script const* script = frame->script;
    int first;
    int last;

    fprintf(
        stderr,
        "CS2VM2: unimplemented opcode %d (%s) — no stack signature\n",
        opcode,
        CS2_OpCode_String(opcode));

    if( !script )
        return;

    first = frame->pc - 8;
    last = frame->pc + 4;
    if( first < 0 )
        first = 0;
    if( last > script->op_count )
        last = script->op_count;

    fprintf(
        stderr,
        "  in script %d (int_locals=%d str_locals=%d), pc=%d:\n",
        script->script_id,
        script->local_int_count,
        script->local_string_count,
        frame->pc);
    for( int pci = first; pci < last; pci++ )
    {
        fprintf(
            stderr,
            "  %s pc=%d op=%d %-22s operand=%d str=%s\n",
            pci == frame->pc ? "->" : "  ",
            pci,
            script->opcodes[pci],
            CS2_OpCode_String(script->opcodes[pci]),
            script->int_operands ? script->int_operands[pci] : 0,
            (script->string_operands && script->string_operands[pci])
                ? script->string_operands[pci]
                : "(null)");
    }
}

/* HIGHLIGHT_* family (7000..7037): the modern OSRS entity-highlight system
 * (flag an NPC / loc / obj / player / tile so it draws with an outline overlay
 * on a given highlight slot). The port has no highlight system yet, so these
 * hand off to the host as a single stubbed request kind (CS2VM_HOST_REQUEST_
 * HIGHLIGHT) that the host will eventually back with real highlight state — the
 * VM's job is only to pop the OSRS args and forward them. The pop counts differ
 * per subject (an NPC is one slot; a loc/obj is keyed by typeId+coord+slot+group;
 * the SETUP variants pop 5), so the caller passes pop_count explicitly. `query`
 * marks the GET variants, whose bool result the host pushes.
 *
 *   NPC:  7000 SETUP pop 5   7001 ON pop 3   7002 OFF pop 3
 *         7003 GET   pop 3, push bool        7004 CLEAR pop 1
 *   LOC:  7011 ON    pop 4   7012 OFF pop 4
 *         7013 GET   pop 4, push bool        7014 CLEAR pop 1
 *   OBJ:  7021 ON    pop 4   7022 OFF pop 4
 *         7023 GET   pop 4, push bool        7025 OBJTYPE_SETUP pop 5
 */
static int
CS2VM2_Op_Highlight(
    struct CS2VM2_Thread* vm,
    int opcode,
    int pop_count,
    bool query)
{
    assert(vm);
    assert(pop_count <= CS2VM_HIGHLIGHT_ARG_MAX);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_HIGHLIGHT;
    request.u.highlight.opcode = opcode;
    request.u.highlight.arg_count = pop_count;
    request.u.highlight.query = query;

    /* Pop into push order: args[0] is the first int the script pushed. */
    for( int i = pop_count - 1; i >= 0; i-- )
    {
        if( CS2VM2_PopInt(vm, &request.u.highlight.args[i]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return vm->vm->host_exec(vm, &request);
}

/* MINIMENU_* (7100..7110): no-arg getters over the current mouseover target and
 * right-click-menu state. They carry no operands to pop, so this just tags the
 * request with the opcode and lets the host push each op's result. */
static int
CS2VM2_Op_Minimenu(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    assert(vm);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_MINIMENU;
    request.u.minimenu.opcode = opcode;
    return vm->vm->host_exec(vm, &request);
}

/* Audio-volume and client/game/device option get/set (3203..3217). The direct
 * volume ops carry no id (has_id = false); the *OPTION_* families are keyed by an
 * option id pushed before the value. SET ops push a value (has_value), getters
 * push nothing here — the host pushes their results. Pop order mirrors the push
 * order: value is on top (popped first), then the id. */
static int
CS2VM2_Op_ClientOption(
    struct CS2VM2_Thread* vm,
    int opcode,
    bool has_id,
    bool has_value)
{
    assert(vm);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CLIENT_OPTION;
    request.u.client_option.opcode = opcode;

    if( has_value )
    {
        if( CS2VM2_PopInt(vm, &request.u.client_option.value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    if( has_id )
    {
        if( CS2VM2_PopInt(vm, &request.u.client_option.option_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return vm->vm->host_exec(vm, &request);
}

/* CLIENTOP_* (6700..6709): install/remove transient client-owned context-menu
 * ops. SET pops string label, then scriptId, then slot (int stack top-first);
 * DEL pops slot only. The host stubs them until an enhanced-menu system exists. */
static int
CS2VM2_Op_ClientOp(
    struct CS2VM2_Thread* vm,
    int opcode,
    bool is_set)
{
    assert(vm);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CLIENTOP;
    request.u.clientop.opcode = opcode;
    request.u.clientop.is_set = is_set;

    if( is_set )
    {
        if( CS2VM2_PopStr(vm, &request.u.clientop.label) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.clientop.script_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    if( CS2VM2_PopInt(vm, &request.u.clientop.slot) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return vm->vm->host_exec(vm, &request);
}

/* Mobile local notifications (3170..3173). Only LOCAL_NOTIFICATION carries a
 * payload — (id, delay_ms) off the int stack and (title, body) off the string
 * stack, each popped top-down — and the host answers it with a cancel handle.
 * CANCEL takes the handle; CANCELALL and SUPPORTED take nothing, and the host
 * pushes SUPPORTED's answer. */
static int
CS2VM2_Op_LocalNotification(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    assert(vm);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_LOCAL_NOTIFICATION;
    request.u.local_notification.opcode = opcode;

    char* title = NULL;
    char* body = NULL;
    int result;

    switch( opcode )
    {
    case CS2_OP_LOCAL_NOTIFICATION:
        /* Strings pop top-down: body was pushed last, so it comes off first. */
        if( CS2VM2_PopStr(vm, &body) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopStr(vm, &title) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.local_notification.delay_ms) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &request.u.local_notification.id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.local_notification.title = title;
        request.u.local_notification.body = body;
        break;
    case CS2_OP_LOCAL_NOTIFICATION_CANCEL:
        if( CS2VM2_PopInt(vm, &request.u.local_notification.id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        break;
    default:
        break;
    }

    /* The host only borrows the strings; the pool keeps them alive. */
    result = vm->vm->host_exec(vm, &request);
    return result;
}

/* Minimap zoom controls (7250..7254). The setters take one value (has_value);
 * GETZOOM takes nothing and the host pushes the current zoom. */
static int
CS2VM2_Op_Minimap(
    struct CS2VM2_Thread* vm,
    int opcode,
    bool has_value)
{
    assert(vm);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_MINIMAP;
    request.u.minimap.opcode = opcode;

    if( has_value )
    {
        if( CS2VM2_PopInt(vm, &request.u.minimap.value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return vm->vm->host_exec(vm, &request);
}

/* Viewport FOV/zoom (6200..6205, GETEFFECTIVESIZE excluded — that one still
 * reads the live canvas size directly). Pop_count is 2 for SETFOV/SETZOOM, 4 for
 * CLAMPFOV, 0 for the GETs; the host pushes the GETs' results. */
static int
CS2VM2_Op_Viewport(
    struct CS2VM2_Thread* vm,
    int opcode,
    int pop_count)
{
    assert(vm);
    assert(pop_count <= CS2VM_VIEWPORT_ARG_MAX);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VIEWPORT;
    request.u.viewport.opcode = opcode;
    request.u.viewport.arg_count = pop_count;

    for( int i = pop_count - 1; i >= 0; i-- )
    {
        if( CS2VM2_PopInt(vm, &request.u.viewport.args[i]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return vm->vm->host_exec(vm, &request);
}

/* UI zoom (6210..6214). SET pops a value (has_value); GET/RESET/GETDEFAULT pop
 * nothing and the host pushes GET/GETDEFAULT's result. */
static int
CS2VM2_Op_UiZoom(
    struct CS2VM2_Thread* vm,
    int opcode,
    bool has_value)
{
    assert(vm);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_UIZOOM;
    request.u.uizoom.opcode = opcode;

    if( has_value )
    {
        if( CS2VM2_PopInt(vm, &request.u.uizoom.value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return vm->vm->host_exec(vm, &request);
}

/* Safe-area bounds (6220..6223, 6231): no-arg getters, the host pushes the
 * result. */
static int
CS2VM2_Op_SafeArea(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    assert(vm);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_SAFEAREA;
    request.u.safearea.opcode = opcode;
    return vm->vm->host_exec(vm, &request);
}

/*
 * Stack signatures for RS2 (634/643) commands the OldSchool table does not carry,
 * or carries with a different shape.
 *
 * Read off the rev-634 client's command dispatcher (`Class66`, and the classes it
 * chains to; the full diff is docs/RS2_634_CLIENT_REFERENCES.md section 3) by
 * counting stack traffic:
 * `anIntArray1149[--anInt1173]` pops an int, `anIntArray1149[anInt1173++] =` pushes
 * one, `aStringArray1152` is the string stack, and `anInt1173 -= N` followed by
 * indexed reads pops N at once.
 *
 * Only ids whose 634 signature is not already in the canonical table belong here.
 * Two ids in this list demonstrably mean different commands in the two eras — 4124
 * (634: two ints in, one string out; OldSchool 239 script 5031 calls it with an
 * empty stack) and 6506 (634 pushes 4 ints + 3 strings; OldSchool 239 script 6918
 * stores 4 ints + 2) — which is why this is an era overlay rather than more
 * entries in gen_opcode_stack.py's MANUAL_STACK.
 *
 * These are stubs, not implementations: the counts keep the stack balanced so the
 * rest of the script runs, and every pushed value is a zero/empty default.
 */
struct CS2VM2OpcodeStackRs2
{
    uint16_t opcode;
    struct CS2VM2OpcodeStack meta;
};

static struct CS2VM2OpcodeStackRs2 const g_cs2vm2_opcode_stack_rs2[] = {
    /* 202/203: remove a widget from its group array (Class66.method714/702).
     * Canonical 202 is CC_FINDROOT, which pushes instead of popping. */
    { 202, { 1, 0, 0, 0, 1 } },
    /* 1122 / 2122: CC_/IF_ set of the type-5 flag bit 1 (Class46.aBoolean745).
     * The IF_ form pops the component id first (Class66:2731 `i -= 1000`). */
    { 1122, { 1, 0, 0, 0, 1 } },
    { 2122, { 2, 0, 0, 0, 1 } },
    /* 1311/2314 collide with CC_SETOPSUBMENU and IF_SETTARGETPRIORITY: at 634
     * both are plain one-int widget setters (Class66 `class46.anInt713` /
     * `anInt719`). */
    { 1311, { 1, 0, 0, 0, 1 } },
    { 2314, { 2, 0, 0, 0, 1 } },
    /* 2703: count of a widget's dynamic children (Class46.aClass46Array798). */
    { 2703, { 1, 0, 1, 0, 1 } },
    /* 3316 STAFFMODLEVEL and 3323 PLAYERMOD used to sit here. Both now carry the
     * same signature in the canonical table (gen_opcode_stack.py's MANUAL_STACK),
     * and the two eras agree on it, so an overlay entry would only shadow an
     * identical one. */
    { 3329, { 0, 0, 1, 0, 1 } }, /* Class50_Sub2.aBoolean5233 */
    { 3335, { 0, 0, 1, 0, 1 } }, /* language index (Class348_Sub33.anInt6967) */
    { 3340, { 0, 0, 1, 0, 1 } }, /* Class175.aBoolean2329 */
    { 3351, { 0, 0, 3, 0, 1 } }, /* three mouse-button booleans, one call */
    /* 3609/3619 take a name string where the canonical pair takes an int. */
    { 3609, { 0, 1, 1, 0, 1 } }, /* friend test by display name */
    { 3619, { 0, 1, 0, 0, 1 } }, /* join clan chat by owner name */
    { 4124, { 2, 0, 0, 1, 1 } }, /* (value, comma-group) -> formatted number */
    { 4125, { 1, 1, 1, 0, 1 } }, /* (font, text) -> rendered width */
    /*
     * 5003..5024 are a friend/ignore-record accessor family at 634 — every one
     * takes a list index and reads a field off the record
     * (`Class147 = s.method3985(index)`). The canonical numbering puts the chat
     * history family on the same ids, with no argument and a different result
     * type, so these have to be overridden even where the two happen to agree on
     * the count.
     */
    { 5003, { 1, 0, 0, 1, 1 } },
    { 5004, { 1, 0, 1, 0, 1 } },
    { 5010, { 1, 0, 0, 1, 1 } },
    { 5011, { 1, 0, 0, 1, 1 } },
    { 5012, { 1, 0, 1, 0, 1 } },
    { 5019, { 1, 0, 0, 1, 1 } },
    { 5024, { 1, 0, 1, 0, 1 } },
    { 5056, { 1, 0, 1, 0, 1 } }, /* array length or 0 */
    { 5102, { 0, 0, 1, 0, 1 } }, /* key-down test */
    { 5420, { 0, 0, 1, 0, 1 } },
    /* 5424: eleven ints — four chat/scroll geometry values, five sprite ids the
     * client immediately preloads, then two more. Sets the chat scrollbar skin. */
    { 5424, { 11, 0, 0, 0, 1 } },
    { 5428, { 2, 0, 1, 0, 1 } },
    { 5504, { 2, 0, 0, 0, 1 } }, /* CAM_FORCEANGLE */
    { 5505, { 0, 0, 1, 0, 1 } }, /* CAM_GETANGLE_XA */
    { 5506, { 0, 0, 1, 0, 1 } }, /* CAM_GETANGLE_YA */
    { 5507, { 0, 0, 0, 0, 1 } },
    { 5508, { 0, 0, 0, 0, 1 } },
    { 5509, { 0, 0, 0, 0, 1 } },
    { 5510, { 0, 0, 0, 0, 1 } },
    { 5547, { 0, 0, 1, 0, 1 } },
    /* 6506 WORLDLIST_SPECIFIC(world) -> id, name, loc-id, loc-name, flags,
     * players, activity. Four ints and three strings, interleaved on the wire but
     * split across the two stacks. */
    { 6506, { 1, 0, 4, 3, 1 } },
    { 6510, { 0, 0, 1, 0, 1 } },
    { 6900, { 0, 0, 1, 0, 1 } },
};

static bool
cs2vm2_opcode_stack_rs2_lookup(
    int opcode,
    struct CS2VM2OpcodeStack* out)
{
    for( size_t i = 0; i < sizeof(g_cs2vm2_opcode_stack_rs2) / sizeof(g_cs2vm2_opcode_stack_rs2[0]);
         i++ )
    {
        if( g_cs2vm2_opcode_stack_rs2[i].opcode == (uint16_t)opcode )
        {
            *out = g_cs2vm2_opcode_stack_rs2[i].meta;
            return true;
        }
    }
    return false;
}

static int
CS2VM2_Op_StackMetaStub(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int opcode,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    /*
     * An opcode past the end of the generated table is UNIMPLEMENTED, not
     * benign — so it takes the same route as one with no signature, below.
     *
     * This used to `return CS2VM_EXECNO_OK` here, which is the same silent
     * no-op the `!meta.known` assert exists to prevent, except that it fired
     * *first* and so could never be caught. The table tops out at 7602
     * (CS2VM2_OPCODE_STACK_MAX, generated), which put the entire 8000-series —
     * the array ops at this revision — permanently out of reach: not
     * implemented, not asserted, and invisible to TORIRS_CS2_SURVEY, which is
     * the tool you would reach for to find exactly this.
     *
     * A zeroed meta has known = 0, which is what routes it.
     */
    struct CS2VM2OpcodeStack meta;

    if( opcode >= 0 && opcode < CS2VM2_OPCODE_STACK_MAX )
    {
        meta = g_cs2vm2_opcode_stack[opcode];
    }
    else
    {
        memset(&meta, 0, sizeof(meta));
    }

    /* The RS2 overlay wins where it has an entry: for the ids it names the
     * canonical signature is either absent or a different command's. */
    if( frame->script && frame->script->rs2_dialect )
    {
        struct CS2VM2OpcodeStack rs2;
        if( cs2vm2_opcode_stack_rs2_lookup(opcode, &rs2) )
            meta = rs2;
    }

    /* Reaching the generic stub with an unknown signature means the opcode is
     * unimplemented: nobody has given it a real stack signature, so the pop/push
     * counts below are just the (0,0,0,0) default. Silently no-oping it corrupts
     * the stack and aborts the script at some unrelated downstream opcode (e.g. a
     * later BRANCH_EQUALS underflows). Assert here so the culprit surfaces at the
     * opcode itself. In release builds (NDEBUG) this compiles out and the stub
     * falls back to its previous best-effort no-op behaviour. */
    if( !meta.known )
    {
        /* TORIRS_CS2_SURVEY=1 downgrades the abort to one report per opcode.
         * Bringing up a new era means walking a list of missing opcodes, and one
         * abort per rebuild makes that list arrive one entry at a time. The run
         * afterwards is not trustworthy — a no-op with the wrong arity corrupts
         * the stack — so this is a survey tool, never a way to ship. */
        static bool survey = false;
        static bool survey_read = false;
        if( !survey_read )
        {
            survey = getenv("TORIRS_CS2_SURVEY") != NULL;
            survey_read = true;
        }
        if( survey )
        {
            /* Sized to the table, but `opcode` may now be past its end — the
             * out-of-range case is exactly what this path was opened up for.
             * Those are reported every time rather than once; a survey run is
             * short and a duplicate line is cheaper than a second table. */
            static bool reported[CS2VM2_OPCODE_STACK_MAX];
            bool in_table = (opcode >= 0 && opcode < CS2VM2_OPCODE_STACK_MAX);

            if( !in_table || !reported[opcode] )
            {
                if( in_table )
                    reported[opcode] = true;
                fprintf(
                    stderr,
                    "cs2-survey: opcode %d unimplemented (script %d pc %d)\n",
                    opcode,
                    frame->script ? frame->script->script_id : -1,
                    frame->pc);
            }
            return CS2VM_EXECNO_OK;
        }
        CS2VM2_ReportUnimplementedOpcode(frame, opcode);
        assert(0 && "unimplemented CS2 opcode reached StackMetaStub");
    }

    /* known == 2: the signature came from the decompiler's table
     * (3rd/rscache/src/cs2/cs2_command.gen.h, bridged in by
     * gen_opcode_stack.py) and nothing here implements the opcode. The stack
     * below stays balanced, which is the whole point — but the results are
     * zeros and empty strings, i.e. a *plausible wrong answer* rather than a
     * crash, and that is the harder failure to find. So say so, once per
     * opcode, unconditionally: this list is the honest inventory of what a run
     * faked its way through. Unlike the survey path above it does not gate on
     * an env var, because a silent wrong answer is not something you should
     * have to go looking for. */
    if( meta.known == 2 && opcode >= 0 && opcode < CS2VM2_OPCODE_STACK_MAX )
    {
        static bool announced[CS2VM2_OPCODE_STACK_MAX];
        if( !announced[opcode] )
        {
            announced[opcode] = true;
            fprintf(
                stderr,
                "cs2-stub: opcode %d has an inherited signature (%d,%d,%d,%d) but no "
                "implementation — stack balanced, results faked (script %d pc %d)\n",
                opcode,
                meta.int_in,
                meta.str_in,
                meta.int_out,
                meta.str_out,
                frame->script ? frame->script->script_id : -1,
                frame->pc);
        }
    }

    for( int i = 0; i < meta.int_in; i++ )
    {
        int discard;
        if( CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    for( int i = 0; i < meta.str_in; i++ )
    {
        char* discard;
        if( CS2VM2_PopStr(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    for( int i = 0; i < meta.int_out; i++ )
    {
        if( CS2VM2_PushInt(vm, 0) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    for( int i = 0; i < meta.str_out; i++ )
    {
        if( CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm)) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return CS2VM_EXECNO_OK;
}

/**
 * World map (6600..6640) and map element config (6693..6699). Both families
 * read one host-side state object, so each gets a single request kind carrying
 * its opcode instead of forty near-identical kinds. Argument counts come from
 * the generated stack table, so the doc comments in cs2_opcode.h stay the one
 * place a signature is written down; the host pushes the results.
 */
static int
CS2VM2_Op_WorldMapFamily(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    struct CS2VM_HostRequest request;
    int args[2] = { 0, 0 };
    int int_in;

    assert(vm);
    assert(opcode >= 0 && opcode < CS2VM2_OPCODE_STACK_MAX);

    int_in = g_cs2vm2_opcode_stack[opcode].int_in;
    assert(int_in <= (int)(sizeof(args) / sizeof(args[0])));

    /* Popping runs last-pushed first, so fill backwards to hand the host its
     * args in source order. */
    for( int i = int_in - 1; i >= 0; i-- )
    {
        if( CS2VM2_PopInt(vm, &args[i]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    memset(&request, 0, sizeof(request));
    if( opcode >= CS2_OP_MEC_TEXT && opcode <= CS2_OP_MEC_SPRITE )
    {
        request.kind = CS2VM_HOST_REQUEST_MEC;
        request.u.mec.opcode = opcode;
        request.u.mec.mec_id = args[0];
    }
    else
    {
        request.kind = CS2VM_HOST_REQUEST_WORLDMAP;
        request.u.worldmap.opcode = opcode;
        request.u.worldmap.arg0 = args[0];
        request.u.worldmap.arg1 = args[1];
    }
    return vm->vm->host_exec(vm, &request);
}

// OC is object config
int
CS2VM2_Op_OC_Param(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // handlers.set(Opcodes.OC_PARAM, (ctx) => {
    //     const paramId = ctx.intStack[--ctx.intStackSize];
    //     const itemId = ctx.intStack[--ctx.intStackSize];
    //     const param = ctx.paramTypeLoader?.load(paramId);
    //     const obj = ctx.objTypeLoader?.load(itemId);
    //     if (param && obj && obj.params) {
    //         const val = obj.params.get(paramId);
    //         if (param.isString()) {
    //             ctx.pushString(typeof val === "string" ? val : param.defaultString || "");
    //         } else {
    //             ctx.pushInt(typeof val === "number" ? val : param.defaultInt || 0);
    //         }
    //     } else {
    //         if (param?.isString()) {
    //             ctx.pushString(param.defaultString || "");
    //         } else {
    //             ctx.pushInt(param?.defaultInt ?? 0);
    //         }
    //     }

    int param_id, item_id;
    if( CS2VM2_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_PARAM;
    request.u.oc_param.param_id = param_id;
    request.u.oc_param.item_id = item_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_OC_Name(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_NAME;
    request.u.oc_name.item_id = item_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_NC_Name(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int npc_id;
    if( CS2VM2_PopInt(vm, &npc_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_NC_NAME;
    request.u.nc_name.npc_id = npc_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_OC_Unplaceholder(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER;
    request.u.oc_unplaceholder.item_id = item_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/* OC_OP/OC_IOP: ground/inventory right-click action string. Rev239 takes two
 * runtime ints, (obj, one-based op). The deob's method2965 removes two ints
 * from the stack and reads [sp] as the obj and [sp+1] as the op; treating the
 * bytecode operand as the op made scripts such as 7779 look up obj ids 1..5
 * instead of the item they were painting. */
static int
CS2VM2_Op_OC_ActionString(
    struct CS2VM2_Thread* vm,
    int opcode,
    int operand,
    enum CS2VM_HostRequestKind kind)
{
    assert(vm);
    (void)operand;

    int item_id;
    int op_index;
    if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.oc_op.opcode = opcode;
    request.u.oc_op.item_id = item_id;
    request.u.oc_op.op_index = op_index - 1;
    return vm->vm->host_exec(vm, &request);
}

/* OC_EXAMINE: real data (ToriRS_Objtype.desc). */
static int
CS2VM2_Op_OC_Examine(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_EXAMINE;
    request.u.oc_examine.item_id = item_id;
    return vm->vm->host_exec(vm, &request);
}

/* OC_PLACEHOLDER: mirrors OC_UNPLACEHOLDER's identity-passthrough stub (no
 * placeholder linkage data exists yet). */
static int
CS2VM2_Op_OC_Placeholder(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_PLACEHOLDER;
    request.u.oc_placeholder.item_id = item_id;
    return vm->vm->host_exec(vm, &request);
}

/* OC_FIND/OC_FINDNEXT/OC_FINDRESET: a stateful item-name search (see
 * CS2VM_HOST_REQUEST_OC_FIND). Only OC_FIND takes a stack argument — the query
 * string.
 *
 * OC_FIND may yield so the host can bulk-load the obj group; the VM then rolls
 * the string stack back and re-executes this op. The popped query survives that
 * (PopStr does not clear the slot, the checkpoint restore re-exposes it, and the
 * pool holds the storage until the script ends), so the host gets a plain
 * borrow. */
static int
CS2VM2_Op_OC_Find(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    assert(vm);

    char* query = NULL;
    if( opcode == CS2_OP_OC_FIND )
    {
        if( CS2VM2_PopStr(vm, &query) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_FIND;
    request.u.oc_find.opcode = opcode;
    request.u.oc_find.query = query; /* borrowed; NULL for FINDNEXT/FINDRESET */

    return vm->vm->host_exec(vm, &request);
}

/*
 * Loot-tracker native store ops (7400-family + 7600-family).
 *
 * Every opcode pops its own arguments here and forwards the opcode + payload
 * to the host, which calls into LootStore_*. The pattern mirrors
 * CS2VM2_Op_Social: one function, per-opcode pop rules, one host request kind.
 */
static int
CS2VM2_Op_Loot(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    struct CS2VM_HostRequest request;

    assert(vm);

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_LOOT;
    request.u.loot.opcode = opcode;

    switch( opcode )
    {
    /* No-arg getters */
    case CS2_OP_LOOT_SOURCE_COUNT:
    case CS2_OP_LOOT_AUX_COUNT_TOTAL:
    case CS2_OP_LOOT_GROUND_COUNT:
    case CS2_OP_LOOT_SRCLIST_COUNT:
    case CS2_OP_LOOT_CLEAR_ALL:
    case CS2_OP_LOOT_IGNORE_CLEAR:
        break;

    /* (int) -> ... */
    case CS2_OP_LOOT_SOURCE_NAME:
    case CS2_OP_LOOT_SOURCE_NAME2:
    case CS2_OP_LOOT_QUERY_ID:
    case CS2_OP_LOOT_ROW_COUNT_BYID:
    case CS2_OP_LOOT_REMOVE_BYID:
    case CS2_OP_LOOT_GROUND_NAME:
    case CS2_OP_LOOT_SRCLIST_NAME:
    case CS2_OP_LOOT_AUX_COUNT:
    case CS2_OP_LOOT_AUX_CLEAR:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 1;
        break;

    /* (string) -> ... */
    case CS2_OP_LOOT_SOURCE_ITEMCOUNT:
    case CS2_OP_LOOT_SOURCE_TOTALVAL:
    case CS2_OP_LOOT_ROW_COUNT_BYNAME:
    case CS2_OP_LOOT_CLEAR_SOURCE:
    case CS2_OP_LOOT_IGNORE_ADD:
    case CS2_OP_LOOT_IGNORE_REMOVE:
    case CS2_OP_LOOT_SOURCE_IGNORE_ADD:
    case CS2_OP_LOOT_SOURCE_IGNORE_REMOVE:
        if( CS2VM2_PopStr(vm, &request.u.loot.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        break;

    /* (int, int, int) -> int : BeginQuery */
    case CS2_OP_LOOT_BEGIN_QUERY:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[2]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[1]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 3;
        break;

    /* (int, int) -> (int, int) : RowById */
    case CS2_OP_LOOT_ROW_BYID:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[1]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 2;
        break;

    /* (string, int) -> (int, int) : RowByName */
    case CS2_OP_LOOT_ROW_BYNAME:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopStr(vm, &request.u.loot.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 1;
        break;

    /* (int, string) -> () : AuxUpsert2 / 7400 */
    case CS2_OP_LOOT_AUX_UPSERT2:
        if( CS2VM2_PopStr(vm, &request.u.loot.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 1;
        break;

    /* (int, string, int) -> () : AuxUpsert / AuxRemove */
    case CS2_OP_LOOT_AUX_UPSERT:
    case CS2_OP_LOOT_AUX_REMOVE:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[1]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopStr(vm, &request.u.loot.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 2;
        break;

    /* (int, int) -> string : AuxGet */
    case CS2_OP_LOOT_AUX_GET:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[1]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 2;
        break;

    /* (int, string, int, int) -> int : AuxLookup */
    case CS2_OP_LOOT_AUX_LOOKUP:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[2]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[1]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopStr(vm, &request.u.loot.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 3;
        break;

    /* (string, int, int, int) -> () : LOOT_ADD — name then obj/qty/eventId */
    case CS2_OP_LOOT_ADD:
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[1]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.loot.int_args[2]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopStr(vm, &request.u.loot.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.u.loot.int_arg_count = 3;
        break;

    default:
        assert(0 && "loot opcode reached CS2VM2_Op_Loot with no pop rule");
        return CS2VM_EXECNO_ERROR;
    }

    return vm->vm->host_exec(vm, &request);
}

/*
 * Hiscores stubs (7809/7811). No-arg opcodes: 7809 pushes a status int,
 * 7811 pushes an error string. The host answers both.
 */
static int
CS2VM2_Op_Hiscores(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    struct CS2VM_HostRequest request;

    assert(vm);

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_HISCORES;
    request.u.hiscores.opcode = opcode;

    return vm->vm->host_exec(vm, &request);
}

/*
 * Friends / ignore list ops (3600..3609, 3621..3623).
 *
 * The pops below are written out per opcode rather than driven off the
 * generated stack table on purpose: three different shapes live in this family
 * (no args, one index, one username), two of them push *two* strings, and the
 * whole reason this stage exists is that the generated table had five of them
 * transposed. Spelling the shape at the call site is what makes a future
 * mismatch a compile-visible edit instead of a silent stack desync.
 *
 * Strings are borrowed the way OC_FIND borrows its query: PopStr does not clear
 * the slot and the pool owns the storage until the script ends, so the host
 * gets a plain const borrow and must not free it.
 *
 * FRIEND_SETRANK (3604) is deliberately absent — it is called only from the
 * clan-rank script and the rank column has no model here, so it keeps falling
 * through to the stack stub, which now pops the right (int, string) pair.
 */
static int
CS2VM2_Op_Social(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    struct CS2VM_HostRequest request;

    assert(vm);

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_SOCIAL;
    request.u.social.opcode = opcode;

    switch( opcode )
    {
    case CS2_OP_FRIEND_COUNT:
    case CS2_OP_IGNORE_COUNT:
        break;
    case CS2_OP_FRIEND_GETNAME:
    case CS2_OP_FRIEND_GETWORLD:
    case CS2_OP_FRIEND_GETRANK:
    case CS2_OP_IGNORE_GETNAME:
        if( CS2VM2_PopInt(vm, &request.u.social.index) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        break;
    case CS2_OP_FRIEND_ADD:
    case CS2_OP_FRIEND_DEL:
    case CS2_OP_IGNORE_ADD:
    case CS2_OP_IGNORE_DEL:
    case CS2_OP_FRIEND_TEST:
    case CS2_OP_IGNORE_TEST:
        if( CS2VM2_PopStr(vm, &request.u.social.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        break;
    default:
        /* Unreachable: the dispatch switch lists exactly the cases above. An
         * opcode added to one list and not the other must abort here rather
         * than pop nothing and desync. */
        assert(0 && "social opcode reached CS2VM2_Op_Social with no pop rule");
        return CS2VM_EXECNO_ERROR;
    }

    return vm->vm->host_exec(vm, &request);
}

/*
 * Chat filter modes (5000/5005/5016 read, 5001 writes all three), the
 * private-message send (5009), and docheat (5020).
 *
 * chat_setfilter takes its three modes in source order (public, private,
 * trade), so they are popped back to front. chat_sendprivate takes
 * (username, mes) — likewise.
 */
static int
CS2VM2_Op_Chat(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    struct CS2VM_HostRequest request;

    assert(vm);

    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CHAT;
    request.u.chat.opcode = opcode;

    switch( opcode )
    {
    case CS2_OP_CHAT_GETFILTER_PUBLIC:
    case CS2_OP_CHAT_GETFILTER_PRIVATE:
    case CS2_OP_CHAT_GETFILTER_TRADE:
    case CS2_OP_CHAT_PLAYERNAME:
        break;
    case CS2_OP_CHAT_SETFILTER:
        if( CS2VM2_PopInt(vm, &request.u.chat.trade_mode) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.chat.private_mode) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &request.u.chat.public_mode) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        break;
    case CS2_OP_CHAT_SENDPRIVATE:
        if( CS2VM2_PopStr(vm, &request.u.chat.text) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopStr(vm, &request.u.chat.name) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        break;
    case CS2_OP_DOCHEAT:
        /* docheat(text): the chatbox's own "::foo" handler, distinct from
         * app.c's native shortcut — pops the string with "::" already
         * stripped (SUBSTRING in the caller script) and pushes nothing. */
        if( CS2VM2_PopStr(vm, &request.u.chat.text) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        break;
    default:
        assert(0 && "chat opcode reached CS2VM2_Op_Chat with no pop rule");
        return CS2VM_EXECNO_ERROR;
    }

    return vm->vm->host_exec(vm, &request);
}

/* OC_SHIFTCLICKIOP: one obj id in, the 1-based inventory op a shift-click runs
 * out (-1 for none). The host owns the rule; see exec_oc_shiftclickiop. */
static int
CS2VM2_Op_OC_ShiftClickIop(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_SHIFTCLICKIOP;
    request.u.oc_shiftclickiop.item_id = item_id;
    return vm->vm->host_exec(vm, &request);
}

/* OC_WEARPOS/WEARPOS2/WEARPOS3: no equip slot data exists yet. */
static int
CS2VM2_Op_OC_WearPos(
    struct CS2VM2_Thread* vm,
    int opcode)
{
    assert(vm);

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_WEARPOS;
    request.u.oc_wearpos.opcode = opcode;
    request.u.oc_wearpos.item_id = item_id;
    return vm->vm->host_exec(vm, &request);
}

/* OC_WEIGHT: no weight data exists yet. */
static int
CS2VM2_Op_OC_Weight(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_WEIGHT;
    request.u.oc_weight.item_id = item_id;
    return vm->vm->host_exec(vm, &request);
}

/* oc_isubop(obj, opIndex, subIndex) -> string. No sub-menu nesting exists yet. */
static int
CS2VM2_Op_OC_Isubop(
    struct CS2VM2_Thread* vm)
{
    assert(vm);

    int item_id;
    int op_index;
    int sub_index;
    if( CS2VM2_PopInt(vm, &sub_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_ISUBOP;
    request.u.oc_isubop.item_id = item_id;
    request.u.oc_isubop.op_index = op_index;
    request.u.oc_isubop.sub_index = sub_index;
    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_Return(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame)
{
    (void)vm;
    assert(vm);
    assert(frame);
    return CS2VM2_PopFrame(vm);
}

int
CS2VM2_Op_InvSize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int inv_id;

    if( CS2VM2_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_SIZE;
    request.u.invs_get_size.inv_id = inv_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_InvGetObj(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const invId = ctx.intStack[--ctx.intStackSize];
    // const slot = ctx.intStack[--ctx.intStackSize];
    // const obj = ctx.invs.get(invId)?.get(slot);
    // ctx.pushInt(obj ?? -1);

    int inv_id, slot;
    if( CS2VM2_PopInt(vm, &slot) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_OBJ;
    request.u.invs_get_obj.inv_id = inv_id;
    request.u.invs_get_obj.slot = slot;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_InvGetNum(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int inv_id, slot;
    if( CS2VM2_PopInt(vm, &slot) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_NUM;
    request.u.invs_get_num.inv_id = inv_id;
    request.u.invs_get_num.slot = slot;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_InvTotal(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int inv_id, item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_TOTAL;
    request.u.invs_get_total.inv_id = inv_id;
    request.u.invs_get_total.item_id = item_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

static int
CS2VM2_DispatchWidgetSetInt(
    struct CS2VM2_Thread* vm,
    int component_id,
    enum CS2VM_WidgetIntField field,
    int value)
{
    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_INT;
    request.u.widget_set_int.component_id = component_id;
    request.u.widget_set_int.field = field;
    request.u.widget_set_int.value = value;
    return vm->vm->host_exec(vm, &request);
}

static int
CS2VM2_Op_CC_WidgetInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_WidgetIntField field)
{
    assert(frame);
    (void)frame;

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result =
        CS2VM2_DispatchWidgetSetInt(vm, CS2VM2_DotOrActiveComponentId(vm, operand), field, value);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_IF_WidgetInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_WidgetIntField field)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int value;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = CS2VM2_DispatchWidgetSetInt(vm, component_id, field, value);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_CC_SetScrollPos(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int scroll_y;
    int scroll_x;
    if( CS2VM2_PopInt(vm, &scroll_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &scroll_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETSCROLLPOS;
    request.u.cc_set_scroll_pos.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_scroll_pos.scroll_x = scroll_x;
    request.u.cc_set_scroll_pos.scroll_y = scroll_y;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_CC_SetScrollSize(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int scroll_height;
    int scroll_width;
    if( CS2VM2_PopInt(vm, &scroll_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &scroll_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE;
    request.u.cc_set_scroll_size.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_scroll_size.scroll_width = scroll_width;
    request.u.cc_set_scroll_size.scroll_height = scroll_height;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_CC_SetModel(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int model_id;
    if( CS2VM2_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL;
    request.u.widget_set_model.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_model.model_id = model_id;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_IF_SetModel(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int model_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL;
    request.u.widget_set_model.component_id = component_id;
    request.u.widget_set_model.model_id = model_id;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_CC_SetModelAngle(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int zoom;
    int angle_z;
    int angle_y;
    int angle_x;
    int offset_y;
    int offset_x;
    if( CS2VM2_PopInt(vm, &zoom) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &angle_z) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &angle_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &angle_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &offset_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &offset_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE;
    request.u.widget_set_model_angle.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_model_angle.offset_x = offset_x;
    request.u.widget_set_model_angle.offset_y = offset_y;
    request.u.widget_set_model_angle.angle_x = angle_x;
    request.u.widget_set_model_angle.angle_y = angle_y;
    request.u.widget_set_model_angle.angle_z = angle_z;
    request.u.widget_set_model_angle.zoom = zoom;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_IF_SetModelAngle(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int zoom;
    int angle_z;
    int angle_y;
    int angle_x;
    int offset_y;
    int offset_x;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &zoom) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &angle_z) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &angle_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &angle_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &offset_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &offset_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE;
    request.u.widget_set_model_angle.component_id = component_id;
    request.u.widget_set_model_angle.offset_x = offset_x;
    request.u.widget_set_model_angle.offset_y = offset_y;
    request.u.widget_set_model_angle.angle_x = angle_x;
    request.u.widget_set_model_angle.angle_y = angle_y;
    request.u.widget_set_model_angle.angle_z = angle_z;
    request.u.widget_set_model_angle.zoom = zoom;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_CC_SetArc(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int arc_end;
    int arc_start;
    if( CS2VM2_PopInt(vm, &arc_end) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &arc_start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_ARC;
    request.u.widget_set_arc.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_arc.arc_start = arc_start;
    request.u.widget_set_arc.arc_end = arc_end;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_IF_SetArc(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int arc_end;
    int arc_start;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &arc_end) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &arc_start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_ARC;
    request.u.widget_set_arc.component_id = component_id;
    request.u.widget_set_arc.arc_start = arc_start;
    request.u.widget_set_arc.arc_end = arc_end;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_CC_SetModelKind(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_ModelKind model_kind,
    bool has_model_id)
{
    assert(frame);
    (void)frame;

    int model_id = -1;
    if( has_model_id )
    {
        if( CS2VM2_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
    request.u.widget_set_model_kind.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_model_kind.model_kind = model_kind;
    request.u.widget_set_model_kind.model_id = model_id;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_IF_SetModelKind(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_ModelKind model_kind,
    bool has_model_id)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int model_id = -1;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( has_model_id )
    {
        if( CS2VM2_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
    request.u.widget_set_model_kind.component_id = component_id;
    request.u.widget_set_model_kind.model_kind = model_kind;
    request.u.widget_set_model_kind.model_id = model_id;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VM2_Op_CC_InputInt(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_WidgetInputField field)
{
    assert(frame);
    (void)frame;

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_INPUT_INT;
    request.u.widget_input_int.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_input_int.field = field;
    request.u.widget_input_int.value = value;

    int result = vm->vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

/* Fills *int_args / *str_args with the stack values this opcode pops.
 * Returns 0 for a fixed count, 1 when the count is variable (e.g. GOSUB). */
static int
CS2VM2_OpArgCounts(
    int opcode,
    int operand,
    int* int_args,
    int* str_args);

int
CS2VM2_RunOp(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int opcode,
    int operand,
    char const* str_operand)
{
    assert(vm);
    assert(frame);
    int intpop_a;
    int intpop_b;
    char* strpop_a;
    char* strpop_b;

    /*
     * An id in the RS2 overlay names a *different command* under RS2 than the
     * canonical handler below implements, so the handler has to be skipped
     * outright — not just given different pop counts. StackMetaStub with the
     * overlay's signature is the honest result: the stack stays balanced and the
     * command is a no-op, rather than the wrong command running.
     *
     * Only ids the overlay actually lists divert; everything RS2 shares with
     * OldSchool falls through to its real handler as before.
     */
    if( frame->script && frame->script->rs2_dialect )
    {
        struct CS2VM2OpcodeStack rs2;
        if( cs2vm2_opcode_stack_rs2_lookup(opcode, &rs2) )
            return CS2VM2_Op_StackMetaStub(vm, frame, opcode, operand);
    }

    switch( opcode )
    {
    case CS2_OP_PUSH_VAR:
        return CS2VM2_Op_PushVar(vm, frame, operand);
    case CS2_OP_POP_VAR:
        return CS2VM2_Op_PopVar(vm, frame, operand);
    case CS2_OP_PUSH_VARBIT:
        return CS2VM2_Op_PushVarbit(vm, frame, operand);
    case CS2_OP_POP_VARBIT:
        return CS2VM2_Op_PopVarbit(vm, frame, operand);
    case CS2_OP_PUSH_VARC_INT:
        return CS2VM2_Op_PushVarcInt(vm, frame, operand);
    case CS2_OP_POP_VARC_INT:
        return CS2VM2_Op_PopVarcInt(vm, frame, operand);
    case CS2_OP_PUSH_VARC_STRING:
    case CS2_OP_PUSH_VARC_STRING_OLD:
        return CS2VM2_Op_PushVarcString(vm, frame, operand);
    case CS2_OP_POP_VARC_STRING:
    case CS2_OP_POP_VARC_STRING_OLD:
        return CS2VM2_Op_PopVarcString(vm, frame, operand);
    case CS2_OP_PUSH_CONSTANT_STRING:
        return CS2VM2_Op_PushConstantString(vm, frame, str_operand);
    case CS2_OP_PUSH_CONSTANT_INT:
        return CS2VM2_Op_PushConstantInt(vm, frame, operand);
    case CS2_OP_PUSH_INT_LOCAL:
        return CS2VM2_Op_PushIntLocal(vm, frame, operand);
    case CS2_OP_PUSH_STRING_LOCAL:
        return CS2VM2_Op_PushStrLocal(vm, frame, operand);
    case CS2_OP_POP_INT_LOCAL:
        return CS2VM2_Op_PopIntLocal(vm, frame, operand);
    case CS2_OP_POP_STRING_LOCAL:
        return CS2VM2_Op_PopStrLocal(vm, frame, operand);
    case CS2_OP_JOIN_STRING:
        return CS2VM2_Op_JoinString(vm, frame, operand);
    case CS2_OP_STRING_LENGTH:
        return CS2VM2_Op_StringLength(vm, frame, operand);
    case CS2_OP_PARAHEIGHT:
        return CS2VM2_Op_ParaHeight(vm, frame, operand);
    case CS2_OP_PARAWIDTH:
        return CS2VM2_Op_ParaWidth(vm, frame, operand);
    case CS2_OP_TOSTRING:
        return CS2VM2_Op_ToString(vm, frame, operand);
    case CS2_OP_APPEND:
        return CS2VM2_Op_Append(vm, frame, operand);
    case CS2_OP_APPEND_NUM:
        return CS2VM2_Op_AppendNum(vm, frame, operand);
    case CS2_OP_APPEND_SIGNNUM:
        return CS2VM2_Op_AppendSignNum(vm, frame, operand);
    case CS2_OP_APPEND_CHAR:
        return CS2VM2_Op_AppendChar(vm, frame, operand);
    case CS2_OP_GOSUB_WITH_PARAMS:
        return CS2VM2_Op_GosubWithParams(vm, frame, operand);
    case CS2_OP_POP_INT_DISCARD:
        return CS2VM2_Op_PopIntDiscard(vm, frame, operand);
    case CS2_OP_POP_STRING_DISCARD:
        return CS2VM2_Op_PopStrDiscard(vm, frame, operand);
    case CS2_OP_ENUM_STRING:
        return CS2VM2_Op_EnumString(vm, frame, operand);
    case CS2_OP_ENUM:
        return CS2VM2_Op_Enum(vm, frame, operand);
    case CS2_OP_ENUM_GETOUTPUTCOUNT:
        return CS2VM2_Op_EnumGetOutputCount(vm, frame, operand);
    /* Client database family (7500..7510): one handler, host-driven stack. */
    case CS2_OP_DB_FIND_WITH_COUNT:
    case CS2_OP_DB_FINDNEXT:
    case CS2_OP_DB_GETFIELD:
    case CS2_OP_DB_GETFIELDCOUNT:
    case CS2_OP_DB_FINDALL_WITH_COUNT:
    case CS2_OP_DB_GETROWTABLE:
    case CS2_OP_DB_GETROW:
    case CS2_OP_DB_FIND_FILTER_WITH_COUNT:
    case CS2_OP_DB_FIND:
    case CS2_OP_DB_FINDALL:
    case CS2_OP_DB_FIND_FILTER:
        return CS2VM2_Op_Db(vm, opcode);
    case CS2_OP_MAP_MEMBERS:
        return CS2VM2_Op_IsMapMembers(vm, frame, operand);
    case CS2_OP_ON_MOBILE:
        return CS2VM2_Op_OnMobile(vm, frame, operand);
    case CS2_OP_LOGIN_INT24:
        return CS2VM2_Op_LoginInt24(vm, frame, operand);
    case CS2_OP_GETCANVASSIZE:
        return CS2VM2_Op_GetCanvasSize(vm, frame, operand);
    /* All of 6200..6205 route through the host so a SET/CLAMP round-trips
     * through the matching GET; see CS2VM2_Op_Viewport. GETEFFECTIVESIZE joins
     * them because its answer is the viewport WIDGET's box put through the
     * CLAMPFOV letterbox, and only the host can reach either. */
    case CS2_OP_VIEWPORT_SETFOV:
    case CS2_OP_VIEWPORT_SETZOOM:
        return CS2VM2_Op_Viewport(vm, opcode, 2);
    case CS2_OP_VIEWPORT_CLAMPFOV:
        return CS2VM2_Op_Viewport(vm, opcode, 4);
    case CS2_OP_VIEWPORT_GETZOOM:
    case CS2_OP_VIEWPORT_GETFOV:
    case CS2_OP_VIEWPORT_GETEFFECTIVESIZE:
        return CS2VM2_Op_Viewport(vm, opcode, 0);
    /* UI zoom (6210..6214): SET pops a value, GET/RESET/GETDEFAULT pop nothing. */
    case CS2_OP_UIZOOM_SET:
        return CS2VM2_Op_UiZoom(vm, opcode, true);
    case CS2_OP_UIZOOM_GET:
    case CS2_OP_UIZOOM_RESET:
    case CS2_OP_UIZOOM_GETDEFAULT:
        return CS2VM2_Op_UiZoom(vm, opcode, false);
    /* Safe-area bounds (6220..6223): no-arg getters. Numeric id 6231 was
     * reclaimed by rev 239 and now consumes two ints; it falls through to the
     * generated stack handler below. */
    case CS2_OP_SAFEAREA_GETMINX:
    case CS2_OP_SAFEAREA_GETMINY:
    case CS2_OP_SAFEAREA_GETMAXX:
    case CS2_OP_SAFEAREA_GETMAXY:
        return CS2VM2_Op_SafeArea(vm, opcode);
    case CS2_OP_LOGOUT:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_LOGOUT;
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_GETWINDOWMODE:
        return CS2VM2_Op_GetWindowMode(vm, frame, operand);
    case CS2_OP_GETDEFAULTWINDOWMODE:
        return CS2VM2_Op_GetDefaultWindowMode(vm, frame, operand);
    case CS2_OP_SETWINDOWMODE:
        return CS2VM2_Op_SetWindowMode(vm, false);
    case CS2_OP_SETDEFAULTWINDOWMODE:
        return CS2VM2_Op_SetWindowMode(vm, true);
    case CS2_OP_CLIENTTYPE:
        return CS2VM2_Op_ClientType(vm, frame, operand);
    case CS2_OP_COORD:
        return CS2VM2_Op_Coord(vm, frame, operand);
    case CS2_OP_COORDX:
        return CS2VM2_Op_CoordX(vm, frame, operand);
    case CS2_OP_COORDY:
        return CS2VM2_Op_CoordY(vm, frame, operand);
    case CS2_OP_COORDZ:
        return CS2VM2_Op_CoordZ(vm, frame, operand);
    case CS2_OP_MOVECOORD:
        return CS2VM2_Op_MoveCoord(vm, frame, operand);
    /* The minimap run orb reads both every frame it repaints. They used to
     * push 0 (RUNWEIGHT_VISIBLE) and fall to the stack-meta stub (3321), which
     * is why the orb read "0" no matter what UPDATE_RUNENERGY said. */
    case CS2_OP_RUNENERGY_VISIBLE:
    case CS2_OP_RUNWEIGHT_VISIBLE:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = opcode == CS2_OP_RUNENERGY_VISIBLE ? CS2VM_HOST_REQUEST_RUNENERGY
                                                          : CS2VM_HOST_REQUEST_RUNWEIGHT;
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_INV_SIZE:
        return CS2VM2_Op_InvSize(vm, frame, operand);
    case CS2_OP_INV_GETOBJ:
        return CS2VM2_Op_InvGetObj(vm, frame, operand);
    case CS2_OP_INV_GETNUM:
        return CS2VM2_Op_InvGetNum(vm, frame, operand);
    case CS2_OP_INV_TOTAL:
        return CS2VM2_Op_InvTotal(vm, frame, operand);
    case CS2_OP_CC_DELETEALL:
        return CS2VM2_Op_CC_DeleteAll(vm, frame);
    case CS2_OP_CC_DELETE:
        return CS2VM2_Op_CC_Delete(vm, frame, operand);
    case CS2_OP_CC_CREATE:
        return CS2VM2_Op_CC_Create(vm, frame, operand);
    case CS2_OP_CC_COPY:
        return CS2VM2_Op_CC_Copy(vm, frame, operand);
    case CS2_OP_CC_FIND:
        return CS2VM2_Op_CC_Find(vm, frame, operand);
    case CS2_OP_CC_CREATECHILD:
        return CS2VM2_Op_CC_CreateChild(vm, frame, operand);
    case CS2_OP_CC_CREATESIBLING:
        return CS2VM2_Op_CC_CreateSibling(vm, frame, operand);
    case CS2_OP_CC_FINDROOT:
        return CS2VM2_Op_CC_FindRoot(vm, frame, operand);
    case CS2_OP_CC_CHILDREN_FIND:
        return CS2VM2_Op_CC_ChildrenFind(vm, frame, operand);
    case CS2_OP_CC_CHILDREN_FINDNEXTID:
        return CS2VM2_Op_CC_ChildrenFindNextId(vm, frame, operand);
    case CS2_OP_CC_CHILDREN_FINDNEXT:
        return CS2VM2_Op_CC_ChildrenFindNext(vm, frame, operand);
    case CS2_OP_CC_CHILDREN_FIND_COUNT:
        return CS2VM2_Op_CC_ChildrenFindCount(vm, frame, operand);
    case CS2_OP_IF_CHILDREN_FIND:
        return CS2VM2_Op_IF_ChildrenFind(vm, frame, operand);
    case CS2_OP_IF_CHILDREN_FINDNEXTID:
        return CS2VM2_Op_IF_ChildrenFindNextId(vm, frame, operand);
    case CS2_OP_IF_CHILDREN_COLLECT:
        return CS2VM2_Op_IF_ChildrenCollect(vm, frame, operand);
    case CS2_OP_CHILDREN_ARRAY:
        return CS2VM2_Op_ChildrenArray(vm, frame, operand);
    case CS2_OP_CC_SETPOSITION:
        return CS2VM2_Op_CC_SetPosition(vm, frame, operand);
    case CS2_OP_CC_SETSIZE:
        return CS2VM2_Op_CC_SetSize(vm, frame, operand);
    case CS2_OP_CC_SETGRAPHIC:
        return CS2VM2_Op_CC_SetGraphic(vm, frame, operand);
    case CS2_OP_CC_SETTILING:
        return CS2VM2_Op_CC_SetTiling(vm, frame, operand);
    case CS2_OP_IF_SETTILING:
        return CS2VM2_Op_IF_SetTiling(vm, frame, operand);
    case CS2_OP_CC_SETOUTLINE:
        return CS2VM2_Op_CC_SetOutline(vm, frame, operand);
    case CS2_OP_CC_SETGRAPHICSHADOW:
        return CS2VM2_Op_CC_SetGraphicShadow(vm, frame, operand);
    case CS2_OP_IF_SETGRAPHICSHADOW:
        return CS2VM2_Op_IF_SetGraphicShadow(vm, frame, operand);
    case CS2_OP_CC_SETCOLOUR:
        return CS2VM2_Op_CC_SetColour(vm, frame, operand);
    case CS2_OP_IF_SETCOLOUR:
        return CS2VM2_Op_IF_SetColour(vm, frame, operand);
    case CS2_OP_CC_SETFILL:
        return CS2VM2_Op_CC_SetFill(vm, frame, operand);
    case CS2_OP_IF_SETFILL:
        return CS2VM2_Op_IF_SetFill(vm, frame, operand);
    case CS2_OP_CC_SETTRANS:
        return CS2VM2_Op_CC_SetTrans(vm, frame, operand);
    case CS2_OP_IF_SETTRANS:
        return CS2VM2_Op_IF_SetTrans(vm, frame, operand);
    case CS2_OP_CC_SETNOCLICKTHROUGH:
        return CS2VM2_Op_CC_SetNoClickThrough(vm, frame, operand);
    case CS2_OP_CC_SETTEXT:
        return CS2VM2_Op_CC_SetText(vm, frame, operand);
    case CS2_OP_CC_SETTEXTFONT:
        return CS2VM2_Op_CC_SetTextFont(vm, frame, operand);
    case CS2_OP_IF_SETTEXTFONT:
        return CS2VM2_Op_IF_SetTextFont(vm, frame, operand);
    case CS2_OP_CC_SETTEXTALIGN:
        return CS2VM2_Op_CC_SetTextAlign(vm, frame, operand);
    case CS2_OP_IF_SETTEXTALIGN:
        return CS2VM2_Op_IF_SetTextAlign(vm, frame, operand);
    case CS2_OP_CC_SETTEXTSHADOW:
        return CS2VM2_Op_CC_SetTextShadow(vm, frame, operand);
    case CS2_OP_IF_SETTEXTSHADOW:
        return CS2VM2_Op_IF_SetTextShadow(vm, frame, operand);
    case CS2_OP_CC_SETDRAGGABLE:
        return CS2VM2_Op_CC_SetDraggable(vm, frame, operand);
    case CS2_OP_CC_SETDRAGGABLEBEHAVIOR:
        return CS2VM2_Op_CC_SetDraggableBehavior(vm, frame, operand);
    case CS2_OP_CC_SETDRAGDEADZONE:
        return CS2VM2_Op_CC_SetDragDeadZone(vm, frame, operand);
    case CS2_OP_CC_SETDRAGDEADTIME:
        return CS2VM2_Op_CC_SetDragDeadTime(vm, frame, operand);
    /* Three opcodes, one handler, and the difference is only the count-text
     * mode the widget keeps: 0 = draw when stackable (plain SETOBJECT),
     * 1 = always, 2 = never (NONUM — the spell tooltip's rune icons). */
    case CS2_OP_CC_SETOBJECT:
        return CS2VM2_Op_CC_SetObject(vm, frame, operand, 0);
    case CS2_OP_CC_SETOBJECT_ALWAYS_NUM:
        return CS2VM2_Op_CC_SetObject(vm, frame, operand, 1);
    case CS2_OP_CC_SETOBJECT_NONUM:
        return CS2VM2_Op_CC_SetObject(vm, frame, operand, 2);
    case CS2_OP_CC_SETOP:
        return CS2VM2_Op_CC_SetOp(vm, frame, operand);
    case CS2_OP_CC_SETOPBASE:
        return CS2VM2_Op_CC_SetOpBase(vm, frame, operand);
    case CS2_OP_CC_SETTARGETVERB:
        return CS2VM2_Op_CC_SetTargetVerb(vm, frame, operand);
    case CS2_OP_CC_CLEAROPS:
        return CS2VM2_Op_CC_ClearOps(vm, frame, operand);
    case CS2_OP_CC_SETOPFORCELEFTCLICK:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FORCE_LEFT_CLICK);
    case CS2_OP_CC_OP1309:
    {
        /* Client stub: discard one int and continue. */
        int discard;
        if( CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_CC_CLEAROPSUBMENU:
        return CS2VM2_Op_CC_ClearOpSubmenu(vm, frame, operand);
    case CS2_OP_CC_SETOPSUBMENU:
        return CS2VM2_Op_CC_SetOpSubmenu(vm, frame, operand);
    case CS2_OP_CC_SETTARGETPRIORITY:
        return CS2VM2_Op_CC_SetTargetPriority(vm, frame, operand);
    case CS2_OP_CC_SETHIDE:
        return CS2VM2_Op_CC_SetHide(vm, frame, operand);
    case CS2_OP_CC_GETID:
        return CS2VM2_Op_CC_GetId(vm, frame, operand);
    case CS2_OP_CC_GETX:
        return CS2VM2_Op_CC_GetX(vm, frame, operand);
    case CS2_OP_CC_GETY:
        return CS2VM2_Op_CC_GetY(vm, frame, operand);
    case CS2_OP_CC_GETWIDTH:
        return CS2VM2_Op_CC_GetWidth(vm, frame, operand);
    case CS2_OP_CC_GETHEIGHT:
        return CS2VM2_Op_CC_GetHeight(vm, frame, operand);
    case CS2_OP_CC_GETHIDE:
        return CS2VM2_Op_CC_GetHide(vm, frame, operand);
    case CS2_OP_CC_GETOP:
        return CS2VM2_Op_CC_GetOp(vm, frame, operand);
    case CS2_OP_CC_SETONCLICK:
        return CS2VM2_Op_CC_SetOnClick(vm, frame, operand);
    case CS2_OP_CC_SETONHOLD:
        return CS2VM2_Op_CC_SetOnHold(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSEOVER:
        return CS2VM2_Op_CC_SetOnMouseOver(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSELEAVE:
        return CS2VM2_Op_CC_SetOnMouseLeave(vm, frame, operand);
    case CS2_OP_CC_SETONTARGETENTER:
        return CS2VM2_Op_CC_SetOnTargetEnter(vm, frame, operand);
    case CS2_OP_CC_SETONTARGETLEAVE:
        return CS2VM2_Op_CC_SetOnTargetLeave(vm, frame, operand);
    case CS2_OP_CC_SETONCLICKREPEAT:
        return CS2VM2_Op_CC_SetOnClickRepeat(vm, frame, operand);
    case CS2_OP_CC_SETONRELEASE:
        return CS2VM2_Op_CC_SetOnRelease(vm, frame, operand);
    case CS2_OP_CC_SETONDIALOGABORT:
        return CS2VM2_Op_CC_SetOnDialogAbort(vm, frame, operand);
    case CS2_OP_CC_SETONFRIENDTRANSMIT:
        return CS2VM2_Op_CC_SetOnFriendTransmit(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSEREPEAT:
        return CS2VM2_Op_CC_SetOnMouseRepeat(vm, frame, operand);
    case CS2_OP_CC_SETONDRAG:
        return CS2VM2_Op_CC_SetOnDrag(vm, frame, operand);
    case CS2_OP_CC_SETONSCROLLWHEEL:
        return CS2VM2_Op_CC_SetOnScrollWheel(vm, frame, operand);
    case CS2_OP_CC_SETONKEY:
        return CS2VM2_Op_CC_SetOnKey(vm, frame, operand);
    /* Named SETONITEMONITEM / SETONCLANSETTINGS by the vendor table; they are
     * the key-down and key-up listeners. See CS2VM2_Op_CC_SetOnKeyDown. */
    case CS2_OP_CC_SETONITEMONITEM:
        return CS2VM2_Op_CC_SetOnKeyDown(vm, frame, operand);
    case CS2_OP_CC_SETONCLANSETTINGS:
        return CS2VM2_Op_CC_SetOnKeyUp(vm, frame, operand);
    case CS2_OP_CC_SETONOP:
        return CS2VM2_Op_CC_SetOnOp(vm, frame, operand);
    case CS2_OP_CC_SETONDRAGCOMPLETE:
        return CS2VM2_Op_CC_SetOnDragComplete(vm, frame, operand);
    case CS2_OP_CC_SETONRESIZE:
        return CS2VM2_Op_CC_SetOnResize(vm, frame, operand);
    case CS2_OP_CC_SETONSUBCHANGE:
        return CS2VM2_Op_CC_SetOnSubChange(vm, frame, operand);
    case CS2_OP_CC_TRIGGEROP:
        return CS2VM2_Op_CC_TriggerOp(vm, frame, operand);
    case CS2_OP_SOUND_SYNTH:
        return CS2VM2_Op_SoundSynth(vm, frame, operand);
    case CS2_OP_SOUND_SONG:
        return CS2VM2_Op_SoundSong(vm, frame, operand);
    case CS2_OP_SOUND_JINGLE:
        return CS2VM2_Op_SoundJingle(vm, frame, operand);
    case CS2_OP_SOUND_SONG_WITHSECONDARY:
        return CS2VM2_Op_SoundSongWithSecondary(vm, frame, operand);
    case CS2_OP_IF_GETWIDTH:
        return CS2VM2_Op_IF_GetWidth(vm, frame, operand);
    case CS2_OP_IF_GETHEIGHT:
        return CS2VM2_Op_IF_GetHeight(vm, frame, operand);
    case CS2_OP_IF_GETY:
        return CS2VM2_Op_IF_GetY(vm, frame, operand);
    case CS2_OP_IF_GETLAYER:
        return CS2VM2_Op_IF_GetLayer(vm, frame, operand);
    case CS2_OP_IF_GETTOP:
        return CS2VM2_Op_IF_GetTop(vm, frame, operand);
    case CS2_OP_IF_GETSCROLLX:
        return CS2VM2_Op_IF_GetScrollX(vm, frame, operand);
    case CS2_OP_IF_GETSCROLLY:
        return CS2VM2_Op_IF_GetScrollY(vm, frame, operand);
    case CS2_OP_IF_GETSCROLLHEIGHT:
        return CS2VM2_Op_IF_GetScrollHeight(vm, frame, operand);
    case CS2_OP_IF_GETHIDE:
        return CS2VM2_Op_IF_GetHide(vm, frame, operand);
    case CS2_OP_IF_GETOP:
        return CS2VM2_Op_IF_GetOp(vm, frame, operand);
    case CS2_OP_IF_HASSUB:
        return CS2VM2_Op_IF_HasSub(vm, frame, operand);
    case CS2_OP_IF_SETPARAM:
        return CS2VM2_Op_IF_SetParam(vm, frame, operand);
    case CS2_OP_IF_HASCHILD_OVERLAY:
        return CS2VM2_Op_IF_HasChild(vm, frame, operand);
    case CS2_OP_IF_SETHIDE:
        return CS2VM2_Op_IF_SetHide(vm, frame, operand);
    case CS2_OP_IF_SETPOSITION:
        return CS2VM2_Op_IF_SetPosition(vm, frame, operand);
    case CS2_OP_IF_SETSIZE:
        return CS2VM2_Op_IF_SetSize(vm, frame, operand);
    case CS2_OP_IF_SETSCROLLPOS:
        return CS2VM2_Op_IF_SetScrollPos(vm, frame, operand);
    case CS2_OP_IF_SETSCROLLSIZE:
        return CS2VM2_Op_IF_SetScrollSize(vm, frame, operand);
    case CS2_OP_IF_SETGRAPHIC:
        return CS2VM2_Op_IF_SetGraphic(vm, frame, operand);
    case CS2_OP_IF_SETGRAPHIC2:
        return CS2VM2_Op_IF_SetGraphic2(vm, frame, operand);
    case CS2_OP_IF_SETTEXT:
        return CS2VM2_Op_IF_SetText(vm, frame, operand);
    case CS2_OP_IF_SETOUTLINE:
        return CS2VM2_Op_IF_SetOutline(vm, frame, operand);
    case CS2_OP_IF_SETONVARTRANSMIT:
        return CS2VM2_Op_IF_SetOnVarTransmit(vm, frame, operand);
    /* Identical operand shape to the var one — script, captured args and a
     * trailing trigger list — so it shares the parser and differs only in the
     * request kind. It was in the discard group above, which parsed the operands
     * correctly and threw the registration away. */
    case CS2_OP_IF_SETONSTATTRANSMIT:
        return CS2VM2_Op_IF_SetOnStatTransmit(vm, frame, operand);
    case CS2_OP_IF_SETONINVTRANSMIT:
        return CS2VM2_Op_IF_SetOnInvTransmit(vm, frame, operand);
    case CS2_OP_IF_SETONOP:
        return CS2VM2_Op_IF_SetOnOp(vm, frame, operand);
    case CS2_OP_IF_SETONCLICK:
        return CS2VM2_Op_IF_SetOnClick(vm, frame, operand);
    case CS2_OP_IF_SETONMOUSEOVER:
        return CS2VM2_Op_IF_SetOnMouseOver(vm, frame, operand);
    case CS2_OP_IF_SETONMOUSELEAVE:
        return CS2VM2_Op_IF_SetOnMouseLeave(vm, frame, operand);
    case CS2_OP_IF_SETONTARGETENTER:
        return CS2VM2_Op_IF_SetOnTargetEnter(vm, frame, operand);
    case CS2_OP_IF_SETONTARGETLEAVE:
        return CS2VM2_Op_IF_SetOnTargetLeave(vm, frame, operand);
    case CS2_OP_IF_SETONCLICKREPEAT:
        return CS2VM2_Op_IF_SetOnClickRepeat(vm, frame, operand);
    case CS2_OP_IF_SETONRELEASE:
        return CS2VM2_Op_IF_SetOnRelease(vm, frame, operand);
    case CS2_OP_IF_SETONDIALOGABORT:
        return CS2VM2_Op_IF_SetOnDialogAbort(vm, frame, operand);
    case CS2_OP_IF_SETONMOUSEREPEAT:
        return CS2VM2_Op_IF_SetOnMouseRepeat(vm, frame, operand);
    case CS2_OP_IF_SETONTIMER:
        return CS2VM2_Op_IF_SetOnTimer(vm, frame, operand);
    case CS2_OP_IF_SETONSCROLLWHEEL:
        return CS2VM2_Op_IF_SetOnScrollWheel(vm, frame, operand);
    case CS2_OP_IF_SETONKEY:
        return CS2VM2_Op_IF_SetOnKey(vm, frame, operand);
    case CS2_OP_IF_SETONITEMONITEM:
        return CS2VM2_Op_IF_SetOnKeyDown(vm, frame, operand);
    case CS2_OP_IF_SETONCLANSETTINGS:
        return CS2VM2_Op_IF_SetOnKeyUp(vm, frame, operand);
    case CS2_OP_IF_SETONMISCTRANSMIT:
        return CS2VM2_Op_IF_SetOnMiscTransmit(vm, frame, operand);
    case CS2_OP_IF_SETONFRIENDTRANSMIT:
        return CS2VM2_Op_IF_SetOnFriendTransmit(vm, frame, operand);
    case CS2_OP_IF_SETOP:
        return CS2VM2_Op_IF_SetOp(vm, frame, operand);
    case CS2_OP_IF_SETOPBASE:
        return CS2VM2_Op_IF_SetOpBase(vm, frame, operand);
    case CS2_OP_IF_SETTARGETVERB:
        return CS2VM2_Op_IF_SetTargetVerb(vm, frame, operand);
    case CS2_OP_IF_SETOPSUBMENU:
        return CS2VM2_Op_IF_SetOpSubmenu(vm, frame, operand);
    case CS2_OP_IF_SETTARGETPRIORITY:
        return CS2VM2_Op_IF_SetTargetPriority(vm, frame, operand);
    case CS2_OP_IF_CLEAROPS:
        return CS2VM2_Op_IF_ClearOps(vm, frame, operand);
    case CS2_OP_IF_OP2309:
    {
        /* Client stub (CC_OP1309 IF counterpart): pop component + one int. */
        int discard;
        if( CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_IF_SETOBJECT:
        return CS2VM2_Op_IF_SetObject(vm, frame, operand, 0);
    case CS2_OP_IF_SETOBJECT_ALWAYS_NUM:
        return CS2VM2_Op_IF_SetObject(vm, frame, operand, 1);
    case CS2_OP_IF_SETOBJECT_NONUM:
        return CS2VM2_Op_IF_SetObject(vm, frame, operand, 2);
    case CS2_OP_BRANCH_LESS_THAN:
        return CS2VM2_Op_BranchLessThan(vm, frame, operand);
    case CS2_OP_BRANCH_GREATER_THAN:
        return CS2VM2_Op_BranchGreaterThan(vm, frame, operand);
    case CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
        return CS2VM2_Op_BranchLessThanOrEquals(vm, frame, operand);
    case CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
        return CS2VM2_Op_BranchGreaterThanOrEquals(vm, frame, operand);
    case CS2_OP_BRANCH_EQUALS:
        return CS2VM2_Op_BranchEquals(vm, frame, operand);
    case CS2_OP_BRANCH_NOT:
        return CS2VM2_Op_BranchNotEquals(vm, frame, operand);
    case CS2_OP_BRANCH:
        return CS2VM2_Op_Branch(vm, frame, operand);
    case CS2_OP_BRANCH_IF_ONE:
        return CS2VM2_Op_BranchIfOne(vm, frame, operand);
    case CS2_OP_SWITCH:
        return CS2VM2_Op_Switch(vm, frame, operand);
    case CS2_OP_RETURN:
        return CS2VM2_Op_Return(vm, frame);
    case CS2_OP_ADD:
        return CS2VM2_Op_Add(vm, frame, operand);
    case CS2_OP_SUB:
        return CS2VM2_Op_Sub(vm, frame, operand);
    case CS2_OP_MULTIPLY:
        return CS2VM2_Op_Mul(vm, frame, operand);
    case CS2_OP_DIV:
        return CS2VM2_Op_Div(vm, frame, operand);
    case CS2_OP_MOD:
        return CS2VM2_Op_Mod(vm, frame, operand);
    case CS2_OP_POW:
        return CS2VM2_Op_Pow(vm, frame, operand);
    case CS2_OP_SCALE:
        return CS2VM2_Op_Scale(vm, frame, operand);
    case CS2_OP_TESTBIT:
        return CS2VM2_Op_TestBit(vm, frame, operand);
    case CS2_OP_OC_PARAM:
        return CS2VM2_Op_OC_Param(vm, frame, operand);
    case CS2_OP_OC_NAME:
        return CS2VM2_Op_OC_Name(vm, frame, operand);
    case CS2_OP_NC_NAME:
        return CS2VM2_Op_NC_Name(vm, frame, operand);
    case CS2_OP_OC_UNPLACEHOLDER:
        return CS2VM2_Op_OC_Unplaceholder(vm, frame, operand);
    case CS2_OP_OC_OP:
        return CS2VM2_Op_OC_ActionString(vm, opcode, operand, CS2VM_HOST_REQUEST_OC_OP);
    case CS2_OP_OC_IOP:
        return CS2VM2_Op_OC_ActionString(vm, opcode, operand, CS2VM_HOST_REQUEST_OC_IOP);
    case CS2_OP_OC_EXAMINE:
        return CS2VM2_Op_OC_Examine(vm);
    case CS2_OP_OC_PLACEHOLDER:
        return CS2VM2_Op_OC_Placeholder(vm);
    case CS2_OP_OC_FIND:
    case CS2_OP_OC_FINDNEXT:
    case CS2_OP_OC_FINDRESET:
        return CS2VM2_Op_OC_Find(vm, opcode);
    case CS2_OP_OC_SHIFTCLICKIOP:
        return CS2VM2_Op_OC_ShiftClickIop(vm);
    case CS2_OP_OC_WEARPOS:
    case CS2_OP_OC_WEARPOS2:
    case CS2_OP_OC_WEARPOS3:
        return CS2VM2_Op_OC_WearPos(vm, opcode);
    case CS2_OP_OC_WEIGHT:
        return CS2VM2_Op_OC_Weight(vm);
    case CS2_OP_OC_ISUBOP:
        return CS2VM2_Op_OC_Isubop(vm);
    case CS2_OP_CC_SETSCROLLPOS:
        return CS2VM2_Op_CC_SetScrollPos(vm, frame, operand);
    case CS2_OP_CC_SETSCROLLSIZE:
        return CS2VM2_Op_CC_SetScrollSize(vm, frame, operand);
    case CS2_OP_CC_SETMODEL:
        return CS2VM2_Op_CC_SetModel(vm, frame, operand);
    case CS2_OP_CC_SETMODELANGLE:
        return CS2VM2_Op_CC_SetModelAngle(vm, frame, operand);
    case CS2_OP_CC_SETMODELANIM:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ANIM);
    case CS2_OP_CC_SETMODELORTHOG:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ORTHOG);
    case CS2_OP_CC_SETMODELTRANSPARENT:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_TRANSPARENT);
    case CS2_OP_CC_SETHFLIP:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_HFLIP);
    case CS2_OP_CC_SETVFLIP:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_VFLIP);
    case CS2_OP_CC_SET2DANGLE:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_ANGLE_2D);
    case CS2_OP_CC_SETFILLCOLOUR:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_COLOUR);
    case CS2_OP_CC_SETLINEWID:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_WIDTH);
    case CS2_OP_CC_SETLINEDIRECTION:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_DIRECTION);
    case CS2_OP_CC_SETGRAPHIC2:
        return CS2VM2_Op_CC_SetGraphic2(vm, frame, operand);
    case CS2_OP_CC_SETTRANSBOT:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_TRANS_BOT);
    case CS2_OP_CC_SETFILLMODE:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_MODE);
    case CS2_OP_CC_SETARC:
        return CS2VM2_Op_CC_SetArc(vm, frame, operand);
    case CS2_OP_CC_SETNOSCROLLTHROUGH:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_NO_SCROLL_THROUGH);
    case CS2_OP_CC_SETPINCH:
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_PINCH);
    case CS2_OP_CC_SETNPCHEAD:
        return CS2VM2_Op_CC_SetModelKind(vm, frame, operand, CS2VM_MODEL_KIND_NPC_HEAD, true);
    case CS2_OP_CC_SETPLAYERHEAD_SELF:
        return CS2VM2_Op_CC_SetModelKind(vm, frame, operand, CS2VM_MODEL_KIND_PLAYER_SELF, false);
    case CS2_OP_CC_SETPLAYERMODEL_SELF:
        return CS2VM2_Op_CC_SetModelKind(vm, frame, operand, CS2VM_MODEL_KIND_PLAYER_SELF, true);
    case CS2_OP_CC_SETMODEL_PLAYERCHATHEAD:
        return CS2VM2_Op_CC_SetModelKind(
            vm, frame, operand, CS2VM_MODEL_KIND_PLAYER_CHATHEAD, true);
    case CS2_OP_CC_RESUME_PAUSEBUTTON:
    {
        /* No stack args — the active/dot component is the button. Stack
         * signature is {0,0,0,0}; do not route through WidgetInt (which pops
         * a phantom value). */
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_RESUME_PAUSEBUTTON;
        request.u.resume_pausebutton.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_CC_INPUT_SETSUBMITMODE:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_SUBMITMODE);
    case CS2_OP_CC_INPUT_SETSELECTCOLOUR:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_SELECTCOLOUR);
    case CS2_OP_CC_INPUT_SETACCEPTMODE:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_ACCEPTMODE);
    case CS2_OP_CC_INPUT_SETWRAPMODE:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_WRAPMODE);
    case CS2_OP_CC_INPUT_SETLINEWRAPPINGWIDTH:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_LINEWRAPPINGWIDTH);
    case CS2_OP_CC_INPUT_SETSELECTBGCOLOUR:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_SELECTBGCOLOUR);
    case CS2_OP_CC_INPUT_SETLINECOUNTLIMIT:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_LINECOUNTLIMIT);
    case CS2_OP_CC_INPUT_SETCURSORCOLOUR:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORCOLOUR);
    case CS2_OP_CC_INPUT_SETCURSORTRANS:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORTRANS);
    case CS2_OP_CC_INPUT_SETCURSORWIDTH:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORWIDTH);
    case CS2_OP_CC_INPUT_SETCURSORHEIGHT:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORHEIGHT);
    case CS2_OP_CC_INPUT_SETCURSOROFFSET:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSOROFFSET);
    case CS2_OP_CC_INPUT_SETLINEWIDTHLIMIT:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_LINEWIDTHLIMIT);
    case CS2_OP_CC_INPUT_SETCHARFILTER:
        return CS2VM2_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CHARFILTER);
    case CS2_OP_IF_SETMODEL:
        return CS2VM2_Op_IF_SetModel(vm, frame, operand);
    case CS2_OP_IF_SETMODELANGLE:
        return CS2VM2_Op_IF_SetModelAngle(vm, frame, operand);
    case CS2_OP_IF_SETMODELANIM:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ANIM);
    case CS2_OP_IF_SETMODELORTHOG:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ORTHOG);
    case CS2_OP_IF_SETMODELTRANSPARENT:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_TRANSPARENT);
    case CS2_OP_IF_SETHFLIP:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_HFLIP);
    case CS2_OP_IF_SETVFLIP:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_VFLIP);
    case CS2_OP_IF_SET2DANGLE:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_ANGLE_2D);
    case CS2_OP_IF_SETFILLCOLOUR:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_COLOUR);
    case CS2_OP_IF_SETLINEWID:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_WIDTH);
    case CS2_OP_IF_SETLINEDIRECTION:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_DIRECTION);
    case CS2_OP_IF_SETTRANSBOT:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_TRANS_BOT);
    case CS2_OP_IF_SETFILLMODE:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_MODE);
    case CS2_OP_IF_SETARC:
        return CS2VM2_Op_IF_SetArc(vm, frame, operand);
    case CS2_OP_IF_SETNOSCROLLTHROUGH:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_NO_SCROLL_THROUGH);
    case CS2_OP_IF_SETNOCLICKTHROUGH:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_NO_CLICK_THROUGH);
    case CS2_OP_IF_SETDRAGDEADZONE:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_DRAG_DEAD_ZONE);
    case CS2_OP_IF_SETDRAGDEADTIME:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_DRAG_DEAD_TIME);
    case CS2_OP_IF_SETCLICKMASK:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_CLICKMASK);
    case CS2_OP_IF_SETPINCH:
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_PINCH);
    case CS2_OP_IF_SETNPCHEAD:
        return CS2VM2_Op_IF_SetModelKind(vm, frame, operand, CS2VM_MODEL_KIND_NPC_HEAD, true);
    case CS2_OP_IF_SETPLAYERHEAD_SELF:
        return CS2VM2_Op_IF_SetModelKind(vm, frame, operand, CS2VM_MODEL_KIND_PLAYER_SELF, false);
    case CS2_OP_IF_SETMODEL_PLAYERCHATHEAD:
        return CS2VM2_Op_IF_SetModelKind(
            vm, frame, operand, CS2VM_MODEL_KIND_PLAYER_CHATHEAD, true);
    case CS2_OP_IF_RESUME_PAUSEBUTTON:
    {
        /* One component on the int stack. Stack signature is {1,0,0,0}; do not
         * route through WidgetInt (which also pops a phantom value). */
        struct CS2VM_HostRequest request;
        int component_id;
        memset(&request, 0, sizeof(request));
        if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        request.kind = CS2VM_HOST_REQUEST_RESUME_PAUSEBUTTON;
        request.u.resume_pausebutton.component_id = component_id;
        (void)frame;
        (void)operand;
        return vm->vm->host_exec(vm, &request);
    }
    /* Op-key bindings. Args are (is_if, is_typed[, is_ignore_held]). */
    case CS2_OP_CC_SETOPKEY:
        return CS2VM2_Op_SetOpKey(vm, frame, operand, 0, 0);
    case CS2_OP_CC_SETOPTKEY:
        return CS2VM2_Op_SetOpKey(vm, frame, operand, 0, 1);
    case CS2_OP_IF_SETOPKEY:
        return CS2VM2_Op_SetOpKey(vm, frame, operand, 1, 0);
    case CS2_OP_IF_SETOPTKEY:
        return CS2VM2_Op_SetOpKey(vm, frame, operand, 1, 1);
    case CS2_OP_CC_SETOPKEYRATE:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 0, 0, 0);
    case CS2_OP_CC_SETOPTKEYRATE:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 0, 1, 0);
    case CS2_OP_IF_SETOPKEYRATE:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 1, 0, 0);
    case CS2_OP_IF_SETOPTKEYRATE:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 1, 1, 0);
    case CS2_OP_CC_SETOPKEYIGNOREHELD:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 0, 0, 1);
    case CS2_OP_CC_SETOPTKEYIGNOREHELD:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 0, 1, 1);
    case CS2_OP_IF_SETOPKEYIGNOREHELD:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 1, 0, 1);
    case CS2_OP_IF_SETOPTKEYIGNOREHELD:
        return CS2VM2_Op_SetOpKeyRate(vm, frame, operand, 1, 1, 1);
    case CS2_OP_KEYHELD:
        return CS2VM2_Op_KeyQuery(vm, frame, CS2VM_HOST_REQUEST_KEYHELD);
    case CS2_OP_KEYPRESSED:
        return CS2VM2_Op_KeyQuery(vm, frame, CS2VM_HOST_REQUEST_KEYPRESSED);
    case CS2_OP_DEFINE_ARRAY:
        return CS2VM2_Op_DefineArray(vm, frame, operand);
    case CS2_OP_PUSH_ARRAY_INT:
        return CS2VM2_Op_PushArrayInt(vm, frame, operand);
    case CS2_OP_POP_ARRAY_INT:
        return CS2VM2_Op_PopArrayInt(vm, frame, operand);
    case CS2_OP_ARRAY_SORT_ALL:
        return CS2VM2_Op_ArraySortAll(vm, frame, operand);
    case CS2_OP_ARRAY_COUNT_MATCHES:
        return CS2VM2_Op_ArrayCountMatches(vm, frame, operand);
    case CS2_OP_ARRAY_LENGTH:
        return CS2VM2_Op_ArrayLength(vm, frame, operand);
    case CS2_OP_ARRAY_SPLIT:
        return CS2VM2_Op_ArraySplit(vm, frame, operand);
    case CS2_OP_ARRAY_JOIN:
        return CS2VM2_Op_ArrayJoin(vm, frame, operand);
    case CS2_OP_ARRAY_NEW:
        return CS2VM2_Op_ArrayNew(vm, frame, operand);
    case CS2_OP_ARRAY_SETLENGTH:
        return CS2VM2_Op_ArraySetLength(vm, frame, operand);
    case CS2_OP_ARRAY_APPEND:
        return CS2VM2_Op_ArrayAppend(vm, frame, operand);
    case CS2_OP_STRING_TO_INT:
        return CS2VM2_Op_StringToInt(vm, frame, operand);
    case CS2_OP_IF_FIND:
        return CS2VM2_Op_IF_Find(vm, frame, operand);
    case CS2_OP_CC_SETONVARTRANSMIT:
        return CS2VM2_Op_CC_SetOnVarTransmit(vm, frame, operand);
    case CS2_OP_CC_SETONTIMER:
        return CS2VM2_Op_CC_SetOnTimer(vm, frame, operand);
    case CS2_OP_CC_SETONINVTRANSMIT:
        return CS2VM2_Op_CC_SetOnInvTransmit(vm, frame, operand);
    case CS2_OP_CC_SETONSTATTRANSMIT:
        return CS2VM2_Op_CC_SetOnStatTransmit(vm, frame, operand);
    /* No model for these events yet. They MUST still be parsed: the handler
     * signature string drives how many operands to pop, so the static stack
     * table cannot describe them and the StackMetaStub fallback would pop
     * nothing and desync the operand stack for every later opcode. */
    case CS2_OP_CC_SETONCHATTRANSMIT:
    /* CC_SETONFRIENDTRANSMIT (1420) was the one member of this list nobody
     * added. Its IF twin was in the matching group, so the omission was
     * invisible until a script reached it — and then it did not no-op, it
     * ASSERTED, because the generated stack table has no signature for a
     * signature-driven SETON op. */
    case CS2_OP_CC_SETONCLANTRANSMIT:
    case CS2_OP_CC_SETONMISCTRANSMIT:
    case CS2_OP_CC_SETONSTOCKTRANSMIT:
    case CS2_OP_CC_SETONCLANSETTINGSTRANSMIT:
    case CS2_OP_CC_SETONCLANCHANNELTRANSMIT:
    case CS2_OP_CC_SETONMAPPOST:
    /* Input-field listeners: no text-entry model yet, but signature-driven
     * operand counts mean they must be parsed, not stubbed. */
    case CS2_OP_CC_INPUT_SETONSUBMIT:
    case CS2_OP_CC_INPUT_SETONABORT:
    case CS2_OP_CC_INPUT_SETONFOCUSCHANGED:
    case CS2_OP_CC_INPUT_SETONUPDATE:
        return CS2VM2_Op_CC_SetOnEventDiscard(vm, frame, operand);
    case CS2_OP_CC_GETTEXT:
        return CS2VM2_Op_CC_GetText(vm, frame, operand);
    case CS2_OP_CC_GETCOLOUR:
        return cs2vm2_op_cc_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_CC_GETCOLOUR);
    case CS2_OP_CC_GETFILLCOLOUR:
        return cs2vm2_op_cc_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_CC_GETFILLCOLOUR);
    case CS2_OP_CC_GETINVOBJECT:
        return cs2vm2_op_cc_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_CC_GETINVOBJECT);
    case CS2_OP_CC_GETINVCOUNT:
        return cs2vm2_op_cc_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_CC_GETINVCOUNT);
    case CS2_OP_CC_GETTRANS:
        return CS2VM2_Op_CC_GetTrans(vm, frame, operand);
    case CS2_OP_CC_GETTARGETMASK:
        return cs2vm2_op_cc_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_CC_GETTARGETMASK);
    case CS2_OP_CC_GETCOMPONENTPARAM:
        return CS2VM2_Op_CC_GetComponentParam(vm, frame, operand);
    case CS2_OP_IF_GETCOMPONENTPARAM:
        return CS2VM2_Op_IF_GetComponentParam(vm, frame, operand);
    case CS2_OP_CC_SETCOMPONENTPARAM:
        return CS2VM2_Op_CC_SetComponentParam(vm, frame, operand);
    case CS2_OP_IF_SETONHOLD:
        return CS2VM2_Op_IF_SetOnHold(vm, frame, operand);
    /* Friend transmit is registered for both IF and CC forms now. */
    case CS2_OP_IF_SETONCLANTRANSMIT:
    case CS2_OP_IF_SETONCLANSETTINGSTRANSMIT:
    case CS2_OP_IF_SETONCLANCHANNELTRANSMIT:
    /* Same reasoning as the CC_SETON* discard group above: signature-driven
     * operand counts, so they must be parsed rather than left to the stub. */
    case CS2_OP_IF_SETONCHATTRANSMIT:
    case CS2_OP_IF_SETONSTOCKTRANSMIT:
    case CS2_OP_IF_SETONMAPPOST:
    case CS2_OP_IF_INPUT_SETONSUBMIT:
    case CS2_OP_IF_INPUT_SETONABORT:
    case CS2_OP_IF_INPUT_SETONFOCUSCHANGED:
    case CS2_OP_IF_INPUT_SETONUPDATE:
        return CS2VM2_Op_IF_SetOnEventDiscard(vm, frame, operand);
    case CS2_OP_IF_SETONDRAG:
        return CS2VM2_Op_IF_SetOnDrag(vm, frame, operand);
    case CS2_OP_IF_SETONDRAGCOMPLETE:
        return CS2VM2_Op_IF_SetOnDragComplete(vm, frame, operand);
    case CS2_OP_IF_SETONSUBCHANGE:
        return CS2VM2_Op_IF_SetOnSubChange(vm, frame, operand);
    case CS2_OP_IF_SETONRESIZE:
        return CS2VM2_Op_IF_SetOnResize(vm, frame, operand);
    case CS2_OP_IF_CALLONRESIZE:
        return CS2VM2_Op_IF_CallOnResize(vm, frame, operand);
    case CS2_OP_IF_TRIGGEROPLOCAL:
        return CS2VM2_Op_IF_TriggerOpLocal(vm, frame, operand);
    case CS2_OP_IF_SETDRAGGABLE:
        return CS2VM2_Op_IF_SetDraggable(vm, frame, operand);
    case CS2_OP_IF_SETDRAGGABLEBEHAVIOR:
        return CS2VM2_Op_IF_SetDraggableBehavior(vm, frame, operand);
    case CS2_OP_IF_DRAGPICKUP:
        return CS2VM2_Op_DragPickup(vm, frame, operand, CS2VM_HOST_REQUEST_IF_DRAGPICKUP);
    case CS2_OP_CC_DRAGPICKUP:
        return CS2VM2_Op_DragPickup(vm, frame, operand, CS2VM_HOST_REQUEST_CC_DRAGPICKUP);
    case CS2_OP_SETANTIDRAG:
        return CS2VM2_Op_SetAntiDrag(vm, frame, operand);
    case CS2_OP_IF_GETX:
        return CS2VM2_Op_IF_GetX(vm, frame, operand);
    case CS2_OP_IF_GETTEXT:
        return CS2VM2_Op_IF_GetText(vm, frame, operand);
    case CS2_OP_IF_GETCOLOUR:
        return cs2vm2_op_if_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_IF_GETCOLOUR);
    case CS2_OP_IF_GETFILLCOLOUR:
        return cs2vm2_op_if_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_IF_GETFILLCOLOUR);
    case CS2_OP_IF_GETINVOBJECT:
        return cs2vm2_op_if_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_IF_GETINVOBJECT);
    case CS2_OP_IF_GETINVCOUNT:
        return cs2vm2_op_if_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_IF_GETINVCOUNT);
    case CS2_OP_IF_GETTARGETMASK:
        return cs2vm2_op_if_get_int(vm, frame, operand, CS2VM_HOST_REQUEST_IF_GETTARGETMASK);
    case CS2_OP_IF_GETSCROLLWIDTH:
        return CS2VM2_Op_IF_GetScrollWidth(vm, frame, operand);
    case CS2_OP_SETBIT:
        return CS2VM2_Op_SetBit(vm, frame, operand);
    case CS2_OP_CLEARBIT:
        return CS2VM2_Op_ClearBit(vm, frame, operand);
    case CS2_OP_OR:
        return CS2VM2_Op_Or(vm, frame, operand);
    case CS2_OP_AND:
        return CS2VM2_Op_And(vm, frame, operand);
    case CS2_OP_MIN:
        return CS2VM2_Op_Min(vm, frame, operand);
    case CS2_OP_MAX:
        return CS2VM2_Op_Max(vm, frame, operand);
    case CS2_OP_ADDPERCENT:
        return CS2VM2_Op_AddPercent(vm, frame, operand);
    case CS2_OP_BITCOUNT:
        return CS2VM2_Op_BitCount(vm, frame, operand);
    case CS2_OP_ABS:
        return CS2VM2_Op_Abs(vm);
    case CS2_OP_TOGGLEBIT:
        return CS2VM2_Op_ToggleBit(vm, frame, operand);
    case CS2_OP_SETBIT_RANGE:
        return CS2VM2_Op_SetBitRange(vm, frame, operand);
    case CS2_OP_CLEARBIT_RANGE:
        return CS2VM2_Op_ClearBitRange(vm, frame, operand);
    case CS2_OP_GETBIT_RANGE:
        return CS2VM2_Op_GetBitRange(vm, frame, operand);
    case CS2_OP_SETBIT_RANGE_VALUE:
        return CS2VM2_Op_SetBitRangeValue(vm, frame, operand);
    case CS2_OP_INVPOW:
        return CS2VM2_Op_InvPow(vm, frame, operand);
    case CS2_OP_RANDOM:
        return CS2VM2_Op_Random(vm, frame, operand);
    case CS2_OP_RANDOMINC:
        return CS2VM2_Op_RandomInc(vm, frame, operand);
    case CS2_OP_INTERPOLATE:
        return CS2VM2_Op_Interpolate(vm, frame, operand);
    case CS2_OP_COMPARE:
        return CS2VM2_Op_Compare(vm, frame, operand);
    case CS2_OP_SUBSTRING:
        return CS2VM2_Op_Substring(vm, frame, operand);
    /* Pure string transforms. These previously fell through to StackMetaStub,
     * which discards the input and pushes "" — so they silently blanked whatever
     * text they touched instead of transforming it. */
    case CS2_OP_ESCAPE:
        return CS2VM2_Op_Escape(vm);
    case CS2_OP_LOWERCASE:
        return CS2VM2_Op_Lowercase(vm);
    case CS2_OP_UPPERCASE:
        return CS2VM2_Op_Uppercase(vm);
    case CS2_OP_REMOVETAGS:
        return CS2VM2_Op_RemoveTags(vm);
    /* Character-class predicates: pop a char code, push a bool. */
    case CS2_OP_CHAR_ISPRINTABLE:
        return CS2VM2_Op_CharIsPrintable(vm);
    case CS2_OP_CHAR_ISALPHANUMERIC:
    case CS2_OP_CHAR_ISALPHA:
    case CS2_OP_CHAR_ISNUMERIC:
        return CS2VM2_Op_CharClass(vm, opcode);
    case CS2_OP_STRING_INDEXOF_STRING:
        return CS2VM2_Op_StringIndexOfString(vm, frame, operand);
    case CS2_OP_STRING_INDEXOF_CHAR:
        return CS2VM2_Op_StringIndexOfChar(vm, frame, operand);
    case CS2_OP_OC_COST:
        return CS2VM2_Op_OC_IntParam(vm, frame, operand, CS2VM_OC_INT_COST);
    case CS2_OP_OC_STACKABLE:
        return CS2VM2_Op_OC_IntParam(vm, frame, operand, CS2VM_OC_INT_STACKABLE);
    case CS2_OP_OC_MEMBERS:
        return CS2VM2_Op_OC_IntParam(vm, frame, operand, CS2VM_OC_INT_MEMBERS);
    case CS2_OP_OC_CERT:
    case CS2_OP_OC_UNCERT:
        return CS2VM2_Op_OC_IntParam(vm, frame, operand, CS2VM_OC_INT_ID);
    case CS2_OP_STRUCT_PARAM:
        return CS2VM2_Op_StructParam(vm, frame, operand);
    case CS2_OP_CC_GETPARAM:
        return CS2VM2_Op_CC_GetParam(vm, frame, operand);
    case CS2_OP_CLIENTCLOCK:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_CLIENTCLOCK;
        return vm->vm->host_exec(vm, &request);
    }
    /*
     * STAT (3305) is the *boosted* level, STAT_BASE (3306) the level the
     * experience buys, STAT_XP (3307) the experience itself. The skills tab
     * reads all three; before these had handlers they fell through to the
     * stack-meta stub, which popped the skill id and pushed 0 — so the tab
     * built correctly and drew "0/0" for every skill, which looks far more like
     * a missing packet than a missing opcode.
     */
    case CS2_OP_STAT:
    case CS2_OP_STAT_BASE:
    case CS2_OP_STAT_XP:
    {
        struct CS2VM_HostRequest request;
        int stat;

        if( CS2VM2_PopInt(vm, &stat) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        memset(&request, 0, sizeof(request));
        request.kind = opcode == CS2_OP_STAT        ? CS2VM_HOST_REQUEST_STAT
                       : opcode == CS2_OP_STAT_BASE ? CS2VM_HOST_REQUEST_STAT_BASE
                                                    : CS2VM_HOST_REQUEST_STAT_XP;
        request.u.stat.stat = stat;
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_MOUSE_GETX:
    case CS2_OP_MOUSE_GETY:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = opcode == CS2_OP_MOUSE_GETX ? CS2VM_HOST_REQUEST_MOUSE_GETX
                                                   : CS2VM_HOST_REQUEST_MOUSE_GETY;
        return vm->vm->host_exec(vm, &request);
    }
    /*
     * Friends / ignore / private chat. Every row the friends panel (429) and
     * the ignore panel (432) draw is cc_created by the client's own scripts
     * 125 and 129 off these accessors — the server cannot if_settext a single
     * row — so with no handlers the panels drew their "you have no friends"
     * empty state however full the store was.
     */
    case CS2_OP_FRIEND_COUNT:
    case CS2_OP_FRIEND_GETNAME:
    case CS2_OP_FRIEND_GETWORLD:
    case CS2_OP_FRIEND_GETRANK:
    case CS2_OP_FRIEND_ADD:
    case CS2_OP_FRIEND_DEL:
    case CS2_OP_FRIEND_TEST:
    case CS2_OP_IGNORE_ADD:
    case CS2_OP_IGNORE_DEL:
    case CS2_OP_IGNORE_COUNT:
    case CS2_OP_IGNORE_GETNAME:
    case CS2_OP_IGNORE_TEST:
        return CS2VM2_Op_Social(vm, opcode);
    case CS2_OP_CHAT_GETFILTER_PUBLIC:
    case CS2_OP_CHAT_GETFILTER_PRIVATE:
    case CS2_OP_CHAT_GETFILTER_TRADE:
    case CS2_OP_CHAT_SETFILTER:
    case CS2_OP_CHAT_SENDPRIVATE:
    case CS2_OP_CHAT_PLAYERNAME:
    case CS2_OP_DOCHEAT:
        return CS2VM2_Op_Chat(vm, opcode);
    case CS2_OP_MAP_WORLD:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_MAP_WORLD;
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_CAM_SETFOLLOWHEIGHT:
    {
        struct CS2VM_HostRequest request;
        int height;
        if( CS2VM2_PopInt(vm, &height) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_CAM_SETFOLLOWHEIGHT;
        request.u.cam_set_follow_height.height = height;
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_CAM_GETFOLLOWHEIGHT:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_CAM_GETFOLLOWHEIGHT;
        return vm->vm->host_exec(vm, &request);
    }
    /* Numeric id 6232 is not CAM_GETYAW in rev 239: its cache sites consume
     * one mode int and return nothing. It reaches StackMetaStub below. */
    /* CAM_FORCEANGLE / CAM_GETANGLE_XA / CAM_GETANGLE_YA (5504..5506): the
     * orbit camera's pitch and yaw, in the units the scripts use — pitch
     * 128..383, yaw 0..2047, matching the reference's orbitCameraPitch /
     * orbitCameraYaw. FORCEANGLE pops (x, y) in push order. These have a
     * (0,0,0,0) entry in the generated table and used to reach StackMetaStub,
     * which now asserts; the RS2 overlay's stub entries for the same ids are
     * shadowed by this dispatch, and the two eras agree on the signature. */
    case CS2_OP_CAM_FORCEANGLE:
    {
        struct CS2VM_HostRequest request;
        int angle_x, angle_y;
        if( CS2VM2_PopInt(vm, &angle_y) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PopInt(vm, &angle_x) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_CAM_FORCEANGLE;
        request.u.cam_force_angle.angle_x = angle_x;
        request.u.cam_force_angle.angle_y = angle_y;
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_CAM_GETANGLE_XA:
    case CS2_OP_CAM_GETANGLE_YA:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = opcode == CS2_OP_CAM_GETANGLE_XA ? CS2VM_HOST_REQUEST_CAM_GETANGLE_XA
                                                        : CS2VM_HOST_REQUEST_CAM_GETANGLE_YA;
        return vm->vm->host_exec(vm, &request);
    }
    /* CLIENTOP_* (6700..6709): enhanced client-side context-menu hooks. SET pops
     * (slot, scriptId) + string label; DEL pops slot. Host-stubbed for now. */
    case CS2_OP_CLIENTOP_NPC_SET:
    case CS2_OP_CLIENTOP_LOC_SET:
    case CS2_OP_CLIENTOP_OBJ_SET:
    case CS2_OP_CLIENTOP_PLAYER_SET:
    case CS2_OP_CLIENTOP_TILE_SET:
        return CS2VM2_Op_ClientOp(vm, opcode, true);
    case CS2_OP_CLIENTOP_NPC_DEL:
    case CS2_OP_CLIENTOP_LOC_DEL:
    case CS2_OP_CLIENTOP_OBJ_DEL:
    case CS2_OP_CLIENTOP_PLAYER_DEL:
    case CS2_OP_CLIENTOP_TILE_DEL:
        return CS2VM2_Op_ClientOp(vm, opcode, false);
    case CS2_OP_HIGHLIGHT_NPC_SETUP:
        return CS2VM2_Op_Highlight(vm, opcode, 5, false);
    case CS2_OP_HIGHLIGHT_NPC_ON:
    case CS2_OP_HIGHLIGHT_NPC_OFF:
        return CS2VM2_Op_Highlight(vm, opcode, 3, false);
    case CS2_OP_HIGHLIGHT_NPC_GET:
        return CS2VM2_Op_Highlight(vm, opcode, 3, true);
    case CS2_OP_HIGHLIGHT_NPC_CLEAR:
        return CS2VM2_Op_Highlight(vm, opcode, 1, false);
    /* HIGHLIGHT_LOC_* (7011..7014): highlight a scene loc keyed by
     * (locTypeId, coordPacked, slot, group) — four ints — so ON/OFF/GET pop 4;
     * CLEAR pops the group only. GET pushes "not highlighted" (0). */
    case CS2_OP_HIGHLIGHT_LOC_ON:
    case CS2_OP_HIGHLIGHT_LOC_OFF:
        return CS2VM2_Op_Highlight(vm, opcode, 4, false);
    case CS2_OP_HIGHLIGHT_LOC_GET:
        return CS2VM2_Op_Highlight(vm, opcode, 4, true);
    case CS2_OP_HIGHLIGHT_LOC_CLEAR:
        return CS2VM2_Op_Highlight(vm, opcode, 1, false);
    /* HIGHLIGHT_OBJ_* (7021..7025): ground-item highlight, same (typeId, coord,
     * slot, group) key as LOC — ON/OFF/GET pop 4, GET pushes a bool; 7025 is the
     * OBJTYPE setup (pop 5). */
    case CS2_OP_HIGHLIGHT_OBJ_ON:
    case CS2_OP_HIGHLIGHT_OBJ_OFF:
        return CS2VM2_Op_Highlight(vm, opcode, 4, false);
    case CS2_OP_HIGHLIGHT_OBJ_GET:
        return CS2VM2_Op_Highlight(vm, opcode, 4, true);
    case CS2_OP_HIGHLIGHT_OBJTYPE_SETUP:
        return CS2VM2_Op_Highlight(vm, opcode, 5, false);
    /* MINIMENU_* (7100..7110): no-arg mouseover / right-click-menu getters; the
     * host answers each with the current hover/menu state. */
    case CS2_OP_MINIMENU_TYPE:
    case CS2_OP_MINIMENU_ENTRY:
    case CS2_OP_MINIMENU_FINDNPC:
    case CS2_OP_MINIMENU_FINDLOC:
    case CS2_OP_MINIMENU_FINDOBJ:
    case CS2_OP_MINIMENU_FINDPLAYER:
    case CS2_OP_MINIMENU_ISOPEN:
    case CS2_OP_MINIMENU_FINDCOMPONENT:
    case CS2_OP_MINIMENU_NUMOPS:
        return CS2VM2_Op_Minimenu(vm, opcode);
    /* Audio volumes (3203..3208): direct setters take just a value, getters take
     * nothing; the host owns the value and pushes it back. */
    case CS2_OP_SETVOLUMEMUSIC:
    case CS2_OP_SETVOLUMESOUNDS:
    case CS2_OP_SETVOLUMEAREASOUNDS:
        return CS2VM2_Op_ClientOption(vm, opcode, false, true);
    case CS2_OP_GETVOLUMEMUSIC:
    case CS2_OP_GETVOLUMESOUNDS:
    case CS2_OP_GETVOLUMEAREASOUNDS:
        return CS2VM2_Op_ClientOption(vm, opcode, false, false);
    /* Hide-roofs (3111/3112). Same shape as the direct volume ops — no id, the
     * host owns the value — because they are the *named* form of game option 1
     * and write the same preference the reference's ::toggleroof cheat does.
     * Both had no case here at all, so the Display panel's Roofs toggle popped
     * its argument through the stack stub and changed nothing. */
    case CS2_OP_SETREMOVEROOFS:
        return CS2VM2_Op_ClientOption(vm, opcode, false, true);
    case CS2_OP_GETREMOVEROOFS:
        return CS2VM2_Op_ClientOption(vm, opcode, false, false);
    /* Client/game/device options (3209..3217): id-keyed. SET pops (id, value);
     * GET/GETRANGE pop the id, the host pushes the result(s). */
    case CS2_OP_CLIENTOPTION_SET:
    case CS2_OP_GAMEOPTION_SET:
    case CS2_OP_DEVICEOPTION_SET:
        return CS2VM2_Op_ClientOption(vm, opcode, true, true);
    case CS2_OP_CLIENTOPTION_GET:
    case CS2_OP_GAMEOPTION_GET:
    case CS2_OP_DEVICEOPTION_GET:
    case CS2_OP_DEVICEOPTION_GETRANGE:
        return CS2VM2_Op_ClientOption(vm, opcode, true, false);
    /* Minimap zoom (7250..7254): setters pop one value, GETZOOM pushes the
     * host-owned zoom. */
    case CS2_OP_MINIMAP_SETZOOMABLE:
    case CS2_OP_MINIMAP_SETZOOM:
    case CS2_OP_MINIMAP_SETICONZOOMLIMIT:
        return CS2VM2_Op_Minimap(vm, opcode, true);
    case CS2_OP_MINIMAP_GETZOOM:
        return CS2VM2_Op_Minimap(vm, opcode, false);
    /* Local notifications (3170..3173): stubbed by the host on desktop. */
    case CS2_OP_LOCAL_NOTIFICATION:
    case CS2_OP_LOCAL_NOTIFICATION_CANCEL:
    case CS2_OP_LOCAL_NOTIFICATION_CANCELALL:
    case CS2_OP_LOCAL_NOTIFICATION_SUPPORTED:
        return CS2VM2_Op_LocalNotification(vm, opcode);
    /* Mobile device queries — we are a desktop client: full battery, on mains,
     * unmetered connection. Pushing nothing here underflows the next opcode. */
    case CS2_OP_MOBILE_BATTERYLEVEL:
        return CS2VM2_PushInt(vm, 100);
    case CS2_OP_MOBILE_BATTERYCHARGING:
        return CS2VM2_PushInt(vm, 1);
    case CS2_OP_MOBILE_WIFIAVAILABLE:
        return CS2VM2_PushInt(vm, 1);
    case CS2_OP_MOBILE_KEYBOARDHIDE:
        /* No soft keyboard to hide. */
        return CS2VM_EXECNO_OK;
    /*
     * SOUND_SYNTH / SOUND_SONG / SOUND_JINGLE are implemented above, next to
     * the other host requests. The stubs that used to sit here discarded their
     * arguments -- and SOUND_SONG discarded *one* when its arity is five,
     * leaving four ints on the operand stack for whatever ran next.
     */
    case CS2_OP_IF_CLOSE:
    {
        /*
         * No stack args. Generated meta wrongly treats this like MES (1 string).
         *
         * This is what every interface's close button runs — `steelborder`
         * binds op 1 to clientscript 29, whose entire body is `if_close`. It is
         * *not* a local close: the reference sends CLOSE_MODAL and waits for the
         * server to unmount. Returning OK here without telling the host made the
         * X on the bank, the world map and every other framed interface a
         * no-op that consumed the click.
         */
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_IF_CLOSE;
        return vm->vm->host_exec(vm, &request);
    }
    case CS2_OP_RESUME_COUNTDIALOG:
    {
        /*
         * resume_countdialog(text): answer a server script parked on
         * P_COUNTDIALOG. One string in, nothing out.
         *
         * The argument is a string because that is what the callers push —
         * the bank PIN keypad assembles its four digits into an int and then
         * `resume_countdialog(tostring($int2))`. The wire packet carries an
         * int; the host converts, the same way app.c's chatbox "Enter amount"
         * path does.
         *
         * Same host split as IF_CLOSE above: the VM knows nothing about the
         * socket. `text` is borrowed from the string pool — the handler
         * copies it (CS2VM2_PopStr does not transfer ownership).
         */
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG;
        if( CS2VM2_PopStr(vm, &request.u.resume_countdialog.text) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return vm->vm->host_exec(vm, &request);
    }
    /* Loot-tracker native store (7400-family + 7600-family). */
    case CS2_OP_LOOT_SOURCE_COUNT:
    case CS2_OP_LOOT_SOURCE_NAME:
    case CS2_OP_LOOT_SOURCE_ITEMCOUNT:
    case CS2_OP_LOOT_SOURCE_TOTALVAL:
    case CS2_OP_LOOT_BEGIN_QUERY:
    case CS2_OP_LOOT_QUERY_ID:
    case CS2_OP_LOOT_AUX_COUNT_TOTAL:
    case CS2_OP_LOOT_ROW_COUNT_BYNAME:
    case CS2_OP_LOOT_ROW_COUNT_BYID:
    case CS2_OP_LOOT_ROW_BYNAME:
    case CS2_OP_LOOT_ROW_BYID:
    case CS2_OP_LOOT_CLEAR_ALL:
    case CS2_OP_LOOT_CLEAR_SOURCE:
    case CS2_OP_LOOT_REMOVE_BYID:
    case CS2_OP_LOOT_IGNORE_ADD:
    case CS2_OP_LOOT_IGNORE_REMOVE:
    case CS2_OP_LOOT_GROUND_COUNT:
    case CS2_OP_LOOT_GROUND_NAME:
    case CS2_OP_LOOT_IGNORE_CLEAR:
    case CS2_OP_LOOT_SOURCE_IGNORE_ADD:
    case CS2_OP_LOOT_SOURCE_IGNORE_REMOVE:
    case CS2_OP_LOOT_SRCLIST_COUNT:
    case CS2_OP_LOOT_SRCLIST_NAME:
    case CS2_OP_LOOT_SOURCE_NAME2:
    case CS2_OP_LOOT_AUX_UPSERT2:
    case CS2_OP_LOOT_AUX_UPSERT:
    case CS2_OP_LOOT_AUX_REMOVE:
    case CS2_OP_LOOT_AUX_GET:
    case CS2_OP_LOOT_AUX_COUNT:
    case CS2_OP_LOOT_AUX_LOOKUP:
    case CS2_OP_LOOT_AUX_CLEAR:
    case CS2_OP_LOOT_ADD:
        return CS2VM2_Op_Loot(vm, opcode);
    /* Hiscores stubs (7809/7811). */
    case CS2_OP_HISCORES_STATUS:
    case CS2_OP_HISCORES_ERROR:
        return CS2VM2_Op_Hiscores(vm, opcode);
    default:
        /* Contiguous families, matched by range rather than forty case labels. */
        if( (opcode >= CS2_OP_WORLDMAP_INIT && opcode <= CS2_OP_WORLDMAP_LISTELEMENT_NEXT) ||
            (opcode >= CS2_OP_MEC_TEXT && opcode <= CS2_OP_WORLDMAP_ELEMENTCOORD) )
            return CS2VM2_Op_WorldMapFamily(vm, opcode);
        return CS2VM2_Op_StackMetaStub(vm, frame, opcode, operand);
    }
}

/* Fills *int_args / *str_args with the stack values this opcode pops.
 * Returns 0 for a fixed count, 1 when the count is variable (e.g. GOSUB). */
static int
CS2VM2_OpArgCounts(
    int opcode,
    int operand,
    int* int_args,
    int* str_args)
{
    *int_args = 0;
    *str_args = 0;
    switch( opcode )
    {
    case CS2_OP_SUB:
    case CS2_OP_MULTIPLY:
    case CS2_OP_DIV:
    case CS2_OP_MOD:
    case CS2_OP_POW:
        *int_args = 2;
        return 0;
    case CS2_OP_SCALE:
        *int_args = 3;
        return 0;
    case CS2_OP_CC_CREATE:
        *int_args = 4;
        return 0;
    case CS2_OP_CC_COPY:
        *int_args = 3;
        return 0;
    case CS2_OP_CC_FIND:
        *int_args = 2;
        return 0;
    case CS2_OP_CC_CREATECHILD:
    case CS2_OP_CC_CREATESIBLING:
        *int_args = 2;
        return 0;
    case CS2_OP_CC_CHILDREN_FIND:
        *int_args = 1;
        return 0;
    case CS2_OP_IF_CHILDREN_FIND:
        *int_args = 2;
        return 0;
    case CS2_OP_POP_INT_LOCAL:
        *int_args = 1;
        return 0;
    case CS2_OP_POP_INT_DISCARD:
        *int_args = operand > 0 ? operand : 0;
        return 0;
    case CS2_OP_POP_STRING_LOCAL:
        *str_args = 1;
        return 0;
    case CS2_OP_POP_STRING_DISCARD:
        *str_args = operand > 0 ? operand : 0;
        return 0;
    case CS2_OP_JOIN_STRING:
        *str_args = operand > 0 ? operand : 0;
        return 0;
    case CS2_OP_STRING_LENGTH:
        *str_args = 1;
        return 0;
    case CS2_OP_GOSUB_WITH_PARAMS:
        return 1;
    case CS2_OP_SWITCH:
        *int_args = 1;
        return 0;
    case CS2_OP_OC_PARAM:
        *int_args = 2;
        return 0;
    case CS2_OP_OC_NAME:
        *int_args = 1;
        return 0;
    case CS2_OP_NC_NAME:
        *int_args = 1;
        return 0;
    case CS2_OP_OC_UNPLACEHOLDER:
        *int_args = 1;
        return 0;
    case CS2_OP_IF_SETOPSUBMENU:
        *int_args = 3;
        *str_args = 1;
        return 0;
    case CS2_OP_IF_SETOPBASE:
        *int_args = 1;
        *str_args = 1;
        return 0;
    case CS2_OP_CC_SETOPBASE:
        *str_args = 1;
        return 0;
    case CS2_OP_IF_SETOUTLINE:
        *int_args = 2;
        return 0;
    case CS2_OP_IF_SETTARGETPRIORITY:
        *int_args = 2;
        return 0;
    default:
        return 0;
    }
}

static void
CS2VM2_DebugPrintOpCode(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int opcode,
    int operand,
    char const* str_operand)
{
#if CS2VM2_DEBUG_OPS
    printf("pc=%d %s (op %d)", frame->pc, CS2_OpCode_String(opcode), opcode);

    switch( cs2_opcode_operand_kind(opcode) )
    {
    case CS2_OPERAND_STRING:
        printf(" operand.str=\"%s\"", str_operand ? str_operand : "(null)");
        break;
    case CS2_OPERAND_INT8:
    case CS2_OPERAND_INT32:
        printf(" operand.int=%d", operand);
        break;
    case CS2_OPERAND_NONE:
    default:
        break;
    }
    printf("\n");

    if( opcode == CS2_OP_PUSH_INT_LOCAL || opcode == CS2_OP_POP_INT_LOCAL )
        printf("    int_local[%d] = %d\n", operand, frame->int_locals[operand]);

    if( opcode == CS2_OP_PUSH_STRING_LOCAL || opcode == CS2_OP_POP_STRING_LOCAL )
        printf(
            "    str_local[%d] = \"%s\"\n",
            operand,
            frame->str_locals[operand] ? frame->str_locals[operand] : "(null)");

    int int_args = 0;
    int str_args = 0;
    if( CS2VM2_OpArgCounts(opcode, operand, &int_args, &str_args) != 0 )
    {
        printf("    args: variable (callee signature)\n");
        return;
    }

    for( int i = 0; i < int_args; i++ )
    {
        int depth = vm->ints_stack_top - 1 - i;
        if( depth >= 0 )
            printf("    int arg[%d] (top-%d) = %d\n", i, i, vm->ints_stack[depth]);
        else
            printf("    int arg[%d] = <stack underflow>\n", i);
    }
    for( int i = 0; i < str_args; i++ )
    {
        int depth = vm->strs_stack_top - 1 - i;
        if( depth >= 0 )
        {
            printf(
                "    str arg[%d] (top-%d) = \"%s\"\n",
                i,
                i,
                vm->strs_stack[depth] ? vm->strs_stack[depth] : "(null)");
        }
        else
            printf("    str arg[%d] = <stack underflow>\n", i);
    }
#endif
}

int
CS2VM2_PushCallScript(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Script* script)
{
    assert(vm);
    assert(vm->frame_sp < CS2VM_MAX_FRAMES);
    assert(script);

    struct CS2VM2_Frame* frame = vm->frame_sp < vm->frames_live
                                     ? vm->frames[vm->frame_sp]
                                     : cs2vm2_thread_frame_grow(vm, vm->frame_sp);
    if( !frame )
        return CS2VM_EXECNO_ERROR;

    memset(frame, 0, sizeof(*frame));
    frame->script = script;
    vm->frame_sp += 1;
    return CS2VM_EXECNO_OK;
}

// Called for onLoad, onOp, onClick, onVarTransmit
// Format: [scriptId, ...args]

// [0] = script ID (not a script local)
// [1..] = int/string args → $int0, $int1, … and $obj0, …
// Many onLoad listeners are just [scriptId] (count=1) → no args, param locals stay 0.
int
CS2VM2_SetActiveAndDotComponentId(
    struct CS2VM2_Thread* vm,
    int component_id)
{
    assert(vm);
    vm->active_component_id = component_id;
    vm->dot_component_id = component_id;
    return CS2VM_EXECNO_OK;
}

/*
 * TORIRS_CS2_PROFILE=1: wall time and opcode count per *entry* script.
 *
 * The frame-stage timers (TORIRS_PERF) say "the cs2 stage spiked to 22 ms";
 * they cannot say which script did it, and a stage that runs a hundred cache
 * scripts per tick needs that answer to be useful. Gosubs are inside one
 * RunScript call, so the cost lands on the script that was entered — which is
 * the unit a caller can actually do something about. Resumes after a yield
 * accumulate into the same row.
 */
#define CS2_PROFILE_ROWS 512

struct cs2_profile_row
{
    int script_id;
    uint64_t ns;
    uint32_t calls;
};

static struct cs2_profile_row g_cs2_profile[CS2_PROFILE_ROWS];
static int g_cs2_profile_rows;
static int g_cs2_profile_on = -1;

static uint64_t
cs2_profile_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void
cs2_profile_report(void)
{
    fprintf(stderr, "=== cs2 script profile (top 20 by total wall time) ===\n");
    for( int rank = 0; rank < 20 && rank < g_cs2_profile_rows; rank++ )
    {
        int best = -1;
        for( int i = 0; i < g_cs2_profile_rows; i++ )
        {
            if( g_cs2_profile[i].calls == 0 )
                continue;
            if( best < 0 || g_cs2_profile[i].ns > g_cs2_profile[best].ns )
                best = i;
        }
        if( best < 0 )
            break;
        fprintf(
            stderr, "  script %-6d %8.3f ms total  %6u calls  %8.3f us/call\n",
            g_cs2_profile[best].script_id, (double)g_cs2_profile[best].ns / 1e6,
            g_cs2_profile[best].calls,
            (double)g_cs2_profile[best].ns / 1e3 / (double)g_cs2_profile[best].calls);
        g_cs2_profile[best].calls = 0; /* consumed: this pass is a selection sort */
    }
    fprintf(stderr, "=== end cs2 script profile ===\n");
}

static struct cs2_profile_row*
cs2_profile_row_for(int script_id)
{
    for( int i = 0; i < g_cs2_profile_rows; i++ )
    {
        if( g_cs2_profile[i].script_id == script_id )
            return &g_cs2_profile[i];
    }
    if( g_cs2_profile_rows >= CS2_PROFILE_ROWS )
        return NULL;
    g_cs2_profile[g_cs2_profile_rows].script_id = script_id;
    return &g_cs2_profile[g_cs2_profile_rows++];
}

static int
cs2vm2_run_script_body(struct CS2VM2_Thread* vm)
{
    assert(vm);
    assert(vm->frame_sp > 0);

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_SCRIPTS, 1);

    int result;
    int cycles = 0;
    while( cycles++ < CS2VM_MAX_CYCLES )
    {
        if( vm->frame_sp <= 0 )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_OPCODES, cycles - 1);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_CYCLES, cycles - 1);
            return CS2VM_EXECNO_DONE;
        }

        struct CS2VM2_Frame* frame = CS2VM_FRAME(vm);
        if( frame->pc >= frame->script->op_count )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_OPCODES, cycles - 1);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_CYCLES, cycles - 1);
            return CS2VM_EXECNO_DONE;
        }

        int opcode = frame->script->opcodes[frame->pc];
        int operand = frame->script->int_operands[frame->pc];
        char const* str_operand_str = NULL;
        if( frame->script->string_operands )
            str_operand_str = frame->script->string_operands[frame->pc];

        CS2VM2_DebugPrintOpCode(vm, frame, opcode, operand, str_operand_str);

        int op_pc = frame->pc;
        frame->pc += 1;

        /* The undo log only tracks the in-flight op: reset it here so a yield
         * rolls back exactly this op's tracked mutations, and committed prior
         * mutations are never touched. */
        vm->undo_log_len = 0;

        struct CS2VM2_YieldCheckpoint yield_cp;
        CS2VM2_SaveYieldCheckpoint(vm, &yield_cp);

        result = CS2VM2_RunOp(vm, frame, opcode, operand, str_operand_str);

        CS2VM2_TraceOpcode(vm, frame, op_pc, opcode, operand, result);

        switch( result )
        {
        case CS2VM_EXECNO_OK:
            if( vm->yield_halt_frame_sp == vm->frame_sp &&
                vm->yield_halt_script_id == frame->script->script_id && vm->yield_halt_pc == op_pc )
                CS2VM2_ClearYieldHalt(vm);
            break;
        case CS2VM_EXECNO_YIELD:
            if( !CS2VM2_CheckYieldHalt(vm, frame, op_pc, opcode) )
            {
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_OPCODES, cycles);
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_CYCLES, cycles);
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_ABORTS, 1);
                return CS2VM_EXECNO_ERROR;
            }
            CS2VM2_RestoreYieldCheckpoint(vm, &yield_cp, op_pc);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_OPCODES, cycles);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_CYCLES, cycles);
            return CS2VM_EXECNO_YIELD;
        default:
            if( result == CS2VM_EXECNO_ERROR )
            {
                vm->last_error_opcode = opcode;
                vm->last_error_pc = op_pc;
                vm->last_error_script_id = frame->script->script_id;
            }
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_OPCODES, cycles);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_CYCLES, cycles);
            if( result == CS2VM_EXECNO_ERROR )
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_ABORTS, 1);
            return result;
        }
    }

    {
        struct CS2VM2_Frame* frame = CS2VM_FRAME(vm);
        vm->last_error_opcode = -1;
        vm->last_error_pc = frame->pc;
        vm->last_error_script_id = frame->script->script_id;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_OPCODES, cycles);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_CYCLES, cycles);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_ABORTS, 1);
    return CS2VM_EXECNO_ERROR;
}

int
CS2VM2_RunScript(struct CS2VM2_Thread* vm)
{
    struct cs2_profile_row* row;
    uint64_t begin_ns;
    int result;

    assert(vm);
    assert(vm->frame_sp > 0);

    if( g_cs2_profile_on < 0 )
    {
        g_cs2_profile_on = getenv("TORIRS_CS2_PROFILE") ? 1 : 0;
        if( g_cs2_profile_on )
            atexit(cs2_profile_report);
    }
    if( !g_cs2_profile_on )
        return cs2vm2_run_script_body(vm);

    row = cs2_profile_row_for(CS2VM_FRAME(vm)->script->script_id);
    begin_ns = cs2_profile_now_ns();
    result = cs2vm2_run_script_body(vm);
    if( row )
    {
        row->ns += cs2_profile_now_ns() - begin_ns;
        row->calls++;
    }
    return result;
}

static int
CS2VM2_Op_CC_CreateUnderParent(
    struct CS2VM2_Thread* vm,
    int parent_id,
    int type,
    int child_index,
    int operand)
{
    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    request.u.cc_create.parent_id = parent_id;
    request.u.cc_create.component_type = type;
    request.u.cc_create.child_index = child_index;
    request.u.cc_create.is_nested = 0;
    request.u.cc_create.dot_operand = operand;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_FindRoot(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_FINDROOT;
    request.u.cc_findroot.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_findroot.dot_operand = operand;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_ChildrenFind(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int start_index;
    if( CS2VM2_PopInt(vm, &start_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_CHILDREN_FIND;
    request.u.cc_children_find.parent_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_children_find.start_index = start_index;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_ChildrenFindNextId(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( vm->children_iter_index < vm->children_iter_count )
        return CS2VM2_PushInt(vm, vm->children_iter_indices[vm->children_iter_index++]);
    return CS2VM2_PushInt(vm, -1);
}

/*
 * Opcode 213 — CC_CHILDREN_FINDNEXT() -> bool.
 * After 203/212 filled children_iter_*, advance to the next child, set it as
 * the active/dot target (same as cc_find), and push 1. Exhausted -> push 0.
 * Script 9179 compares against 1 then runs .cc_setop/.cc_setonop on that child
 * — aliasing this to FINDNEXTID (204) broke Overview/Quest XP tab clicks.
 */
int
CS2VM2_Op_CC_ChildrenFindNext(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    if( vm->children_iter_index >= vm->children_iter_count ||
        vm->children_iter_parent < 0 )
        return CS2VM2_PushInt(vm, 0);

    int sub_id = vm->children_iter_indices[vm->children_iter_index++];

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_FIND;
    request.u.cc_find.parent_id = vm->children_iter_parent;
    request.u.cc_find.sub_id = sub_id;
    request.u.cc_find.dot_operand = operand;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_ChildrenFind(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int start_index, uid;
    if( CS2VM2_PopInt(vm, &start_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CHILDREN_FIND;
    request.u.if_children_find.uid = uid;
    request.u.if_children_find.start_index = start_index;
    request.u.if_children_find.dot_operand = operand;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_ChildrenFindNextId(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    return CS2VM2_Op_CC_ChildrenFindNextId(vm, frame, operand);
}

int
CS2VM2_Op_CC_CreateChild(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int child_index, type;
    if( CS2VM2_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int parent_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    if( parent_id < 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_Op_CC_CreateUnderParent(vm, parent_id, type, child_index, operand);
}

int
CS2VM2_Op_CC_CreateSibling(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int child_index, type;
    if( CS2VM2_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int current_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    if( current_id < 0 )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT;
    request.u.cc_resolve_parent.component_id = current_id;

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    int parent_id;
    if( CS2VM2_PopInt(vm, &parent_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( parent_id < 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VM2_Op_CC_CreateUnderParent(vm, parent_id, type, child_index, operand);
}

int
CS2VM2_Op_StructParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int param_id;
    int struct_id;
    if( CS2VM2_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &struct_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_STRUCT_PARAM;
    request.u.struct_param.struct_id = struct_id;
    request.u.struct_param.param_id = param_id;

    return vm->vm->host_exec(vm, &request);
}

/*
 * CC_GETPARAM (RS2 wire 1613): read a param off the active widget.
 *
 * Type-polymorphic — the ParamType decides whether an int or a string comes
 * back, which is why it cannot be a StackMetaStub entry. A rev-634 widget only
 * carries its own param table when the file's leading version byte is >= 0, and
 * no file in a 634-era cache is (they all lead with 255 = -1), so the answer is
 * always the ParamType's default. STRUCT_PARAM with struct -1 already means
 * exactly that — "no record, fall through to the param default" — so this
 * forwards to it rather than duplicating the type dispatch.
 */
int
CS2VM2_Op_CC_GetParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int param_id;
    if( CS2VM2_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_STRUCT_PARAM;
    request.u.struct_param.struct_id = -1;
    request.u.struct_param.param_id = param_id;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_GetText(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETTEXT;
    request.u.cc_gettext.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_GetTrans(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETTRANS;
    request.u.cc_gettrans.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);

    return vm->vm->host_exec(vm, &request);
}

/*
 * CC_GETCOMPONENTPARAM (1703) / CC_SETCOMPONENTPARAM (1704): the component's own
 * runtime param table.
 *
 * Not CC_GETPARAM (1613), which is the RS2-era read of a *file* param table.
 * Nothing but 1704 ever writes this one: every IF3 component in an OldSchool
 * cache consumes its bytes exactly, with no param section at the end, so the
 * table a component is born with is empty. The gameframe scripts build a widget
 * with cc_create, tag it (param 2365 = "what kind of row is this", 2370 = its
 * index, ...), and later cc_find it and read the tag back to decide what to do.
 */
int
CS2VM2_Op_CC_GetComponentParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int param_id;
    if( CS2VM2_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM;
    request.u.cc_component_param.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_component_param.param_id = param_id;

    return vm->vm->host_exec(vm, &request);
}

/*
 * IF_GETCOMPONENTPARAM (2703) — the same table, for a component named by
 * argument rather than the active one.
 *
 * Three ints in, one out: `(param, component, fallback)`, fallback on top. The
 * arity is read rather than inferred — script 8304's entire body is
 * `push 2356; push $com; push -1; 2703; return`, script 9181 feeds the result
 * straight into a three-argument `if_setscrollsize`, and script 9182 compares
 * it against 4. All 16 call sites in cache.osrs239 push three and consume one.
 *
 * The third argument is the literal -1 at every one of those sites, so
 * "fallback for a miss" and "sub-id, with -1 meaning the component itself"
 * cannot be told apart from this cache. It is read as the fallback because
 * every read site guards the result against -1, and a table that starts empty
 * — which an OldSchool IF3 component's does, see above — misses far more often
 * than it hits.
 */
int
CS2VM2_Op_IF_GetComponentParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int param_id;
    int component_id;
    int fallback;

    if( CS2VM2_PopInt(vm, &fallback) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK ||
        CS2VM2_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETCOMPONENTPARAM;
    request.u.cc_component_param.component_id = component_id;
    request.u.cc_component_param.param_id = param_id;
    request.u.cc_component_param.value = fallback;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_CC_SetComponentParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int param_id;
    int value = 0;
    int kind;
    char* str_value = NULL;

    /* `kind` says which stack the value came in on, so it has to be popped
     * first: 2 is a string param and the value is on the string stack, anything
     * else is an int. Popping three ints unconditionally — which is what the
     * arity solver's flat signature says — steals an unrelated int from under a
     * string write and leaves the string behind. */
    if( CS2VM2_PopInt(vm, &kind) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( kind == CS2_CC_COMPONENTPARAM_KIND_STRING )
    {
        if( CS2VM2_PopStr(vm, &str_value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    else if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM;
    request.u.cc_component_param.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.cc_component_param.param_id = param_id;
    request.u.cc_component_param.value = value;
    request.u.cc_component_param.str_value = str_value;
    request.u.cc_component_param.kind = kind;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_Find(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_FIND;
    request.u.if_find.component_id = component_id;
    request.u.if_find.dot_operand = operand;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_GetX(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETX;
    request.u.if_getx.component_id = component_id;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_GetText(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETTEXT;
    request.u.if_gettext.component_id = component_id;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_IF_GetScrollWidth(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VM2_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH;
    request.u.if_getscrollwidth.component_id = component_id;

    return vm->vm->host_exec(vm, &request);
}

int
CS2VM2_Op_OC_IntParam(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand,
    enum CS2VM_OC_IntField field)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_INT_PARAM;
    request.u.oc_int_param.item_id = item_id;
    request.u.oc_int_param.field = field;

    return vm->vm->host_exec(vm, &request);
}

void
CS2VM2_ResetRuntime(struct CS2VM2_Thread* vm)
{
    assert(vm);
    vm->ints_stack_top = 0;
    vm->strs_stack_top = 0;
    vm->frame_sp = 0;
    /* Dropping the stacks and frames drops the last references to every string
     * the finished script made, so this is where the pool is reclaimed. Callers
     * must therefore reset only between scripts, never mid-run. */
    CS2VM2_StrPool_Reset(&vm->str_pool);
    CS2VM2_ClearYieldHalt(vm);
    vm->has_awaited = false;
    vm->children_collect_handle = NULL;
}

/*
 * Reset one thread without touching its bulk arrays.
 *
 * A blanket memset of the whole VM on every script invocation was one of the
 * largest
 * single costs in the frame (it showed up as __bzero), and none of that zeroing
 * is load-bearing:
 *
 *   - frames[]  — the table is only read below frames_live, which is set to 0
 *                 here, so the slots themselves need no clearing; whoever grows
 *                 the stack writes the slot, and whoever pushes the frame
 *                 memsets its contents.
 *   - stacks    — ints_stack/strs_stack are only read below their _top.
 *   - undo_log, children_iter_indices — only read below their counters.
 *   - arrays[]  — every read is guarded by .defined && index < .size, and
 *                 defining an array initialises its own cells.
 *   - str_pool  — zeroed by CS2VM2_StrPool_Init, which never reads the old
 *                 state (the memory here may be uninitialised malloc'd bytes).
 *
 * So only the scalars and the per-array .defined/.size flags need clearing.
 * Values below match exactly what the old memset produced (note yield_halt_pc
 * is 0 here, not the -1 that CS2VM2_ClearYieldHalt uses).
 *
 * Like CS2VM2_StrPool_Init, this drops rather than frees what the thread held:
 * it is for a fresh block or one that CS2VM2_Free has already emptied.
 */
static void
cs2vm2_thread_init(
    struct CS2VM2_Thread* thread,
    struct CS2VM2* vm)
{
    thread->vm = vm;

    thread->frames_live = 0;

    thread->ints_stack_top = 0;
    thread->strs_stack_top = 0;
    thread->frame_sp = 0;

    thread->active_component_id = 0;
    thread->dot_component_id = 0;

    thread->last_error_opcode = 0;
    thread->last_error_pc = 0;
    thread->last_error_script_id = 0;

    thread->yield_halt_frame_sp = 0;
    thread->yield_halt_script_id = 0;
    thread->yield_halt_pc = 0;
    thread->yield_halt_count = 0;

    thread->has_awaited = false;
    thread->awaited_kind = 0;
    thread->awaited_id = 0;
    thread->awaited_id2 = 0;

    thread->undo_log_len = 0;

    thread->children_iter_count = 0;
    thread->children_iter_index = 0;
    thread->children_iter_parent = -1;
    thread->children_collect_handle = NULL;

    /* Cell blocks are dropped here, not freed: like str_pool, this is for a
     * fresh block or one CS2VM2_Free has already emptied (see the note above).
     * A pooled VM comes back through Release -> CS2VM2_Free, which frees them,
     * so dropping the pointers here cannot leak. */
    for( int i = 0; i < CS2VM2_MAX_ARRAYS; i++ )
    {
        thread->arrays[i].cells.strings = NULL;
        thread->arrays[i].capacity = 0;
        thread->arrays[i].defined = 0;
        thread->arrays[i].size = 0;
        thread->arrays[i].is_string = 0;
    }
    thread->array_alloc = 0;

    CS2VM2_StrPool_Init(&thread->str_pool);

    thread->canvas_w = 0;
    thread->canvas_h = 0;
    thread->window_mode = 0;
    thread->default_window_mode = 0;
}

void
CS2VM2_Init(struct CS2VM2* vm)
{
    assert(vm);
    vm->thread_count = CS2VM2_MAX_THREADS;
    vm->host_exec = NULL;
    vm->user = NULL;
    for( int i = 0; i < CS2VM2_MAX_THREADS; i++ )
        cs2vm2_thread_init(&vm->threads[i], vm);
}

void
CS2VM2_Free(struct CS2VM2* vm)
{
    assert(vm);
    for( int i = 0; i < CS2VM2_MAX_THREADS; i++ )
    {
        CS2VM2_ResetRuntime(&vm->threads[i]);
        /* ResetRuntime keeps a block for reuse; nothing will reuse it now. */
        CS2VM2_StrPool_Free(&vm->threads[i].str_pool);
        for( int a = 0; a < CS2VM2_MAX_ARRAYS; a++ )
        {
            free(vm->threads[i].arrays[a].cells.strings);
            vm->threads[i].arrays[a].cells.strings = NULL;
            vm->threads[i].arrays[a].capacity = 0;
        }
        cs2vm2_thread_frames_release(&vm->threads[i]);
    }
}

/* --- VM block pool ------------------------------------------------------- */
/* Scripts nest (a script awaits a load and another starts), so this is a small
 * free list rather than a singleton. The cap bounds what the pool retains when
 * a burst of nesting unwinds; past it, blocks go back to the allocator. */
#define CS2VM2_POOL_MAX 16

static struct CS2VM2* g_vm_pool[CS2VM2_POOL_MAX];
static int g_vm_pool_count;

struct CS2VM2*
CS2VM2_Acquire(void)
{
    struct CS2VM2* vm;
    struct timespec t0;
    struct timespec t1;
    int64_t init_ns;

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_VM_ACQUIRE, 1);

    if( g_vm_pool_count > 0 )
    {
        vm = g_vm_pool[--g_vm_pool_count];
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_VM_POOL_HIT, 1);
    }
    else
    {
        vm = (struct CS2VM2*)malloc(sizeof(*vm));
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_VM_POOL_MISS, 1);
    }
    if( !vm )
        return NULL;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    CS2VM2_Init(vm);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    init_ns = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000LL + (int64_t)(t1.tv_nsec - t0.tv_nsec);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_VM_INIT_NS, init_ns);
    return vm;
}

void
CS2VM2_Release(struct CS2VM2* vm)
{
    if( !vm )
        return;

    CS2VM2_Free(vm);
    if( g_vm_pool_count < CS2VM2_POOL_MAX )
        g_vm_pool[g_vm_pool_count++] = vm;
    else
        free(vm);
}

void
CS2VM2_PoolDrain(void)
{
    while( g_vm_pool_count > 0 )
        free(g_vm_pool[--g_vm_pool_count]);
    /* Parked VMs hold no frames (Release empties them), so the free list is all
     * that is left to give back. */
    while( g_frame_pool_count > 0 )
        free(g_frame_pool[--g_frame_pool_count]);
}

void
CS2VM2_Run(struct CS2VM2* vm)
{
    assert(vm);
    (void)CS2VM2_RunScript(&vm->threads[0]);
}

struct CS2VM2_Thread*
CS2VM2_ThreadMain(struct CS2VM2* vm)
{
    assert(vm);
    return &vm->threads[0];
}

void
CS2VM2_ThreadSetCanvas(
    struct CS2VM2_Thread* thread,
    int w,
    int h)
{
    assert(thread);
    thread->canvas_w = w;
    thread->canvas_h = h;
}

void
CS2VM2_ThreadSetWindowMode(
    struct CS2VM2_Thread* thread,
    int mode,
    int default_mode)
{
    assert(thread);
    thread->window_mode = mode;
    thread->default_window_mode = default_mode;
}

int
CS2VM2_ThreadStart(
    struct CS2VM2_Thread* thread,
    struct CS2VM2_Script* script)
{
    assert(thread);
    assert(script);
    CS2VM2_ResetRuntime(thread);
    return CS2VM2_PushCallScript(thread, script);
}

static enum CS2VM2_ThreadStatus
CS2VM2_ThreadStatusFromRc(
    int rc,
    struct CS2VM2_Thread* thread,
    struct CS2VM2_ThreadError* err_out)
{
    if( rc == CS2VM_EXECNO_DONE || rc == CS2VM_EXECNO_OK )
        return CS2VM2_THREAD_DONE;
    if( rc == CS2VM_EXECNO_YIELD )
        return CS2VM2_THREAD_YIELDED;
    if( err_out )
    {
        err_out->opcode = thread->last_error_opcode;
        err_out->pc = thread->last_error_pc;
        err_out->script_id = thread->last_error_script_id;
    }
    return CS2VM2_THREAD_ERROR;
}

enum CS2VM2_ThreadStatus
CS2VM2_ThreadResume(
    struct CS2VM2_Thread* thread,
    struct CS2VM2_ThreadError* err_out)
{
    assert(thread);
    int rc = CS2VM2_RunScript(thread);
    return CS2VM2_ThreadStatusFromRc(rc, thread, err_out);
}

enum CS2VM2_ThreadStatus
CS2VM2_ThreadRun(
    struct CS2VM2_Thread* thread,
    struct CS2VM2_ThreadError* err_out)
{
    assert(thread);
    return CS2VM2_ThreadResume(thread, err_out);
}
