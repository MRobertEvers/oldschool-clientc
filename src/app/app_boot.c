/*
 * Boot sequencing: the warm gameframe bake, the title swap, boot-bar captions, and the async polls that gate readiness.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* IF_OPENSUB wrapper: mount a cache interface pack under a component slot of an
 * already-open root, then relayout + re-request CS1 over the new subtree. Runs
 * on the serial exec pipeline so a mount a packet triggers completes before the
 * next packet is popped (packet order holds), mirroring rs_ui_slots' slot mount.
 * type -1 means close (unmount via CreateTask_InterfaceOpenSub with iface<=0). */
struct Task_OpenSubRefresh
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int target_uid;
    int interface_id;
    int type;
};

/* Private to this unit, declared up front so definition order is free. */
static int
app_boot_bake_is_quiet(struct App const* app);
static int
app_boot_bar_font_scene_id(struct App* app);
static int
Task_AppBoot_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io);
static void
Task_AppBoot_Free(struct ToriRS_Task* base);
static void
app_open_tree(
    struct App* app,
    int interface_id,
    char const* layout_group,
    char const* layout_group_exclude);
static int
Task_OpenSubRefresh_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io);
static void
Task_OpenSubRefresh_Free(struct ToriRS_Task* base);
static void
app_enqueue_open_sub(
    struct App* app,
    int target_uid,
    int interface_id,
    int type);

/**
 * Tell the provider which cache it is reading.
 *
 * The profile is what rscache's decoders consult instead of a bare revision number.
 * Resolving it here, once, is the point: era information used to reach decoders as
 * whichever JS5 archive counter the record happened to come from — a per-archive value
 * whose units differ between eras — or as a flag constant spelled out at the call site.
 *
 * The manifest states all four identity fields (game, epoch, revision, quirks).
 * RSCache_ProfileForIdentity returns them verbatim and borrows codec pins from the
 * revision registry on an exact match. There is no nearest-lower fallback and no
 * guessing from the container alone.
 */
void
app_provider_set_cache_profile(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(app->provider);
    assert(cfg->cache_identity_set && "manifest must state [cache:boot] identity");

    struct RSCache profile = RSCache_ProfileForIdentity(
        cfg->cache_game, cfg->cache_epoch, cfg->cache_revision, cfg->cache_quirks);

    char quirks_buf[32];
    RSCache_QuirksName(profile.quirks, quirks_buf, (int)sizeof(quirks_buf));
    TORIRS_LOG(
        "app: cache profile epoch=%s game=%s revision=%d quirks=%s\n",
        RSCache_EpochName(profile.epoch),
        RSCache_GameName(profile.game),
        profile.revision,
        quirks_buf);
    if( getenv("TORIRS_TRACE_NATIVE_UI") )
        TORIRS_REPORT(
            "NATIVE_REVISION epoch=%s game=%s revision=%d\n",
            RSCache_EpochName(profile.epoch),
            RSCache_GameName(profile.game),
            profile.revision);

    /* The disk resolves logical table names to ids and decides map XTEA, so it
     * needs the same identity the decoders got. Without this it answers as
     * unset, which on a 643 cache means every logical table is ABSENT. */
    if( app->dat2_disk )
        RSCache_Dat2DiskSetProfile(app->dat2_disk, &profile);

    CacheProvider_SetProfile(app->provider, &profile);
}

/*
 * The boot cannot proceed, for a reason the person running this can fix.
 *
 * A missing cache and a cache server that is not up are DEPLOYMENT states, not
 * contract violations: an assert would name the wrong culprit, and carrying on
 * is worse than either -- that is what this client used to do, limping past a
 * failed on-demand enable with no cache provider at all and taking SIGSEGV in
 * the first buildcache lookup, a mile from the cause.
 *
 * So it refuses, loudly, in one sentence addressed to whoever has to act on it.
 * WHERE that sentence has to land is the platform's business and not this
 * function's: a desktop run prints it to the terminal the command was typed in
 * and exits, and on Android there is no such terminal -- exit() there kills the
 * process, the activity vanishes to the launcher, and the diagnosis sits in
 * logcat where nobody holding a phone will read it. Which is to say it reads
 * exactly like a crash. @see PlatformAndroid_BootFailed.
 */
void
app_boot_refuse(char const* message)
{
    assert(message);
    TORIRS_ERR("app: %s\n", message);
#if defined(TORIRS_PLATFORM_ANDROID)
    /* Hands the message to the boot menu and ends the frame thread; the process
     * survives, so the gear can fix the profile and the next run is a new run
     * rather than a new launch. Does not return. */
    PlatformAndroid_BootFailed(message);
#endif
    exit(1);
}

/* A bake that must not replay the loading screen's staged captions or walk
 * the bar back: everything after the session's first. On a profile with a
 * title screen the loading ran once, at boot (App_BootGameframeThenTitle,
 * screen still APP_SCREEN_BOOT); the title bake that follows it, the
 * post-login gameframe rebake and any in-game display-mode remount all cross
 * caches that bake warmed, and replaying 10..100 with captions over them
 * would put a second loading sequence after the first -- or worse, after the
 * login screen. A quiet bake leaves the bar where the loud one parked it
 * (100) and App_Render's caption fallback says "loading" / "entering world".
 * The lanes with no title screen or no net keep announcing: their startup
 * bake is the only loading they have. */
static int
app_boot_bake_is_quiet(struct App const* app)
{
    assert(app);
    return app->net_enabled && App_HasTitleScreen(app) && app->screen != APP_SCREEN_BOOT;
}

int
App_BootTextOnly(struct App const* app)
{
    assert(app);
    /* GAME only: the startup title bake is also quiet, but it belongs to the
     * boot's loading screen and holds the bar at 100 instead -- text-only is
     * the POST-LOGIN picture. */
    return app->app_state == APP_STATE_BOOTING && app->screen == APP_SCREEN_GAME &&
           app_boot_bake_is_quiet(app);
}

/* Shared per-frame completion polls for async work (world load, textures,
 * deferred seq binds, tree refresh). Not run while BOOTING. */
