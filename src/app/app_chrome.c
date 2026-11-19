/*
 * The developer chrome instance: fonts, scale, check style, the in-canvas
 * fallback for retained panel primitives, and the input routing every panel
 * of both chrome instances (the developer chrome and the plugin window)
 * shares: who owns the pointer, the pointer and keyboard fed to an instance,
 * and a focused field's clipboard chords; and the dev panels' tick, which
 * routes this frame's input into the developer chrome around the side-apps'
 * panel points.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

#include "platform/platform_clipboard.h"
#include "ui/chrome_compositor.h"
#include "ui/torirs_chrome_bake.h"

#include <stdio.h>
#include <stdlib.h>

/* Private to this unit, declared up front so definition order is free. */
static int
app_panel_overlay_to_chrome(
    struct App const* app,
    int index,
    struct ToriRSChromePrim* out);
static void
app_chrome_fonts_resolve(struct App* app);
static int
app_dbgui_key_edit_from_osrs(int osrs_key);

/** Convert one retained panel primitive for the in-canvas fallback. The
 * translation is ToriRSChromePanelDraw_ToChromePrim's; this is the spelling
 * that asks whether the primitive is showing at all. */
static int
app_panel_overlay_to_chrome(
    struct App const* app,
    int index,
    struct ToriRSChromePrim* out)
{
    struct UITreeEntityOverlay visible;

    assert(app);
    assert(out);
    if( !app_plugin_panel_overlay_visible(app, index, &visible) )
        return 0;
    return ToriRSChromePanelDraw_ToChromePrim(&visible, out);
}

/**
 * Both chrome instances' display lists, then panel-local custom primitives.
 *
 * Rebuilt only when the pair actually differs from what was merged last: the
 * prim arrays are handed downstream by pointer and a steady frame must stay a
 * pointer copy, which is the property the whole retained design is for. The
 * cheap comparison is the two counts plus the two damage states, and Build
 * having already decided nothing changed is what makes both stable.
 *
 * The plugin window goes SECOND, so it draws over the developer readout: it is
 * the one a player opened, and a frame-time counter on top of it would be a
 * developer tool covering a user's window.
 */
struct ToriRSChromePrim const*
app_chrome_merged_prims(
    struct App* app,
    int* out_count)
{
    int dbg_count = 0;
    int win_count = 0;
    struct ToriRSChromePrim const* dbg = ToriRSChrome_Prims(&app->dev_overlay->ui, &dbg_count);
    struct ToriRSChromePrim const* win = ToriRSChrome_Prims(&app->plugin_win->ui, &win_count);

    assert(out_count);

    /*
     * Nothing to merge: hand the developer chrome's own array straight out, so
     * the common case -- no plugin window open -- costs exactly what it did
     * before this existed.
     *
     * WEB/BROWSER rebuild the window as DOM controls, so putting its prims in
     * the canvas as well would draw it twice. BUFFER is the only internal
     * presentation that consumes the display list here.
     */
    if( win_count == 0 || app->plugin_win->exec_kind != TORIRS_CHROME_EXEC_BUFFER ||
        app_plugin_page_on_surface(app) )
    {
        *out_count = dbg_count;
        return dbg;
    }

    if( !ChromeCompositor_MergedFor(
            app->chrome_compositor,
            app->dev_overlay->ui.build_serial,
            app->plugin_win->ui.build_serial,
            PanelOverlayStage_Revision(&app->panel_wells)) )
    {
        int n = ChromeCompositor_MergeLists(app->chrome_compositor, dbg, dbg_count, win, win_count);
        for( int i = 0;
             i < PanelOverlayStage_KeptCount(&app->panel_wells) && n < CHROME_COMPOSITOR_PRIMS_MAX;
             i++ )
            if( app_panel_overlay_to_chrome(
                    app, i, ChromeCompositor_MergedSlot(app->chrome_compositor, n)) )
                n++;
        ChromeCompositor_MergeCommit(
            app->chrome_compositor,
            n,
            app->dev_overlay->ui.build_serial,
            app->plugin_win->ui.build_serial,
            PanelOverlayStage_Revision(&app->panel_wells));
    }
    return ChromeCompositor_Merged(app->chrome_compositor, out_count);
}

