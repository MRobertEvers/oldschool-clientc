/*
 * The bank.
 *
 * Three separate things live here, and only the first is interesting:
 *
 *   1. The **arithmetic** — deposit, withdraw, note/un-note, insert vs swap.
 *      This is a port of LostCity's `content/scripts/interface_bank`, whose
 *      rules are the OldSchool ones and worth keeping: a deposit un-notes, a
 *      withdraw refuses rather than half-completes when the backpack cannot
 *      hold the result, and both report which of the three "no space" messages
 *      applies.
 *   2. The **settings**, which at rev 230 are varbits — bit ranges inside
 *      shared varplayers. Resolved through the cache, never written down.
 *   3. The **wire**: two IF_OPENSUBs and an UPDATE_INV_FULL of container 95.
 *
 * What this does *not* do is lay the interface out. Every component the player
 * sees is created by the bank's own CS2 (`bankmain_init`, script 274) out of
 * the container and the varbits. That is the whole reason the server side is
 * this small: the client already knows how to draw a bank, and it only needs to
 * be told what is in one.
 *
 * See docs/mock230_bank.md.
 */
#include "mock230_bank.h"

#include "mock230.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Cache-derived tables                                                */
/* ------------------------------------------------------------------ */

struct BankVarbit
{
    int16_t basevar;
    int8_t lsb;
    int8_t msb;
};

/** Indexed by varbit id; basevar -1 means "no such record". */
static struct BankVarbit* g_varbits;
static int g_varbit_count;

/** Indexed by inv id; 0 means "no such record". */
static int* g_inv_sizes;
static int g_inv_count;

/*
 * A file list for one config group, plus the archive it borrows its buffers
 * from. Both have to stay alive until the caller is done decoding, which is why
 * this returns the pair rather than just the list.
 */
struct ConfigGroup
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
};

static int
config_group_open(
    struct RSCache_Dat2Disk* disk,
    int kind,
    struct ConfigGroup* out)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);

    out->archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, kind);
    out->files = NULL;
    if( !out->archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, out->archive) ||
        out->archive->file_count <= 0 )
    {
        if( out->archive )
            RSCache_Dat2DiskArchiveFree(out->archive);
        out->archive = NULL;
        return 0;
    }
    out->files = RSCache_FileListNewFromDecode(
        out->archive->data, out->archive->data_size, out->archive->file_count);
    if( !out->files )
    {
        RSCache_Dat2DiskArchiveFree(out->archive);
        out->archive = NULL;
        return 0;
    }
    return 1;
}

static void
config_group_close(struct ConfigGroup* group)
{
    if( group->files )
        RSCache_FileListFree(group->files);
    if( group->archive )
        RSCache_Dat2DiskArchiveFree(group->archive);
    group->files = NULL;
    group->archive = NULL;
}

/** file_ids are sparse, so a table has to be sized from the largest one. */
static int
config_group_max_id(const struct ConfigGroup* group)
{
    int max_id = 0;

    for( int i = 0; i < group->files->file_count; i++ )
    {
        int file_id = (group->archive->file_ids && i < group->archive->file_count)
                          ? group->archive->file_ids[i]
                          : i;
        if( file_id + 1 > max_id )
            max_id = file_id + 1;
    }
    return max_id;
}

static void
load_inv_sizes(struct RSCache_Dat2Disk* disk)
{
    struct ConfigGroup group;

    if( !config_group_open(disk, RSCACHE_DAT2_CONFIG_KIND_INV, &group) )
        return;

    g_inv_count = config_group_max_id(&group);
    g_inv_sizes = calloc((size_t)(g_inv_count > 0 ? g_inv_count : 1), sizeof(*g_inv_sizes));
    if( !g_inv_sizes )
    {
        g_inv_count = 0;
        config_group_close(&group);
        return;
    }

    for( int i = 0; i < group.files->file_count; i++ )
    {
        int file_id = (group.archive->file_ids && i < group.archive->file_count)
                          ? group.archive->file_ids[i]
                          : i;
        struct RSCache_Dat2ConfigInv inv;
        struct RSCache_Buffer buffer;

        if( group.files->file_sizes[i] <= 0 || file_id < 0 || file_id >= g_inv_count )
            continue;
        memset(&inv, 0, sizeof(inv));
        RSCache_BufferInit(&buffer, (uint8_t*)group.files->files[i], group.files->file_sizes[i]);
        RSCache_Dat2ConfigInvDecode(&inv, &buffer);
        g_inv_sizes[file_id] = inv.size;
    }
    config_group_close(&group);
}