void
app_async_polls(struct App* app)
{
    /* World-load completion is no longer polled here: Task_WorldLoad runs
     * App_WorldLoadFinish itself at its synchronous tail (via on_done, or the
     * REBUILD_NORMAL path's inline call after it awaits the load). */
    if( app_tex_trace_enabled() )
        TORIRS_LOG("tex_trace: --- frame %d ---\n", ++g_tex_trace_frame);
    app_world_map_poll(app);
    app_sync_textures_poll(app);
    app_world_bind_pending_seqs(app);

    if( app->pending_tree_refresh )
    {
        app->pending_tree_refresh = 0;
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app_request_cs1_eval(app);
        app->need_redraw = 1;
    }
}

void
App_BootWait(struct App* app)
{
    /* Headless harnesses/tests only: step both pipelines until the boot task
     * AND every load it queued (world, anims, textures) settle. IO still runs
     * exclusively inside the platform pump; the interactive loop never calls
     * this — it renders the loading state instead. */
    long guard = 1000000;

    assert(app);
    while( guard-- > 0 )
    {
        enum TaskRunnerStat main_stat;
        enum TaskRunnerStat exec_stat;
        app->boot_steps += 2;
        main_stat = TaskRunner_Step(&app->runner);
        exec_stat = TaskRunner_Step(&app->exec_runner);
        app_title_swap_if_pending(app);
        if( app->app_state == APP_STATE_READY )
            app_async_polls(app);
        if( main_stat == TASK_RUNNER_IDLE && exec_stat == TASK_RUNNER_IDLE &&
            app->app_state == APP_STATE_READY && !app->pending_tree_refresh &&
            !app->world_load_inflight )
            break;
    }
    if( guard <= 0 )
        TORIRS_LOG("app: boot wait exceeded step guard\n");
}

/*
 * The face the boot bar's caption is drawn with, on every lane.
 *
 * ONE face for the whole boot, and a baked one: ToriRSChromeFont_Menu is cache
 * archive 496 (b12) baked into .rdata (engine/torirs_debug_font_baked.h),
 * needing no cache and no IO.
 *
 * The alternative -- resolve the profile's p12 when the cache has handed it
 * over and fall back to the baked face until then -- is what this used to do,
 * and it is wrong twice. It changes face mid-boot, so the same bar draws its
 * sentences in two or three different hands as the archives land; and it makes
 * the caption depend on cache state that the GPU lanes, which draw the bar
 * before any frame is built, have no path to wait for.
 *
 * The reference has neither problem because it has one face for the whole
 * screen and always has it: an AWT `java.awt.Font("Helvetica", BOLD, 13)`
 * (deob class510; Client-TS GameShell.messageBox draws `bold 13px helvetica`).
 * A software rasteriser has no system face, so a baked one is the same answer
 * to the same question -- and the BOLD one, for the same reason.
 *
 * At 1x, because this lands in canvas pixels on every lane: the boot bar is
 * placed in canvas coordinates, and a chrome-scaled face would paint
 * double-size text into them.
 *
 * The Ensure is what puts the font IN the scene -- ToriDraw_SceneFontAdd emits
 * TORIDRAW_EVENT_FONT_LOAD -- and the GPU backends resolve a font id by
 * looking it up there (d3d9_ui_ensure_font, gl3_ensure_font_slot). A lane that
 * never called this would find no font under the id and draw nothing, which is
 * exactly what the D3D9 boot screen did while the caption lived in App_Render.
 */
static int
app_boot_bar_font_scene_id(struct App* app)
{
    assert(app);
    return UITreeSceneBridge_EnsureDebugFont1x(&app->bridge, TORIRS_CHROME_FONT_MENU);
}

char const*
App_BootBarCaption(
    struct App* app,
    int* out_font_scene_id)
{
    char const* caption = NULL;
    int font_scene_id;

    assert(app);
    assert(out_font_scene_id);

    /*
     * What the boot task asked for, when it asked for anything; otherwise the
     * profile's own word for the phase.
     *
     * The render step does not decide what a load looks like: the task that
     * knows which stage it is at says so, and this obeys. A task which never
     * asks gets the standing sentence rather than silence.
     */
    if( app->runner.render.intent == TORIRS_RENDER_BOOT_BAR && app->runner.render.caption &&
        app->runner.render.caption[0] )
        caption = app->runner.render.caption;
    else
        caption = RS_LoginReplies_String(
            &app->login_replies, app->screen == APP_SCREEN_GAME ? "entering_world" : "loading");
    if( !caption || !caption[0] )
        return NULL;

    font_scene_id = app_boot_bar_font_scene_id(app);
    if( font_scene_id < 0 )
        return NULL;
    *out_font_scene_id = font_scene_id;
    return caption;
}

/*
 * The bar's caption, centred on `center_x` with its baseline at `baseline_y`.
 *
 * The software lane's half of App_BootBarCaption: the GPU lanes draw the same
 * two facts (`text`, `font_scene_id`) through their own text paths.
 */
void
app_boot_bar_caption(
    struct App* app,
    int* pixels,
    int width,
    int height,
    int center_x,
    int baseline_y,
    char const* text,
    int font_scene_id)
{
    struct ToriDraw_Font* font;
    struct ToriDraw_ViewPort vp;

    assert(app);
    assert(pixels);
    assert(text);
    assert(font_scene_id >= 0);

    font = ToriDraw_SceneFontGet(app->scene, font_scene_id);
    if( !font )
        return;

    vp.width = width;
    vp.height = height;
    vp.stride = width;
    vp.x_center = width / 2;
    vp.y_center = height / 2;
    vp.clip_left = 0;
    vp.clip_top = 0;
    vp.clip_right = width;
    vp.clip_bottom = height;

    (void)ToriDraw2D_DrawString(
        font, &vp, center_x, baseline_y, text, 0xFFFFFF, true, false, pixels);
}

