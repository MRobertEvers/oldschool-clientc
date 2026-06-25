#ifndef CSVM_H
#define CSVM_H

#include <stdbool.h>
#include <stdint.h>

struct CSVM;

/** Host-supplied state. Any callback may be NULL (treated as 0 / stub). */
struct CSVM_State
{
    int (*get_varp)(
        void* ud,
        int id);
    int (*get_varbit)(
        void* ud,
        int id);
    int (*get_stat_level)(
        void* ud,
        int skill);
    int (*get_stat_base_level)(
        void* ud,
        int skill);
    int (*get_stat_xp)(
        void* ud,
        int skill);
    void* ud;
};

struct CSVM*
csvm_new(void);

void
csvm_free(struct CSVM* vm);

/** Evaluate one CS1 script (opcode-0 terminated int array). */
int
csvm_eval(
    struct CSVM* vm,
    int const* script,
    struct CSVM_State const* state);

/** Returns true when (value, comparator, operand) satisfies the active condition. */
bool
csvm_compare(
    int comparator,
    int value,
    int operand);

#endif
