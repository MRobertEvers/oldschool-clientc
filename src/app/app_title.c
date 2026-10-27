/*
 * The title screen: flames, captions, field lines, and the login handshake.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "mem/torimem_toridraw.h"
#include "app/app_internal.h"
#include "boot_telemetry.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_login_set_client_identity(struct App* app);
static void
app_login_refresh_jag_checksums(struct App* app);
static enum RS_TitleScreenPhase
app_title_phase(struct App const* app);
static enum RS_TitleLinkState
app_title_link_state(struct App const* app);
static void
app_title_resume_to_login(struct App* app);
static void
app_title_layout_accounts(struct App* app);

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
    UITREE_SCAN_METER(app->tree);
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
        column[side] = ToriMem_Alloc(ToriMem_Process(), (size_t)TORIRS_FLAME_W * col_h * sizeof(*column[side]));
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

    app->flames = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*app->flames));
    assert(app->flames);
    TitleFlames_Init(app->flames, pair, TORIRS_FLAME_W, col_h, runes);
    app->flames_last_ms = 0;

    for( int side = 0; side < TORIRS_FLAME_SIDES; side++ )
        ToriMem_Free(ToriMem_Process(), column[side]);
}

void
app_title_flames_stop(struct App* app)
{
    assert(app);
    if( !app->flames )
        return;
    TitleFlames_Free(app->flames);
    ToriMem_Free(ToriMem_Process(), app->flames);
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
        uint32_t* copy = TORIDRAW_MALLOC(bytes); /* Owned by the sprite */
        struct ToriDraw_Sprite* sprite;
        struct ToriDraw_Sprite** sprites;

        assert(copy);
        memcpy(copy, TitleFlames_Pixels(app->flames, (enum TitleFlameSide)side), bytes);
        sprite = ToriDraw_SpriteNewFromArgbOwned(copy, app->flames->width, app->flames->height);
        if( !sprite )
        {
            TORIDRAW_FREE(copy);
            continue;
        }
        sprites = TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
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
        "title_accounts_group",
        "title_link_group",
        "title_progress_group",
    };
    /* Parallel to k_groups: which RS_TitleScreen each belongs to, and -1 for
     * the loading bar, which answers to the boot progress instead. */
    static int const k_screen[] = {
        RS_TITLE_MAIN_MENU,
        RS_TITLE_LOGIN_FORM,
        RS_TITLE_INFO,
        RS_TITLE_ACCOUNTS,
        RS_TITLE_LINK_PROMPT,
        -1,
    };
    int showing_progress;

    assert(app);
    if( !app->tree )
        return;

    /* First, because it can move the screen: once the device holds accounts
     * their cards are home, and the front menu is no longer where a player
     * lands. */
    app_title_layout_accounts(app);

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
 * Which accounts get the cards: the site login's linked accounts first, then
 * the device's own that are not among them, into app->title_cards.
 */
static void
app_title_build_cards(struct App* app)
{
    int count = 0;

    assert(app);
    if( app->oauth && app->site.signed_in )
        for( int i = 0; i < app->site.count && count < APP_TITLE_ACCOUNT_CARDS; i++ )
        {
            app->title_cards[count].site = 1;
            app->title_cards[count].index = i;
            count++;
        }
    if( app->oauth )
        for( int i = 0; i < app->accounts.count && count < APP_TITLE_ACCOUNT_CARDS; i++ )
        {
            if( RS_SiteSession_Find(&app->site, app->accounts.list[i].name) >= 0 )
                continue;
            app->title_cards[count].site = 0;
            app->title_cards[count].index = i;
            count++;
        }
    app->title_card_count = count;
}

/* The name on card `card`. */
static char const*
app_title_card_name(
    struct App const* app,
    int card)
{
    assert(card >= 0 && card < app->title_card_count);
    if( app->title_cards[card].site )
        return app->site.players[app->title_cards[card].index];
    return app->accounts.list[app->title_cards[card].index].name;
}

/*
 * The Play Now cards, placed by how many there are.
 *
 * One card stands in the middle of the box; two stand where the front menu's
 * two buttons do; three or four make two rows, with a third card centred
 * under the first two. Cards past the count are hidden, and so is every one
 * on a world without the account server. The form's account icon shows only
 * when there is somewhere for it to go back to.
 *
 * Runs on a state change, never per frame: each role lookup here is a table
 * read, and nothing about the cards moves between changes.
 */
