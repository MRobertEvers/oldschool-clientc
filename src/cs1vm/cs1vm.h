#ifndef CS1VM_H
#define CS1VM_H

#include "cs1_opcode.h"
#include "cs1vm_host.h"
#include "cs1vm_script.h"

#include <stdbool.h>

/* On CS1VM_EXECNO_YIELD the host must not have mutated VM state; the opcode
 * checkpoint in cs1vm_run_script rolls the register file and pc back so the
 * eval can be re-entered after external host work. */
#define CS1VM_EXECNO_YIELD -2
#define CS1VM_EXECNO_ERROR -1
#define CS1VM_EXECNO_OK 0
#define CS1VM_EXECNO_DONE 1

/* getIfVar result sentinels (Client-TS parity). These are script *values*, not
 * statuses: a malformed or absent script still completes, it just evaluates to
 * a sentinel that the comparator pass then tests like any other number. */
#define CS1VM_VALUE_ERR_EXCEPTION -1
#define CS1VM_VALUE_ERR_NO_SCRIPT -2

/** "Infinite" count from CS1_OP_INV_CONTAINS; also the %N "*" display cutoff. */
#define CS1VM_VALUE_INFINITY 999999999

/** IF1 comparator codes (UITreeBehavior.script_comparator entries). */
#define CS1VM_COMPARATOR_EQUAL 1
#define CS1VM_COMPARATOR_LESS 2
#define CS1VM_COMPARATOR_GREATER 3
#define CS1VM_COMPARATOR_NOT_EQUAL 4

/** Pending-arithmetic register: how the next value folds into the accumulator. */
#define CS1VM_ARITH_ADD 0
#define CS1VM_ARITH_SUB 1
#define CS1VM_ARITH_DIV 2
#define CS1VM_ARITH_MUL 3

#define CS1VM_MAX_CYCLES 4096

enum CS1VM_EvalStatus
{
    CS1VM_EVAL_DONE,    /* out params valid */
    CS1VM_EVAL_YIELDED, /* host staged a load; caller retries after servicing it */
    CS1VM_EVAL_ERROR,   /* contract violation (see yield_halt_*) */
};

/* CS1 has no frames, call stack, or operand stacks, so a yield checkpoint is
 * the whole register file. Same contract as CS2VM2_YieldCheckpoint: restore
 * resets pc to the yielding opcode so it re-runs on the retry. */
struct CS1VM_YieldCheckpoint
{
    int acc;
    int pending_arith;
    int pc;
};

#define CS1VM_USER(vm) ((struct CS1VM*)(vm))->user

struct CS1VM
{
    CS1VM_HostExec_Fn host_exec;
    void* user;

    /* Registers of the eval in progress. */
    int acc;
    int pending_arith;
    int pc;

    /* Tracks cooperative yields for the current opcode site; error if > 1.
     * Keyed by script index too: scripts within one component share pc values. */
    int yield_halt_script_index;
    int yield_halt_pc;
    int yield_halt_count;

    /* Script index being evaluated, for the yield-halt key. */
    int script_index;

    /* Diagnostics for the opcode that last caused CS1VM_EXECNO_ERROR. */
    int last_error_opcode;
    int last_error_pc;
};

void
CS1VM_Init(struct CS1VM* vm);

void
CS1VM_BindHost(
    struct CS1VM* vm,
    void* user,
    CS1VM_HostExec_Fn host_exec);

/**
 * getIfVar: run one value script to completion. On CS1VM_EVAL_DONE *out_value
 * holds the result, which may be a CS1VM_VALUE_ERR_* sentinel.
 */
enum CS1VM_EvalStatus
CS1VM_EvalValue(
    struct CS1VM* vm,
    struct CS1VM_ComponentScripts const* set,
    int script_index,
    int* out_value);

/**
 * getIfActive: every comparator entry must pass for the component to be active.
 * A component with no comparators is never active.
 */
enum CS1VM_EvalStatus
CS1VM_EvalActive(
    struct CS1VM* vm,
    struct CS1VM_ComponentScripts const* set,
    bool* out_active);

#endif /* CS1VM_H */
