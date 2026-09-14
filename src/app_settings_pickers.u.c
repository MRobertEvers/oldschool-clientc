/*
 * The two settings PICKERS the client builds itself: the colour swatch editor
 * and the number-input row. Neither exists in the cache -- the rows that open
 * them are cache-authored, but what they open is this.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader.
 *
 * Where the popup goes is not here -- both pickers now ask
 * UITree_PlacePopupBesideAnchor, which is why they agree about it. What stays
 * is the part that needs an App: which row opened, what it is bound to, and
 * what to write back when the picker closes.
 *
 * @see src/app.c, src/ui/uitree_popup_place.h
 */

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
app_settings_colour_close(struct App* app)
{
    assert(app);
    app->settings_colour_visible = 0;
    if( app->settings_colour_panel >= 0 )
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_colour_panel, 0);
}

/**
 * Write `rgb` to the open row's varp, in the row's own encoding.
 *
 * `colour + 1`, which is what `settings_get_colour` reads back with
 * `calc(%var<n> - 1)` and what makes a varp of 0 mean "never chosen" rather
 * than "black".
 */
static void
app_settings_colour_commit(
    struct App* app,
    uint32_t rgb)
{
    assert(app);
    /* A picker is only ever opened for a row whose varp is known, so this is a
     * contract and not a state to tolerate: a commit with nowhere to go would
     * be a picker the user is dragging that changes nothing. */
    assert(app->settings_colour_req.varp_id >= 0);
    RS_CS2Host_ScriptWriteVarp(
        &app->host, app->settings_colour_req.varp_id, (int)(rgb & 0xFFFFFFu) + 1);
    app->need_redraw = 1;
}

/** Put the picker beside the swatch that opened it, clamped onto the canvas. */
static void
app_settings_colour_place(
    struct App* app,
    int component_id)
{
    int scale;
    int width;
    int32_t idx;
    struct UITreeComponent const* anchor = NULL;
    struct UIPopupPlacement placement;

    assert(app);
    scale = ToriRSChrome_Scale(&app->dbg_ui);
    width = 230 * scale;
    idx = app->tree && component_id >= 0 ? UITree_FindByComponentId(app->tree, component_id) : -1;
    if( idx >= 0 )
        anchor = &app->tree->components[idx];

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

    ToriRSChrome_PanelSetFixedWidth(&app->dbg_ui, app->settings_colour_panel, width);
    ToriRSChrome_PanelMove(&app->dbg_ui, app->settings_colour_panel, placement.x, placement.y);
}

static void
app_settings_colour_open(
    struct App* app,
    struct RS_CS2SettingsColourRequest const* req)
{
    assert(app);
    assert(req);

    if( app->settings_colour_panel < 0 )
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

    app->settings_colour_req = *req;
    ToriRSChrome_PanelClearWidgets(&app->dbg_ui, app->settings_colour_panel);
    ToriRSChrome_PanelSetTitle(
        &app->dbg_ui, app->settings_colour_panel, req->label[0] ? req->label : "Colour");
    /* Seeded through NearestRgb, not the reference quantiser: this value is
     * read back and re-shown every time the row is opened, and the reference
     * round trip moves nearly every entry by a shade each pass. */
    app->settings_colour_pick = ToriRSChrome_ColorPick(
        &app->dbg_ui,
        app->settings_colour_panel,
        "Colour",
        ToriRSChrome_Hsl16NearestRgb((uint32_t)req->colour & 0xFFFFFFu));
    app->settings_colour_default_btn =
        ToriRSChrome_Button(&app->dbg_ui, app->settings_colour_panel, "Default");
    app->settings_colour_close_btn =
        ToriRSChrome_Button(&app->dbg_ui, app->settings_colour_panel, "Done");
    ToriRSChrome_PanelSetClosable(&app->dbg_ui, app->settings_colour_panel, 1);

