/*
 * The bank.
 *
 * Two separate things live here, and only the first is interesting. The third
 * — the cache-derived inv-size and varbit tables — moved to
 * `torirs_server_bank_tables.c`, so a binary that needs only those facts (the content
 * validator) does not have to link the wire half below to get them.
 *
 *   1. The **arithmetic** — deposit, withdraw, note/un-note, insert vs swap.
 *      This is a port of LostCity's `content/scripts/interface_bank`, whose
 *      rules are the OldSchool ones and worth keeping: a deposit un-notes, a
 *      withdraw refuses rather than half-completes when the backpack cannot
 *      hold the result, and both report which of the three "no space" messages
 *      applies.
 *   2. The **settings**, which at rev 230 are varbits — bit ranges inside
 *      shared varplayers. Resolved through the cache (see
 *      `torirs_server_bank_tables.c`), never written down.
 *   3. The **wire**: two IF_OPENSUBs and an UPDATE_INV_FULL of the bank
 *      container.
 *
 * What this does *not* do is lay the interface out. Every component the player
 * sees is created by the bank's own CS2 (`bankmain_init`, script 274) out of
 * the container and the varbits. That is the whole reason the server side is
 * this small: the client already knows how to draw a bank, and it only needs to
 * be told what is in one.
 *
 * See docs/torirs_server_bank.md.
 */
#include "torirs_server_bank.h"

#include "torirs_server_container.h"

#include "torirs_server.h"
#include "torirs_server_content.h"
#include "torirs_server_ids.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Varps and varbits                                                   */
/* ------------------------------------------------------------------ */

void
ToriRSServer_BankSetVarbit(
    struct ToriRSServer* srv,
    int varbit_id,
    int value)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int basevar;
    int lsb;
    int msb;
    uint32_t mask;
    uint32_t current;

    if( !ToriRSServer_BankVarbitResolve(varbit_id, &basevar, &lsb, &msb) )
    {
        fprintf(stderr, "torirsserver: varbit %d is not in the cache; not sent\n", varbit_id);
        return;
    }
    if( basevar < 0 || basevar >= TORIRSSERVER_VARP_COUNT )
    {
        fprintf(stderr, "torirsserver: varbit %d lives in varp %d, past TORIRSSERVER_VARP_COUNT\n",
                varbit_id, basevar);
        return;
    }

    /* A 32-bit-wide varbit (3960 is bits 1..31, and the reference has some at
     * 0..31) would shift by 32, which is undefined. Build the mask from the
     * width in a way that is defined for every width the format allows. */
    mask = (msb - lsb) >= 31 ? 0xffffffffu : (((1u << (msb - lsb + 1)) - 1u));
    current = (uint32_t)player->varps[basevar];
    current &= ~(mask << lsb);
    current |= ((uint32_t)value & mask) << lsb;
    if( (int32_t)current == player->varps[basevar] )
        return;
    player->varps[basevar] = (int32_t)current;
    ToriRSServer_WorldMarkVarp(player, basevar);
}

int
ToriRSServer_BankGetVarbit(
    struct ToriRSServer* srv,
    int varbit_id)
{
    int basevar;
    int lsb;
    int msb;
    uint32_t mask;

    if( !ToriRSServer_BankVarbitResolve(varbit_id, &basevar, &lsb, &msb) )
        return 0;
    if( basevar < 0 || basevar >= TORIRSSERVER_VARP_COUNT )
        return 0;
    mask = (msb - lsb) >= 31 ? 0xffffffffu : (((1u << (msb - lsb + 1)) - 1u));
    return (int)(((uint32_t)srv->active_player->varps[basevar] >> lsb) & mask);
}

/* ------------------------------------------------------------------ */
/* Container helpers                                                   */
/* ------------------------------------------------------------------ */

static int
inv_free_slots(const struct ToriRSServerPlayer* player)
{
    int free_slots = 0;

    for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id < 0 )
            free_slots++;
    return free_slots;
}

static int
inv_slot_of(
    const struct ToriRSServerPlayer* player,
    int obj_id)
{
    for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            return i;
    return -1;
}

static void
inv_write(
    struct ToriRSServerPlayer* player,
    int slot,
    int obj_id,
    int count)
{
    if( slot < 0 || slot >= TORIRSSERVER_INV_SLOTS )
        return;
    player->inv[slot].obj_id = count > 0 ? obj_id : -1;
    player->inv[slot].count = count > 0 ? count : 0;
    player->inv_dirty |= 1u << slot;
}

/**
 * Put `count` of `obj_id` in the backpack, honouring stacking.
 *
 * Returns how many landed. A stackable obj takes at most one slot; anything
 * else takes one slot each, and a partial result is the normal case rather than
 * an error — the caller decides whether a short add is worth a message.
 */
/*
 * `out_fresh_slot`, when non-NULL, receives the inv slot a *single fresh
 * unstackable unit* landed in — `count == 1`, not merged onto anything —
 * else -1. See ToriRSServer_ItemVarsCopy: a charged item withdrawn from the
 * bank needs to know exactly which slot to write its charges into, and
 * "the slot a brand-new unit took" is the only shape that is unambiguous.
 */
