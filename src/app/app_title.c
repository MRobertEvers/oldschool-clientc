/*
 * The title screen: flames, captions, field lines, and the login handshake.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_login_set_client_identity(struct App* app);
static void
app_login_refresh_jag_checksums(struct App* app);
static enum RS_TitleScreenPhase
app_title_phase(struct App const* app);
static enum RS_TitleLinkState
app_title_link_state(struct App const* app);

/*
 * The system-update line, or NULL when no update is pending.
 *
 * Reference drawScene: seconds = rebootTimer / 50 (fifty 20ms cycles to the
 * second), then minutes:seconds zero-padded. The 50 is spelled here as the
 * cycles-per-second it is, so the one place the client turns its own clock
 * into a wall-clock reading says which clock it means.
 *
 * The returned pointer is App-owned and lives until the next call, which is
 * the same frame lifetime the hovertext model has.
 */
char const*
app_reboot_timer_text(struct App* app)
{
    int seconds;
    int minutes;

    assert(app);
    if( app->reboot_timer == 0 )
        return NULL;

    seconds = app->reboot_timer / APP_LOGIC_CYCLES_PER_SECOND;
    minutes = seconds / 60;
    seconds %= 60;
    snprintf(
        app->reboot_timer_text,
        sizeof(app->reboot_timer_text),
        "System update in: %d:%02d",
        minutes,
        seconds);
    return app->reboot_timer_text;
}

/*
 * The blink period the title tree's focused input asks for, or 0 when nothing
 * on screen blinks.
 *
 * Read off the tree rather than kept on App because it is the widget's
 * property: two revisions may spell the caret differently and time it
 * differently, and both say so in their own INI.
 */
int
app_title_caret_blink(struct App const* app)
{
    assert(app);
    if( !app->tree )
        return 0;
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* comp = &app->tree->components[i];
        if( comp->freed || comp->type != UIELEM_BUILTIN_LOGIN_INPUT )
            continue;
        if( UITree_LoginInput(comp)->field != app->title.focus )
            continue;
        return UITree_LoginInput(comp)->caret_blink;
    }
    return 0;
}

/*
 * Light the braziers, once the title tree's art is resident.
 *
 * The fire burns in front of two 128-wide columns of the backdrop, so it needs
 * the composited panel before it can start -- which is also why this is not
 * done at bake time: the sprite arrives through the same async asset pass
 * everything else does.
 *
 * A profile with no backdrop gets no fire rather than a fire over black. That
 * is the undeclared-means-absent contract again, and it is the honest answer:
 * the flames are a lighting effect on a picture, and without the picture they
 * are just two glowing rectangles.
 */
