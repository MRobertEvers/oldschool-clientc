#ifndef CS2VM_H
#define CS2VM_H

#include "cs2_script.h"

#include <stdbool.h>
#include <stdint.h>

struct CS2VM;
struct CS2VMFrame;

struct CS2_InvokeCtx
{
    void* host_ud;
    struct CS2VM* vm;
    struct CS2VMFrame* frame;
    struct CS2_Script const* script;
    int pc;
    int opcode;
    int operand;
    int active_component;
    int dot_component;
};

/** Host-supplied callbacks. Any callback may be NULL (treated as 0 / stub). */
struct CS2Host
{
    void* ud;
    int (*get_varp)(void* ud, int id);
    int (*get_varbit)(void* ud, int id);
    int (*get_varc_int)(void* ud, int id);
    const char* (*get_varc_string)(void* ud, int id);
    void (*set_varp)(void* ud, int id, int value);
    void (*set_varbit)(void* ud, int id, int value);
    void (*set_varc_int)(void* ud, int id, int value);
    void (*set_varc_string)(void* ud, int id, char const* value);
    struct CS2_Script* (*resolve_script)(void* ud, int script_id);
    void (*invoke)(void* ud, struct CS2_InvokeCtx* ctx);
    /** CS2 enum(input_type, output_type, enum_id, key); return -1 if missing. */
    int (*enum_lookup)(void* ud, int input_type, int output_type, int enum_id, int key);
    /** NULL when enum is int-valued; otherwise the mapped string (never NULL for string enums). */
    char const* (*enum_lookup_string)(
        void* ud,
        int input_type,
        int output_type,
        int enum_id,
        int key);
};

struct CS2_RunArgs
{
    int int_argc;
    int const* int_argv;
    int string_argc;
    char const* const* string_argv;
};

#define CS2VM_OK 0
#define CS2VM_ERR_INVALID (-1)
#define CS2VM_ERR_PC_OOB (-2)
#define CS2VM_ERR_STEP_LIMIT (-3)
#define CS2VM_ERR_STACK (-4)
#define CS2VM_ERR_UNIMPL (-5)

/** OSRS script hook magic int args (Integer.MIN_VALUE + N). */
#define CS2_SCRIPT_ARG_MAGIC_MOUSE_X (-2147483647)
#define CS2_SCRIPT_ARG_MAGIC_MOUSE_Y (-2147483646)
#define CS2_SCRIPT_ARG_MAGIC_WIDGET_ID (-2147483645)
#define CS2_SCRIPT_ARG_MAGIC_OP_INDEX (-2147483644)
#define CS2_SCRIPT_ARG_MAGIC_WIDGET_CHILD_INDEX (-2147483643)
#define CS2_SCRIPT_ARG_MAGIC_DRAG_TARGET_ID (-2147483642)
#define CS2_SCRIPT_ARG_MAGIC_DRAG_TARGET_CHILD_INDEX (-2147483641)

struct CS2_ScriptArgSubstitute
{
    int component_id;
};

/** Replace cache hook magic ints (e.g. WIDGET_ID) with runtime values. */
int
cs2_script_arg_substitute_int(
    int value,
    struct CS2_ScriptArgSubstitute const* sub);

struct CS2VM*
cs2vm_new(void);

void
cs2vm_free(struct CS2VM* vm);

/** Run one clientscript. Returns CS2VM_OK on success, or a CS2VM_ERR_* code. */
int
cs2vm_run(
    struct CS2VM* vm,
    struct CS2_Script const* script,
    struct CS2Host const* host,
    struct CS2_RunArgs const* args);

int
cs2vm_host_pop_int(struct CS2_InvokeCtx* ctx);

bool
cs2vm_host_try_pop_int(
    struct CS2_InvokeCtx* ctx,
    int* out);

void
cs2vm_host_push_int(
    struct CS2_InvokeCtx* ctx,
    int value);

char*
cs2vm_host_pop_string(struct CS2_InvokeCtx* ctx);

bool
cs2vm_host_try_pop_string(
    struct CS2_InvokeCtx* ctx,
    char** out);

void
cs2vm_host_push_string(
    struct CS2_InvokeCtx* ctx,
    char* value);

/** Allocate a string in the VM string pool (lifetime = current script run). */
char*
cs2vm_host_alloc_string(
    struct CS2_InvokeCtx* ctx,
    char const* src);

void
cs2vm_host_set_active_component(
    struct CS2_InvokeCtx* ctx,
    int component_id);

void
cs2vm_set_active_component(
    struct CS2VM* vm,
    int component_id);

int
cs2vm_get_active_component(struct CS2VM const* vm);

void
cs2vm_host_set_dot_component(
    struct CS2_InvokeCtx* ctx,
    int component_id);

void
cs2vm_set_dot_component(
    struct CS2VM* vm,
    int component_id);

int
cs2vm_get_dot_component(struct CS2VM const* vm);

/** Enable/disable per-opcode stderr trace (also seeded from IE_CS2_TRACE env on first run). */
void
cs2vm_set_trace(bool enabled);

bool
cs2vm_get_trace(void);

/** Enable structured JSON-lines trace for parity tests (CS2_PARITY_TRACE env). */
void
cs2vm_set_trace_json(bool enabled);

bool
cs2vm_get_trace_json(void);

/** Opcode steps executed during the last cs2vm_run. */
int
cs2vm_get_opcount(struct CS2VM const* vm);

/** Int stack depth after the last cs2vm_run. */
int
cs2vm_get_int_stack_size(struct CS2VM const* vm);

/** String stack depth after the last cs2vm_run. */
int
cs2vm_get_string_stack_size(struct CS2VM const* vm);

/** Copy int stack values [0, size) into out (caller provides buffer). */
void
cs2vm_copy_int_stack(struct CS2VM const* vm, int* out, int max_count);

/** Copy string stack values [0, size) into out (pointers valid until next run). */
void
cs2vm_copy_string_stack(struct CS2VM const* vm, char const** out, int max_count);

/** Reset structured trace buffer (used with cs2vm_set_trace_json). */
void
cs2vm_clear_trace_buffer(void);

/** Number of trace entries recorded during the last run. */
int
cs2vm_get_trace_count(void);

struct CS2TraceEntry
{
    int step;
    int pc;
    int opcode;
    int int_sp;
    int str_sp;
    int top_int;
};

/** Copy trace entries [0, count) into out. */
void
cs2vm_copy_trace_buffer(struct CS2TraceEntry* out, int max_count);

/** Record a host-side failure and stop the current script. */
void
cs2vm_host_fail(
    struct CS2_InvokeCtx* ctx,
    int err_code);

/** Stop execution cleanly (TS RuntimeException / ABORTED). Does not set run_error. */
void
cs2vm_host_halt(struct CS2_InvokeCtx* ctx);

/** Last attributed error site (valid when cs2vm_run returned non-zero). */
void
cs2vm_get_error_info(
    struct CS2VM const* vm,
    int* out_opcode,
    int* out_pc,
    int* out_script_id);

char const*
cs2vm_err_name(int err_code);

#endif
