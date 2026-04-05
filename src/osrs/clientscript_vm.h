#ifndef CLIENTSCRIPT_VM_H
#define CLIENTSCRIPT_VM_H

#include <stdbool.h>

struct GGame;
struct CacheDatConfigComponent;

/**
 * Holds state for interface "client script" evaluation (varp/varbit/stat bytecode on components).
 * Extend with stacks / string table as needed.
 */
struct ClientScriptVM
{
    int int_stack[256];
    int int_stack_ptr;
};

struct ClientScriptVM*
clientscript_vm_new(void);

void
clientscript_vm_free(struct ClientScriptVM* vm);

/** Same contract as legacy interface_get_if_var: return value, or -1/-2 on missing script. */
int
clientscript_vm_if_var(
    struct ClientScriptVM* vm,
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int script_id);

bool
clientscript_vm_if_active(
    struct ClientScriptVM* vm,
    struct GGame* game,
    struct CacheDatConfigComponent* component);

#endif