static int
inv_add_ex(
    struct ToriRSServerPlayer* player,
    int obj_id,
    int count,
    int* out_fresh_slot)
{
    const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(obj_id);
    int added = 0;

    if( out_fresh_slot )
        *out_fresh_slot = -1;
    if( obj_id < 0 || count <= 0 )
        return 0;

    if( info->stackable )
    {
        int slot = inv_slot_of(player, obj_id);

        if( slot < 0 )
        {
            for( int i = 0; i < TORIRSSERVER_INV_SLOTS && slot < 0; i++ )
                if( player->inv[i].obj_id < 0 )
                    slot = i;
        }
        if( slot < 0 )
            return 0;
        inv_write(player, slot, obj_id, player->inv[slot].obj_id == obj_id
                                            ? player->inv[slot].count + count
                                            : count);
        return count;
    }

    for( int i = 0; i < TORIRSSERVER_INV_SLOTS && added < count; i++ )
    {
        if( player->inv[i].obj_id >= 0 )
            continue;
        inv_write(player, i, obj_id, 1);
        if( out_fresh_slot && count == 1 )
            *out_fresh_slot = i;
        added++;
    }
    return added;
}


/** How many of `obj_id` the backpack could still take. */
static int
inv_space_for(
    const struct ToriRSServerPlayer* player,
    int obj_id,
    int wanted)
{
    const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(obj_id);
    int free_slots = inv_free_slots(player);

    if( info->stackable )
        return (inv_slot_of(player, obj_id) >= 0 || free_slots > 0) ? wanted : 0;
    return wanted < free_slots ? wanted : free_slots;
}

static void
bank_write(
    struct ToriRSServerBank* bank,
    int slot,
    int obj_id,
    int count)
{
    if( slot < 0 || slot >= bank->size )
        return;
    bank->slots[slot].obj_id = count > 0 ? obj_id : -1;
    bank->slots[slot].count = count > 0 ? count : 0;
    bank->dirty = 1;
}

static int
bank_slot_of(
    const struct ToriRSServerBank* bank,
    int obj_id)
{
    for( int i = 0; i < bank->size; i++ )
        if( bank->slots[i].obj_id == obj_id )
            return i;
    return -1;
}

/* Contiguous tab prefix: tab 1 owns [0, size0), tab 2 owns [size0, size0+size1),
 * …, main (tab 0) owns everything past the sum. Matches bank_gettabrange CS2. */
static int
bank_tab_prefix(const struct ToriRSServerBank* bank)
{
    int sum = 0;

    for( int i = 0; i < TORIRSSERVER_BANK_TABS; i++ )
        sum += bank->tab_size[i];
    return sum;
}

/** Tab holding `slot`: 1..9, or 0 for main. */
static int
bank_tab_for_slot(
    const struct ToriRSServerBank* bank,
    int slot)
{
    int end = 0;

    if( slot < 0 )
        return 0;
    for( int t = 0; t < TORIRSSERVER_BANK_TABS; t++ )
    {
        end += bank->tab_size[t];
        if( slot < end )
            return t + 1;
    }
    return 0;
}

/** First slot of tab `tab` (1..9), or the main prefix when tab is 0 / past end. */
static int
bank_tab_start(
    const struct ToriRSServerBank* bank,
    int tab)
{
    int start = 0;

    if( tab <= 0 )
        return bank_tab_prefix(bank);
    for( int t = 1; t < tab && t <= TORIRSSERVER_BANK_TABS; t++ )
        start += bank->tab_size[t - 1];
    return start;
}

static void
bank_shift_left(
    struct ToriRSServerBank* bank,
    int from)
{
    if( from < 0 || from >= bank->size )
        return;
    for( int i = from; i < bank->size - 1; i++ )
        bank->slots[i] = bank->slots[i + 1];
    bank->slots[bank->size - 1].obj_id = -1;
    bank->slots[bank->size - 1].count = 0;
    bank->dirty = 1;
}

static void
bank_shift_right(
    struct ToriRSServerBank* bank,
    int at)
{
    if( at < 0 || at >= bank->size )
        return;
    if( bank->slots[bank->size - 1].obj_id >= 0 )
        return; /* no room to open a hole */
    for( int i = bank->size - 1; i > at; i-- )
        bank->slots[i] = bank->slots[i - 1];
    bank->slots[at].obj_id = -1;
    bank->slots[at].count = 0;
    bank->dirty = 1;
}

static void
bank_push_tab_settings(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    const struct ToriRSServerEnumDef* tabs = ToriRSServer_ContentEnum("bank_tabs");

    /* Sizes only — currenttab is owned by content / CS2 switchtab. */
    for( int i = 0; tabs && i < tabs->count; i++ )
    {
        int tab = tabs->values[i].key;

        if( tab < 0 || tab >= TORIRSSERVER_BANK_TABS )
            continue;
        ToriRSServer_BankSetVarbit(srv, tabs->values[i].value, bank->tab_size[tab]);
    }
}

/**
 * Move the stack at `from_slot` into `dest_tab` (0 = main, 1..9 = a tab).
 * Updates tab_size[] and re-pushes the tab varbits while the bank is open.
 */
