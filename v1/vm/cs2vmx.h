#ifndef CS2VMX_H
#define CS2VMX_H

#include "cs2_script.h"
#include "cs2vmx_host.h"

#include <stdbool.h>
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
 * checkpoint in CS2VMX_RunScript rolls back stack, frames, and pc so RunScript can
 * be re-entered after external host work. */
#define CS2VM_EXECNO_YIELD -2
#define CS2VM_EXECNO_ERROR -1
#define CS2VM_EXECNO_OK 0
#define CS2VM_EXECNO_DONE 1

#define CS2VM_STACK_MAX 1024

#define CS2VM_USER(vm) ((struct CS2VMX*)(vm))->user
#define CS2VM_FRAME(vm) &((struct CS2VMX*)(vm))->frames[((struct CS2VMX*)(vm))->frame_sp - 1]
#define CS2VM_MAX_LOCALS 1024
struct CS2VMX_Frame
{
    struct CS2_Script* script;
    int pc;
    int int_locals[CS2VM_MAX_LOCALS];
    char* str_locals[CS2VM_MAX_LOCALS];

    int return_pc;
    int return_frame;

    int has_return;
    int return_int_count;
    int return_ints[8];
};

#define CS2VM_MAX_FRAMES 32
#define CS2VM_MAX_CYCLES 1000000
#define CS2VMX_MAX_ARRAYS 128
#define CS2VMX_ARRAY_CAPACITY 256

struct CS2VMXArray
{
    int values[CS2VMX_ARRAY_CAPACITY];
    int size;
    int defined;
};

#define CS2VMX_CHILDREN_ITER_MAX 256

/* Opcodes missing from cs2_opcode.h but used by gameframe scripts. */
#define CS2_OP_CC_CREATECHILD 106
#define CS2_OP_CC_CREATESIBLING 107
#define CS2_OP_CC_FINDROOT 202
#define CS2_OP_CC_CHILDREN_FIND 203
#define CS2_OP_CC_CHILDREN_FINDNEXTID 204
#define CS2_OP_IF_CHILDREN_FIND 205
#define CS2_OP_IF_CHILDREN_FINDNEXTID 206
#define CS2_OP_CC_SETGRAPHIC2 1122
#define CS2_OP_CC_SETTRANSBOT 1124
#define CS2_OP_CC_SETFILLMODE 1125
#define CS2_OP_CC_SETARC 1128
#define CS2_OP_CC_SETPINCH 1308
#define CS2_OP_CC_SETPLAYERMODEL_SELF 1203
#define CS2_OP_CC_SETMODEL_PLAYERCHATHEAD 1204
#define CS2_OP_IF_SETTRANSBOT 2124
#define CS2_OP_IF_SETFILLMODE 2125
#define CS2_OP_IF_SETARC 2128
#define CS2_OP_IF_SETCLICKMASK 2308
#define CS2_OP_IF_SETPINCH 2309
#define CS2_OP_IF_SETMODEL_PLAYERCHATHEAD 2203
#define CS2_OP_CC_INPUT_SETSUBMITMODE 7200
#define CS2_OP_CC_INPUT_SETSELECTCOLOUR 7201
#define CS2_OP_CC_INPUT_SETWRAPMODE 7202
#define CS2_OP_CC_INPUT_SETLINEWRAPPINGWIDTH 7203
#define CS2_OP_CC_INPUT_SETSELECTBGCOLOUR 7204
#define CS2_OP_CC_INPUT_SETLINECOUNTLIMIT 7205
#define CS2_OP_CC_INPUT_SETCURSORCOLOUR 7206
#define CS2_OP_CC_INPUT_SETCURSORTRANS 7207
#define CS2_OP_CC_INPUT_SETCURSORWIDTH 7208
#define CS2_OP_CC_INPUT_SETCURSORHEIGHT 7209
#define CS2_OP_CC_INPUT_SETCURSOROFFSET 7210
#define CS2_OP_CC_INPUT_SETLINEWIDTHLIMIT 7211
#define CS2_OP_CC_INPUT_SETCHARFILTER 7212


struct CS2VMX
{
    void* user;
    CS2VMX_HostExec_Fn host_exec;
    struct CS2VMX_Frame frames[CS2VM_MAX_FRAMES];

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

    int children_iter_indices[CS2VMX_CHILDREN_ITER_MAX];
    int children_iter_count;
    int children_iter_index;

    struct CS2VMXArray arrays[CS2VMX_MAX_ARRAYS];

    /* Host-provided canvas size for GETCANVASSIZE / viewport ops. */
    int canvas_w;
    int canvas_h;
};

struct CS2VMX_YieldCheckpoint
{
    int ints_stack_top;
    int strs_stack_top;
    int frame_sp;
    int active_component_id;
    int dot_component_id;
    struct CS2VMX_Frame frames[CS2VM_MAX_FRAMES];
};

/* Trace controls (0=off, 1=targeting ops, 2=all). */
extern int g_cs2_trace_mode;
extern char g_cs2_trace_extra[512];

void
CS2VMX_BindHost(
    struct CS2VMX* vm,
    void* user,
    CS2VMX_HostExec_Fn host_exec);

int
CS2VMX_DotOrActiveComponentId(
    struct CS2VMX* vm,
    int operand);

void
CS2VMX_SetTargetComponentId(
    struct CS2VMX* vm,
    int operand,
    int component_id);

void
CS2VMX_SetTraceExtra(
    char const* fmt,
    ...);

void
CS2VMX_ResetChildrenIter(struct CS2VMX* vm);

int
CS2VMX_PopInt(
    struct CS2VMX* vm,
    int* operand);

int
CS2VMX_PushInt(
    struct CS2VMX* vm,
    int operand);

int
CS2VMX_PopStr(
    struct CS2VMX* vm,
    char** operand);

int
CS2VMX_PushStr(
    struct CS2VMX* vm,
    char* operand);

int
CS2VMX_PushCallScript(
    struct CS2VMX* vm,
    struct CS2_Script* script);

int
CS2VMX_SetActiveAndDotComponentId(
    struct CS2VMX* vm,
    int component_id);

int
CS2VMX_SetIntCurrentFrameLocal(
    struct CS2VMX* vm,
    int local,
    int value);

int
CS2VMX_RunScript(struct CS2VMX* vm);

void
CS2VMX_ResetRuntime(struct CS2VMX* vm);

void
CS2VMX_ClearYieldHalt(struct CS2VMX* vm);

#endif /* CS2VMX_H */
