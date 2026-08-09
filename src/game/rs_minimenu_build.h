#ifndef SRC_RS_MINIMENU_BUILD_H
#define SRC_RS_MINIMENU_BUILD_H

#include "engine/cache_provider.h"
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
};

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