void
ToriRSServer_BankMoveToTab(
    struct ToriRSServer* srv,
    int from_slot,
    int dest_tab)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    struct ToriRSServerItem item;
    int old_tab;
    int insert;

    if( from_slot < 0 || from_slot >= bank->size )
        return;
    if( dest_tab < 0 || dest_tab > TORIRSSERVER_BANK_TABS )
        return;
    if( bank->slots[from_slot].obj_id < 0 )
        return;

    old_tab = bank_tab_for_slot(bank, from_slot);
    if( old_tab == dest_tab )
        return;

    item = bank->slots[from_slot];
    bank_shift_left(bank, from_slot);
    if( old_tab >= 1 )
        bank->tab_size[old_tab - 1]--;

    /* Insertion point after the removal. */
    if( dest_tab >= 1 )
    {
        insert = bank_tab_start(bank, dest_tab) + bank->tab_size[dest_tab - 1];
        if( from_slot < insert )
            insert--;
    }
    else
    {
        insert = bank_tab_prefix(bank);
        if( from_slot < insert )
            insert--;
        /* Append after the last occupied main slot. */
        while( insert < bank->size - 1 && bank->slots[insert].obj_id >= 0 )
            insert++;
    }

    if( insert < 0 )
        insert = 0;
    if( insert >= bank->size )
        insert = bank->size - 1;

    if( bank->slots[insert].obj_id >= 0 )
        bank_shift_right(bank, insert);
    bank->slots[insert] = item;
    bank->dirty = 1;
    if( dest_tab >= 1 )
        bank->tab_size[dest_tab - 1]++;

    if( bank->open )
        bank_push_tab_settings(srv);
}

int
ToriRSServer_BankCountPlayer(
    struct ToriRSServerPlayer* player,
    int obj_id)
{
    struct ToriRSServerBank* bank = &player->bank;
    int slot = bank_slot_of(bank, obj_id);

    return slot >= 0 ? bank->slots[slot].count : 0;
}

int
ToriRSServer_BankCount(
    struct ToriRSServer* srv,
    int obj_id)
{
    return ToriRSServer_BankCountPlayer(srv->active_player, obj_id);
}

/* ------------------------------------------------------------------ */
/* Wire                                                                */
/* ------------------------------------------------------------------ */

/*
 * Transmit the container.
 *
 * The component uid is the bank's item grid, and the inv id is the bank's
 * container. Both
 * are sent because rev 230 sends both: the client binds the container by inv
 * id (which is what `inv_getobj(bank, …)` reads) and uses the component only to
 * decide which interface to notify. Sending only the component would leave the
 * bank's CS2 reading an empty container.
 *
 * Only the used prefix goes out. `UPDATE_INV_FULL` clears everything past the
 * capacity it carries, so a bank holding 12 objs is a 12-slot packet and the
 * other 1208 slots are cleared by omission — which is also what stops a 5 KB
 * packet going out on every deposit.
 */
static void
bank_transmit(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    int used = 0;

    for( int i = 0; i < bank->size; i++ )
        if( bank->slots[i].obj_id >= 0 )
            used = i + 1;

    ToriRSServer_SendInvFull(
        srv->active_player, ToriRSServer_Ids()->com_bankmain_items, ToriRSServer_Ids()->inv_bank, bank->slots, used);
}

void
ToriRSServer_BankFlush(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;

    if( !bank->dirty )
        return;
    bank->dirty = 0;
    /* Only while the interface is up. A deposit cannot happen with the bank
     * closed, but a script can move objs in and out of it, and re-transmitting
     * a container nothing is bound to makes the client re-run a paint hook for
     * an interface that is not mounted. */
    if( !bank->open )
        return;
    bank_transmit(srv);
}

/** Push every setting the interface reads. Sent on open, because the client's
 *  copy of a varp is whatever the last session left there. */
static void
bank_push_settings(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    /* Tab index -> the varbit holding that tab's size. A keyed table rather
     * than `first + index`: see interface_bank/configs/bank.enum. */
    const struct ToriRSServerEnumDef* tabs = ToriRSServer_ContentEnum("bank_tabs");

    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_withdrawnotes, bank->note_mode);
    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_insertmode, bank->insert_mode);
    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_quantity_type, bank->quantity_mode);
    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_requestedquantity, bank->requested_quantity);
    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_currenttab, bank->current_tab);
    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_tab_display, bank->tab_display);
    for( int i = 0; tabs && i < tabs->count; i++ )
    {
        int tab = tabs->values[i].key;

        if( tab < 0 || tab >= TORIRSSERVER_BANK_TABS )
            continue;
        ToriRSServer_BankSetVarbit(srv, tabs->values[i].value, bank->tab_size[tab]);
    }

    /*
     * Three panels the mock has no content for. The interface ships them
     * visible and their CS2 reads these varbits to decide, so leaving them at
     * whatever the varp happened to hold draws an incinerator and a deposit-
     * worn button over a bank that implements neither.
     */
    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_showincinerator, 0);
    /*
     * `bank_leaveplaceholders` is NOT pushed to 0 any more.
     *
     * It used to be, under the same "no server state behind it" rule as the
     * incinerator, and that stopped being true the moment withdrawing could
     * leave a placeholder behind. The varbit *is* the storage — it lives in
     * varp 1053, which is `transmit=yes` and `scope=perm` — so forcing it here
     * threw the player's setting away on every open, and the Enable/Disable
     * button appeared to work (the client flips its own copy) right up until
     * the next visit.
     */
    /* The side panel draws a lock overlay on every inventory slot unless told
     * to ignore the lock varbit, which the mock never sets. */
    ToriRSServer_BankSetVarbit(srv, ids->varbit_bank_side_slot_ignore, 1);
}

