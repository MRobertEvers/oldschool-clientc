#include "rs_minimenu_build.h"

#include "rs_minimenu_world.h"

#include "revconfig/revconfig.h"
#include "ui/uitree_input.h"
#include "ui/uitree_inv_view.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_obj_cell.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RS_MINIMENU_HIT_STACK_MAX 64

/* Client-TS ClientCode.ts social ranges (addSocialOptions); mirrors
 * v0/osrs/client_code.h. */
enum
{
    RS_CLIENT_CODE_FRIENDS_START = 1,
    RS_CLIENT_CODE_FRIENDS_END = 100,
    RS_CLIENT_CODE_FRIENDS_UPDATE_START = 101,
    RS_CLIENT_CODE_FRIENDS_UPDATE_END = 200,
    RS_CLIENT_CODE_IGNORES_START = 401,
    RS_CLIENT_CODE_IGNORES_END = 500,
    RS_CLIENT_CODE_FRIENDS2_START = 701,
    RS_CLIENT_CODE_FRIENDS2_UPDATE_START = 801,
    RS_CLIENT_CODE_FRIENDS2_UPDATE_END = 900,
};

static bool
client_code_is_friend_list_entry(int client_code)
{
    return (client_code >= RS_CLIENT_CODE_FRIENDS_START &&
            client_code <= RS_CLIENT_CODE_FRIENDS_UPDATE_END) ||
           (client_code >= RS_CLIENT_CODE_FRIENDS2_START &&
            client_code <= RS_CLIENT_CODE_FRIENDS2_UPDATE_END);
}

static bool
client_code_is_ignore_list_entry(int client_code)
{
    return client_code >= RS_CLIENT_CODE_IGNORES_START &&
           client_code <= RS_CLIENT_CODE_IGNORES_END;
}

static int
client_code_friend_slot_index(int client_code)
{
    if( client_code >= RS_CLIENT_CODE_FRIENDS2_UPDATE_START )
        return client_code - RS_CLIENT_CODE_FRIENDS2_START;
    if( client_code >= RS_CLIENT_CODE_FRIENDS2_START )
        return client_code - 601;
    if( client_code >= RS_CLIENT_CODE_FRIENDS_UPDATE_START )
        return client_code - RS_CLIENT_CODE_FRIENDS_UPDATE_START;
    if( client_code >= RS_CLIENT_CODE_FRIENDS_START &&
        client_code <= RS_CLIENT_CODE_FRIENDS_END )
        return client_code - 1;
    return -1;
}

static int
client_code_ignore_slot_index(int client_code)
{
    if( client_code_is_ignore_list_entry(client_code) )
        return client_code - RS_CLIENT_CODE_IGNORES_START;
    return -1;
}

static struct UIMinimenuPick
pick_ui(int component_id)
{
    struct UIMinimenuPick pick = { .kind = UI_MINIMENU_PICK_UI, .id = component_id };
    return pick;
}

static struct UIMinimenuPick
pick_inv_slot(int component_id, int slot, int obj_id, int obj_count)
{
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_INV_SLOT,
        .id = component_id,
        .secondary_id = slot,
        .tertiary_id = obj_id,
        /* Stack count carried for OPHELD6: a huge stack (>=100000) examines as
         * "<count> x <name>" instead of the desc (reference doAction OP_HELD6). */
        .quaternary_id = obj_count,
    };
    return pick;
}

/** "<verb> @lre@ <name>" — reference inv-item row formatting (orange name). */
static void
format_inv_item_option(char* out, size_t out_size, char const* verb, char const* obj_name)
{
    snprintf(out, out_size, "%s @lre@ %s", verb, obj_name);
}

/* The reference MiniMenuAction ids are NOT contiguous — OPHELD1..5 are
 * 694/962/795/681/100 and INV_BUTTON1..5 are 582/113/555/331/354 — so op
 * slot -> action id must go through a table. `OPHELD1 + op` silently built
 * ids (695..698) that matched no dispatch case: every obj op except op 0
 * (and the explicit Drop/Use/Examine rows) was a dead row — "Wield" did
 * nothing. */
