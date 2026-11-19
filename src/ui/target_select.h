#ifndef SRC_UI_TARGET_SELECT_H
#define SRC_UI_TARGET_SELECT_H

/*
 * What the next click is going to be used ON: the held item armed by a "Use"
 * row ("Use <item> with ..."), or the spell or prayer armed by a target
 * button's "Cast" row ("Cast <spell> on ..."), or neither.
 *
 * The two are mutually exclusive. Arming one requires the other to be clear
 * already, and both are cleared together when a selection ends, so at most
 * one is ever live. A clear drops only the `active` flag: the rest of the
 * record stays as it was armed, and the item's cell and the spell's component
 * are still readable after it.
 *
 * The one thing built here is the spell's prompt text, `target.op`, which the
 * minimenu puts in front of every row while a spell is armed. A classic
 * target button joins its verb's first word, its base and the rest of its
 * verb; an IF3 one joins its verb and op base and ends in an arrow, which the
 * row builder follows with the target's own name.
 *
 * This does not know about a widget tree, a cache, a hook or a packet. The
 * caller finds the item's name and the spell's verb, base and effective target
 * mask, dispatches the spell's target-enter and target-leave hooks around an
 * arm and a clear, resolves the component the server knows the spell by, and
 * decides what a change redraws.
 */

#include <stdbool.h>

/** "Use <item> with ..." (reference objSelected). */
struct UITargetSelectObj
{
    int active;
    int obj_id;
    int slot;
    int component_id;
    char name[40];
};

/** "Cast <spell> on ..." (reference targetMode/targetComId/targetMask/targetOp). */
struct UITargetSelectTarget
{
    int active;
    /** The TREE id the hooks are dispatched against, not the wire id. */
    int component_id;
    int mask;
    char op[64];
};

struct UITargetSelect
{
    struct UITargetSelectObj obj;
    struct UITargetSelectTarget target;
};

/** Nothing armed, every field zero. Also the state of a zeroed struct. */
void
UITargetSelect_Reset(struct UITargetSelect* sel);

/** Either selection is live. */
static inline bool
UITargetSelect_Armed(struct UITargetSelect const* sel)
{
    return sel->obj.active || sel->target.active;
}

/**
 * Arm the held item. The spell must already be clear. `name` is copied and
 * cut to fit; the caller passes its own fallback for an item with no name.
 */
void
UITargetSelect_ArmObj(
    struct UITargetSelect* sel,
    int obj_id,
    int slot,
    int component_id,
    char const* name);

/** Drop the held item. Only `obj.active` changes. */
void
UITargetSelect_ClearObj(struct UITargetSelect* sel);

/**
 * Arm the spell on `component_id` with mask 0 and an empty prompt. The held
 * item must already be clear. The caller sets the mask and the prompt next,
 * once it has read them off the component.
 */
void
UITargetSelect_ArmTarget(
    struct UITargetSelect* sel,
    int component_id);

/** The armed spell's effective target mask. */
void
UITargetSelect_SetTargetMask(
    struct UITargetSelect* sel,
    int mask);

/**
 * The armed spell's prompt for a classic target button: "<verb-prefix> <base>
 * <verb-suffix>", the verb split at its first space, each part cut short so
 * the whole fits.
 */
void
UITargetSelect_SetTargetOpClassic(
    struct UITargetSelect* sel,
    char const* verb,
    char const* base);

/**
 * The armed spell's prompt for an IF3 target button: "<verb> <op_base> ->",
 * or "<verb> ->" when the op base is empty.
 */
void
UITargetSelect_SetTargetOpIf3(
    struct UITargetSelect* sel,
    char const* verb,
    char const* op_base);

/** Drop the spell. Only `target.active` changes. */
void
UITargetSelect_ClearTarget(struct UITargetSelect* sel);

#endif /* SRC_UI_TARGET_SELECT_H */