/*
 * Unlock the clickable ranges.
 *
 * At rev 230 nothing is clickable until the server says so. Two shapes of that
 * here, and the second is the one that is easy to get wrong:
 *
 *   - **The two item grids** are *dynamic* components, created by CS2 after the
 *     interface mounts, so the events go on the container by slot range rather
 *     than on each child. Bit 20 makes it a drag target and bits 17..19 carry
 *     the drag depth.
 *   - **The settings buttons along the bottom** already have a CS2 hook of
 *     their own (`bankmain_itemnote_op` and friends), which flips the varbit
 *     *client-side* and sends nothing. Arming them is what makes the click
 *     reach the server as well: the client sends IF_BUTTON<n> **and** runs the
 *     hook, gated on this mask (app.c, the PICK_UI branch). Without it the
 *     panel visibly toggles and the server's copy never moves, so a withdraw
 *     after pressing Note comes out as an item.
 *
 * The bit for op N is `1 << N`, with N one-based — which is the client's own
 * test (`events & (1 << op_num)`), not the 0-based reading the same word gets
 * elsewhere. 0x7fe is ops 1..10; op 10 is Examine, which is why the range does
 * not stop at 5.
 */
static void
bank_set_events(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    const int ops_1_to_10 = 0x7fe;
    const int op_1 = 1 << 1;
    const int drag_depth_1 = 1 << 17;
    const int drag_target = 1 << 20;
    const int useable_on = 1 << 21;
    const int k_buttons[] = {
        ids->com_bankmain_swap_insert, ids->com_bankmain_note,
        ids->com_bankmain_qty_1,       ids->com_bankmain_qty_5,
        ids->com_bankmain_qty_10,      ids->com_bankmain_qty_x,
        ids->com_bankmain_qty_all,     ids->com_bankmain_deposit_inv,
        ids->com_bankmain_deposit_worn,
    };

    ToriRSServer_SendIfSetevents(
        srv->active_player,
        ids->com_bankmain_items,
        0,
        bank->size - 1,
        ops_1_to_10 | drag_depth_1 | drag_target);
    ToriRSServer_SendIfSetevents(
        srv->active_player,
        ids->com_bankside_items,
        0,
        TORIRSSERVER_INV_SLOTS - 1,
        ops_1_to_10 | drag_depth_1 | drag_target | useable_on);

    for( size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++ )
        ToriRSServer_SendIfSetevents(srv->active_player, k_buttons[i], 0, 0, op_1);

    /* Tab strip: CS2 creates backgrounds 0..9 and icons at 10+tab. Ops for
     * View tab / Collapse; drag target so items can be dropped onto a tab. */
    ToriRSServer_SendIfSetevents(
        srv->active_player,
        ids->com_bankmain_tabs,
        0,
        19,
        ops_1_to_10 | drag_depth_1 | drag_target);
}

void
ToriRSServer_BankOpen(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    if( bank->open )
        return;

    /* The reference compacts on open, because a drag can leave gaps and the
     * client lays the grid out from slot order. */
    ToriRSServer_BankReorganize(srv);
    bank->note_mode = 0;
    bank->open = 1;

    bank_push_settings(srv);

    /*
     * Two mounts, in this order.
     *
     * `type` is the mount kind the client's IF_OPENSUB carries: 0 is a modal
     * (it takes input focus and closing it closes the interface), 3 is the
     * sidebar replacement. The side panel has to arrive after the main one
     * because the sidebar's own CS2 keys "is a modal open" off the main mount.
     */
    ToriRSServer_SendIfOpensub(
        srv->active_player,
        ids->iface_gameframe,
        TORIRSSERVER_COM_CHILD(ids->com_gameframe_mainmodal),
        ids->iface_bankmain,
        0);
    ToriRSServer_SendIfOpensub(
        srv->active_player,
        ids->iface_gameframe,
        TORIRSSERVER_COM_CHILD(ids->com_gameframe_sidemodal),
        ids->iface_bankside,
        3);

    bank_set_events(srv);

    /* Both containers, in full. The side panel paints the backpack out of the
     * inv container the client already holds — but its paint hook only runs on
     * a transmit, so it has to be re-sent or the panel mounts empty. */
    bank_transmit(srv);
    ToriRSServer_SendInvFull(
        srv->active_player,
        ids->com_bankside_items,
        ids->inv_backpack,
        srv->active_player->inv,
        TORIRSSERVER_INV_SLOTS);
    bank->dirty = 0;

    /* Capacity under occupiedslots — CS2 never writes it. Content's openbank
     * also sends this after if_openmain_side; this covers the C-only open path. */
    {
        char capacity[16];

        snprintf(capacity, sizeof(capacity), "%d", bank->size);
        ToriRSServer_SendIfSettext(srv->active_player, ids->com_bankmain_capacity, capacity);
    }

    /* Bonus rows: [if_open,bankmain] ~equipment_refresh (IF_OPENSUB fires it). */
}