static int
load_varbits(struct RSCache_Dat2Disk* disk)
{
    struct ConfigGroup group;
    int loaded = 0;

    if( !config_group_open(disk, RSCACHE_DAT2_CONFIG_KIND_VARBIT, &group) )
        return 0;

    g_varbit_count = config_group_max_id(&group);
    g_varbits = calloc((size_t)(g_varbit_count > 0 ? g_varbit_count : 1), sizeof(*g_varbits));
    if( !g_varbits )
    {
        g_varbit_count = 0;
        config_group_close(&group);
        return 0;
    }
    for( int i = 0; i < g_varbit_count; i++ )
        g_varbits[i].basevar = -1;

    for( int i = 0; i < group.files->file_count; i++ )
    {
        int file_id = (group.archive->file_ids && i < group.archive->file_count)
                          ? group.archive->file_ids[i]
                          : i;
        struct RSCache_Dat2ConfigVarbit varbit;
        struct RSCache_Buffer buffer;

        if( group.files->file_sizes[i] <= 0 || file_id < 0 || file_id >= g_varbit_count )
            continue;
        memset(&varbit, 0, sizeof(varbit));
        RSCache_BufferInit(&buffer, (uint8_t*)group.files->files[i], group.files->file_sizes[i]);
        RSCache_Dat2ConfigVarbitDecode(&varbit, &buffer);
        if( varbit.basevar < 0 || varbit.startbit < 0 || varbit.endbit > 31 ||
            varbit.endbit < varbit.startbit )
            continue;
        g_varbits[file_id].basevar = (int16_t)varbit.basevar;
        g_varbits[file_id].lsb = (int8_t)varbit.startbit;
        g_varbits[file_id].msb = (int8_t)varbit.endbit;
        loaded++;
    }
    config_group_close(&group);
    return loaded;
}