void
app_title_flames_start(struct App* app)
{
    struct ToriRS_Sprite* runes;
    struct ToriDraw_Sprite** panel_frames;
    struct ToriDraw_Sprite const* panel;
    uint32_t* column[TORIRS_FLAME_SIDES] = { NULL, NULL };
    uint32_t const* pair[TORIRS_FLAME_SIDES];
    int sprite_id;
    int scene_id;
    int frame_count = 0;
    int col_h;

    assert(app);
    if( app->flames || !app->provider || !app->scene )
        return;

    sprite_id = CacheProvider_SpriteIdByName(app->provider, "title_background");
    if( sprite_id < 0 )
        return;

    /*
     * Read the backdrop out of the SCENE, not the provider.
     *
     * Uploading a sprite hands its pixels to the scene and leaves the
     * provider's copy empty -- the client deliberately does not keep two of
     * every image. The scene is therefore where the picture actually is by the
     * time anything wants to look at it.
     */
    scene_id = UITreeSceneBridge_EnsureSprite(&app->bridge, sprite_id);
    if( scene_id < 0 )
        return;
    panel_frames = ToriDraw_SceneSpriteGet(app->scene, scene_id, &frame_count);
    if( !panel_frames || frame_count < 1 || !panel_frames[0] )
        return;
    panel = panel_frames[0];
    if( !panel->pixels_argb || panel->width < TORIRS_FLAME_W * 2 )
        return;

    /*
     * The column is taller than the heat field, and deliberately.
     *
     * The reference's surface is 128x265 while the fire it holds is
     * 128x256: the fire is drawn nine rows down, so its base lands in the
     * brazier bowl rather than at the bottom edge of the strip. Cutting the
     * column to the fire's own height instead leaves the flame standing on
     * the surface's edge, with a hard seam where the copied wall stops.
     */
    col_h = panel->height < TORIRS_FLAME_COLUMN_H ? panel->height : TORIRS_FLAME_COLUMN_H;

    /* The two strips the reference burns in: hard against each edge of the
     * panel, which is where the braziers are painted. */
    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
    {
        int src_x = side == TORIRS_FLAME_LEFT ? 0 : panel->width - TORIRS_FLAME_W;
        column[side] = malloc((size_t)TORIRS_FLAME_W * col_h * sizeof(*column[side]));
        assert(column[side]);
        for( int y = 0; y < col_h; y++ )
            memcpy(
                &column[side][(size_t)y * TORIRS_FLAME_W],
                &panel->pixels_argb[(size_t)y * panel->width + src_x],
                (size_t)TORIRS_FLAME_W * sizeof(*column[side]));
        pair[side] = column[side];
    }

    /* Runes are optional: without them the cooling map carries no glyphs and
     * the fire is a plain one, which is what a revision with no rune pack
     * honestly has. */
    sprite_id = CacheProvider_SpriteIdByName(app->provider, "runes");
    runes = sprite_id >= 0 ? CacheProvider_SpriteGet(app->provider, sprite_id) : NULL;

    app->flames = calloc(1, sizeof(*app->flames));
    assert(app->flames);
    TitleFlames_Init(app->flames, pair, TORIRS_FLAME_W, col_h, runes);
    app->flames_last_ms = 0;

    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
        free(column[side]);
}

void
app_title_flames_stop(struct App* app)
{
    assert(app);
    if( !app->flames )
        return;
    TitleFlames_Free(app->flames);
    free(app->flames);
    app->flames = NULL;
}

/* One frame of fire, uploaded into the two reserved scene slots. */
void
app_title_flames_tick(
    struct App* app,
    uint64_t now_ms)
{
    static int const k_slot[TORIRS_FLAME_SIDES] = {
        UITREE_SCENE_TITLE_FLAME_LEFT_ID,
        UITREE_SCENE_TITLE_FLAME_RIGHT_ID,
    };
    int elapsed;

    assert(app);
    if( !app->flames || !app->scene )
        return;

    elapsed = app->flames_last_ms == 0 ? 0 : (int)(now_ms - app->flames_last_ms);
    app->flames_last_ms = now_ms;
    if( !TitleFlames_Advance(app->flames, elapsed) )
        return;

    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
    {
        size_t bytes = (size_t)app->flames->width * app->flames->height * sizeof(uint32_t);
        uint32_t* copy = malloc(bytes);
        struct ToriDraw_Sprite* sprite;
        struct ToriDraw_Sprite** sprites;

        assert(copy);
        memcpy(copy, TitleFlames_Pixels(app->flames, (enum TitleFlameSide)side), bytes);
        sprite = ToriDraw_SpriteNewFromArgbOwned(copy, app->flames->width, app->flames->height);
        if( !sprite )
        {
            free(copy);
            continue;
        }
        sprites = malloc(sizeof(*sprites));
        assert(sprites);
        sprites[0] = sprite;
        /* Adding over a live id frees what was there and re-emits the load, so
         * the GPU lanes pick the new pixels up; the soft lane reads the scene
         * directly. */
        if( ToriDraw_SceneSpriteHas(app->scene, k_slot[side]) )
            ToriDraw_SceneSpriteRemove(app->scene, k_slot[side]);
        ToriDraw_SceneSpriteAdd(app->scene, k_slot[side], sprites, 1);
    }

    app->need_redraw = 1;
}

