/*
 * The container registry. See mock230_container.h.
 */

#include "mock230_container.h"

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_shop.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Per-slot vars                                                       */
/* ------------------------------------------------------------------ */

static void
item_clear_vars(struct Mock230Item* item)
{
    int i;

    assert(item);
    for( i = 0; i < MOCK230_ITEM_VAR_MAX; i++ )
    {
        item->var_key[i] = -1;
        item->var_val[i] = 0;
    }
}

int
mock230_item_get_var(
    const struct Mock230Item* item,
    int key_obj)
{
    int i;

    if( !item || item->obj_id < 0 )
        return 0;
    for( i = 0; i < MOCK230_ITEM_VAR_MAX; i++ )
    {
        if( item->var_key[i] == key_obj )
            return item->var_val[i];
    }
    return 0;
}

void
mock230_item_set_var(
    struct Mock230Item* item,
    int key_obj,
    int value)
{
    int i;
    int free_i = -1;

    assert(item);
    if( item->obj_id < 0 )
        return;
    for( i = 0; i < MOCK230_ITEM_VAR_MAX; i++ )
    {
        if( item->var_key[i] == key_obj )
        {
            item->var_val[i] = value;
            return;
        }
        if( free_i < 0 && item->var_key[i] < 0 )
            free_i = i;
    }
    assert(free_i >= 0 && "Mock230Item var table full");
    item->var_key[free_i] = key_obj;
    item->var_val[free_i] = value;
}

/* ------------------------------------------------------------------ */
/* Scope                                                               */
/* ------------------------------------------------------------------ */

int
mock230_container_scope(int32_t inv_id)
{
    /*
     * Every inv defaults to per-player. The one exception is a shop: content
     * declares `scope=shared` in a `.inv` file (mock230_content.c's
     * load_inv_config, docs/SHOPS_PLAN.md §3.1), which registers a
     * Mock230ShopDef with `shared` set. An inv nothing declared a shop
     * definition for — the overwhelming majority of the namespace, including
     * the player's own backpack/worn/bank — is not asked and stays PLAYER.
     */
    if( mock230_shop_is_shared(inv_id) )
        return MOCK230_CONTAINER_WORLD;
    return MOCK230_CONTAINER_PLAYER;
}

/* ------------------------------------------------------------------ */
/* Table access                                                        */
/* ------------------------------------------------------------------ */

static struct Mock230Container*
table_for(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int scope,
    int* out_count)
{
    if( scope == MOCK230_CONTAINER_WORLD )
    {
        *out_count = MOCK230_WORLD_CONTAINER_MAX;
        return srv ? srv->world_containers : NULL;
    }
    *out_count = MOCK230_CONTAINER_MAX;
    return player ? player->containers : NULL;
}

static struct Mock230Container*
find_row(
    struct Mock230Container* table,
    int count,
    int32_t inv_id)
{
    for( int i = 0; i < count; i++ )
        if( table[i].used && table[i].inv_id == inv_id )
            return &table[i];
    return NULL;
}

static struct Mock230Container*
free_row(
    struct Mock230Container* table,
    int count)
{
    for( int i = 0; i < count; i++ )
        if( !table[i].used )
            return &table[i];
    return NULL;
}

static void
row_init(
    struct Mock230Container* row,
    int32_t inv_id,
    int slots,
    int scope,
    struct Mock230Player* owner)
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
     * (mock230_encode.c: `dirty & (1u << i)`), so anything larger has to be
     * re-sent whole. The backpack (28) and worn set (14) land on the per-slot
     * side by measurement rather than by being named here.
     */
    row->per_slot = slots > 0 && slots <= 32;
}

/* ------------------------------------------------------------------ */
/* Resolve                                                             */
/* ------------------------------------------------------------------ */

struct Mock230Container*
mock230_container_resolve(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t inv_id)
{
    int scope;
    int count = 0;
    struct Mock230Container* table;
    struct Mock230Container* row;
    int slots;

    if( inv_id < 0 )
        return NULL;

    scope = mock230_container_scope(inv_id);
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
    slots = mock230_bank_inv_size((int)inv_id);
    if( slots <= 0 )
        slots = mock230_shop_content_size(inv_id);
    if( slots <= 0 )
        return NULL;

    row = free_row(table, count);
    if( !row )
    {
        fprintf(stderr,
                "mock230: container table full (%d rows); inv %d cannot be registered\n",
                count, (int)inv_id);
        return NULL;
    }

    row_init(row, inv_id, slots, scope, scope == MOCK230_CONTAINER_WORLD ? NULL : player);
    row->items = calloc((size_t)slots, sizeof(*row->items));
    if( !row->items )
    {
        row->used = 0;
        return NULL;
    }
    row->owns_items = 1;
    for( int i = 0; i < slots; i++ )
    {
        row->items[i].obj_id = -1;
        row->items[i].count = 0;
        item_clear_vars(&row->items[i]);
    }
    return row;
}