int
mock230_bank_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    int loaded;

    mock230_bank_free();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = 230;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Same ../ fallback as mock230_objinfo_load: the binary is run both
         * from the repo root and from src/. */
        char parent[512];
        snprintf(parent, sizeof(parent), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(parent);
    }
    if( !disk )
    {
        fprintf(stderr,
                "mock230: no cache at %s — bank varbits unavailable "
                "(settings cannot be pushed to the interface)\n",
                cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    load_inv_sizes(disk);
    loaded = load_varbits(disk);
    RSCache_Dat2DiskFree(disk);

    fprintf(stderr, "mock230: bank tables loaded (%d varbits, bank=%d slots)\n", loaded,
            mock230_bank_inv_size(MOCK230_INV_BANK));
    return loaded;
}

void
mock230_bank_free(void)
{
    free(g_varbits);
    g_varbits = NULL;
    g_varbit_count = 0;
    free(g_inv_sizes);
    g_inv_sizes = NULL;
    g_inv_count = 0;
}

int
mock230_bank_inv_size(int inv_id)
{
    if( !g_inv_sizes || inv_id < 0 || inv_id >= g_inv_count )
        return 0;
    return g_inv_sizes[inv_id];
}

int
mock230_bank_varbit_resolve(
    int varbit_id,
    int* basevar,
    int* lsb,
    int* msb)
{
    if( !g_varbits || varbit_id < 0 || varbit_id >= g_varbit_count )
        return 0;
    if( g_varbits[varbit_id].basevar < 0 )
        return 0;
    *basevar = g_varbits[varbit_id].basevar;
    *lsb = g_varbits[varbit_id].lsb;
    *msb = g_varbits[varbit_id].msb;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Varps and varbits                                                   */
/* ------------------------------------------------------------------ */

static void
varp_mark(
    struct Mock230Player* player,
    int varp)
{
    for( int i = 0; i < player->varp_changed_count; i++ )
        if( player->varp_changed[i] == varp )
            return;
    if( player->varp_changed_count < MOCK230_VARP_DIRTY_MAX )
        player->varp_changed[player->varp_changed_count++] = varp;
    else
        fprintf(stderr, "mock230: varp change list full, varp %d not sent\n", varp);
}

void
mock230_bank_set_varbit(
    struct Mock230Server* srv,
    int varbit_id,
    int value)
{
    struct Mock230Player* player = &srv->player;
    int basevar;
    int lsb;
    int msb;
    uint32_t mask;
    uint32_t current;

    if( !mock230_bank_varbit_resolve(varbit_id, &basevar, &lsb, &msb) )
    {
        fprintf(stderr, "mock230: varbit %d is not in the cache; not sent\n", varbit_id);
        return;
    }
    if( basevar < 0 || basevar >= MOCK230_VARP_COUNT )
    {
        fprintf(stderr, "mock230: varbit %d lives in varp %d, past MOCK230_VARP_COUNT\n",
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
    varp_mark(player, basevar);
}

int
mock230_bank_get_varbit(
    struct Mock230Server* srv,
    int varbit_id)
{
    int basevar;
    int lsb;
    int msb;
    uint32_t mask;

    if( !mock230_bank_varbit_resolve(varbit_id, &basevar, &lsb, &msb) )
        return 0;
    if( basevar < 0 || basevar >= MOCK230_VARP_COUNT )
        return 0;
    mask = (msb - lsb) >= 31 ? 0xffffffffu : (((1u << (msb - lsb + 1)) - 1u));
    return (int)(((uint32_t)srv->player.varps[basevar] >> lsb) & mask);
}

/* ------------------------------------------------------------------ */
/* Container helpers                                                   */
/* ------------------------------------------------------------------ */

static void
say(
    struct Mock230Server* srv,
    const char* text)
{
    mock230_send_message(srv, text);
}

static int
inv_free_slots(const struct Mock230Player* player)
{
    int free_slots = 0;

    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
        if( player->inv[i].obj_id < 0 )
            free_slots++;
    return free_slots;
}

static int
inv_slot_of(
    const struct Mock230Player* player,
    int obj_id)
{
    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            return i;
    return -1;
}

static void
inv_write(
    struct Mock230Player* player,
    int slot,
    int obj_id,
    int count)
{
    if( slot < 0 || slot >= MOCK230_INV_SLOTS )
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
static int
inv_add(
    struct Mock230Player* player,
    int obj_id,
    int count)
{
    const struct Mock230ObjInfo* info = mock230_objinfo(obj_id);
    int added = 0;

    if( obj_id < 0 || count <= 0 )
        return 0;

    if( info->stackable )
    {
        int slot = inv_slot_of(player, obj_id);

        if( slot < 0 )
        {
            for( int i = 0; i < MOCK230_INV_SLOTS && slot < 0; i++ )
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

    for( int i = 0; i < MOCK230_INV_SLOTS && added < count; i++ )
    {
        if( player->inv[i].obj_id >= 0 )
            continue;
        inv_write(player, i, obj_id, 1);
        added++;
    }
    return added;
}

/** How many of `obj_id` the backpack could still take. */
static int
inv_space_for(
    const struct Mock230Player* player,
    int obj_id,
    int wanted)
{
    const struct Mock230ObjInfo* info = mock230_objinfo(obj_id);
    int free_slots = inv_free_slots(player);

    if( info->stackable )
        return (inv_slot_of(player, obj_id) >= 0 || free_slots > 0) ? wanted : 0;
    return wanted < free_slots ? wanted : free_slots;
}

static void
bank_write(
    struct Mock230Bank* bank,
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
    const struct Mock230Bank* bank,
    int obj_id)
{
    for( int i = 0; i < bank->size; i++ )
        if( bank->slots[i].obj_id == obj_id )
            return i;
    return -1;
}

static int
bank_first_free(const struct Mock230Bank* bank)
{
    for( int i = 0; i < bank->size; i++ )
        if( bank->slots[i].obj_id < 0 )
            return i;
    return -1;
}

int
mock230_bank_count(
    struct Mock230Server* srv,
    int obj_id)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int slot = bank_slot_of(bank, obj_id);

    return slot >= 0 ? bank->slots[slot].count : 0;
}

/* ------------------------------------------------------------------ */
/* Wire                                                                */
/* ------------------------------------------------------------------ */

/*
 * Transmit the container.
 *
 * The component uid is the bank's item container, and the inv id is 95. Both
 * are sent because rev 230 sends both: the client binds the container by inv
 * id (which is what `inv_getobj(bank, …)` reads) and uses the component only to
 * decide which interface to notify. Sending only the component would leave the
 * bank's CS2 reading an empty container id 95.
 *
 * Only the used prefix goes out. `UPDATE_INV_FULL` clears everything past the
 * capacity it carries, so a bank holding 12 objs is a 12-slot packet and the
 * other 1208 slots are cleared by omission — which is also what stops a 5 KB
 * packet going out on every deposit.
 */
static void
bank_transmit(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int used = 0;

    for( int i = 0; i < bank->size; i++ )
        if( bank->slots[i].obj_id >= 0 )
            used = i + 1;

    mock230_send_inv_full(
        srv, (MOCK230_BANK_IFACE << 16) | MOCK230_BANK_COM_ITEMS, MOCK230_INV_BANK, bank->slots,
        used);
}

void
mock230_bank_flush(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;

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
bank_push_settings(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;

    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_WITHDRAWNOTES, bank->note_mode);
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_INSERTMODE, bank->insert_mode);
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_QUANTITY_TYPE, bank->quantity_mode);
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_REQUESTEDQUANTITY,
                            bank->requested_quantity);
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_CURRENTTAB, bank->current_tab);
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_TAB_DISPLAY, bank->tab_display);
    for( int i = 0; i < MOCK230_BANK_TABS; i++ )
        mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_TAB_1 + i, bank->tab_size[i]);

    /*
     * Three panels the mock has no content for. The interface ships them
     * visible and their CS2 reads these varbits to decide, so leaving them at
     * whatever the varp happened to hold draws an incinerator and a deposit-
     * worn button over a bank that implements neither.
     */
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_SHOWINCINERATOR, 0);
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_LEAVEPLACEHOLDERS, 0);
    /* The side panel draws a lock overlay on every inventory slot unless told
     * to ignore the lock varbit, which the mock never sets. */
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_SIDE_SLOT_IGNORE, 1);
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
bank_set_events(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;
    const int ops_1_to_10 = 0x7fe;
    const int op_1 = 1 << 1;
    const int drag_depth_1 = 1 << 17;
    const int drag_target = 1 << 20;
    static const int k_buttons[] = {
        MOCK230_BANK_COM_SWAP,      MOCK230_BANK_COM_INSERT,
        MOCK230_BANK_COM_ITEM_MODE, MOCK230_BANK_COM_NOTE_MODE,
        MOCK230_BANK_COM_QTY_1,     MOCK230_BANK_COM_QTY_5,
        MOCK230_BANK_COM_QTY_10,    MOCK230_BANK_COM_QTY_X,
        MOCK230_BANK_COM_QTY_ALL,   MOCK230_BANK_COM_DEPOSIT_INV,
        MOCK230_BANK_COM_DEPOSIT_WORN,
    };

    mock230_send_if_setevents(
        srv, (MOCK230_BANK_IFACE << 16) | MOCK230_BANK_COM_ITEMS, 0, bank->size - 1,
        ops_1_to_10 | drag_depth_1 | drag_target);
    mock230_send_if_setevents(
        srv, (MOCK230_BANKSIDE_IFACE << 16) | MOCK230_BANKSIDE_COM_ITEMS, 0,
        MOCK230_INV_SLOTS - 1, ops_1_to_10 | drag_depth_1 | drag_target);

    for( size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++ )
        mock230_send_if_setevents(
            srv, (MOCK230_BANK_IFACE << 16) | k_buttons[i], 0, 0, op_1);
}