/*
 * The plugin page on its SURFACE: the strip beside the SDL rail, or the page's
 * pop-out window.
 *
 * The same display list the canvas would have drawn -- the window's prims, then
 * its custom wells' -- through the same frame translator and software backend
 * the canvas and the BMP dump use, so the page is pixel-for-pixel the in-canvas
 * one. Only when something moved: the window's build serial, the custom wells'
 * revision, or the surface's size (a pop-out, a docking, a window resize).
 */
void
app_chrome_plugin_surface_present(struct App* app)
{
    struct UITreeEmitDesc desc;
    struct ToriRSChromePrim const* win;
    uint32_t* pixels;
    int win_count = 0;
    int width = 0;
    int height = 0;
    int n;

    assert(app);
    if( !app_plugin_page_surface(app, &width, &height) ||
        !app->plugin_win->sync.exec.surface_present )
        return;
    /* Before the scene exists there is nothing to raster with; the keys stay
     * stale and the first frame that has one presents. */
    if( !app->scene || !ChromeCompositor_Soft(app->chrome_compositor) )
        return;
    if( ChromeCompositor_SurfaceFor(
            app->chrome_compositor, app->plugin_win->ui.build_serial,
            PanelOverlayStage_Revision(&app->panel_wells), width, height) )
        return;

    win = ToriRSChrome_Prims(&app->plugin_win->ui, &win_count);
    n = ChromeCompositor_SurfaceLists(app->chrome_compositor, win, win_count);
    for( int i = 0; i < PanelOverlayStage_KeptCount(&app->panel_wells) &&
                    n < CHROME_COMPOSITOR_SURFACE_PRIMS_MAX; i++ )
        if( app_panel_overlay_to_chrome(
                app, i, ChromeCompositor_SurfaceSlot(app->chrome_compositor, n)) )
            n++;

    pixels = ChromeCompositor_SurfacePixels(app->chrome_compositor, width, height);

    /* One debug-overlay descriptor, exactly what the emit pass builds for the
     * canvas, with the faces and skin asked of the bridge directly: there is
     * no overlay node behind a surface. */
    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_DEBUG_OVERLAY;
    desc.node_index = -1;
    desc.debug_prims = ChromeCompositor_SurfacePrims(app->chrome_compositor);
    desc.debug_prim_count = n;
    desc.debug_font_id[TORIRS_CHROME_FONT_SMALL] =
        UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_SMALL);
    desc.debug_font_id[TORIRS_CHROME_FONT_MENU] =
        UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_MENU);
    desc.debug_font_id[TORIRS_CHROME_FONT_BODY] =
        UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_BODY);
    desc.debug_skin_scene_id = UITreeSceneBridge_EnsureChromeSkin(&app->bridge);
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
        desc.debug_skin_atlas[i] = desc.debug_skin_scene_id >= 0 ? i : -1;
    desc.clip.x = 0;
    desc.clip.y = 0;
    desc.clip.w = width;
    desc.clip.h = height;
    UITreeCmd_RenderToPixelsWith(
        ChromeCompositor_Soft(app->chrome_compositor), app->scene, &desc, 1, (int*)pixels, width,
        height);
    app->plugin_win->sync.exec.surface_present(app->plugin_win->sync.exec.user, pixels, width, height);
    /* The surface is not in the canvas TORIRS_EXIT_BMP dumps: a headless run
     * names its own file for it (each present overwrites the last). */
    {
        char const* dump = getenv("TORIRS_CHROME_SURFACE_BMP");
        if( dump && dump[0] )
            bmp_write_file(dump, (int*)pixels, width, height);
        if( getenv("TORIRS_CHROME_DEBUG") )
            fprintf(stderr, "chrome: surface page presented %dx%d, %d prims\n", width, height, n);
    }

    ChromeCompositor_SurfaceCommit(
        app->chrome_compositor, app->plugin_win->ui.build_serial,
        PanelOverlayStage_Revision(&app->panel_wells), width, height);
}

/*
 * Point the tree's overlay components at the faces baked for the current
 * chrome scale.
 *
 * Split out because it runs twice: once when the scale is set, and again after
 * a tree rebuild, which resolves the ids itself at bake time but from whatever
 * scale the bridge is holding. Both paths end at the same three ids.
 */
