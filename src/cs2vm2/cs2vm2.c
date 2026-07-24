/*
 * CS2VM2 — yield-capable ClientScript 2 bytecode interpreter.
 * Ported from v1/vm/cs2vmx (CS2VMX_*). Host ops go through CS2VM2_HostExec_Fn.
 */

#include "cs2vm2.h"

#include "cs2_opcode.h"
#include "cs2_opcode_meta.h"
#include "cs2vm2_opcode_stack.gen.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS2VM2_DEBUG_OPS 0

int g_cs2_trace_mode = 0;
char g_cs2_trace_extra[512];

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
    for( int i = cp->frame_sp; i < vm->frame_sp; i++ )
        memset(&vm->frames[i], 0, sizeof(struct CS2VM2_Frame));

    vm->ints_stack_top = cp->ints_stack_top;
    vm->strs_stack_top = cp->strs_stack_top;
    vm->frame_sp = cp->frame_sp;
    vm->active_component_id = cp->active_component_id;
    vm->dot_component_id = cp->dot_component_id;
    vm->frames[vm->frame_sp - 1].pc = op_pc;

    /* Undo any opt-in VM-field mutations the yielding op applied before it
     * yielded, newest first, so the op re-runs from the same clean state as the
     * stacks/frames. Ops that never mutate persistent fields append nothing and
     * pay nothing here. */
    while( vm->undo_log_len > cp->undo_log_len )
    {
        struct CS2VM2_ArrayUndo const* undo = &vm->undo_log[--vm->undo_log_len];
        if( undo->slot >= 0 && undo->slot < CS2VM2_MAX_ARRAYS &&
            undo->index >= 0 && undo->index < CS2VM2_ARRAY_CAPACITY )
            vm->arrays[undo->slot].values[undo->index] = undo->old_value;
    }
}

/* Opt-in tracked array store: records the prior value so a yield restore can undo
 * it (see the undo_log contract in CS2VM2_Thread). Used by array-writing opcodes
 * whose replay-on-yield would otherwise double-apply. */