/*
 * Show the group belonging to the current title screen, hide the rest.
 *
 * The title tree carries every screen at once -- menu, form, info, loading --
 * because they share a panel and rebuilding the tree per screen would flash
 * the whole backdrop. Which one is visible is the only thing that changes.
 *
 * Found by role, never by index: the tree is rebuilt whenever the window
 * changes shape, and a remembered index would then point at whatever landed in
 * that slot.
 */
void
app_title_sync_groups(struct App* app)
{
    static char const* const k_groups[] = {
        "title_menu_group",
        "title_form_group",
        "title_info_group",
        "title_progress_group",
    };
    /* Parallel to k_groups: which RS_TitleScreen each belongs to, and -1 for
     * the loading bar, which answers to the boot progress instead. */
    static int const k_screen[] = {
        RS_TITLE_MAIN_MENU,
        RS_TITLE_LOGIN_FORM,
        RS_TITLE_INFO,
        -1,
    };
    int showing_progress;

    assert(app);
    if( !app->tree )
        return;

    /* The bar owns the panel while there is one: the reference draws the
     * loading screen INSTEAD of the login box, in the same 360x200 space. */
    showing_progress = app->title.progress_percent >= 0;

    for( size_t i = 0; i < sizeof(k_groups) / sizeof(k_groups[0]); i++ )
    {
        int32_t idx = UITree_RoleNodeByName(app->tree, &app->ui_roles, k_groups[i]);
        int visible;

        if( idx < 0 )
            continue;
        visible = k_screen[i] < 0 ? showing_progress
                                  : (!showing_progress && k_screen[i] == (int)app->title.screen);
        UITree_SetScreenHiddenAt(app->tree, idx, !visible);
    }

    /* While a submitted login is dialling, the form stays up -- its message
     * lines are where "Connecting to server..." appears -- but the Login and
     * Cancel buttons are withdrawn: mid-handshake there is nothing either
     * could meaningfully do. The profile declares the role around exactly
     * the widgets it wants withdrawn; one that declares no such layer keeps
     * its buttons and loses nothing else. */
    {
        int32_t idx = UITree_RoleNodeByName(app->tree, &app->ui_roles, "title_form_buttons");
        if( idx >= 0 )
            UITree_SetScreenHiddenAt(app->tree, idx, app->screen == APP_SCREEN_CONNECTING);
    }
}

/*
 * The title screen's state changed: redraw, and tell the retention gate.
 *
 * Both halves matter. Without the epoch bump the emit walk reuses last frame's
 * command buffer and the typed character never appears; without need_redraw
 * the frame loop may not present at all.
 */
void
app_title_state_changed(struct App* app)
{
    assert(app);
    app_title_sync_groups(app);
    UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CLIENT_STATE));
    app->need_redraw = 1;
}

/*
 * Publish a boot step to the title screen's loading bar.
 *
 * The percentage is the client's; the words are the profile's, looked up by
 * name. A revision that declares no such string gets the bar with no caption
 * rather than an English sentence it never chose.
 */
/*
 * Announce one step of the profile's preload list.
 *
 * The percentage and the words are both the step's, so a revision that
 * counts its boot differently -- and the two here do; one steps through
 * positions while the other sums weights -- says so in its profile rather
 * than in this function. A step the profile does not declare announces
 * nothing and the bar stays where it was, which is the same
 * undeclared-means-absent rule the rest of revconfig runs on.
 *
 * Returns the step so the caller can see whether it asked to be rendered.
 */
struct RS_PreloadStep const*
app_preload_announce(
    struct App* app,
    char const* step_name)
{
    struct RS_PreloadStep const* step = NULL;

    assert(app);
    assert(step_name);
    for( int i = 0; i < app->preload.count; i++ )
    {
        if( strcmp(app->preload.steps[i].name, step_name) == 0 )
        {
            step = &app->preload.steps[i];
            break;
        }
    }
    if( !step )
        return NULL;

    if( step->percent >= 0 )
        app->boot_progress = step->percent;
    RS_Title_SetProgress(
        &app->title,
        step->percent,
        step->say[0] ? RS_LoginReplies_String(&app->login_replies, step->say) : NULL);
    if( app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING )
        app_title_state_changed(app);
    return step;
}