static void
app_chrome_fonts_resolve(struct App* app)
{
    int small;
    int menu;
    int body;
    int ids[TORIRS_CHROME_FONT_SLOT_COUNT];

    assert(app);
    /* Before the tree exists there is nothing to point at, and the bake will
     * resolve these itself from the scale the bridge is now holding. Not a
     * contract violation: App_SetChromeScale is legitimately called at boot,
     * ahead of the first build. */
    if( !app->tree )
        return;
    small = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_SMALL);
    menu = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_MENU);
    body = UITreeSceneBridge_EnsureDebugFont(&app->bridge, TORIRS_CHROME_FONT_BODY);
    UITree_DebugOverlaySetFontIds(app->tree, small, menu, body);
    /* And the UI faces the editor's theme draws in (-1 each in a session
     * whose chrome names none: UITreeSceneBridge_SetChromeUiFonts). */
    UITreeSceneBridge_EnsureDebugFonts(&app->bridge, ids);
    UITree_DebugOverlaySetUiFontIds(app->tree, ids);
}

/*
 * The step the chrome tiles its parchment by: the size of the image the
 * bridge uploaded for the panel body at the chrome's scale (the tiled fill
 * blits each copy at its own size, and steps by this). At 1x it is the 1x
 * tile the init set; at 2x the 2x tier's, which is twice it; at 1.5x the
 * area-averaged image's, ToriRSChrome_ScalePx of it. A step left at the 1x
 * size would overdraw every copy with the next and show only each image's
 * top-left corner.
 */
static void
app_chrome_skin_tile(struct App* app)
{
    struct ToriRSChromeBakeImage const* body;

    assert(app);
    /* A build without the skin draws the flat look and tiles nothing. */
    if( !(app->dev_overlay->ui.skin_avail & ((uint64_t)1 << TORIRS_CHROME_SKIN_PANEL_BODY)) )
        return;
    body = ToriRSChromeBake_Skin(
        TORIRS_CHROME_SKIN_PANEL_BODY, ToriRSChrome_ScaleQ(&app->dev_overlay->ui));
    assert(body);
    app->dev_overlay->ui.skin_tile_w = body->w;
    app->dev_overlay->ui.skin_tile_h = body->h;
    app->plugin_win->ui.skin_tile_w = body->w;
    app->plugin_win->ui.skin_tile_h = body->h;
}

int
App_SetChromeScaleQ(
    struct App* app,
    int q)
{
    assert(app);
    /* Clamped, not asserted: this number comes from the DISPLAY, and a 4x
     * monitor is a fact about the world rather than a caller's bug. Chrome one
     * baked size below the display's density is a little small; an assert here
     * would be a crash on a machine nobody tested on. */
    if( q < TORIRS_CHROME_SCALE_Q_MIN )
        q = TORIRS_CHROME_SCALE_Q_MIN;
    if( q > TORIRS_CHROME_SCALE_Q_MAX )
        q = TORIRS_CHROME_SCALE_Q_MAX;
    if( ToriRSChrome_ScaleQ(&app->dev_overlay->ui) == q &&
        ToriRSChrome_ScaleQ(&app->plugin_win->ui) == q &&
        UITreeSceneBridge_ChromeScaleQ(&app->bridge) == q )
        return 0;

    /* The scale exactly, fractions included: the bridge registers the bake's
     * faces and skin at q (UITreeSceneBridge_SetChromeScaleQ), so what the
     * model lays out with quarter metrics is what the renderer paints. */
    ToriRSChrome_SetScaleQ(&app->dev_overlay->ui, q);
    /* Both instances, because there is one scale: the font ids resolved below
     * are shared, so a plugin window left at 1x would lay its rows out for a
     * face the renderer draws at 2x -- text overflowing boxes sized for a
     * smaller font, which is the exact failure SetScale exists to prevent. */
    ToriRSChrome_SetScaleQ(&app->plugin_win->ui, q);
    UITreeSceneBridge_SetChromeScaleQ(&app->bridge, q);
    app_chrome_skin_tile(app);
    app_chrome_fonts_resolve(app);
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome: scale q=%d (%d%%), bridge q=%d, tile %dx%d\n", q, q * 25,
            UITreeSceneBridge_ChromeScaleQ(&app->bridge), app->dev_overlay->ui.skin_tile_w,
            app->dev_overlay->ui.skin_tile_h);
    return 1;
}