void
ToriRSServer_BankClose(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    if( !bank->open )
        return;
    bank->open = 0;

    ToriRSServer_SendIfClosesub(srv->active_player, ids->com_gameframe_mainmodal);
    ToriRSServer_SendIfClosesub(srv->active_player, ids->com_gameframe_sidemodal);

    /* The sidebar's inventory tab is a different interface from the bank's side
     * panel, and it was never unmounted — but its container binding is, so the
     * backpack has to be re-sent against the tab's own component or the tab
     * comes back empty. */
    ToriRSServer_SendInvFull(
        srv->active_player,
        ids->com_inventory_items,
        ids->inv_backpack,
        srv->active_player->inv,
        TORIRSSERVER_INV_SLOTS);

    /* The reference compacts on close as well, in a queued script, so a bank
     * re-opened later is already tidy. */
    ToriRSServer_BankReorganize(srv);
}

/* ------------------------------------------------------------------ */
/* Deposits and withdrawals                                            */
/* ------------------------------------------------------------------ */

/*
 * The un-noted form of an obj.
 *
 * A note carries `noted_template` (the shared 799-style template record) and
 * `noted_id` (the item it stands for); a plain item carries neither. So the
 * un-note direction reads straight out of the record, and the note direction
 * needs the reverse index ToriRSServer_ObjInfo builds — nothing in the cache points
 * from an item to its note.
 */
static int
uncert(int obj_id)
{
    const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(obj_id);

    return info->noted_template >= 0 && info->noted_id >= 0 ? info->noted_id : obj_id;
}

static int
cert(int obj_id)
{
    const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(obj_id);

    return info->cert_id >= 0 ? info->cert_id : obj_id;
}

/*
 * The bank-placeholder form of an obj, or -1 when it has none.
 *
 * The note pair above in shape — obj opcodes 148/149 against 97/98 — read
 * forward: an *item* states `placeholder_id` and no template, a *placeholder*
 * states both. Most objs state neither, because the cache only mints
 * placeholders for things a bank can hold; -1 is that, and it is the answer for
 * a placeholder handed back in too.
 */
static int
placeholder_of(int obj_id)
{
    const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(obj_id);

    return info->placeholder_template < 0 && info->placeholder_id >= 0 ? info->placeholder_id : -1;
}

/**
 * Empty `bank_slot`, whose obj was `obj_id`: leave a placeholder behind, or
 * close the gap.
 *
 * This is one decision and it has to be made here, in the same breath as the
 * emptying, because the gap-closing shift destroys the evidence. Content used
 * to own the placeholder half (`~bank_leave_placeholder`, called from
 * `~bank_withdraw_request`) and it ran *after* this function returned, so it
 * looked at a slot the shift had already refilled with the next row, decided
 * the withdraw had not emptied anything, and returned. The feature therefore
 * worked for exactly one obj in the bank — the last occupied row, where the
 * shift is a no-op — and for nothing else. Content keeps the button and the
 * varbit; the slot mechanics are the engine's, the same as tabs and note mode.
 *
 * Not gated on the bank being open: a script can withdraw from a closed bank,
 * and the setting is the player's either way.
 */
static void
bank_slot_emptied(
    struct ToriRSServer* srv,
    int bank_slot,
    int obj_id)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    int placeholder = -1;

    if( ToriRSServer_BankGetVarbit(srv, ToriRSServer_Ids()->varbit_bank_leaveplaceholders) )
        placeholder = placeholder_of(obj_id);

    if( placeholder >= 0 )
    {
        /* A real obj at a count of zero, which is what a placeholder *is* on
         * the wire — `bankmain_drawitem` reads the pair back as
         * `oc_unplaceholder($obj) ! $obj` and draws the cell at trans 120 with
         * op 8 "Release". The slot stays occupied, so the tab it belongs to
         * keeps its size and nothing shifts. */
        bank->slots[bank_slot].obj_id = placeholder;
        bank->slots[bank_slot].count = 0;
        /* A different obj is in the cell now, so whatever the withdrawn item
         * had charged, dosed or degraded to does not follow it — the same rule
         * `ToriRSServer_ContainerSet` applies when an obj changes under a slot. */
        ToriRSServer_ItemClearVars(&bank->slots[bank_slot]);
        bank->dirty = 1;
        return;
    }

    /* Emptying a tab slot closes the gap so the contiguous prefix stays true. */
    {
        int tab = bank_tab_for_slot(bank, bank_slot);

        bank_shift_left(bank, bank_slot);
        if( tab >= 1 )
        {
            bank->tab_size[tab - 1]--;
            if( bank->open )
                bank_push_tab_settings(srv);
        }
    }
}

/*
 * `out_fresh_slot`, when non-NULL, receives the bank slot a deposit landed in
 * when — and only when — it opened a *new* bank row (`count == 1` and no
 * existing stack to merge onto). Merging onto an existing stack is left at
 * -1: the bank has one var table per slot and one count for the whole stack
 * (the comment below already says "everything in the bank is a stack"), so
 * once two charged items of the same obj_id have merged there is no single
 * charge count to preserve — a stated limit, not fixed here, the same way
 * ITEM_CHARGES_PLAN.md §3a states the ground-obj one.
 */
