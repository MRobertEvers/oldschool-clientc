/*
 * What an opcode is allowed to be moved, merged or deleted around.
 *
 * This is the optimizer's safety boundary, and it is deliberately the smallest
 * file in it. Every pass that reorders, folds or removes work asks exactly one
 * question — "may I?" — and the answer comes from here, so getting an entry
 * wrong is a miscompile and getting the *default* wrong is a miscompile in
 * every script at once.
 *
 * So the default is the most restrictive class there is, and the allowlist is
 * short, explicit, and derived by reading the matching `CS2VM2_Op_*` handler in
 * `src/cs2vm2/cs2vm2.c`. An opcode earns PURE only by touching nothing but the
 * operand stacks: no `host_exec`, no varp, no component, no clock, no random.
 * Nothing is inferred from a name.
 */
#ifndef RSCACHE_CS2_EFFECTS_H
#define RSCACHE_CS2_EFFECTS_H

#include <stdbool.h>

/**
 * Bumped whenever the table below changes.
 *
 * Feeds the optimizer manifest's input hash, so widening the allowlist
 * invalidates every `.cs2b` that was produced under the narrower one instead of
 * silently shipping code folded under different rules.
 */
#define RSCACHE_CS2_EFFECTS_VERSION 1

enum RSCache_CS2_Effect
{
    /**
     * A deterministic function of its arguments. No state is read or written.
     * May be folded when its arguments are constant, deleted when its result is
     * unused, hoisted out of a loop, and shared with an identical earlier
     * occurrence.
     */
    RSCACHE_CS2_EFFECT_PURE = 0,
    /**
     * Reads mutable client state and writes none. Safe to delete when unused;
     * NOT safe to fold, and only safe to share with an earlier occurrence when
     * nothing in between could have written what it reads.
     */
    RSCACHE_CS2_EFFECT_READ_STATE,
    /**
     * Everything else, and the default: writes state, talks to the host, may
     * yield. Order is preserved absolutely and it is never deleted.
     */
    RSCACHE_CS2_EFFECT_HOST,
};

enum RSCache_CS2_Effect
RSCache_CS2_EffectOf(int opcode);

/** True for the class that may be constant-folded and deleted when unused. */
bool
RSCache_CS2_EffectIsPure(int opcode);

/** True when an unused result may be dropped — PURE and READ_STATE. */
bool
RSCache_CS2_EffectIsDeletable(int opcode);

/**
 * Fold a PURE opcode over constant integer arguments.
 *
 * `args` is in the order the operands were pushed, which is the order the IR
 * keeps them in. Returns false when the opcode is not foldable, when the arity
 * is wrong, or when the VM would *fail* on these values — division by zero
 * stops the script in `CS2VM2_Op_Div`, and folding it to a number would replace
 * a halt with an answer.
 *
 * Every case here is transcribed from its handler in `src/cs2vm2/cs2vm2.c`,
 * including the parts that look like bugs: `scale` reads its arguments in an
 * order its name does not suggest, `invpow` is a root rather than a power, and
 * `interpolate` does its multiply in 32 bits while `addpercent` does its in 64.
 * A folder that is merely *correct* would disagree with the client.
 */
bool
RSCache_CS2_EffectFoldInt(int opcode, const int* args, int arg_count, int* out);

#endif