static int const k_opheld_action[TORIRS_MENU_ACTION_SLOTS] = {
    REVCONFIG_MINIMENU_OPHELD1, REVCONFIG_MINIMENU_OPHELD2, REVCONFIG_MINIMENU_OPHELD3,
    REVCONFIG_MINIMENU_OPHELD4, REVCONFIG_MINIMENU_OPHELD5,
};
static int const k_inv_button_action[UITREE_MENU_OPTION_SLOTS] = {
    REVCONFIG_MINIMENU_INV_BUTTON1, REVCONFIG_MINIMENU_INV_BUTTON2,
    REVCONFIG_MINIMENU_INV_BUTTON3, REVCONFIG_MINIMENU_INV_BUTTON4,
    REVCONFIG_MINIMENU_INV_BUTTON5,
};

/*
 * Rows from a node's cache/script ops (op slot 4 down to 0, so op 1 lands on
 * top after the bottom-to-top draw). Action id = op_actions[i] when a script
 * assigned one, else the INV_BUTTON default for the slot. target_suffix is
 * appended to each verb (reference: "<op> <target>", e.g. "Withdraw-1
 * <col>Coins</col>"); NULL/empty for plain verbs. Port of v1
 * ui_click_add_menu_ops_rows + xrsps buildWidgetOpEntry labeling.
 */
static int
add_menu_ops_rows(
    struct UIMinimenu* menu,
    struct UITreeMenuOptions const* opts,
    struct UIMinimenuPick pick,
    char const* target_suffix)
{
    int const before = menu->option_count;
    char text[UITREE_MINIMENU_OPTION_LEN];

    for( int i = UITREE_MENU_OPTION_SLOTS - 1; i >= 0; i-- )
    {
        if( opts->ops[i][0] == '\0' )
            continue;

        if( target_suffix && target_suffix[0] != '\0' )
            snprintf(text, sizeof(text), "%s %s", opts->ops[i], target_suffix);
        else
            snprintf(text, sizeof(text), "%s", opts->ops[i]);

        {
            int action = k_inv_button_action[i];
            if( opts->op_actions[i] != 0 )
                action = opts->op_actions[i];
            UIMinimenu_AddOption(menu, text, action, i, pick);
        }
    }
    return menu->option_count - before;
}

/* Held-item ObjType-op rows for an inventory slot, gated on the component flags
 * (reference addComponentOptions, Client.ts:9936-9991): ops 5/4 (Drop default)
 * and ops 3..1 come from ObjType.iop and are shown only when the component has
 * `obj_ops` (IfType.objOps); "Use" only when `obj_use` (IfType.objUse). A shop's
 * sell grid decodes both false, so those rows vanish and only the grid's own iop
 * buttons (Value/Sell, added by the caller) + Examine remain. Insertion order
 * (op 4/3, Use, op 2..0) mirrors the reference so the primary op ends on top;
 * the caller emits the component iop rows and Examine after this, matching the
 * reference's trailing order. */
static void
add_inv_obj_rows(
    struct UIMinimenu* menu,
    struct UIMinimenuPick pick,
    struct ToriRS_Objtype const* obj,
    bool obj_ops,
    bool obj_use)
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";

    if( obj_ops )
    {
        for( int op = 4; op >= 3; op-- )
        {
            if( obj && obj->inv_actions[op][0] != '\0' )
            {
                format_inv_item_option(text, sizeof(text), obj->inv_actions[op], obj_name);
                UIMinimenu_AddOption(menu, text, k_opheld_action[op], op, pick);
            }
            else if( op == 4 )
            {
                format_inv_item_option(text, sizeof(text), "Drop", obj_name);
                UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPHELD5, 4, pick);
            }
        }
    }

    if( obj_use )
    {
        format_inv_item_option(text, sizeof(text), "Use", obj_name);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPHELDT_START, 0, pick);
    }

    if( obj_ops )
    {
        for( int op = 2; op >= 0; op-- )
        {
            if( obj && obj->inv_actions[op][0] != '\0' )
            {
                format_inv_item_option(text, sizeof(text), obj->inv_actions[op], obj_name);
                UIMinimenu_AddOption(menu, text, k_opheld_action[op], op, pick);
            }
        }
    }
}

