/*
 * The container registry. See mock230_container.h.
 */

#include "mock230_container.h"

#include "mock230.h"
#include "mock230_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Scope                                                               */
/* ------------------------------------------------------------------ */

int
mock230_container_scope(int32_t inv_id)
{
    (void)inv_id;
    /*
     * Every inv is per-player, and this is a missing *input* rather than a
     * decision. LostCity reads `scope` out of its server-side inv.dat (opcode
     * 1); the client cache's inv record carries only size and params, so there
     * is nothing here to read. The field needs `fields/inv.ini` plus a
     * `[namespace:inv]` in `content.ini`, neither of which exists yet.
     *
     * The consequence, stated so nobody has to rediscover it: `shop`'s 107
     * reference invs are `scope=shared` and cannot be modelled until this
     * function can answer. The branch below it is real and tested; the
     * classifier is the hole.
     */
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
    row->component = -1;
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
     * from the cache; an inv it does not size is not a container. */
    slots = mock230_bank_inv_size((int)inv_id);
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
    if( !container || !container->used || !container->items )
        return;
    if( slot < 0 || slot >= container->slots )
        return;
    container->items[slot].obj_id = obj_id < 0 ? -1 : obj_id;
    container->items[slot].count = obj_id < 0 ? 0 : count;
    mock230_container_mark(container, slot);
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

int
mock230_container_bind(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int32_t inv_id,
    int32_t component)
{
    struct Mock230Container* row = mock230_container_resolve(srv, player, inv_id);

    if( !row )
        return 0;
    row->component = component;
    row->first_seen = 1;
    /* The reference sends a full update the moment a listener is added, and the
     * interface being painted needs it: a paint hook only runs on a transmit,
     * so a panel mounted before the container existed stays empty otherwise. */
    mock230_send_inv_full(player, (int)component, (int)inv_id, row->items, used_prefix(row));
    row->first_seen = 0;
    mock230_container_clean(row);
    return 1;
}

int
mock230_container_unbind(
    struct Mock230Player* player,
    int32_t component)
{
    int dropped = 0;

    if( !player )
        return 0;
    for( int i = 0; i < MOCK230_CONTAINER_MAX; i++ )
    {
        struct Mock230Container* row = &player->containers[i];

        if( !row->used || row->component != component )
            continue;
        row->component = -1;
        row->first_seen = 0;
        dropped++;
    }
    return dropped;
}

void
mock230_container_flush(struct Mock230Player* player)
{
    if( !player )
        return;
    for( int i = 0; i < MOCK230_CONTAINER_MAX; i++ )
    {
        struct Mock230Container* row = &player->containers[i];

        if( !row->used || !row->items )
            continue;
        if( row->component < 0 )
        {
            /*
             * Nothing is painting from it. The dirty state still has to be
             * dropped: a container written while its interface was closed would
             * otherwise re-transmit the moment something bound to it, at which
             * point the bind's own full update has already covered it.
             *
             * The bank is deliberately one of these. Its row exists for resolve
             * and dirty — which is what fixes `inv_del(bank,…)` marking a worn
             * slot — but its *transmit* still runs through mock230_bank_flush,
             * which gates on the interface being open and re-sends tab
             * bookkeeping with it. Folding that in means moving `bank.open`
             * into this binding table; it is a real simplification and it is
             * not this stage's.
             */
            continue;
        }
        if( !mock230_container_is_dirty(row) && !row->first_seen )
            continue;
        if( row->per_slot )
            mock230_send_inv_partial(player, (int)row->component, (int)row->inv_id, row->items,
                                     row->slots, mock230_container_slot_mask(row));
        else
            mock230_send_inv_full(player, (int)row->component, (int)row->inv_id, row->items,
                                  used_prefix(row));
        row->first_seen = 0;
        mock230_container_clean(row);
    }
}