static int
Task_AppBoot_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppBoot* self = (struct Task_AppBoot*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    /* This stage's place in the profile's own list, where it declares
     * one. The modern lane's boot is numbered by the deob and continues
     * from where its cache-index steps left off; a lane that names no
     * such step keeps the client's own position. */
    if( !app_boot_bake_is_quiet(app) )
    {
        app->boot_progress = 10;
        if( !app_preload_announce(app, "boot_config") )
            app_title_progress(app, 10, "loading_config");
    }
    /* Let the frame loop draw the bar where it now stands before the
     * next stretch of work begins. Without this the whole boot settles
     * inside one frame and the bar only ever appears at 100. */
    PT_TASK_YIELD_TO_RENDER(
        TORIRS_RENDER_BOOT_BAR, app->title.progress_percent, app->title.progress_text);

    /*
     * Varbit types, before anything that can run a script.
     *
     * A varbit read happens deep inside CS2 execution with nowhere to yield to a
     * load, so the table has to be resident first. Without it every varbit reads 0
     * and any script branching on one silently takes the zero path.
     *
     * Both eras, different storage: dat2 splits config group 14 into one file per id,
     * dat1 packs them into a single `varbit.dat` inside the config jagfile. CS1 reads
     * varbits through the same VarPManager as CS2, so both need it.
     */
    /*
     * Varplayer types first, and only on dat2.
     *
     * The client reads one field off a varplayer record — `clientcode`, the
     * marker that a varp drives behaviour baked into the client rather than a
     * script (sound volume; the Controls panel's two Attack options). Nothing
     * loaded this table before, so on an OldSchool cache every clientcode read
     * 0 and none of that behaviour could fire.
     *
     * Before the varbits because SetVarpTypes reallocates the var value arrays
     * and SetVarbitTypes does not: the other order works only for as long as
     * nothing has written a varp in between.
     *
     * dat1 keeps varplayers in `varp.dat` inside the config jagfile; that path
     * has no loader and the classic revisions this tree boots have no
     * clientcode-driven settings, so it stays untyped there.
     */
    /*
     * Everything from here to `boot_config_ready = 1` is ONCE PER SESSION, not
     * once per boot — and this task boots again on every root remount (the
     * Display panel's Fixed/Classic/Modern switch, App_OpenRootInterface).
     *
     * `VarPManager_SetVarpTypes` reallocates the varp *value* arrays, so a
     * second pass over the loads below calloc-zeroes every varp the server has
     * sent this session. Measured on a layout switch: varp 1737 went
     * -2147483648 -> 0 across the remount, which clears varbit 8119
     * (`has_displayname_transmitter`), and the chatbox's own onload then paints
     * "You must set a name before you can chat." over the input line. The name
     * bit is only the visible one; run/attack-style/prayer/bank/settings varps
     * go with it, and the server transmits a varp on WRITE, so nothing puts
     * them back until content happens to write each one again.
     *
     * The seeds further down are the same shape: they are what the client
     * believes before a server speaks, so re-running them mid-session would
     * overwrite what the server said with a boot-time default.
     */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2VarpLoad(app->provider, &app->varps));

    /*
     * The clientscript index's reference table.
     *
     * Loaded once, for its group IDENTIFIERS: the cache binds a script to
     * "this npc appeared" / "this loc was placed" through the group NAME and
     * nothing else, so without this table 218 loc scripts and 23 npc scripts
     * in cache.osrs239 are unreachable. See game/rs_client_trigger.h.
     */
    if( !app->boot_config_ready )
        PT_TASK_AWAITSELF_IF(CreateTask_ClientScriptTableLoad(app->provider));

    if( !app->boot_config_ready && app->cfg.cache_kind == APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat1VarbitLoad(app->provider, &app->varps));
    else if( !app->boot_config_ready )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2VarbitLoad(app->provider, &app->varps));

    /* Hitsplat types. dat1 has no such config group — it keeps the splat
     * graphics in a "hitmarks" sprite archive, which the static-sprite path
     * binds — so this is a dat2-only load and an absent group is not an error. */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2HitsplatLoad(app->provider, &app->hitsplats));

    /* WorldEntityConfig types (sailing boats, config group 72). dat2 only and
     * OldSchool 239+ within that; an absent group leaves the table empty,
     * which is the pre-sailing world rather than an error. */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2WevConfigLoad(app->provider, &app->wev_configs));

    /* Healthbar types. dat2 only for the same reason as hitsplats -- dat1 has
     * no such config group, and the table's defaults are the reference's own
     * constructor, so an absent group draws exactly what the hardcoded 30-wide
     * bar used to. */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2HealthbarLoad(app->provider, &app->healthbars));

    /*
     * Ambient soundscapes. dat2 only, and OldSchool 231+ within that -- an
     * absent group leaves the table empty, which the audio layer reads as the
     * pre-231 meaning of AMBIENTSOUND_START (the id is a sound effect). Binding
     * the table unconditionally is deliberate: the empty case is a supported
     * reading, not a failure to load.
     */
    if( !app->boot_config_ready && app->cfg.cache_kind != APP_CACHE_DAT1 )
        PT_TASK_AWAITSELF_IF(CreateTask_Dat2SoundscapeLoad(app->provider, &app->soundscapes));
    RS_Audio_SetSoundscapes(&app->audio, &app->soundscapes);

    /*
     * The settings the player chose last launch, before anything reads one.
     *
     * Volumes are device settings: no packet carries them to a server and none
     * carries them back, so the file this reads is the only thing standing
     * between "turn the music down" and a client that is loud again tomorrow.
     * Restoring into the option store (not the varps) is the right direction —
     * the store is what GAMEOPTION_GET answers with, and the four varps below
     * are then seeded from it, so both halves of interface 116 agree.
     */
    if( !app->boot_config_ready )
    {
        app->prefs_path = RS_Prefs_Path();
        RS_Prefs_Defaults(&app->prefs);
    }
    if( !app->boot_config_ready && app->prefs_path )
        PT_TASK_AWAITSELF_IF(CreateTask_PrefsLoad(&app->prefs, app->prefs_path));
    if( !app->boot_config_ready )
        RS_Prefs_ApplyToHost(&app->prefs, &app->host);
    /*
     * A window mode the manifest or command line stated wins over the saved
     * one. App_SetBootWindowMode has already run (it must, before the root's
     * scripts call getwindowmode), so the line above just overwrote it;
     * `cfg.window_mode` is 0 when nothing said anything, which is when the
     * saved default is the only opinion there is. Same precedence as a server
     * VARP over the seeded volumes: an explicit instruction for this run beats
     * what the last run happened to leave behind.
     *
     * Immediately after the clobber, and BEFORE the plugin boot below, because
     * the window a frame is laid out in is decided during that boot: committing
     * a selection calls App_SyncPluginLayoutCanvas, whose native answer IS
     * `default_window_mode`. Restoring after it meant the dat1 lane -- whose
     * boot mode is fixed because its 2004 gameframe is a baked 765x503 layout
     * -- was flipped to the saved resizable default by plugin selection and
     * left there, since nothing calls that function again while the native
     * frame stands. On screen: the frame stranded in the top-left of a grey
     * canvas.
     */
    if( !app->boot_config_ready && app->cfg.window_mode )
        app->host.default_window_mode = app->cfg.window_mode;

    /*
     * Plugins: read the script manifest, compile each script, apply saved
     * settings, then start everything. Awaited here, on the boot path, for the
     * same reason the settings above are -- a plugin that starts after the
     * first frame has already missed events, and one that starts before its
     * saved config arrives runs a frame on defaults and then jumps.
     *
     * Everything it touches goes through the IO queue, so this is the same
     * code on the native lanes and in the browser, where a synchronous read
     * does not exist.
     */
    if( !app->boot_config_ready && app->plugins )
        PT_TASK_AWAITSELF_IF(
            CreateTask_PluginBoot(app->plugins, PluginManifest_Path(), PluginPrefs_Path()));

    /*
     * Seed the four audio volumes.
     *
     * The option store holds the real volumes -- the restore above, or the
     * defaults -- but the varps those sliders actually read are whatever
     * SetVarpTypes left behind, which is zero. Interface 116 believes the
     * varps: script 7101 greys every bobble while %var3796 <= 0 and script 9254
     * shows the mute cross on all four icons, so a client playing at the
     * restored volume would come up looking muted, and the first click on
     * "Mute" would read the zero, take script 9255's unmute branch and turn the
     * volume *up*.
     *
     * A client that boots muted (RS_CS2Host_OptionDefault) is not that bug: the
     * master option really is zero, so the cross the panel draws is the truth
     * and the first click on it is a genuine unmute.
     *
     * These are ordinary player varps, so a server that sends VARP_SMALL/LARGE
     * for them overwrites this — which is the right precedence, and ToriRSServer
     * now exercises it: [proc,settings_audio_login] seeds and transmits all four
     * on every login, because a reference client has no seeding of its own and
     * the gameframe root's onload (clientscript 876 -> 4618) applies the varps
     * OVER the client's 127/127/127/100 defaults. An untransmitted zero there is
     * silence, and the rev-239 MIDI_SONG handler drops the packet outright while
     * the music volume is zero — the whole music path was unreachable in
     * RuneLite for exactly that reason. So what is seeded here is the
     * pre-login default and nothing more.
     *
     * Optimistic (not the server setter) so the ChangeFn runs and the host
     * snapshot stays in agreement with the varps; and before the tree is built,
     * so 116 constructs its bobbles green rather than needing a repaint.
     */
    if( !app->boot_config_ready )
    {
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_MASTER_VOLUME,
            RS_CS2Host_GetOption(
                &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MASTER_VOLUME));
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_MUSIC_VOLUME,
            RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_MUSIC_VOLUME));
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_SOUND_VOLUME,
            RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_SOUND_VOLUME));
        VarPManager_SetVarpOptimistic(
            &app->varps,
            RS_CS2_VARP_AREA_VOLUME,
            RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_AREA_VOLUME));
    }

    /* Baseline for the change detection below: what is in the host now is what
     * is on disk, so nothing written here counts as a change worth saving. */
    if( !app->boot_config_ready )
    {
        RS_Prefs_CaptureFromHost(&app->prefs, &app->host);
        app->prefs_dirty_cycle = 0;
    }
    if( getenv("TORIRS_AUDIO_TRACE") || getenv("TORIRS_AUDIO_DEBUG") )
        TORIRS_LOG(
            "audio: seeded volume varps master(%d)=%d music(%d)=%d "
            "sfx(%d)=%d area(%d)=%d\n",
            RS_CS2_VARP_MASTER_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_MASTER_VOLUME),
            RS_CS2_VARP_MUSIC_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_MUSIC_VOLUME),
            RS_CS2_VARP_SOUND_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_SOUND_VOLUME),
            RS_CS2_VARP_AREA_VOLUME,
            VarPManager_GetVarp(&app->varps, RS_CS2_VARP_AREA_VOLUME));

    /*
     * Seed the "interface resizing" setting, for the same reason and with the
     * same precedence as the four volumes above: it is a client setting nobody
     * transmits, and the zero SetVarbitTypes leaves behind is a value, not an
     * absence.
     *
     * Which era owns the id — and the whole account of what the two branches do
     * — is in features.h. In short: at zero the cache's interface-window helper
     * (clientscript 1898, and 1904 for the skill guide) positions a modal's
     * panel at `if_getx/if_gety(mainmodal)`, the slot's *parent-relative*
     * origin, inside the modal's own root. In resizable mode the slot is
     * centred in `hud_container_front`, so that applies the centring offset
     * twice and every main modal sits down-and-right of the hole clientscript
     * 910 dims for it, half-under the sidebar and the chatbox.
     *
     * Optimistic, before the tree is built, and overridable by a server VARP —
     * all three for the reasons stated above the volumes.
     */
    if( !app->boot_config_ready && app->features->varbit_interface_resizing > 0 )
        VarPManager_SetVarbitOptimistic(&app->varps, app->features->varbit_interface_resizing, 1);

    /* End of the once-per-session half. A remount from here down rebuilds the
     * tree against the var state the session already has. */
    app->boot_config_ready = 1;

    /* `[ui:varc]` — the var writes that accompany the login IF_OPENSUB burst.
     * Before the tree opens, because the root's onLoad scripts branch on them.
     * Skipped when networked, for the same reason [ui:gameframe] is. */
    if( app->cfg.varc_seed_count > 0 && !app->net_enabled )
    {
        for( int i = 0; i < app->cfg.varc_seed_count; i++ )
            VarCManager_SetInt(
                &app->varcs, app->cfg.varc_seeds[i].id, app->cfg.varc_seeds[i].value);
        TORIRS_LOG("varc: seeded %d client vars from the manifest\n", app->cfg.varc_seed_count);
    }

    /* A CS2Dom native preview seeds the actual client stores after cache types
     * and manifest defaults exist, but before UITree build/onLoad reads them.
     * The state path is an exact environment value supplied to a child process;
     * its contents are a bounded binary packet, never command-line or shell
     * syntax. Re-apply on a root remount so every preview build sees the same
     * declared host state. Ordinary client runs cannot enable this by setting a
     * state path alone: the headless preview output must also be requested. */
    if( getenv("TORIRS_PREVIEW_BMP") && getenv("TORIRS_PREVIEW_STATE") )
    {
        char state_error[192];
        int applied = 0;
        app->preview_state_failed = !ToriRSPreviewState_ApplyFile(
            getenv("TORIRS_PREVIEW_STATE"),
            &app->varps,
            &app->varcs,
            &app->stats,
            &applied,
            &app->preview_state_stat_mask,
            state_error,
            sizeof(state_error));
        if( app->preview_state_failed )
            TORIRS_ERR("preview_state: %s\n", state_error);
        else
            TORIRS_LOG("preview_state: applied %d records\n", applied);
    }

    /* The config tables are in; the tree build below is the long one, and the
     * references both name it while it runs rather than leaving the bar still
     * (Client-TS "Requesting interface", the deob "Loading interfaces"). */
    /* This stage's place in the profile's own list, where it declares
     * one. The modern lane's boot is numbered by the deob and continues
     * from where its cache-index steps left off; a lane that names no
     * such step keeps the client's own position. */
    if( !app_boot_bake_is_quiet(app) )
    {
        app->boot_progress = 30;
        if( !app_preload_announce(app, "boot_tree") )
            app_title_progress(app, 30, "loaded_config");
    }
    /* Let the frame loop draw the bar where it now stands before the
     * next stretch of work begins. Without this the whole boot settles
     * inside one frame and the bar only ever appears at 100. */
    PT_TASK_YIELD_TO_RENDER(
        TORIRS_RENDER_BOOT_BAR, app->title.progress_percent, app->title.progress_text);

    /* One root-build path. A manifest that names no RevConfig at all still comes
     * through here: the builder synthesises the single rs_iface mount of
     * boot_interface_id, which is what the old open-the-interface-directly
     * branch produced (TS parity: WidgetManager.setRootInterface(groupId) — any
     * group can be the root, no hardcoded 161 chrome required). */
    PT_TASK_AWAITSELF(CreateTask_UITreeBuild(&app->builder));

    /* Root-build already performs the reference client's initial var-transmit
     * pass after onLoad, so preview varp/varbit seeds reach listeners without a
     * synthetic tick. Stats normally arrive later in UPDATE_STAT and therefore
     * have no equivalent build pass. A seeded preview stat is both the initial
     * value onLoad reads and one precise post-registration stat transmit. This
     * is preview-only and filtered to the supplied skills; unrelated hooks are
     * not made to run merely because a state packet exists. */
    if( app->preview_state_stat_mask )
    {
        int stat_ids[RS_PLAYER_STATS_SKILL_COUNT];
        int stat_count = 0;
        for( int stat_id = 0; stat_id < RS_PLAYER_STATS_SKILL_COUNT; stat_id++ )
        {
            if( app->preview_state_stat_mask & ((uint32_t)1u << stat_id) )
                stat_ids[stat_count++] = stat_id;
        }
        PT_TASK_AWAITSELF_IF(
            CreateTask_CS2StatTransmitDispatchSet(&app->host, stat_ids, stat_count));
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    }
    TORIRS_LOG(
        "RevConfigBuild done: iface=%d ui=%s inline=%s tree_components=%u sprites=%d "
        "fonts=%d onloads=%d inv_hooks=%d var_hooks=%d mounts=%d\n",
        app->boot_interface_id,
        app->cfg.revconfig_ui_ini ? app->cfg.revconfig_ui_ini : "-",
        app->cfg.revconfig_inline_ini ? app->cfg.revconfig_inline_ini : "-",
        app->tree->component_count,
        app->builder.sprite_count,
        app->builder.font_count,
        app->builder.onload_count,
        app->host.inv_transmit_hook_count,
        app->host.var_transmit_hook_count,
        app->tree->interface_parent_count);

    /* This stage's place in the profile's own list, where it declares
     * one. The modern lane's boot is numbered by the deob and continues
     * from where its cache-index steps left off; a lane that names no
     * such step keeps the client's own position. */
    if( !app_boot_bake_is_quiet(app) )
    {
        app->boot_progress = 60;
        if( !app_preload_announce(app, "boot_interfaces") )
            app_title_progress(app, 60, "loading_interfaces");
    }
    /* Let the frame loop draw the bar where it now stands before the
     * next stretch of work begins. Without this the whole boot settles
     * inside one frame and the bar only ever appears at 100. */
    PT_TASK_YIELD_TO_RENDER(
        TORIRS_RENDER_BOOT_BAR, app->title.progress_percent, app->title.progress_text);

    /* Shared b12 fallback before configured overlay models are bound. Normally
     * already resident — the RevConfig assets pass loads every declared
     * [font:…] — so this only does work when the profile reaches b12 through a
     * different section. A profile that declares no b12 at all skips it, and
     * the minimenu falls back to a text node's font. */
    PT_TASK_AWAITSELF_IF(
        app_font_b12_cache_id(app) >= 0
            ? CreateTask_FontLoad(app->provider, app_font_b12_cache_id(app))
            : NULL);

    /* This stage's place in the profile's own list, where it declares
     * one. The modern lane's boot is numbered by the deob and continues
     * from where its cache-index steps left off; a lane that names no
     * such step keeps the client's own position. */
    if( !app_boot_bake_is_quiet(app) )
    {
        app->boot_progress = 75;
        if( !app_preload_announce(app, "boot_media") )
            app_title_progress(app, 75, "loading_media");
    }
    /* Let the frame loop draw the bar where it now stands before the
     * next stretch of work begins. Without this the whole boot settles
     * inside one frame and the bar only ever appears at 100. */
    PT_TASK_YIELD_TO_RENDER(
        TORIRS_RENDER_BOOT_BAR, app->title.progress_percent, app->title.progress_text);

    if( getenv("TORIRS_ANIM_DEBUG") )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( node->freed || node->type != UIELEM_RS_MODEL )
                continue;
            TORIRS_LOG(
                "anim_debug: com=0x%x model=%d seq=%d\n",
                node->component_id,
                node->u.rs_model.gamecache_model_id,
                node->u.rs_model.anim_seq_id);
        }
    }

    app_bind_configured_overlays(app);

    /*
     * `[ui:gameframe]` — the login IF_OPENSUB burst, from the manifest.
     *
     * A gameframe root is a frame of empty slots; every panel in it (chat box,
     * orbs, the sidebar tabs, the inventory) arrives as a separate IF_OPENSUB
     * once the server has the player. Offline there is no server, so the frame
     * renders as chrome around nothing. Listing the mounts in the manifest
     * reproduces the burst without inventing a client-side default: the values
     * are whatever the era's server actually sends (for rev 634 they are read
     * off Void's `openGamframe` — see docs/RS2_634_CLIENT_REFERENCES.md).
     *
     * Skipped when networked: the server sends the real sequence, and mounting
     * on top of it would fight whatever it opens.
     */
    if( app->cfg.gameframe_mount_count > 0 && !app->net_enabled )
    {
        for( int i = 0; i < app->cfg.gameframe_mount_count; i++ )
        {
            struct BootManifestGameframeMount const* mount = &app->cfg.gameframe_mounts[i];
            int parent = mount->parent_interface_id > 0 ? mount->parent_interface_id
                                                        : app->boot_interface_id;
            App_OpenSubInterface(
                app, (parent << 16) | (mount->component & 0xFFFF), mount->interface_id, 0);
        }
        TORIRS_LOG(
            "gameframe: queued %d sub-interface mounts under iface %d\n",
            app->cfg.gameframe_mount_count,
            app->boot_interface_id);
    }

    /* Tab/interface-slot state seeds from the baked tree (INI componentno= and
     * selected= drive it; nothing here is hardcoded). */
    RS_UISlots_RebindTree(&app->slots, app->tree);

    /* Queue model-widget sequences (they land through the frame pump and
     * render at rest pose meanwhile) and apply whatever is already loaded. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    UITreeAnim_Advance(app->tree, app->scene, 0);

    app_sync_textures(app);

    /* No viewport component in the opened interface -> no map at all. Trees
     * that grow one later (a mounted interface) load lazily in App_RunOnce.
     * When networked, the server's REBUILD_NORMAL is the sole world-load driver
     * (its region + scene base match the entities); a default region 50,50 load
     * here would race it and clobber the rebuilt scene, so skip it. */
    if( App_WorldNodeIndex(app) >= 0 && !app->net_enabled )
        app_world_load_begin(app, NULL, 0);

    /* This stage's place in the profile's own list, where it declares
     * one. The modern lane's boot is numbered by the deob and continues
     * from where its cache-index steps left off; a lane that names no
     * such step keeps the client's own position. */
    if( !app_boot_bake_is_quiet(app) )
    {
        app->boot_progress = 90;
        if( !app_preload_announce(app, "boot_ready") )
            app_title_progress(app, 90, "preparing");
    }
    /* Let the frame loop draw the bar where it now stands before the
     * next stretch of work begins. Without this the whole boot settles
     * inside one frame and the bar only ever appears at 100. */
    PT_TASK_YIELD_TO_RENDER(
        TORIRS_RENDER_BOOT_BAR, app->title.progress_percent, app->title.progress_text);

    app_chat_build_view(app);
    app->emit.count = 0;
    UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, -1);
    /* Prime the viewport cache so the first App_Render can draw the world
     * without waiting for a App_RunOnce pass to latch it. */
    app_update_world_viewport(app);

    app->boot_progress = 100;
    /* Cleared, not filled: a bar left at 100 keeps the loading group in
     * front of the login form it was covering. */
    RS_Title_SetProgress(&app->title, -1, NULL);
    app->app_state = APP_STATE_READY;
    /* A freshly baked title tree shows every screen's group at once until it is
     * told which one is current. */
    if( app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING )
    {
        app_title_sync_groups(app);
        app_title_flames_start(app);
    }
    app->pending_tree_refresh = 1;
    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_AppBoot_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppBoot_VTable = {
    .run = Task_AppBoot_Run,
    .free = Task_AppBoot_Free,
};