static void
app_title_layout_accounts(struct App* app)
{
    static int const k_column[2] = { 229, 389 };
    int const centre_x = 309;
    int shown;
    int32_t icon;

    assert(app);
    assert(app->tree);
    app_title_build_cards(app);
    shown = app->title_card_count;

    for( int i = 0; i < APP_TITLE_ACCOUNT_CARDS; i++ )
    {
        char role[48];
        int32_t card;
        int32_t name;
        int x;
        int y;

        snprintf(role, sizeof(role), "title_account_%d", i);
        card = UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
        if( card < 0 )
            continue;
        UITree_SetScreenHiddenAt(app->tree, card, i >= shown);
        if( i >= shown )
            continue;
        if( shown == 1 )
        {
            x = centre_x;
            y = 236;
        }
        else if( shown == 2 )
        {
            x = k_column[i];
            y = 236;
        }
        else
        {
            x = shown == 3 && i == 2 ? centre_x : k_column[i % 2];
            y = i < 2 ? 205 : 267;
        }
        UITree_SetPositionAt(app->tree, card, x, y);
        snprintf(role, sizeof(role), "title_account_name_%d", i);
        name = UITree_RoleNodeByName(app->tree, &app->ui_roles, role);
        if( name >= 0 )
            UITree_SetTextAt(app->tree, name, app_title_card_name(app, i));
    }

    icon = UITree_RoleNodeByName(app->tree, &app->ui_roles, "title_form_accounts_icon");
    if( icon >= 0 )
        UITree_SetScreenHiddenAt(app->tree, icon, shown == 0);
    RS_Title_SetHome(&app->title, shown > 0 ? RS_TITLE_ACCOUNTS : RS_TITLE_MAIN_MENU);
}

/* Write the device's accounts (FILE_WRITE; the page's IndexedDB on the
 * browser). Queued, never waited on: the list in memory is the truth. */
static void
app_accounts_save(struct App* app)
{
    assert(app);
    if( app->accounts_path )
        ToriRS_TaskQueue_Add(app->runner.queue,
                             CreateTask_AccountsSave(&app->accounts, app->accounts_path));
}

/*
 * Start a sign-in at the account server: a device account's Play Now (`index`
 * is the account, whose refresh token is spent), a linked account's (`index`
 * into site.players, played on the site's session), or the credential form --
 * a password grant or a link -- (its two fields, the password cleared out of
 * the form as it is copied). The title shows "Connecting to server..." until
 * the task answers; app_title_tick waits.
 */
static void
app_accounts_begin_signin(
    struct App* app,
    enum RS_AccountSignInGrant grant,
    int index)
{
    char const* connecting = RS_LoginReplies_String(&app->login_replies, "connecting");

    assert(app);
    assert(!app->signin_busy);
    memset(&app->signin, 0, sizeof(app->signin));
    app->signin.grant = grant;
    if( grant == RS_ACCOUNT_SIGNIN_REFRESH )
    {
        assert(index >= 0 && index < app->accounts.count);
        snprintf(app->signin.username, sizeof(app->signin.username), "%s",
                 app->accounts.list[index].name);
        snprintf(app->signin.refresh_token, sizeof(app->signin.refresh_token), "%s",
                 app->accounts.list[index].refresh_token);
    }
    else if( grant == RS_ACCOUNT_SIGNIN_SITE )
    {
        assert(index >= 0 && index < app->site.count);
        snprintf(app->signin.username, sizeof(app->signin.username), "%s",
                 app->site.players[index]);
    }
    else
    {
        snprintf(app->signin.username, sizeof(app->signin.username), "%s",
                 RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME));
        snprintf(app->signin.password, sizeof(app->signin.password), "%s",
                 RS_Title_FieldText(&app->title, RS_TITLE_FIELD_PASSWORD));
        RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_PASSWORD, NULL);
    }
    app->signin_busy = 1;
    ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_AccountSignIn(&app->signin));
    RS_Title_SetMessages(&app->title, connecting, NULL, NULL);
    app->screen = APP_SCREEN_CONNECTING;
    app_title_state_changed(app);
}