struct Mock230Container*
mock230_container_adopt(
    struct Mock230Player* player,
    int32_t inv_id,
    struct Mock230Item* items,
    int slots,
    uint32_t* slot_dirty_ref,
    int* dirty_ref,
    int appearance)
{
    struct Mock230Container* row;

    if( !player || inv_id < 0 || !items || slots <= 0 )
        return NULL;

    row = find_row(player->containers, MOCK230_CONTAINER_MAX, inv_id);
    if( row && row->owns_items )
    {
        free(row->items);
        row->items = NULL;
        row->owns_items = 0;
    }
    if( !row )
        row = free_row(player->containers, MOCK230_CONTAINER_MAX);
    if( !row )
    {
        fprintf(stderr, "mock230: container table full; inv %d cannot be adopted\n",
                (int)inv_id);
        return NULL;
    }

    row_init(row, inv_id, slots, MOCK230_CONTAINER_PLAYER, player);
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
                "mock230: inv %d has %d slots and cannot carry a per-slot dirty mask\n",
                (int)inv_id, slots);
        row->slot_dirty_ref = NULL;
        row->per_slot = 0;
    }
    if( dirty_ref )
        row->per_slot = 0;
    return row;
}

void
mock230_container_forget(
    struct Mock230Player* player,
    int32_t inv_id)
{
    struct Mock230Container* row;

    if( !player )
        return;
    row = find_row(player->containers, MOCK230_CONTAINER_MAX, inv_id);
    if( !row )
        return;
    if( row->owns_items )
        free(row->items);
    memset(row, 0, sizeof(*row));
}

void
mock230_container_shutdown_player(struct Mock230Player* player)
{
    if( !player )
        return;
    for( int i = 0; i < MOCK230_CONTAINER_MAX; i++ )
    {
        struct Mock230Container* row = &player->containers[i];

        if( row->owns_items )
            free(row->items);
        memset(row, 0, sizeof(*row));
    }
}

