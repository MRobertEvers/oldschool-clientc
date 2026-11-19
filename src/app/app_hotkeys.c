/*
 * Hotkeys: the game's configured UI hotkeys, the CTRL modifier rule and the
 * key predicate the side-apps are delivered under, and the bridge for the
 * developer hotkeys not moved onto the registry yet.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_debug_modifier_held(struct LibToriRS_Input* input);
static struct SideAppBinding const*
app_debug_binding_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target);
static int
app_debug_world_key_fires(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key);
static int
app_side_app_key_fires(
    void* ctx,
    struct SideAppBinding const* binding,
    enum SideAppGate gate,
    enum SideAppKeyMode mode);

/*
 * The bridge: the names of the hotkeys whose tools are not side-apps yet, one
 * per enum AppDebugHotkey value, registered as one side-app's polled actions
 * so a manifest binding to one resolves. app_debug_key_down reads the
 * bindings resolved to it. An entry leaves when its tool registers the name
 * itself.
 */
#define APP_DEBUG_HOTKEYS_BRIDGE "app_debug_hotkeys"
#define APP_DEBUG_HOTKEY_POLLED(n) { (n), SIDE_APP_PHASE_POLLED, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 }
static struct SideAppAction const k_app_debug_hotkey_actions[APP_DEBUG_HOTKEY_COUNT] = {
    [APP_DEBUG_HOTKEY_MAP_EDITOR] = APP_DEBUG_HOTKEY_POLLED("map_editor_toggle"),
};
#undef APP_DEBUG_HOTKEY_POLLED

static struct SideAppHooks const k_app_debug_hotkeys_hooks = { 0 };

void
app_debug_hotkeys_register(struct App* app)
{
    assert(app);
    assert(app->side_apps);
    for( int i = 0; i < APP_DEBUG_HOTKEY_COUNT; i++ )
        assert(k_app_debug_hotkey_actions[i].name);
    SideApps_Register(
        app->side_apps,
        APP_DEBUG_HOTKEYS_BRIDGE,
        NULL,
        &k_app_debug_hotkeys_hooks,
        k_app_debug_hotkey_actions,
        APP_DEBUG_HOTKEY_COUNT);
}

/*
 * A chord the map editor's table claims this frame (bound in a live context:
 * app_editor_hotkeys.c) is the editor's, whether or not it acted, so the
 * binding readers below skip it -- Ctrl+S saves and never also flies the
 * camera back.
 *
 * Developer hotkeys hold CTRL.
 *
 * Because the game's own keys are not the client's to intercept. Every key
 * event goes to every registered onKey hook and the cache's scripts decide
 * what to do with it -- that is the reference client's dispatch, and it means
 * a bare `p` is a letter somebody is typing into the chat line, not a request
 * to open the debug overlay. The client used to know better by tracking chat
 * focus itself, which took naming the chatbox's interface id.
 *
 * The modifier applies in every era rather than only where a cache chatbox
 * exists: one rule the muscle memory can hold, and a dat1 boot has a chat line
 * to type into as well.
 */
static int
app_debug_modifier_held(struct LibToriRS_Input* input)
{
    assert(input);
    return LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL);
}

static struct SideAppBinding const*
app_debug_binding_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    struct SideApps const* apps = app->side_apps;
    int bridge;

    if( !app_debug_modifier_held(input) )
        return NULL;
    bridge = SideApps_IndexOf(apps, APP_DEBUG_HOTKEYS_BRIDGE);
    for( int i = 0; i < SideApps_BindingCount(apps); i++ )
    {
        struct SideAppBinding const* binding = SideApps_BindingAt(apps, i);
        if( SideApps_BindingAnswers(apps, i, bridge, target) &&
            LibToriRS_Input_IsKeyDown(input, binding->key) &&
            !app_editor_hotkeys_claimed(app, binding->key) )
            return binding;
    }
    return NULL;
}

int
app_debug_key_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    return app_debug_binding_down(app, input, target) != NULL;
}

/*
 * Configured hotkeys: revconfig binds a key to one chrome node + effect, and
 * the effects themselves are hard-coded here (enum UITreeHotkeyEffect).
 *
 * Dispatch lives in the app rather than in uitree_interact because the
 * suppression rule does: a key that reaches a hotkey must not also be a
 * character being typed, and only the app knows which of the chat, social, and
 * dialog input lines has focus. Keys are read off osrs_key_pressed — the same
 * edge array CS2's KEYPRESSED reads — which, unlike enum LibToriRS_KeyCode,
 * covers the F-keys.
 *
 * Returns nothing, but claims each key it acted on in app->input_claims so a
 * bound key does not also fire a side-app's world key on the same press.
 */
