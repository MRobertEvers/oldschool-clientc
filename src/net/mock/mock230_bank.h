#ifndef SRC_NET_MOCK_MOCK230_BANK_H
#define SRC_NET_MOCK_MOCK230_BANK_H

/*
 * The bank: a 1220-slot container, the settings the interface reads out of
 * varbits, and the deposit/withdraw arithmetic.
 *
 * Split out of mock230_world.c because almost none of it is world state. The
 * bank is a container plus a dozen packed player variables, and the only thing
 * it shares with the rest of the tick is "mark it dirty, phase 10 sends it".
 *
 * Two things here are read from the cache rather than written down:
 *
 *   - **Container sizes** come from config group 5 (inv). The bank is 1220
 *     slots at rev 230, the backpack 28 and the worn container 14 — and the
 *     bank's CS2 walks exactly `inv_size(bank)` slots, so a server that
 *     disagrees paints a short grid or walks off the end of one.
 *   - **Varbit bit ranges** come from config group 14. A bank setting is a bit
 *     range inside a shared varplayer, not a variable of its own: withdraw-as-
 *     note is varp 115 bit 0 and the current tab is varp 115 bits 4..7, so
 *     writing either as a whole varp silently destroys the other. Resolving
 *     them through the cache means the ranges cannot drift from the client's.
 *
 * See docs/mock230_bank.md.
 */

#include <stdint.h>

struct Mock230Server;
struct Mock230Item;

enum
{
    /** Container id the client's InvManager knows (INV_MANAGER_CONTAINER_BANK)
     *  and the id `inv_getobj(bank, …)` resolves in the bank's own CS2. */
    MOCK230_INV_BANK = 95,

    /** Slots the mock allocates. The cache's inv config decides how many are
     *  actually used; this is the ceiling the array is sized to. */
    MOCK230_BANK_SLOTS = 1220,
    MOCK230_BANK_TABS = 9,

    /* Interface groups, verified with tools/dump_interface against
     * cache.osrs230 and named the same way OpenRune's gameval table does
     * (interfaces.bankmain / interfaces.bankside). */
    MOCK230_BANK_IFACE = 12,
    MOCK230_BANKSIDE_IFACE = 15,

    /*
     * Where the two halves mount, in toplevel_osrs_stretch (161).
     *
     * `mainmodal` is the big centre panel every full-screen interface opens
     * into; `sidemodal` replaces the whole sidebar, which is what makes the
     * bank's inventory panel appear where the tabs were. Opening the side half
     * anywhere else leaves the tab strip on top of it.
     */
    MOCK230_MAINMODAL_SLOT = 16,
    MOCK230_SIDEMODAL_SLOT = 74,

    /*
     * Components of interface 12 the server addresses directly. Every one was
     * read out of tools/dump_interface rather than taken from another server's
     * table — the bank's child numbering moved between OldSchool revisions, and
     * a constant borrowed from a newer cache names a different component here.
     */
    MOCK230_BANK_COM_TITLE = 3,
    MOCK230_BANK_COM_ITEMS = 13,
    MOCK230_BANK_COM_SCROLLBAR = 14,
    MOCK230_BANK_COM_SWAP = 19,
    MOCK230_BANK_COM_INSERT = 21,
    MOCK230_BANK_COM_ITEM_MODE = 24,
    MOCK230_BANK_COM_NOTE_MODE = 26,
    MOCK230_BANK_COM_QTY_1 = 30,
    MOCK230_BANK_COM_QTY_5 = 32,
    MOCK230_BANK_COM_QTY_10 = 34,
    MOCK230_BANK_COM_QTY_X = 36,
    MOCK230_BANK_COM_QTY_ALL = 38,
    MOCK230_BANK_COM_DEPOSIT_INV = 44,
    MOCK230_BANK_COM_DEPOSIT_WORN = 46,

    /** Bank side: the inventory grid the deposit ops come from. */
    MOCK230_BANKSIDE_COM_ITEMS = 3,