/*
 * Rebuild the whole tree from RevConfig, taking one layout group.
 *
 * The body App_OpenRootInterface and App_OpenTitleScreen share. The only
 * things that differ between a gameframe bake and a title bake are which
 * [layout:] group is selected, which is refused, and whether there is a root
 * interface to mount -- the title screen has none, because no revision ships
 * one as interface data.
 */
static void
app_open_tree(
    struct App* app,
    int interface_id,
    char const* layout_group,
    char const* layout_group_exclude)
{
    struct Task_AppBoot* task;

    assert(app);
    memset(&app->open_stats, 0, sizeof(app->open_stats));

    if( getenv("TORIRS_CS2_TRACE") )
        g_cs2_trace_mode = 2;

    /* Display Fixed/Classic/Modern remount: drop the live gameframe before the
     * new root bakes. Without this, InterfaceOpen would add 548/164 beside the
     * old 161 forest, and IF_OPENSUB targets for the new top race the boot. */
    if( app->tree && app->tree->root_index >= 0 )
    {
        UITree_Clear(app->tree);
        app->host.inv_transmit_hook_count = 0;
        app->host.var_transmit_hook_count = 0;
        app->host.stat_transmit_hook_count = 0;
    }

    /* RevConfig build, always: the config names the whole gameframe (chrome
     * widgets plus the cache interface packs mounted under them), and a manifest
     * that declares none of it still gets the plain `interface_id` mount
     * synthesised for it. That is what keeps declared sibling order meaningful —
     * a pack is baked *under* the rs_iface node its layout record placed, so
     * whatever the CS2 scripts then do to it stays inside that subtree.
     *
     * The CS2 host is passed only for dat2 — dat1 interface packs carry IF1
     * scripts, which the CS1 host evaluates on the tick instead. */
    if( app->builder_active )
        UITreeBuilder_Free(&app->builder); /* display-mode remount: re-Init below */
    UITreeBuilder_InitEx(
        &app->builder,
        app->provider,
        app->tree,
        &app->invs,
        App_UiLogic(app) == APP_UI_LOGIC_CS1 ? NULL : &app->host,
        app->cfg.revconfig_ui_ini,
        app->cfg.revconfig_cache_ini);
    if( app->cfg.revconfig_inline_ini )
        strncpy(
            app->builder.inline_ini_path,
            app->cfg.revconfig_inline_ini,
            sizeof(app->builder.inline_ini_path) - 1);
    /* A componentno-less rs_iface means "the root", which is whatever we are
     * being asked to root to — the manifest's boot interface on the first call,
     * the server's IF_SETTOPLEVEL group on a display-mode remount. */
    app->builder.root_interface_id = interface_id;
    /* Which [layout:] group this bake takes, and which it refuses. The
     * gameframe refuses the title group so the title screen's widgets -- and
     * their every-frame repaint -- never enter the in-game tree. */
    app->builder.layout_group[0] = '\0';
    app->builder.layout_group_exclude[0] = '\0';
    if( layout_group )
        strncpy(app->builder.layout_group, layout_group, sizeof(app->builder.layout_group) - 1);
    if( layout_group_exclude )
        strncpy(
            app->builder.layout_group_exclude,
            layout_group_exclude,
            sizeof(app->builder.layout_group_exclude) - 1);
    /* Bake remaps sprite/font ids to scene ids so the tree renders directly. */
    app->builder.bridge = &app->bridge;
    /* Where a component's `role=` is interned. The table outlives the tree, so
     * a remount re-stamps the same ids onto the new nodes rather than handing
     * out fresh ones. */
    app->builder.roles = &app->ui_roles;
    app->builder_active = 1;

    app->app_state = APP_STATE_BOOTING;
    app->boot_interface_id = interface_id;
    /* A quiet bake leaves the bar where the loud one parked it (100) instead
     * of walking it back to zero for work the first bake already narrated.
     * @see app_boot_bake_is_quiet. */
    if( !app_boot_bake_is_quiet(app) )
        app->boot_progress = 0;
    /* The booted gameframe root is what IF_GETTOP reports: this client mounts
     * every server sub-interface into it and treats a differing server
     * IF_OPENTOP as informational (see rs_gameproto_exec), so the two agree. */
    app->host.top_interface_id = interface_id;

    /*
     * The profile's own preload list first, as a SIBLING rather than a child.
     *
     * The queue runs its head to completion before anything behind it, so
     * ordering is free -- and being the head is what lets its render
     * requests reach the runner directly, which is the whole point of a task
     * that exists to show the bar moving.
     *
     * NULL on a profile that names no cache indices, which is every dat1
     * lane: their list is jag archives, loaded by other machinery.
     */
    if( app->provider )
    {
        /* One of the two answers, never both: a profile lists cache indices
         * or jag archives, and each constructor returns NULL for the list
         * that is not its own. The epoch is not tested here because the
         * profile already decides it by what it declares. */
        struct ToriRS_Task* preload =
            CreateTask_Dat2Preload(app->provider, &app->preload, &app->login_replies);

        if( !preload )
        {
            preload = CreateTask_Dat1Preload(
                app->provider,
                &app->preload,
                &app->login_replies,
                app->cache_on_demand && !app->dat1_prefetch_queued);
            app->dat1_prefetch_queued = 1;
        }
        if( preload )
            ToriRS_TaskQueue_Add(app->runner.queue, preload);
    }

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppBoot_VTable;
    strncpy(task->task.name, "AppBoot", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
}

void
App_OpenRootInterface(
    struct App* app,
    int interface_id)
{
    assert(app);
    app->screen = APP_SCREEN_GAME;
    /* Half a megabyte of simulation buffers that only the title screen wants,
     * and the tree that drew them is about to be cleared. */
    app_title_flames_stop(app);
    app_open_tree(app, interface_id, NULL, APP_TITLE_LAYOUT_GROUP);
}

int
App_HasTitleScreen(struct App const* app)
{
    assert(app);
    /* Undeclared means absent, the whole revconfig contract: a profile that
     * names no [layout:title] has no title screen and must boot straight into
     * the game, the way every offline and bench manifest here already does. */
    return app->cfg.revconfig_ui_ini && app->cfg.revconfig_ui_ini[0] &&
           revconfig_ini_has_layout_group(app->cfg.revconfig_ui_ini, APP_TITLE_LAYOUT_GROUP);
}

void
App_OpenTitleScreen(struct App* app)
{
    assert(app);
    /* No root interface: the title screen is client widgets over a cache
     * background, and the gameframe the server will re-root to does not exist
     * until a login succeeds. */
    app->screen = APP_SCREEN_TITLE;
    app_open_tree(app, -1, APP_TITLE_LAYOUT_GROUP, NULL);
}

void
App_BootGameframeThenTitle(struct App* app)
{
    assert(app);
    /* The loading half of the boot, run FIRST: the same gameframe bake the
     * login used to pay for. Everything it fetches -- interface packs, media,
     * fonts, the profile's preload list -- lands in the provider's caches
     * before the login screen is offered, which is the reference's own order
     * (its bar walks "Loading interfaces"/"Loaded media" to 100 before the
     * title appears). The tree itself is baked to be thrown away: the swap in
     * app_title_swap_if_pending replaces it with the title tree the moment it
     * settles, and the post-login rebake redoes only the CPU half against
     * warm caches. */
    app->screen = APP_SCREEN_BOOT;
    app->title_session.pending_after_boot = true;
    app_open_tree(app, -1, NULL, APP_TITLE_LAYOUT_GROUP);
}

/* The warm gameframe bake settled: swap to the title screen. Called from the
 * two places that observe the bake finishing -- App_RunOnce before it can
 * render a frame of the gameframe, and App_BootWait so headless harnesses
 * return with the title (not the warm-up tree) as the settled state. */
void
app_title_swap_if_pending(struct App* app)
{
    assert(app);
    if( !app->title_session.pending_after_boot || app->app_state != APP_STATE_READY )
        return;
    app->title_session.pending_after_boot = false;
    App_OpenTitleScreen(app);
}

static int
Task_OpenSubRefresh_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_OpenSubRefresh* self = (struct Task_OpenSubRefresh*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    /* IF_OPENTOP remounts on the asset runner; IF_OPENSUB rides the exec
     * pipeline. Wait out APP_STATE_BOOTING so the new root's slots exist
     * before mount_pack_under_target asserts.
     *
     * TASK_AWAIT_STATE, not a bare PT_YIELD loop, and the difference is a
     * deadlock. Only Task_AppBoot on app->runner clears APP_STATE_BOOTING, and
     * app->runner is stepped by the frame loop — which cannot get its turn back
     * while this queue is still settling. A plain yield here therefore spins
     * app_logic_tick's exec drain forever at 100% CPU: measured on every
     * Display-panel layout switch, which sends IF_OPENTOP and its IF_OPENSUB
     * mounts in one burst. */
    TASK_AWAIT_STATE(base, &self->pt, app->app_state != APP_STATE_BOOTING);
    if( self->interface_id > 0 && UITree_FindByComponentId(app->tree, self->target_uid) < 0 )
    {
        TORIRS_ERR(
            "if-opensub: target 0x%08x missing after boot (iface=%d); skip\n",
            (unsigned)self->target_uid,
            self->interface_id);
        PT_EXIT(&self->pt);
    }
    if( self->interface_id > 0 )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_InterfaceOpenSub(
            app->provider,
            app->tree,
            &app->host,
            &app->invs,
            &app->bridge,
            self->target_uid,
            self->interface_id,
            self->type,
            &app->open_stats));
    }
    else
    {
        /* IF_CLOSESUB: reclaim the outgoing group, then drop the mount record.
         *
         * This is the reference client's rule, and it has no per-slot cases in
         * it: `method9520` closes a sub-interface by unloading the whole widget
         * group (class304.method7206 nulls every Widget and clears the loaded
         * flag), and the single exemption lives on the OPEN side — a group
         * being moved to another slot is not unloaded. So a close always
         * reclaims here.
         *
         * The version before this hid the group instead (hide +
         * mount_hidden on its roots) so the next mount could reuse the bake,
         * with `chatbox:chatmodal` carved out because alternating
         * chat_left/chat_right left shadowed text that IF_SETTEXT then
         * updated instead of the live copy. That carve-out was the
         * optimisation admitting it was wrong; reclaiming everywhere is both
         * the parity behaviour and one rule instead of two.
         *
         * Hiding the SLOT here was wrong in an earlier version still: nothing
         * ever un-hides a slot, so after one close every later mount into it
         * laid out and never drew. The mount task asserts interface_id>0, so
         * a close never routes through it. */
        {
            struct timespec close_t0;
            struct timespec close_t1;
            uint64_t close_ns;
            int rec = UITree_InterfaceParentFind(app->tree, self->target_uid);
            clock_gettime(CLOCK_MONOTONIC, &close_t0);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_CLOSE, 1);
            if( rec >= 0 )
            {
                int old_group = app->tree->interface_parents[rec].group_id;
                struct UITreeNodeSet const* gset = UITree_GroupNodes(app->tree, old_group);
                UITreeIfaceStats_NoteClose(old_group);
                TORIRS_PERF_COUNT(
                    TORIRS_PERF_CTR_IFACE_GROUP_SCAN_NODES, gset ? (int64_t)gset->count : 0);
                UITree_ReclaimInterfaceGroup(app->tree, old_group);
                RS_CS2Host_ClearHooksForInterfaceGroup(&app->host, old_group);
            }
            UITree_InterfaceParentClear(app->tree, self->target_uid);
            clock_gettime(CLOCK_MONOTONIC, &close_t1);
            close_ns = (uint64_t)(close_t1.tv_sec - close_t0.tv_sec) * 1000000000ull +
                       (uint64_t)(close_t1.tv_nsec - close_t0.tv_nsec);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_CLOSE_NS, (int64_t)close_ns);
        }
        /*
         * Then tell the tree a sub-interface went away.
         *
         * The mount path runs these hooks (task_interface_open step 8) and this
         * one did not, which is asymmetric and was wrong: the gameframe's
         * `on_sub_change` is what decides whether the sidebar shows the tab
         * strip or whatever replaced it. Opening the bank hid the tabs and
         * closing it left them hidden, so the sidebar came back blank.
         */
        PT_TASK_AWAITSELF_IF(CreateTask_CS2SubChangeDispatch(&app->host));
    }
    App_RefreshAfterTreeMutation(app);
    PT_END(&self->pt);
}