void
app_ui_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input)
{
    ToriRS_InputClaimsClearKeys(&app->input_claims);
    /* The map editor's chords acted earlier this frame (app_editor_hotkeys.c,
     * before the overlay ticks); their keys stay consumed for the world
     * hotkeys below. */
    app_editor_hotkeys_mark_consumed(app);

    /* Escape, ahead of every gate below: it must fire with the chat line
     * focused (the line is focused by default) and with no revconfig bindings
     * loaded (cache chrome has none). Releases chat focus and asks the server
     * to close whatever modal is up — the same CLOSE_MODAL the gameframe X's
     * clientscript (29, if_close) raises, so what closes stays the server's
     * decision and an idle Escape is a no-op there. Read off osrs_key_pressed
     * rather than the key_events queue so CS2 KEYPRESSED-style injection (and
     * TORIRS_SIM_HOTKEY) reach it too; a real press feeds both, and the two
     * writes to the same request flag collapse into one packet. */
    if( input->osrs_key_pressed[TORIRS_OSRSKEY_ESCAPE] )
    {
        UIChatLine_SetInputActive(&app->chat_line, 0);
        app->host.close_modal_requested = true;
        RedrawRequest_Raise(&app->redraw_request);
    }

    if( !app->tree || app->tree->hotkey_count <= 0 )
        return;
    /* Typing wins over every binding — otherwise "f" in a chat line, or a digit
     * in a bank amount, silently switches tabs behind the caret. */
    if( app_text_input_focused(app) )
        return;
    /* An open right-click menu owns the pointer; let it own the keyboard too,
     * matching how interact_minimenu swallows everything until it closes. */
    if( app->world_views.frame->minimenu->visible )
        return;

    for( int i = 0; i < app->tree->hotkey_count; i++ )
    {
        struct UITreeHotkey const* binding = &app->tree->hotkeys[i];
        struct UITreeComponent const* node;

        if( binding->osrs_key < 0 || binding->osrs_key >= TORIRS_OSRSKEY_COUNT )
            continue;
        if( !input->osrs_key_pressed[binding->osrs_key] )
            continue;
        if( binding->node_index < 0 || (uint32_t)binding->node_index >= app->tree->component_count )
            continue;

        node = &app->tree->components[binding->node_index];
        if( node->freed || UITree_NodeOrAncestorDisplayHidden(app->tree, binding->node_index) )
            continue;

        switch( binding->effect )
        {
        case UITREE_HOTKEY_EFFECT_SELECT_TAB:
        {
            /* Same path a click on the tab takes (interact_click's chrome
             * gestures), enabled-gate included: a tab with no interface
             * mounted is not selectable by key any more than by mouse. */
            int tabno = -1;
            if( node->type == UIELEM_BUILTIN_TAB_ICONS )
                tabno = node->u.tab_icon.tabno;
            else if( node->type == UIELEM_BUILTIN_REDSTONE_TAB )
                tabno = node->u.redstone_tab.tabno;
            else if( node->type == UIELEM_BUILTIN_SIDEBAR )
                tabno = node->u.sidebar.tabno;
            if( tabno < 0 )
                break;
            {
                struct UITreeHostRequest enabled_req = {
                    .kind = UITREE_HOST_GET_TAB_ENABLED,
                    .u.tab_enabled.tabno = tabno,
                };
                if( !UITree_Host(&app->ui_host, &enabled_req) )
                    break;
            }
            {
                struct UITreeHostRequest set_req = {
                    .kind = UITREE_HOST_SET_SELECTED_TAB,
                    .u.set_selected_tab.tabno = tabno,
                };
                UITree_Host(&app->ui_host, &set_req);
            }
            ToriRS_InputClaimsClaimKey(&app->input_claims, binding->osrs_key);
            RedrawRequest_Raise(&app->redraw_request);
            if( getenv("TORIRS_HOTKEY_DEBUG") )
                TORIRS_LOG(
                    "hotkey: osrs_key=%d node=%d effect=select_tab tab=%d\n",
                    binding->osrs_key,
                    binding->node_index,
                    tabno);
            break;
        }
        default:
            break;
        }
    }
}

/* The OSRS key code of a LibToriRS key: name -> VK -> OSRS, through the
 * cache keymap's one table. -1 for a key it does not map. */
int
app_keycode_osrs(int key)
{
    static const struct
    {
        int key;
        int vk;
    } k_vk[] = {
        { TORIRSK_ESCAPE, TORIRS_VK_ESCAPE }, { TORIRSK_RETURN, TORIRS_VK_ENTER },
        { TORIRSK_BACKSPACE, TORIRS_VK_BACKSPACE }, { TORIRSK_DELETE, TORIRS_VK_DELETE },
        { TORIRSK_SHIFT, TORIRS_VK_SHIFT }, { TORIRSK_CTRL, TORIRS_VK_CTRL }, { TORIRSK_ALT, TORIRS_VK_ALT },
        { TORIRSK_TAB, TORIRS_VK_TAB }, { TORIRSK_SPACE, TORIRS_VK_SPACE }, { TORIRSK_LEFT, 37 },
        { TORIRSK_UP, 38 }, { TORIRSK_RIGHT, 39 }, { TORIRSK_DOWN, 40 }, { TORIRSK_PAGE_UP, 33 },
        { TORIRSK_PAGE_DOWN, 34 }, { TORIRSK_END, 35 }, { TORIRSK_HOME, 36 }, { TORIRSK_INSERT, 45 },
        { TORIRSK_SEMICOLON, 186 }, { TORIRSK_EQUALS, 187 }, { TORIRSK_COMMA, 188 }, { TORIRSK_MINUS, 189 },
        { TORIRSK_PERIOD, 190 }, { TORIRSK_SLASH, 191 }, { TORIRSK_GRAVE, 192 }, { TORIRSK_LBRACKET, 219 },
        { TORIRSK_BACKSLASH, 220 }, { TORIRSK_RBRACKET, 221 }, { TORIRSK_APOSTROPHE, 222 },
    };
    int vk = -1;

    if( key >= TORIRSK_A && key <= TORIRSK_Z )
        vk = 65 + (key - TORIRSK_A);
    else if( key >= TORIRSK_0 && key <= TORIRSK_9 )
        vk = 48 + (key - TORIRSK_0);
    else if( key >= TORIRSK_F1 && key <= TORIRSK_F12 )
        vk = 112 + (key - TORIRSK_F1);
    else
        for( size_t i = 0; i < sizeof(k_vk) / sizeof(k_vk[0]); i++ )
            if( k_vk[i].key == key )
                vk = k_vk[i].vk;
    return vk < 0 ? -1 : LibToriRS_OsrsKeyFromVk(vk);
}