/** Objtype for menu text; queue the load when not yet resident and return
 * NULL for now — hover text rebuilds every frame and the right-click menu
 * rebuilds on open, so the name self-heals a frame after the load lands
 * (callers already render an "item" placeholder for NULL). */
static struct ToriRS_Objtype const*
ensure_objtype(struct RS_MinimenuBuildCtx const* ctx, int obj_id)
{
    struct ToriRS_Objtype const* obj = CacheProvider_ObjtypeGet(ctx->provider, obj_id);
    if( obj )
        return obj;
    if( ctx->runner )
    {
        struct ToriRS_Task* task = CreateTask_ObjLoad(ctx->provider, obj_id);
        if( task )
            ToriRS_TaskQueue_Add(ctx->runner->queue, task);
    }
    return CacheProvider_ObjtypeGet(ctx->provider, obj_id);
}

/* "Use <held> with <this item>" / "<spell> <this item>" row for an inventory
 * slot while a select mode is armed (reference addComponentOptions useMode/
 * targetMode inv branch). Returns true when a mode was active (so the caller
 * skips the normal op rows). The reference skips the row for the very item that
 * armed use-mode. */
static bool
add_inv_slot_select_row(
    struct RS_MinimenuSelection const* sel,
    struct UIMinimenuPick pick,
    int com_id,
    int slot,
    char const* obj_name,
    struct UIMinimenu* menu)
{
    char text[UITREE_MINIMENU_OPTION_LEN];

    if( sel->mode == RS_MINIMENU_SELECT_USE_ITEM )
    {
        if( com_id == sel->obj_com_id && slot == sel->obj_slot )
            return true; /* can't use an item on itself */
        snprintf(
            text,
            sizeof(text),
            "Use %s with @lre@ %s",
            sel->obj_name[0] ? sel->obj_name : "item",
            obj_name);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_USEHELD_ONHELD, 0, pick);
        return true;
    }
    if( sel->mode == RS_MINIMENU_SELECT_TARGET )
    {
        if( (sel->target_mask & 0x10) == 0 )
            return true; /* spell does not target held items */
        snprintf(text, sizeof(text), "%s @lre@ %s", sel->target_op, obj_name);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_TGT_HELD, 0, pick);
        return true;
    }
    return false;
}

/*
 * Held-item rows for one item cell, whichever shape the tree expressed it in
 * (UITree_ObjCellForNode resolves both — see ui/uitree_obj_cell.h).
 *
 * The container's own iop buttons come with it — a shop grid's Value/Sell, the
 * worn tab's "Remove" — and `cell->obj_ops` says whether the ObjType's own
 * verbs join them. cell->ops_node_index is where those buttons live: itself
 * for a grid, the static parent for a rev-230 CS2 cell, because that is where
 * the paint script puts them. (It leaves a stray op4 "Read" on each backpack
 * *child*, which the real client hides behind the server's IF_SETEVENTS op
 * mask; reading the parent's ops instead of the child's is what keeps that row
 * out without needing the mask — docs/osrs230_mockserver.md §3.6.)
 */
static int
add_obj_cell_rows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UITreeObjCell const* cell,
    int obj_id,
    int obj_count,
    struct UIMinimenu* menu)
{
    struct UITreeMenuOptions component_ops =
        ctx->tree->components[cell->ops_node_index].menu_options;
    int const before = menu->option_count;
    struct ToriRS_Objtype const* obj = ensure_objtype(ctx, obj_id);
    struct UIMinimenuPick pick = pick_inv_slot(cell->component_id, cell->slot, obj_id, obj_count);
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";
    char suffix[UITREE_MINIMENU_OPTION_LEN];
    bool component_ops_armed = false;

    if( add_inv_slot_select_row(
            &ctx->selection, pick, cell->component_id, cell->slot, obj_name, menu) )
        return menu->option_count - before;

