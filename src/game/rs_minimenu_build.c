#include "rs_minimenu_build.h"

#include "rs_minimenu_world.h"
#include "rs_ui_slots.h"

#include "revconfig/revconfig.h"
#include "ui/torirs_chrome_exec.h"
#include "ui/uitree_input.h"
#include "ui/uitree_inv_view.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_obj_cell.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* Row text below is display-only (dispatch runs on the separate action/pick
 * fields) and safely clipped by snprintf if a name/verb is unusually long, so
 * GCC's worst-case-from-declared-array-bounds truncation math is noise here. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

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

/** "<verb> @lre@<name>" — reference inv-item row formatting (orange name).
 * The colour tag has zero rendered width, so a space on both sides would draw
 * as two spaces between the verb and name. */
static void
format_inv_item_option(char* out, size_t out_size, char const* verb, char const* obj_name)
{
    snprintf(out, out_size, "%s @lre@%s", verb, obj_name);
}

/* ObjType's revision-defined fifth inventory action. The cache omits this
 * default; both the legacy TYPE_INV builder and rev239's scripted backpack
 * must therefore draw it from the same client-owned label. */
static char const* const k_default_drop_verb = "Drop";

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
/* Component ops on IF_SETEVENTS-armed grids (bank, farming tools, …) all
 * ride IF_BUTTON1..10 — the classic INV_BUTTON family stops at five on the
 * wire, and modern CS2 banks use one numbered family for the whole ladder. */
static int const k_inv_button_action[UITREE_MENU_OPTION_SLOTS] = {
    REVCONFIG_MINIMENU_IF_BUTTON, REVCONFIG_MINIMENU_IF_BUTTON,
    REVCONFIG_MINIMENU_IF_BUTTON,  REVCONFIG_MINIMENU_IF_BUTTON,
    REVCONFIG_MINIMENU_IF_BUTTON,  REVCONFIG_MINIMENU_IF_BUTTON,
    REVCONFIG_MINIMENU_IF_BUTTON,  REVCONFIG_MINIMENU_IF_BUTTON,
    REVCONFIG_MINIMENU_IF_BUTTON,  REVCONFIG_MINIMENU_IF_BUTTON,
};
/*
 * The same ops, when the component under them is a legacy TYPE_INV container.
 *
 * Those are a family of their own on the wire — INV_BUTTON1..5, one opcode per
 * slot (Client.ts:9772-9795, `if (child.iop)`) — and the pre-237 protocol has
 * no IF_BUTTON<n> at all. Sending the worn tab's "Remove" through the
 * IF_BUTTON ladder therefore reached net_out_if_button_op with a name no lc254
 * or lc289 table carries, which resolves to -1, which is how net_out declines
 * to send: the menu row was built, the click was consumed, and the helmet
 * stayed on with nothing on the socket to explain it.
 *
 * Slots 5..9 keep IF_BUTTON. The classic family stops at five and a container
 * that names a sixth op is by construction a CS2 one, where the numbered
 * ladder is the right answer.
 */
static int const k_container_iop_action[UITREE_MENU_OPTION_SLOTS] = {
    REVCONFIG_MINIMENU_INV_BUTTON1, REVCONFIG_MINIMENU_INV_BUTTON2,
    REVCONFIG_MINIMENU_INV_BUTTON3, REVCONFIG_MINIMENU_INV_BUTTON4,
    REVCONFIG_MINIMENU_INV_BUTTON5, REVCONFIG_MINIMENU_IF_BUTTON,
    REVCONFIG_MINIMENU_IF_BUTTON,   REVCONFIG_MINIMENU_IF_BUTTON,
    REVCONFIG_MINIMENU_IF_BUTTON,   REVCONFIG_MINIMENU_IF_BUTTON,
};

/*
 * Rows from a node's cache/script ops (op slot 4 down to 0, so op 1 lands on
 * top after the bottom-to-top draw). Action id = op_actions[i] when a script
 * assigned one, else `default_actions[i]` — which family the click belongs to
 * is the CALLER's knowledge, not this loop's (k_inv_button_action for a plain
 * widget, k_container_iop_action for an item container's own buttons).
 * target_suffix is appended to each verb (reference: "<op> <target>", e.g.
 * "Withdraw-1 <col>Coins</col>"); NULL/empty for plain verbs. Port of v1
 * ui_click_add_menu_ops_rows + xrsps buildWidgetOpEntry labeling.
 */
