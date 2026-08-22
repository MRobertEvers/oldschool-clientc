#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_IDS_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_IDS_H

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
 * So: the index files state the ids, the table below names the ones the engine
 * needs, and `ToriRSServer_IdsResolve` fills it in once. Nothing downstream holds a
 * literal. Adding an interface means a line in `pack/3_interfaces.pack` — which
 * `cachepack unpack` seeds from the cache's own gameval table — and a field here;
 * a symbol that has moved between revisions then fails loudly at boot instead of
 * quietly addressing the wrong component.
 *
 * Two kinds of number are deliberately NOT here:
 *
 *   - **Storage ceilings** (TORIRSSERVER_BANK_TABS, TORIRSSERVER_INV_SLOTS). Those size
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
#define TORIRSSERVER_COM_GROUP(uid) (((uid) >> 16) & 0xffff)
#define TORIRSSERVER_COM_CHILD(uid) ((uid) & 0xffff)
#define TORIRSSERVER_COM(iface, child) (((iface) << 16) | ((child) & 0xffff))

struct ToriRSServerIds
{
    /* --- Interfaces (pack/interface.pack) --- */

    /** toplevel_osrs_stretch, the default gameframe at login. Session current
     *  top lives on ToriRSServerPlayer.gameframe_iface after if_opentop. */
    int iface_gameframe;
    int iface_toplevel;
    int iface_toplevel_pre_eoc;
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
    /**
     * The enemy health overlay, which opens into the floater slot too.
     *
     * Four All Settings rows configure it (111, 299, 300, 301) and the cache
     * lays it out itself; nothing in the cache OPENS it. See
     * torirs_server_hpbar.c.
     */
    int iface_hpbar_hud;

    /* --- Containers (pack/inv.pack) --- */

    /*
     * The ids the client's own InvManager knows. An UPDATE_INV carries one, and
     * the receiving CS2 reads `inv_getobj(bank, …)` against it — so a wrong id
     * is not a wrong container, it is an empty one.
     */
    int inv_backpack;
    int inv_worn;
    int inv_bank;

    /*
     * The two containers that stack an obj the obj record says never stacks —
     * LostCity's `InvType.stackType = ALWAYS_STACK`, which this cache's inv
     * config cannot carry (`fields/inv.ini`: size and params, nothing else).
     * `ToriRSServer_ContainerAdd` reads them by id, so they are named once here
     * rather than at the add.
     *
     * A bank holds one cell per obj id by definition. The collection log is the
     * same claim about a display: `[proc,collection_earn]` says "duplicates
     * raise the stack count", and five sets of dragon boots is one row reading
     * "5" rather than five rows.
     */
    int inv_collection_log;

    /*
     * The overhead bars, one per footprint band, resolved from the cache's
     * healthbar namespace.
     *
     * `healthbar_standard` is the reference's `default` arm and covers sizes
     * 1..3 as well as anything that never states a size; the other four are the
     * bands Near-Reality's `EntityHitBar.getType()` names. Symbols rather than
     * literals for the usual reason -- the ids are a cache fact and the content
     * tree is where cache facts are named -- and the WIDTHS deliberately do not
     * live beside them: those come from the records themselves, through
     * `ToriRSServer_HealthbarWidth`.
     */
    int healthbar_standard;
    /** Size 4. */
    int healthbar_size4;
    /** Size 5. */
    int healthbar_size5;
    /** Sizes 6 and 7. */
    int healthbar_size67;
    /** Sizes 8 and 9. */
    int healthbar_size89;

    /** The content-owned mapping from worn-tab components to wear slots. */
    int enum_worn_slots;

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

    /*
     * Where a chatbox dialogue mounts.
     *
     * The client's own clientscripts key off this exact component: the
     * gameframe's on_sub_change hook runs script908, which reads
     * `if_hassub(chatbox:chatmodal)` and, when something is mounted there,
     * unhides the modal and hides `chatbox:chatdisplay` — the scrollback and
     * the input line — behind it. So the server neither reveals the dialogue
     * nor hides the chat: mounting here is the whole message, and mounting
     * anywhere else produces a dialogue drawn on top of live chat text.
     */
    int com_chatbox_modal;

    /** Text under the music tab's `Playing:` label. Region music is selected
     *  by C, so C must also tell the tab which named track it selected. */
    int com_music_now_playing_text;

    /** The backpack's 28 cells hang off this one component, and an inventory op
     *  names it rather than a cell. */
    int com_inventory_items;
    /** 387:1, the worn tab's only op: "View equipment stats". */
    int com_worn_equipment_stats;

    /** The bank's item grid and the buttons along its bottom. */
    int com_bankmain_items;
    /** Max-slot text under occupiedslots — CS2 never writes it; the server does. */
    int com_bankmain_capacity;
    /** Tab strip CS2 fills with dynamic children (View all / tab icons / New tab). */
    int com_bankmain_tabs;
    /*
     * One component each, not two.
     *
     * These were four ids taken from another server's name table, and all four
     * were wrong: `swap`/`insert` and `item_mode`/`note_mode` read as pairs of
     * buttons, but rev 230's bank has a single toggle for each — `swap_insert`
     * (child 23) and `note` (child 25), both carrying `op1=*` because the label
     * is set by a clientscript from the varbit. The four ids pointed at
     * `potionstore_container`, `banktags_header_separator` and two `_graphic`
     * children that have no op at all.
     */
    int com_bankmain_swap_insert;
    int com_bankmain_note;
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
     * The orb that opens it is NOT here — see torirs_server_worldmap.c.
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
     * config group 14 at runtime — see ToriRSServer_BankVarbitResolve — because
     * that is the same table the client unpacks them with.
     */
    /** The npc type the enemy health overlay is about; -1 for none. */
    int varp_hpbar_hud_npc;

    /* The enemy health overlay's inputs and its switch. The switch is named
     * `..._disabled` in the cache, which is the inversion stated rather than
     * inferred: 1 is OFF. */
    int varbit_hpbar_hud_hp;
    int varbit_hpbar_hud_basehp;
    int varbit_hpbar_hud_standard_off;

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
    /** The stamina potion's 70% run-drain cut — read by `run_energy_tick`,
     *  written only by content (`%stamina_active`). Here rather than looked up
     *  per tick, and a varbit rather than a varp: the varp lookup it used to go
     *  through answered -1 for this name and the drain was decided by an
     *  out-of-bounds read. */
    int varbit_stamina_active;

    /* --- Constants (`.constant`) --- */

    /** Quantity modes, as `bank_quantity_type` encodes them. */
    int bank_qty_1;
    int bank_qty_5;
    int bank_qty_10;
    int bank_qty_x;
    int bank_qty_all;
    /** Ticks a dropped obj stays on the floor — `^lootdrop_duration`, which
     *  drop_tables/configs/lootdrop.constant has stated all along while
     *  `TORIRSSERVER_LOOT_TICKS` said the same 200 in C beside it. */
    int lootdrop_duration;
};

/** The table. Every field is -1 until ToriRSServer_IdsResolve has run. */
const struct ToriRSServerIds*
ToriRSServer_Ids(void);

/**
 * Fill it in. **Call after ToriRSServer_ContentLoad**, which is where the packs and
 * constants come from.
 *
 * Returns the number of names that did not resolve; each one is reported
 * through the content error count, so `ToriRSServer_Pack` fails on it too. A
 * non-zero return means the server will address something that does not exist —
 * it is not fatal, because a mock with no content tree is a supported way to
 * run the network layer on its own, but nothing that needs the missing id will
 * work.
 */
int
ToriRSServer_IdsResolve(void);

#endif
