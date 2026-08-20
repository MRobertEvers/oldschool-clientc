/*
 * The container registry. See torirs_server_container.h.
 */

#include "torirs_server_container.h"

#include "torirs_server.h"
#include "torirs_server_bank.h"
#include "torirs_server_shop.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Per-slot vars                                                       */
/* ------------------------------------------------------------------ */

static void
item_clear_vars(struct ToriRSServerItem* item)
{
    int i;

    assert(item);
    for( i = 0; i < TORIRSSERVER_ITEM_VAR_MAX; i++ )
    {
        item->var_key[i] = -1;
        item->var_val[i] = 0;
    }
}

int
ToriRSServer_ItemGetVar(
    const struct ToriRSServerItem* item,
    int key_obj)
{
    int i;

    assert(item);
    if( item->obj_id < 0 )
        return 0;
    for( i = 0; i < TORIRSSERVER_ITEM_VAR_MAX; i++ )
    {
        if( item->var_key[i] == key_obj )
            return item->var_val[i];
    }
    return 0;
}

void
ToriRSServer_ItemSetVar(
    struct ToriRSServerItem* item,
    int key_obj,
    int value)
{
    int i;
    int free_i = -1;

    assert(item);
    if( item->obj_id < 0 )
        return;
    for( i = 0; i < TORIRSSERVER_ITEM_VAR_MAX; i++ )
    {
        if( item->var_key[i] == key_obj )
        {
            item->var_val[i] = value;
            return;
        }
        if( free_i < 0 && item->var_key[i] < 0 )
            free_i = i;
    }
    assert(free_i >= 0 && "ToriRSServerItem var table full");
    item->var_key[free_i] = key_obj;
    item->var_val[free_i] = value;
}

/*
 * `ToriRSServer_ContainerSet` and `ToriRSServer_ContainerAdd` never carry a slot's
 * vars to another slot — the former clears them on any obj_id change (that is
 * correct: a *different* obj landing in a slot must not inherit the old
 * occupant's charges), and the latter never writes them at all, so a
 * dropped-and-re-added charged item, an unequipped one, or a banked one lost
 * its charge count. This is the primitive that lets a caller carry a single
 * unstackable unit's vars across such a move deliberately, once it knows the
 * source and destination genuinely are the same logical item (see
 * torirs_server_ops_inv.c's `INV_MOVEFROMSLOT` / `INV_MOVEITEM` arms and
 * torirs_server_bank.c's deposit/withdraw, which are the two shapes that need it).
 */
void
ToriRSServer_ItemVarsCopy(
    struct ToriRSServerItem* dst,
    const struct ToriRSServerItem* src)
{
    int i;

    assert(dst);
    assert(src);
    for( i = 0; i < TORIRSSERVER_ITEM_VAR_MAX; i++ )
    {
        dst->var_key[i] = src->var_key[i];
        dst->var_val[i] = src->var_val[i];
    }
}

/* ------------------------------------------------------------------ */
/* Scope                                                               */
/* ------------------------------------------------------------------ */

int
ToriRSServer_ContainerScope(int32_t inv_id)
{
    /*
     * Every inv defaults to per-player. The one exception is a shop: content
     * declares `scope=shared` in a `.inv` file (torirs_server_content.c's
     * load_inv_config, docs/SHOPS_PLAN.md §3.1), which registers a
     * ToriRSServerShopDef with `shared` set. An inv nothing declared a shop
     * definition for — the overwhelming majority of the namespace, including
     * the player's own backpack/worn/bank — is not asked and stays PLAYER.
     */
    if( ToriRSServer_ShopIsShared(inv_id) )
        return TORIRSSERVER_CONTAINER_WORLD;
    return TORIRSSERVER_CONTAINER_PLAYER;
}

/* ------------------------------------------------------------------ */
/* Table access                                                        */
/* ------------------------------------------------------------------ */

static struct ToriRSServerContainer*
table_for(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int scope,
    int* out_count)
{
    if( scope == TORIRSSERVER_CONTAINER_WORLD )
    {
        *out_count = TORIRSSERVER_WORLD_CONTAINER_MAX;
        return srv ? srv->world_containers : NULL;
    }
    *out_count = TORIRSSERVER_CONTAINER_MAX;
    return player ? player->containers : NULL;
}

static struct ToriRSServerContainer*
find_row(
    struct ToriRSServerContainer* table,
    int count,
    int32_t inv_id)
{
    for( int i = 0; i < count; i++ )
        if( table[i].used && table[i].inv_id == inv_id )
            return &table[i];
    return NULL;
}

static struct ToriRSServerContainer*
free_row(
    struct ToriRSServerContainer* table,
    int count)
{
    for( int i = 0; i < count; i++ )
        if( !table[i].used )
            return &table[i];
    return NULL;
}