    /*
     * Bank varbits. Names are OpenRune's gameval `varbits` group; the bit
     * ranges are *not* here on purpose — they come from the cache, see
     * mock230_bank_varbit_resolve.
     */
    MOCK230_VARBIT_BANK_WITHDRAWNOTES = 3958,
    MOCK230_VARBIT_BANK_INSERTMODE = 3959,
    MOCK230_VARBIT_BANK_REQUESTEDQUANTITY = 3960,
    MOCK230_VARBIT_BANK_QUANTITY_TYPE = 6590,
    MOCK230_VARBIT_BANK_CURRENTTAB = 4150,
    MOCK230_VARBIT_BANK_TAB_DISPLAY = 4170,
    MOCK230_VARBIT_BANK_TAB_1 = 4171,
    MOCK230_VARBIT_BANK_LEAVEPLACEHOLDERS = 3755,
    MOCK230_VARBIT_BANK_SHOWINCINERATOR = 5102,
    MOCK230_VARBIT_BANK_HIDEDEPOSITWORN = 5364,
    MOCK230_VARBIT_BANK_SIDE_SLOT_IGNORE = 5450,

    /** What a pending "-X" prompt will do with the number when it arrives. */
    MOCK230_BANK_PENDING_NONE = 0,
    MOCK230_BANK_PENDING_WITHDRAW = 1,
    MOCK230_BANK_PENDING_DEPOSIT = 2,

    /** Quantity modes, in the order the five buttons sit in. */
    MOCK230_BANK_QTY_1 = 0,
    MOCK230_BANK_QTY_5 = 1,
    MOCK230_BANK_QTY_10 = 2,
    MOCK230_BANK_QTY_X = 3,
    MOCK230_BANK_QTY_ALL = 4,
};

/**
 * Per-player bank state.
 *
 * The settings are duplicated here rather than read back out of the varps they
 * live in, because a varbit read is a bit-range lookup and this code asks for
 * them on every click. The varps stay authoritative for the *client* — every
 * write goes through mock230_bank_set_varbit — and these are the server's copy.
 */
struct Mock230Bank
{
    struct Mock230Item* slots;
    /** Slots the cache's inv config says this container has. */
    int size;

    int open;
    /** Withdraw as note rather than as item (varbit 3958). */
    int note_mode;
    /** Insert rather than swap when a slot is dragged (varbit 3959). */
    int insert_mode;
    /** MOCK230_BANK_QTY_* (varbit 6590). */
    int quantity_mode;

    /*
     * A "-X" row that is waiting on the player's number.
     *
     * The prompt is the client's and the answer arrives one or more ticks
     * later, so which slot was clicked and which direction it was going have to
     * be held here — by the time RESUME_P_COUNTDIALOG lands, nothing else
     * remembers. Content that drives the bank through scripts does not use
     * this: a script simply blocks on p_countdialog and keeps its own locals.
     */
    int pending_kind;
    int pending_slot;
    /** The X in "Withdraw-X" (varbit 3960). */
    int requested_quantity;
    /** 0 = all items, 1..9 = a tab (varbit 4150). */
    int current_tab;
    /** How the tab strip labels itself (varbit 4170): 0 numbers, 3 hides it
     *  when no tab holds anything. */
    int tab_display;
    /** Objs in each tab, which is what the client lays the tab strip out from
     *  (varbits 4171..4179). Tab 0 is "everything not in a tab" and is derived,
     *  not stored. */
    int tab_size[MOCK230_BANK_TABS];

    /** Set by any mutation; drained by mock230_bank_flush in phase 10. A whole
     *  re-transmit rather than a delta: the bank changes in bursts (a deposit-
     *  all moves 28 slots) and 1220 slots is 5 KB, which is nothing once per
     *  interaction and never once per tick. */
    int dirty;
};

/* ------------------------------------------------------------------ */
/* Cache-derived tables                                                */
/* ------------------------------------------------------------------ */

/**
 * Decode config group 5 (inv) and group 14 (varbit).
 *
 * Returns the number of varbits indexed, or 0 when the cache is absent — in
 * which case container sizes fall back to the protocol defaults and every
 * varbit write is dropped with a warning. The bank still runs; the interface
 * simply cannot be told which settings are on.
 */
int
mock230_bank_load(const char* cache_dir);

void
mock230_bank_free(void);

/** Slot count for a container id, or 0 when the cache did not name one. */
int
mock230_bank_inv_size(int inv_id);

/** Resolve a varbit to the varplayer and bit range holding it. Returns 0 when
 *  the cache has no such record, in which case nothing is written. */
int
mock230_bank_varbit_resolve(
    int varbit_id,
    int* basevar,
    int* lsb,
    int* msb);