int
App_SetChromeTextSmooth(
    struct App* app,
    int smooth)
{
    assert(app);
    smooth = smooth ? 1 : 0;
    if( ToriRSChrome_TextSmooth(&app->dev_overlay->ui) == smooth &&
        ToriRSChrome_TextSmooth(&app->plugin_win->ui) == smooth &&
        UITreeSceneBridge_ChromeTextSmooth(&app->bridge) == smooth )
        return 0;

    /* Both models (a DOM hears the mode with SCALE; the bake's own drawer
     * reads it per text prim, and SetTextSmooth builds the chrome's scale's
     * vector atlases first), and the bridge, whose faces are what the native
     * renderer draws: at a whole scale the tiers or the vector face, then the
     * font ids every overlay holds re-resolved. Once per move; the bake builds
     * an atlas once per (face, q) for the life of the process. */
    ToriRSChromeBake_SetTextSmooth(&app->dev_overlay->ui, smooth);
    ToriRSChromeBake_SetTextSmooth(&app->plugin_win->ui, smooth);
    UITreeSceneBridge_SetChromeTextSmooth(&app->bridge, smooth);
    app_chrome_fonts_resolve(app);
    if( getenv("TORIRS_CHROME_DEBUG") )
        fprintf(
            stderr, "chrome: text rendering %s (q=%d)\n",
            smooth ? "smooth always" : "bitmap at whole scales",
            UITreeSceneBridge_ChromeScaleQ(&app->bridge));
    return 1;
}

int
App_SetChromeScale(
    struct App* app,
    int scale)
{
    assert(app);
    if( scale < TORIRS_CHROME_SCALE_MIN )
        scale = TORIRS_CHROME_SCALE_MIN;
    /* Clamped, for App_SetChromeScaleQ's reason. */
    if( scale > TORIRS_CHROME_SCALE_MAX )
        scale = TORIRS_CHROME_SCALE_MAX;
    return App_SetChromeScaleQ(app, scale * TORIRS_CHROME_SCALE_Q_ONE);
}

int
App_ChromeScale(struct App const* app)
{
    assert(app);
    return ToriRSChrome_Scale(&app->dev_overlay->ui);
}

int
App_SetChromeCheckStyle(
    struct App* app,
    int style)
{
    assert(app);
    if( ToriRSChrome_CheckStyle(&app->dev_overlay->ui) == style )
        return 0;
    /* Both instances, because there is one answer to "what does a checkbox
     * look like here" -- the same rule App_SetChromeScale keeps, and for a
     * sharper reason: the two panels are commonly on screen together. */
    ToriRSChrome_SetCheckStyle(&app->dev_overlay->ui, style);
    ToriRSChrome_SetCheckStyle(&app->plugin_win->ui, style);
    return 1;
}

int
App_ChromeCheckStyle(struct App const* app)
{
    assert(app);
    return ToriRSChrome_CheckStyle(&app->dev_overlay->ui);
}

/**
 * OSRS key code -> the overlay's editing key, or TORIRS_CHROME_KEY_NONE.
 *
 * The overlay deliberately owns no keymap (see uitree_debug_overlay.h), so the
 * translation lives here, where the client's own key codes already are.
 * Printable characters do not come through this at all — they arrive as
 * `key_pressed` and go straight to ToriRSChrome_KeyChar.
 */

static int
app_dbgui_key_edit_from_osrs(int osrs_key)
{
    switch( osrs_key )
    {
    case TORIRS_OSRSKEY_BACKSPACE:
        return TORIRS_CHROME_KEY_BACKSPACE;
    case TORIRS_OSRSKEY_DELETE:
        return TORIRS_CHROME_KEY_DELETE;
    case TORIRS_OSRSKEY_ENTER:
        return TORIRS_CHROME_KEY_ENTER;
    case TORIRS_OSRSKEY_ESCAPE:
        return TORIRS_CHROME_KEY_ESCAPE;
    /* Arrows and home/end have no named constants; the keymap table spells
     * them (src/input/torirs_keymap.c: 96 left, 97 right, 98 up, 99 down,
     * 102 home, 103 end). */
    case 96:
        return TORIRS_CHROME_KEY_LEFT;
    case 97:
        return TORIRS_CHROME_KEY_RIGHT;
    /* Up and down a LINE, for a multiline field. The model ignores them on
     * every other kind, so routing them costs nothing where there is none. */
    case 98:
        return TORIRS_CHROME_KEY_UP;
    case 99:
        return TORIRS_CHROME_KEY_DOWN;
    case 102:
        return TORIRS_CHROME_KEY_HOME;
    case 103:
        return TORIRS_CHROME_KEY_END;
    default:
        return TORIRS_CHROME_KEY_NONE;
    }
}

