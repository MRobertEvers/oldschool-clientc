#ifndef CS2VM2_H
#define CS2VM2_H

#include "cs2_opcode.h"
#include "cs2vm2_host.h"
#include "cs2vm2_script.h"

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
#define CS2VM_FRAME(thread) &((struct CS2VM2_Thread*)(thread))->frames[((struct CS2VM2_Thread*)(thread))->frame_sp - 1]
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

#define CS2VM_MAX_FRAMES 32
#define CS2VM_MAX_CYCLES 1000000
#define CS2VM2_MAX_ARRAYS 128
#define CS2VM2_ARRAY_CAPACITY 256

struct CS2VM2_Array
{
    int values[CS2VM2_ARRAY_CAPACITY];
    int size;
    int defined;
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
};

#define CS2VM2_MAX_THREADS 4
struct CS2VM2_Thread
{
    struct CS2VM2* vm;

    struct CS2VM2_Frame frames[CS2VM_MAX_FRAMES];

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

    int children_iter_indices[CS2VM2_CHILDREN_ITER_MAX];
    int children_iter_count;
    int children_iter_index;

    struct CS2VM2_Array arrays[CS2VM2_MAX_ARRAYS];

    /* Host-provided canvas size for GETCANVASSIZE / viewport ops. */
    int canvas_w;
    int canvas_h;
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

void
CS2VM2_Init(struct CS2VM2* vm);

void
CS2VM2_Free(struct CS2VM2* vm);

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

int
CS2VM2_PushStr(
    struct CS2VM2_Thread* thread,
    char* operand);

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

struct CS2VM2_Thread*
CS2VM2_ThreadMain(struct CS2VM2* vm);

void
CS2VM2_ThreadSetCanvas(
    struct CS2VM2_Thread* thread,
    int w,
    int h);

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
