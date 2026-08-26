#ifndef SRC_RS_MINIMENU_BUILD_H
#define SRC_RS_MINIMENU_BUILD_H

#include "engine/cache_provider.h"
#include "features/features.h"
#include "inv/inv_manager.h"
#include "revconfig/revconfig.h"
#include "task_runner.h"
#include "ui/uitree.h"
#include "ui/uitree_host.h"
#include "ui/uitree_minimenu.h"

#include <stdbool.h>

struct World;
struct World_PickSet;

/*
 * Minimenu population (reference buildMinimenu, v1 ui_click builders): walks
 * the UI hit stack under the click and turns component ops / inventory-slot
 * obj configs / social client codes into menu rows. Lives in game/ because it
 * reads the cache provider (objtype names + inv_actions) and inventories.
 */

/** Chat-line seam: when non-NULL and the hit stack contains a chat node with
 * a UITreeChatMinimenuConfig, resolves the sender under (x, y). Returns 1 and
 * fills out_sender/out_chat_type on a hit. NULL until chat rendering exists. */
struct RS_MinimenuChatSource
{
    int (*line_at)(void* user, int x, int y, char* out_sender, int sender_cap, int* out_chat_type);
    void* user;
};

/* Active "select-a-target" mode (reference Client.useMode / targetMode). While
 * one is set the whole menu is rebuilt as "<verb> <target>" rows instead of the
 * targets' own ops — a held item picks a use-on target, a spell/prayer button
 * picks a cast target. NONE is the normal menu. */
enum RS_MinimenuSelectMode
{
    RS_MINIMENU_SELECT_NONE = 0,
    RS_MINIMENU_SELECT_USE_ITEM = 1, /* objSelected: "Use <obj> with ..." */
    RS_MINIMENU_SELECT_TARGET = 2,   /* spell on: "<targetOp> ..." */
};

struct RS_MinimenuSelection
{
    enum RS_MinimenuSelectMode mode;
    /* USE_ITEM: the armed inventory item (reference objSelected*). */
    char obj_name[40];
    int obj_slot;
    int obj_com_id;
    /* TARGET: the spell verb ("Cast Wind Strike") + which target kinds it
     * accepts (reference targetOp / targetMask bits: 0x1 obj, 0x2 npc, 0x4 loc,
     * 0x8 player). */
    char target_op[64];
    int target_mask;
    /** Which bit of target_mask means "a held item" — 0x10 classic, 0x20
     *  OldSchool. Carried on the selection rather than read here because it is
     *  a ToriRS_Features slot (`target_mask_held`) and this file has no era.
     *  0 leaves the classic value, per the feature table's own rule. */
    int target_mask_held_bit;
};

struct RS_MinimenuBuildCtx
{
    struct UITree* tree;
    struct UITreeHost const* ui_host;
    struct CacheProvider* provider;
    struct TaskRunner* runner;
    struct InvManager* invs;
    struct RS_MinimenuChatSource const* chat; /* NULL = no chat lines yet */

    /* Server-declared component events (IF_SETEVENTS). At rev 230 nothing is
     * clickable by default: a component the server never enabled produces no
     * menu row, however clickable it looks. A callback rather than the App
     * itself keeps this file free of the game layer, like `chat` above.
     * NULL = no server events, which is correct for the classic revisions. */
    /* sub_id is -1 for a component itself, or the dynamic/grid slot when a
     * ranged IF_SETEVENTS entry governs an object cell. */
    int (*events_for_component)(void* user, int com_id, int sub_id);
    void* events_user;

    /* Held-item / spell targeting mode (reference useMode/targetMode). */
    struct RS_MinimenuSelection selection;

    /* SET_PLAYER_OP rows for OPPLAYER1..5 text (NULL = no player ops). Points
     * at App::player_ops / player_ops_primary — five slots, index 0 = op 1. */
    char const (*player_ops)[40];
    int const* player_ops_primary;

    /* Controls-settings Attack options (enum RS_AttackOption, rs_attack_option.h).
     * These decide whether an "Attack" row is emitted at all and whether it may
     * be the left-click default. Callers that leave them zero get
     * RS_ATTACK_OPTION_DEPENDS, which is the pre-settings behaviour this file
     * shipped with and what the standalone tests want. */
    int player_attack_option;
    int npc_attack_option;
    /* enum ToriRS_AttackOptionModel (features/features.h). Zero — the classic
     * 2004 client, which has no such setting — is what the standalone tests
     * want, and it is the value that keeps the "Depends on combat levels" bump
     * inside the NPC attack pass instead of spreading it over every op. */
    int attack_option_model;

    /* Clan-channel membership test for RS_ATTACK_OPTION_CLAN (reference
     * ClientPlayer.isClanMember, which scans the four clan channels). NULL —
     * the state of this tree, which has no clan chat — means nobody is a
     * clanmate, so the option behaves as "Left-click where available". */
    bool (*is_clan_member)(void* user, char const* player_name);
    void* clan_user;