static void
Task_OpenSubRefresh_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_OpenSubRefresh_VTable = {
    .run = Task_OpenSubRefresh_Run,
    .free = Task_OpenSubRefresh_Free,
};

static void
app_enqueue_open_sub(
    struct App* app,
    int target_uid,
    int interface_id,
    int type)
{
    struct Task_OpenSubRefresh* task;

    assert(app);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_OpenSubRefresh_VTable;
    strncpy(task->task.name, "OpenSubRefresh", sizeof(task->task.name) - 1);
    task->app = app;
    task->target_uid = target_uid;
    task->interface_id = interface_id;
    task->type = type;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
App_OpenSubInterface(
    struct App* app,
    int target_uid,
    int interface_id,
    int type)
{
    assert(app);
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "if-opensub: mount iface=%d under uid=0x%08x (%d<<16|%d) type=%d\n",
            interface_id,
            (unsigned)target_uid,
            (target_uid >> 16) & 0xffff,
            target_uid & 0xffff,
            type);
    /* Type 0 is the modal mount. Recorded on the OPEN and not on the close --
     * App_CloseSubInterface reaches app_enqueue_open_sub directly with
     * iface -1, so an unmount cannot erase the frame's answer. */
    if( type == 0 && interface_id > 0 )
        app->modal_host_uid = target_uid;
    app_enqueue_open_sub(app, target_uid, interface_id, type);
}

void
App_CloseSubInterface(
    struct App* app,
    int target_uid)
{
    assert(app);
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "if-closesub: unmount uid=0x%08x (%d<<16|%d)\n",
            (unsigned)target_uid,
            (target_uid >> 16) & 0xffff,
            target_uid & 0xffff);
    /* iface_id <= 0 makes the mount task just clear the slot. */
    app_enqueue_open_sub(app, target_uid, -1, 0);
}

void
App_MoveSubInterface(
    struct App* app,
    int source_uid,
    int dest_uid)
{
    int idx;
    int group_id;
    int type;

    assert(app);
    if( !app->tree || source_uid < 0 || dest_uid < 0 )
        return;
    idx = UITree_InterfaceParentFind(app->tree, source_uid);
    if( idx < 0 )
        return;
    group_id = app->tree->interface_parents[idx].group_id;
    type = app->tree->interface_parents[idx].type;
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "if-movesub: group=%d type=%d src=0x%08x dest=0x%08x\n",
            group_id,
            type,
            (unsigned)source_uid,
            (unsigned)dest_uid);
    App_CloseSubInterface(app, source_uid);
    App_OpenSubInterface(app, dest_uid, group_id, type);
}