void
mock230_bank_open(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;

    if( bank->open )
        return;

    /* The reference compacts on open, because a drag can leave gaps and the
     * client lays the grid out from slot order. */
    mock230_bank_reorganize(srv);
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
    mock230_send_if_opensub(
        srv, MOCK230_ROOT_IFACE, MOCK230_MAINMODAL_SLOT, MOCK230_BANK_IFACE, 0);
    mock230_send_if_opensub(
        srv, MOCK230_ROOT_IFACE, MOCK230_SIDEMODAL_SLOT, MOCK230_BANKSIDE_IFACE, 3);

    bank_set_events(srv);

    /* Both containers, in full. The side panel paints the backpack out of
     * container 93, which the client already holds — but its paint hook only
     * runs on a transmit, so it has to be re-sent or the panel mounts empty. */
    bank_transmit(srv);
    mock230_send_inv_full(
        srv, (MOCK230_BANKSIDE_IFACE << 16) | MOCK230_BANKSIDE_COM_ITEMS,
        MOCK230_INV_BACKPACK, srv->player.inv, MOCK230_INV_SLOTS);
    bank->dirty = 0;
}

void
mock230_bank_close(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;

    if( !bank->open )
        return;
    bank->open = 0;

    mock230_send_if_closesub(srv, (MOCK230_ROOT_IFACE << 16) | MOCK230_MAINMODAL_SLOT);
    mock230_send_if_closesub(srv, (MOCK230_ROOT_IFACE << 16) | MOCK230_SIDEMODAL_SLOT);

    /* The sidebar's inventory tab is a different interface from the bank's side
     * panel, and it was never unmounted — but its container binding is, so the
     * backpack has to be re-sent against the tab's own component or the tab
     * comes back empty. */
    mock230_send_inv_full(
        srv, (MOCK230_INV_IFACE << 16) | 0, MOCK230_INV_BACKPACK, srv->player.inv,
        MOCK230_INV_SLOTS);

    /* The reference compacts on close as well, in a queued script, so a bank
     * re-opened later is already tidy. */
    mock230_bank_reorganize(srv);
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
 * needs the reverse index mock230_objinfo builds — nothing in the cache points
 * from an item to its note.
 */
static int
uncert(int obj_id)
{
    const struct Mock230ObjInfo* info = mock230_objinfo(obj_id);

    return info->noted_template >= 0 && info->noted_id >= 0 ? info->noted_id : obj_id;
}

static int
cert(int obj_id)
{
    const struct Mock230ObjInfo* info = mock230_objinfo(obj_id);

    return info->cert_id >= 0 ? info->cert_id : obj_id;
}

static int
bank_add(
    struct Mock230Server* srv,
    int obj_id,
    int count)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int slot;

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
    slot = bank_first_free(bank);
    if( slot < 0 )
    {
        say(srv, "You don't have enough space in your bank account.");
        return 0;
    }
    bank_write(bank, slot, obj_id, count);
    return count;
}