/**
 * Does chrome DRAWN IN THIS CANVAS own the pointer at (x, y)?
 *
 * The game's interaction pass, the world hittest and the camera all run long
 * after the chrome handled this frame's input, and by then they cannot read
 * "the chrome took it" off the frame claim (`input_claims`) -- the shell takes
 * it on every frame as its own replay fence. They ask this instead, and a press, a
 * wheel or a right click over a panel stops there.
 *
 * The plugin window counts only under the BUFFER executor. Every other
 * presentation draws it somewhere else -- its own SDL window, a DOM, the
 * interface tree -- while the in-canvas model keeps the geometry it was laid
 * out with. That geometry is a ghost: hit-testable at a floating position
 * nothing draws, so honouring it would punch an invisible hole in the game.
 */
int
app_chrome_wants_pointer(
    struct App const* app,
    int x,
    int y)
{
    assert(app);
    if( ToriRSChrome_WantsPointer(&app->dev_overlay->ui, x, y) )
        return 1;
    if( app->plugin_win->visible && app->plugin_win->exec_kind == TORIRS_CHROME_EXEC_BUFFER &&
        !app_plugin_page_on_surface(app) &&
        ToriRSChrome_WantsPointer(&app->plugin_win->ui, x, y) )
        return 1;
    return 0;
}

/**
 * Feed one frame's pointer and keyboard to one chrome instance.
 *
 * Instance-taking rather than reaching for app->dev_overlay->ui, because the plugin
 * window is a chrome of its own: routing input is the half of "a second chrome
 * is a second of all of this" that genuinely would have been duplicated, and
 * this is the one copy both instances share.
 *
 * Chrome first, then the game (README_DEBUG_OVERLAY.md §6): a click or drag
 * that lands on a panel must not also reach the world's click-to-walk
 * underneath it, which is what the frame claim (`input_claims`) says.
 */