    /*
     * A CS2 cell's verbs are offered only where the server armed them.
     *
     * A grid keeps the cache's own answer, but a script-created cell has no
     * cache record and the strings on it are not evidence: rev 230's backpack
     * paint script leaves a fixed op4 "Read" on every slot regardless of what
     * is in it, while the bank's leaves genuine per-item rows on the same kind
     * of node. What separates them is the server's IF_SETEVENTS mask — the bank
     * arms its items component, the gameframe's backpack is never armed — which
     * is exactly the distinction the real client makes and the reason the stray
     * "Read" is invisible there.
     *
     * Without this the two containers cannot both be right: honouring the child
     * strings gives a bank its Withdraw ladder and costs the backpack its
     * Wear/Use/Drop, and ignoring them does the reverse.
     */
    if( cell->kind == UITREE_OBJ_CELL_DYNAMIC )
    {
        int const events =
            ctx->events_for_component ? ctx->events_for_component(ctx->events_user, cell->component_id) : 0;
        /* The three things that decide an item cell's rows, in one line —
         * which container the wire will name, and whether its verbs are live. */
        if( getenv("TORIRS_MINIMENU_DEBUG") )
            fprintf(
                stderr, "objcell: com=%d|%d events=0x%x ops_node=%d\n",
                (cell->component_id >> 16) & 0xFFFF, cell->component_id & 0xFFFF, events,
                (int)cell->ops_node_index);
        for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        {
            if( (events & (1 << (i + 1))) == 0 )
                component_ops.ops[i][0] = '\0';
            else if( component_ops.ops[i][0] != '\0' )
                component_ops_armed = true;
        }
    }
    else
    {
        component_ops_armed = true;
    }

    /* The ObjType's own verbs stand in when the container named none that are
     * live — the backpack's Wear/Eat/Drop, and "Use". */
    add_inv_obj_rows(
        menu,
        pick,
        obj,
        cell->obj_ops != 0 || !component_ops_armed,
        cell->obj_use != 0 || !component_ops_armed);
    snprintf(suffix, sizeof(suffix), "@lre@ %s", obj_name);
    /* Container's own iop buttons (a shop's Value/Sell 1/5/10, the worn tab's
     * Remove), then the always-present Examine — trailing order per reference
     * (Client.ts: 9993-10020). */
    add_menu_ops_rows(menu, &component_ops, pick, suffix);
    {
        char examine[UITREE_MINIMENU_OPTION_LEN];
        format_inv_item_option(examine, sizeof(examine), "Examine", obj_name);
        UIMinimenu_AddOption(menu, examine, REVCONFIG_MINIMENU_OPHELD6, 0, pick);
    }
    return menu->option_count - before;
}

/* Item cell under the click, of either shape. The grid's obj lives in
 * InvManager; a CS2 cell carries its own. */
static int
add_inv_slot_rows(
    struct RS_MinimenuBuildCtx const* ctx,
    int32_t node_idx,
    int click_x,
    int click_y,
    struct UIMinimenu* menu)
{
    struct UITreeObjCell cell;

    if( !UITree_ObjCellForNode(ctx->tree, node_idx, click_x, click_y, &cell) )
        return 0;

    if( cell.kind == UITREE_OBJ_CELL_GRID )
    {
        struct InvSlot inv_slot;
        if( !InvManager_GetSlot(ctx->invs, cell.inv_source_id, cell.slot, &inv_slot) )
            return 0;
        if( inv_slot.obj_id <= 0 )
            return 0;
        return add_obj_cell_rows(ctx, &cell, inv_slot.obj_id, inv_slot.obj_count, menu);
    }

    return add_obj_cell_rows(ctx, &cell, cell.obj_id, cell.obj_count, menu);
}

/* Friend/ignore list entries by client_code. src/main has no chat/social
 * state yet, so the display name comes from the node's own text label
 * (v1's ignore path); rows are skipped when the label is empty. */
static bool
add_social_rows(
    struct UITreeComponent const* node,
    struct UIMinimenu* menu)
{
    int const client_code = node->behavior.client_code;
    char const* label = NULL;
    char text[UITREE_MINIMENU_OPTION_LEN];

    if( node->type == UIELEM_RS_TEXT && node->u.rs_text.text )
        label = node->u.rs_text.text;
    if( !label || label[0] == '\0' )
        return false;