static void
row_init(
    struct ToriRSServerContainer* row,
    int32_t inv_id,
    int slots,
    int scope,
    struct ToriRSServerPlayer* owner)
{
    memset(row, 0, sizeof(*row));
    row->used = 1;
    row->owner_kind = (uint8_t)scope;
    row->inv_id = inv_id;
    row->slots = slots;
    row->owner = owner;
    row->listener_count = 0;
    /*
     * The transmit shape is a function of the size and nothing else.
     * UPDATE_INV_PARTIAL indexes its slots out of a 32-bit mask
     * (torirs_server_encode.c: `dirty & (1u << i)`), so anything larger has to be
     * re-sent whole. The backpack (28) and worn set (14) land on the per-slot
     * side by measurement rather than by being named here.
     */
    row->per_slot = slots > 0 && slots <= 32;
}

/* ------------------------------------------------------------------ */
/* Resolve                                                             */
/* ------------------------------------------------------------------ */

struct ToriRSServerContainer*
ToriRSServer_ContainerResolve(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int32_t inv_id)
{
    int scope;
    int count = 0;
    struct ToriRSServerContainer* table;
    struct ToriRSServerContainer* row;
    int slots;

    if( inv_id < 0 )
        return NULL;

    scope = ToriRSServer_ContainerScope(inv_id);
    table = table_for(srv, player, scope, &count);
    if( !table )
        return NULL;

    row = find_row(table, count, inv_id);
    if( row )
        return row;

    /* Create on first use, the way `Player.getInventory` does. The size comes
     * from the cache; an inv it does not size falls back to a shop's own
     * declared `size=` (docs/SHOPS_PLAN.md §8.5 — a `pack/inv.alloc` id for a
     * region this cache snapshot never packed a real inv for). Still not a
     * container if neither names one. */
    slots = ToriRSServer_BankInvSize((int)inv_id);
    if( slots <= 0 )
        slots = ToriRSServer_ShopContentSize(inv_id);
    if( slots <= 0 )
        return NULL;

    row = free_row(table, count);
    if( !row )
    {
        fprintf(stderr,
                "torirsserver: container table full (%d rows); inv %d cannot be registered\n",
                count, (int)inv_id);
        return NULL;
    }

    row_init(row, inv_id, slots, scope, scope == TORIRSSERVER_CONTAINER_WORLD ? NULL : player);
    row->items = calloc((size_t)slots, sizeof(*row->items));
    assert(row->items);
    row->owns_items = 1;
    for( int i = 0; i < slots; i++ )
    {
        row->items[i].obj_id = -1;
        row->items[i].count = 0;
        item_clear_vars(&row->items[i]);
    }
    return row;
}

struct ToriRSServerContainer*
ToriRSServer_ContainerAdopt(
    struct ToriRSServerPlayer* player,
    int32_t inv_id,
    struct ToriRSServerItem* items,
    int slots,
    uint32_t* slot_dirty_ref,
    int* dirty_ref,
    int appearance)
{
    struct ToriRSServerContainer* row;

    if( inv_id < 0 || slots <= 0 )
        return NULL;
    assert(player);
    assert(items);

    row = find_row(player->containers, TORIRSSERVER_CONTAINER_MAX, inv_id);
    if( row && row->owns_items )
    {
        free(row->items);
        row->items = NULL;
        row->owns_items = 0;
    }
    if( !row )
        row = free_row(player->containers, TORIRSSERVER_CONTAINER_MAX);
    if( !row )
    {
        fprintf(stderr, "torirsserver: container table full; inv %d cannot be adopted\n",
                (int)inv_id);
        return NULL;
    }

    row_init(row, inv_id, slots, TORIRSSERVER_CONTAINER_PLAYER, player);
    row->items = items;
    row->owns_items = 0;
    row->appearance = (uint8_t)(appearance ? 1 : 0);
    row->slot_dirty_ref = slot_dirty_ref;
    row->dirty_ref = dirty_ref;
    /* The caller's choice of flag has to agree with what the size allows. A
     * per-slot mask over a 1410-slot bank is the shift-past-the-width bug this
     * registry exists to remove, so it is refused here rather than shifted. */
    if( slot_dirty_ref && !row->per_slot )
    {
        fprintf(stderr,
                "torirsserver: inv %d has %d slots and cannot carry a per-slot dirty mask\n",
                (int)inv_id, slots);
        row->slot_dirty_ref = NULL;
        row->per_slot = 0;
    }
    if( dirty_ref )
        row->per_slot = 0;
    return row;
}

void
ToriRSServer_ContainerForget(
    struct ToriRSServerPlayer* player,
    int32_t inv_id)
{
    struct ToriRSServerContainer* row;

    assert(player);
    row = find_row(player->containers, TORIRSSERVER_CONTAINER_MAX, inv_id);
    if( !row )
        return;
    /* A private row may be painted by a different player. Retire every
     * embedded item array before releasing the storage, so an owner logout or
     * row replacement cannot leave a guest looking at a stale collection. Do
     * not send the inventory-global STOPTRANSMIT packet here: that viewer may
     * also be listening to their own row with the same inv id. */
    for( int i = 0; i < row->listener_count; i++ )
        if( row->listeners[i].player )
            ToriRSServer_SendIfClearinv(row->listeners[i].player,
                                     row->listeners[i].component);
    if( row->owns_items )
        free(row->items);
    memset(row, 0, sizeof(*row));
}