void
app_title_progress(
    struct App* app,
    int percent,
    char const* string_key)
{
    assert(app);
    assert(string_key);
    RS_Title_SetProgress(
        &app->title, percent, RS_LoginReplies_String(&app->login_replies, string_key));
    if( app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING )
        app_title_state_changed(app);
}

/*
 * Compose one credential line: prefix, the value (masked if the widget asked),
 * and the caret when this field has focus and the blink is showing.
 *
 * The host composes it rather than the widget because the pieces belong to
 * different owners -- the value and the focus are the model's, the blink is the
 * client's clock, and the spelling of the prefix, the mask and the caret are
 * the revision's. The reference draws exactly this, as one string, because
 * centring or measuring the label separately from the value would not
 * reproduce it (Client-TS titleScreenDraw, deob method8166).
 *
 * The returned pointer is App-owned and lives until the next call: the same
 * frame lifetime the hovertext and reboot-timer strings have.
 */
int
app_title_field_line(
    struct App* app,
    struct UITreeHostRequest* req)
{
    struct UITreeLoginInputConfig const* cfg = req->u.get_title_field.config;
    char const* value;
    char masked[RS_TITLE_FIELD_LEN];
    int focused;
    int caret_showing = 0;

    assert(app);
    assert(cfg);

    if( app->screen != APP_SCREEN_TITLE && app->screen != APP_SCREEN_CONNECTING )
        return 0;
    if( cfg->field < 0 || cfg->field >= RS_TITLE_FIELD_COUNT )
        return 0;

    value = RS_Title_FieldText(&app->title, (enum RS_TitleField)cfg->field);
    if( cfg->mask[0] != '\0' )
    {
        size_t len = strlen(value);
        if( len >= sizeof(masked) )
            len = sizeof(masked) - 1;
        memset(masked, cfg->mask[0], len);
        masked[len] = '\0';
        value = masked;
    }

    focused = app->title.focus == cfg->field;
    /* caret_blink=0 is a SOLID caret, not an absent one -- the header's
     * "default 0 = never blink" (revconfig.h) means always shown. A profile
     * that wants no caret at all declares caret= empty instead. */
    if( focused )
        caret_showing =
            cfg->caret_blink > 0
                ? (int)(app->logic_cycle % (uint64_t)cfg->caret_blink) < cfg->caret_blink / 2
                : 1;

    snprintf(
        app->title_session.field_line[cfg->field],
        sizeof(app->title_session.field_line[cfg->field]),
        "%s%s%s",
        cfg->prefix,
        value,
        caret_showing ? cfg->caret : "");

    if( req->u.get_title_field.out_focused )
        *req->u.get_title_field.out_focused = focused;
    *req->u.get_title_field.out_text = app->title_session.field_line[cfg->field];
    return 1;
}

/*
 * Submit whatever is in the login form.
 *
 * The one path a clicked Login, an Enter on the password and an autologin all
 * take, which is what makes the scripted lanes exercise the login screen
 * instead of going around it.
 */
/*
 * Restate the login checksums from the cache server, when that is where they
 * came from at boot.
 *
 * Before EVERY dial, because the init-time read alone left a trap: the server
 * repacks whenever its content changes, and a client that had been sitting at
 * the title since before the repack sent boot-time sums with each attempt --
 * a reply=6 ("client out of date") loop that no retry could leave, on
 * exactly the machine that keeps a client open across server iterations.
 * One HTTP GET per Login click; a failed read keeps the table it has.
 */
/* The login block carries the same identity the cache scripts are told
 * (CS2VM2_SetClientIdentity, resolved in App_Init): a mobile client says so
 * on the wire, and the server opens the mobile gameframe for it. */