    if( client_code_is_friend_list_entry(client_code) )
    {
        int const slot = client_code_friend_slot_index(client_code);
        struct UIMinimenuPick pick = pick_ui(node->component_id);
        pick.secondary_id = slot;

        snprintf(text, sizeof(text), "Remove @whi@%s", label);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_FRIENDLIST_DEL, slot, pick);
        snprintf(text, sizeof(text), "Message @whi@%s", label);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_MESSAGE_PRIVATE, slot, pick);
        return true;
    }

    if( client_code_is_ignore_list_entry(client_code) )
    {
        int const slot = client_code_ignore_slot_index(client_code);
        struct UIMinimenuPick pick = pick_ui(node->component_id);
        pick.secondary_id = slot;

        snprintf(text, sizeof(text), "Remove @whi@%s", label);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_IGNORELIST_DEL, slot, pick);
        return true;
    }

    return false;
}

/* "%s" -> sender substitution for chat social-op templates (port of v1
 * ui_chat_minimenu_format_template). */
static void
format_chat_template(char* out, size_t out_len, char const* tmpl, char const* sender)
{
    char const* pct;

    assert(out);
    if( out_len == 0 )
        return;
    out[0] = '\0';
    if( !tmpl || tmpl[0] == '\0' )
        return;

    pct = strstr(tmpl, "%s");
    if( !pct )
    {
        snprintf(out, out_len, "%s", tmpl);
        return;
    }

    {
        size_t copy_len = (size_t)(pct - tmpl);
        if( copy_len >= out_len )
            copy_len = out_len - 1;
        memcpy(out, tmpl, copy_len);
        out[copy_len] = '\0';
        snprintf(out + copy_len, out_len - copy_len, "%s", sender ? sender : "");
        if( pct[2] != '\0' )
        {
            size_t cur = strlen(out);
            snprintf(out + cur, out_len - cur, "%s", pct + 2);
        }
    }
}

static void
add_chat_template_row(
    struct UIMinimenu* menu,
    char const* tmpl,
    int action,
    int deprioritize,
    char const* sender)
{
    char text[UITREE_MINIMENU_OPTION_LEN];

    if( !tmpl || tmpl[0] == '\0' || action == 0 )
        return;
    format_chat_template(text, sizeof(text), tmpl, sender);
    if( text[0] == '\0' )
        return;
    if( deprioritize )
        action = UIMinimenu_ActionDeprioritize(action);
    UIMinimenu_AddOption(
        menu, text, action, -1, (struct UIMinimenuPick){ .kind = UI_MINIMENU_PICK_NONE });
}

/* Chat panel node: resolve the chat line under the click through the chat
 * source seam and add its social template rows (report gated on staff level,
 * which does not exist yet — v1 ui_chat_minimenu_add_social_rows). No chat
 * source (today) -> no rows. */
static int
add_chat_rows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UITreeComponent const* node,
    int click_x,
    int click_y,
    struct UIMinimenu* menu)
{
    struct UITreeChatMinimenuConfig const* config = &node->u.chat.minimenu;
    char sender[UITREE_MINIMENU_OPTION_LEN];
    int chat_type = 0;
    int const before = menu->option_count;

    if( !ctx->chat || !ctx->chat->line_at )
        return 0;
    if( !ctx->chat->line_at(ctx->chat->user, click_x, click_y, sender, sizeof(sender), &chat_type) )
        return 0;
    if( sender[0] == '\0' )
        return 0;

    /* Staff-only report row omitted (staff level fixed at 0 until chat/social
     * state exists). Social adds are deprioritized like the reference private
     * strip so they sit under the line's primary rows. */
    add_chat_template_row(
        menu, config->op_add_ignore, config->op_add_ignore_action, 1, sender);
    add_chat_template_row(
        menu, config->op_add_friend, config->op_add_friend_action, 1, sender);
    return menu->option_count - before;
}