void
mock230_container_shutdown(struct Mock230Server* srv)
{
    if( !srv )
        return;
    for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
        mock230_container_shutdown_player(&srv->players[i]);
    for( int i = 0; i < MOCK230_WORLD_CONTAINER_MAX; i++ )
    {
        struct Mock230Container* row = &srv->world_containers[i];

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
slot_mask_of(struct Mock230Container* row)
{
    return row->slot_dirty_ref ? row->slot_dirty_ref : &row->slot_dirty_own;
}

static int*
dirty_of(struct Mock230Container* row)
{
    return row->dirty_ref ? row->dirty_ref : &row->dirty_own;
}

void
mock230_container_mark(
    struct Mock230Container* container,
    int slot)
{
    if( !container || !container->used )
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
        container->owner->masks |= MOCK230_PMASK_APPEARANCE;
}

void
mock230_container_mark_all(struct Mock230Container* container)
{
    if( !container || !container->used )
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
        container->owner->masks |= MOCK230_PMASK_APPEARANCE;
}

void
mock230_container_set(
    struct Mock230Container* container,
    int slot,
    int obj_id,
    int count)
{
    struct Mock230Item* item;

    if( !container || !container->used || !container->items )
        return;
    if( slot < 0 || slot >= container->slots )
        return;
    item = &container->items[slot];
    if( obj_id < 0 )
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
    mock230_container_mark(container, slot);
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
 * mock230_ids.h for why each). That is a smaller and more honest gap than the
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
 * (opcode 148, `Mock230ObjInfo.placeholder_id`), read forward: an *item* states
 * its placeholder and no template.
 *
 * Not gated on the container being a bank, and it needs no gate: a placeholder
 * obj only ever exists in one, so the scan cannot match anywhere else.
 */
int
mock230_container_placeholder_slot(
    const struct Mock230Item* items,
    int slots,
    int obj_id)
{
    const struct Mock230ObjInfo* info = mock230_objinfo(obj_id);
    int placeholder;

    if( !items || slots <= 0 )
        return -1;
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
 * in mock230_ids.h. Every other inv follows the obj record. Kept as one function
 * so the day a real stack-policy field lands, this is its only caller-visible
 * seam; an unresolved id is -1 and matches no container.
 */
static int
always_stacks(const struct Mock230Container* container)
{
    const struct Mock230Ids* ids = mock230_ids();

    /* A shop says so in its own `.inv` (`stackall=yes`), which is the real
     * `stackType` field rather than a name this file had to invent. The two
     * hardcoded ids below are the containers that have no `.inv` to say it in. */
    if( mock230_shop_stackall(container->inv_id) )
        return 1;
    return container->inv_id == ids->inv_bank ||
           container->inv_id == ids->inv_collection_log;
}

int
mock230_container_stacks_obj(
    const struct Mock230Container* container,
    int obj_id)
{
    if( obj_id < 0 )
        return 0;
    if( mock230_objinfo(obj_id)->stackable )
        return 1;
    return container && always_stacks(container);
}

/*
 * Empty one slot — and a shop's baseline slot empties to ZERO, not to nothing.
 *
 * This is LostCity's `stockobj`, the one thing `mock230_container_add`'s header
 * says was left out of the `Inventory` port. Buying a store's last pot used to
 * run `items[slot].obj_id = -1`, and three things followed from that one line:
 *
 *   1. The cell vanished from the shop. `shop_main_update` (clientscript 1076)
 *      does `cc_sethide(true)` on a slot whose `inv_getobj` is null, so an
 *      out-of-stock line is not drawn greyed at 0 — it is not drawn at all.
 *   2. It never came back. `mock230_shop_restock_tick` skips `obj_id < 0`
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
mock230_container_clear_slot(
    struct Mock230Container* container,
    int slot)
{
    int obj_id;

    if( !container || !container->used || !container->items )
        return;
    if( slot < 0 || slot >= container->slots )
        return;
    obj_id = container->items[slot].obj_id;
    if( obj_id >= 0 && mock230_shop_has_stock_line(container->inv_id, obj_id) )
    {
        mock230_container_set(container, slot, obj_id, 0);
        return;
    }
    mock230_container_set(container, slot, -1, 0);
}

int
mock230_container_add(
    struct Mock230Container* container,
    int obj_id,
    int count,
    int assure_full)
{
    int stackable;
    int free_slots = 0;
    int added = 0;
    int placeholder_slot;

    if( !container || !container->used || !container->items )
        return 0;
    if( obj_id < 0 || count <= 0 )
        return 0;

    stackable = mock230_objinfo(obj_id)->stackable || always_stacks(container) ? 1 : 0;

    for( int i = 0; i < container->slots; i++ )
    {
        if( container->items[i].obj_id < 0 )
            free_slots++;
    }
    placeholder_slot =
        mock230_container_placeholder_slot(container->items, container->slots, obj_id);

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
        mock230_container_set(container, slot, obj_id, have + count);
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
        mock230_container_set(container, placeholder_slot, obj_id, 1);
        added++;
    }
    for( int i = 0; i < container->slots && added < count; i++ )
    {
        if( container->items[i].obj_id >= 0 )
            continue;
        mock230_container_set(container, i, obj_id, 1);
        added++;
    }
    return added;
}

void
mock230_container_clean(struct Mock230Container* container)
{
    if( !container || !container->used )
        return;
    *slot_mask_of(container) = 0;
    *dirty_of(container) = 0;
}

int
mock230_container_is_dirty(const struct Mock230Container* container)
{
    struct Mock230Container* row = (struct Mock230Container*)container;

    if( !row || !row->used )
        return 0;
    return row->per_slot ? (*slot_mask_of(row) != 0) : (*dirty_of(row) != 0);
}

uint32_t
mock230_container_slot_mask(const struct Mock230Container* container)
{
    struct Mock230Container* row = (struct Mock230Container*)container;

    if( !row || !row->used || !row->per_slot )
        return 0;
    return *slot_mask_of(row);
}

/* ------------------------------------------------------------------ */
/* Bindings                                                            */
/* ------------------------------------------------------------------ */

/** Slots up to and including the last occupied one. UPDATE_INV_FULL clears
 *  everything past the capacity it carries, so the empty tail costs nothing
 *  and sending it costs 7 bytes a slot — 9.8 KB on a full-size bank. */
static int
used_prefix(const struct Mock230Container* row)
{
    int used = 0;

    for( int i = 0; i < row->slots; i++ )
        if( row->items[i].obj_id >= 0 )
            used = i + 1;
    return used;
}

/* A player row's listener always belongs to that row's own player; a world
 * row's listener belongs to whichever player's bind created it. One test
 * covers both without the caller needing to know which kind of row it has. */
static int
listener_matches(
    const struct Mock230Container* row,
    int i,
    struct Mock230Player* player)
{
    if( row->owner_kind == MOCK230_CONTAINER_WORLD )
        return row->listeners[i].player == player;
    return 1;
}

static int
unbind_row(
    struct Mock230Container* row,
    struct Mock230Player* player,
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
                mock230_send_if_clearinv(player, component);
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
mock230_container_unbind(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t component)
{
    int dropped = 0;

    if( !player )
        return 0;
    for( int i = 0; i < MOCK230_CONTAINER_MAX; i++ )
        dropped += unbind_row(&player->containers[i], player, component);
    if( srv )
    {
        for( int i = 0; i < MOCK230_WORLD_CONTAINER_MAX; i++ )
            dropped += unbind_row(&srv->world_containers[i], player, component);
    }
    return dropped;
}

int
mock230_container_bind(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t inv_id,
    int32_t component)
{
    struct Mock230Container* row = mock230_container_resolve(srv, player, inv_id);
    int listener_i;

    if( !row || !player )
        return 0;

    /* Same (inv, com[, player on a shared row]) already listening — LostCity's
     * early return. */
    for( listener_i = 0; listener_i < row->listener_count; listener_i++ )
    {
        if( row->listeners[listener_i].component == component &&
            listener_matches(row, listener_i, player) )
            return 1;
    }

    /* A component listens to at most one inv; move it if it was elsewhere.
     * Only this player's prior binding of `component` moves — a shared row's
     * other listeners are other players and must not be touched. */
    mock230_container_unbind(srv, player, component);

    if( row->listener_count >= MOCK230_CONTAINER_LISTENERS_MAX )
    {
        fprintf(stderr,
                "mock230: inv %d already has %d listeners; cannot bind component %d\n",
                (int)inv_id, MOCK230_CONTAINER_LISTENERS_MAX, (int)component);
        return 0;
    }

    listener_i = row->listener_count++;
    row->listeners[listener_i].component = component;
    row->listeners[listener_i].first_seen = 1;
    row->listeners[listener_i].player = player;
    /* The reference sends a full update the moment a listener is added, and the
     * interface being painted needs it: a paint hook only runs on a transmit,
     * so a panel mounted before the container existed stays empty otherwise.
     * Only this listener's first_seen is cleared — other listeners keep theirs. */
    mock230_send_inv_full(player, (int)component, (int)inv_id, row->items, used_prefix(row));
    row->listeners[listener_i].first_seen = 0;
    return 1;
}

void
mock230_container_flush(struct Mock230Player* player)
{
    if( !player )
        return;
    for( int i = 0; i < MOCK230_CONTAINER_MAX; i++ )
    {
        struct Mock230Container* row = &player->containers[i];
        int dirty;
        int any_first_seen = 0;

        if( !row->used || !row->items )
            continue;

        dirty = mock230_container_is_dirty(row);
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
             * through mock230_bank_flush, which gates on bank.open. Cleaning
             * here would clear bank.dirty before that flush could send
             * UPDATE_INV_FULL. Folding bank transmit into this table means
             * moving bank.open onto the binding; that is a real simplification
             * and not this stage's.
             */
            if( dirty && !row->dirty_ref )
                mock230_container_clean(row);
            continue;
        }

        if( !dirty && !any_first_seen )
            continue;

        for( int l = 0; l < row->listener_count; l++ )
        {
            int32_t component = row->listeners[l].component;

            if( !dirty && !row->listeners[l].first_seen )
                continue;
            if( row->per_slot && dirty && !row->listeners[l].first_seen )
                mock230_send_inv_partial(player, (int)component, (int)row->inv_id, row->items,
                                         row->slots, mock230_container_slot_mask(row));
            else
                mock230_send_inv_full(player, (int)component, (int)row->inv_id, row->items,
                                      used_prefix(row));
            row->listeners[l].first_seen = 0;
        }
        mock230_container_clean(row);
    }
}

void
mock230_container_flush_world(struct Mock230Server* srv)
{
    if( !srv )
        return;
    for( int i = 0; i < MOCK230_WORLD_CONTAINER_MAX; i++ )
    {
        struct Mock230Container* row = &srv->world_containers[i];
        int dirty;
        int any_first_seen = 0;

        if( !row->used || !row->items )
            continue;

        dirty = mock230_container_is_dirty(row);
        for( int l = 0; l < row->listener_count; l++ )
        {
            if( row->listeners[l].first_seen )
                any_first_seen = 1;
        }

        /* Same "nobody is painting from it" drop as mock230_container_flush —
         * see its comment. A shared row's dirty_ref is always NULL (every
         * shop row owns its own dirty state), so that branch does not apply
         * here. */
        if( row->listener_count == 0 )
        {
            if( dirty )
                mock230_container_clean(row);
            continue;
        }

        if( !dirty && !any_first_seen )
            continue;

        for( int l = 0; l < row->listener_count; l++ )
        {
            int32_t component = row->listeners[l].component;
            struct Mock230Player* target = row->listeners[l].player;

            if( !target )
                continue; /* should not happen on a world row; skip rather than crash */
            if( !dirty && !row->listeners[l].first_seen )
                continue;
            if( row->per_slot && dirty && !row->listeners[l].first_seen )
                mock230_send_inv_partial(target, (int)component, (int)row->inv_id, row->items,
                                         row->slots, mock230_container_slot_mask(row));
            else
                mock230_send_inv_full(target, (int)component, (int)row->inv_id, row->items,
                                      used_prefix(row));
            row->listeners[l].first_seen = 0;
        }
        mock230_container_clean(row);
    }
}