static void
app_login_set_client_identity(struct App* app)
{
    assert(app);
    assert(app->net);
    app->net->client_type = CS2VM2_ClientType();
    app->net->platform_type = CS2VM2_OnMobile() ? 2 : 0;
}

static void
app_login_refresh_jag_checksums(struct App* app)
{
#if !defined(TORIRS_PLATFORM_WEB)
    int32_t crc[9];

    assert(app);
    if( !app->jag_crc_from_ondemand || !app->net || !app->net->rev )
        return;
    if( PlatformXIO_Dat1OnDemandJagChecksumsRefresh(app->runner.px, crc) == 0 )
        GameProtoRev_SetJagChecksums(app->net->rev, crc);
#else
    (void)app;
#endif
}

/* The session machine works in phases and link states rather than in this
 * client's screens and socket states, because the rules it owns are the same
 * whether the screen is a cache-built title or a headless boot. These two say
 * which is which. */
static enum RS_TitleScreenPhase
app_title_phase(struct App const* app)
{
    assert(app);
    switch( app->screen )
    {
    case APP_SCREEN_TITLE: return RS_TITLE_PHASE_TITLE;
    case APP_SCREEN_CONNECTING: return RS_TITLE_PHASE_CONNECTING;
    case APP_SCREEN_GAME: return RS_TITLE_PHASE_GAME;
    default: return RS_TITLE_PHASE_OTHER;
    }
}

static enum RS_TitleLinkState
app_title_link_state(struct App const* app)
{
    assert(app);
    if( !app->net )
        return RS_TITLE_LINK_ABSENT;
    if( app->net->state == TORIRS_NET_GAME )
        return RS_TITLE_LINK_IN_GAME;
    if( app->net->state == TORIRS_NET_DISCONNECTED )
        return RS_TITLE_LINK_DOWN;
    return RS_TITLE_LINK_BUSY;
}

void
app_title_submit(struct App* app)
{
    char const* user;

    assert(app);
    app->title.submit_requested = 0;

    /* The password is not read here: the connect happens a tick later and
     * takes both straight off the form -- see RS_TitleSession. */
    user = RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME);
    if( !RS_TitleSession_Submit(
            &app->title_session, app->net_enabled && app->net != NULL, user[0] != '\0') )
        return;

    /* The client knows WHEN to say this; the revision says what. */
    RS_Title_SetMessages(
        &app->title, NULL, RS_LoginReplies_String(&app->login_replies, "connecting"), NULL);
    app->screen = APP_SCREEN_CONNECTING;
    app_title_state_changed(app);
    /*
     * And stop here, one frame short of dialling.
     *
     * Everything above is a change to what is ON SCREEN -- the message line,
     * and the Login/Cancel buttons that app_title_sync_groups withdraws for
     * APP_SCREEN_CONNECTING -- and none of it has been drawn yet.
     * app_title_state_changed has asked for the frame; connecting here would
     * spend the rest of this tick, and every tick after it, on a handshake and
     * then on the whole gameframe's assets, with the pre-click picture still on
     * the screen. The player clicks Login and watches nothing happen.
     *
     * The tick ends instead, the frame goes out, and app_title_tick dials on
     * the next one -- RS_TitleSession_Submit has already armed that.
     */
}

/*
 * The title screen's own tick: autologin, submit, and the login result.
 *
 * Runs while the session is on the title screen or connecting through it. The
 * frame loop keeps drawing throughout, which is where "Connecting to
 * server..." appears -- the reference does the same, and it is the reason the
 * connect had to come out of App_Init.
 */