void
app_chrome_route_input(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(ui);
    assert(input);

    /* The modifiers down, for the presses below: Shift+Alt (Option) on a
     * press anywhere on a window picks the window up (ToriRSChrome::mods),
     * the way to move one whose title bar is off the surface. */
    ToriRSChrome_SetModifiers(
        ui,
        (LibToriRS_Input_IsKeyHeld(input, TORIRSK_SHIFT) ? TORIRS_CHROME_MOD_SHIFT : 0u)
            | (LibToriRS_Input_IsKeyHeld(input, TORIRSK_ALT) ? TORIRS_CHROME_MOD_ALT : 0u)
            | (LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL) ? TORIRS_CHROME_MOD_CTRL : 0u));
    if( ToriRSChrome_MouseMove(ui, input->curr.mouse_x, input->curr.mouse_y) )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);
    /*
     * The press lands where the button actually went DOWN, not where the
     * pointer finished the frame.
     *
     * curr.mouse_x is the last position any event in this frame's batch
     * carried, and a press is one event in that batch. A finger crossing the
     * drag slop pushes move(landed), down(landed), move(now) in a single batch
     * (input/torirs_touch.c), so reading curr here grabbed a scrollbar a slop's
     * width from where the finger was put -- which pages the list instead of
     * taking the grip when the grip's edge is inside that gap. press_origin is
     * the position the down carried, and for a mouse it is the same number.
     */
    if( input->curr.mouse_button_down[TORIRSM_LEFT] &&
        ToriRSChrome_MouseDown(
            ui, input->press_origin_x[TORIRSM_LEFT], input->press_origin_y[TORIRSM_LEFT]) )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);
    if( input->curr.mouse_button_up[TORIRSM_LEFT] &&
        ToriRSChrome_MouseUp(ui, input->curr.mouse_x, input->curr.mouse_y) )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);

    /*
     * The SECONDARY button, on the DOWN edge and with no release half.
     *
     * The rest of this library only ever sees the left button, so a right
     * button has no `press` to be matched a release against; requiring one
     * would make the channel dead. The down edge is also where every other
     * secondary click in the client fires -- the world's minimenu opens on
     * mouse-down -- so a well and the game agree about when a right click
     * happened.
     *
     * Nothing but a CUSTOM well answers it, and the world's own minimenu is
     * already suppressed over this window by app_chrome_wants_pointer, so a
     * click a well declines is swallowed rather than opening a game menu
     * through the panel.
     */
    if( input->curr.mouse_button_down[TORIRSM_RIGHT] &&
        ToriRSChrome_SecondaryClick(
            ui, input->press_origin_x[TORIRSM_RIGHT], input->press_origin_y[TORIRSM_RIGHT]) )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);

    /* The wheel, so an open dropdown or a scrolling panel moves, plus the
     * trackpad's exact scroll and pinch, which only a GRAPH well (the map
     * editor's world map) takes -- everywhere else the integer wheel goes
     * exactly where it always went. Consumed when the chrome takes any of it,
     * or the camera would zoom behind it at the same time. An unclaimed pinch
     * is dropped: the desktop game has no pinch of its own. */
    {
        struct ToriRSChromeWheelFrame frame;
        memset(&frame, 0, sizeof(frame));
        frame.x = input->curr.mouse_x;
        frame.y = input->curr.mouse_y;
        frame.wheel = input->curr.mouse_wheel_y;
        frame.scroll_dx = input->curr.scroll_dx;
        frame.scroll_dy = input->curr.scroll_dy;
        frame.px_per_notch = input->curr.scroll_px_per_notch;
        frame.trackpad = input->curr.scroll_trackpad;
        frame.flipped = input->curr.scroll_flipped;
        frame.ctrl = input->key_held[TORIRSK_CTRL];
        frame.pinch_scale = input->curr.pinch_scale;
        frame.pinch_x = input->curr.pinch_x;
        frame.pinch_y = input->curr.pinch_y;
        frame.pinch_from_wheel = input->curr.pinch_from_wheel;
        if( ToriRSChrome_WheelFrame(ui, &frame) )
            ToriRS_InputClaimsTakeFrame(&app->input_claims);
    }

    app_chrome_route_keys(app, ui, input);
}

/**
 * Is the chrome's focus on something that TYPES -- a text field, or a code
 * view? Then a Ctrl+letter is a chord aimed at it and never a character, and
 * the keys of the frame are its and not a panel's shortcuts.
 */
int
app_chrome_focus_types(struct ToriRSChrome const* ui)
{
    assert(ui);
    if( ToriRSChrome_InputFocused(ui) >= 0 )
        return 1;
    return ui->focus >= 0 && ui->widgets[ui->focus].kind == TORIRS_CHROME_W_CODE;
}

/**
 * The clipboard chords of a focused chrome text field (2026-11-09: a field
 * had no way in or out but the keyboard, and was cleared one Backspace a
 * character):
 *
 *   Ctrl+A   select the whole content (a double-click in a one-line field
 *            does the same, inside the chrome);
 *   Ctrl+C   the whole content to the OS clipboard, selected or not -- the
 *            only selection there is is "all of it";
 *   Ctrl+X   with the content selected, to the clipboard and out of the field;
 *   Ctrl+V   the OS clipboard's text in at the caret, over the content when
 *            it is selected (the chrome cuts it at the field's capacity and
 *            turns line breaks into spaces: ToriRSChrome_InputInsertText).
 *
 * Here and not in the chrome because the chrome reaches nothing outside the C
 * library: the clipboard is the platform's. Ctrl is Command on a Mac, the way
 * the platform reports it for every other chord. With Alt held as well it is
 * not a chord -- AltGr is Ctrl+Alt, and it types.
 *
 * @return 1 when a chord ran (the frame's input is the chrome's).
 */
