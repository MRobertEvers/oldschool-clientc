/*
 * Who owns the keyboard: the chat line, interface text fields, and the chrome.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static char const*
app_chat_prompt(struct App const* app);
static int
app_point_in_chat(
    struct App const* app,
    int x,
    int y);

struct RS_ChatFilters
app_chat_filters(struct App const* app)
{
    struct RS_ChatFilters filters = {
        .public_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC],
        .private_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE],
        .trade_mode = app->slots.chat_filter_mode[RS_UI_CHAT_FILTER_TRADE],
        .social = &app->social,
    };
    return filters;
}

/* chat_index is a long-lived slot-table index rather than an identity.  Check
 * that it still names the authored chat slot before using it, and apply the
 * same effective visibility predicate as paint/hit traversal. */
int32_t
app_chat_node_index(struct App const* app)
{
    struct UITreeComponent const* node;
    int32_t idx;

    if( !app || !app->tree )
        return -1;
    idx = app->slots.chat_index;
    if( idx < 0 || (uint32_t)idx >= app->tree->component_count )
        return -1;
    node = &app->tree->components[idx];
    if( node->freed || node->slot_tag != UITREE_SLOT_CHAT ||
        UITree_NodeOrAncestorDisplayHidden(app->tree, idx) )
        return -1;
    return idx;
}

/* Chat node geometry + font, resolved through the slot index so nothing here
 * names coordinates or interface ids. Returns 0 when no chat region exists. */
int
app_chat_region(
    struct App const* app,
    int* out_x,
    int* out_y,
    int* out_font_id)
{
    struct UITreeComponent const* node;
    int32_t idx;
    int x = 0, y = 0, w = 0, h = 0;

    idx = app_chat_node_index(app);
    if( idx < 0 )
        return 0;
    node = &app->tree->components[idx];
    UITree_LayoutGetBounds(&node->position, &x, &y, &w, &h);
    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_font_id )
        *out_font_id = UITree_Chat(node)->font_id > 0 ? UITree_Chat(node)->font_id : 1;
    return 1;
}

/* The unfocused input line's wording, from the chat component's `prompt=` key
 * (a `@mobile` override is how a touch lane says "Tap here to chat...").
 * NULL when no chat region exists or the profile stated nothing, which
 * RS_Chat_BuildView reads as "use the reference wording". */
static char const*
app_chat_prompt(struct App const* app)
{
    int32_t idx;

    idx = app_chat_node_index(app);
    if( idx < 0 )
        return NULL;
    return UITree_Chat(&app->tree->components[idx])->prompt;
}

/* True when a canvas-space point lands inside the chat region's bounds. Used to
 * decide chat input focus on a left click.
 *
 * A revconfig (dat1) idea only: that tree tags its chat region with a slot, and
 * paints the messages into it itself. A cache tree has no chat *region* at all
 * -- its chatbox is an interface whose own onKey hook is the typed line, and
 * which the client does not route keys for. */