static int
add_menu_ops_rows(
    struct UIMinimenu* menu,
    struct UITreeMenuOptions const* opts,
    struct UIMinimenuPick pick,
    char const* target_suffix,
    int const* default_actions)
{
    int const before = menu->option_count;
    char text[UITREE_MINIMENU_OPTION_LEN];

    assert(menu);
    assert(opts);
    assert(default_actions);

    for( int i = UITREE_MENU_OPTION_SLOTS - 1; i >= 0; i-- )
    {
        if( opts->ops[i][0] == '\0' )
            continue;

        if( target_suffix && target_suffix[0] != '\0' )
            snprintf(text, sizeof(text), "%s %s", opts->ops[i], target_suffix);
        else
            snprintf(text, sizeof(text), "%s", opts->ops[i]);

        {
            int action = default_actions[i];
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
                format_inv_item_option(text, sizeof(text), k_default_drop_verb, obj_name);
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
 * armed use-mode, and for cells whose IF_SETEVENTS lack bit 21 (usable-on). */
static bool
add_inv_slot_select_row(
    struct RS_MinimenuSelection const* sel,
    struct UIMinimenuPick pick,
    int com_id,
    int slot,
    char const* obj_name,
    int events,
    struct UIMinimenu* menu)
{
    char text[UITREE_MINIMENU_OPTION_LEN];

    if( sel->mode == RS_MINIMENU_SELECT_USE_ITEM )
    {
        if( com_id == sel->obj_com_id && slot == sel->obj_slot )
            return true; /* can't use an item on itself */
        /* Deob method2195: (events >> 21 & 1) != 0. events < 0 means no
         * IF_SETEVENTS store (CS1) — every cell is usable-on. */
        if( events >= 0 && (events & UITREE_FLAG_USEABLE_ON) == 0 )
            return true;
        snprintf(
            text,
            sizeof(text),
            "Use %s with @lre@%s",
            sel->obj_name[0] ? sel->obj_name : "item",
            obj_name);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_USEHELD_ONHELD, 0, pick);
        return true;
    }
    if( sel->mode == RS_MINIMENU_SELECT_TARGET )
    {
        int const held_bit = sel->target_mask_held_bit != 0 ? sel->target_mask_held_bit
                                                           : TORIRS_TARGET_MASK_HELD_CLASSIC;
        if( (sel->target_mask & held_bit) == 0 )
            return true; /* spell does not target held items */
        snprintf(text, sizeof(text), "%s @lre@%s", sel->target_op, obj_name);
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
        *UITree_MenuOptions(&ctx->tree->components[cell->ops_node_index]);
    int const before = menu->option_count;
    struct ToriRS_Objtype const* obj = ensure_objtype(ctx, obj_id);
    struct UIMinimenuPick pick = pick_inv_slot(cell->component_id, cell->slot, obj_id, obj_count);
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";
    char suffix[UITREE_MINIMENU_OPTION_LEN];
    int const events = ctx->events_for_component
                           ? ctx->events_for_component(
                                 ctx->events_user, cell->component_id, cell->slot)
                           : -1;

    if( add_inv_slot_select_row(
            &ctx->selection, pick, cell->component_id, cell->slot, obj_name, events, menu) )
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
        int const ev = events < 0 ? 0 : events;
        struct UITreeComponent const* ops_node =
            &ctx->tree->components[cell->ops_node_index];
        struct UITreeRuntimeHooks const* hooks = UITree_Hooks(ops_node);
        int const has_on_op = hooks->on_op.script_id > 0;
        char const* target_verb = component_ops.target_verb;
        int target_priority = ops_node->target_priority;
        int32_t target_ancestor = ops_node->parent;
        while( target_verb[0] == '\0' && target_ancestor >= 0 )
        {
            struct UITreeComponent const* ancestor =
                &ctx->tree->components[target_ancestor];
            target_verb = UITree_MenuOptions(ancestor)->target_verb;
            target_priority = ancestor->target_priority;
            target_ancestor = ancestor->parent;
        }
        /* The three things that decide an item cell's rows, in one line —
         * which container the wire will name, and whether its verbs are live. */
        if( getenv("TORIRS_MINIMENU_DEBUG") )
            TORIRS_LOG("objcell: com=%d|%d events=0x%x ops_node=%d onop=%d target=\"%s\"\n",
                (cell->component_id >> 16) & 0xFFFF, cell->component_id & 0xFFFF, ev,
                (int)cell->ops_node_index, has_on_op, target_verb);
        if( !has_on_op )
        {
            for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
            {
                if( (ev & (1 << (i + 1))) == 0 )
                    component_ops.ops[i][0] = '\0';
            }
        }

        /* Rev239 ObjType initializes iop5 to the localized default before it
         * decodes the cache record. Our neutral cache record contains only
         * authored opcodes, so reproduce the script-visible default in the
         * backpack's numbered ladder (op 7), without synthesizing the rest of
         * the ObjType rows. The target verb identifies this item-use container;
         * bank/worn children have their own component operation vocabulary. */
        if( target_verb[0] != '\0' && obj &&
            obj->inv_actions[4][0] == '\0' && component_ops.ops[6][0] == '\0' )
        {
            snprintf(
                component_ops.ops[6], sizeof(component_ops.ops[6]), "%s",
                k_default_drop_verb);
        }

        /* Official method5229 walks op slots high-to-low and inserts the target
         * verb immediately before the operation at component.targetPriority.
         * Since insertion order is drawn bottom-to-top, this puts Use directly
         * below that operation on screen. method12079 still gates the row on a
         * non-empty verb and nonzero target mask (flags bits 11..16). */
        snprintf(suffix, sizeof(suffix), "@lre@%s", obj_name);
        for( int i = UITREE_MENU_OPTION_SLOTS - 1; i >= 0; i-- )
        {
            if( i == target_priority && target_verb[0] != '\0' &&
                ((unsigned)ev >> 11 & 0x3Fu) != 0 )
            {
                char target_text[UITREE_MINIMENU_OPTION_LEN];
                format_inv_item_option(
                    target_text, sizeof(target_text), target_verb, obj_name);
                UIMinimenu_AddOption(
                    menu, target_text, REVCONFIG_MINIMENU_OPHELDT_START, 0, pick);
            }
            if( component_ops.ops[i][0] != '\0' )
            {
                char op_text[UITREE_MINIMENU_OPTION_LEN];
                int action = component_ops.op_actions[i] != 0
                                 ? component_ops.op_actions[i]
                                 : k_inv_button_action[i];
                if( i > target_priority )
                    action = UIMinimenu_ActionDeprioritize(action);
                snprintf(
                    op_text, sizeof(op_text), "%s %s", component_ops.ops[i], suffix);
                UIMinimenu_AddOption(menu, op_text, action, i, pick);
            }
        }
        return menu->option_count - before;
    }

    /* Legacy TYPE_INV grids own their actions through ObjType + cache flags. */
    add_inv_obj_rows(
        menu,
        pick,
        obj,
        cell->obj_ops != 0,
        cell->obj_use != 0);
    snprintf(suffix, sizeof(suffix), "@lre@%s", obj_name);
    /* Container's own iop buttons (a shop's Value/Sell 1/5/10, the worn tab's
     * Remove), then the always-present Examine — trailing order per reference
     * (Client.ts: 9993-10020). */
    add_menu_ops_rows(menu, &component_ops, pick, suffix, k_container_iop_action);
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
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_FRIENDLIST_DEL, -1, pick);
        snprintf(text, sizeof(text), "Message @whi@%s", label);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_MESSAGE_PRIVATE, -1, pick);
        return true;
    }

    if( client_code_is_ignore_list_entry(client_code) )
    {
        int const slot = client_code_ignore_slot_index(client_code);
        struct UIMinimenuPick pick = pick_ui(node->component_id);
        pick.secondary_id = slot;

        snprintf(text, sizeof(text), "Remove @whi@%s", label);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_IGNORELIST_DEL, -1, pick);
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

    assert(tmpl);
    if( tmpl[0] == '\0' || action == 0 )
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
    struct UITreeChatMinimenuConfig const* config = &UITree_Chat(node)->minimenu;
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

