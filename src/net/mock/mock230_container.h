#ifndef SRC_NET_MOCK_MOCK230_CONTAINER_H
#define SRC_NET_MOCK_MOCK230_CONTAINER_H

/*
 * The container registry.
 *
 * What it replaces: `container_for` in mock230_scripts.c, which was three
 * `if( inv_id == mock230_ids()->… )` branches over three different storage
 * shapes, every one of them resolved off `srv->active_player`. That shape was
 * wrong in three measurable ways and all three are gone here:
 *
 *   1. It could only answer for three of the cache's 1026 invs, so
 *      `inv_size(anything_else)` was 0 and every other container op was a
 *      silent no-op on a container that in fact exists.
 *   2. Its dirty half (`container_dirty`) was a *second* three-way branch, and
 *      two callers hand-rolled a "backpack, else worn" version instead — so a
 *      write to the bank marked a worn slot, and a bank slot past 31 shifted a
 *      32-bit mask by more than its width (undefined; `i` reaches 1409).
 *   3. `active_player` is not a thing a `scope=shared` container has. Resolving
 *      through it means world-owned containers cannot be expressed at all, and
 *      no amount of adding cases fixes that.
 *
 * Where the reference puts it: `Player.getInventory(inv)`
 * (`engine/src/engine/entity/Player.ts:1463`) is the whole seam — it branches
 * on `invType.scope === SCOPE_SHARED` to `World.getInventory(inv)`, and
 * otherwise looks the container up in the player's own map, **creating it from
 * the type on first use**. This is a port of that function, not an invention:
 * resolve-or-create is why content can name any inv without the engine having
 * heard of it, and why no inv id needs to enter C.
 *
 * ---------------------------------------------------------------------------
 * Two things this deliberately does NOT do
 * ---------------------------------------------------------------------------
 *
 * **No size constant enters C.** A row's slot count is
 * `mock230_bank_inv_size(inv_id)`, decoded from the cache's config group 5 at
 * boot for every inv id. An inv the cache does not size cannot be registered,
 * which is the correct answer: it is not a container, it is a typo.
 *
 * **`scope` is not classified.** `mock230_container_scope` returns
 * MOCK230_CONTAINER_PLAYER for everything, because the field it needs is
 * decoded from LostCity's server-side `inv.dat` (opcode 1) and does not exist
 * in the client cache at all — `3rd/rscache/src/datatypes/dat2_config_inv.c`
 * reads only size (2) and params (249). This tree has no `fields/inv.ini` and
 * no `[namespace:inv]` in `content.ini` for `scope=`/`restock=`/`stockN=` to
 * live in. Until that lands, the world table is empty by construction. That is
 * the concrete reason `shop` is still blocked and is stated here rather than
 * discovered later.
 */

#include <stdint.h>

struct Mock230Server;
struct Mock230Player;
struct Mock230Container;
struct Mock230Item;

enum
{
    MOCK230_CONTAINER_PLAYER = 0,
    MOCK230_CONTAINER_WORLD = 1,
};

/* ------------------------------------------------------------------ */
/* Resolve                                                             */
/* ------------------------------------------------------------------ */

/**
 * Which table owns `inv_id`.
 *
 * One function, so that landing `fields/inv.ini` is a change to one body rather
 * than to every call site. See the header comment for why it is a constant now.
 */
int
mock230_container_scope(int32_t inv_id);

/**
 * The container `inv_id` names, creating it on first use.
 *
 * `player` is who is asking; it is *unused* for a world-scoped container, which
 * is the point of the signature. NULL is returned only when the inv id is not
 * one the cache sizes, or when the owner's table is full — both of which are
 * bugs at the call site rather than states to absorb, so every ServerScript
 * container op aborts on NULL rather than returning a plausible zero.
 *
 * The returned pointer is stable across a script yield: rows live in the player
 * (or server) struct, which is a fixed array element, and `items` is a heap
 * block owned by the row until shutdown. `p_countdialog` parks between an
 * `inv_getobj` and the write that follows it, so this is load-bearing.
 */
struct Mock230Container*
mock230_container_resolve(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t inv_id);

