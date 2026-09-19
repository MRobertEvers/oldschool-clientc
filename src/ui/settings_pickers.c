#include "ui/settings_pickers.h"

#include "log/torirs_log.h"
#include "ui/uitree.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_popup_place.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
UISettingsPickers_Init(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome)
{
    assert(pickers);
    assert(chrome);

    /* Built empty and hidden. A picker's rows are the ROW's -- title, default
     * swatch -- and are only known once a control has been clicked, so every
     * open clears and rebuilds them. Declared here all the same, so the handles
     * are valid from the first frame and no path has to test for a panel that
     * does not exist yet. */
    memset(pickers, 0, sizeof(*pickers));
    pickers->colour_panel =
        ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, 8, 40, 0, "Colour");
    ToriRSChrome_PanelSetFramed(chrome, pickers->colour_panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, pickers->colour_panel, 0);
    pickers->colour_pick = -1;
    pickers->colour_default_button = -1;
    pickers->colour_close_button = -1;

    pickers->number_panel =
        ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, 8, 40, 0, "Value");
    ToriRSChrome_PanelSetFramed(chrome, pickers->number_panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, pickers->number_panel, 0);
    pickers->number_input = -1;
    pickers->number_close_button = -1;
}

bool
UISettingsPickers_ColourVisible(struct UISettingsPickers const* pickers)
{
    assert(pickers);
    return pickers->colour_visible != 0;
}

bool
UISettingsPickers_NumberVisible(struct UISettingsPickers const* pickers)
{
    assert(pickers);
    return pickers->number_visible != 0;
}

/* =========================================================================
 * All Settings: the colour rows
 *
 * A colour row in the All Settings panel (interface 134) is a title, a
 * description and a swatch with a "Select" op on it, and that op's script --
 * `settings_colour_input_click`, cache script 4183 -- is two lines long:
 *
 *     [clientscript,settings_colour_input_click](int $int0, int $int1)
 *     if (~settings_op_checker($int0, $int1) = 0) {
 *         return;
 *     }
 *
 * That is the whole body. `~settings_op_checker` plays the panel's click sound
 * and, for a row the player is not allowed to change, prints the row's own
 * refusal message. Nothing writes a colour, because in the reference the
 * picker is the ENGINE's: it opens its own, and it writes the row's varp
 * itself. Read one way that makes every colour row in the panel inert here --
 * "Tile highlight colour" showed the default green swatch, said what it was
 * for, and did nothing at all when clicked. Read the other way it is the same
 * arrangement as the two Activities buttons and the client layout dropdown:
 * the cache has stated everything except the part only a client can do.
 *
 * So this is that part. RS_CS2Host_ScriptStarted catches the click with its
 * arguments intact and resolves the row -- setting id, title, default swatch,
 * and the varp the read hub was seen reading for it. Here that becomes a
 * picker on the HSL16 axes the renderer actually draws in, and its value goes
 * back into the varp the way the row stores it: `colour + 1`, so that zero
 * keeps meaning "never chosen".
 *
 * Committing to the varp is the whole apply. The cache does the rest of the
 * work it always did -- writing the varp fires the var-transmit hooks the row
 * itself installed, so `settings_colour_input_update` re-fills the swatch and,
 * for the tile markers, clientscript 4763 re-runs HIGHLIGHT_TILE_SETUP on
 * group 6 in the new colour. Nothing here knows what a tile marker is.
 * ========================================================================= */

static void
settings_colour_close(struct UISettingsPickers* pickers, struct ToriRSChrome* chrome)
{
    assert(pickers);
    assert(chrome);
    pickers->colour_visible = 0;
    if( pickers->colour_panel >= 0 )
        ToriRSChrome_PanelSetVisible(chrome, pickers->colour_panel, 0);
}

/**
 * Write `rgb` to the open row's varp, in the row's own encoding.
 *
 * `colour + 1`, which is what `settings_get_colour` reads back with
 * `calc(%var<n> - 1)` and what makes a varp of 0 mean "never chosen" rather
 * than "black".
 */
