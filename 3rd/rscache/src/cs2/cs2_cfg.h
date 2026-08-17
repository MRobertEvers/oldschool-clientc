/*
 * Flow analysis over the linear IR, for the optimizer.
 *
 * Deliberately not `cs2_cfa.c`. That file answers "which source construct is
 * this?" and refuses a graph it cannot spell as if/while/switch, because a
 * decompile that will not structure cannot be printed. This one answers "what
 * reaches here, and what is still needed after here?", and must keep working on
 * exactly the graphs the other one gives up on — an inlined early return and an
 * unrolled loop body both produce flow the source language has no word for.
 *
 * Rebuilt from the chain on demand. Every pass that edits the chain drops its
 * graph rather than repairing it; the analysis is cheap next to the walk that
 * caused the edit, and a stale graph is the kind of bug that only shows up as a
 * miscompiled script months later.
 */
#ifndef RSCACHE_CS2_CFG_H
#define RSCACHE_CS2_CFG_H

#include "cs2_ir.h"

#include <stdbool.h>

struct RSCache_CS2_Block
{
    int index;
    /** First and last instruction of the block, inclusive. */
    struct RSCache_CS2_Insn* first;
    struct RSCache_CS2_Insn* last;
    /** Block indices. A branch has two, a switch has one per case plus fallthrough. */
    int* successors;
    int successor_count;
    int* predecessors;
    int predecessor_count;
    bool reachable;
};

/**
 * One local variable slot, as the analysis counts them.
 *
 * Banks are separate address spaces in the VM — int local 3 and string local 3
 * are different storage — so a slot is a (bank, index) pair flattened into one
 * number, with the int bank first.
 */
struct RSCache_CS2_Slots
{
    int int_count;
    int string_count;
    int total;
};

struct RSCache_CS2_Cfg
{
    struct RSCache_CS2_Function* function;
    struct RSCache_CS2_Block* blocks;
    int block_count;

    struct RSCache_CS2_Slots slots;
    /** Per block, `slots.total` bits each: read-before-written, and written. */
    unsigned char* use;
    unsigned char* def;
    /** Per block: live on entry / on exit. Solved to a fixpoint. */
    unsigned char* live_in;
    unsigned char* live_out;
    int bitset_bytes;

    /** Back edges, as (from block, to block) pairs. */
    int* loop_headers;
    int* loop_latches;
    int loop_count;
};

/** Build. Never fails on a well-formed chain; asserts on a malformed one. */
void
RSCache_CS2_CfgBuild(struct RSCache_CS2_Cfg* cfg, struct RSCache_CS2_Function* function);

void
RSCache_CS2_CfgFree(struct RSCache_CS2_Cfg* cfg);

/** The block containing an instruction, or -1. */
int
RSCache_CS2_CfgBlockOf(const struct RSCache_CS2_Cfg* cfg, const struct RSCache_CS2_Insn* insn);

/** The flattened slot number of a local variable, or -1 when it is not one. */
int
RSCache_CS2_CfgSlotOf(const struct RSCache_CS2_Cfg* cfg, const struct RSCache_CS2_Variable* variable);

/** True when the slot is live at the *end* of the block. */
bool
RSCache_CS2_CfgLiveOut(const struct RSCache_CS2_Cfg* cfg, int block, int slot);

/**
 * True when the slot is read before it is written on some path from entry.
 *
 * This is the question a reused frame asks. A `gosub` gives the callee a frame
 * of zeroes; a tail call that jumps back to the top instead hands it whatever
 * the previous iteration left behind, so every slot that answers true here has
 * to be re-zeroed for the jump to mean the same thing as the call.
 */
bool
RSCache_CS2_CfgLiveAtEntry(const struct RSCache_CS2_Cfg* cfg, int slot);

/**
 * True when the slot is live immediately after `insn` inside its own block.
 *
 * Walked forward from the instruction rather than read out of the block
 * summary, because a dead store is almost always dead within one block and the
 * summary only says what survives to the end of it.
 */
bool
RSCache_CS2_CfgLiveAfter(
    const struct RSCache_CS2_Cfg* cfg,
    struct RSCache_CS2_Insn* insn,
    int slot);

#endif
