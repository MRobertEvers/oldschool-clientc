/*
 * A loc placement's own right-click menu — LOC_ADD_CHANGE_V2's opFlags and op
 * overrides, as pure value operations.
 *
 * Its own file rather than a corner of `mock230_scene.c` for the same reason
 * that file already gives for keeping content out of itself: these three are
 * needed by the *wire* (the encoder fills an event's menu), by the *scene* (a
 * placement carries one) and by the *zone* (a record replays one), and only the
 * scene half needs a cache, a collision map and a map square behind it. Linking
 * all of that into a test of the packet layout would make the packet's own
 * round trip depend on the world building correctly.
 *
 * Nothing here reads a loctype. The type's label is passed in, because the
 * order it is applied in is the whole subtlety and belongs beside the mask.
 */

#include "mock230_scene.h"

#include <assert.h>
#include <string.h>

void
mock230_loc_ops_default(struct Mock230LocOps* ops)
{
    assert(ops);
    memset(ops, 0, sizeof(*ops));
    ops->flags = MOCK230_LOC_OPS_ALL_SHOWN;
}

int
mock230_loc_ops_is_default(const struct Mock230LocOps* ops)
{
    assert(ops);
    if( ops->flags != MOCK230_LOC_OPS_ALL_SHOWN )
        return 0;
    for( int i = 0; i < 5; i++ )
    {
        if( ops->name[i][0] != '\0' )
            return 0;
    }
    return 1;
}

const char*
mock230_loc_ops_label(
    const struct Mock230LocOps* ops,
    int op_num,
    const char* type_op)
{
    assert(ops);
    assert(op_num >= 1);
    assert(op_num <= 5);

    /* Step 1 — the shown mask, and it comes first because it is a veto: the
     * client `continue`s past a masked-out slot before it has looked at either
     * label (class108, deob src_osrs239_rl1_12_33). A placement that hides op1
     * hides it whether or not it also renames it. */
    if( (ops->flags & (1 << (op_num - 1))) == 0 )
        return NULL;
    /* Steps 2-3 — the placement's label wins over the loctype's, and it wins
     * on a slot the loctype left empty too. That asymmetry is the whole
     * mechanism: it is how one cache record offers "Open" where the map put it
     * and "Close" where it swung to. */
    if( ops->name[op_num - 1][0] != '\0' )
        return ops->name[op_num - 1];
    if( type_op && type_op[0] != '\0' )
        return type_op;
    return NULL;
}

