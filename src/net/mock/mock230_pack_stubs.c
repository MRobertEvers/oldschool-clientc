/*
 * The world half of the content loader, for a binary that has no world.
 *
 * `mock230_pack` is a validator: it loads the content tree, checks every id
 * against the cache, and exits. It is not a server, and it deliberately does
 * not link one — `mock230_world.c` alone is a quarter of a million lines of
 * game logic whose only role here would be to satisfy a symbol.
 *
 * But `mock230_varbit.c` writes through `mock230_world_set_varp`, because a
 * varbit is a slice of a varp and writing one has to write the other. That call
 * is real at run time and meaningless at validation time: there is no player,
 * `srv` is NULL, and nothing has been asked about a variable's *value*. So the
 * symbol has to exist and must do nothing.
 *
 * It asserts instead of silently returning, which is the tree's rule and the
 * right one here: reaching this from the validator means content load has
 * started evaluating player state, and the validator would then be reporting on
 * a world that does not exist. `srv` is always NULL on this path, so the assert
 * costs nothing and pins the assumption.
 *
 * The three container stubs below are the same shape and the same argument.
 * `mock230_shop.c` is linked for its DEFINITION half — `mock230_shop_def_begin`
 * and friends, which is how an `.inv` block becomes a shop the validator can
 * check — and its runtime half (`mock230_shop_seed`, `mock230_shop_restock_tick`)
 * sits in the same translation unit and reaches for containers. Nothing calls
 * those two here; the linker just wants the symbols to exist.
 *
 * If a second WORLD symbol ever appears here, that is the signal that
 * `mock230_content.c` has grown a dependency on the running game rather than on
 * the tree it is reading, and the fix is there, not another stub. A container
 * one means `mock230_shop.c` wants splitting the way `mock230_bank_tables.c`
 * was — same reasoning, one file further on.
 */

#include "mock230.h"
#include "mock230_container.h"

#include <assert.h>

void
mock230_world_set_varp(
    struct Mock230Server* srv,
    int varp,
    int value)
{
    (void)varp;
    (void)value;
    /* No server, so no varp to write. A non-NULL `srv` here would mean the
     * validator had somehow been handed a live world — which is the case worth
     * stopping on, not the one worth tolerating. */
    assert(!srv);
}

struct Mock230Container*
mock230_container_resolve(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t inv_id)
{
    (void)srv;
    (void)player;
    (void)inv_id;
    /* Unreachable: only mock230_shop_seed / _restock_tick call this, and the
     * validator seeds no shop and runs no tick. */
    assert(0 && "mock230_pack does not have containers");
    return NULL;
}

void
mock230_container_set(
    struct Mock230Container* container,
    int slot,
    int obj_id,
    int count)
{
    (void)container;
    (void)slot;
    (void)obj_id;
    (void)count;
    assert(0 && "mock230_pack does not have containers");
}

void
mock230_container_clean(struct Mock230Container* container)
{
    (void)container;
    assert(0 && "mock230_pack does not have containers");
}
