#ifndef CS2VM_H
#define CS2VM_H

#include <stdbool.h>
#include <stdint.h>

struct CS2VM;

/** Host-supplied state for CS2 script evaluation. */
struct CS2VM_State
{
    int (*get_varp)(void* ud, int id);
    int (*get_varbit)(void* ud, int id);
    int (*get_varc)(void* ud, int id);
    int (*get_stat_level)(void* ud, int skill);
    void (*set_varp_optimistic)(void* ud, int id, int value);
    void* ud;
};

#define CS2VM_SCRIPT_KIND_CS1 0
#define CS2VM_SCRIPT_KIND_CS2 1

struct CS2VM*
cs2vm_new(void);

void
cs2vm_free(struct CS2VM* vm);

/** Evaluate one CS2 script (opcode-0 terminated int array). Returns top-of-stack or 0. */
int
cs2vm_eval(
    struct CS2VM* vm,
    int const* script,
    struct CS2VM_State const* state);

bool
cs2vm_compare(
    int comparator,
    int value,
    int operand);

#endif