static int
bank_add_ex(
    struct ToriRSServer* srv,
    int obj_id,
    int count,
    int* out_fresh_slot)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    int slot;
    int dest_tab;

    if( out_fresh_slot )
        *out_fresh_slot = -1;
    if( obj_id < 0 || count <= 0 )
        return 0;
    /* Everything in the bank is a stack, whatever the objtype says: that is
     * what a bank is. Only the withdraw side consults `stackable`. */
    slot = bank_slot_of(bank, obj_id);
    if( slot >= 0 )
    {
        bank_write(bank, slot, obj_id, bank->slots[slot].count + count);
        return count;
    }

    /*
     * Before any of the placement rules below: the obj's own placeholder, if
     * the bank is holding one.
     *
     * A placeholder *is* the slot this item came out of, remembered — landing
     * beside it instead of in it is the one outcome the feature exists to
     * prevent, and it also leaves a stale placeholder for an item that is now
     * back. It wins over the current tab for the same reason: the player put it
     * there, and the tab is only a default for something with no home.
     *
     * `bank_slot_of` cannot find it because a placeholder is a *different obj*
     * (14730 for a bronze sword), which is exactly why this needs its own scan.
     */
    slot = ToriRSServer_ContainerPlaceholderSlot(bank->slots, bank->size, obj_id);
    if( slot >= 0 )
    {
        bank_write(bank, slot, obj_id, count);
        if( out_fresh_slot && count == 1 )
            *out_fresh_slot = slot;
        return count;
    }

    /* New stack: land in the viewed tab (varbit), else main after the prefix. */
    dest_tab = ToriRSServer_BankGetVarbit(srv, ids->varbit_bank_currenttab);
    if( dest_tab < 0 || dest_tab > TORIRSSERVER_BANK_TABS || dest_tab == 15 )
        dest_tab = 0;
    bank->current_tab = dest_tab;

    if( dest_tab >= 1 && dest_tab <= TORIRSSERVER_BANK_TABS )
    {
        int insert = bank_tab_start(bank, dest_tab) + bank->tab_size[dest_tab - 1];

        if( insert < 0 || insert >= bank->size || bank->slots[bank->size - 1].obj_id >= 0 )
        {
            ToriRSServer_Say(srv, "bank_full_message", NULL);
            return 0;
        }
        if( bank->slots[insert].obj_id >= 0 )
            bank_shift_right(bank, insert);
        bank_write(bank, insert, obj_id, count);
        if( out_fresh_slot && count == 1 )
            *out_fresh_slot = insert;
        bank->tab_size[dest_tab - 1]++;
        if( bank->open )
            bank_push_tab_settings(srv);
        return count;
    }

    /* Main: first free at or past the tab prefix. */
    {
        int prefix = bank_tab_prefix(bank);

        slot = -1;
        for( int i = prefix; i < bank->size; i++ )
        {
            if( bank->slots[i].obj_id < 0 )
            {
                slot = i;
                break;
            }
        }
        if( slot < 0 )
        {
            ToriRSServer_Say(srv, "bank_full_message", NULL);
            return 0;
        }
        bank_write(bank, slot, obj_id, count);
        if( out_fresh_slot && count == 1 )
            *out_fresh_slot = slot;
        return count;
    }
}


int
ToriRSServer_BankDeposit(
    struct ToriRSServer* srv,
    int inv_slot,
    int amount)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int obj_id;
    int held;
    int banked;

    if( inv_slot < 0 || inv_slot >= TORIRSSERVER_INV_SLOTS )
        return 0;
    obj_id = player->inv[inv_slot].obj_id;
    if( obj_id < 0 )
        return 0;

    /* A stack deposits from the one slot; a non-stackable obj deposits from
     * every slot holding it, which is what "Deposit-All" means for 25 bones. */
    held = 0;
    if( ToriRSServer_ObjInfo(obj_id)->stackable )
        held = player->inv[inv_slot].count;
    else
        for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
            if( player->inv[i].obj_id == obj_id )
                held += player->inv[i].count;

    if( amount > held )
        amount = held;
    if( amount <= 0 )
        return 0;

    /* A single unstackable unit deposited from the exact slot clicked carries
     * its vars into the bank slot it opens — see ToriRSServer_ItemVarsCopy and
     * bank_add_ex's comment on why anything else (a merge, or more than one
     * unit) is left alone. Snapshot before inv_write clears the source. */
    {
        int fresh_slot = -1;
        int carry_vars = amount == 1 && !ToriRSServer_ObjInfo(obj_id)->stackable &&
                          player->inv[inv_slot].obj_id == obj_id;
        struct ToriRSServerItem saved;

        if( carry_vars )
            saved = player->inv[inv_slot];
        banked = bank_add_ex(srv, uncert(obj_id), amount, &fresh_slot);
        if( banked <= 0 )
            return 0;
        if( carry_vars && banked == 1 && fresh_slot >= 0 )
            ToriRSServer_ItemVarsCopy(&srv->active_player->bank.slots[fresh_slot], &saved);
    }

    /* Take it back out of the backpack, starting with the slot that was
     * clicked so a partial deposit empties the one the player pointed at. */
    {
        int remaining = banked;

        if( player->inv[inv_slot].obj_id == obj_id )
        {
            int take = player->inv[inv_slot].count < remaining ? player->inv[inv_slot].count
                                                               : remaining;
            inv_write(player, inv_slot, obj_id, player->inv[inv_slot].count - take);
            remaining -= take;
        }
        for( int i = 0; i < TORIRSSERVER_INV_SLOTS && remaining > 0; i++ )
        {
            int take;

            if( player->inv[i].obj_id != obj_id )
                continue;
            take = player->inv[i].count < remaining ? player->inv[i].count : remaining;
            inv_write(player, i, obj_id, player->inv[i].count - take);
            remaining -= take;
        }
    }
    return banked;
}