/*
 * The modes one privacy button can be put into, as menu rows.
 *
 * Named rather than stepped: "Public chat: Friends" says which mode it means,
 * and the same row means the same thing wherever the filter happens to be.
 * That is the whole reason these exist -- the modern frames give the button's
 * left click to the chatbox switch, so cycling is no longer available, and a
 * setting with no gesture left is a setting you cannot change.
 *
 * A filter that does not cycle contributes nothing. Report abuse is one: it is
 * a click-through to the report interface and has no modes to choose between,
 * so its right click offers what any other component's does and no more.
 */
static int
add_chat_button_rows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UITreeComponent const* node,
    struct UIMinimenu* menu)
{
    (void)ctx;
    struct UITreeChatButtonConfig const* cfg = UITree_ChatButton(node);
    int const filter = (int)cfg->filter;
    int const modes = RS_UISlots_ChatFilterModeCount(filter);
    int const before = menu->option_count;

    assert(ctx);
    assert(node);
    assert(menu);
    if( modes <= 1 )
        return 0;

    /*
     * Highest mode FIRST, because a later row draws higher: walking down puts
     * On at the top of the menu, which is the order the button itself cycles
     * in and the order the labels read in the ini.
     */
    for( int mode = modes - 1; mode >= 0; mode-- )
    {
        char text[UITREE_MINIMENU_OPTION_LEN];

        if( !cfg->mode_label[mode][0] )
            continue;
        snprintf(text, sizeof(text), "%s: %s", cfg->label, cfg->mode_label[mode]);
        UIMinimenu_AddOption(
            menu,
            text,
            RS_MINIMENU_ACTION_CHAT_FILTER,
            -1,
            (struct UIMinimenuPick){
                .kind = UI_MINIMENU_PICK_UI,
                .id = node->component_id,
                .secondary_id = filter,
                .tertiary_id = mode,
            });
    }
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
    struct UITreeMenuOptions const* opts = UITree_MenuOptions(node);
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
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_TGT_BUTTON, -1, pick);
    return true;
}

