#ifndef SRC_NET_MOCK_MOCK230_IDS_H
#define SRC_NET_MOCK_MOCK230_IDS_H

/*
 * Every id the engine addresses by hand, resolved from the content tree at boot.
 *
 * LostCity's engine contains no interface numbers at all: its content names the
 * bank's grid `bank_main:inv` and the compiler turns that into an id, so the
 * only place a number appears is a `.pack` line generated from the cache. This
 * server implements its interface logic in C rather than in RuneScript, so it
 * cannot get that for free — but it can get the same *property*, which is that
 * the numbers live in one place, beside a name, checked against the cache.
 *
 * So: the `.pack` files under content/pack state the ids, the table below names
 * the ones the engine needs, and `mock230_ids_resolve` fills it in once. Nothing downstream
 * holds a literal. Adding an interface means adding a line to
 * tools/gameval_import.names and a field here, and a symbol that has moved
 * between revisions fails loudly at boot instead of quietly addressing the
 * wrong component.
 *
 * Two kinds of number are deliberately NOT here:
 *
 *   - **Storage ceilings** (MOCK230_BANK_TABS, MOCK230_PRAYER_MAX). Those size
 *     C arrays, so they have to be compile-time constants; content decides how
 *     much of the array is used and the loader checks it fits.
 *   - **Protocol encodings** (mask bits, op numbers, IF_OPENSUB types). Those
 *     are what the *packet* means, not what the cache calls something. A cache
 *     cannot state them and no content file could be checked against one.
 */

/*
 * A component is addressed on the wire as one packed number, and that is the
 * form `component.pack` states — so these are for the two places that need the
 * halves: an IF_OPENSUB, which names the parent and the child separately, and a
 * router matching an incoming uid against an interface.
 */
#define MOCK230_COM_GROUP(uid) (((uid) >> 16) & 0xffff)
#define MOCK230_COM_CHILD(uid) ((uid) & 0xffff)
#define MOCK230_COM(iface, child) (((iface) << 16) | ((child) & 0xffff))

struct Mock230Ids
{
    /* --- Interfaces (pack/interface.pack) --- */

    /** toplevel_osrs_stretch, the gameframe everything else mounts into. */
    int iface_gameframe;
    /** The sidebar's inventory tab and worn-equipment tab. */
    int iface_inventory;
    int iface_wornitems;
    /** The bank's two halves. */
    int iface_bankmain;
    int iface_bankside;
    /** The equipment-stats screen and the backpack panel beside it. */
    int iface_equipment_stats;
    int iface_equipment_stats_side;
    /** The prayer book. */
    int iface_prayerbook;
    /** The world map, which opens into the gameframe's floater slot rather
     *  than into the main modal. */
    int iface_worldmap;

    /* --- Containers (pack/inv.pack) --- */

    /*
     * The ids the client's own InvManager knows. An UPDATE_INV carries one, and
     * the receiving CS2 reads `inv_getobj(bank, …)` against it — so a wrong id
     * is not a wrong container, it is an empty one.
     */
    int inv_backpack;
    int inv_worn;
    int inv_bank;

    /* --- Components (pack/component.pack), as packed (iface << 16) | child --- */

    /*
     * Where a full-screen interface and a sidebar replacement mount. The bank
     * uses both at once, which is what makes its inventory panel appear where
     * the tab strip was.
     */
    int com_gameframe_mainmodal;
    int com_gameframe_sidemodal;
    /** The floating panel, which is where the world map goes. */
    int com_gameframe_floater;

    /** The backpack's 28 cells hang off this one component, and an inventory op
     *  names it rather than a cell. */
    int com_inventory_items;
    /** 387:1, the worn tab's only op: "View equipment stats". */
    int com_worn_equipment_stats;

    /** The bank's item grid and the buttons along its bottom. */
    int com_bankmain_items;
    int com_bankmain_swap;
    int com_bankmain_insert;
    int com_bankmain_item_mode;
    int com_bankmain_note_mode;
    int com_bankmain_qty_1;
    int com_bankmain_qty_5;
    int com_bankmain_qty_10;
    int com_bankmain_qty_x;
    int com_bankmain_qty_all;
    int com_bankmain_deposit_inv;
    int com_bankmain_deposit_worn;
    /*
     * The world map's two close buttons. `esckey` is a zero-sized component
     * that exists only to hold the escape binding; `close` is the red X in the
     * corner. Neither carries an onop script, so both are the server's.
     *
     * The orb that opens it is NOT here — see mock230_worldmap.c.
     */
    int com_worldmap_esckey;
    int com_worldmap_close;

    /** The bank side panel's inventory grid, where deposits come from. */
    int com_bankside_items;

    /*
     * The equipment-stats screen: the container the worn inv binds to, and the
     * eighteen empty text rows the server fills in. OldSchool's client draws
     * them and waits — the screen looks broken rather than empty when a server
     * forgets one, so all eighteen are addressed by name.
     */
    int com_equipment_stats_container;
    int com_equipment_stats_stabatt;
    int com_equipment_stats_slashatt;
    int com_equipment_stats_crushatt;
    int com_equipment_stats_magicatt;
    int com_equipment_stats_rangeatt;
    int com_equipment_stats_stabdef;
    int com_equipment_stats_slashdef;
    int com_equipment_stats_crushdef;
    int com_equipment_stats_magicdef;
    int com_equipment_stats_rangedef;
    int com_equipment_stats_meleestrength;
    int com_equipment_stats_rangestrength;
    int com_equipment_stats_magicdamage;
    int com_equipment_stats_prayer;
    int com_equipment_stats_typemultiplier;
    int com_equipment_stats_slayermultiplier;
    int com_equipment_stats_attackspeedbase;
    int com_equipment_stats_attackspeedactual;

    /* --- Varbits (pack/varbit.pack) --- */

    /*
     * Ids only. Which bits of which varplayer each one occupies comes from
     * config group 14 at runtime — see mock230_bank_varbit_resolve — because
     * that is the same table the client unpacks them with.
     */
    int varbit_bank_withdrawnotes;
    int varbit_bank_insertmode;
    int varbit_bank_requestedquantity;
    int varbit_bank_quantity_type;
    int varbit_bank_currenttab;
    int varbit_bank_tab_display;
    int varbit_bank_leaveplaceholders;
    int varbit_bank_showincinerator;
    int varbit_bank_hidedepositworn;
    int varbit_bank_side_slot_ignore;

    /* --- Constants (`.constant`) --- */

    /** Quantity modes, as `bank_quantity_type` encodes them. */
    int bank_qty_1;
    int bank_qty_5;
    int bank_qty_10;
    int bank_qty_x;
    int bank_qty_all;
};

/** The table. Every field is -1 until mock230_ids_resolve has run. */
const struct Mock230Ids*
mock230_ids(void);

/**
 * Fill it in. **Call after mock230_content_load**, which is where the packs and
 * constants come from.
 *
 * Returns the number of names that did not resolve; each one is reported
 * through the content error count, so `mock230_pack` fails on it too. A
 * non-zero return means the server will address something that does not exist —
 * it is not fatal, because a mock with no content tree is a supported way to
 * run the network layer on its own, but nothing that needs the missing id will
 * work.
 */
int
mock230_ids_resolve(void);

#endif