int
mock230_bank_deposit(
    struct Mock230Server* srv,
    int inv_slot,
    int amount)
{
    struct Mock230Player* player = &srv->player;
    int obj_id;
    int held;
    int banked;

    if( inv_slot < 0 || inv_slot >= MOCK230_INV_SLOTS )
        return 0;
    obj_id = player->inv[inv_slot].obj_id;
    if( obj_id < 0 )
        return 0;

    /* A stack deposits from the one slot; a non-stackable obj deposits from
     * every slot holding it, which is what "Deposit-All" means for 25 bones. */
    held = 0;
    if( mock230_objinfo(obj_id)->stackable )
        held = player->inv[inv_slot].count;
    else
        for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
            if( player->inv[i].obj_id == obj_id )
                held += player->inv[i].count;

    if( amount > held )
        amount = held;
    if( amount <= 0 )
        return 0;

    banked = bank_add(srv, uncert(obj_id), amount);
    if( banked <= 0 )
        return 0;

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
        for( int i = 0; i < MOCK230_INV_SLOTS && remaining > 0; i++ )
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
mock230_bank_deposit_worn(
    struct Mock230Server* srv,
    int worn_slot,
    int amount)
{
    struct Mock230Player* player = &srv->player;
    int obj_id;
    int banked;

    if( worn_slot < 0 || worn_slot >= MOCK230_WORN_SLOTS )
        return 0;
    obj_id = player->worn[worn_slot].obj_id;
    if( obj_id < 0 )
        return 0;
    if( amount > player->worn[worn_slot].count )
        amount = player->worn[worn_slot].count;
    if( amount <= 0 )
        return 0;

    banked = bank_add(srv, uncert(obj_id), amount);
    if( banked <= 0 )
        return 0;

    player->worn[worn_slot].count -= banked;
    if( player->worn[worn_slot].count <= 0 )
    {
        player->worn[worn_slot].obj_id = -1;
        player->worn[worn_slot].count = 0;
    }
    player->worn_dirty |= 1u << worn_slot;
    /* Taking equipment off changes what the player looks like, and the
     * appearance mask is the only thing that tells anyone. */
    player->masks |= MOCK230_PMASK_APPEARANCE;
    return banked;
}

int
mock230_bank_withdraw(
    struct Mock230Server* srv,
    int bank_slot,
    int amount)
{
    struct Mock230Player* player = &srv->player;
    struct Mock230Bank* bank = &player->bank;
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
        say(srv, "This item can not be withdrawn as a note.");
    }

    space = inv_space_for(player, form, amount);
    if( space <= 0 )
    {
        say(srv, "You don't have enough inventory space.");
        return 0;
    }
    if( space < amount )
    {
        /* The reference distinguishes these two, and they are genuinely
         * different situations: one stack that will not fit versus a pile of
         * separate items that will not all fit. */
        if( mock230_objinfo(form)->stackable )
            say(srv, "You're not going to be able to carry all that!");
        else
            say(srv, "You don't have enough inventory space to withdraw that many.");
        amount = space;
    }

    moved = inv_add(player, form, amount);
    if( moved <= 0 )
        return 0;
    bank_write(bank, bank_slot, obj_id, bank->slots[bank_slot].count - moved);
    return moved;
}