static int
app_chrome_clipboard_chords(
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input)
{
    int const field = ToriRSChrome_InputFocused(ui);

    if( field < 0 )
        return 0;
    if( !LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL) || LibToriRS_Input_IsKeyHeld(input, TORIRSK_ALT) )
        return 0;

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_A) )
    {
        (void)ToriRSChrome_InputSelectAll(ui);
        return 1;
    }
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_C) )
    {
        /* 0 back is a machine with no clipboard to take it; the field is
         * unchanged either way, so there is nothing to undo or to say. */
        (void)PlatformClipboard_SetText(ToriRSChrome_Text(ui, field));
        return 1;
    }
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_X) )
    {
        /* Cut what is selected: with nothing selected there is nothing to
         * cut, and the field keeps its content. Only once the clipboard has
         * it -- a cut that loses the text is worse than one that does not
         * cut. */
        if( ToriRSChrome_InputAllSelected(ui) && PlatformClipboard_SetText(ToriRSChrome_Text(ui, field)) )
            (void)ToriRSChrome_KeyEdit(ui, TORIRS_CHROME_KEY_DELETE);
        return 1;
    }
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_V) )
    {
        /* Room for a field's worth of text with its line ends and a lead of
         * things the field will not take; the chrome cuts what does not fit. */
        char text[TORIRS_CHROME_INPUT_MAX * 4];

        if( PlatformClipboard_GetText(text, sizeof(text)) > 0 )
            (void)ToriRSChrome_InputInsertText(ui, text);
        return 1;
    }
    return 0;
}

/*
 * The keyboard half of the routing above, on its own because a browser-backed
 * presentation needs exactly this half: pointer intents arrive through the
 * browser bridge, but its text fields are the model's, so typing still has
 * to reach the model. Routing the MOUSE too would hand clicks to the in-canvas
 * window's ghost -- laid out and hit-testable at its floating position even
 * though nothing draws it there.
 *
 * `key_typed` carries the OSRS key code and `key_pressed` the typed character
 * (see torirs_input.h), so editing keys and printable bytes come off different
 * fields of the same event.
 *
 * Safe to run for any visible panel: with nothing focused the chrome consumes
 * neither, so a panel that is merely on screen -- the developer readout, say
 * -- never swallows a keystroke meant for the game.
 */
void
app_chrome_route_keys(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input)
{
    int chord;

    assert(app);
    assert(ui);
    assert(input);

    if( app_chrome_clipboard_chords(ui, input) )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);
    /* Ctrl (without Alt) held over something that types: the letters of this
     * frame are chords, not characters. Windows never sends the character of
     * a Ctrl+letter; other platforms may, and Ctrl+V must not paste and then
     * type a "v" after it. */
    chord = LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL) && !LibToriRS_Input_IsKeyHeld(input, TORIRSK_ALT)
            && app_chrome_focus_types(ui);

    for( int i = 0; i < input->key_event_count; i++ )
    {
        struct LibToriRS_KeyEvent const* ev = &input->key_events[i];
        int consumed = 0;

        if( ev->key_pressed >= 32 && ev->key_pressed < 127 )
            consumed = chord ? 1 : ToriRSChrome_KeyChar(ui, ev->key_pressed);
        else
        {
            int const edit = app_dbgui_key_edit_from_osrs(ev->key_typed);
            if( edit != TORIRS_CHROME_KEY_NONE )
                consumed = ToriRSChrome_KeyEdit(ui, edit);
        }
        if( consumed )
            ToriRS_InputClaimsTakeFrame(&app->input_claims);
    }
}

/*
 * The dev panels' tick: the panel toggles, the pick filter, this frame's input
 * routed into the developer chrome, the keyboard holders' shared work, and the
 * chrome built.
 *
 * Runs in the early chrome phase after the plugin window's tick (app_frame.c),
 * before the BOOTING early-out, and before anything downstream reads the frame
 * claim for click-to-walk. The side-apps' parts run at its points
 * (SIDE_APP_PHASE_DEV_PANELS_*, and the keys of SIDE_APP_PHASE_DEV_PANELS) in
 * registration order (app_tools.c).
 */