/* ------------------------------------------------------------------ */
/* Varbits                                                             */
/* ------------------------------------------------------------------ */

/** Insert `value` into the bit range the cache gives this varbit, and mark the
 *  varplayer holding it for this tick's flush. */
void
mock230_bank_set_varbit(
    struct Mock230Server* srv,
    int varbit_id,
    int value);

int
mock230_bank_get_varbit(
    struct Mock230Server* srv,
    int varbit_id);

/* ------------------------------------------------------------------ */
/* The bank                                                            */
/* ------------------------------------------------------------------ */

/** Allocate the container and put every setting at its default. Called from
 *  mock230_world_init. */
void
mock230_bank_init(struct Mock230Server* srv);
void
mock230_bank_shutdown(struct Mock230Server* srv);

/** Open both halves: main into 161:16, side into 161:74, push every setting
 *  varbit, and transmit both containers. */
void
mock230_bank_open(struct Mock230Server* srv);

/** Close both halves and stop transmitting. Safe to call when not open. */
void
mock230_bank_close(struct Mock230Server* srv);

/** Transmit the bank if anything changed. Called from phase 10. */
void
mock230_bank_flush(struct Mock230Server* srv);

/**
 * Move `amount` of the obj in backpack slot `inv_slot` into the bank.
 *
 * Notes are un-noted on the way in, which is what makes a bank hold one stack
 * of an item rather than two. Returns the number actually moved.
 */
int
mock230_bank_deposit(
    struct Mock230Server* srv,
    int inv_slot,
    int amount);

/** Same, from a worn equipment slot. */
int
mock230_bank_deposit_worn(
    struct Mock230Server* srv,
    int worn_slot,
    int amount);

/** Move `amount` out of bank slot `bank_slot`, as a note when note mode is on
 *  and the obj has a note form. Returns the number actually moved. */
int
mock230_bank_withdraw(
    struct Mock230Server* srv,
    int bank_slot,
    int amount);

int
mock230_bank_deposit_all_inv(struct Mock230Server* srv);
int
mock230_bank_deposit_all_worn(struct Mock230Server* srv);

/** Swap two bank slots, or shuffle one into the other's place when insert mode
 *  is on. */
void
mock230_bank_move_slot(
    struct Mock230Server* srv,
    int from,
    int to);

/** Compact the container towards slot 0, preserving order. The reference does
 *  this on open and on close, because a drag can leave gaps. */
void
mock230_bank_reorganize(struct Mock230Server* srv);

/** How many of an obj the bank holds. */
int
mock230_bank_count(
    struct Mock230Server* srv,
    int obj_id);

/** Returned by mock230_bank_quantity_for_op when the row was "Withdraw-X" /
 *  "Deposit-X": the amount is not known until the player types it. */
#define MOCK230_BANK_ASK (-1)

/**
 * The quantity an op index means.
 *
 * This is the fiddliest thing in the file and it is not the mock's invention.
 * The bank's own CS2 builds the item's right-click rows *conditionally* — the
 * row matching the current default quantity is omitted, because it would
 * duplicate the first one — so the op index a click arrives with depends on
 * the settings at the time it was drawn. Script 669 is the ladder for the main
 * panel; `bankside_drawitem` uses fixed indices for the side panel, which is
 * why `side` is a parameter rather than an offset.
 *
 * Returns the quantity, 0 for a row the mock does not implement, or
 * MOCK230_BANK_ASK when the row was the "-X" prompt.
 */
int
mock230_bank_quantity_for_op(
    struct Mock230Server* srv,
    int op,
    int available,
    int side);

/**
 * The engine's own click router, for when no script pack is loaded.
 *
 * Returns 1 when the click was a bank click and was acted on. Content that
 * binds `[if_button,…]` sees the click first; this is the fallback that keeps
 * the bank usable with no content at all, the same contract every other
 * trigger site in the mock has.
 */
int
mock230_bank_handle_button(
    struct Mock230Server* srv,
    int uid,
    int sub,
    int obj,
    int op);

/** Finish whatever "-X" row is waiting on a number. Returns 1 when one was.
 *  Called from the RESUME_P_COUNTDIALOG handler, after the script VM has had
 *  its chance — a parked script owns the answer if there is one. */
int
mock230_bank_resume_countdialog(
    struct Mock230Server* srv,
    int amount);

#endif