int
mock230_bank_deposit_all_inv(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    int moved = 0;

    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id < 0 )
            continue;
        moved += mock230_bank_deposit(srv, i, player->inv[i].count);
    }
    return moved;
}

int
mock230_bank_deposit_all_worn(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    int moved = 0;

    for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
    {
        if( player->worn[i].obj_id < 0 )
            continue;
        moved += mock230_bank_deposit_worn(srv, i, player->worn[i].count);
    }
    return moved;
}

void
mock230_bank_move_slot(
    struct Mock230Server* srv,
    int from,
    int to)
{
    struct Mock230Bank* bank = &srv->player.bank;

    if( from < 0 || from >= bank->size || to < 0 || to >= bank->size || from == to )
        return;

    if( !bank->insert_mode )
    {
        struct Mock230Item swap = bank->slots[from];

        bank->slots[from] = bank->slots[to];
        bank->slots[to] = swap;
        bank->dirty = 1;
        return;
    }

    /* Insert: walk the dragged slot one step at a time towards its
     * destination, swapping as it goes, so everything between shuffles up by
     * one rather than being displaced. This is LostCity's `insert_bank`. */
    {
        struct Mock230Item moving = bank->slots[from];
        int step = from < to ? 1 : -1;

        for( int i = from; i != to; i += step )
            bank->slots[i] = bank->slots[i + step];
        bank->slots[to] = moving;
        bank->dirty = 1;
    }
}

