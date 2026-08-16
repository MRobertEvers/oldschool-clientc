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
 * **`scope` is not classified.** Phase 6a declares the cache-native `size`
 * field and routes private feature-lane inventories, which is enough for a
 * player-owned Beast-of-Burden container. It intentionally does not invent a
 * server `scope`, `restock`, `stockN`, or stack-policy field: the client cache
 * has none (`dat2_config_inv.c` reads only size (2) and params (249)).
 * `mock230_container_scope` therefore still returns MOCK230_CONTAINER_PLAYER
 * for every inv. A shared shop needs a real server-side definition/parser plus
 * player-qualified listener fan-out and durable world-state policy; the world
 * table remains empty until that separate slice lands.
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
 * One function, so that landing server-side inv scope semantics is a change to
 * one body rather than to every call site. See the header comment for why it is
 * a constant through the private-container foundation.
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

/** Write one slot and mark it. `obj_id` < 0 empties the slot (and its vars). */
void
mock230_container_set(
    struct Mock230Container* container,
    int slot,
    int obj_id,
    int count);

/**
 * Take everything out of one slot — the removal paths' "this cell is now
 * empty", and NOT a synonym for `mock230_container_set(c, slot, -1, 0)`.
 *
 * A shop's baseline slot stays put at a count of 0 (LostCity's `stockobj`), so
 * an out-of-stock line still draws, still prices, and can still restock. Every
 * other slot is cleared outright. See the body for the three separate bugs the
 * raw `obj_id = -1` caused in a shop.
 */
void
mock230_container_clear_slot(
    struct Mock230Container* container,
    int slot);

/**
 * Does `obj_id` occupy one slot in THIS container however many there are?
 *
 * The obj record's own `stackable` OR the container's policy — LostCity's
 * `InvType.stackType`, which for a shop comes out of its `.inv`
 * (`stackall=yes`) and for the bank and collection log is named in
 * mock230_ids.h. `mock230_container_add` has always applied this; the space
 * tests used to read the obj record alone and so disagreed with it about every
 * unstackable in a stacking container.
 */
int
mock230_container_stacks_obj(
    const struct Mock230Container* container,
    int obj_id);

/** `inv_getvar` — 0 when the slot is empty or the key is unset. */
int
mock230_item_get_var(
    const struct Mock230Item* item,
    int key_obj);

/** `inv_setvar` — keyed by obj id; at most MOCK230_ITEM_VAR_MAX keys per slot. */
void
mock230_item_set_var(
    struct Mock230Item* item,
    int key_obj,
    int value);

/**
 * Copy `src`'s vars onto `dst` (obj_id/count untouched). For a caller that
 * knows `src` and `dst` are the same logical item moving between slots —
 * `mock230_container_set` and `mock230_container_add` never do this on their
 * own, deliberately: a different obj landing in a slot must not inherit
 * whatever was there before.
 */
void
mock230_item_vars_copy(
    struct Mock230Item* dst,
    const struct Mock230Item* src);

/**
 * The slot holding `obj_id`'s bank *placeholder*, or -1.
 *
 * A placeholder is the slot an item came out of, remembered — a different obj
 * id (14730 for a bronze sword) sitting in the bank with a count of zero — so a
 * scan for the item itself cannot find it and every "where does this land"
 * decision has to ask this first. The link is the obj record's own, opcode 148.
 *
 * Takes the raw array rather than a container row because the bank's slots are
 * reached both ways here: as an adopted container row (`inv_add(bank, …)`) and
 * as `player->bank.slots` directly (`mock230_bank_deposit`). One rule, one
 * implementation, both callers.
 */
int
mock230_container_placeholder_slot(
    const struct Mock230Item* items,
    int slots,
    int obj_id);

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

/**
 * Same as `mock230_container_add`, but `*out_slot` receives the slot a unit
 * landed in when — and only when — the placement is unambiguous: a single
 * unstackable unit (`count == 1`) that landed in exactly one slot. Every
 * other shape (a merged stack, more than one unit, nothing added) leaves it
 * at -1. `out_slot` may be NULL. See `mock230_item_vars_copy`.
 */
int
mock230_container_add_out_slot(
    struct Mock230Container* container,
    int obj_id,
    int count,
    int assure_full,
    int* out_slot);

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
 * Bind a container to a component and send a full update to that component now.
 *
 * LostCity's `Player.invListenOnCom`: a player may have several listeners, and
 * one inv may appear on more than one component (worn tab + equipment stats).
 * Re-binding the same `(inv, com)` is a no-op; binding a `com` that already
 * listens to a different inv moves it. The bank still owns its own transmit
 * for the reasons in mock230_container.c.
 */
int
mock230_container_bind(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t inv_id,
    int32_t component);

/** Drop the listener that names `component` **for `player`**. Revision 239
 * first sends IF_CLEARINV for the component's embedded item array. The
 * inventory-global UPDATE_INV_STOPTRANSMIT remains the caller's
 * responsibility because another component (or, on a shared row, another
 * player) may still listen to the same inventory. `srv` is needed to reach a
 * shared row's table — a component id alone cannot say whether the container
 * behind it is `player`'s own or a world row several players share. Returns
 * how many dropped (0 or 1; a player can bind one inv per component). */
int
mock230_container_unbind(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t component);

/**
 * Send every listener of every dirty container this tick, and clear dirty.
 *
 * Partial when the row is per-slot, full otherwise — 304 of the cache's 1026
 * invs are past the 32 slots UPDATE_INV_PARTIAL's mask can address, so this is
 * a correctness branch and not an optimisation. A full update is trimmed to the
 * used prefix, because UPDATE_INV_FULL clears everything past the capacity it
 * carries. A row with no listeners still has its dirty dropped so a later bind
 * is not followed by a stale partial on top of the bind's own full update.
 */
void
mock230_container_flush(struct Mock230Player* player);

/**
 * The world-row sibling of `mock230_container_flush`.
 *
 * A player's own containers are flushed from that player's own tick pass
 * (`phase_client_out`); a shared row belongs to no one player, so it needs its
 * own pass over `srv->world_containers`, once per tick, sending each dirty
 * row's listeners to the specific player each listener names (see the
 * `player` field on the listener struct in mock230.h for why that is not
 * `srv->active_player`).
 */
void
mock230_container_flush_world(struct Mock230Server* srv);

#endif