static void
settings_colour_commit(
    struct UISettingsPickers* pickers,
    UISettingsPickerCommitFn commit,
    void* userdata,
    uint32_t rgb)
{
    assert(pickers);
    assert(commit);
    /* A picker is only ever opened for a row whose varp is known, so this is a
     * contract and not a state to tolerate: a commit with nowhere to go would
     * be a picker the user is dragging that changes nothing. */
    assert(pickers->colour_request.varp_id >= 0);
    commit(userdata, pickers->colour_request.varp_id, (int)(rgb & 0xFFFFFFu) + 1);
}

/** Put the picker beside the swatch that opened it, clamped onto the canvas. */
static void
settings_colour_place(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    int component_id)
{
    int scale;
    int width;
    int32_t idx;
    struct UITreeComponent const* anchor = NULL;
    struct UIPopupPlacement placement;

    assert(pickers);
    assert(chrome);
    scale = ToriRSChrome_Scale(chrome);
    width = 230 * scale;
    idx = tree && component_id >= 0 ? UITree_FindByComponentId(tree, component_id) : -1;
    if( idx >= 0 )
        anchor = &tree->components[idx];

    /* Two thirds: the colour picker's axis popup drops BELOW the panel, so it
     * needs room under itself as well as for itself. */
    placement = UITree_PlacePopupBesideAnchor(
        UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H,
        width,
        anchor != NULL,
        anchor ? anchor->position.abs_x : 0,
        anchor ? anchor->position.abs_y : 0,
        8 * scale,
        2,
        3);

    ToriRSChrome_PanelSetFixedWidth(chrome, pickers->colour_panel, width);
    ToriRSChrome_PanelMove(chrome, pickers->colour_panel, placement.x, placement.y);
}

void
UISettingsPickers_OpenColour(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    struct RS_CS2SettingsColourRequest const* req)
{
    assert(pickers);
    assert(chrome);
    assert(req);

    if( pickers->colour_panel < 0 )
        return;
    if( req->varp_id < 0 )
    {
        /* The read hub never named a varp for this row, so there is nowhere to
         * put an answer. Said out loud rather than opening a picker whose
         * every move would be discarded. */
        TORIRS_LOG(
            "settings: colour row %d (%s) has no varp; not opening a picker\n",
            req->setting_id,
            req->label[0] ? req->label : "unnamed");
        return;
    }

    pickers->colour_request = *req;
    ToriRSChrome_PanelClearWidgets(chrome, pickers->colour_panel);
    ToriRSChrome_PanelSetTitle(
        chrome, pickers->colour_panel, req->label[0] ? req->label : "Colour");
    /* Seeded through NearestRgb, not the reference quantiser: this value is
     * read back and re-shown every time the row is opened, and the reference
     * round trip moves nearly every entry by a shade each pass. */
    pickers->colour_pick = ToriRSChrome_ColorPick(
        chrome,
        pickers->colour_panel,
        "Colour",
        ToriRSChrome_Hsl16NearestRgb((uint32_t)req->colour & 0xFFFFFFu));
    pickers->colour_default_button =
        ToriRSChrome_Button(chrome, pickers->colour_panel, "Default");
    pickers->colour_close_button =
        ToriRSChrome_Button(chrome, pickers->colour_panel, "Done");
    ToriRSChrome_PanelSetClosable(chrome, pickers->colour_panel, 1);

    settings_colour_place(pickers, chrome, tree, req->component_id);
    ToriRSChrome_PanelSetVisible(chrome, pickers->colour_panel, 1);
    pickers->colour_visible = 1;
}

/*
 * Open, drive and commit the picker. Called once a frame, after the developer
 * overlay's tick has already routed this frame's input into dbg_ui.
 *
 * The activation is PEEKED and only taken when it belongs to this panel.
 * dbg_ui's activation latch is shared with the loc editor and the map editor,
 * and draining it unconditionally is how the loc editor once swallowed the map
 * editor's clicks -- a dropdown that showed the new value while nothing
 * changed. Peeking costs nothing and cannot do that to anyone.
 */