static int
if_button_action_for_type(int button_type)
{
    switch( button_type )
    {
    case REVCONFIG_BUTTON_TYPE_TOGGLE:
        return REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE;
    case REVCONFIG_BUTTON_TYPE_SELECT:
        return REVCONFIG_MINIMENU_IF_BUTTON_SELECT;
    case REVCONFIG_BUTTON_TYPE_CONTINUE:
        return REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON;
    case REVCONFIG_BUTTON_TYPE_CLOSE:
        return REVCONFIG_MINIMENU_CLOSE_MODAL;
    case REVCONFIG_BUTTON_TYPE_OK:
    default:
        return REVCONFIG_MINIMENU_IF_BUTTON;
    }
}

int
RS_Minimenu_IfButtonActionForType(int button_type)
{
    return if_button_action_for_type(button_type);
}

/* "<verb> @gre@<base>" spell/prayer row for a BUTTON_TARGET component when no
 * mode is armed (reference addComponentOptions BUTTON_TARGET branch, targetMode
 * === 0). Clicking it arms target mode. The verb is the first word of
 * targetVerb (reference splits on the first space). */
static bool
add_target_button_row(
    struct UITreeComponent const* node,
    struct UIMinimenu* menu)
{
    struct UITreeMenuOptions const* opts = &node->menu_options;
    struct UIMinimenuPick pick = pick_ui(node->component_id);
    char text[UITREE_MINIMENU_OPTION_LEN];
    char verb[UITREE_MENU_OPTION_LEN];
    char const* space;

    if( node->behavior.button_type != REVCONFIG_BUTTON_TYPE_TARGET )
        return false;
    if( opts->target_verb[0] == '\0' )
        return false;

    snprintf(verb, sizeof(verb), "%s", opts->target_verb);
    space = strchr(verb, ' ');
    if( space )
        verb[space - verb] = '\0';

    snprintf(text, sizeof(text), "%s @gre@%s", verb, opts->target_base);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_TGT_BUTTON, 0, pick);
    return true;
}

/* Generic component rows: social first, else op rows (with the component's
 * option label as the row target — reference "<op> <target>") or, for
 * op-less buttons, a single label row. Port of v1
 * ui_click_add_component_menu_rows with xrsps target labeling. */
/*
 * Bit 0 of an IF_SETEVENTS mask: the component accepts a plain click, which the
 * client answers with IF_BUTTON. It is what makes a dialogue's "Click here to
 * continue" live — the cache marks the component clickable, but at rev 230 only
 * the server decides whether it currently does anything.
 */
#define RS_MINIMENU_EVENT_CLICK 0x1

static int
add_component_rows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UITreeComponent const* node,
    enum RS_MinimenuSelectMode select_mode,
    struct UIMinimenu* menu)
{
    int const before = menu->option_count;
    struct UITreeMenuOptions const* opts = &node->menu_options;
    struct UIMinimenuPick const pick = pick_ui(node->component_id);
    int ops_added;
    char const* label = NULL;

    if( add_social_rows(node, menu) )
        return menu->option_count - before;

    /* A spell/prayer button turns into a "Cast <spell>" target-select row
     * rather than its op rows — but only when nothing is already armed
     * (reference gates the BUTTON_TARGET branch on targetMode === 0). */
    if( select_mode == RS_MINIMENU_SELECT_NONE && add_target_button_row(node, menu) )
        return menu->option_count - before;

    ops_added = add_menu_ops_rows(
        menu, opts, pick, opts->option[0] != '\0' ? opts->option : NULL);
    if( ops_added > 0 )
        return menu->option_count - before;

    if( opts->option[0] != '\0' )
        label = opts->option;
    else if( node->behavior.button_type == REVCONFIG_BUTTON_TYPE_CLOSE )
        label = "Close";
    else if(
        node->behavior.button_type == REVCONFIG_BUTTON_TYPE_OK &&
        node->behavior.client_code == 0 )
        label = "Ok";

    if( label )
    {
        int action = opts->option_action != 0
                         ? opts->option_action
                         : if_button_action_for_type(node->behavior.button_type);
        UIMinimenu_AddOption(menu, label, action, 0, pick);
        return menu->option_count - before;
    }

