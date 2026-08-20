#ifndef SRC_NET_MOCK_MOCK230_BANK_H
#define SRC_NET_MOCK_MOCK230_BANK_H

/*
 * The bank: a 1410-slot container, the settings the interface reads out of
 * varbits, and the deposit/withdraw arithmetic.
 *
 * Split out of mock230_world.c because almost none of it is world state. The
 * bank is a container plus a dozen packed player variables, and the only thing
 * it shares with the rest of the tick is "mark it dirty, phase 10 sends it".
 *
 * Two things here are read from the cache rather than written down:
 *
 *   - **Container sizes** come from config group 5 (inv). The bank is 1410
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
struct Mock230Player;
struct Mock230Item;

/*
 * No interface, component, container or varbit id appears in this file.
 *
 * Every one of them is the cache's number rather than this server's, so they
 * are named in the content tree and resolved at boot — `mock230_ids.h` for the
 * interfaces, components and varbits, `interface_bank/configs/bank.constant`
 * for the quantity modes, `bank.enum` for the tab varbits. What is left below
 * is what content cannot state: how much memory a bank takes and what a pending
 * prompt is going to do with the number when it arrives.
 */
enum
{
    /** Slots the mock allocates. The cache's inv config decides how many are
     *  actually used (1410 at rev 239); this is the fallback when there is no
     *  cache to ask, and NOT a ceiling — it was applied as an upper clamp
     *  until 2026-08-02, which silently cost every bank 190 slots. The
     *  allocation is a calloc; there is nothing for a ceiling to protect.
     *  See docs/mock230_containers.md §5. */
    MOCK230_BANK_SLOTS = 1220,
    /** Ceiling on `tab_size`, which is a fixed array. The number of tabs is
     *  however many the `bank_tabs` enum lists. */
    MOCK230_BANK_TABS = 9,

    /** What a pending "-X" prompt will do with the number when it arrives. This
     *  is the server's own bookkeeping and never reaches the wire. */
    MOCK230_BANK_PENDING_NONE = 0,
    MOCK230_BANK_PENDING_WITHDRAW = 1,
    MOCK230_BANK_PENDING_DEPOSIT = 2,
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
    /** Withdraw as note rather than as item (varbit bank_withdrawnotes). */
    int note_mode;
    /** Insert rather than swap when a slot is dragged (bank_insertmode). */
    int insert_mode;
    /** One of the `^bank_qty_*` constants (varbit bank_quantity_type). */
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
    /** The X in "Withdraw-X" (varbit bank_requestedquantity). */
    int requested_quantity;
    /** 0 = all items, 1..n = a tab (varbit bank_currenttab). */
    int current_tab;
    /** How the tab strip labels itself (bank_tab_display): 0 numbers, 3 hides
     *  it when no tab holds anything. */
    int tab_display;
    /** Objs in each tab, which is what the client lays the tab strip out from
     *  (the `bank_tabs` enum's varbits). Tab 0 is "everything not in a tab" and
     *  is derived, not stored. */
    int tab_size[MOCK230_BANK_TABS];

    /** Set by any mutation; drained by mock230_bank_flush in phase 10. A whole
     *  re-transmit rather than a delta: the bank changes in bursts (a deposit-
     *  all moves 28 slots) and 1410 slots is 11 KB, which is nothing once per
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
 *  mock230_world_player_init — a bank belongs to a player, not to a world. */
void
mock230_bank_init_player(struct Mock230Player* player);
void
mock230_bank_shutdown_player(struct Mock230Player* player);

/** Every player's, for a host tearing the world down. */
void
mock230_bank_shutdown(struct Mock230Server* srv);

/** Open both halves — main into the gameframe's mainmodal slot, side into its
 *  sidemodal — push every setting varbit, and transmit both containers. */
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

/**
 * Move the stack at `from_slot` into bank tab `dest_tab` (0 = main / unassigned,
 * 1..9 = a named tab). Updates `tab_size[]` and re-pushes the tab varbits.
 */
void
mock230_bank_move_to_tab(
    struct Mock230Server* srv,
    int from_slot,
    int dest_tab);

/** Compact the main section towards the end of the tab prefix, preserving tab
 *  boundaries. The reference does this on open and on close. */
void
mock230_bank_reorganize(struct Mock230Server* srv);

/** How many of an obj the bank holds. */
/** The same, for a player who is not whoever's turn it is — which a test
 *  asserting on two players at once needs and the active-player form cannot
 *  express. */
int
mock230_bank_count_player(
    struct Mock230Player* player,
    int obj_id);

int
mock230_bank_count(
    struct Mock230Server* srv,
    int obj_id);

/**
 * Former click-router stub. Item withdraw/deposit and settings are content
 * (`[if_buttonN,bankmain:items]` / armed settings comps). Always returns 0.
 */
int
mock230_bank_handle_button(
    struct Mock230Server* srv,
    int uid,
    int sub,
    int obj,
    int op);

/** No C pending-X path — content parks on p_countdialog. Always returns 0. */
int
mock230_bank_resume_countdialog(
    struct Mock230Server* srv,
    int amount);

#endif