    /* World hittest results for this click (NULL/false = no world rows). The
     * pickset must have been refreshed at the click point by the caller. */
    struct World* world;
    struct World_PickSet const* world_pickset;
    bool click_in_world;

    /*
     * Destination for "Walk here" when the click hit no terrain at all — the
     * sky, the void outside an instance's floor, a tile the pick refuses.
     * Unset (the zero value) leaves the row inert, which is the reference
     * behaviour: such a click walks nowhere. Tile (0,0) is a legal scene tile,
     * so presence is carried by the flag, never by a sentinel coordinate.
     *
     * A resolved value rather than a callback because only the click paths pay
     * for it: the hover-text build runs this whole builder every frame and has
     * no use for the tile, so it leaves the flag false.
     */
    bool ground_fallback_valid;
    int ground_fallback_x;
    int ground_fallback_z;
    int ground_fallback_level;

    /* The loc editor dev tool (src/app.c, `app->locedit_visible`) wants a
     * disambiguated target rather than "first loc found on this tile" --
     * while it is open, every loc that already earns an Examine row also
     * earns a Select row (RS_MINIMENU_ACTION_LOCEDIT_SELECT, rs_minimenu_world.c),
     * intercepted client-side in app_minimenu_run_option before it can reach
     * the real OPLOC dispatch. False in every build path that predates the
     * tool, so this changes nothing when it is closed. */
    bool locedit_active;

    /* The map editor's SELECT tool (src/editor/editor_panel.h,
     * `app->editor_panel`) wants the same disambiguated-target rows
     * locedit_active earns above, but latches into the editor panel's own
     * sel_kind/sel_scene_x/z/sel_element_id rather than locedit_loc_id --
     * the two tools can be open together, on different subjects. Labeled by
     * loc SHAPE category ("Select Wall", "Select Ground Decor", ...) rather
     * than by name, since the map editor's tile tools care which layer they
     * would be affecting, not which loc it is. */
    bool mapedit_select_active;

    /*
     * The plugin lane's server is unreachable, so no "Manage Plugins" row is
     * offered at all.
     *
     * Every file the plugin system needs after the module itself -- the
     * manifest, each script it names, each shipped asset -- arrives over the
     * same transport as a cache read (TORIRS_IOK_SCRIPT, task_plugin_io.c).
     * With that transport down the panel can list nothing, load nothing and
     * save nothing, so a row that opens it is a row that leads to an empty
     * window and no explanation.
     *
     * Dropped here rather than refused in the dispatcher because the row is
     * AUTHORED: a profile puts `op0_action=PLUGIN_PANEL` on whatever component
     * it likes, so the client does not know which component to grey and cannot
     * reach it even if it did. Suppressing by ACTION covers every one of them,
     * on every gameframe, including the launcher this client builds itself --
     * and it takes the left-click default with it, since a row that is not
     * there cannot be chosen (RS_Minimenu_ActionIsDefaultable).
     *
     * Sense is deliberately "down", not "available": false is the pre-existing
     * behaviour, so every build path that predates this -- the standalone
     * minimenu tests included -- keeps offering the row exactly as it did.
     */
    bool plugin_io_down;
};

/* Custom, client-only minimenu action id: never sent to a server, and picked
 * well clear of both the real rev-254 action-id band (tops out ~1714, or
 * ~3714 deprioritized, revconfig.h) and the >1000 "deprioritized" bit
 * SortPriorityActions tests for, so a Select row sorts like any ordinary
 * option instead of sinking to the bottom.
 *
 * Derived from the ui/ constant rather than restated, because being at or
 * above it is what exempts the id from the +2000 priority bias — and an id
 * merely "well clear" of the reference band is NOT enough on its own: 500000
 * is >= 2000, so before that exemption existed the dispatcher's normalize step
 * turned this into 498000 and the Select row did nothing at all. */
#define RS_MINIMENU_ACTION_LOCEDIT_SELECT (UITREE_MINIMENU_ACTION_CLIENT_BASE + 0)

/* The same, for the GROUND rather than a loc. A tile is the other half of a
 * placement question — whether a loc looks wrong because it is on the wrong
 * square or because the square itself is a bridge deck is not answerable from
 * the loc alone — and the terrain pick is already in the set, so selecting one
 * costs a row rather than a second mechanism. */
#define RS_MINIMENU_ACTION_LOCEDIT_SELECT_TERRAIN (UITREE_MINIMENU_ACTION_CLIENT_BASE + 1)

/* The map editor SELECT tool's pair of the same two ids -- one action id
 * covers every loc shape category, since the row TEXT is what varies
 * ("Select Wall" vs "Select Ground Decor"), not the handler. */
#define RS_MINIMENU_ACTION_MAPEDIT_SELECT (UITREE_MINIMENU_ACTION_CLIENT_BASE + 2)
#define RS_MINIMENU_ACTION_MAPEDIT_SELECT_TERRAIN (UITREE_MINIMENU_ACTION_CLIENT_BASE + 3)

