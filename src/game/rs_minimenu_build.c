#include "rs_minimenu_build.h"

#include "revconfig/revconfig.h"
#include "ui/uitree_input.h"
#include "ui/uitree_inv_view.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
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
pick_inv_slot(int component_id, int slot, int obj_id)
{
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_INV_SLOT,
        .id = component_id,
        .secondary_id = slot,
        .tertiary_id = obj_id,
    };
    return pick;
}

/** "<verb> @lre@ <name>" — reference inv-item row formatting (orange name). */
static void
format_inv_item_option(char* out, size_t out_size, char const* verb, char const* obj_name)
{
    snprintf(out, out_size, "%s @lre@ %s", verb, obj_name);
}

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
            int action = REVCONFIG_MINIMENU_INV_BUTTON1 + i;
            if( opts->op_actions[i] != 0 )
                action = opts->op_actions[i];
            UIMinimenu_AddOption(menu, text, action, i, pick);
        }
    }
    return menu->option_count - before;
}

/* Held-item rows for an inventory slot: ops 5/4 (Drop default), Use, ops 3..1,
 * Examine — insertion order mirrors v1 ui_click_add_inv_obj_options so the
 * primary op ends on top. */
static void
add_inv_obj_rows(
    struct UIMinimenu* menu,
    struct UIMinimenuPick pick,
    struct ToriRS_Objtype const* obj)
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";

    for( int op = 4; op >= 3; op-- )
    {
        if( obj && obj->inv_actions[op][0] != '\0' )
        {
            format_inv_item_option(text, sizeof(text), obj->inv_actions[op], obj_name);
            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPHELD1 + op, op, pick);
        }
        else if( op == 4 )
        {
            format_inv_item_option(text, sizeof(text), "Drop", obj_name);
            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPHELD5, 4, pick);
        }
    }

    format_inv_item_option(text, sizeof(text), "Use", obj_name);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPHELDT_START, 0, pick);

    for( int op = 2; op >= 0; op-- )
    {
        if( obj && obj->inv_actions[op][0] != '\0' )
        {
            format_inv_item_option(text, sizeof(text), obj->inv_actions[op], obj_name);
            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPHELD1 + op, op, pick);
        }
    }

    format_inv_item_option(text, sizeof(text), "Examine", obj_name);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPHELD6, 0, pick);
}

/** Objtype for menu text; queue-then-drain when not yet resident (same
 * pattern as app_sync_textures). */
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
        {
            ToriRS_TaskQueue_Add(ctx->runner->queue, task);
            TaskRunner_Drain(ctx->runner);
        }
    }
    return CacheProvider_ObjtypeGet(ctx->provider, obj_id);
}

/* Inventory grid node: resolve the slot under the click (content coords fold
 * ancestor scroll back in) and emit held-item + component-op rows. */
static int
add_inv_slot_rows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UITreeComponent const* node,
    int32_t node_idx,
    int click_x,
    int click_y,
    struct UIMinimenu* menu)
{
    struct UITreeInvGridLayout layout;
    int bx = 0, by = 0, bw = 0, bh = 0;
    int offx = 0, offy = 0;
    int slot;
    struct InvSlot inv_slot;

    layout.cols = node->u.rs_inv.cols;
    layout.rows = node->u.rs_inv.rows;
    layout.margin_x = node->u.rs_inv.margin_x;
    layout.margin_y = node->u.rs_inv.margin_y;
    layout.offset_x = node->u.rs_inv.inv_slot_offset_x;
    layout.offset_y = node->u.rs_inv.inv_slot_offset_y;

    UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
    UITree_AccumScrollOffset(ctx->tree, node_idx, &offx, &offy);
    slot = UITree_InvViewGridHitTest(bx, by, &layout, click_x + offx, click_y + offy);
    if( slot < 0 )
        return 0;
    if( !InvManager_GetSlot(ctx->invs, node->u.rs_inv.inv_source_id, slot, &inv_slot) )
        return 0;
    if( inv_slot.obj_id <= 0 )
        return 0;

    {
        int const before = menu->option_count;
        struct ToriRS_Objtype const* obj = ensure_objtype(ctx, inv_slot.obj_id);
        struct UIMinimenuPick pick =
            pick_inv_slot(node->component_id, slot, inv_slot.obj_id);
        char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";
        char suffix[UITREE_MINIMENU_OPTION_LEN];

        add_inv_obj_rows(menu, pick, obj);
        snprintf(suffix, sizeof(suffix), "@lre@ %s", obj_name);
        add_menu_ops_rows(menu, &node->menu_options, pick, suffix);
        return menu->option_count - before;
    }
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

/* Generic component rows: social first, else op rows (with the component's
 * option label as the row target — reference "<op> <target>") or, for
 * op-less buttons, a single label row. Port of v1
 * ui_click_add_component_menu_rows with xrsps target labeling. */
static int
add_component_rows(
    struct UITreeComponent const* node,
    struct UIMinimenu* menu)
{
    int const before = menu->option_count;
    struct UITreeMenuOptions const* opts = &node->menu_options;
    struct UIMinimenuPick const pick = pick_ui(node->component_id);
    int ops_added;
    char const* label = NULL;

    if( add_social_rows(node, menu) )
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

    /* World pickset rows insert here once world hit-testing exists
     * (v1 ui_click_append_world_options_to_menu: Walk here + NPC/loc rows). */

    hit_count =
        UITree_CollectNodesAt(ctx->tree, ctx->ui_host, click_x, click_y, hits, RS_MINIMENU_HIT_STACK_MAX);

    /* Bottom-most node first: later-inserted rows draw higher, so the
     * top-most component's rows land on top of the menu. */
    for( int i = hit_count - 1; i >= 0; i-- )
    {
        struct UITreeComponent const* node = &ctx->tree->components[hits[i]];
        if( node->type == UIELEM_RS_INV )
            add_inv_slot_rows(ctx, node, hits[i], click_x, click_y, out);
        else if( node->type == UIELEM_BUILTIN_CHAT )
            add_chat_rows(ctx, node, click_x, click_y, out);
        else
            add_component_rows(node, out);
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
