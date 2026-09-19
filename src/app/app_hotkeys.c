/*
 * Developer hotkeys: the CTRL modifier rule, the UI and world bindings, and
 * the spawn/test shortcuts.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_debug_modifier_held(struct LibToriRS_Input* input);
static struct AppDebugHotkeyBinding const*
app_debug_binding_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target);
static struct AppDebugHotkeyBinding const*
app_debug_world_key(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target);

/*
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

static struct AppDebugHotkeyBinding const*
app_debug_binding_down(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    if( !app_debug_modifier_held(input) )
        return NULL;
    for( int i = 0; i < app->cfg.debug_hotkey_count; i++ )
        if( app->cfg.debug_hotkeys[i].target == target &&
            LibToriRS_Input_IsKeyDown(input, app->cfg.debug_hotkeys[i].key) )
            return &app->cfg.debug_hotkeys[i];
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

int
app_debug_key_held(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    if( !app_debug_modifier_held(input) )
        return 0;
    for( int i = 0; i < app->cfg.debug_hotkey_count; i++ )
        if( app->cfg.debug_hotkeys[i].target == target &&
            LibToriRS_Input_IsKeyHeld(input, app->cfg.debug_hotkeys[i].key) )
            return 1;
    return 0;
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
 * Returns nothing, but marks each key it acted on in app->hotkey_consumed so a
 * bound key does not also fire a debug world hotkey on the same press.
 */
void
app_ui_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input)
{
    memset(app->hotkey_consumed, 0, sizeof(app->hotkey_consumed));

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
        app->chat_input_active = 0;
        app->host.close_modal_requested = true;
        app->need_redraw = 1;
    }

    if( !app->tree || app->tree->hotkey_count <= 0 )
        return;
    /* Typing wins over every binding — otherwise "f" in a chat line, or a digit
     * in a bank amount, silently switches tabs behind the caret. */
    if( app_text_input_focused(app) )
        return;
    /* An open right-click menu owns the pointer; let it own the keyboard too,
     * matching how interact_minimenu swallows everything until it closes. */
    if( app->interact.minimenu.visible )
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
            app->hotkey_consumed[binding->osrs_key] = 1;
            app->need_redraw = 1;
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

/* A configured debug key is available only if no revconfig hotkey binding
 * already acted on the corresponding OSRS key this frame. */
static struct AppDebugHotkeyBinding const*
app_debug_world_key(
    struct App const* app,
    struct LibToriRS_Input* input,
    enum AppDebugHotkey target)
{
    for( int i = 0; i < app->cfg.debug_hotkey_count; i++ )
    {
        enum LibToriRS_KeyCode key = app->cfg.debug_hotkeys[i].key;
        int vk = -1;
        int osrs_key;

        if( app->cfg.debug_hotkeys[i].target != target || !LibToriRS_Input_IsKeyDown(input, key) )
            continue;
        if( key >= TORIRSK_A && key <= TORIRSK_Z )
            vk = 65 + (key - TORIRSK_A);
        else if( key >= TORIRSK_0 && key <= TORIRSK_9 )
            vk = 48 + (key - TORIRSK_0);
        else
            switch( key )
            {
            case TORIRSK_ESCAPE:
                vk = TORIRS_VK_ESCAPE;
                break;
            case TORIRSK_RETURN:
                vk = TORIRS_VK_ENTER;
                break;
            case TORIRSK_BACKSPACE:
                vk = TORIRS_VK_BACKSPACE;
                break;
            case TORIRSK_DELETE:
                vk = TORIRS_VK_DELETE;
                break;
            case TORIRSK_SHIFT:
                vk = TORIRS_VK_SHIFT;
                break;
            case TORIRSK_CTRL:
                vk = TORIRS_VK_CTRL;
                break;
            case TORIRSK_TAB:
                vk = TORIRS_VK_TAB;
                break;
            case TORIRSK_SPACE:
                vk = TORIRS_VK_SPACE;
                break;
            case TORIRSK_LEFT:
                vk = 37;
                break;
            case TORIRSK_UP:
                vk = 38;
                break;
            case TORIRSK_RIGHT:
                vk = 39;
                break;
            case TORIRSK_DOWN:
                vk = 40;
                break;
            case TORIRSK_PAGE_UP:
                vk = 33;
                break;
            case TORIRSK_PAGE_DOWN:
                vk = 34;
                break;
            default:
                break;
            }
        osrs_key = LibToriRS_OsrsKeyFromVk(vk);
        if( osrs_key < 0 || osrs_key >= TORIRS_OSRSKEY_COUNT || !app->hotkey_consumed[osrs_key] )
            return &app->cfg.debug_hotkeys[i];
    }
    return NULL;
}

/* Manifest-configured spawn/test shortcuts act on the tile under the mouse, so
 * they no-op when nothing is hovered. */
void
app_world_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    struct AppDebugHotkeyBinding const* binding;
    /* Spawn hotkeys gate on the hovered world tile, not on onKey targets —
     * under the real gameframe there is always some visible onKey component
     * and gating on it made every press suppress itself. */
    (void)out;
    if( !app->world_active || !app_world_viewport_component_live(app) )
        return;
    /* Suppressed while any text input has focus, so spawn-digit keys type
     * instead. */
    if( app_text_input_focused(app) )
        return;
    if( app->world_hover_tile_x < 0 || app->world_hover_tile_z < 0 )
        return;

    if( app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_PLAYER) )
        app_world_spawn_player(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_NPC)) )
        app_world_spawn_npc(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
    if( app_debug_world_key(app, input, APP_DEBUG_HOTKEY_DAMAGE_TEST) )
        app_world_damage_test(app);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_ENTITY_SPOTANIM)) )
        app_world_entity_spotanim_test(app, binding->args);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_SPOTANIM)) )
        app_world_spawn_spotanim(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_OBJ)) )
        app_world_spawn_obj(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
    if( (binding = app_debug_world_key(app, input, APP_DEBUG_HOTKEY_SPAWN_PROJECTILE)) )
        app_world_spawn_projectile(
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level,
            binding->args);
}