/*
 * Does this component offer an IF3 "Cast <spell>" row?
 *
 * IF3 has no `buttonType` at all — the field is IF1's, and every IF3 widget
 * decodes it as 0 — so the branch above can never fire for an OldSchool
 * spellbook. The reference's test is instead deob `method12079`: a non-zero
 * target mask AND a non-empty target verb, with no button type involved
 * (`method5229`'s `if (!isIf3) return;` walk). Gating on buttonType was why
 * every rev-230/239 spell built a plain button row and nothing ever armed.
 */
static bool
component_offers_if3_target(struct UITreeComponent const* node)
{
    return node->behavior.button_type != REVCONFIG_BUTTON_TYPE_TARGET &&
           node->behavior.target_mask != 0 &&
            UITree_MenuOptions(node)->target_verb[0] != '\0';
}

/*
 * The IF3 op walk: slots high to low, with the target verb inserted where the
 * walk reaches `targetPriority` and everything above that slot deprioritized
 * (deob method5229's trailing loop, the same rule add_obj_cell_rows already
 * follows for item cells).
 *
 * Insertion order is draw order reversed, so emitting high slots first puts op 1
 * on top and leaves the target row sitting directly under the operation that
 * owns its priority — which is also what decides the left-click default, since
 * a deprioritized action can never be it. High Alchemy is the case that shows
 * it: ops 9 and 10 ("Animation", "Warnings") sit above priority 4, so they
 * stay right-click-only and "Cast High Alchemy" is what a left click does.
 */
static int
add_if3_target_op_rows(
    struct UIMinimenu* menu,
    struct UITreeComponent const* node,
    struct UITreeMenuOptions const* rows,
    struct UIMinimenuPick pick)
{
    struct UITreeMenuOptions const* opts = UITree_MenuOptions(node);
    int const before = menu->option_count;
    /* IF3 has no targetText: the row's target is the component's opBase, which
     * is the same string its op rows are suffixed with and which the spellbook
     * fills in already coloured (`<col=00ff00>Wind Strike</col>`). */
    char const* base = opts->option;
    char text[UITREE_MINIMENU_OPTION_LEN];
    /* The reference walks 32 operation slots; this tree stores 10. A priority
     * past the end still has to produce its row, so it lands on the first slot
     * walked — the same "above every op" position it holds there. A negative
     * priority is `cc_settargetpriority(-1)`, "no target row at all". */
    int const priority = node->target_priority >= UITREE_MENU_OPTION_SLOTS
                             ? UITREE_MENU_OPTION_SLOTS - 1
                             : node->target_priority;