/* Forget the device's own Play Now for `name`: the account is linked now, its
 * refresh tokens revoked, and its card is the site's. 1 when one was there. */
static int
app_accounts_forget_linked(
    struct App* app,
    char const* name)
{
    int index = RS_Accounts_Find(&app->accounts, name);

    if( index < 0 )
        return 0;
    RS_Accounts_Forget(&app->accounts, index);
    return 1;
}

/*
 * The sign-in answered. Good: a password or refresh grant's account goes first
 * in the device's list with its rotated refresh token, a link's joins the site
 * login's (and leaves the device's), and the game is dialled with the one-time
 * login -- from here the ordinary login result handling takes over. Refused:
 * back to the title with the server's words, and a refresh token the server
 * refused is forgotten, its account's name put in the form for a password --
 * unless the account is linked, whose card is the site's.
 */
static void
app_accounts_finish_signin(struct App* app)
{
    struct RS_AccountSignIn* signin = &app->signin;

    assert(app);
    assert(signin->done);
    if( signin->ok )
    {
        if( signin->grant == RS_ACCOUNT_SIGNIN_LINK )
        {
            RS_SiteSession_Add(&app->site, signin->name);
            if( app_accounts_forget_linked(app, signin->name) )
                app_accounts_save(app);
        }
        else if( signin->grant != RS_ACCOUNT_SIGNIN_SITE )
        {
            RS_Accounts_Remember(&app->accounts, signin->name, signin->new_refresh_token);
            app_accounts_save(app);
        }
        app_login_refresh_jag_checksums(app);
        app_login_set_client_identity(app);
        ToriRS_Network_ConnectLoginToken(
            app->net, app->title_session.connect_target, signin->name, signin->otl);
        memset(signin, 0, sizeof(*signin));
        app_title_state_changed(app);
        return;
    }

    TORIRS_LOG("login: account server refused the sign-in: %s\n", signin->error);
    app->screen = APP_SCREEN_TITLE;
    if( signin->refresh_refused && RS_SiteSession_Find(&app->site, signin->username) >= 0 )
    {
        /* Linked since this device saved it: the site's card plays it. */
        if( app_accounts_forget_linked(app, signin->username) )
            app_accounts_save(app);
        RS_Title_SetMessages(&app->title, signin->error, NULL, NULL);
    }
    else if( signin->refresh_refused )
    {
        int index = RS_Accounts_Find(&app->accounts, signin->username);

        if( index >= 0 )
        {
            RS_Accounts_Forget(&app->accounts, index);
            app_accounts_save(app);
        }
        RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_USERNAME, signin->username);
        RS_Title_SetScreen(&app->title, RS_TITLE_LOGIN_FORM);
        RS_Title_SetMessages(&app->title, "Your session has expired.",
                             "Please log in again.", NULL);
    }
    else
        RS_Title_SetMessages(&app->title, signin->error, NULL, NULL);
    memset(signin, 0, sizeof(*signin));
    app_title_state_changed(app);
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
 * The title music button.
 *
 * The reference's corner icon (`title_mute`, deob method1013) flips device
 * option 4 and restarts or stops "scape main"; class97 saves the flag with the
 * rest of preferences<N>.dat. Here the option store is the same flag and
 * RS_Prefs is the same file, written through the IO queue on every lane.
 *
 * "On" also asks that it be AUDIBLE, which the reference never had to: this
 * client boots with the master gain at zero (RS_CS2Host_OptionDefault), so an
 * enabled track on a fresh install is silent. A button that reads "on" over
 * silence is a lie, and one whose first click does nothing is broken, so
 * turning it on raises a zero master or music volume to full.
 */
bool
app_title_music_on(struct App const* app)
{
    assert(app);
    return RS_CS2Host_GetOption(
               &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_TITLE_MUSIC_DISABLED) == 0 &&
           RS_CS2Host_GetOption(
               &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MASTER_VOLUME) > 0 &&
           RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_MUSIC_VOLUME) >
               0;
}

