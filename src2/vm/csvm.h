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

/**
 * Evaluate one CS1 script with an explicit int count from cache.
 * When script_len > 0, stops at pc >= script_len (no opcode-0 required).
 * When script_len <= 0, uses opcode-0 termination (same as csvm_eval).
 */
int
csvm_eval_len(
    struct CSVM* vm,
    int const* script,
    struct CSVM_State const* state,
    int script_len);

/** Byte length of one CS1 script as int count (includes terminating opcode 0). */
int
csvm_script_length(int const* script);

/** Returns true when (value, comparator, operand) satisfies the active condition. */
bool
csvm_compare(
    int comparator,
    int value,
    int operand);

#endif
