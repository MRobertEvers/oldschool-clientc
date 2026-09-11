/*
 * The world half of the content loader, for a binary that has no world.
 *
 * `ToriRSServer_Pack` is a validator: it loads the content tree, checks every id
 * against the cache, and exits. It is not a server, and it deliberately does
 * not link one — `torirs_server_world.c` alone is a quarter of a million lines of
 * game logic whose only role here would be to satisfy a symbol.
 *
 * But `torirs_server_varbit.c` writes through `ToriRSServer_WorldSetVarp`, because a
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
 * `torirs_server_shop.c` is linked for its DEFINITION half — `ToriRSServer_ShopDefBegin`
 * and friends, which is how an `.inv` block becomes a shop the validator can
 * check — and its runtime half (`ToriRSServer_ShopSeed`, `ToriRSServer_ShopRestockTick`)
 * sits in the same translation unit and reaches for containers. Nothing calls
 * those two here; the linker just wants the symbols to exist.
 *
 * If a second WORLD symbol ever appears here, that is the signal that
 * `torirs_server_content.c` has grown a dependency on the running game rather than on
 * the tree it is reading, and the fix is there, not another stub. A container
 * one means `torirs_server_shop.c` wants splitting the way `torirs_server_bank_tables.c`
 * was — same reasoning, one file further on.
 */

#include "torirs_server.h"
#include "torirs_server_container.h"

#include <assert.h>

void
ToriRSServer_WorldSetVarp(
    struct ToriRSServer* srv,
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

void
ToriRSServer_WorldSetVarpOn(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int varp,
    int value)
{
    (void)player;
    (void)varp;
    (void)value;
    /* The named-player sibling of `ToriRSServer_WorldSetVarp`, and reached the same
     * way: `ToriRSServer_VarbitSet` writes the carrier varp after patching the bit
     * range. Same argument, same assert — a non-NULL `srv` means the validator
     * was handed a live world. */
    assert(!srv);
}

struct ToriRSServerContainer*
ToriRSServer_ContainerResolve(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int32_t inv_id)
{
    (void)srv;
    (void)player;
    (void)inv_id;
    /* Unreachable: only ToriRSServer_ShopSeed / _restock_tick call this, and the
     * validator seeds no shop and runs no tick. */
    assert(0 && "ToriRSServer_Pack does not have containers");
    return NULL;
}

void
ToriRSServer_ContainerSet(
    struct ToriRSServerContainer* container,
    int slot,
    int obj_id,
    int count)
{
    (void)container;
    (void)slot;
    (void)obj_id;
    (void)count;
    assert(0 && "ToriRSServer_Pack does not have containers");
}

void
ToriRSServer_ContainerClean(struct ToriRSServerContainer* container)
{
    (void)container;
    assert(0 && "ToriRSServer_Pack does not have containers");
}