void
CS2VM2_ArrayStore(struct CS2VM2_Thread* vm, int slot, int index, int value)
{
    assert(vm);
    if( slot < 0 || slot >= CS2VM2_MAX_ARRAYS || !vm->arrays[slot].defined ||
        index < 0 || index >= vm->arrays[slot].size )
        return;
    if( vm->undo_log_len < CS2VM2_ARRAY_UNDO_MAX )
    {
        vm->undo_log[vm->undo_log_len].slot = (short)slot;
        vm->undo_log[vm->undo_log_len].index = index;
        vm->undo_log[vm->undo_log_len].old_value = vm->arrays[slot].values[index];
        vm->undo_log_len++;
    }
    vm->arrays[slot].values[index] = value;
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
    case CS2_OP_CC_CHILDREN_FINDNEXTID:
    case CS2_OP_IF_CHILDREN_FIND:
    case CS2_OP_IF_CHILDREN_FINDNEXTID:
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

static void
CS2VM2_TraceOpcode(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int op_pc,
    int opcode,
    int operand,
    int result)
{
    if( !g_cs2_trace_mode )
        return;
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
    frame->str_locals[idx] = value ? strdup(value) : NULL;
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

    /* Stack strings are owned by the stack (consumers free them); pushing the
     * local's pointer directly lets a consumer free the local's storage and
     * leave every alias dangling (e.g. IF_SETON* hook args). Push a copy. */
    return CS2VM2_PushStr(
        vm, frame->str_locals[idx] ? strdup(frame->str_locals[idx]) : NULL);
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
    char* copy = value ? strdup(value) : strdup("");
    assert(copy);

    return CS2VM2_PushStr(vm, copy);
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
    {
        char* empty = strdup("");
        if( !empty )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushStr(vm, empty);
    }

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

    size_t total = 1;
    for( int i = 0; i < count; i++ )
        total += parts[i] ? strlen(parts[i]) : 0;

    char* joined = malloc(total);
    if( !joined )
    {
        free(parts);
        return CS2VM_EXECNO_ERROR;
    }

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

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);

    char* str = strdup(buf);
    assert(str);

    return CS2VM2_PushStr(vm, str);
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
    char* out = malloc((len * 4u) + 1u);
    assert(out);

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

    free(text);
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
    /* PopStr already transferred ownership, so the buffer can be pushed back. */
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
    /* PopStr already transferred ownership, so the buffer can be pushed back. */
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
    char* out = malloc(len + 1u);
    assert(out);

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

    free(text);
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

    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s", dest ? dest : "", src ? src : "");

    char* result = strdup(buf);
    assert(result);

    return CS2VM2_PushStr(vm, result);
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

    char buf[512];
    snprintf(buf, sizeof(buf), "%s%d", str ? str : "", num);

    char* result = strdup(buf);
    assert(result);

    return CS2VM2_PushStr(vm, result);
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

    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s%d", str ? str : "", num >= 0 ? "+" : "", num);

    char* result = strdup(buf);
    assert(result);

    return CS2VM2_PushStr(vm, result);
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

    char buf[512];
    size_t len = str ? strlen(str) : 0;
    if( len >= sizeof(buf) - 2 )
        len = sizeof(buf) - 2;
    if( str && len > 0 )
        memcpy(buf, str, len);
    buf[len] = (char)chr;
    buf[len + 1] = '\0';

    char* result = strdup(buf);
    assert(result);

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
        return CS2VM_EXECNO_ERROR;

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

    struct CS2VM2_Frame* callee = &vm->frames[vm->frame_sp - 1];
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

    int is_nested, child_index, type, parent_id;

    if( CS2VM2_PopInt(vm, &is_nested) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
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
    int operand)
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

    int result = vm->vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_IF_SetObject(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
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
        int int_args[32] = { 0 };
        int int_arg_count = 0;
        char* str_by_pos[32] = { 0 };
        uint32_t str_arg_mask = 0;

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
                        for( int k = 0; k < 32; k++ )
                            free(str_by_pos[k]);
                        free(trigger_ids);
                        return CS2VM_EXECNO_ERROR;
                    }
                    if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                    {
                        str_by_pos[i] = v;
                        str_arg_mask |= (uint32_t)1 << i;
                        if( i + 1 > int_arg_count )
                            int_arg_count = i + 1;
                    }
                    else
                    {
                        free(v);
                    }
                }
                else
                {
                    int v = 0;
                    if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                    {
                        for( int k = 0; k < 32; k++ )
                            free(str_by_pos[k]);
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
            for( int k = 0; k < 32; k++ )
                free(str_by_pos[k]);
            free(trigger_ids);
            return CS2VM_EXECNO_ERROR;
        }

        if( script_id == -1 )
        {
            for( int k = 0; k < 32; k++ )
                free(str_by_pos[k]);
            free(trigger_ids);
            return CS2VM_EXECNO_OK;
        }

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
        for( int i = 0; i < 32; i++ )
        {
            if( !(str_arg_mask & ((uint32_t)1 << i)) )
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
        for( int k = 0; k < 32; k++ )
            free(str_by_pos[k]);
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
        int int_args[32] = { 0 };
        int int_arg_count = 0;
        char* str_by_pos[32] = { 0 };
        uint32_t str_arg_mask = 0;

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
                        for( int k = 0; k < 32; k++ )
                            free(str_by_pos[k]);
                        free(trigger_ids);
                        return CS2VM_EXECNO_ERROR;
                    }
                    if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                    {
                        str_by_pos[i] = v;
                        str_arg_mask |= (uint32_t)1 << i;
                        if( i + 1 > int_arg_count )
                            int_arg_count = i + 1;
                    }
                    else
                    {
                        free(v);
                    }
                }
                else
                {
                    int v = 0;
                    if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                    {
                        for( int k = 0; k < 32; k++ )
                            free(str_by_pos[k]);
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
            for( int k = 0; k < 32; k++ )
                free(str_by_pos[k]);
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
        for( int i = 0; i < 32; i++ )
        {
            if( !(str_arg_mask & ((uint32_t)1 << i)) )
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
        for( int k = 0; k < 32; k++ )
            free(str_by_pos[k]);
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
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.widget_set_int.component_id = CS2VM2_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_int.value = 1;
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

int
CS2VM2_Op_IF_SetOnVarTransmit(
    struct CS2VM2_Thread* vm,
    struct CS2VM2_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int widget_uid, script_id, trigger_count;
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

    int int_args[32] = { 0 };
    int int_arg_count = 0;
    char* str_by_pos[32] = { 0 };
    uint32_t str_arg_mask = 0;

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
                    for( int k = 0; k < 32; k++ )
                        free(str_by_pos[k]);
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                {
                    str_by_pos[i] = v;
                    str_arg_mask |= (uint32_t)1 << i;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
                else
                {
                    free(v);
                }
            }
            else
            {
                int v = 0;
                if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    for( int k = 0; k < 32; k++ )
                        free(str_by_pos[k]);
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
        for( int k = 0; k < 32; k++ )
            free(str_by_pos[k]);
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        for( int k = 0; k < 32; k++ )
            free(str_by_pos[k]);
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT;
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
    for( int i = 0; i < 32; i++ )
    {
        if( !(str_arg_mask & ((uint32_t)1 << i)) )
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
    for( int k = 0; k < 32; k++ )
        free(str_by_pos[k]);

    int result = vm->vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
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

    int int_args[32] = { 0 };
    int int_arg_count = 0;
    char* str_by_pos[32] = { 0 };
    uint32_t str_arg_mask = 0;

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
                    for( int k = 0; k < 32; k++ )
                        free(str_by_pos[k]);
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                if( i < (int)(sizeof(str_by_pos) / sizeof(str_by_pos[0])) )
                {
                    str_by_pos[i] = v;
                    str_arg_mask |= (uint32_t)1 << i;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
                else
                {
                    free(v);
                }
            }
            else
            {
                int v = 0;
                if( CS2VM2_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    for( int k = 0; k < 32; k++ )
                        free(str_by_pos[k]);
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
        for( int k = 0; k < 32; k++ )
            free(str_by_pos[k]);
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        for( int k = 0; k < 32; k++ )
            free(str_by_pos[k]);
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

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
    for( int i = 0; i < 32; i++ )
    {
        if( !(str_arg_mask & ((uint32_t)1 << i)) )
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
    for( int k = 0; k < 32; k++ )
        free(str_by_pos[k]);

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

    for( int i = 0; i < operand; i++ )
    {
        int intpop;
        if( CS2VM2_PopInt(vm, &intpop) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return CS2VM_EXECNO_OK;
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

    for( int i = 0; i < operand; i++ )
    {
        char* strpop;
        if( CS2VM2_PopStr(vm, &strpop) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return CS2VM_EXECNO_OK;
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
CS2VM2_Op_ViewPortGetEffectiveSize(
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

    /* Resizable mode (2) matches live-client semantics for gameframe scripts. */
    return CS2VM2_PushInt(vm, 2);
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

    return CS2VM2_PushInt(vm, 2);
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
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VM2_PushInt(vm, 0);
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
    (void)operand;

    int value;
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM_EXECNO_OK;
}

int
CS2VM2_Op_PopVarbit(
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
    return CS2VM_EXECNO_OK;
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

    int const slot = CS2VM2_ArrayDefineSlot(operand);
    if( slot < 0 || slot >= CS2VM2_MAX_ARRAYS )
        return CS2VM_EXECNO_OK;

    if( size < 0 )
        size = 0;
    if( size > CS2VM2_ARRAY_CAPACITY )
        size = CS2VM2_ARRAY_CAPACITY;

    vm->arrays[slot].defined = 1;
    vm->arrays[slot].size = size;
    memset(vm->arrays[slot].values, 0, sizeof(vm->arrays[slot].values));
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

    int value = 0;
    if( operand >= 0 && operand < CS2VM2_MAX_ARRAYS && vm->arrays[operand].defined && index >= 0 &&
        index < vm->arrays[operand].size )
        value = vm->arrays[operand].values[index];

    return CS2VM2_PushInt(vm, value);
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
    if( CS2VM2_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    /* Tracked store: records the prior cell value in the per-op undo log so a
     * yield inside this op cannot leave the write half-applied on replay. */
    CS2VM2_ArrayStore(vm, operand, index, value);

    return CS2VM_EXECNO_OK;
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

    return CS2VM2_PushInt(vm, rand());
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
    char* out = malloc((size_t)out_len + 1u);
    assert(out);

    if( out_len > 0 )
        memcpy(out, text + start, (size_t)out_len);
    out[out_len] = '\0';
    return CS2VM2_PushStr(vm, out);
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

    int ch;
    int start;
    char* haystack;
    if( CS2VM2_PopInt(vm, &ch) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopInt(vm, &start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VM2_PopStr(vm, &haystack) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = -1;
    if( haystack )
    {
        int len = (int)strlen(haystack);
        if( start < 0 )
            start = 0;
        for( int i = start; i < len; i++ )
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
        {
            free(body);
            return CS2VM_EXECNO_ERROR;
        }
        if( CS2VM2_PopInt(vm, &request.u.local_notification.delay_ms) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &request.u.local_notification.id) != CS2VM_EXECNO_OK )
        {
            free(body);
            free(title);
            return CS2VM_EXECNO_ERROR;
        }
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

    result = vm->vm->host_exec(vm, &request);
    /* The host only borrows the strings, so they are freed here regardless. */
    free(title);
    free(body);
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

    if( opcode < 0 || opcode >= CS2VM2_OPCODE_STACK_MAX )
        return CS2VM_EXECNO_OK;

    struct CS2VM2OpcodeStack const meta = g_cs2vm2_opcode_stack[opcode];

    /* Reaching the generic stub with an unknown signature means the opcode is
     * unimplemented: nobody has given it a real stack signature, so the pop/push
     * counts below are just the (0,0,0,0) default. Silently no-oping it corrupts
     * the stack and aborts the script at some unrelated downstream opcode (e.g. a
     * later BRANCH_EQUALS underflows). Assert here so the culprit surfaces at the
     * opcode itself. In release builds (NDEBUG) this compiles out and the stub
     * falls back to its previous best-effort no-op behaviour. */
    if( !meta.known )
    {
        CS2VM2_ReportUnimplementedOpcode(frame, opcode);
        assert(0 && "unimplemented CS2 opcode reached StackMetaStub");
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
        /* PopStr transfers ownership and every string on the stack is heap
         * allocated, so the stub owns the only reference to a value it is
         * about to throw away. */
        free(discard);
    }
    for( int i = 0; i < meta.int_out; i++ )
    {
        if( CS2VM2_PushInt(vm, 0) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    for( int i = 0; i < meta.str_out; i++ )
    {
        if( CS2VM2_PushStr(vm, strdup("")) != CS2VM_EXECNO_OK )
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

/* OC_OP/OC_IOP: ground/inventory right-click action string. The menu slot is
 * the opcode's baked-in operand (0..4), not a popped arg — same convention as
 * e.g. CC_SETOP's op index. */
static int
CS2VM2_Op_OC_ActionString(
    struct CS2VM2_Thread* vm,
    int opcode,
    int operand,
    enum CS2VM_HostRequestKind kind)
{
    assert(vm);

    int item_id;
    if( CS2VM2_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.oc_op.opcode = opcode;
    request.u.oc_op.item_id = item_id;
    request.u.oc_op.op_index = operand;
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
 * OC_FIND may yield so the host can bulk-load the obj group. On a yield the VM
 * rolls the string stack back and re-executes this op, so the popped query must
 * stay valid and un-freed: it is left in place (PopStr does not clear the slot,
 * and the checkpoint restore re-exposes it) and passed to the host only as a
 * borrow. It is freed here only once the host completes. */
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

    int result = vm->vm->host_exec(vm, &request);

    /* On yield, leave `query` on the (rolled-back) stack for the retry; free it
     * only when the op is truly finished. */
    if( result != CS2VM_EXECNO_YIELD )
        free(query);
    return result;
}

/* OC_SHIFTCLICKIOP: no per-item shift-click preference data exists yet. */
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
        return CS2VM2_Op_PushVarcString(vm, frame, operand);
    case CS2_OP_POP_VARC_STRING:
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
    case CS2_OP_GETCANVASSIZE:
        return CS2VM2_Op_GetCanvasSize(vm, frame, operand);
    case CS2_OP_VIEWPORT_GETEFFECTIVESIZE:
        return CS2VM2_Op_ViewPortGetEffectiveSize(vm, frame, operand);
    /* SETFOV/SETZOOM/CLAMPFOV/GETFOV/GETZOOM (6200..6205, GETEFFECTIVESIZE
     * excluded) all route through the host so a SET/CLAMP round-trips through
     * the matching GET; see CS2VM2_Op_Viewport. */
    case CS2_OP_VIEWPORT_SETFOV:
    case CS2_OP_VIEWPORT_SETZOOM:
        return CS2VM2_Op_Viewport(vm, opcode, 2);
    case CS2_OP_VIEWPORT_CLAMPFOV:
        return CS2VM2_Op_Viewport(vm, opcode, 4);
    case CS2_OP_VIEWPORT_GETZOOM:
    case CS2_OP_VIEWPORT_GETFOV:
        return CS2VM2_Op_Viewport(vm, opcode, 0);
    /* UI zoom (6210..6214): SET pops a value, GET/RESET/GETDEFAULT pop nothing. */
    case CS2_OP_UIZOOM_SET:
        return CS2VM2_Op_UiZoom(vm, opcode, true);
    case CS2_OP_UIZOOM_GET:
    case CS2_OP_UIZOOM_RESET:
    case CS2_OP_UIZOOM_GETDEFAULT:
        return CS2VM2_Op_UiZoom(vm, opcode, false);
    /* Safe-area bounds (6220..6223, 6231): no-arg getters. */
    case CS2_OP_SAFEAREA_GETMINX:
    case CS2_OP_SAFEAREA_GETMINY:
    case CS2_OP_SAFEAREA_GETMAXX:
    case CS2_OP_SAFEAREA_GETMAXY:
    case CS2_OP_SAFEAREA_GETMAXY_ALT:
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
    case CS2_OP_RUNWEIGHT_VISIBLE:
        return CS2VM2_Op_RunWeightVisible(vm, frame, operand);
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
    case CS2_OP_IF_CHILDREN_FIND:
        return CS2VM2_Op_IF_ChildrenFind(vm, frame, operand);
    case CS2_OP_IF_CHILDREN_FINDNEXTID:
        return CS2VM2_Op_IF_ChildrenFindNextId(vm, frame, operand);
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
    case CS2_OP_CC_SETOBJECT:
    case CS2_OP_CC_SETOBJECT_ALWAYS_NUM:
    case CS2_OP_CC_SETOBJECT_NONUM:
        return CS2VM2_Op_CC_SetObject(vm, frame, operand);
    case CS2_OP_CC_SETOP:
        return CS2VM2_Op_CC_SetOp(vm, frame, operand);
    case CS2_OP_CC_SETOPBASE:
        return CS2VM2_Op_CC_SetOpBase(vm, frame, operand);
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
    case CS2_OP_CC_SETONCLICK:
        return CS2VM2_Op_CC_SetOnClick(vm, frame, operand);
    case CS2_OP_CC_SETONHOLD:
        return CS2VM2_Op_CC_SetOnHold(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSEOVER:
        return CS2VM2_Op_CC_SetOnMouseOver(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSELEAVE:
        return CS2VM2_Op_CC_SetOnMouseLeave(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSEREPEAT:
        return CS2VM2_Op_CC_SetOnMouseRepeat(vm, frame, operand);
    case CS2_OP_CC_SETONDRAG:
        return CS2VM2_Op_CC_SetOnDrag(vm, frame, operand);
    case CS2_OP_CC_SETONSCROLLWHEEL:
        return CS2VM2_Op_CC_SetOnScrollWheel(vm, frame, operand);
    case CS2_OP_CC_SETONKEY:
        return CS2VM2_Op_CC_SetOnKey(vm, frame, operand);
    case CS2_OP_CC_SETONOP:
        return CS2VM2_Op_CC_SetOnOp(vm, frame, operand);
    case CS2_OP_CC_SETONDRAGCOMPLETE:
        return CS2VM2_Op_CC_SetOnDragComplete(vm, frame, operand);
    case CS2_OP_CC_SETONRESIZE:
        return CS2VM2_Op_CC_SetOnResize(vm, frame, operand);
    case CS2_OP_CC_SETONSUBCHANGE:
        return CS2VM2_Op_CC_SetOnSubChange(vm, frame, operand);
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
    case CS2_OP_IF_HASSUB:
        return CS2VM2_Op_IF_HasSub(vm, frame, operand);
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
    case CS2_OP_IF_SETONMOUSEREPEAT:
        return CS2VM2_Op_IF_SetOnMouseRepeat(vm, frame, operand);
    case CS2_OP_IF_SETONTIMER:
        return CS2VM2_Op_IF_SetOnTimer(vm, frame, operand);
    case CS2_OP_IF_SETONSCROLLWHEEL:
        return CS2VM2_Op_IF_SetOnScrollWheel(vm, frame, operand);
    case CS2_OP_IF_SETONKEY:
        return CS2VM2_Op_IF_SetOnKey(vm, frame, operand);
    case CS2_OP_IF_SETONMISCTRANSMIT:
        return CS2VM2_Op_IF_SetOnMiscTransmit(vm, frame, operand);
    case CS2_OP_IF_SETOP:
        return CS2VM2_Op_IF_SetOp(vm, frame, operand);
    case CS2_OP_IF_SETOPBASE:
        return CS2VM2_Op_IF_SetOpBase(vm, frame, operand);
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
    case CS2_OP_IF_SETOBJECT_ALWAYS_NUM:
    case CS2_OP_IF_SETOBJECT_NONUM:
        return CS2VM2_Op_IF_SetObject(vm, frame, operand);
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
        return CS2VM2_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON);
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
        return CS2VM2_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON);
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
    case CS2_OP_IF_FIND:
        return CS2VM2_Op_IF_Find(vm, frame, operand);
    /* CC_SETTARGETVERB has no model yet, but it takes a string argument. An
     * explicit no-op case would leave that string on the stack and desync
     * every later opcode, so let it fall through to the stack-meta stub, which
     * pops (and frees) per the doc comment. IF_SETTARGETVERB already does. */
    case CS2_OP_CC_SETONVARTRANSMIT:
        return CS2VM2_Op_CC_SetOnVarTransmit(vm, frame, operand);
    case CS2_OP_CC_SETONTIMER:
        return CS2VM2_Op_CC_SetOnTimer(vm, frame, operand);
    case CS2_OP_CC_SETONINVTRANSMIT:
        return CS2VM2_Op_CC_SetOnInvTransmit(vm, frame, operand);
    case CS2_OP_CC_SETONSTATTRANSMIT:
    /* No model for these events yet. They MUST still be parsed: the handler
     * signature string drives how many operands to pop, so the static stack
     * table cannot describe them and the StackMetaStub fallback would pop
     * nothing and desync the operand stack for every later opcode. */
    case CS2_OP_CC_SETONRELEASE:
    case CS2_OP_CC_SETONTARGETLEAVE:
    case CS2_OP_CC_SETONTARGETENTER:
    case CS2_OP_CC_SETONCLICKREPEAT:
    case CS2_OP_CC_SETONCHATTRANSMIT:
    case CS2_OP_CC_SETONCLANTRANSMIT:
    case CS2_OP_CC_SETONMISCTRANSMIT:
    case CS2_OP_CC_SETONDIALOGABORT:
    case CS2_OP_CC_SETONSTOCKTRANSMIT:
    case CS2_OP_CC_SETONCLANSETTINGSTRANSMIT:
    case CS2_OP_CC_SETONCLANCHANNELTRANSMIT:
    case CS2_OP_CC_SETONITEMONITEM:
    case CS2_OP_CC_SETONCLANSETTINGS:
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
    case CS2_OP_CC_GETTRANS:
        return CS2VM2_Op_CC_GetTrans(vm, frame, operand);
    case CS2_OP_IF_SETONHOLD:
        return CS2VM2_Op_IF_SetOnHold(vm, frame, operand);
    case CS2_OP_IF_SETONRELEASE:
    case CS2_OP_IF_SETONTARGETLEAVE:
    case CS2_OP_IF_SETONSTATTRANSMIT:
    case CS2_OP_IF_SETONTARGETENTER:
    case CS2_OP_IF_SETONFRIENDTRANSMIT:
    case CS2_OP_IF_SETONCLANTRANSMIT:
    case CS2_OP_IF_SETONDIALOGABORT:
    case CS2_OP_IF_SETONCLANSETTINGSTRANSMIT:
    case CS2_OP_IF_SETONCLANCHANNELTRANSMIT:
    /* Same reasoning as the CC_SETON* discard group above: signature-driven
     * operand counts, so they must be parsed rather than left to the stub. */
    case CS2_OP_IF_SETONCLICKREPEAT:
    case CS2_OP_IF_SETONCHATTRANSMIT:
    case CS2_OP_IF_SETONSTOCKTRANSMIT:
    case CS2_OP_IF_SETONITEMONITEM:
    case CS2_OP_IF_SETONCLANSETTINGS:
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
    case CS2_OP_CLIENTCLOCK:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_CLIENTCLOCK;
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
    case CS2_OP_CAM_GETYAW:
    {
        struct CS2VM_HostRequest request;
        memset(&request, 0, sizeof(request));
        request.kind = CS2VM_HOST_REQUEST_CAM_GETYAW;
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
    case CS2_OP_SOUND_SYNTH:
    {
        /* id, loops, delay — audio not implemented; discard args. */
        int discard;
        if( CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_SOUND_SONG:
    {
        int discard;
        if( CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_SOUND_JINGLE:
    {
        int discard;
        if( CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_IF_CLOSE:
        /* No stack args. Generated meta wrongly treats this like MES (1 string). */
        return CS2VM_EXECNO_OK;
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

    memset(&vm->frames[vm->frame_sp], 0, sizeof(struct CS2VM2_Frame));
    vm->frames[vm->frame_sp].script = script;
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

int
CS2VM2_RunScript(struct CS2VM2_Thread* vm)
{
    assert(vm);
    assert(vm->frame_sp > 0);

    int result;
    int cycles = 0;
    while( cycles++ < CS2VM_MAX_CYCLES )
    {
        if( vm->frame_sp <= 0 )
            return CS2VM_EXECNO_DONE;

        struct CS2VM2_Frame* frame = &vm->frames[vm->frame_sp - 1];
        if( frame->pc >= frame->script->op_count )
            return CS2VM_EXECNO_DONE;

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
                return CS2VM_EXECNO_ERROR;
            CS2VM2_RestoreYieldCheckpoint(vm, &yield_cp, op_pc);
            return CS2VM_EXECNO_YIELD;
        default:
            if( result == CS2VM_EXECNO_ERROR )
            {
                vm->last_error_opcode = opcode;
                vm->last_error_pc = op_pc;
                vm->last_error_script_id = frame->script->script_id;
            }
            return result;
        }
    }

    {
        struct CS2VM2_Frame* frame = &vm->frames[vm->frame_sp - 1];
        vm->last_error_opcode = -1;
        vm->last_error_pc = frame->pc;
        vm->last_error_script_id = frame->script->script_id;
    }
    return CS2VM_EXECNO_ERROR;
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
    CS2VM2_ClearYieldHalt(vm);
}

void
CS2VM2_Init(struct CS2VM2* vm)
{
    assert(vm);
    memset(vm, 0, sizeof(*vm));
    vm->thread_count = CS2VM2_MAX_THREADS;
    for( int i = 0; i < CS2VM2_MAX_THREADS; i++ )
        vm->threads[i].vm = vm;
}

void
CS2VM2_Free(struct CS2VM2* vm)
{
    assert(vm);
    for( int i = 0; i < CS2VM2_MAX_THREADS; i++ )
        CS2VM2_ResetRuntime(&vm->threads[i]);
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