void
app_dev_panels_tick(
    struct App* app,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    SideApps_Frame(app->side_apps, SIDE_APP_PHASE_DEV_PANELS_BEGIN, app_side_app_host(app), input);

    /* The side-apps' panel toggles (the loc editor's, then the hover
     * footprint's, then the map editor's), suppressed while a chat line has
     * focus: typing a message must not flip debug chrome. */
    if( !app_text_input_focused(app) )
        app_side_app_deliver_keys(app, input, SIDE_APP_PHASE_DEV_PANELS);

    /* The side-apps that inspect locs (the loc editor, the hover footprint,
     * the map editor's shown panel) see locs the pick classifier drops by
     * default -- walls, fences, gravel, ground decor: everything with no ops
     * on it, which is most of what a placement or footprint question is
     * actually about. Told here, once, from the toggles that own the state, so
     * no tool has to reach into the pick path itself. */
    WorldEntity_SceneryDebugSetTools(SideApps_AnyInspectsScenery(app->side_apps));

    SideApps_Frame(app->side_apps, SIDE_APP_PHASE_DEV_PANELS_HOVER, app_side_app_host(app), input);

    /*
     * Overlay input, for ANY visible chrome panel.
     *
     * Asked of the chrome rather than listed here, because the list was the
     * bug: this was gated on the loc editor, then on the loc editor OR the map
     * editor, and every panel added after that -- the plugin settings panel
     * among them -- silently got no clicks. A panel whose checkboxes cannot be
     * ticked reads as broken, not as unrouted, so the gate is now "is anything
     * of this instance on screen" and a new panel needs no gate edit at all.
     */
    if( ToriRSChrome_HasVisiblePanel(&app->dev_overlay->ui) )
        app_chrome_route_input(app, &app->dev_overlay->ui, input);

    if( SideApps_AnyHoldsKeyboard(app->side_apps) || app_map_editor_panel_shown(app) )
    {
        /* A chat line stealing W/A/S/D/R/Space/Backspace would make an editor
         * panel unusable, so force it (and the modal chat variants) closed for
         * as long as one is open rather than merely suppressing the toggle key
         * like the developer overlay does. The later chat-focus code (Enter /
         * click-in-chat-region) is itself gated on the same holders so it
         * cannot steal focus back mid-session.
         *
         * Deliberately NOT part of the generic routing above: this is an
         * editor's claim on the whole keyboard, and a panel a player may leave
         * open beside the game -- the plugin window -- must not disable chat
         * for as long as it is up. */
        UIChatLine_SetInputActive(&app->chat_line, 0);
        app->chat.social_input_open = 0;
        app->chat.dialog_input_open = 0;

        /*
         * A focused model view owns the movement keys: WASD orbits, E/F zooms,
         * arrows orbit too. Every accepted key re-renders with the camera
         * held, consumes the frame, and never reaches the world camera --
         * which also checks this focus itself, for the keys that arrive on
         * frames this block does not see.
         *
         * HELD, not the down edge. The world camera flies for as long as W is
         * down and the preview has to answer the same gesture the same way: on
         * the edge alone, holding a key nudged the model once and then sat
         * there, which is indistinguishable from a control that does not work.
         * The steps are per FRAME because of it -- a fifth of the old edge
         * step, so a press-and-release is still a small turn and a hold is a
         * smooth orbit rather than a spin.
         */
        if( app_modelview_focused(app) )
        {
            struct EditorPreviewCamera* camera = &app->dev_overlay->preview_camera;
            int took = 0;
            int const yaw_step = 12;
            int const pitch_step = 6;

            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
            {
                EditorPreviewCamera_Orbit(camera, 0, -yaw_step);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
            {
                EditorPreviewCamera_Orbit(camera, 0, yaw_step);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
            {
                EditorPreviewCamera_Orbit(camera, -pitch_step, 0);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
            {
                EditorPreviewCamera_Orbit(camera, pitch_step, 0);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_E) )
            {
                EditorPreviewCamera_Zoom(camera, -1);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
            {
                EditorPreviewCamera_Zoom(camera, 1);
                took = 1;
            }
            if( took )
            {
                /* Orbit and Zoom have already said this is the same model. */
                ToriRS_InputClaimsTakeFrame(&app->input_claims);
                RedrawRequest_Raise(&app->redraw_request);
            }
        }
    }

    /* The side-apps' own keys and rows (the loc editor's shortcuts), after the
     * routing above. The map editor's keys are its hotkey table's
     * (app_editor_hotkeys.c). */
    SideApps_Frame(app->side_apps, SIDE_APP_PHASE_DEV_PANELS_INPUT, app_side_app_host(app), input);

    if( ToriRSChrome_Build(&app->dev_overlay->ui) )
    {
        RedrawRequest_Raise(&app->redraw_request);
        ToriRSChrome_DamageClear(&app->dev_overlay->ui);
    }
}