static int
app_point_in_chat(
    struct App const* app,
    int x,
    int y)
{
    struct UITreeComponent const* node;
    int32_t idx;
    int bx = 0, by = 0, bw = 0, bh = 0;

    idx = app_chat_node_index(app);
    if( idx < 0 )
        return 0;
    node = &app->tree->components[idx];
    UITree_LayoutGetBounds(&node->position, &bx, &by, &bw, &bh);
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/*
 * An interface's own text box has the keyboard.
 *
 * A panel with a search field (settings 134, collection log 621, league tasks)
 * takes it by calling `~chatdefault_stopinput`, which sets this varc and
 * disarms the chatbox's onKey; `~chatdefault_restoreinput` puts both back. It
 * is the cache's own focus flag, and reading it is how the client learns that
 * something it does not own is being typed into. Unset reads -1, which is not
 * "a box has it" — hence `== 1`. */
int
app_iface_text_input_focused(struct App const* app)
{
    struct VarCIds const* ids =
        varc_ids_for_revision(app->net && app->net->rev ? (int)app->net->rev->revision : 0);

    assert(app);
    if( ids->interface_input_active < 0 )
        return 0;
    return VarCManager_GetInt(&app->varcs, ids->interface_input_active) == 1;
}

int
app_text_input_focused(struct App const* app)
{
    struct UIKeyboardOwners owners;
    struct VarCIds const* ids;

    assert(app);
    ids = varc_ids_for_revision(app->net && app->net->rev ? (int)app->net->rev->revision : 0);

    owners.chat_line_present = app_chat_node_index(app) >= 0;
    owners.chat_input_active = app->chat_input_active != 0;
    owners.chat_social_input_open = app->chat.social_input_open != 0;
    owners.chat_dialog_input_open = app->chat.dialog_input_open != 0;
    owners.interface_input_active_varc =
        ids->interface_input_active >= 0
            ? VarCManager_GetInt((struct VarCManager*)&app->varcs, ids->interface_input_active)
            : -1;
    owners.tree_input_focus_id = app->tree ? UITree_InputFocusId(app->tree) : -1;
    owners.chrome_field_focused = app_chrome_holds_keyboard(app) != 0;

    return UIKeyboard_TextInputFocused(&owners) ? 1 : 0;
}

/*
 * Is a ToriRSChrome field being typed into?
 *
 * BOTH instances, which is the whole point of it being a function. dbg_ui is
 * the developer chrome (the map editor's Height field and its neighbours);
 * plugin_ui is the plugin window, and it was missing -- so a keystroke aimed
 * at a plugin's colour or note field also fired whatever debug hotkey shares
 * that letter, and was typed into the chat line underneath. Both are the same
 * bug app_text_input_focused exists to kill, and naming only one instance is
 * how it came back.
 */
int
app_chrome_holds_keyboard(struct App const* app)
{
    assert(app);
    return app->dbg_ui.focus >= 0 || app->plugin_ui.focus >= 0;
}

/*
 * Chat input focus, for the frame. A dat1/revconfig idea only.
 *
 * Runs ahead of the keyboard broadcast so the frame's keys are routed by a
 * focus state this frame's clicks and Enter have already been folded into,
 * rather than by last frame's.
 *
 * Focus is taken by clicking the chat region or by pressing Enter, and dropped
 * by clicking anywhere else, by Escape, and by sending the line. Which is the
 * whole point of the state: while it is off every key belongs to the hotkeys,
 * and while it is on none of them do.
 *
 * A CACHE revision has none of this, and that is the reference client's own
 * answer rather than a gap: it has no press-enter-to-chat, no focused/unfocused
 * chat line and no client-side routing of keys to the chatbox. Every key goes
 * to every registered onKey hook and the cache's scripts decide -- script 73
 * takes the typed line, script 905 takes the F-key tab switches, and a panel
 * search box takes the keyboard by disarming the chatbox's hook. The gate is
 * `slots.chat_index`, the revconfig-declared chat region, because a client that
 * paints its own chatbox is exactly the client that has to own focus for it.
 *
 * @param pointer_consumed an open minimenu already claimed this frame's press.
 * @param out_submit set when Enter arrived with the line focused — the line
 *        sends this frame, and the focus goes with it once the sending script
 *        has run (which is after this returns).
 * @return nonzero when the frame's keys must not reach the chat line at all:
 *         the Escape that dropped focus and the Enter that took it are focus
 *         commands, not text. It suppresses anything else typed in the same
 *         20ms frame, which is not a rate a player types at.
 */
int
app_chat_focus_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed,
    int* out_submit)
{
    int suppress = 0;
    int const was_focused = app->chat_input_active;

    assert(app);
    assert(input);
    assert(out_submit);
    *out_submit = 0;

    if( app_chat_node_index(app) < 0 )
    {
        if( app->chat_input_active )
        {
            app->chat_input_active = 0;
            app->need_redraw = 1;
        }
        return 0;
    }
    /* The loc editor took W/A/S/D/R/Space/Backspace for the frame and has
     * already forced the focus flags off; do not hand them back under it. */
    if( app->locedit.visible )
        return 0;
    /* An IF3 text-entry field has the caret. Enter belongs to that field (it
     * submits), and without this the chat line would claim the same press and
     * open itself behind the box being typed into. @see UITree_InputFocusId. */
    if( app->tree && UITree_InputFocusId(app->tree) >= 0 )
    {
        app->chat_input_active = 0;
        return 0;
    }
    /* A panel's own search box has the keyboard (the cache disarmed the
     * chatbox's onKey to give it to them), so the chat line cannot also have
     * it. Dropping focus here rather than merely ignoring it keeps
     * app_text_input_focused's two halves from both claiming to be the focused
     * one. */
    if( app_iface_text_input_focused(app) )
    {
        app->chat_input_active = 0;
        return 0;
    }

    if( !pointer_consumed && LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
        app->chat_input_active = app_point_in_chat(app, input->curr.mouse_x, input->curr.mouse_y);

    for( int e = 0; e < input->key_event_count; e++ )
    {
        int const typed = input->key_events[e].key_typed;

        if( typed == TORIRS_OSRSKEY_ESCAPE )
        {
            app->chat_input_active = 0;
            suppress = 1;
            continue;
        }
        if( typed != TORIRS_OSRSKEY_ENTER )
            continue;
        /* Enter with the line unfocused takes focus instead of submitting, so
         * the keyboard alone can start a message; with it focused it submits,
         * and the focus is released with the message the way the reference's
         * press-enter-to-chat does. */
        if( app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open )
            *out_submit = 1;
        else
        {
            app->chat_input_active = 1;
            suppress = 1;
        }
    }

    if( app->chat_input_active != was_focused )
        app->need_redraw = 1;
    return suppress;
}

/*
 * Rebuild the chat presentation (called before every emit).
 *
 * One shape, and it belongs to the eras whose chatbox is a *surface* the client
 * paints: a revconfig tree reserves a region, tags it with a slot, and this
 * flattens the message list into it for `emit_chat`.
 *
 * A cache revision has no chat region and nothing here to do. Its chatbox is
 * interface 162 -- 500 text components inside a scrolling layer -- and the
 * cache's own `[proc,rebuildchatbox]` writes every one of them, off the
 * chat-transmit hook, reading the same message store through the
 * CHAT_GETHISTORY* opcodes. The client used to write those components itself
 * from here, which meant it had to be told which components they were.
 */
void
app_chat_build_view(struct App* app)
{
    struct RS_ChatFilters filters = app_chat_filters(app);
    int font_id = 1;

    if( !app_chat_region(app, NULL, NULL, &font_id) )
    {
        memset(&app->chat_view, 0, sizeof(app->chat_view));
        return;
    }
    RS_Chat_BuildView(
        &app->chat,
        &filters,
        &app->ui_host,
        font_id,
        /* Not chat_com_id: the tutorial-progress component shares this region
         * and suppresses the log the same way a dialogue does. */
        RS_UISlots_ChatRegionIface(&app->slots) != -1,
        app->chat_input_active || app->chat.social_input_open || app->chat.dialog_input_open,
        app_chat_prompt(app),
        &app->chat_view);
}

/* RS_MinimenuChatSource seam: sender under a canvas-space click. */
int
app_chat_line_at(
    void* user,
    int x,
    int y,
    char* out_sender,
    int sender_cap,
    int* out_chat_type)
{
    struct App* app = (struct App*)user;
    struct RS_ChatFilters filters = app_chat_filters(app);
    int rx = 0;
    int ry = 0;

    if( !app_chat_region(app, &rx, &ry, NULL) )
        return 0;
    return RS_Chat_LineAt(
        &app->chat, &filters, x - rx, y - ry, out_sender, sender_cap, out_chat_type);
}
