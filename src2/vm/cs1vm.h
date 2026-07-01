#ifndef CS1VM_H
#define CS1VM_H

#include <stdbool.h>
#include <stdint.h>

struct CS1VM;

/** Host-supplied callbacks. Any callback may be NULL (treated as 0 / stub). */
struct CS1Host
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
    int (*get_inv_count)(
        void* ud,
        int iface_id,
        int obj_id);
    int (*inv_contains)(
        void* ud,
        int iface_id,
        int obj_id);
    int (*get_stat_xp_remaining)(
        void* ud,
        int skill);
    int (*get_combat_level)(void* ud);
    int (*get_total_level)(void* ud);
    int (*get_runenergy)(void* ud);
    int (*get_runweight)(void* ud);
    int (*get_coord_x)(void* ud);
    int (*get_coord_z)(void* ud);
    void* ud;
};

/** Inline dat1 behavior script classification (cs1Scripts). */
#define CS1VM_SCRIPT_KIND_CS1 0
#define CS1VM_SCRIPT_KIND_CS2 1

struct CS1VM*
cs1vm_new(void);

void
cs1vm_free(struct CS1VM* vm);

/** Evaluate one CS1 script (opcode-0 terminated int array). */
int
cs1vm_eval(
    struct CS1VM* vm,
    int const* script,
    struct CS1Host const* host);

/**
 * Evaluate one CS1 script with an explicit int count from cache.
 * When script_len > 0, stops at pc >= script_len (no opcode-0 required).
 * When script_len <= 0, uses opcode-0 termination (same as cs1vm_eval).
 */
int
cs1vm_eval_len(
    struct CS1VM* vm,
    int const* script,
    struct CS1Host const* host,
    int script_len);

/** Byte length of one CS1 script as int count (includes terminating opcode 0). */
int
cs1vm_script_length(int const* script);

/** Returns true when (value, comparator, operand) satisfies the active condition. */
bool
cs1vm_compare(
    int comparator,
    int value,
    int operand);

#endif
