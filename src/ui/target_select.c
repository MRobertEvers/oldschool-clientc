#include "ui/target_select.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
UITargetSelect_Reset(struct UITargetSelect* sel)
{
    assert(sel);
    memset(sel, 0, sizeof(*sel));
}

void
UITargetSelect_ArmObj(
    struct UITargetSelect* sel,
    int obj_id,
    int slot,
    int component_id,
    char const* name)
{
    assert(sel);
    assert(name);
    assert(!sel->target.active);
    sel->obj.active = 1;
    sel->obj.obj_id = obj_id;
    sel->obj.slot = slot;
    sel->obj.component_id = component_id;
    /* Precision caps the copy at sizeof(name)-1 so this is provably in-bounds
     * regardless of the source's declared size, rather than relying on
     * snprintf's implicit (GCC-unprovable) truncation. */
    snprintf(sel->obj.name, sizeof(sel->obj.name), "%.*s", (int)sizeof(sel->obj.name) - 1, name);
}

void
UITargetSelect_ClearObj(struct UITargetSelect* sel)
{
    assert(sel);
    sel->obj.active = 0;
}

void
UITargetSelect_ArmTarget(
    struct UITargetSelect* sel,
    int component_id)
{
    assert(sel);
    assert(!sel->obj.active);
    sel->target.active = 1;
    sel->target.component_id = component_id;
    sel->target.mask = 0;
    sel->target.op[0] = '\0';
}

void
UITargetSelect_SetTargetMask(
    struct UITargetSelect* sel,
    int mask)
{
    assert(sel);
    assert(sel->target.active);
    sel->target.mask = mask;
}

/* Each %s below is precision-capped so the sum of parts is provably within
 * sizeof(target.op), rather than relying on snprintf's implicit
 * (GCC-unprovable) truncation. */
void
UITargetSelect_SetTargetOpClassic(
    struct UITargetSelect* sel,
    char const* verb,
    char const* base)
{
    char const* space;

    assert(sel);
    assert(verb);
    assert(base);
    assert(sel->target.active);
    /* Classic targetOp = "<verb-prefix> <base> <verb-suffix>", the verb split
     * on its first space (Client-TS TGT_BUTTON). */
    space = strchr(verb, ' ');
    if( space )
    {
        int prefix_len = (int)(space - verb);
        if( prefix_len > 20 )
            prefix_len = 20;
        snprintf(
            sel->target.op,
            sizeof(sel->target.op),
            "%.*s %.*s%.*s",
            prefix_len,
            verb,
            20,
            base,
            20,
            space);
    }
    else
        snprintf(sel->target.op, sizeof(sel->target.op), "%.*s %.*s", 31, verb, 31, base);
}

void
UITargetSelect_SetTargetOpIf3(
    struct UITargetSelect* sel,
    char const* verb,
    char const* op_base)
{
    assert(sel);
    assert(verb);
    assert(op_base);
    assert(sel->target.active);
    /* IF3 (deob method523 + the world rows that read it back): the verb is
     * whole, its base is the component's opBase -- IF3 has no targetText --
     * and the target's own name is joined on with an arrow rather than a verb
     * suffix, so a row reads "Cast <col>Wind Strike</col> -> <col>Goblin</col>".
     *
     * An empty opBase is the verb ALONE, not "verb + a space": a cc_create'd
     * button with no op text would otherwise read "Edit-navigator  -> ..." with
     * the hole still in it. */
    if( op_base[0] == '\0' )
        snprintf(sel->target.op, sizeof(sel->target.op), "%.*s ->", 29, verb);
    else
        snprintf(sel->target.op, sizeof(sel->target.op), "%.*s %.*s ->", 29, verb, 29, op_base);
}

void
UITargetSelect_ClearTarget(struct UITargetSelect* sel)
{
    assert(sel);
    sel->target.active = 0;
}