/* A configured debug key is available only if no revconfig hotkey binding
 * already acted on the corresponding OSRS key this frame. */
static int
app_debug_world_key_fires(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum LibToriRS_KeyCode key)
{
    int osrs_key;

    if( !LibToriRS_Input_IsKeyDown(input, key) || app_editor_hotkeys_claimed(app, key) )
        return 0;
    osrs_key = app_keycode_osrs(key);
    return osrs_key < 0 || osrs_key >= TORIRS_OSRSKEY_COUNT
           || !ToriRS_InputClaimsKeyClaimed(&app->input_claims, osrs_key);
}

/*
 * The side-apps' key predicate: the same rules the bridge readers above apply,
 * named by gate. CTRL: the modifier held, the key down (PRESS), held (HELD)
 * or either (PRESS_OR_HELD), not a key the map editor claimed. WORLD (app_debug_world_key_fires): no
 * modifier, the key down, not a key the map editor or a revconfig hotkey
 * claimed this frame.
 */
struct AppSideAppKeyCtx
{
    struct App const* app;
    struct LibToriRS_Input* input;
};

static int
app_side_app_key_fires(
    void* ctx,
    struct SideAppBinding const* binding,
    enum SideAppGate gate,
    enum SideAppKeyMode mode)
{
    struct AppSideAppKeyCtx const* keys = ctx;

    assert(keys);
    assert(binding);
    if( gate == SIDE_APP_GATE_WORLD )
    {
        /* No world key fires while held: the world gate reads presses. */
        assert(mode == SIDE_APP_KEY_PRESS);
        return app_debug_world_key_fires(keys->app, keys->input, binding->key);
    }
    assert(gate == SIDE_APP_GATE_CTRL);
    if( !app_debug_modifier_held(keys->input) )
        return 0;
    if( mode == SIDE_APP_KEY_HELD )
        return LibToriRS_Input_IsKeyHeld(keys->input, binding->key)
               && !app_editor_hotkeys_claimed(keys->app, binding->key);
    if( mode == SIDE_APP_KEY_PRESS_OR_HELD )
        return (LibToriRS_Input_IsKeyDown(keys->input, binding->key)
                || LibToriRS_Input_IsKeyHeld(keys->input, binding->key))
               && !app_editor_hotkeys_claimed(keys->app, binding->key);
    return LibToriRS_Input_IsKeyDown(keys->input, binding->key)
           && !app_editor_hotkeys_claimed(keys->app, binding->key);
}

void
app_side_app_deliver_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    enum SideAppPhase phase)
{
    struct AppSideAppKeyCtx keys = { app, input };

    assert(app);
    assert(input);
    SideApps_DeliverKeys(app->side_apps, phase, app_side_app_host(app), app_side_app_key_fires, &keys);
}

void
app_side_app_probe_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    enum SideAppPhase phase)
{
    struct AppSideAppKeyCtx keys = { app, input };

    assert(app);
    assert(input);
    SideApps_ProbeKeys(app->side_apps, phase, app_side_app_host(app), app_side_app_key_fires, &keys);
}

/* The side-apps' world keys act on the tile under the mouse, so they no-op
 * when nothing is hovered. */
void
app_world_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    /* World keys gate on the hovered world tile, not on onKey targets --
     * under the real gameframe there is always some visible onKey component
     * and gating on it made every press suppress itself. */
    (void)out;
    if( !WorldLoadState_Active(&app->world_load) || !app_world_viewport_component_live(app) )
        return;
    /* Suppressed while any text input has focus, so digit keys type
     * instead. */
    if( app_text_input_focused(app) )
        return;
    if( app->world_views.frame->world_hover_tile_x < 0 || app->world_views.frame->world_hover_tile_z < 0 )
        return;

    /* The side-apps' world keys, under the gates above. */
    app_side_app_deliver_keys(app, input, SIDE_APP_PHASE_WORLD_HOTKEYS);
}