int
app_title_tick(struct App* app)
{
    int redraw = 0;

    assert(app);

    /*
     * A networked profile that declares no title screen still has to log in.
     *
     * Any manifest whose revconfig predates [layout:title] is in that
     * position: it boots straight to the gameframe, so there is no screen to
     * show progress on -- but the credentials still have to reach the server,
     * which is what App_Init used to do before the connect moved to submit.
     */
    if( RS_TitleSession_TakeHeadlessLogin(
            &app->title_session, app->net_enabled && app->net != NULL, app_title_phase(app)) )
    {
        app_login_refresh_jag_checksums(app);
        app_login_set_client_identity(app);
        ToriRS_Network_ConnectLogin(
            app->net,
            app->title_session.connect_target,
            app->title_session.user,
            app->title_session.password);
        return 0;
    }

    if( app->screen != APP_SCREEN_TITLE && app->screen != APP_SCREEN_CONNECTING )
        return 0;
    /* Nothing to submit into until the title tree is up. */
    if( app->app_state != APP_STATE_READY )
        return 0;

    /* Credentials from the command line or the manifest: prefill and submit
     * once. */
    if( RS_TitleSession_TakePrefill(
            &app->title_session, app->app_state == APP_STATE_READY, app_title_phase(app)) )
    {
        RS_Title_SetFieldText(
            &app->title, RS_TITLE_FIELD_USERNAME, app->title_session.user);
        RS_Title_SetFieldText(
            &app->title, RS_TITLE_FIELD_PASSWORD, app->title_session.password);
        RS_Title_SetScreen(&app->title, RS_TITLE_LOGIN_FORM);
        app->title.submit_requested = 1;
        redraw = 1;
    }

    if( app->title.submit_requested )
    {
        app_title_submit(app);
        redraw = 1;
    }
    /*
     * The connect the previous tick deferred, now that its frame has been
     * drawn. Read out of the form rather than carried across, because the form
     * IS the state -- nothing can have edited it in between, and a copy would
     * be a second place for the credentials to live.
     */
    else if( RS_TitleSession_TakePendingConnect(&app->title_session) )
    {
        app_login_refresh_jag_checksums(app);
        app_login_set_client_identity(app);
        ToriRS_Network_ConnectLogin(
            app->net,
            app->title_session.connect_target,
            RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME),
            RS_Title_FieldText(&app->title, RS_TITLE_FIELD_PASSWORD));
        redraw = 1;
    }

    /* The handshake finished: the server's IF_OPENTOP roots the gameframe,
     * exactly as a networked boot used to do straight out of App_Init. */
    if( RS_TitleSession_LoginSucceeded(app_title_phase(app), app_title_link_state(app)) )
    {
        App_OpenRootInterface(app, -1);
        return 1;
    }

    /*
     * The handshake failed. Back to the form, with what the server said.
     *
     * The reply code is the only thing separating "wrong password" from "this
     * world is full" from "you have only just left another world", and the
     * player is owed the difference. The words are the profile's; if it
     * declares none, the code goes to the log and the screen says nothing
     * rather than inventing a sentence.
     *
     * Not while the dial is still queued. app_title_submit puts the screen on
     * APP_SCREEN_CONNECTING one tick BEFORE it connects (RS_TitleSession),
     * and TORIRS_NET_DISCONNECTED is also the state a network that has never
     * been dialled sits in -- so without this the submit tick reads its own
     * not-yet-started handshake as a failure, and the player is shown
     * [login_reply:default] ("Unexpected server response") on the way to a
     * login that then succeeds.
     */
    if( RS_TitleSession_LoginFailed(
            &app->title_session, app_title_phase(app), app_title_link_state(app)) )
    {
        struct RS_LoginReply const* reply =
            RS_LoginReplies_Get(&app->login_replies, app->net->login_reply);

        app->screen = APP_SCREEN_TITLE;
        if( reply )
        {
            RS_Title_SetMessages(&app->title, reply->line[0], reply->line[1], reply->line[2]);
            if( reply->screen >= 0 )
                RS_Title_SetScreen(&app->title, (enum RS_TitleScreen)reply->screen);
        }
        else
        {
            TORIRS_ERR(
                "login: rejected with reply=%d and the profile declares no text for it\n",
                app->net->login_reply);
            RS_Title_SetMessages(&app->title, NULL, NULL, NULL);
        }
        app_title_state_changed(app);
        return 1;
    }

    return redraw;
}