void
ToriRSServer_ContainerShutdownPlayer(struct ToriRSServerPlayer* player)
{
    assert(player);
    for( int i = 0; i < TORIRSSERVER_CONTAINER_MAX; i++ )
    {
        struct ToriRSServerContainer* row = &player->containers[i];

        for( int l = 0; l < row->listener_count; l++ )
            if( row->listeners[l].player )
                ToriRSServer_SendIfClearinv(row->listeners[l].player,
                                         row->listeners[l].component);
        if( row->owns_items )
            free(row->items);
        memset(row, 0, sizeof(*row));
    }
}

void
ToriRSServer_ContainerShutdown(struct ToriRSServer* srv)
{
    assert(srv);
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
        ToriRSServer_ContainerShutdownPlayer(&srv->players[i]);
    for( int i = 0; i < TORIRSSERVER_WORLD_CONTAINER_MAX; i++ )
    {
        struct ToriRSServerContainer* row = &srv->world_containers[i];

        if( row->owns_items )
            free(row->items);
        memset(row, 0, sizeof(*row));
    }
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

/* No self-referential pointer is ever stored: a row that owns its dirty state
 * is asked for the address of its own field here, at the point of use, so a
 * memset or a struct copy cannot leave a row pointing at a stale twin. */
static uint32_t*
slot_mask_of(struct ToriRSServerContainer* row)
{
    return row->slot_dirty_ref ? row->slot_dirty_ref : &row->slot_dirty_own;
}

static int*
dirty_of(struct ToriRSServerContainer* row)
{
    return row->dirty_ref ? row->dirty_ref : &row->dirty_own;
}

void
ToriRSServer_ContainerMark(
    struct ToriRSServerContainer* container,
    int slot)
{
    assert(container);
    if( !container->used )
        return;
    if( container->per_slot )
    {
        if( slot < 0 || slot >= container->slots )
            return;
        *slot_mask_of(container) |= 1u << slot;
    }
    else
    {
        *dirty_of(container) = 1;
    }
    if( container->appearance && container->owner )
        container->owner->masks |= TORIRSSERVER_PMASK_APPEARANCE;
}

void
ToriRSServer_ContainerMarkAll(struct ToriRSServerContainer* container)
{
    assert(container);
    if( !container->used )
        return;
    if( container->per_slot )
    {
        uint32_t all = container->slots >= 32 ? 0xffffffffu
                                              : (uint32_t)((1u << container->slots) - 1u);

        *slot_mask_of(container) |= all;
    }
    else
    {
        *dirty_of(container) = 1;
    }
    if( container->appearance && container->owner )
        container->owner->masks |= TORIRSSERVER_PMASK_APPEARANCE;
}

/*
 * May this cell hold an obj at a count of zero?
 *
 * Two things do, and the encoder writes the pair independently for exactly this
 * reason (torirs_server_encode.c: "`obj_id >= 0` is the occupancy test, not
 * `count > 0`"):
 *
 *   - A **bank placeholder**. An obj that carries a placeholder *template* is
 *     one (torirs_server.h's table: 14730 has template 14401 and stands for 1277),
 *     and it exists only to hold a slot at zero. Content creates it through
 *     `inv_setslot(bank, $slot, $placeholder, 0)` — `~bank_leave_placeholder`,
 *     interface_bank/scripts/bank_placeholder.rs2:77.
 *   - A **shop's baseline line**, kept in place at zero rather than deleted so
 *     it still draws, still prices and can still restock. See
 *     ToriRSServer_ContainerClearSlot for the three bugs deleting it caused, and
 *     torirs_server_shop.c's restock walk, which reaches zero by subtraction.
 *
 * Everything else at zero is a wedge. `obj_id >= 0` is the occupancy test in
 * every free-slot scan the server has — `ToriRSServer_ContainerAdd`'s,
 * `inv_freespace`, `inv_itemspace`, torirs_server_bank.c's `inv_free_slots` — so such
 * a cell counts as full while holding nothing, and for an unstackable it never
 * heals: the add loop only writes cells whose obj is negative. A backpack
 * collecting these starts answering "did not fit" over cells the player can see
 * are empty.
 *
 * Both predicates are about the container and the obj, not about the caller,
 * which is why the rule lives here rather than at the two opcodes that let
 * content name a count (`inv_setslot`, `inv_changeslot`).
 */
static int
zero_count_is_meaningful(
    const struct ToriRSServerContainer* container,
    int obj_id)
{
    assert(container);
    if( obj_id < 0 )
        return 0;
    if( ToriRSServer_ObjInfo(obj_id)->placeholder_template >= 0 )
        return 1;
    return ToriRSServer_ShopHasStockLine(container->inv_id, obj_id) != 0;
}

void
ToriRSServer_ContainerSet(
    struct ToriRSServerContainer* container,
    int slot,
    int obj_id,
    int count)
{
    struct ToriRSServerItem* item;

    assert(container);
    if( !container->used || !container->items )
        return;
    if( slot < 0 || slot >= container->slots )
        return;
    item = &container->items[slot];
    if( obj_id < 0 || (count <= 0 && !zero_count_is_meaningful(container, obj_id)) )
    {
        item->obj_id = -1;
        item->count = 0;
        item_clear_vars(item);
    }
    else
    {
        if( item->obj_id != obj_id )
            item_clear_vars(item);
        item->obj_id = obj_id;
        item->count = count;
    }
    ToriRSServer_ContainerMark(container, slot);
}

/*
 * `Inventory.add` — the one place in this server that decides how an obj lands
 * in a container.
 *
 * It is here rather than in either caller because there were two callers and
 * one of them was wrong: `SS_OP_INV_ADD` wrote the first slot whose `obj_id` is
 * negative and never merged, so a second `inv_add(inv, coins, …)` opened a
 * *second* coin slot — reachable today from `~pickpocket`, which adds coins on
 * every success. The reference has one method and every caller goes through it.
 *
 * Ported from `engine/src/engine/Inventory.ts:158`, minus `stockobj` (shops)
 * and `beginSlot`. Stackability is read from the obj record here rather than
 * passed in, so the callers cannot come to disagree about it.
 *
 * `assure_full` is the reference's `assureFullInsertion`: on, nothing is added
 * unless all of it fits — which is what `obj_takeitem` needs, since a partially
 * taken pile that is then deleted destroys the rest. Off, it adds what fits and
 * says how much, which is what `inv_add` needs.
 *
 * ------------------------------------------------------------------
 * Stack policy: one slot per unit, and the two containers that override it
 * ------------------------------------------------------------------
 *
 * `Inventory.add` puts an unstackable obj in **one slot per unit**, and this
 * does too. It used to put all `count` in one slot pending `InvType.stackType`,
 * and that shortcut is what a player saw on death: `[proc,moveallinv]` moves an
 * obj by `inv_total`, so three separately-kept sharks left `deathkeep` as one
 * `inv_moveitem(deathkeep, inv, shark, 3)` and came back as a single backpack
 * cell reading "3". The same route runs the gravestone (`inv` -> `gravestone`
 * -> `inv`), so every unstackable a death touched arrived home stacked.
 *
 * The input that was missing is still missing — LostCity reads `stackType`
 * (`ALWAYS_STACK` for a bank, `NEVER_STACK` for the shops' sale invs) from its
 * server-side `inv.dat`, and this cache's inv config carries only size — so the
 * containers that genuinely always stack are named here instead, in
 * `always_stacks()` below (the bank and the collection log; see
 * torirs_server_ids.h for why each). That is a smaller and more honest gap than the
 * old one: those two are the whole of what this tree adds an unstackable pile
 * to in bulk (`[proc,newplayer_bank]` says `inv_add(bank, logs, 100)`), and
 * everything else now behaves the way `~pickup_obj_check_for_space` already
 * assumed it did when it asked `inv_itemspace` for one free slot per unit.
 *
 * A consequence worth stating: with `assure_full` set, a pile of three
 * unstackables over two free slots is now refused rather than crammed into one
 * cell. That is `obj_takeitem` agreeing with the content guard in front of it.
 *
 * Returns the number added: never negative, never more than `count`.
 */
/*
 * The slot holding this obj's *placeholder*, or -1.
 *
 * A bank placeholder is the slot an item came out of, remembered — so the item
 * coming back has to land in that slot rather than beside it, or the feature
 * does the opposite of what it is for. The link is the obj record's own
 * (opcode 148, `ToriRSServerObjInfo.placeholder_id`), read forward: an *item* states
 * its placeholder and no template.
 *
 * Not gated on the container being a bank, and it needs no gate: a placeholder
 * obj only ever exists in one, so the scan cannot match anywhere else.
 */
int
ToriRSServer_ContainerPlaceholderSlot(
    const struct ToriRSServerItem* items,
    int slots,
    int obj_id)
{
    const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(obj_id);
    int placeholder;

    if( slots <= 0 )
        return -1;
    assert(items);
    if( info->placeholder_template >= 0 || info->placeholder_id < 0 )
        return -1;
    placeholder = info->placeholder_id;
    for( int i = 0; i < slots; i++ )
        if( items[i].obj_id == placeholder )
            return i;
    return -1;
}

/*
 * Does this container stack everything, whatever the obj record says?
 *
 * LostCity's `InvType.stackType == ALWAYS_STACK`, for the invs this tree can
 * answer without a server-side inv definition — the two are named and explained
 * in torirs_server_ids.h. Every other inv follows the obj record. Kept as one function
 * so the day a real stack-policy field lands, this is its only caller-visible
 * seam; an unresolved id is -1 and matches no container.
 */
static int
always_stacks(const struct ToriRSServerContainer* container)
{
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    /* A shop says so in its own `.inv` (`stackall=yes`), which is the real
     * `stackType` field rather than a name this file had to invent. The two
     * hardcoded ids below are the containers that have no `.inv` to say it in. */
    if( ToriRSServer_ShopStackall(container->inv_id) )
        return 1;
    return container->inv_id == ids->inv_bank ||
           container->inv_id == ids->inv_collection_log;
}

int
ToriRSServer_ContainerStacksObj(
    const struct ToriRSServerContainer* container,
    int obj_id)
{
    if( obj_id < 0 )
        return 0;
    if( ToriRSServer_ObjInfo(obj_id)->stackable )
        return 1;
    return container && always_stacks(container);
}

/*
 * Empty one slot — and a shop's baseline slot empties to ZERO, not to nothing.
 *
 * This is LostCity's `stockobj`, the one thing `ToriRSServer_ContainerAdd`'s header
 * says was left out of the `Inventory` port. Buying a store's last pot used to
 * run `items[slot].obj_id = -1`, and three things followed from that one line:
 *
 *   1. The cell vanished from the shop. `shop_main_update` (clientscript 1076)
 *      does `cc_sethide(true)` on a slot whose `inv_getobj` is null, so an
 *      out-of-stock line is not drawn greyed at 0 — it is not drawn at all.
 *   2. It never came back. `ToriRSServer_ShopRestockTick` skips `obj_id < 0`
 *      because it reads the baseline off the obj that is *in* the slot, so the
 *      slot it needed to refill no longer named an obj. A bought-out line was
 *      gone until the next server boot re-seeded it.
 *   3. Its price was lost with it. `~price_mod` scales off `inv_stockbase`
 *      against current stock, so the item could not be valued or re-sold at the
 *      right price even though the shop still nominally traded it.
 *
 * Only baseline lines survive at zero. A non-baseline slot — what a player sold
 * into an `allstock=yes` general store — is genuinely gone when it hits zero,
 * which is what `allstock`'s own one-a-minute decay is for.
 */
void
ToriRSServer_ContainerClearSlot(
    struct ToriRSServerContainer* container,
    int slot)
{
    int obj_id;

    assert(container);
    if( !container->used || !container->items )
        return;
    if( slot < 0 || slot >= container->slots )
        return;
    obj_id = container->items[slot].obj_id;
    if( obj_id >= 0 && ToriRSServer_ShopHasStockLine(container->inv_id, obj_id) )
    {
        ToriRSServer_ContainerSet(container, slot, obj_id, 0);
        return;
    }
    ToriRSServer_ContainerSet(container, slot, -1, 0);
}

/*
 * The real body, with an optional `out_slot` for callers that need to know
 * exactly where a unit landed — see ToriRSServer_ItemVarsCopy's comment.
 * `*out_slot` is only ever set when the placement is unambiguous: a single
 * unstackable unit (count == 1 at entry) that landed in exactly one slot.
 * Every other shape (a merged stack, more than one unit, nothing added)
 * leaves it at -1, because "which slot" has no one answer there.
 */
static int
ToriRSServer_ContainerAddEx(
    struct ToriRSServerContainer* container,
    int obj_id,
    int count,
    int assure_full,
    int* out_slot)
{
    int stackable;
    int free_slots = 0;
    int added = 0;
    int placeholder_slot;
    int requested = count;

    if( out_slot )
        *out_slot = -1;
    assert(container);
    if( !container->used || !container->items )
        return 0;
    if( obj_id < 0 || count <= 0 )
        return 0;

    stackable = ToriRSServer_ContainerStacksObj(container, obj_id);

    for( int i = 0; i < container->slots; i++ )
    {
        if( container->items[i].obj_id < 0 )
            free_slots++;
    }
    placeholder_slot =
        ToriRSServer_ContainerPlaceholderSlot(container->items, container->slots, obj_id);

    if( stackable )
    {
        int slot = -1;
        int have;
        int room;

        for( int i = 0; i < container->slots && slot < 0; i++ )
        {
            if( container->items[i].obj_id == obj_id )
                slot = i;
        }
        /* The placeholder before any free slot: it *is* where this belongs. */
        if( slot < 0 )
            slot = placeholder_slot;
        if( slot < 0 )
        {
            for( int i = 0; i < container->slots && slot < 0; i++ )
            {
                if( container->items[i].obj_id < 0 )
                    slot = i;
            }
        }
        if( slot < 0 )
            return 0;
        /* The reference clamps at `Inventory.STACK_LIMIT = 0x7fffffff`. Same
         * clamp, written as the overflow it is: the stack and `count` are both
         * int32 and content can legally name two billion of something. */
        have = container->items[slot].obj_id == obj_id ? container->items[slot].count : 0;
        room = INT32_MAX - have;
        if( count > room )
        {
            if( assure_full )
                return 0;
            count = room;
        }
        if( count <= 0 )
            return 0;
        ToriRSServer_ContainerSet(container, slot, obj_id, have + count);
        /* A merge onto an existing stack is never a single fresh unit even
         * when `requested == 1` — the destination slot already had vars of
         * its own (or none), and this is not the "carry a charge" shape. */
        return count;
    }

    /* Unstackable: one slot per unit, `Inventory.add`'s own loop. A placeholder
     * slot is not free but is available to the obj it stands for, so it counts
     * toward the space test as well as winning the position of the first unit. */
    if( placeholder_slot >= 0 )
        free_slots++;
    if( assure_full && count > free_slots )
        return 0;
    if( count > free_slots )
        count = free_slots;
    if( count <= 0 )
        return 0;
    if( placeholder_slot >= 0 )
    {
        ToriRSServer_ContainerSet(container, placeholder_slot, obj_id, 1);
        if( out_slot && requested == 1 )
            *out_slot = placeholder_slot;
        added++;
    }
    for( int i = 0; i < container->slots && added < count; i++ )
    {
        if( container->items[i].obj_id >= 0 )
            continue;
        ToriRSServer_ContainerSet(container, i, obj_id, 1);
        if( out_slot && requested == 1 )
            *out_slot = i;
        added++;
    }
    return added;
}

int
ToriRSServer_ContainerAdd(
    struct ToriRSServerContainer* container,
    int obj_id,
    int count,
    int assure_full)
{
    return ToriRSServer_ContainerAddEx(container, obj_id, count, assure_full, NULL);
}

int
ToriRSServer_ContainerAddOutSlot(
    struct ToriRSServerContainer* container,
    int obj_id,
    int count,
    int assure_full,
    int* out_slot)
{
    return ToriRSServer_ContainerAddEx(container, obj_id, count, assure_full, out_slot);
}

void
ToriRSServer_ContainerClean(struct ToriRSServerContainer* container)
{
    assert(container);
    if( !container->used )
        return;
    *slot_mask_of(container) = 0;
    *dirty_of(container) = 0;
}

int
ToriRSServer_ContainerIsDirty(const struct ToriRSServerContainer* container)
{
    struct ToriRSServerContainer* row = (struct ToriRSServerContainer*)container;

    if( !row || !row->used )
        return 0;
    return row->per_slot ? (*slot_mask_of(row) != 0) : (*dirty_of(row) != 0);
}

uint32_t
ToriRSServer_ContainerSlotMask(const struct ToriRSServerContainer* container)
{
    struct ToriRSServerContainer* row = (struct ToriRSServerContainer*)container;

    if( !row || !row->used || !row->per_slot )
        return 0;
    return *slot_mask_of(row);
}

/* ------------------------------------------------------------------ */
/* Bindings                                                            */
/* ------------------------------------------------------------------ */

/*
 * How many slots UPDATE_INV_FULL carries.
 *
 * The field is named `capacity` on the wire and the client sizes its container
 * from it, so trimming it is not free the way trimming a payload would be. A
 * client told the backpack holds 17 slots draws 17 cells, answers `inv_size`
 * with 17, and — until it learned to widen (inv_manager.c's
 * `inv_container_ensure_slots`) — dropped every partial update for a slot above
 * that. The player saw empty cells the server had already filled.
 *
 * So a container small enough to transmit per slot sends its real capacity: the
 * backpack's 28 and the worn set's 14 cost a few dozen bytes and are exactly
 * the two whose size the grid and CS2 read.
 *
 * A big one still sends only the used prefix — the bank is 1,410 slots at up to
 * 7 bytes each, and it re-sends whole on every change. UPDATE_INV_FULL clears
 * everything past what it carries, so the tail is still correct; the client
 * widens on demand when a later update reaches past it.
 *
 * A SHOP IS NOT ALLOWED THAT PREFIX, and the reason is the grid rather than the
 * container. `shop_main_init` (clientscript 1074) builds `inv_size($shop)` cells
 * **once**, at open, and `shop_main_update` (1076) repaints exactly that many on
 * every later transmit — unlike the bank, whose own script rebuilds its grid
 * from scratch each time. So a 40-slot general store with 15 lines stocked was
 * transmitted as 15, drew 15 cells, and could never show a 16th: selling into it
 * moved the item onto the shelf and told the client about it, and the client had
 * nowhere to put it. From in front of the counter that is exactly "selling to a
 * general store just deletes the item" — the backpack cell empties, the coins
 * arrive, and the shop looks untouched. It also cost the empty cells a real shop
 * shows below its stock, which are what you drop an item into.
 *
 * Specialty shops never showed it because their cache invs are small enough to
 * be per-slot (Bob's is 7) and a sale merges into a line that already has a cell.
 */
static int
full_capacity(const struct ToriRSServerContainer* row)
{
    int used = 0;

    if( row->per_slot )
        return row->slots;
    if( ToriRSServer_ShopDef(row->inv_id) )
        return row->slots;
    for( int i = 0; i < row->slots; i++ )
        if( row->items[i].obj_id >= 0 )
            used = i + 1;
    return used;
}

/* A listener is always qualified by its viewer. This used to matter only for
 * shared world rows, where the same component id exists on every client. A
 * Costume Room guest can now also view another player's private row, so the
 * same rule is required there: one guest closing component 675:11 must not
 * unbind the owner or every other guest from their copies of 675:11. */
static int
listener_matches(
    const struct ToriRSServerContainer* row,
    int i,
    struct ToriRSServerPlayer* player)
{
    return row->listeners[i].player == player;
}

static int
unbind_row(
    struct ToriRSServerContainer* row,
    struct ToriRSServerPlayer* player,
    int32_t component)
{
    int dropped = 0;
    int cleared = 0;
    int w = 0;

    if( !row->used )
        return 0;
    for( int r = 0; r < row->listener_count; r++ )
    {
        if( row->listeners[r].component == component && listener_matches(row, r, player) )
        {
            /* IF_CLEARINV retires the item array embedded in this widget;
             * UPDATE_INV_STOPTRANSMIT is separate, inventory-global state
             * and is sent by inv_stoptransmit only when no other component
             * (or, on a shared row, no other player) still listens. Clear
             * before dropping the association so a component moved to another
             * inventory cannot retain old rows underneath its immediate
             * UPDATE_INV_FULL. */
            if( !cleared )
            {
                ToriRSServer_SendIfClearinv(player, component);
                cleared = 1;
            }
            dropped++;
            continue;
        }
        row->listeners[w++] = row->listeners[r];
    }
    row->listener_count = (uint8_t)w;
    return dropped;
}

int
ToriRSServer_ContainerUnbind(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int32_t component)
{
    int dropped = 0;

    assert(player);
    /* A viewer may be listening to another player's private container (the
     * Costume Room collection view). Component ownership belongs to the
     * listener, not the row, so remove that listener from every live private
     * table rather than assuming it is stored on the viewer. */
    if( srv )
    {
        for( int p = 0; p < srv->player_count; p++ )
        {
            struct ToriRSServerPlayer* owner = &srv->players[p];

            if( !owner->active )
                continue;
            for( int i = 0; i < TORIRSSERVER_CONTAINER_MAX; i++ )
                dropped += unbind_row(&owner->containers[i], player, component);
        }
        for( int i = 0; i < TORIRSSERVER_WORLD_CONTAINER_MAX; i++ )
            dropped += unbind_row(&srv->world_containers[i], player, component);
    }
    else
    {
        for( int i = 0; i < TORIRSSERVER_CONTAINER_MAX; i++ )
            dropped += unbind_row(&player->containers[i], player, component);
    }
    return dropped;
}

static int
bind_row(
    struct ToriRSServer* srv,
    struct ToriRSServerContainer* row,
    struct ToriRSServerPlayer* viewer,
    int32_t inv_id,
    int32_t component)
{
    int listener_i;

    if( !row )
        return 0;
    assert(viewer);

    /* Same (inv, component, viewer) already listening — LostCity's early
     * return. Component ids are client-local, so even a private row may have
     * several listeners using the same numeric component. */
    for( listener_i = 0; listener_i < row->listener_count; listener_i++ )
    {
        if( row->listeners[listener_i].component == component &&
            listener_matches(row, listener_i, viewer) )
            return 1;
    }

    /* A component listens to at most one inv; move it if it was elsewhere.
     * Only this player's prior binding of `component` moves — a shared row's
     * other listeners are other players and must not be touched. */
    ToriRSServer_ContainerUnbind(srv, viewer, component);

    if( row->listener_count >= TORIRSSERVER_CONTAINER_LISTENERS_MAX )
    {
        fprintf(stderr,
                "torirsserver: inv %d already has %d listeners; cannot bind component %d\n",
                (int)inv_id, TORIRSSERVER_CONTAINER_LISTENERS_MAX, (int)component);
        return 0;
    }

    listener_i = row->listener_count++;
    row->listeners[listener_i].component = component;
    row->listeners[listener_i].first_seen = 1;
    row->listeners[listener_i].player = viewer;
    /* The reference sends a full update the moment a listener is added, and the
     * interface being painted needs it: a paint hook only runs on a transmit,
     * so a panel mounted before the container existed stays empty otherwise.
     * Only this listener's first_seen is cleared — other listeners keep theirs. */
    ToriRSServer_SendInvFull(viewer, (int)component, (int)inv_id, row->items, full_capacity(row));
    row->listeners[listener_i].first_seen = 0;
    return 1;
}

int
ToriRSServer_ContainerBind(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int32_t inv_id,
    int32_t component)
{
    return bind_row(srv, ToriRSServer_ContainerResolve(srv, player, inv_id), player, inv_id,
                    component);
}

int
ToriRSServer_ContainerBindFrom(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* owner,
    struct ToriRSServerPlayer* viewer,
    int32_t inv_id,
    int32_t component)
{
    if( !owner || !viewer )
        return 0;
    return bind_row(srv, ToriRSServer_ContainerResolve(srv, owner, inv_id), viewer, inv_id,
                    component);
}

void
ToriRSServer_ContainerFlush(struct ToriRSServerPlayer* player)
{
    assert(player);
    for( int i = 0; i < TORIRSSERVER_CONTAINER_MAX; i++ )
    {
        struct ToriRSServerContainer* row = &player->containers[i];
        int dirty;
        int any_first_seen = 0;

        if( !row->used || !row->items )
            continue;

        dirty = ToriRSServer_ContainerIsDirty(row);
        for( int l = 0; l < row->listener_count; l++ )
        {
            if( row->listeners[l].first_seen )
                any_first_seen = 1;
        }

        if( row->listener_count == 0 )
        {
            /*
             * Nothing is painting from it. Drop the dirty state: a container
             * written while its interface was closed would otherwise re-transmit
             * the moment something bound to it, at which point the bind's own
             * full update has already covered it.
             *
             * Exception: an external dirty_ref means another flush owns the
             * bit. The bank is the case — its row exists for resolve and dirty
             * (so `inv_del(bank,…)` marks correctly) but transmit still runs
             * through ToriRSServer_BankFlush, which gates on bank.open. Cleaning
             * here would clear bank.dirty before that flush could send
             * UPDATE_INV_FULL. Folding bank transmit into this table means
             * moving bank.open onto the binding; that is a real simplification
             * and not this stage's.
             */
            if( dirty && !row->dirty_ref )
                ToriRSServer_ContainerClean(row);
            continue;
        }

        if( !dirty && !any_first_seen )
            continue;

        for( int l = 0; l < row->listener_count; l++ )
        {
            int32_t component = row->listeners[l].component;
            struct ToriRSServerPlayer* target = row->listeners[l].player;

            if( !target )
                continue;
            if( !dirty && !row->listeners[l].first_seen )
                continue;
            if( row->per_slot && dirty && !row->listeners[l].first_seen )
                ToriRSServer_SendInvPartial(target, (int)component, (int)row->inv_id, row->items,
                                         row->slots, ToriRSServer_ContainerSlotMask(row));
            else
                ToriRSServer_SendInvFull(target, (int)component, (int)row->inv_id, row->items,
                                      full_capacity(row));
            row->listeners[l].first_seen = 0;
        }
        ToriRSServer_ContainerClean(row);
    }
}

void
ToriRSServer_ContainerFlushWorld(struct ToriRSServer* srv)
{
    assert(srv);
    for( int i = 0; i < TORIRSSERVER_WORLD_CONTAINER_MAX; i++ )
    {
        struct ToriRSServerContainer* row = &srv->world_containers[i];
        int dirty;
        int any_first_seen = 0;

        if( !row->used || !row->items )
            continue;

        dirty = ToriRSServer_ContainerIsDirty(row);
        for( int l = 0; l < row->listener_count; l++ )
        {
            if( row->listeners[l].first_seen )
                any_first_seen = 1;
        }

        /* Same "nobody is painting from it" drop as ToriRSServer_ContainerFlush —
         * see its comment. A shared row's dirty_ref is always NULL (every
         * shop row owns its own dirty state), so that branch does not apply
         * here. */
        if( row->listener_count == 0 )
        {
            if( dirty )
                ToriRSServer_ContainerClean(row);
            continue;
        }

        if( !dirty && !any_first_seen )
            continue;

        for( int l = 0; l < row->listener_count; l++ )
        {
            int32_t component = row->listeners[l].component;
            struct ToriRSServerPlayer* target = row->listeners[l].player;

            if( !target )
                continue; /* should not happen on a world row; skip rather than crash */
            if( !dirty && !row->listeners[l].first_seen )
                continue;
            if( row->per_slot && dirty && !row->listeners[l].first_seen )
                ToriRSServer_SendInvPartial(target, (int)component, (int)row->inv_id, row->items,
                                         row->slots, ToriRSServer_ContainerSlotMask(row));
            else
                ToriRSServer_SendInvFull(target, (int)component, (int)row->inv_id, row->items,
                                      full_capacity(row));
            row->listeners[l].first_seen = 0;
        }
        ToriRSServer_ContainerClean(row);
    }
}