int
UISettingsPickers_ColourTick(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    UISettingsPickerCommitFn commit,
    void* userdata)
{
    int activated;
    int changed = 0;

    assert(pickers);
    assert(chrome);
    assert(commit);

    if( !pickers->colour_visible )
        return 0;

    /*
     * The panel's own Close button hid it; the flag above is this side's idea
     * of whether the picker is up, and left unreconciled the next click on the
     * same swatch would "reopen" something that is already open.
     */
    if( pickers->colour_panel >= 0 && !chrome->panels[pickers->colour_panel].visible )
    {
        pickers->colour_visible = 0;
        return 1;
    }

    /*
     * Follow the panel out of existence.
     *
     * All Settings is opened and closed by the interface stack, and a picker
     * still floating over the game after the panel it belongs to is gone has
     * nothing to point at.
     *
     * Asked of the GROUP -- the interface the swatch's component id names in
     * its high half -- and not of the component. A colour row's swatch is a
     * DYNAMIC child, created by `cc_create` on a container with a component id
     * of its own, so looking that id up finds the CONTAINER: it is present for
     * as long as any of the interface is, which made the test true forever and
     * left the picker over the world after the panel had gone. The group is
     * the question actually being asked -- is the panel still open.
     */
    if( pickers->colour_request.component_id >= 0 && tree &&
        !UITree_GroupPresent(tree, pickers->colour_request.component_id >> 16) )
    {
        settings_colour_close(pickers, chrome);
        return 1;
    }

    activated = chrome->activated;
    if( activated < 0 )
        return changed;
    if( activated == pickers->colour_pick )
    {
        (void)ToriRSChrome_TakeActivated(chrome);
        changed = 1;
        settings_colour_commit(
            pickers,
            commit,
            userdata,
            ToriRSChrome_Hsl16ToRgb(
                ToriRSChrome_ColorPickValue(chrome, pickers->colour_pick)));
    }
    else if( activated == pickers->colour_default_button )
    {
        /* The DEFAULT is committed verbatim, not as the palette entry nearest
         * to it. `param_1230` is a colour the cache authored and the row draws
         * its swatch in before anyone picks; restoring it as an approximation
         * would mean "Default" never quite got back to where the row started.
         * The picker still shows the nearest entry, because that is the only
         * thing its axes can hold -- and what it shows is honestly what the
         * next pick would produce. */
        uint32_t const rgb = (uint32_t)pickers->colour_request.default_colour & 0xFFFFFFu;
        (void)ToriRSChrome_TakeActivated(chrome);
        changed = 1;
        ToriRSChrome_ColorPickSet(
            chrome, pickers->colour_pick, ToriRSChrome_Hsl16NearestRgb(rgb));
        settings_colour_commit(pickers, commit, userdata, rgb);
    }
    else if( activated == pickers->colour_close_button )
    {
        (void)ToriRSChrome_TakeActivated(chrome);
        changed = 1;
        settings_colour_close(pickers, chrome);
    }

    if( ToriRSChrome_Build(chrome) )
    {
        changed = 1;
        ToriRSChrome_DamageClear(chrome);
    }
    return changed;
}

/* =========================================================================
 * All Settings: the number-input rows
 *
 * The numeric twin of the colour block above, and it exists for the identical
 * reason. A number row -- built by `settings_create_input_setting`, cache
 * script 3856 -- draws its value in a boxed field with a "Select" op, and that
 * op's script is
 *
 *     [clientscript,settings_input_op](int $int0, int $int1)
 *     if (~settings_op_checker($int0, $int1) = 0) {
 *         return;
 *     }
 *     %varbit16074 = 0;
 *
 * -- a click sound, the row's refusal message when it is blocked, and a flag
 * that closes the panel's search box. Nothing types anything, because in the
 * reference the entry is the ENGINE's: `%varbit16075` ("a settings row is
 * being edited") is read by three clientscripts and written by none, and
 * `settings_input_timer` blinks a caret in a field the cache never fills.
 *
 * Untyped, those rows are not merely inert, they are the feature: the five
 * ground-items price tiers decide which of the six colours a pile's name is
 * drawn in, and `ground_items_max_lines` decides how many names appear at all.
 * At their zero default every pile draws in the tier-5 colour and, with the
 * line limit at zero, nothing draws.
 *
 * Committing to the varp is the whole apply, exactly as it is for a colour:
 * the row installed its own var-transmit hook (`settings_input_setting_
 * transmit`), so the field repaints itself and the overlay's own hooks pick
 * the new thresholds up.
 * ========================================================================= */