    /* Nothing in the cache described this component as a button, but the server
     * may have enabled it at run time. That is the normal case at rev 230 for
     * anything a script drives — a dialogue's continue prompt carries
     * button_type 0 and no menu ops, and is live only because IF_SETEVENTS said
     * so. The component's own text is the label the player already sees. */
    if( ctx->events_for_component )
    {
        int events = ctx->events_for_component(ctx->events_user, node->component_id);

        if( events & RS_MINIMENU_EVENT_CLICK )
        {
            /* A text component's own string is the label the player is
             * already reading ("Click here to continue"); anything else falls
             * back to a generic verb. */
            char const* text = (node->type == UIELEM_RS_TEXT && node->u.rs_text.text &&
                                node->u.rs_text.text[0] != '\0')
                                   ? node->u.rs_text.text
                                   : "Continue";

            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_IF_BUTTON, 0, pick);
        }
    }

    return menu->option_count - before;
}

void
RS_Minimenu_Build(
    struct RS_MinimenuBuildCtx const* ctx,
    int click_x,
    int click_y,
    struct UIMinimenu* out)
{
    int32_t hits[RS_MINIMENU_HIT_STACK_MAX];
    int hit_count;
    int const font_id = out->font_id;

    assert(ctx && ctx->tree && out);

    UIMinimenu_Reset(out);
    out->font_id = font_id;
    UIMinimenu_AddOption(
        out,
        "Cancel",
        REVCONFIG_MINIMENU_CANCEL,
        -1,
        (struct UIMinimenuPick){ .kind = UI_MINIMENU_PICK_NONE });

    RS_Minimenu_AddWorldRows(ctx, out);

    hit_count =
        UITree_CollectNodesAt(ctx->tree, ctx->ui_host, click_x, click_y, hits, RS_MINIMENU_HIT_STACK_MAX);

    /* A cell's rows are emitted for the cell, and the container it borrowed
     * its verbs from must not emit them a second time on its own account —
     * the worn tab would otherwise offer "Remove" twice, once as an inventory
     * op and once as a plain CS2 button. Collected up front because the hits
     * are walked parent-first below, so the container is reached before the
     * child that claims it. */
    int32_t claimed[RS_MINIMENU_HIT_STACK_MAX];
    int claimed_count = 0;
    for( int i = 0; i < hit_count; i++ )
    {
        struct UITreeObjCell cell;
        if( UITree_ObjCellForNode(ctx->tree, hits[i], click_x, click_y, &cell) &&
            cell.ops_node_index != hits[i] )
            claimed[claimed_count++] = cell.ops_node_index;
    }

    /* Bottom-most node first: later-inserted rows draw higher, so the
     * top-most component's rows land on top of the menu. */
    for( int i = hit_count - 1; i >= 0; i-- )
    {
        struct UITreeComponent const* node = &ctx->tree->components[hits[i]];
        bool is_claimed = false;

        /* An item cell wins over its own component rows: a rev-230 backpack
         * slot is a plain dynamic RS_GRAPHIC, so the generic path would claim
         * it and dispatch a CS2 hook instead of sending an inventory op. */
        if( add_inv_slot_rows(ctx, hits[i], click_x, click_y, out) > 0 )
            continue;
        if( node->type == UIELEM_RS_INV )
            continue;
        for( int j = 0; j < claimed_count; j++ )
            is_claimed = is_claimed || claimed[j] == hits[i];
        if( is_claimed )
            continue;
        if( node->type == UIELEM_BUILTIN_CHAT )
            add_chat_rows(ctx, node, click_x, click_y, out);
        else
            add_component_rows(ctx, node, ctx->selection.mode, out);
    }

    UIMinimenu_SortPriorityActions(out);
}

int
RS_Minimenu_DefaultOptionIndex(struct UIMinimenu const* menu)
{
    int walk = -1;

    assert(menu);
    /* Scan from the top row down (reference chooseDefaultMenuEntry: first
     * non-deprioritized entry excluding walk/examine/cancel, else walk). In
     * the rev-254 id scheme every deprioritized/system row is > 1000. */
    for( int i = menu->option_count - 1; i >= 0; i-- )
    {
        int const action = menu->options[i].action;
        if( action == REVCONFIG_MINIMENU_WALK )
        {
            if( walk < 0 )
                walk = i;
            continue;
        }
        if( action < 1000 )
            return i;
    }
    return walk;
}