void
app_title_music_toggle(struct App* app)
{
    assert(app);
    if( app_title_music_on(app) )
    {
        RS_CS2Host_SetOption(
            &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_TITLE_MUSIC_DISABLED, 1);
        return;
    }
    RS_CS2Host_SetOption(
        &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_TITLE_MUSIC_DISABLED, 0);
    if( RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MASTER_VOLUME) <=
        0 )
        RS_CS2Host_SetOption(
            &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MASTER_VOLUME, 100);
    if( RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_MUSIC_VOLUME) <= 0 )
        RS_CS2Host_SetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_MUSIC_VOLUME, 100);
}

/*
 * Play the title's track while the title is up and the button allows it.
 *
 * The track is the profile's (the music button's song=), so nothing plays
 * until that button has been drawn once. Leaving the title does not stop it:
 * the reference lets "scape main" run into the world until the server's first
 * song replaces it, and a logout's StopAll silences it on the way back.
 */
static void
app_title_music_tick(struct App* app)
{
    bool want;

    if( app->screen != APP_SCREEN_TITLE && app->screen != APP_SCREEN_CONNECTING )
    {
        app->title_music_playing = false;
        return;
    }
    want = app->title_song_known &&
           RS_CS2Host_GetOption(
               &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_TITLE_MUSIC_DISABLED) == 0;
    if( want == app->title_music_playing )
        return;
    if( want )
        App_PlaySong(app, app->title_song, true, 0, 0);
    else
        App_StopSong(app, 0);
    app->title_music_playing = want;
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
 * What the finished boot hands the screen over to.
 *
 * Normally the bar comes down: one left standing at 100 keeps the loading
 * group in front of whatever it was covering, which on a title bake is the
 * login form and after a login is the gameframe.
 *
 * Except on the boot a reloaded page makes. That client is not going to show a
 * login form at all -- it is going to ask for its session back -- and the bar
 * is the only thing on screen saying so. Clearing it here uncovers the form
 * for however many frames pass before the first logic tick arms the reconnect,
 * and the accumulator makes that a real number rather than a theoretical one:
 * a frame that pays out no 20 ms cycle runs no title tick at all. What the
 * player would see is an empty login box flashing up over a refresh they
 * asked for. So the bar stays where the boot left it and takes the
 * reconnect's own caption.
 */
void
app_title_boot_settled(struct App* app)
{
    assert(app);
    if( app->screen == APP_SCREEN_TITLE && app->title_session.resume_pending )
    {
        /* 100, because the boot really did finish: everything this bar was
         * counting is loaded, and what is left is a handshake. */
        app_title_progress(app, 100, "reconnecting");
        return;
    }
    RS_Title_SetProgress(&app->title, -1, NULL);
}

/*
 * Give up on the resumed session and hand the screen to the player.
 *
 * Both ways out of a reconnect that will not happen come here: the server
 * saying it no longer has that session, and a client with nothing to dial it
 * with. Three things have to happen together, which is what makes it one
 * function rather than three lines at each site.
 *
 * The page's entry is DROPPED, because it names a session that is gone and a
 * second refresh would present the same retired key. The bar comes DOWN,
 * because it is the only thing on screen and it says "reconnecting". And the
 * form comes up EMPTY -- not because the fields are worth clearing, but
 * because they were never filled: the page handed this boot a name and a key
 * and no password, and a login is now the player's to perform.
 */
static void
app_title_resume_to_login(struct App* app)
{
    assert(app);
    app_session_resume_forget();
    RS_TitleSession_ResumeRefused(&app->title_session);
    RS_Title_SetProgress(&app->title, -1, NULL);
    RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_USERNAME, "");
    RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_PASSWORD, "");
    /* After the fields, so the caret starts in the username rather than in the
     * password a prefilled name would have sent it to. */
    RS_Title_SetScreen(&app->title, RS_TITLE_LOGIN_FORM);
    /* What happened, then what to do about it -- and both are the profile's
     * words. A revision that declares neither shows a bare form, which is
     * still the right screen. */
    RS_Title_SetMessages(
        &app->title,
        RS_LoginReplies_String(&app->login_replies, "session_expired"),
        RS_LoginReplies_String(&app->login_replies, "enter_credentials"),
        NULL);
    app->screen = APP_SCREEN_TITLE;
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
    /* LoginPlatformType: 2 ANDROID, 3 APPLE (the iOS client, 8). */
    app->net->platform_type = !CS2VM2_OnMobile()           ? 0
                              : CS2VM2_ClientType() == 8 ? 3
                                                         : 2;
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
    ToriRS_BootTelemetry_Mark("login_submit");

    /* On an account-server world the form is a password grant, not a password
     * on the wire: the dial waits for the one-time login it buys. */
    if( app->oauth )
    {
        char const* name = RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME);
        int linked;

        if( !app->net_enabled || !app->net || !name[0] || app->signin_busy )
            return;
        if( !app->site.signed_in )
        {
            app_accounts_begin_signin(app, RS_ACCOUNT_SIGNIN_PASSWORD, -1);
            return;
        }
        /* Already linked to this site login: its password no longer signs
         * it in, and the session does -- so the session plays it. */
        linked = RS_SiteSession_Find(&app->site, name);
        if( linked >= 0 )
        {
            RS_Title_SetFieldText(&app->title, RS_TITLE_FIELD_PASSWORD, NULL);
            app_accounts_begin_signin(app, RS_ACCOUNT_SIGNIN_SITE, linked);
            return;
        }
        /* Signed in to a site login: link this account to it, or just log
         * in. The form keeps both fields behind the prompt for either. */
        {
            char line[RS_TITLE_MESSAGE_LEN];

            snprintf(line, sizeof(line), "Link %s to %s?", name, app->site.user);
            RS_Title_SetMessages(&app->title, line,
                                 "A linked account signs in only from this site login.",
                                 "Log in signs in with the password, unlinked.");
        }
        app->title.link_choice = RS_TITLE_LINK_NONE;
        RS_Title_SetScreen(&app->title, RS_TITLE_LINK_PROMPT);
        app_title_state_changed(app);
        return;
    }

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
    app_title_music_tick(app);

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

    /* The device's saved accounts, read once. The title is the plain front
     * menu until they land, then their cards (app_title_layout_accounts). */
    if( app->oauth && app->accounts_path && !app->accounts_load_started )
    {
        app->accounts_load_started = 1;
        ToriRS_TaskQueue_Add(
            app->runner.queue,
            CreateTask_AccountsLoad(&app->accounts, app->accounts_path, &app->accounts_loaded));
    }
    if( app->accounts_loaded && !app->accounts_shown )
    {
        app->accounts_shown = 1;
        app_title_state_changed(app);
        redraw = 1;
    }
    /* The site session, asked once. Its linked accounts become cards, and
     * the device's own Play Now for any of them is dropped: linking revoked
     * its token. Waits for the device's file, which it edits. */
    if( app->oauth && app->accounts_loaded && !app->site_load_started )
    {
        app->site_load_started = 1;
        ToriRS_TaskQueue_Add(app->runner.queue,
                             CreateTask_SiteSessionLoad(&app->site, &app->site_loaded));
    }
    if( app->site_loaded && !app->site_shown )
    {
        int forgot = 0;

        app->site_shown = 1;
        for( int i = 0; i < app->site.count; i++ )
            forgot |= app_accounts_forget_linked(app, app->site.players[i]);
        if( forgot )
            app_accounts_save(app);
        app_title_state_changed(app);
        redraw = 1;
    }

    /*
     * A sign-in at the account server is in flight. Nothing else on the
     * title moves until it answers -- above all not the login-failed check
     * below, which reads an undialled link as a refusal.
     */
    if( app->signin_busy )
    {
        if( !app->signin.done )
            return redraw;
        app->signin_busy = 0;
        app_accounts_finish_signin(app);
        return 1;
    }
    if( app->title.play_requested >= 0 )
    {
        int card = app->title.play_requested;

        app->title.play_requested = -1;
        if( app->oauth && card < app->title_card_count && app->net_enabled && app->net )
            app_accounts_begin_signin(app,
                                      app->title_cards[card].site ? RS_ACCOUNT_SIGNIN_SITE
                                                                  : RS_ACCOUNT_SIGNIN_REFRESH,
                                      app->title_cards[card].index);
        return 1;
    }
    /* The link prompt was answered: back on the form, whose fields it kept,
     * where "Connecting..." and any refusal are shown. */
    if( app->title.link_choice != RS_TITLE_LINK_NONE )
    {
        enum RS_TitleLinkChoice choice = app->title.link_choice;

        app->title.link_choice = RS_TITLE_LINK_NONE;
        RS_Title_SetScreen(&app->title, RS_TITLE_LOGIN_FORM);
        if( app->oauth && app->net_enabled && app->net )
            app_accounts_begin_signin(app,
                                      choice == RS_TITLE_LINK_LINK ? RS_ACCOUNT_SIGNIN_LINK
                                                                   : RS_ACCOUNT_SIGNIN_PASSWORD,
                                      -1);
        else
            app_title_state_changed(app);
        return 1;
    }

    /*
     * A reloaded page, asking for the session it was already in.
     *
     * Ahead of the prefill because it replaces it. The page hands a reload a
     * name and a resume token, and the name is NOT typed into the form: the
     * player refreshed a tab, and what they are owed is the world back, not a
     * login screen with their own account already filled into it. Nothing is
     * shown of the form at all -- the bar app_title_boot_settled left standing
     * covers it and says "reconnecting" -- and the dial that arms here reads
     * the session rather than the two empty boxes behind that bar.
     */
    if( RS_TitleSession_TakeResume(
            &app->title_session, app->app_state == APP_STATE_READY, app_title_phase(app)) )
    {
        ToriRS_BootTelemetry_Mark("resume_submit");
        if( RS_TitleSession_Submit(&app->title_session, app->net_enabled && app->net != NULL, true) )
        {
            app->screen = APP_SCREEN_CONNECTING;
            app_title_state_changed(app);
        }
        else
        {
            /* Nothing to dial it through. Left alone, the bar would say
             * "reconnecting" until the player closed the tab. */
            TORIRS_ERR("login: a resumed session has no link to reconnect through\n");
            app_title_resume_to_login(app);
        }
        return 1;
    }

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
        /* A sign-in at the account server started: the link is not dialled
         * yet, and the login-failed check below would read that as a refusal. */
        if( app->signin_busy )
            return redraw;
    }
    /*
     * The connect the previous tick deferred, now that its frame has been
     * drawn. Read out of the form rather than carried across, because the form
     * IS the state -- nothing can have edited it in between, and a copy would
     * be a second place for the credentials to live.
     */
    else if( RS_TitleSession_TakePendingConnect(&app->title_session) )
    {
        /* Except a reconnect, which has no form to read: nothing was typed
         * into one and the boxes behind the bar are empty. It dials the name
         * the page handed back, and GAMERECONNECT presents the session's key
         * where a login would put the password. */
        bool const resumed = app->title_session.resume_dial;

        app_login_refresh_jag_checksums(app);
        app_login_set_client_identity(app);
        ToriRS_Network_ConnectLogin(
            app->net,
            app->title_session.connect_target,
            resumed ? app->title_session.user
                    : RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME),
            resumed ? app->title_session.password
                    : RS_Title_FieldText(&app->title, RS_TITLE_FIELD_PASSWORD));
        redraw = 1;
    }

    /* The handshake finished: the server's IF_OPENTOP roots the gameframe,
     * exactly as a networked boot used to do straight out of App_Init. */
    if( RS_TitleSession_LoginSucceeded(app_title_phase(app), app_title_link_state(app)) )
    {
        ToriRS_BootTelemetry_Mark("login_ok");
        /*
         * The one moment these credentials are known to work.
         *
         * A browser tab that is reloaded loses the heap and the socket with
         * it; handing the page this session's resume token and what it was
         * dialled with is what lets the next boot ask for the same session
         * back instead of opening on an empty form. Taken from the network
         * rather than from the form because that is what was actually sent --
         * the same pair the in-process reconnect redials with. Native lanes
         * keep the record and have nothing to hand it to.
         * @see app_session_resume.c.
         */
        app_session_resume_remember_net(app->net);
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
        struct RS_LoginReply const* reply;

        /*
         * A reloaded page's GAMERECONNECT was refused: the session it named has
         * gone (logged out, saved, its slot reused, or older than the server
         * keeps one).
         *
         * This is where the reload stops being special. There is no password
         * kept anywhere to log in with -- deliberately, see
         * app_session_resume.c -- so the client stops pretending the refresh
         * is still in progress and hands the screen to the player. The reply
         * code is not shown: it is an answer about a session the player never
         * asked about, and "invalid username or password" over an empty form
         * would be a lie about the one they are about to type.
         */
        if( ToriRS_Network_TakeResumeRefused(app->net) )
        {
            TORIRS_LOG("login: the resumed session was not handed back; back to the form\n");
            app_title_resume_to_login(app);
            return 1;
        }
        reply = RS_LoginReplies_Get(&app->login_replies, app->net->login_reply);

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

/*
 * The login form, for the browser's password manager (platform_credential_form.h).
 *
 * The credential form is "showing" only while it is the screen and its two
 * fields are on the tree: the inputs the page lays over them are only ever
 * where a field is drawn. The boxes are the fields' drawn bounds, the same
 * rectangles a click on the canvas hits.
 */
int
App_TakeCredentialForm(
    struct App* app,
    struct PlatformCredentialForm* out)
{
    static char const* const k_roles[2] = { "login_username", "login_password" };
    struct PlatformCredentialForm now;

    assert(app);
    assert(out);
    memset(&now, 0, sizeof(now));
    now.connecting = app->screen == APP_SCREEN_CONNECTING;
    now.in_game = app->screen == APP_SCREEN_GAME;
    now.showing = app->tree && app->screen == APP_SCREEN_TITLE &&
                  app->title.screen == RS_TITLE_LOGIN_FORM && app->title.progress_percent < 0;
    for( int i = 0; i < 2 && now.showing; i++ )
    {
        int32_t node = UITree_RoleNodeByName(app->tree, &app->ui_roles, k_roles[i]);
        struct ToriDraw_Font* font;

        if( node < 0 ||
            !UITree_NodeDrawnBounds(app->tree, node, &now.x[i], &now.y[i], &now.w[i], &now.h[i]) )
        {
            now.showing = 0;
            continue;
        }
        /* A login_input's row y is the text's BASELINE (the deob's
         * drawString(s, x, y); uitree_emit.c), so the line it draws starts one
         * ascent above it and is as tall as the font's line box. That line,
         * not the node's box below it, is where the input has to be. */
        font = ToriDraw_SceneFontGet(app->scene,
                                     UITree_LoginInput(&app->tree->components[node])->font_id);
        if( font )
        {
            now.y[i] -= font->line_height;
            now.h[i] = ToriDraw_FontLineBoxHeight(font);
        }
    }
    now.focus = app->title.focus;
    now.password_len = (int)strlen(RS_Title_FieldText(&app->title, RS_TITLE_FIELD_PASSWORD));
    snprintf(now.username, sizeof(now.username), "%s",
             RS_Title_FieldText(&app->title, RS_TITLE_FIELD_USERNAME));
    /* Both zeroed whole, so the padding compares too. */
    if( memcmp(&now, &app->credential_form_sent, sizeof(now)) == 0 )
        return 0;
    app->credential_form_sent = now;
    *out = now;
    return 1;
}

/* The page's form is only ever over the credential form; anything it says
 * once that screen has gone is a message that crossed a screen change. */
static int
app_credential_form_live(struct App const* app)
{
    return app->screen == APP_SCREEN_TITLE && app->title.screen == RS_TITLE_LOGIN_FORM;
}

void
App_CredentialInput(
    struct App* app,
    int field,
    char const* text)
{
    assert(app);
    assert(field == RS_TITLE_FIELD_USERNAME || field == RS_TITLE_FIELD_PASSWORD);
    assert(text);
    if( !app_credential_form_live(app) )
        return;
    if( strcmp(RS_Title_FieldText(&app->title, (enum RS_TitleField)field), text) == 0 )
        return;
    RS_Title_SetFieldText(&app->title, (enum RS_TitleField)field, text);
    app_title_state_changed(app);
}

void
App_CredentialFocus(
    struct App* app,
    int field)
{
    assert(app);
    assert(field == RS_TITLE_FIELD_USERNAME || field == RS_TITLE_FIELD_PASSWORD);
    if( !app_credential_form_live(app) || app->title.focus == field )
        return;
    app->title.focus = field;
    app_title_state_changed(app);
}

void
App_CredentialSubmit(struct App* app)
{
    assert(app);
    if( !app_credential_form_live(app) )
        return;
    app->title.submit_requested = 1;
    app_title_state_changed(app);
}