static void
settings_number_close(struct UISettingsPickers* pickers, struct ToriRSChrome* chrome)
{
    assert(pickers);
    assert(chrome);
    pickers->number_visible = 0;
    if( pickers->number_panel >= 0 )
        ToriRSChrome_PanelSetVisible(chrome, pickers->number_panel, 0);
}

/*
 * Commit what is typed in the box.
 *
 * Stored PLAIN, unlike a colour row's `value + 1`: zero is a real answer for
 * every one of these rows (a threshold of 0 colours everything at that tier,
 * a line limit of 0 hides the overlay), so there is no never-chosen sentinel
 * to make room for -- which is also why `settings_get_number_input` reads them
 * back with a bare `%var<n>` and not a `calc(%var<n> - 1)`.
 *
 * A field emptied and confirmed commits zero rather than being ignored: "off"
 * is a thing these rows can be set to, and the row's own `param_1113` is the
 * word it draws for it.
 */
static void
settings_number_commit(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    UISettingsPickerCommitFn commit,
    void* userdata)
{
    char const* text;
    long value;

    assert(pickers);
    assert(chrome);
    assert(commit);
    /* A box is only ever opened for a row whose varp is known. */
    assert(pickers->number_request.varp_id >= 0);
    text = ToriRSChrome_Text(chrome, pickers->number_input);
    value = strtol(text, NULL, 10);
    /* Clamped rather than asserted: this is a number a person typed, and
     * "2000000000000" is a typo and not a caller's bug. The floor is zero
     * because none of these rows means anything negative -- a threshold below
     * nothing would colour every pile at that tier for ever. */
    if( value < 0 )
        value = 0;
    if( value > INT_MAX )
        value = INT_MAX;
    commit(userdata, pickers->number_request.varp_id, (int)value);
}

/** Put the box beside the field that opened it, clamped onto the canvas. */
static void
settings_number_place(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    int component_id)
{
    int scale;
    int width;
    int32_t idx;
    struct UITreeComponent const* anchor = NULL;
    struct UIPopupPlacement placement;

    assert(pickers);
    assert(chrome);
    scale = ToriRSChrome_Scale(chrome);
    width = 200 * scale;
    idx = tree && component_id >= 0 ? UITree_FindByComponentId(tree, component_id) : -1;
    if( idx >= 0 )
        anchor = &tree->components[idx];

    /* Three quarters, not two thirds: a number entry has no axis popup under
     * it, so it may sit lower than the colour picker. */
    placement = UITree_PlacePopupBesideAnchor(
        UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H,
        width,
        anchor != NULL,
        anchor ? anchor->position.abs_x : 0,
        anchor ? anchor->position.abs_y : 0,
        8 * scale,
        3,
        4);

    ToriRSChrome_PanelSetFixedWidth(chrome, pickers->number_panel, width);
    ToriRSChrome_PanelMove(chrome, pickers->number_panel, placement.x, placement.y);
}