/*
 * A CLIENTOP_* row -- one the cache installed with 6700..6709 ("Mark tile",
 * "Tag"), run client-side and never sent to a server.
 *
 * ONE action id for the whole family, with the kind and slot carried in the
 * option's `action_index`: a per-(kind, slot) id would be thirty of them, and
 * the dispatcher does the same thing for every one -- look the slot up and run
 * its script. See RS_MINIMENU_CLIENTOP_INDEX.
 */
#define RS_MINIMENU_ACTION_CLIENTOP (UITREE_MINIMENU_ACTION_CLIENT_BASE + 4)

/*
 * "Manage Plugins": open the plugin window.
 *
 * Declared in revconfig.h rather than here, because unlike the four above it
 * is AUTHORED -- a profile writes `op0_action=PLUGIN_PANEL` on a component and
 * the parser has to turn that name into this number. The assertion is what
 * keeps the leaf header's literal and this band from drifting apart; without
 * it the id would land among the reference's, pick up the +2000 bias, and the
 * dispatcher's equality test would quietly stop matching.
 */
#define RS_MINIMENU_ACTION_PLUGIN_PANEL (UITREE_MINIMENU_ACTION_CLIENT_BASE + 5)
_Static_assert(
    RS_MINIMENU_ACTION_PLUGIN_PANEL == REVCONFIG_MINIMENU_PLUGIN_PANEL,
    "the profile's PLUGIN_PANEL id is not this client action");

/**
 * Set one chat filter to one named mode.
 *
 * `pick.id` is the button's component id, `secondary_id` the filter and
 * `tertiary_id` the mode. The row exists because the modern frames take the
 * LEFT click off these buttons -- there it opens and closes the chatbox -- and
 * a filter you can no longer cycle has to be reachable some other way. The
 * fixed frames keep the click and get these rows as well, which is no loss:
 * naming a mode beats stepping to it.
 */
#define RS_MINIMENU_ACTION_CHAT_FILTER (UITREE_MINIMENU_ACTION_CLIENT_BASE + 7)

_Static_assert(
    RS_MINIMENU_ACTION_CHAT_FILTER == REVCONFIG_MINIMENU_CHAT_FILTER,
    "the profile's CHAT_FILTER id is not this client action");

/*
 * A PLUGIN CANVAS REGION's row: "Toggle Run" on an orb a plugin drew.
 *
 * One action id for every region, with the region's index carried in the
 * option's `action_index` -- the same shape RS_MINIMENU_ACTION_CLIENTOP uses,
 * and for the same reason: the dispatcher does one thing for all of them, look
 * the region up and tell its owner.
 *
 * Distinct from the plugin MENU rows (torirs_plugin_host.c's own action base),
 * which are api->menu_add's, sit in a menu the game owns, and must never take
 * the default click from what is underneath them. A region has nothing
 * underneath it -- it is a rectangle the plugin drew and claimed -- so its row
 * is default-eligible, and having two ids is how that difference is stated.
 */
#define RS_MINIMENU_ACTION_PLUGIN_REGION (UITREE_MINIMENU_ACTION_CLIENT_BASE + 6)

/**
 * May this row be the LEFT-click default?
 *
 * The reference's rule is "an ordinary action sorts below 1000", and every
 * client-band id is far above it -- deliberately, so that a developer tool's
 * row can never become what a bare click does in the world. The plugin
 * launcher is the one client row that is not a tool but a BUTTON: it is the
 * only thing on the component it sits on, and a button that needs a
 * right-click to find is a button most people never find.
 *
 * Named rather than folded into the scan so that adding a second one is a
 * decision made here, once, next to the reason.
 */
static inline int
RS_Minimenu_ActionIsDefaultable(int action)
{
    return action < 1000 || action == RS_MINIMENU_ACTION_PLUGIN_PANEL ||
           action == RS_MINIMENU_ACTION_PLUGIN_REGION;
}

/** Pack a client op's (kind, slot) into a minimenu option's action_index, and
 *  read it back. `action_index` is otherwise the config op slot 0..4, which a
 *  client-op row does not have. */
#define RS_MINIMENU_CLIENTOP_INDEX(kind, slot) ((kind) * 16 + (slot))
#define RS_MINIMENU_CLIENTOP_KIND(index) ((index) / 16)
#define RS_MINIMENU_CLIENTOP_SLOT(index) ((index) % 16)

/** Build the full menu for a right click at (click_x, click_y): Cancel row,
 * per-hit-node rows (top-most component first), priority-sorted. */
void
RS_Minimenu_Build(
    struct RS_MinimenuBuildCtx const* ctx,
    int click_x,
    int click_y,
    struct UIMinimenu* out);

/** Default (left-click) entry after sorting: the top-most normal-priority row
 * (excluding Walk here / Examine / Cancel), else the Walk row, else -1
 * (caller falls back to the legacy click-hook path). Reference
 * chooseDefaultMenuEntry. */
int
RS_Minimenu_DefaultOptionIndex(struct UIMinimenu const* menu);

/** Minimenu action for a bare IF1 buttonType (OK/TOGGLE/SELECT/CONTINUE/
 * CLOSE) — the default action when a button is clicked outside a menu. */
int
RS_Minimenu_IfButtonActionForType(int button_type);

#endif