void
mock230_bank_reorganize(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int write = 0;

    for( int read = 0; read < bank->size; read++ )
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

/** Clamp, with `wanted <= 0` meaning "nothing". */
static int
at_most(
    int wanted,
    int available)
{
    if( wanted <= 0 )
        return 0;
    return wanted < available ? wanted : available;
}

/** What the "default quantity" radio buttons currently select. */
static int
default_quantity(
    const struct Mock230Bank* bank,
    int available)
{
    switch( bank->quantity_mode )
    {
    case MOCK230_BANK_QTY_1:
        return at_most(1, available);
    case MOCK230_BANK_QTY_5:
        return at_most(5, available);
    case MOCK230_BANK_QTY_10:
        return at_most(10, available);
    case MOCK230_BANK_QTY_X:
        return bank->requested_quantity > 0 ? at_most(bank->requested_quantity, available)
                                            : MOCK230_BANK_ASK;
    case MOCK230_BANK_QTY_ALL:
    default:
        return available;
    }
}

int
mock230_bank_quantity_for_op(
    struct Mock230Server* srv,
    int op,
    int available,
    int side)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int mode = bank->quantity_mode;
    int requested = bank->requested_quantity;

    if( available <= 0 )
        return 0;

    /*
     * The side panel numbers its rows with constants (`bankside_drawitem`),
     * even the ones it does not draw — op 3 is Deposit-1 whether or not it is
     * on the menu. So this half is a plain table.
     */
    if( side )
    {
        switch( op )
        {
        case 2:
            return default_quantity(bank, available);
        case 3:
            return at_most(1, available);
        case 4:
            return at_most(5, available);
        case 5:
            return at_most(10, available);
        case 6:
            return at_most(requested, available);
        case 7:
            return MOCK230_BANK_ASK;
        case 8:
            return available;
        default:
            /* 1 is View/Configure, 9 an item-specific extra, 10 Examine or the
             * slot lock — none of which move objs. */
            return 0;
        }
    }

    /*
     * The main panel does not. Script 669 builds its rows with a running
     * counter and *skips* the one that duplicates the current default, so the
     * index of "Withdraw-All" moves with the settings. Rebuilding the same
     * ladder is the only way to read the index the client sent — a fixed table
     * is right for exactly one combination of them.
     */
    {
        enum
        {
            ROW_DEFAULT,
            ROW_ONE,
            ROW_FIVE,
            ROW_TEN,
            ROW_REQUESTED,
            ROW_ASK,
            ROW_ALL,
            ROW_ALL_BUT_ONE,
        };
        int rows[8];
        int count = 0;

        rows[count++] = ROW_DEFAULT;
        if( mode != MOCK230_BANK_QTY_1 )
            rows[count++] = ROW_ONE;
        if( mode != MOCK230_BANK_QTY_5 )
            rows[count++] = ROW_FIVE;
        if( mode != MOCK230_BANK_QTY_10 )
            rows[count++] = ROW_TEN;
        if( mode != MOCK230_BANK_QTY_X && requested > 0 )
            rows[count++] = ROW_REQUESTED;
        rows[count++] = ROW_ASK;
        if( mode != MOCK230_BANK_QTY_ALL )
            rows[count++] = ROW_ALL;
        rows[count++] = ROW_ALL_BUT_ONE;

        /* Past the ladder are Configure-Charges and Placeholder, neither of
         * which the mock models. */
        if( op < 1 || op > count )
            return 0;

        switch( rows[op - 1] )
        {
        case ROW_DEFAULT:
            return default_quantity(bank, available);
        case ROW_ONE:
            return at_most(1, available);
        case ROW_FIVE:
            return at_most(5, available);
        case ROW_TEN:
            return at_most(10, available);
        case ROW_REQUESTED:
            return at_most(requested, available);
        case ROW_ASK:
            return MOCK230_BANK_ASK;
        case ROW_ALL:
            return available;
        case ROW_ALL_BUT_ONE:
        default:
            return available > 1 ? available - 1 : 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The engine's own click router                                       */
/* ------------------------------------------------------------------ */

static void
set_quantity_mode(
    struct Mock230Server* srv,
    int mode)
{
    srv->player.bank.quantity_mode = mode;
    mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_QUANTITY_TYPE, mode);
}

int
mock230_bank_handle_button(
    struct Mock230Server* srv,
    int uid,
    int sub,
    int obj,
    int op)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int group = (uid >> 16) & 0xffff;
    int child = uid & 0xffff;

    (void)obj;

    if( group == MOCK230_BANKSIDE_IFACE )
    {
        if( child != MOCK230_BANKSIDE_COM_ITEMS || sub < 0 )
            return 0;
        if( !bank->open )
            return 0;
        {
            int obj_id = sub < MOCK230_INV_SLOTS ? srv->player.inv[sub].obj_id : -1;
            int held = 0;
            int amount;

            if( obj_id < 0 )
                return 1;
            if( mock230_objinfo(obj_id)->stackable )
                held = srv->player.inv[sub].count;
            else
                /* Deposit-All on a non-stackable obj means every one of them,
                 * not the one slot's count of 1. */
                for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                    if( srv->player.inv[i].obj_id == obj_id )
                        held += srv->player.inv[i].count;

            amount = mock230_bank_quantity_for_op(srv, op, held, 1);
            if( amount == MOCK230_BANK_ASK )
            {
                /* "Deposit-X". The prompt is the client's; the answer comes
                 * back as RESUME_P_COUNTDIALOG, and the pending action has to
                 * survive until it does. */
                bank->pending_kind = MOCK230_BANK_PENDING_DEPOSIT;
                bank->pending_slot = sub;
                mock230_send_if_opencountdialog(srv);
            }
            else if( amount > 0 )
                mock230_bank_deposit(srv, sub, amount);
        }
        return 1;
    }

    if( group != MOCK230_BANK_IFACE )
        return 0;

    switch( child )
    {
    case MOCK230_BANK_COM_ITEMS:
        if( !bank->open || sub < 0 || sub >= bank->size )
            return 1;
        {
            int available = bank->slots[sub].count;
            int amount = mock230_bank_quantity_for_op(srv, op, available, 0);

            if( amount == MOCK230_BANK_ASK )
            {
                bank->pending_kind = MOCK230_BANK_PENDING_WITHDRAW;
                bank->pending_slot = sub;
                mock230_send_if_opencountdialog(srv);
            }
            else if( amount > 0 )
                mock230_bank_withdraw(srv, sub, amount);
        }
        return 1;

    case MOCK230_BANK_COM_SWAP:
        bank->insert_mode = 0;
        mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_INSERTMODE, 0);
        return 1;
    case MOCK230_BANK_COM_INSERT:
        bank->insert_mode = 1;
        mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_INSERTMODE, 1);
        return 1;
    case MOCK230_BANK_COM_ITEM_MODE:
        bank->note_mode = 0;
        mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_WITHDRAWNOTES, 0);
        return 1;
    case MOCK230_BANK_COM_NOTE_MODE:
        bank->note_mode = 1;
        mock230_bank_set_varbit(srv, MOCK230_VARBIT_BANK_WITHDRAWNOTES, 1);
        return 1;

    case MOCK230_BANK_COM_QTY_1:
        set_quantity_mode(srv, MOCK230_BANK_QTY_1);
        return 1;
    case MOCK230_BANK_COM_QTY_5:
        set_quantity_mode(srv, MOCK230_BANK_QTY_5);
        return 1;
    case MOCK230_BANK_COM_QTY_10:
        set_quantity_mode(srv, MOCK230_BANK_QTY_10);
        return 1;
    case MOCK230_BANK_COM_QTY_X:
        set_quantity_mode(srv, MOCK230_BANK_QTY_X);
        return 1;
    case MOCK230_BANK_COM_QTY_ALL:
        set_quantity_mode(srv, MOCK230_BANK_QTY_ALL);
        return 1;

    case MOCK230_BANK_COM_DEPOSIT_INV:
        mock230_bank_deposit_all_inv(srv);
        return 1;
    case MOCK230_BANK_COM_DEPOSIT_WORN:
        mock230_bank_deposit_all_worn(srv);
        return 1;

    default:
        break;
    }
    return 0;
}