    app_settings_colour_place(app, req->component_id);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_colour_panel, 1);
    app->settings_colour_visible = 1;
    app->need_redraw = 1;
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
static void
app_settings_colour_tick(struct App* app)
{
    struct RS_CS2SettingsColourRequest req;
    int activated;

    assert(app);

    if( RS_CS2Host_TakeSettingsColourRequest(&app->host, &req) )
        app_settings_colour_open(app, &req);

    if( !app->settings_colour_visible )
        return;

    /*
     * The panel's own Close button hid it; the flag above is this side's idea
     * of whether the picker is up, and left unreconciled the next click on the
     * same swatch would "reopen" something that is already open.
     */
    if( app->settings_colour_panel >= 0 && !app->dbg_ui.panels[app->settings_colour_panel].visible )
    {
        app->settings_colour_visible = 0;
        return;
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
    if( app->settings_colour_req.component_id >= 0 && app->tree &&
        !UITree_GroupPresent(app->tree, app->settings_colour_req.component_id >> 16) )
    {
        app_settings_colour_close(app);
        return;
    }

    activated = app->dbg_ui.activated;
    if( activated < 0 )
        return;
    if( activated == app->settings_colour_pick )
    {
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_colour_commit(
            app,
            ToriRSChrome_Hsl16ToRgb(
                ToriRSChrome_ColorPickValue(&app->dbg_ui, app->settings_colour_pick)));
    }
    else if( activated == app->settings_colour_default_btn )
    {
        /* The DEFAULT is committed verbatim, not as the palette entry nearest
         * to it. `param_1230` is a colour the cache authored and the row draws
         * its swatch in before anyone picks; restoring it as an approximation
         * would mean "Default" never quite got back to where the row started.
         * The picker still shows the nearest entry, because that is the only
         * thing its axes can hold -- and what it shows is honestly what the
         * next pick would produce. */
        uint32_t const rgb = (uint32_t)app->settings_colour_req.default_colour & 0xFFFFFFu;
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        ToriRSChrome_ColorPickSet(
            &app->dbg_ui, app->settings_colour_pick, ToriRSChrome_Hsl16NearestRgb(rgb));
        app_settings_colour_commit(app, rgb);
    }
    else if( activated == app->settings_colour_close_btn )
    {
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_colour_close(app);
    }

    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
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
app_settings_number_close(struct App* app)
{
    assert(app);
    app->settings_number_visible = 0;
    if( app->settings_number_panel >= 0 )
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_number_panel, 0);
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
app_settings_number_commit(struct App* app)
{
    char const* text;
    long value;

    assert(app);
    /* A box is only ever opened for a row whose varp is known. */
    assert(app->settings_number_req.varp_id >= 0);
    text = ToriRSChrome_Text(&app->dbg_ui, app->settings_number_input);
    value = strtol(text, NULL, 10);
    /* Clamped rather than asserted: this is a number a person typed, and
     * "2000000000000" is a typo and not a caller's bug. The floor is zero
     * because none of these rows means anything negative -- a threshold below
     * nothing would colour every pile at that tier for ever. */
    if( value < 0 )
        value = 0;
    if( value > INT_MAX )
        value = INT_MAX;
    RS_CS2Host_ScriptWriteVarp(&app->host, app->settings_number_req.varp_id, (int)value);
    app->need_redraw = 1;
}

/** Put the box beside the field that opened it, clamped onto the canvas. */
static void
app_settings_number_place(
    struct App* app,
    int component_id)
{
    int scale;
    int width;
    int32_t idx;
    struct UITreeComponent const* anchor = NULL;
    struct UIPopupPlacement placement;

    assert(app);
    scale = ToriRSChrome_Scale(&app->dbg_ui);
    width = 200 * scale;
    idx = app->tree && component_id >= 0 ? UITree_FindByComponentId(app->tree, component_id) : -1;
    if( idx >= 0 )
        anchor = &app->tree->components[idx];

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

    ToriRSChrome_PanelSetFixedWidth(&app->dbg_ui, app->settings_number_panel, width);
    ToriRSChrome_PanelMove(&app->dbg_ui, app->settings_number_panel, placement.x, placement.y);
}

static void
app_settings_number_open(
    struct App* app,
    struct RS_CS2SettingsNumberRequest const* req)
{
    char value[32];
    char label[128];

    assert(app);
    assert(req);

    if( app->settings_number_panel < 0 )
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

    app->settings_number_req = *req;
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

    ToriRSChrome_PanelClearWidgets(&app->dbg_ui, app->settings_number_panel);
    ToriRSChrome_PanelSetTitle(
        &app->dbg_ui, app->settings_number_panel, req->label[0] ? req->label : "Value");
    app->settings_number_input =
        ToriRSChrome_TextInput(&app->dbg_ui, app->settings_number_panel, label, value);
    app->settings_number_close_btn =
        ToriRSChrome_Button(&app->dbg_ui, app->settings_number_panel, "Done");
    ToriRSChrome_PanelSetClosable(&app->dbg_ui, app->settings_number_panel, 1);

    app_settings_number_place(app, req->component_id);
    ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->settings_number_panel, 1);
    app->settings_number_visible = 1;
    app->need_redraw = 1;

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
static void
app_settings_number_tick(struct App* app)
{
    struct RS_CS2SettingsNumberRequest req;
    int activated;

    assert(app);

    if( RS_CS2Host_TakeSettingsNumberRequest(&app->host, &req) )
        app_settings_number_open(app, &req);

    if( !app->settings_number_visible )
        return;

    /* The panel's own Close button hid it. */
    if( app->settings_number_panel >= 0 && !app->dbg_ui.panels[app->settings_number_panel].visible )
    {
        app->settings_number_visible = 0;
        return;
    }

    /* Follow All Settings out. Asked of the GROUP, not the component: the
     * field is a dynamic child whose component id names its container, which
     * outlives the row. Same trap as the colour picker's. */
    if( app->settings_number_req.component_id >= 0 && app->tree &&
        !UITree_GroupPresent(app->tree, app->settings_number_req.component_id >> 16) )
    {
        app_settings_number_close(app);
        return;
    }

    activated = app->dbg_ui.activated;
    if( activated < 0 )
        return;
    if( activated == app->settings_number_input )
    {
        /* Enter, which is when a chrome text input activates. The box stays up
         * so a mistyped threshold can be corrected without clicking the row
         * again. */
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_number_commit(app);
    }
    else if( activated == app->settings_number_close_btn )
    {
        (void)ToriRSChrome_TakeActivated(&app->dbg_ui);
        app_settings_number_commit(app);
        app_settings_number_close(app);
    }

    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}