int
ToriRSServer_BankDepositWorn(
    struct ToriRSServer* srv,
    int worn_slot,
    int amount)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int obj_id;
    int banked;

    if( worn_slot < 0 || worn_slot >= TORIRSSERVER_WORN_SLOTS )
        return 0;
    obj_id = player->worn[worn_slot].obj_id;
    if( obj_id < 0 )
        return 0;
    if( amount > player->worn[worn_slot].count )
        amount = player->worn[worn_slot].count;
    if( amount <= 0 )
        return 0;

    {
        int fresh_slot = -1;
        int carry_vars = amount == 1 && !ToriRSServer_ObjInfo(obj_id)->stackable;
        struct ToriRSServerItem saved;

        if( carry_vars )
            saved = player->worn[worn_slot];
        banked = bank_add_ex(srv, uncert(obj_id), amount, &fresh_slot);
        if( banked <= 0 )
            return 0;
        if( carry_vars && banked == 1 && fresh_slot >= 0 )
            ToriRSServer_ItemVarsCopy(&srv->active_player->bank.slots[fresh_slot], &saved);
    }

    player->worn[worn_slot].count -= banked;
    if( player->worn[worn_slot].count <= 0 )
    {
        player->worn[worn_slot].obj_id = -1;
        player->worn[worn_slot].count = 0;
    }
    player->worn_dirty |= 1u << worn_slot;
    /* Taking equipment off changes what the player looks like, and the
     * appearance mask is the only thing that tells anyone. */
    player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
    return banked;
}

int
ToriRSServer_BankWithdraw(
    struct ToriRSServer* srv,
    int bank_slot,
    int amount)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerBank* bank = &player->bank;
    int obj_id;
    int form;
    int space;
    int moved;

    if( bank_slot < 0 || bank_slot >= bank->size )
        return 0;
    obj_id = bank->slots[bank_slot].obj_id;
    if( obj_id < 0 )
        return 0;
    if( amount > bank->slots[bank_slot].count )
        amount = bank->slots[bank_slot].count;
    if( amount <= 0 )
        return 0;

    form = bank->note_mode ? cert(obj_id) : uncert(obj_id);
    if( bank->note_mode && form == obj_id )
    {
        /* Every obj is withdrawable; not every obj has a note form. The
         * reference says so and withdraws the item anyway. */
        ToriRSServer_Say(srv, "bank_not_notable_message", NULL);
    }

    space = inv_space_for(player, form, amount);
    if( space <= 0 )
    {
        ToriRSServer_Say(srv, "bank_withdraw_no_space_message", NULL);
        return 0;
    }
    if( space < amount )
    {
        /* The reference distinguishes these two, and they are genuinely
         * different situations: one stack that will not fit versus a pile of
         * separate items that will not all fit. */
        if( ToriRSServer_ObjInfo(form)->stackable )
            ToriRSServer_Say(srv, "bank_withdraw_too_many_message", NULL);
        else
            ToriRSServer_Say(srv, "bank_carry_message", NULL);
        amount = space;
    }

    {
        int fresh_slot = -1;
        int carry_vars = amount == 1 && form == obj_id && !ToriRSServer_ObjInfo(obj_id)->stackable;
        struct ToriRSServerItem saved;

        if( carry_vars )
            saved = bank->slots[bank_slot];
        moved = inv_add_ex(player, form, amount, &fresh_slot);
        if( moved <= 0 )
            return 0;
        if( carry_vars && moved == 1 && fresh_slot >= 0 )
            ToriRSServer_ItemVarsCopy(&player->inv[fresh_slot], &saved);
    }
    bank_write(bank, bank_slot, obj_id, bank->slots[bank_slot].count - moved);
    if( bank->slots[bank_slot].obj_id < 0 )
        bank_slot_emptied(srv, bank_slot, obj_id);
    return moved;
}

int
ToriRSServer_BankDepositAllInv(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int moved = 0;

    for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id < 0 )
            continue;
        moved += ToriRSServer_BankDeposit(srv, i, player->inv[i].count);
    }
    return moved;
}

int
ToriRSServer_BankDepositAllWorn(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int moved = 0;

    for( int i = 0; i < TORIRSSERVER_WORN_SLOTS; i++ )
    {
        if( player->worn[i].obj_id < 0 )
            continue;
        moved += ToriRSServer_BankDepositWorn(srv, i, player->worn[i].count);
    }
    return moved;
}