    for( int i = UITREE_MENU_OPTION_SLOTS - 1; i >= 0; i-- )
    {
        if( i == priority )
        {
            snprintf(text, sizeof(text), "%s %s", opts->target_verb, base);
            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_TGT_BUTTON, -1, pick);
        }
        if( !rows || rows->ops[i][0] == '\0' )
            continue;
        {
            int action =
                rows->op_actions[i] != 0 ? rows->op_actions[i] : k_inv_button_action[i];
            if( i > priority )
                action = UIMinimenu_ActionDeprioritize(action);
            if( base[0] != '\0' )
                snprintf(text, sizeof(text), "%s %s", rows->ops[i], base);
            else
                snprintf(text, sizeof(text), "%s", rows->ops[i]);
            UIMinimenu_AddOption(menu, text, action, i, pick);
        }
    }
    return menu->option_count - before;
}

/* Generic component rows: social first, else op rows (with the component's
 * option label as the row target — reference "<op> <target>") or, for
 * op-less buttons, a single label row. Port of v1
 * ui_click_add_component_menu_rows with xrsps target labeling. */
/*
 * Bit 0 of an IF_SETEVENTS mask: the component accepts Continue (menu action
 * 30 / RESUME_PAUSEBUTTON). It is what makes a dialogue's prompt live.
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
    struct UITreeMenuOptions const* opts = UITree_MenuOptions(node);
    struct UIMinimenuPick const pick = pick_ui(node->component_id);
    int ops_added;
    char const* label = NULL;
    int events = 0;
    int has_local_hook = 0;
    struct UITreeMenuOptions filtered;
    struct UITreeMenuOptions const* rows = opts;

    /*
     * CLIENT CHROME IS NOT GAME CONTENT.
     *
     * The CS2 chrome executor builds the plugin window out of real interface
     * components, and the sidebar's Plugin button is one too. They are armed
     * for clicks so the executor hears about them -- and that arming is
     * exactly what this function reads, so every field and every checkbox in
     * that window grew a right-click menu offering "Continue", and the
     * mouseover text read "Continue" with the pointer anywhere over the panel.
     * That is the generic verb the reference gives a component a script
     * enabled, and it means nothing for a control the game does not own.
     *
     * Recognised by GROUP, the same bounds test the click interception uses
     * (TORIRS_CHROME_CS2_GROUP is the tree's own "app-overlay chrome" group).
     * One test here covers the right-click menu, the left-click default row
     * and the mouseover text, because all three are this one menu build.
     */
    if( ((node->component_id >> 16) & 0xFFFF) == TORIRS_CHROME_CS2_GROUP )
        return 0;

    if( add_social_rows(node, menu) )
        return menu->option_count - before;

    /* A spell/prayer button turns into a "Cast <spell>" target-select row
     * rather than its op rows — but only when nothing is already armed
     * (reference gates the BUTTON_TARGET branch on targetMode === 0). */
    if( select_mode == RS_MINIMENU_SELECT_NONE && add_target_button_row(node, menu) )
        return menu->option_count - before;

    /* At rev 230 an op string is not enough: either the server armed that op
     * (IF_SETEVENTS) or a local on_op/on_click handles it (XP-orb Show/Hide).
     * Ops with neither produce "minimenu: no hook" and steal the default click
     * from a real control underneath (fixed-mode gameframe dynamics). */
    {
        struct UITreeRuntimeHooks const* hooks = UITree_Hooks(node);
        has_local_hook =
            hooks->on_op.script_id > 0 || hooks->on_click.script_id > 0;
    }
    if( ctx->events_for_component )
        events = ctx->events_for_component(ctx->events_user, node->component_id, -1);

    if( !has_local_hook )
    {
        int any = 0;
        filtered = *opts;
        for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        {
            if( (events & (1 << (i + 1))) == 0 )
                filtered.ops[i][0] = '\0';
            else if( filtered.ops[i][0] != '\0' )
                any = 1;
        }
        if( !any )
        {
            /* No armed op rows — fall through to CONTINUE / label paths. */
            rows = NULL;
        }
        else
            rows = &filtered;
    }

    /* An IF3 target component's verb and its ops are ONE ordered walk, not two
     * competing branches: High Alchemy carries "Animation" and "Warnings"
     * alongside "Cast High Alchemy" and the reference emits all three from the
     * same loop. Falling into the plain op path instead would emit the ops and
     * silently drop the cast; taking the classic early-return would emit the
     * cast and drop the ops. */
    if( select_mode == RS_MINIMENU_SELECT_NONE && component_offers_if3_target(node) )
    {
        ops_added = add_if3_target_op_rows(menu, node, rows, pick);
        if( ops_added > 0 )
            return menu->option_count - before;
    }

    if( rows )
    {
        ops_added = add_menu_ops_rows(
            menu, rows, pick, rows->option[0] != '\0' ? rows->option : NULL,
            k_inv_button_action);
        if( ops_added > 0 )
            return menu->option_count - before;
    }

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
        /*
         * A row's action_index is WHICH NUMBERED OP it is, and it is the thing
         * the dispatcher uses to decide the click is an IF3 `IF_BUTTON<n>` for
         * the server (app.c, `opt.action_index >= 0 && < 10`). A row built from
         * the cache's BUTTON TYPE is not a numbered op at all -- it is an IF1
         * button, applied locally by RS_IF1_ApplyButtonClick, which is what
         * turns buttonType 6 into RESUME_PAUSEBUTTON and buttonType 3 into a
         * close.
         *
         * Passing 0 here said "this is op 1". A dialogue's "Click here to
         * continue" therefore went out as IF_BUTTON1 on a component the server
         * had armed no op on, `if_button_sent` suppressed the local apply, and
         * the prompt did nothing at all -- no continue, no error. -1 is the
         * same "no numbered op" the Cancel and social rows already use.
         *
         * An explicit `option_action` from a revconfig IS an op-0 row and keeps
         * its index; only the button-type fallback is not one.
         */
        int const from_button_type = opts->option_action == 0;
        int const action = from_button_type
                               ? if_button_action_for_type(node->behavior.button_type)
                               : opts->option_action;
        UIMinimenu_AddOption(menu, label, action, from_button_type ? -1 : 0, pick);
        return menu->option_count - before;
    }

    /* Nothing in the cache described this component as a button, but the server
     * may have enabled it at run time. That is the normal case at rev 230 for
     * anything a script drives — a dialogue's continue prompt carries
     * button_type 0 and no menu ops, and is live only because IF_SETEVENTS said
     * so. The component's own text is the label the player already sees. */
    if( events & RS_MINIMENU_EVENT_CLICK )
    {
        /* A text component's own string is the label the player is
         * already reading ("Click here to continue"); anything else falls
         * back to a generic verb. */
        char const* text = (node->type == UIELEM_RS_TEXT && node->u.rs_text.text &&
                            node->u.rs_text.text[0] != '\0')
                               ? node->u.rs_text.text
                               : "Continue";

        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON, -1, pick);
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
        else if( node->type == UIELEM_BUILTIN_CHAT_BUTTON )
            add_chat_button_rows(ctx, node, out);
        else
            add_component_rows(ctx, node, ctx->selection.mode, out);
    }

    /*
     * Drop the plugin launcher's row when its server is gone. See
     * RS_MinimenuBuildCtx::plugin_io_down for why this is by action id.
     *
     * After the walk and before the sort: the row can come from an authored
     * component op or from the launcher this client builds itself, and this is
     * the one point both have passed through. A compaction rather than a flag
     * on the option, because everything downstream -- the sort, the width
     * measure, the default-row scan -- counts rows, and a row that is present
     * but ignored would have to be taught to each of them separately.
     *
     * Only the launcher. A plugin's own canvas-region rows are its FEATURES,
     * not a way into the panel, and one already running off assets it loaded
     * before the outage keeps working; taking its orb's rows away would break
     * a plugin that is fine.
     */
    if( ctx->plugin_io_down )
    {
        int kept = 0;

        for( int i = 0; i < out->option_count; i++ )
        {
            if( out->options[i].action == RS_MINIMENU_ACTION_PLUGIN_PANEL )
                continue;
            if( kept != i )
                out->options[kept] = out->options[i];
            kept++;
        }
        out->option_count = kept;
    }

    UIMinimenu_SortPriorityActions(out);
}

int
RS_Minimenu_DefaultOptionIndex(struct UIMinimenu const* menu)
{
    int walk = -1;

    assert(menu);
    /* Scan from the top row down for the first ordinary action, falling back
     * to Walk. Widget target-priority defaults to 4 in the gamepack; preserving
     * that default is what keeps common component ops (for example bank
     * Deposit-current-quantity at op 2) in this ordinary-action set. */
    for( int i = menu->option_count - 1; i >= 0; i-- )
    {
        int const action = menu->options[i].action;
        if( action == REVCONFIG_MINIMENU_WALK )
        {
            if( walk < 0 )
                walk = i;
            continue;
        }
        if( RS_Minimenu_ActionIsDefaultable(action) )
            return i;
    }
    return walk;
}