int
mock230_bank_resume_countdialog(
    struct Mock230Server* srv,
    int amount)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int kind = bank->pending_kind;
    int slot = bank->pending_slot;

    bank->pending_kind = MOCK230_BANK_PENDING_NONE;
    bank->pending_slot = -1;
    if( kind == MOCK230_BANK_PENDING_NONE || amount <= 0 )
        return kind != MOCK230_BANK_PENDING_NONE;

    if( kind == MOCK230_BANK_PENDING_WITHDRAW )
        mock230_bank_withdraw(srv, slot, amount);
    else
        mock230_bank_deposit(srv, slot, amount);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void
mock230_bank_init(struct Mock230Server* srv)
{
    struct Mock230Bank* bank = &srv->player.bank;
    int size = mock230_bank_inv_size(MOCK230_INV_BANK);

    mock230_bank_shutdown(srv);

    /* No cache means no inv config; fall back to the full array rather than to
     * zero, so the bank is usable and only the client's own grid decides how
     * much of it is reachable. */
    if( size <= 0 || size > MOCK230_BANK_SLOTS )
        size = MOCK230_BANK_SLOTS;

    bank->slots = calloc((size_t)size, sizeof(*bank->slots));
    if( !bank->slots )
    {
        bank->size = 0;
        return;
    }
    bank->size = size;
    for( int i = 0; i < size; i++ )
    {
        bank->slots[i].obj_id = -1;
        bank->slots[i].count = 0;
    }
    bank->open = 0;
    bank->note_mode = 0;
    bank->insert_mode = 0;
    bank->quantity_mode = MOCK230_BANK_QTY_1;
    bank->requested_quantity = 0;
    bank->current_tab = 0;
    bank->tab_display = 0;
    bank->pending_kind = MOCK230_BANK_PENDING_NONE;
    bank->pending_slot = -1;
    memset(bank->tab_size, 0, sizeof(bank->tab_size));
    bank->dirty = 0;
}

void
mock230_bank_shutdown(struct Mock230Server* srv)
{
    free(srv->player.bank.slots);
    srv->player.bank.slots = NULL;
    srv->player.bank.size = 0;
}