void
ToriRSServer_BankMoveSlot(
    struct ToriRSServer* srv,
    int from,
    int to)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    int insert = ToriRSServer_BankGetVarbit(srv, ToriRSServer_Ids()->varbit_bank_insertmode);

    if( from < 0 || from >= bank->size || to < 0 || to >= bank->size || from == to )
        return;

    /* Content owns `%bank_insertmode`; read the varbit, not a C mirror. */
    if( !insert )
    {
        struct ToriRSServerItem swap = bank->slots[from];

        bank->slots[from] = bank->slots[to];
        bank->slots[to] = swap;
        bank->dirty = 1;
        return;
    }

    /* Insert: walk the dragged slot one step at a time towards its
     * destination, swapping as it goes, so everything between shuffles up by
     * one rather than being displaced. This is LostCity's `insert_bank`. */
    {
        struct ToriRSServerItem moving = bank->slots[from];
        int step = from < to ? 1 : -1;

        for( int i = from; i != to; i += step )
            bank->slots[i] = bank->slots[i + step];
        bank->slots[to] = moving;
        bank->dirty = 1;
    }
}

void
ToriRSServer_BankReorganize(struct ToriRSServer* srv)
{
    struct ToriRSServerBank* bank = &srv->active_player->bank;
    int prefix = bank_tab_prefix(bank);
    int write = prefix;

    /* Tab prefix stays contiguous by construction (withdraw/move_to_tab shift).
     * Only the main section is packed — collapsing across tab boundaries would
     * steal slots from the tab sizes CS2 reads. */
    for( int read = prefix; read < bank->size; read++ )
    {
        if( bank->slots[read].obj_id < 0 )
            continue;
        if( write != read )
        {
            bank->slots[write] = bank->slots[read];
            bank->slots[read].obj_id = -1;
            bank->slots[read].count = 0;
            bank->dirty = 1;
        }
        write++;
    }
}

/*
 * Item clicks and settings are content (`[if_buttonN,bankmain:items]`,
 * settings binds on the armed components). This stub remains so the IF_BUTTON
 * fallback call site compiles; it never acts.
 */
int
ToriRSServer_BankHandleButton(
    struct ToriRSServer* srv,
    int uid,
    int sub,
    int obj,
    int op)
{
    (void)srv;
    (void)uid;
    (void)sub;
    (void)obj;
    (void)op;
    return 0;
}

int
ToriRSServer_BankResumeCountdialog(
    struct ToriRSServer* srv,
    int amount)
{
    /* Content parks on p_countdialog and resumes through the VM. */
    (void)srv;
    (void)amount;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void
ToriRSServer_BankInitPlayer(struct ToriRSServerPlayer* player)
{
    struct ToriRSServerBank* bank = &player->bank;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    int size = ToriRSServer_BankInvSize(ids->inv_bank);

    ToriRSServer_BankShutdownPlayer(player);

    /*
     * No cache means no inv config; fall back to a usable container rather than
     * to zero, so only the client's own grid decides how much is reachable.
     *
     * The `size > TORIRSSERVER_BANK_SLOTS` half of this test used to be here too,
     * described as the same fallback. It was not — it was a clamp, and the
     * cache says 1410, so every bank this server ever allocated was **190 slots
     * short of the container the client walks**. The allocation is a calloc;
     * there was never a ceiling for it to enforce.
     */
    if( size <= 0 )
        size = TORIRSSERVER_BANK_SLOTS;

    bank->slots = calloc((size_t)size, sizeof(*bank->slots));
    assert(bank->slots);
    bank->size = size;
    for( int i = 0; i < size; i++ )
    {
        bank->slots[i].obj_id = -1;
        bank->slots[i].count = 0;
        /* calloc leaves the var keys at 0, and 0 is obj id 0, not "unset" —
         * without this every slot in a fresh bank looks to
         * `ToriRSServer_ItemSetVar` like its four var entries are already taken,
         * and the first `inv_setvar` a script aims at a banked item aborts. */
        ToriRSServer_ItemClearVars(&bank->slots[i]);
    }
    bank->open = 0;
    bank->note_mode = 0;
    bank->insert_mode = 0;
    bank->quantity_mode = ids->bank_qty_1;
    bank->requested_quantity = 0;
    bank->current_tab = 0;
    bank->tab_display = 0;
    bank->pending_kind = TORIRSSERVER_BANK_PENDING_NONE;
    bank->pending_slot = -1;
    memset(bank->tab_size, 0, sizeof(bank->tab_size));
    bank->dirty = 0;

    /*
     * Into the registry, over the allocation the bank owns and the flag
     * `ToriRSServer_BankFlush` reads. Whole-container rather than per-slot: 1410
     * slots cannot be addressed by a 32-bit mask, and the registry refuses the
     * combination rather than shifting past the width — which is what the two
     * hand-rolled `inv_del` dirty paths in torirs_server_scripts.c were doing.
     */
    ToriRSServer_ContainerAdopt(player, ids->inv_bank, bank->slots, bank->size, NULL, &bank->dirty,
                            0);
}

void
ToriRSServer_BankShutdownPlayer(struct ToriRSServerPlayer* player)
{
    /* The registry row points at the array this is about to free, so it goes
     * first. It never owned the storage — `owns_items` is 0 for an adopted
     * row — so forgetting it frees nothing twice. */
    ToriRSServer_ContainerForget(player, ToriRSServer_Ids()->inv_bank);
    free(player->bank.slots);
    player->bank.slots = NULL;
    player->bank.size = 0;
}

/* Every player's, for a host tearing the whole world down. Per-player rather
 * than "the primary player's", which is what leaked a bank per logout the
 * moment there were two. */
void
ToriRSServer_BankShutdown(struct ToriRSServer* srv)
{
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
        ToriRSServer_BankShutdownPlayer(&srv->players[i]);
}