/**
 * Register a container over storage the registry does not own.
 *
 * For the three containers that predate it: the backpack and worn set are fixed
 * arrays inside `struct Mock230Player`, the bank is mock230_bank's own calloc,
 * and their dirty flags are read by code outside the registry (the appearance
 * mask, `mock230_bank_flush`, two selftests). Passing those in keeps one dirty
 * flag per container rather than two that can disagree.
 *
 * Exactly one of `slot_dirty_ref` / `dirty_ref` may be non-NULL, and it must
 * agree with the slot count: a per-slot mask cannot address more than 32.
 */
struct Mock230Container*
mock230_container_adopt(
    struct Mock230Player* player,
    int32_t inv_id,
    struct Mock230Item* items,
    int slots,
    uint32_t* slot_dirty_ref,
    int* dirty_ref,
    int appearance);

/** Drop one row, without freeing storage the registry does not own. Called by
 *  mock230_bank_shutdown_player, which frees the bank's array itself. */
void
mock230_container_forget(
    struct Mock230Player* player,
    int32_t inv_id);

/** Release every row this player owns storage for. Must run before the player
 *  struct is memset, the same contract mock230_bank_shutdown_player has. */
void
mock230_container_shutdown_player(struct Mock230Player* player);

/** Every player's, plus the world table. For a host tearing the world down. */
void
mock230_container_shutdown(struct Mock230Server* srv);

/* ------------------------------------------------------------------ */
/* Mutation — the registry owns `dirty`                                */
/* ------------------------------------------------------------------ */

/**
 * Mark one slot changed.
 *
 * Callers do not touch dirty state directly, and that is the fix for defect 2
 * above rather than a style preference: the two `inv_del` paths that wrote
 * `items[i]` and then hand-rolled the flag are exactly how the bank came to
 * mark worn slots. LostCity has the same rule — `Inventory.update` is set by
 * every mutating method on the class and cleared once per tick in
 * `World.processCleanup()`, never by a caller.
 */
void
mock230_container_mark(
    struct Mock230Container* container,
    int slot);

/** Mark the whole container changed. */
void
mock230_container_mark_all(struct Mock230Container* container);

/** Write one slot and mark it. `obj_id` < 0 empties the slot. */
void
mock230_container_set(
    struct Mock230Container* container,
    int slot,
    int obj_id,
    int count);

/**
 * Put `count` of `obj_id` in, the way `Inventory.add` does, and say how many
 * landed.
 *
 * A stackable obj merges onto the stack already held (taking a free slot only
 * when there is none); an unstackable one takes one slot per unit.
 * `assure_full` is the reference's `assureFullInsertion` — on, it is all or
 * nothing.
 *
 * One function because there was one before it and it gave two half-answers:
 * the `inv_add` opcode wrote the first free slot and never merged, so a
 * stackable obj added twice took two slots, and an unstackable one added five
 * at a time took one. See the body for what is deliberately not ported.
 */
int
mock230_container_add(
    struct Mock230Container* container,
    int obj_id,
    int count,
    int assure_full);

/** Clear the dirty state after a flush. */
void
mock230_container_clean(struct Mock230Container* container);

/** True when anything in this container changed since the last clean. */
int
mock230_container_is_dirty(const struct Mock230Container* container);

/** The per-slot mask, for the encoder. 0 when the row is whole-container. */
uint32_t
mock230_container_slot_mask(const struct Mock230Container* container);

/* ------------------------------------------------------------------ */
/* Bindings — `inv_transmit` / `inv_stoptransmit`                      */
/* ------------------------------------------------------------------ */

/**
 * Bind a container to a component and send it now.
 *
 * The reference keeps a list of `{type, com, source}` per client and sends a
 * full update when one is added (`Player.invListenOnCom`). A row holds one
 * binding rather than a list because one container painting into two components
 * at once has no consumer here yet — and the bank, which is the nearest thing,
 * still owns its own transmit for the reasons in mock230_container.c.
 */
int
mock230_container_bind(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t inv_id,
    int32_t component);

/** Drop whatever binding names `component`. Returns 1 when one did. */
int
mock230_container_unbind(
    struct Mock230Player* player,
    int32_t component);

/**
 * Send every bound container that changed this tick, and clear the rest.
 *
 * Partial when the row is per-slot, full otherwise — 304 of the cache's 1026
 * invs are past the 32 slots UPDATE_INV_PARTIAL's mask can address, so this is
 * a correctness branch and not an optimisation. A full update is trimmed to the
 * used prefix, because UPDATE_INV_FULL clears everything past the capacity it
 * carries.
 */
void
mock230_container_flush(struct Mock230Player* player);

#endif
