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

void
cs2vm_host_push_int(
    struct CS2_InvokeCtx* ctx,
    int value);

char*
cs2vm_host_pop_string(struct CS2_InvokeCtx* ctx);

void
cs2vm_host_push_string(
    struct CS2_InvokeCtx* ctx,
    char* value);

void
cs2vm_host_set_active_component(
    struct CS2_InvokeCtx* ctx,
    int component_id);

#endif