void
UISettingsPickers_OpenNumber(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    struct RS_CS2SettingsNumberRequest const* req)
{
    char value[32];
    char label[128];

    assert(pickers);
    assert(chrome);
    assert(req);

    if( pickers->number_panel < 0 )
        return;
    if( req->varp_id < 0 )
    {
        /* The read hub never named a varp for this row, so there is nowhere to
         * put an answer. Said out loud rather than opening a box whose every
         * keystroke would be discarded. */
        TORIRS_LOG(
            "settings: number row %d (%s) has no varp; not opening an entry\n",
            req->setting_id,
            req->label[0] ? req->label : "unnamed");
        return;
    }

    pickers->number_request = *req;
    snprintf(value, sizeof(value), "%d", req->value);
    /* The row's own suffix on the box's label, so the two agree about what is
     * being typed -- "Value (gp)" over a field the panel prints as "20,000 gp".
     * Its zero-word goes in the same place, because it is the other half of
     * what this number means to the row. */
    if( req->suffix[0] && req->zero_label[0] )
        snprintf(label, sizeof(label), "Value (%s, 0 = %s)", req->suffix, req->zero_label);
    else if( req->suffix[0] )
        snprintf(label, sizeof(label), "Value (%s)", req->suffix);
    else if( req->zero_label[0] )
        snprintf(label, sizeof(label), "Value (0 = %s)", req->zero_label);
    else
        snprintf(label, sizeof(label), "Value");

    ToriRSChrome_PanelClearWidgets(chrome, pickers->number_panel);
    ToriRSChrome_PanelSetTitle(
        chrome, pickers->number_panel, req->label[0] ? req->label : "Value");
    pickers->number_input =
        ToriRSChrome_TextInput(chrome, pickers->number_panel, label, value);
    pickers->number_close_button =
        ToriRSChrome_Button(chrome, pickers->number_panel, "Done");
    ToriRSChrome_PanelSetClosable(chrome, pickers->number_panel, 1);

    settings_number_place(pickers, chrome, tree, req->component_id);
    ToriRSChrome_PanelSetVisible(chrome, pickers->number_panel, 1);
    pickers->number_visible = 1;

    /* fprintf, and gated on its own name: a dbg_ui panel reaches the platform
     * renderer as ToriRSChrome_Prims and is invisible to every BMP path this
     * client has, so a screenshot cannot answer "did the box open" -- and
     * TORIRS_LOG is stripped from the optimized build, which is the only build
     * worth taking a screenshot of. Same choice as TORIRS_GROUND_ITEMS_DEBUG. */
    if( getenv("TORIRS_SETTINGS_DEBUG") )
        fprintf(
            stderr,
            "settings: number entry open, setting=%d varp=%d value=%d \"%s\"\n",
            req->setting_id,
            req->varp_id,
            req->value,
            req->label);
}

/* Open, drive and commit the entry. The activation is PEEKED and only taken
 * when it belongs to this panel, for the same reason the colour tick peeks. */
int
UISettingsPickers_NumberTick(
    struct UISettingsPickers* pickers,
    struct ToriRSChrome* chrome,
    struct UITree const* tree,
    UISettingsPickerCommitFn commit,
    void* userdata)
{
    int activated;
    int changed = 0;

    assert(pickers);
    assert(chrome);
    assert(commit);

    if( !pickers->number_visible )
        return 0;

    /* The panel's own Close button hid it. */
    if( pickers->number_panel >= 0 && !chrome->panels[pickers->number_panel].visible )
    {
        pickers->number_visible = 0;
        return 1;
    }

    /* Follow All Settings out. Asked of the GROUP, not the component: the
     * field is a dynamic child whose component id names its container, which
     * outlives the row. Same trap as the colour picker's. */
    if( pickers->number_request.component_id >= 0 && tree &&
        !UITree_GroupPresent(tree, pickers->number_request.component_id >> 16) )
    {
        settings_number_close(pickers, chrome);
        return 1;
    }

    activated = chrome->activated;
    if( activated < 0 )
        return changed;
    if( activated == pickers->number_input )
    {
        /* Enter, which is when a chrome text input activates. The box stays up
         * so a mistyped threshold can be corrected without clicking the row
         * again. */
        (void)ToriRSChrome_TakeActivated(chrome);
        changed = 1;
        settings_number_commit(pickers, chrome, commit, userdata);
    }
    else if( activated == pickers->number_close_button )
    {
        (void)ToriRSChrome_TakeActivated(chrome);
        changed = 1;
        settings_number_commit(pickers, chrome, commit, userdata);
        settings_number_close(pickers, chrome);
    }

    if( ToriRSChrome_Build(chrome) )
    {
        changed = 1;
        ToriRSChrome_DamageClear(chrome);
    }
    return changed;
}
