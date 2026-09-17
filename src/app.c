/*
 * How an App is assembled: construction, teardown, and the globals the frame
 * loop shares with main.c.
 *
 * The composition root of the App layer. The layer is this file plus every
 * unit under src/app/; app/app_internal.h is its map. Nothing outside the
 * layer may include that header or name `struct App`.
 */

#include "app/app_internal.h"

#include "perf_audit.h"
#include "plugin/porcelain/torirs_porcelain.h"

#if defined(TORIRS_PLATFORM_WEB)
#include "ui/torirs_chrome_exec_web.h"

#include <emscripten.h>

/*
 * Ask the page to open the command-panel tab (`[editor:boot] panel=tab`).
 *
 * EM_JS rather than EM_ASM, matching main.c: EM_ASM is rejected in `-std=c*`
 * modes and this file is C11. The page owns the channel -- this only asks, and
 * a page that defines no hook (or a browser that blocks the popup) leaves the
 * editor running without its chrome rather than failing the boot.
 */
// clang-format off
EM_JS(
    void,
    web_editor_open_panel_tab,
    (void),
    {
        if( typeof window.torirsOpenPanelTab === 'function' )
            window.torirsOpenPanelTab();
        else
            console.warn('[torirs] panel=tab, but the page defines no torirsOpenPanelTab()');
    });
// clang-format on
#endif

/* Private to this unit, declared up front so definition order is free. */
static void
app_prefs_flush(struct App* app);

/* A/B probe for the world painter: 0 = default (bucket, or world3d under
 * TORIRS_PAINTER_W3D=1), 1 = force world3d, 2 = force bucket. Set by the
 * TORIRS_PAINTER_ALT same-frame BMP pair in main.c. */
int g_torirs_painter_force = 0;
/* Ordinal of the frame being drawn, bumped once per main-loop iteration in
 * main.c. Anything whose phase must be a function of the frame rather than
 * of the clock reads it -- TORIRS_WEDGE_CAM_PATH does. Distinct from main's
 * frame_count, which only advances when TORIRS_MAX_FRAMES bounded the run;
 * a camera path has to keep moving in an unbounded session too. */
long g_torirs_frame_no = 0;
/* TORIRS_MAX_FRAMES, mirrored here from main.c so the logic pacer can see it.
 * Zero in an ordinary session. See the pacer for what it changes. */
long g_torirs_max_frames = 0;
/*
 * Construction and teardown.
 *
 * Included into app.c rather than compiled on its own. App_Init names nearly
 * every field App has, in the order they have to come up in, and that order IS
 * the contract -- which is why it is one function and not a dozen. It shrinks
 * as each field family gains a struct with its own Init/Free, not by being
 * cut into pieces that hide the order.
 */

void
App_Init(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(cfg);
    memset(app, 0, sizeof(*app));
    app->cfg = *cfg;
    /* 0 is a legal component uid, so "nothing has mounted a modal yet" needs a
     * value of its own. */
    app->modal_host_uid = -1;

    /*
     * Subsystems that own their own zero state, said out loud.
     *
     * The memset above already leaves every one of these correct, and they are
     * here anyway: a subsystem whose reset is a memset today may not be one
     * tomorrow, and a constructor that never named it would not notice. This
     * is what replaces the dozens of individual field initialisations these
     * structs took with them. @see tools/appc_map.py fields.
     */
    NetLinkWatch_Reset(&app->net_link);
    RS_ClientScriptQueue_Reset(&app->pending_clientscripts);
    FrameTimeRing_Reset(&app->dbg_frame_times);
    MapEditorGhost_Reset(&app->map_ghost);
    EditorPreviewCamera_Reset(&app->preview_camera);

    /* Before any subsystem: RS_CS2Host_Init wants its script ids, and the
     * boot font loads want theirs. Local-file parse only — no cache IO, so it
     * does not need the task runtime that phase 1 builds below. */
    RevConfigRefs_Init(&app->revconfig_refs);
    RevConfigRefs_LoadSources(
        &app->revconfig_refs,
        app->cfg.revconfig_ui_ini,
        app->cfg.revconfig_cache_ini,
        app->cfg.revconfig_inline_ini);
    /* Same three sources, same load order, read for the other two things a
     * revision profile states about behaviour rather than about ids: the
     * feature table (consumed a few hundred lines below, where the era is
     * resolved) and the camera policy. */
    /* Same three sources again, for the sentences a login failure needs. Here
     * rather than in the builder because the builder's items are torn down
     * after the bake and a rejection can arrive at any time. */
    RS_LoginReplies_Init(&app->login_replies);
    RS_LoginReplies_LoadSources(
        &app->login_replies,
        app->cfg.revconfig_ui_ini,
        app->cfg.revconfig_cache_ini,
        app->cfg.revconfig_inline_ini);
    /* And the loading screen's own work list, from the same three sources.
     * What this revision fetches before it can show a title, in its order,
     * with the percentage and the sentence each step carries. */
    RS_Preload_Init(&app->preload);
    RS_Preload_LoadSources(
        &app->preload,
        app->cfg.revconfig_ui_ini,
        app->cfg.revconfig_cache_ini,
        app->cfg.revconfig_inline_ini);
    RevConfigProfile_Init(&app->revconfig_profile);
    RevConfigProfile_LoadSources(
        &app->revconfig_profile,
        app->cfg.revconfig_ui_ini,
        app->cfg.revconfig_cache_ini,
        app->cfg.revconfig_inline_ini);
    /* And the third: the semantic roles. After the refs and not beside them,
     * because a matcher spelled `iface(<name>)` is resolved through the table
     * loaded just above. */
    memset(&app->ui_roles, 0, sizeof(app->ui_roles));
    UITreeRoleLoad_LoadSources(
        &app->ui_roles,
        &app->revconfig_refs,
        app->cfg.revconfig_ui_ini,
        app->cfg.revconfig_cache_ini,
        app->cfg.revconfig_inline_ini);

    ToriDraw_Init();

    /* Phase 1: task runtime + disk. The runner owns the async pipeline every
     * other phase loads through. */
    app->runner.io = ToriRS_IO_New();
    app->runner.queue = ToriRS_TaskQueue_New();
    app->runner.px = Platform_IO_New();
    assert(app->runner.px != NULL);
    /*
     * The asset pipeline runs its tasks in parallel.
     *
     * Nothing downstream of a cache read cares which of a region's models,
     * textures or sprites lands first -- they are independent reads with a
     * common consumer -- and making each wait for the last is what put a
     * network round trip between every group of the couple of thousand a
     * login streams. The game-action queue below stays strictly ordered,
     * because the server chose the order of the packets that fill it.
     *
     * TORIRS_SERIAL_ASSETS puts this queue back in single file. It is the A/B
     * the change was measured with, and the first thing to try if a load ever
     * looks order-dependent when it should not be.
     */
    app->runner.parallel = getenv("TORIRS_SERIAL_ASSETS") == NULL;

    /* Serial game-action pipeline: own queue + io slots, SHARED platform
     * pump (there is exactly one IO backend). */
    app->exec_runner.io = ToriRS_IO_New();
    app->exec_runner.queue = ToriRS_TaskQueue_New();
    app->exec_runner.px = app->runner.px;

#if defined(TORIRS_PLATFORM_WEB)
    /*
     * A browser opens no cache here, and that is the architecture rather than a
     * gap.
     *
     * The platform IO executor answers cache reads on this lane
     * (platform/platform_web_io.js): it reads the record database the cache
     * producers fill and decodes through the format's own C API. Nothing above
     * the queue needs a disk handle, so leaving both NULL is what keeps that
     * honest -- anything that tried to read locally would fault rather than
     * quietly answer out of an empty cache.
     */
    (void)cfg->cache_dir;
#else
    if( cfg->cache_kind == APP_CACHE_DAT1 && cfg->cache_on_demand )
    {
        /* The cache is the server's. Nothing local is opened, so a missing or
         * stale main_file_cache.* on this machine cannot affect this boot --
         * which is the whole reason to run this way against LostCity. The IO
         * owns the client, the same way it owns the JS5 one. */
        char const* host =
            cfg->connect_target && cfg->connect_target[0] ? cfg->connect_target : "localhost";
        /* `dir=` under an ondemand source is where the stream is written
         * down, not where it is read from -- the same shape dat2 has had all
         * along with its sparse cache. Absent means stream everything, every
         * boot, which is what this world did before. */
        int enabled = PlatformXIO_Dat1OnDemandEnable(
            app->runner.px, host, cfg->connect_port, cfg->web_port, cfg->cache_dir);
        if( enabled != 0 )
        {
            /* Once more before giving up: the first attempt rides a cold
             * Wi-Fi association or a server mid-repack often enough on the
             * phone that a single 10s HTTP miss is not yet "the server is
             * down". A failed enable leaves the IO with no on-demand source,
             * so the second call starts clean. */
            struct timespec pause = { 1, 0 };
            nanosleep(&pause, NULL);
            enabled = PlatformXIO_Dat1OnDemandEnable(
                app->runner.px, host, cfg->connect_port, cfg->web_port, cfg->cache_dir);
        }
        if( enabled != 0 )
        {
            /* An unreachable cache server is a RUNTIME state -- the server is
             * down, the host moved with a DHCP lease, the phone is on the
             * wrong network -- not a caller bug, so it must fail loudly on
             * every build flavor. This used to be a TORIRS_LOG plus an
             * assert: the release client printed nothing, limped on with no
             * cache provider at all, and SIGSEGV'd in the first buildcache
             * hmap lookup -- which, on a device whose profile armed --webgl1,
             * read as a GLES2 renderer crash. */
            char message[512];
            int used;

            /* Composed rather than printed in pieces: on Android this same
             * string is what the boot menu shows, and a diagnosis split across
             * two calls arrives there as half of one. */
            used = snprintf(
                message,
                sizeof(message),
                "[cache:boot] source=ondemand, but %s is not serving a cache "
                "(game port %d, web port %d); this profile streams its cache from "
                "there and cannot boot without it.",
                host,
                cfg->connect_port > 0 ? cfg->connect_port : 43594,
                cfg->web_port > 0 ? cfg->web_port : 80);
            if( used < 0 || used >= (int)sizeof(message) )
                used = (int)sizeof(message) - 1;
#if defined(TORIRS_PLATFORM_ANDROID)
            /* The trap this message exists for: a manifest authored on the
             * desktop says host=localhost, and on the phone localhost IS the
             * phone. The gear in the boot menu edits the profile's host; the
             * server machine's bare hostname resolves through the router's
             * DNS (`.local` does not on old Android). */
            if( strcmp(host, "localhost") == 0 || strcmp(host, "127.0.0.1") == 0 )
                snprintf(
                    message + used,
                    sizeof(message) - (size_t)used,
                    "\n\nOn this device 'localhost' is the phone itself. Tap the gear and set "
                    "this profile's host to the server machine's LAN name.");
#endif
            app_boot_refuse(message);
        }
        app->cache_on_demand = 1;
    }
    else if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_disk = RSCache_Dat1DiskNewFromDirectory(cfg->cache_dir);
        if( !app->dat1_disk )
        {
            /* Same rule as the ondemand refusal above: a missing cache is a
             * deployment state, and the message is the whole diagnosis. */
            char message[512];

            snprintf(
                message,
                sizeof(message),
                "no dat1 cache at %s (expected main_file_cache.dat; pass --dat2 for a js5 cache)",
                cfg->cache_dir);
            app_boot_refuse(message);
        }
        Platform_IO_InitDat1Disk(app->runner.px, app->dat1_disk);
        /* No xtea step: dat1 archives are not encrypted. */
    }
    else
    {
        /* Lazy: the client reads maybe two thirds of the tables a cache ships,
         * and decoding the rest at open cost several MB it never looked at. */
        app->dat2_disk = RSCache_Dat2DiskNewFromDirectoryLazyTables(cfg->cache_dir);
        if( !app->dat2_disk )
        {
            char message[512];

            snprintf(
                message,
                sizeof(message),
                "no dat2 cache at %s (expected main_file_cache.dat2; pass --dat1 for a "
                "317-era cache)",
                cfg->cache_dir);
            app_boot_refuse(message);
        }
        /* Map archives may be xtea-encrypted (OldSchool below 237; RS2 dat2
         * from 414). Keys load into the rscache global table the disk layer
         * consults on archive fetch — only when the identity gate says so. */
        {
            struct RSCache probe = RSCache_ProfileForIdentity(
                cfg->cache_game, cfg->cache_epoch, cfg->cache_revision, cfg->cache_quirks);
            if( RSCache_MapLocsEncrypted(&probe) )
            {
                char xtea_path[1024];
                snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cfg->cache_dir);
                if( RSCache_XteaConfigLoadKeys(xtea_path) <= 0 )
                    TORIRS_ERR("app: no xtea keys at %s (world maps may fail)\n", xtea_path);
            }
            else
            {
                TORIRS_LOG("app: map archives are unencrypted at this revision\n");
            }
        }
        Platform_IO_InitDat2Disk(app->runner.px, app->dat2_disk);
    }
#endif
    /* What the boot manifest said about the cache. A local backend ignores it
     * (it has the disk); a remote one needs it to know what to open. */
    Platform_IO_InitCacheId(
        app->runner.px,
        cfg->cache_epoch,
        cfg->cache_game,
        cfg->cache_revision,
        cfg->cache_quirks,
        cfg->cache_dir);
    Platform_IO_InitConfigPath(app->runner.px, cfg->config_dir);
    char const* plugin_script_root = getenv("TORIRS_SCRIPT_DIR");
    Platform_IO_InitScriptPath(
        app->runner.px,
        plugin_script_root && *plugin_script_root ? plugin_script_root : cfg->script_dir);
    /* After the script path, because it is the fallback FOR it: a stored file
     * is looked for under script_dir first and asked of this server second. */
    Platform_IO_InitIoServer(app->runner.px, cfg->io_host, cfg->io_port);

    /* Phase 2: asset pipeline (provider is a view over the build cache). */
    if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_bc = dat1_buildcache_new();
        app->provider = dat1_buildcache_as_provider(app->dat1_bc);
    }
    else
    {
        app->dat2_bc = dat2_buildcache_new();
        app->provider = dat2_buildcache_as_provider(app->dat2_bc);
    }
    /* Every loader that fans out needs somewhere to put its siblings, and the
     * parallel asset queue is that place -- see CacheProvider::asset_queue. */
    if( app->provider )
        CacheProvider_SetAssetQueue(app->provider, app->runner.queue);
    app_provider_set_cache_profile(app, cfg);
    if( !TorirsModelInstCache_Init(&app->model_inst_cache) )
        assert(0 && "model_inst_cache init");

    /* Phase 3: renderer scene + id bridge (bridge needs scene + provider). */
    /* The scene carries a depth buffer for the models that ask for one
     * (TORIDRAW_MODEL_FLAG_ZBUFFER — see app_npc_wants_zbuffer). It is
     * allocated lazily on the first such model, so a session that never draws
     * one never pays for it. The painter's sort remains what everything else
     * uses: it is what OSRS content was authored against and is right for it.
     * Imported content — models built for a z-buffered client, whose parts
     * genuinely interpenetrate — has no correct face order to find, and this is
     * how those get the answer their geometry assumes. */
    /* SMALL selects the CSR face sorter, not a capacity: the tier still sets
     * vertex/face limits. The dense sorter's bucket table costs
     * depth_levels * depth_stride * 2 bytes (16MB at DEPTH_16K) and ran at
     * under 1%% occupancy; the CSR form scales with max_faces (~1MB) and its
     * per-model passes are windowed to the model's depth span, so the draw
     * cost stays proportional to the model like the dense path. */
    app->scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    assert(app->scene);
    /* Once each, not once a frame: the outline/shadow LRU inside a renderer is
     * only worth having if it survives the frame that filled it. */
    app->soft = ToriRS_Soft3D_New();
    app->soft_chrome = ToriRS_Soft3D_New();
    UITreeSceneBridge_Init(&app->bridge, app->scene, app->provider);
    /* The seq the design/local-player previews are posed at. Stated here, once,
     * for both the boot bake and the runtime interface mount — they share the
     * bridge and nothing else. -1 when the profile does not name one, which
     * leaves the preview in its bind pose instead of another cache's animation. */
    app->bridge.player_idle_seq = RevConfigRefs_Get(&app->revconfig_refs, "seq", "human_readyanim");

    /* Phase 4: game state (host needs tree + provider + invs + varps, then
     * the bridge for icon rasterization). */
    app->tree = UITree_New(256);
    assert(app->tree);
    /* Which side of a minimap/compass mask is the window is a property of the
     * era's art, not of the widget — see UITree.mask_keep_opaque. RS2 (634)
     * ships a stencil, OldSchool a corner cover. */
    app->tree->mask_keep_opaque = RSCache_IsRs2Dat2(CacheProvider_Profile(app->provider)) ? 1 : 0;
    InvManager_Init(&app->invs);
    VarPManager_Init(&app->varps);
    VarCManager_Init(&app->varcs);
    LootStore_Init(&app->loot);

    /* Phase 4b: world sim + builder. The World is a pure simulation that
     * references scene elements/assets by integer id; the builder keeps it in
     * sync with the shared scene from cache data. */
    app->world = World_New();
    assert(app->world);
    World_SetScene(app->world, app->scene);
    app->world_builder = WorldBuilder_New(app->world, app->provider, app->scene, &app->varps);
    assert(app->world_builder);
    /* Multi-world substrate (SAILING_PLAN C0): the root view borrows the pair
     * above; boat views register owned pairs later. The cursor starts — and
     * after every SERVER_TICK_END returns — on the root. */
    WorldviewRegistry_Init(&app->worldviews);
    WorldviewRegistry_RegisterRoot(&app->worldviews, app->world, app->world_builder);
    app->active_world = WORLDVIEW_ROOT;
    /* World entities (sailing, SAILING_PLAN C1): no boats until
     * WORLDENTITY_INFO spawns one; the config table fills at boot. */
    Wevs_Init(&app->wevs);
    app->sailing_at_helm_varbit =
        RevConfigRefs_Get(&app->revconfig_refs, "varbit", "sailing_player_at_helm");
    app->sailing_captain_role_varbit =
        RevConfigRefs_Get(&app->revconfig_refs, "varbit", "sailing_captain_role");
    for( int slot = 0; slot < 5; ++slot )
    {
        char key[48];
        snprintf(key, sizeof(key), "sailing_crew_duty_%d", slot + 1);
        app->sailing_crew_duty_varbit[slot] =
            RevConfigRefs_Get(&app->revconfig_refs, "varbit", key);
        snprintf(key, sizeof(key), "sailing_crew_roster_%d", slot + 1);
        app->sailing_crew_roster_varbit[slot] =
            RevConfigRefs_Get(&app->revconfig_refs, "varbit", key);
    }
    app->sailing_crew_category =
        RevConfigRefs_Get(&app->revconfig_refs, "category", "sailing_crew");
    app->sailing_arrow_model[0] =
        RevConfigRefs_Get(&app->revconfig_refs, "model", "sailing_heading_hover");
    app->sailing_arrow_model[1] =
        RevConfigRefs_Get(&app->revconfig_refs, "model", "sailing_heading_selected");
    app->sailing_arrow_element[0] = app->sailing_arrow_element[1] = -1;
    WevConfigTable_Init(&app->wev_configs);
    /* The dynamic-registration pass puts every entity floating in this world
     * into its painter as a transient pseudo-loc (SAILING_PLAN C3). Boat worlds
     * get the same hook at spawn, so nesting registers itself. */
    World_SetWorldEntityRegisterFn(app->world, app_wev_register_pseudo_locs, app);
    /* The root's rebuild sweep must not free the hulls' flatten-bake elements
     * (root-dynamic, claimed by no root entity pool) — see the ROOT branch of
     * app_wev_claim_deck_actors. */
    World_SetForeignDynamicClaimFn(app->world, app_wev_claim_deck_actors, app);
    /* Facing across the gunwale computes in the root frame — see
     * app_wev_actor_root_frame. */
    World_SetActorRootFrameFn(app->world, app_wev_actor_root_frame, app);
    World_SetLocalPlaneFn(app->world, app_world_local_plane, app);
    app->painter_buffer = painter_buffer_new();
    assert(app->painter_buffer);
    /* v1 GameRunescape camera defaults; repositioned on world load complete. */
    /* Both knobs are populated; projection_mode picks. See graphics/projection.h. */
    app->world_camera.projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
    app->world_camera.projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT;
    app->world_camera.fov_rpi2048 = TORIDRAW_PROJECTION_FOV_DEFAULT;
    /* 50 also load-bearing for the raster, not just for what gets drawn: the
     * near plane is what keeps projected coordinates inside the kernels' 16.16
     * edge representation (+/-32,768 px). Lowering it moves the largest models
     * into overflow range -- see the note at the near clip in
     * graphics/projection.u.c before changing it or TORIRS_NEAR_PLANE. */
    app->world_camera.near_plane_z =
        (getenv("TORIRS_NEAR_PLANE") ? atoi(getenv("TORIRS_NEAR_PLANE")) : 50);
    app->world_camera.pitch = 148;
    app->world_camera_pos.z = -800;
    /* The flattest the profile allows: the reference's orbitCameraPitch
     * default IS its own lower bound, so a lane that states a different range
     * boots at the bottom of the range it stated rather than at a 128 that no
     * longer means anything there. */
    app->orbit.pitch = app->revconfig_profile.camera.pitch_flattest;
    app->orbit.yaw = 0;
    /* Where the profile says this camera sits before anyone touches it.
     * `[camera] rest=`, and the band is around it, not the other way up. */
    app->world_cam_zoom = app->revconfig_profile.camera.rest;
    app->world_hover_tile_x = -1;
    app->world_hover_tile_z = -1;
    app->world_hover_tile_level = 0;
    app->world_hover_view = 0;
    /* -2, not -1: -1 is "no tile", a state the refreshers must still be run
     * for once, and seeding them equal to it would skip that first run. */
    app->highlight_last_hover_coord = -2;
    app->highlight_last_route = -2;
    app->highlight_last_dest_coord = -2;
    app->highlight_last_mouseover = -2;
    app->ground_items_dirty.count = 0;
    app->ground_items_dirty.refresh_all = 0;
    app->ground_items_settings_varp[0] = -1;
    app->ground_items_settings_varp[1] = -1;
    app->ground_items_settings_seen[0] = 0;
    app->ground_items_settings_seen[1] = 0;
    app->ground_items_aux_seen[0] = 0;
    app->ground_items_aux_seen[1] = 0;
    app->world_map_scene_id = -1;
    app->worldmap.render = RS_WorldMapRender_New();
    app->worldmap.overview_scene_id = 0;
    app->worldmap.overview_area_id = -1;
    MinimapView_Reset(&app->minimap);
    /*
     * The plugin host.
     *
     * Built here so a plugin's config is readable the moment anything asks,
     * but NOT started: PluginHost_Start runs after the saved settings have
     * been applied, or every plugin would spend its first frames on defaults
     * and then jump when the ini arrived. `[ui:boot] plugins=0` in the manifest
     * switches the whole layer off, and TORIRS_PLUGINS outranks the manifest in
     * both directions -- which is what the headless parity harnesses use to
     * prove a change is theirs and not a plugin's.
     */
    {
        char const* plugins_env = getenv("TORIRS_PLUGINS");
        int want_plugins = cfg->plugins >= 0;
        if( plugins_env )
            want_plugins = atoi(plugins_env) != 0;
        if( want_plugins )
        {
            struct ToriRS_PluginEngine engine = app_plugin_engine(app);
            app->plugin_panel = -1;
            app->plugin_panel_built_for = 0;
            /* The client is what LINKS Porcelain; the host only carries the
             * pointer, so the host's own test binaries stay free of it. */
            PluginHost_SetPorcelain(ToriRS_PorcelainApiTable());
            app->plugins = PluginHost_New(&engine);
            app->plugin_prefs_path = PluginPrefs_Path();
            PluginRegistry_RegisterAll(app->plugins);
        }
    }
    app->rebuild_zone_x = -1;
    app->rebuild_zone_z = -1;
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    app->proj_src_tile_level = 0;
    /* Element id 0 is valid, so free entity-spotanim slots must be -1. */
    for( size_t i = 0; i < sizeof(app->entity_spotanims) / sizeof(app->entity_spotanims[0]); i++ )
        app->entity_spotanims[i].body_element_id = -1;

    RS_EntitySync_Init(&app->esync);
    RS_Audio_Init(&app->audio);
    ToriRS_AudioQueue_Reset(&app->audio_out);
    UIInvDrag_Reset(&app->inv_drag);
    app->reboot_timer = 0;
    app->multiway = 0;
    app->minimap_state = 0;
    memset(&app->welcome, 0, sizeof(app->welcome));
    RS_Social_Init(&app->social);
    /* Reference reset path: idkDesignGender = male, then validateIdkDesign().
     * The kit scan itself waits for the idk configs, so the clientCode tick
     * resolves the parts the first time the preview asks for a rebuild. */
    RS_IdkDesign_Init(&app->idk_design);
    RS_Chat_Init(&app->chat, "Player");
    RS_Title_Init(&app->title);
    /* No hardcoded welcome line: the server sends the real "Welcome to
     * RuneScape." MESSAGE_GAME packet on login (reference has no client-side
     * welcome message; its only "Welcome to RuneScape" is the login title). */
    app->chat_source.line_at = app_chat_line_at;
    app->chat_source.user = app;
    RS_CS2Host_Init(
        &app->host,
        app->tree,
        app->provider,
        &app->invs,
        &app->varps,
        &app->varcs,
        &app->revconfig_refs);
    app->host.loot = &app->loot;
    app->host.events_override_for_component = app_cs2_events_override_for_component;
    app->host.events_user = app;
    app->host.script_callback = app_script_callback;
    app->host.script_callback_user = app;
    app->host.loc_at_coord = app_cs2_loc_at_coord;
    app->host.coord_in_scene = app_cs2_coord_in_scene;
    app->host.player_route = app_cs2_player_route;
    app->host.npc_by_uid = app_cs2_npc_by_uid;
    app->host.player_slot_by_name = app_cs2_player_slot_by_name;
    app->host.worldentity_config_name = app_cs2_worldentity_config_name;
    app->host.objs_on_coord = app_cs2_objs_on_coord;
    app->host.world_user = app;
    /*
     * State the starting gain once, so a backend is never left guessing at a
     * volume the game already has an opinion about -- the mixer's own buses
     * come up wide open, which is not what the client wants said on its behalf.
     *
     * Read from the option store rather than restating a default here: that is
     * what interface 116 shows, what the preferences file is diffed against,
     * and (RS_CS2Host_OptionDefault) what makes a fresh client boot muted. A
     * second opinion in this file is a client that is audibly one thing while
     * its settings panel says another.
     *
     * Setting the master pushes all three buses, each scaled by it, so this is
     * the whole boot state in one call. A saved volume lands a tick later, when
     * the boot task's preferences restore reaches the tick's snapshot.
     */
    RS_Audio_SetMasterVolume(
        &app->audio,
        (RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MASTER_VOLUME) *
             TORIRS_AUDIO_VOLUME_MAX +
         50) /
            100,
        &app->audio_out);
    /* Publish the boot canvas through the one setter so the host's viewport
     * copy starts out agreeing with the layout root. main.c may already have
     * moved the root (TORIRS_ROOT_SIZE, which must be applied before App_Init
     * because the open path lays out immediately); without this the host kept
     * its own 765x503 and VIEWPORT_GETEFFECTIVESIZE lied for the whole run. */
    App_SetCanvasSize(app, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    /* The skills tab reads levels and xp through STAT / STAT_BASE / STAT_XP,
     * which need somewhere to read them from. */
    RS_CS2Host_SetStats(&app->host, &app->stats);
    /* The friends and ignore panels read every row they draw through the
     * FRIEND_* / IGNORE_* opcodes, and clientscript 681 writes the chat filter
     * modes through CHAT_SETFILTER — the same three ints the IF1 privacy bar
     * cycles, handed over by pointer so there is only ever one copy. */
    RS_CS2Host_SetSocial(
        &app->host, &app->social, app->slots.chat_filter_mode, app->social.node_id);
    /* The chatbox at a cache revision is 500 text components the cache's own
     * scripts fill, and they read every line back through the CHAT_GETHISTORY*
     * opcodes. This is what those read. */
    RS_CS2Host_SetChat(&app->host, &app->chat);
    RS_CS2Host_SetBridge(&app->host, &app->bridge);
    /* Close the reactive loop: a varp update from the *server* flags a
     * var-transmit re-dispatch on the host, so interfaces react to value changes
     * and not only to unhide. See app_varp_server_update for why script-side
     * writes and varcs stay out of the transmit ring. ChangeFn remorphs
     * multilocs for both optimistic and server value changes. */
    VarPManager_SetChangeCallback(&app->varps, app_varp_change, app);
    VarPManager_SetServerUpdateCallback(&app->varps, app_varp_server_update, app);
    RS_PlayerStats_Init(&app->stats);
    RS_CS1Host_Init(&app->cs1_host, app->tree, app->provider, &app->invs, &app->varps, &app->stats);

    /* Phase 5: frame state. */
    UITree_EmitBufferInit(&app->emit);
    UITree_HostInit(&app->ui_host);
    app->ui_host.user = app;
    app->ui_host.request = app_host_request;
    InvManager_SetChangeCallback(&app->invs, app_inv_ui_host_change, app);
    UIInteraction_Init(&app->interact);
    UIHoverText_Reset(&app->hover_text);
    app_debug_overlay_init(app);
    app->hover_com_id = -1;
    app->clicked_com_id = -1;
    app->need_redraw = 1;

    /*
     * Client-behaviour era. Resolved unconditionally — an offline boot still
     * clicks locs, so it still needs an approach model.
     *
     * Precedence: manifest > env > revconfig `[features]` > derived from the
     * cache. The revconfig sits under the manifest and not over it because the
     * two answer different questions. `[features]` is a statement about the
     * REVISION, shared by every world that boots that profile; a manifest's
     * `[features:boot]` is a statement about one WORLD, and some of what it has
     * to say is not derivable from the cache at all — era=server_routed is a
     * property of the server, and manifest_osrs233xrsps.ini states it over a
     * rev-233 cache whose own profile would say osrs.
     */
    {
        struct RevConfigFeaturesItem const* rc_features = &app->revconfig_profile.features;
        char const* era_name = cfg->features_era;

        /*
         * What the scripts are told this client is. Precedence, most specific
         * first: TORIRS_CLIENTTYPE / TORIRS_ON_MOBILE, the manifest's
         * `[ui:boot]` (which a profile's `[override:ui:boot]` writes), the
         * revconfig `[features]`, then the platform: a phone is the mobile
         * client (7, on_mobile -- the pair the cache's own script 1972 reads
         * as the mobile layout), everything else the desktop enhanced one.
         */
        {
#if defined(TORIRS_PLATFORM_ANDROID)
            int clienttype = 7;
            int on_mobile = 1;
#else
            int clienttype = 10;
            int on_mobile = 0;
#endif
            char const* env_clienttype = getenv("TORIRS_CLIENTTYPE");
            char const* env_on_mobile = getenv("TORIRS_ON_MOBILE");
            if( rc_features->clienttype >= 0 )
                clienttype = rc_features->clienttype;
            if( rc_features->on_mobile >= 0 )
                on_mobile = rc_features->on_mobile;
            if( cfg->clienttype > 0 )
                clienttype = cfg->clienttype;
            if( cfg->on_mobile )
                on_mobile = cfg->on_mobile > 0;
            if( env_clienttype && env_clienttype[0] )
                clienttype = atoi(env_clienttype);
            if( env_on_mobile && env_on_mobile[0] )
                on_mobile = atoi(env_on_mobile) != 0;
            CS2VM2_SetClientIdentity(clienttype, on_mobile);
            TORIRS_REPORT(
                "app: clientscript identity: clienttype %d, on_mobile %d\n", clienttype, on_mobile);
            /*
             * The touch UI/input policy, resolved HERE and not in the frame
             * loop.
             *
             * It is what core.capability("touch") answers, and the frame loop
             * runs after PluginHost_Start: a plugin asking at on_start -- the
             * only place a key declaration can be made -- was told false on
             * every lane, measured, and then told true at frame 60 where
             * nothing was listening. A statement about the BOOT belongs at
             * boot.
             *
             * It follows the client IDENTITY, not just the platform: a run
             * that has told the cache's scripts it is the mobile client
             * (clienttype 7, or on_mobile) is running the phone's interface,
             * and a `touch` capability that answered false there made every
             * rule written against that lane measure nothing.
             *
             * TORIRS_TOUCH_UI is read as a NUMBER and outranks all of it, so
             * `=0` can take a mobile-identity run back to the desktop popup;
             * as a presence test it could only ever turn the policy on, which
             * left no way to say no.
             */
            {
                char const* const env_touch = torirs_env_touch_ui();
                app->touch_ui = (clienttype == 7 || on_mobile) ? 1 : 0;
#if defined(TORIRS_PLATFORM_ANDROID)
                app->touch_ui = 1;
#endif
                if( env_touch && env_touch[0] )
                    app->touch_ui = atoi(env_touch) != 0;
                app->interact.touch_scroll = app->touch_ui;
            }
        }
        if( !era_name || !era_name[0] )
            era_name = getenv("TORIRS_FEATURES_ERA");
        if( !era_name || !era_name[0] )
        {
            era_name = rc_features->era;
            if( era_name[0] && !ToriRS_Features_ByName(era_name) )
                TORIRS_LOG(
                    "app: [features] era must be lostcity|osrs|server_routed, got '%s'\n",
                    era_name);
        }
        app->features = era_name && era_name[0] ? ToriRS_Features_ByName(era_name) : NULL;
        if( era_name && era_name[0] && !app->features )
            TORIRS_ERR("app: unknown features era '%s', deriving from cache\n", era_name);
        /* Audio behaviour is era-dependent too: monophonic effects are a 2004
         * client property, not a general one. */
        if( !app->features )
            app->features =
                ToriRS_Features_ForCache(cfg->cache_game, cfg->cache_epoch, cfg->cache_revision);
        assert(app->features);

        /*
         * Per-item overrides on top of the era.
         *
         * The era getters hand back shared static singletons, so an override
         * has to be written into the app's own copy — otherwise one manifest
         * key would move the table every other boot in the process reads. This
         * is the copy app->features points at from here on; the singleton stays
         * the pristine statement of what the era is.
         */
        app->features_storage = *app->features;
        app->features = &app->features_storage;
        /* The era is now known, so the two Attack options can take their boot
         * value; see app_attack_options_reset. */
        app_attack_options_reset(app);
        {
            char const* env = getenv("TORIRS_GROUND_CLICK_NEAREST");
            int model = -1;
            if( rc_features->ground_click_nearest[0] )
            {
                model = ToriRS_Features_NearestModelByName(rc_features->ground_click_nearest);
                if( model < 0 )
                    TORIRS_LOG(
                        "app: [features] ground_click_nearest must be "
                        "ring3|box10_rect|none, got '%s'\n",
                        rc_features->ground_click_nearest);
            }
            if( cfg->features_ground_click_nearest_set )
                model = cfg->features_ground_click_nearest;
            if( env && env[0] )
            {
                int from_env = ToriRS_Features_NearestModelByName(env);
                if( from_env < 0 )
                    TORIRS_LOG(
                        "app: TORIRS_GROUND_CLICK_NEAREST must be "
                        "ring3|box10_rect|none, got '%s'\n",
                        env);
                else
                    model = from_env;
            }
            if( model >= 0 )
                app->features_storage.ground_click_nearest_model = model;
        }
        /*
         * The two permissive ground-click extensions. Every era table leaves
         * them off — the client is deob-exact unless a boot asks otherwise —
         * so this is the only place either can be turned on.
         */
        if( rc_features->ground_click_unbounded >= 0 )
            app->features_storage.ground_click_nearest_unbounded =
                rc_features->ground_click_unbounded;
        if( rc_features->ground_click_offmap >= 0 )
            app->features_storage.ground_click_offmap_nearest = rc_features->ground_click_offmap;
        if( cfg->features_ground_click_unbounded )
            app->features_storage.ground_click_nearest_unbounded = 1;
        if( cfg->features_ground_click_offmap )
            app->features_storage.ground_click_offmap_nearest = 1;
        {
            char const* env = getenv("TORIRS_GROUND_CLICK_UNBOUNDED");
            if( env && env[0] )
                app->features_storage.ground_click_nearest_unbounded = env[0] != '0';
            env = getenv("TORIRS_GROUND_CLICK_OFFMAP");
            if( env && env[0] )
                app->features_storage.ground_click_offmap_nearest = env[0] != '0';
        }
        if( rc_features->painter_draw_distance > 0 )
        {
            if( rc_features->painter_draw_distance < TORIRS_PAINTER_DRAW_DISTANCE_MIN ||
                rc_features->painter_draw_distance > TORIRS_PAINTER_DRAW_DISTANCE_MAX )
                TORIRS_LOG(
                    "app: [features] painter_draw_distance must be %d..%d, got %d\n",
                    TORIRS_PAINTER_DRAW_DISTANCE_MIN,
                    TORIRS_PAINTER_DRAW_DISTANCE_MAX,
                    rc_features->painter_draw_distance);
            else
                app->features_storage.painter_draw_distance = rc_features->painter_draw_distance;
        }
        if( cfg->features_painter_draw_distance_set )
            app->features_storage.painter_draw_distance = cfg->features_painter_draw_distance;

        /*
         * The mover model, on top of the era.
         *
         * It needs an override where the other era fields do not, because
         * ToriRS_Features_ForCache decides the era from cache *lineage* and has
         * exactly one table for everything that is not dat2+oldschool -- so a
         * rev-377 or rev-634 boot lands on the lostcity table and inherits its
         * 2004 per-cycle mover. Those lanes are not reproducing the 2004
         * client, and `[features:boot] mover=frame` in their manifests is how
         * they say so. TORIRS_MOVER_MODEL is the same switch for an A/B without
         * editing a manifest.
         */
        {
            int model = -1;

            if( rc_features->mover[0] )
            {
                model = ToriRS_Features_MoverModelByName(rc_features->mover);
                if( model < 0 )
                    TORIRS_LOG(
                        "app: [features] mover must be cycle|frame, got '%s'\n",
                        rc_features->mover);
            }
            if( cfg->features_mover_model_set )
                model = cfg->features_mover_model;
            {
                char const* env = getenv("TORIRS_MOVER_MODEL");
                if( env && env[0] )
                {
                    int from_env = ToriRS_Features_MoverModelByName(env);
                    if( from_env < 0 )
                        TORIRS_ERR(
                            "app: unknown TORIRS_MOVER_MODEL '%s' "
                            "(cycle|frame)\n",
                            env);
                    else
                        model = from_env;
                }
            }
            if( model >= 0 )
                app->features_storage.mover_model = model;
        }

        if( torirs_env_net_debug() )
            TORIRS_LOG(
                "app: features era=%s ground_click_nearest=%s "
                "unbounded=%d offmap=%d painter_draw_distance=%d\n",
                app->features->name,
                ToriRS_Features_NearestModelName(app->features->ground_click_nearest_model),
                app->features->ground_click_nearest_unbounded,
                app->features->ground_click_offmap_nearest,
                ToriRS_Features_PainterDrawDistance(app->features));
        RS_Audio_SetFeatures(&app->audio, app->features);
        /* And the world sim: the actor mover is era-dependent too (rev-239
         * integrates movement per rendered frame, the 2004 client per 20ms
         * cycle -- enum ToriRS_MoverModel). Pointed at app->features_storage
         * rather than the singleton so a manifest override reaches it, and set
         * after the overrides above for the same reason. */
        World_SetFeatures(app->world, app->features);
        if( torirs_env_net_debug() )
            TORIRS_LOG(
                "app: world mover=%s\n",
                ToriRS_Features_MoverModelName(World_MoverModel(app->world)));

        /* Model lighting: era defaults for the two xrsps-vs-Client-TS
         * divergences, then [render:light] overrides, then push the regimes
         * into toridraw (compiled-in actor/scene profiles unless overridden). */
        app->npc_light_uses_type_ambient_contrast =
            app->features->npc_light_uses_type_ambient_contrast;
        app->player_head_light_ambient = app->features->player_head_light_ambient;
        if( cfg->light_npc_type_ambient_contrast_set )
            app->npc_light_uses_type_ambient_contrast = cfg->light_npc_type_ambient_contrast;
        if( cfg->light_player_head_ambient_set )
            app->player_head_light_ambient = cfg->light_player_head_ambient;

        {
            struct ToriDraw_LightProfile actor = *ToriDraw_LightActorProfile();
            struct ToriDraw_LightProfile scene = *ToriDraw_LightSceneProfile();
            int actor_override = 0;
            int scene_override = 0;

            if( cfg->light_actor_ambient_set )
            {
                actor.ambient = cfg->light_actor_ambient;
                actor_override = 1;
            }
            if( cfg->light_actor_attenuation_set )
            {
                actor.attenuation = cfg->light_actor_attenuation;
                actor_override = 1;
            }
            if( cfg->light_actor_set )
            {
                actor.src_x = cfg->light_actor_x;
                actor.src_y = cfg->light_actor_y;
                actor.src_z = cfg->light_actor_z;
                actor_override = 1;
            }
            if( cfg->light_scene_ambient_set )
            {
                scene.ambient = cfg->light_scene_ambient;
                scene_override = 1;
            }
            if( cfg->light_scene_attenuation_set )
            {
                scene.attenuation = cfg->light_scene_attenuation;
                scene_override = 1;
            }
            if( cfg->light_scene_set )
            {
                scene.src_x = cfg->light_scene_x;
                scene.src_y = cfg->light_scene_y;
                scene.src_z = cfg->light_scene_z;
                scene_override = 1;
            }
            if( actor_override || scene_override )
                ToriDraw_LightSetProfiles(
                    actor_override ? &actor : NULL, scene_override ? &scene : NULL);
        }
        app->bridge.npc_light_uses_type_ambient_contrast =
            app->npc_light_uses_type_ambient_contrast;
        app->bridge.player_head_light_ambient = app->player_head_light_ambient;
    }

    /*
     * Phase 5b: the world map editor (opt-in, and mutually exclusive with a
     * GAME server in practice).
     *
     * Constructed before the networking phase below on purpose: an editor boot
     * states no `[net:boot]`, so that phase does not run at all and this is the
     * last thing built. The editor is the second writer of world state — the
     * first being the packet layer that is absent here — and it reaches the
     * world through the same seams that layer would.
     *
     * The session is a client of ToriRSMapEd, and the manifest picks the
     * deployment the same way [net:boot] picks the game server:
     * `server=embed` (default) hosts the server inside this process over
     * content_dir, `server=tcp` dials the torirsmaped daemon — which is what
     * enables the editor even without a content_dir of its own, since the
     * daemon owns the tree in that deployment.
     */
    if( (cfg->editor_content_dir && cfg->editor_content_dir[0]) ||
        cfg->editor_server == BOOTMANIFEST_EDITOR_SERVER_TCP )
    {
        struct EditorHost editor_host = { NULL, NULL };
        char editor_label[600];
        int editor_ok;

        if( cfg->editor_server == BOOTMANIFEST_EDITOR_SERVER_TCP )
        {
            char const* maped_host = cfg->editor_server_host && cfg->editor_server_host[0]
                                         ? cfg->editor_server_host
                                         : "localhost";
            snprintf(
                editor_label,
                sizeof(editor_label),
                "maped://%s:%d",
                maped_host,
                cfg->editor_server_port > 0 ? cfg->editor_server_port : TORIRSMAPED_DEFAULT_PORT);
            /* A client with a world is a VIEWER; `client=` joins a Client
             * another connection already started, so several processes can
             * share one selection. */
            editor_ok = Editor_HostOpenMapEdTcp(
                &editor_host,
                maped_host,
                cfg->editor_server_port,
                TORIRSMAPED_ROLE_VIEWER,
                (uint32_t)cfg->editor_client_id);
        }
        else
        {
            snprintf(
                editor_label,
                sizeof(editor_label),
                "%s (embedded ToriRSMapEd)",
                cfg->editor_content_dir);
            editor_ok = Editor_HostOpenMapEdEmbed(
                &editor_host, cfg->editor_content_dir, cfg->editor_repo_root);
        }

        /* An unreachable daemon is a deployment state, not a build defect:
         * boot the client without the editor and say why, rather than booting
         * an editor whose every operation would fail one at a time. */
        if( !editor_ok )
        {
            TORIRS_ERR(
                "app: cannot reach ToriRSMapEd at %s — the map editor is disabled "
                "this session\n",
                editor_label);
            goto editor_skipped;
        }

        app->editor = malloc(sizeof(*app->editor));
        assert(app->editor);
        Editor_OpenHost(
            app->editor, &editor_host, editor_label, CacheProvider_Profile(app->provider));
        /* The Client id is printed because it is the handle another PROCESS
         * needs to join this session: `torirsmapedctl --client <id>`. */
        TORIRS_LOG(
            "app: map editor over %s (%s, client %u)\n",
            editor_label,
            app->editor->writable ? "writable" : "read-only, another server holds it",
            Editor_HostMapEdClientId(&app->editor->host));
        /* Open on boot: this manifest asked for an editor, so the panel is the
         * point of the session rather than a debug aid to go find. */
        Editor_PanelInit(&app->editor_panel, &app->dbg_ui);
        /* The selection relay, both halves: the panel publishes its latch
         * through the session, and the Client's state facts land back on the
         * panel — which is how a controller connection follows this viewer's
         * clicks, and how this panel will follow a detached viewer's. */
        app->editor_panel.editor = app->editor;
        Editor_SetStateCallback(app->editor, app_editor_on_state, app);
        Editor_PanelSetVisible(
            &app->editor_panel,
            &app->dbg_ui,
            cfg->editor_panel == BOOTMANIFEST_EDITOR_PANEL_INPROCESS);

        /*
         * panel=tab: the panel lives in a second browser tab instead of in
         * this window, so the in-process rows stay hidden and the page is
         * asked to open it. The world view keeps the whole canvas.
         *
         * Only the *open* happens here. Whether that tab ever attaches is the
         * channel's business (torirs_channel.js), and the renderer never waits
         * on it -- a blocked popup or a closed tab leaves an editor that still
         * edits, just without its chrome, which is why nothing below this is
         * conditional on the tab appearing.
         */
        if( cfg->editor_panel == BOOTMANIFEST_EDITOR_PANEL_TAB )
        {
#if defined(TORIRS_PLATFORM_WEB)
            web_editor_open_panel_tab();
#else
            /* Unreachable: bootmanifest refuses panel=tab on a native build.
             * Kept as a loud stop rather than a silent skip, so a future
             * platform that reaches here has to decide what it means. */
            assert(0 && "panel=tab reached a build with no tabs to open");
#endif
        }
    editor_skipped:;
    }

    /* Phase 6: networking (opt-in). The default RSA key is the rs245_2lc Lost
     * City pair (v0 tori_rs_init); TORIRS_RSA_EXP/MOD override it. */
    if( cfg->connect_target && cfg->connect_target[0] )
    {
        /* RSA key precedence: env > manifest (cfg) > built-in default pair. */
        char const* rsa_e = getenv("TORIRS_RSA_EXP");
        char const* rsa_n = getenv("TORIRS_RSA_MOD");
        if( !rsa_e )
            rsa_e = cfg->rsa_exp;
        if( !rsa_n )
            rsa_n = cfg->rsa_mod;
        if( !rsa_e )
            rsa_e = "81f390b2cf8ca7039ee507975951d5a0b15a87bf8b3f99c966834118c50fd94d";
        if( !rsa_n )
            rsa_n = "88c38748a58228f7261cdc340b5691d7d0975dee0ecdb717609e6bf971eb3fe723ef9d130e468"
                    "6813739768ad9472eb46d8bfcc042c1a5fcb05e931f632eea5d";

        char const* rev_name = cfg->rev_name;
        if( !rev_name || !rev_name[0] )
            rev_name = getenv("TORIRS_REV");
        struct GameProtoRevTable const* rev =
            rev_name && rev_name[0] ? GameProtoRev_ByName(rev_name) : GameProtoRev_LC254();
        if( !rev )
        {
            TORIRS_ERR("app: unknown protocol rev '%s', using lc254\n", rev_name);
            rev = GameProtoRev_LC254();
        }

        /* Manifest login params override the table defaults, but env still wins
         * (the lazy TORIRS_JAG_CRC parse in the rev getter already ran). */
        if( cfg->jag_crc_set && !getenv("TORIRS_JAG_CRC") )
            GameProtoRev_SetJagChecksums(rev, cfg->jag_crc);
#if !defined(TORIRS_PLATFORM_WEB)
        /* An on-demand boot can do better than any stated value: the cache and
         * the checksums come from the same server, in the same second, so they
         * cannot disagree. A manifest `jag_crc=` (or the env) is a deliberate
         * override and still wins -- that is the seam for pointing a client at
         * one server while claiming another's cache. */
        if( app->cache_on_demand && !cfg->jag_crc_set && !getenv("TORIRS_JAG_CRC") )
        {
            int32_t crc[9];
            /* The dial path refreshes these before every login attempt --
             * this read only primes the table for a client that never dials
             * (and keeps the failure loud when the endpoint is down). */
            app->jag_crc_from_ondemand = 1;
            if( PlatformXIO_Dat1OnDemandJagChecksums(app->runner.px, crc) == 0 )
                GameProtoRev_SetJagChecksums(rev, crc);
            else
                TORIRS_ERR(
                    "app: could not read /crc from the cache server; login will be "
                    "refused as out of date\n");
        }
#endif
        if( cfg->client_version > 0 )
            GameProtoRev_SetClientVersion(rev, cfg->client_version);

        app->net = calloc(1, sizeof(struct ToriRS_Network));
        assert(app->net);
        ToriRS_Network_Init(app->net, rev, rsa_e, rsa_n);
        app->net_enabled = 1;
        /* IF1 button clicks now notify the server (reference IF_BUTTON /
         * RESUME_PAUSEBUTTON). */
        app->button_sink.user = app;
        app->button_sink.if_button = app_send_if_button;
        app->button_sink.resume_pausebutton = app_send_resume_pausebutton;
        app->button_sink.close_modal = app_send_close_modal;
        /*
         * Dialling is NOT done here any more.
         *
         * It used to be: App_Init opened the socket with whatever credentials
         * the command line carried, before a single frame had been drawn. That
         * left no room for a login screen -- by the time anything could be
         * shown, the handshake had already happened.
         *
         * The connect now happens on submit (app_title_submit), which is the
         * one path a clicked Login, a pressed Enter and an autologin all take.
         * A profile with no title screen still connects without one: see
         * app_title_tick's autologin branch.
         *
         * Credentials are kept for that submit rather than passed here. The old
         * "guest"/"" defaults are gone with the call: absent credentials now
         * mean an interactive login form, which is the point.
         */
        RS_TitleSession_SetCredentials(
            &app->title_session, cfg->connect_user, cfg->connect_pass);
        RS_TitleSession_SetConnectTarget(&app->title_session, cfg->connect_target);
    }
}

int
App_UiLogic(struct App const* app)
{
    assert(app);
    if( app->cfg.ui_logic == APP_UI_LOGIC_CS1 || app->cfg.ui_logic == APP_UI_LOGIC_CS2 )
        return app->cfg.ui_logic;
    /* DEFAULT: derive from cache format (bit-identical to the legacy keying). */
    return app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_UI_LOGIC_CS1 : APP_UI_LOGIC_CS2;
}

/*
 * Write out a settings change that has not reached its settle window yet.
 *
 * Quitting is exactly when that happens: the player turns the music down and
 * closes the client, and the tick that would have queued the save never comes.
 *
 * Stepped here against a private IO rather than handed to the App's runner:
 * that queue may hold tasks parked on state a shutting-down client will never
 * produce, so draining it could return with the save still queued. One task
 * against one IO list terminates on both backends — the platform answers a
 * client-file item inline in Process.
 */
static void
app_prefs_flush(struct App* app)
{
    struct ToriRS_Task* task;
    struct ToriRS_IO* io;
    int guard = 0;

    if( !app->prefs_path )
        return;
    if( !RS_Prefs_CaptureFromHost(&app->prefs, &app->host) && !app->prefs_dirty_cycle )
        return; /* everything the player chose is already on disk */
    app->prefs_dirty_cycle = 0;

    /* A real IO list, not a zeroed struct on the stack: the slot table is
     * heap-grown (ToriRS_IO_SlotReserve), so a zeroed one has no slots and
     * the task's first queue is a write through NULL -- a crash on the way
     * out, on exactly the exit where the player had changed a setting. */
    io = ToriRS_IO_New();
    task = CreateTask_PrefsSave(&app->prefs, app->prefs_path);
    while( task_run(task, io) == PT_YIELDED && guard++ < 8 )
        Platform_IO_Process(app->runner.px, io);
    task_free(task);
    ToriRS_IO_Free(io);
}

void
App_Shutdown(struct App* app)
{
    assert(app);
    app_prefs_flush(app);
    /* Plugins first: on_stop callbacks may still read world state and the
     * config store, and both are torn down below. */
    PluginHost_Free(app->plugins);
    app->plugins = NULL;
    if( app->editor )
    {
        /* Releases the content-tree lock. Unsaved edits are NOT written here:
         * a save is something the user asks for, and silently flushing on exit
         * would put edits on disk that were abandoned on purpose. */
        if( Editor_DocHasUnsaved(&app->editor->doc) )
            TORIRS_LOG("app: map editor closing with unsaved edits\n");
        Editor_Close(app->editor);
        free(app->editor);
        app->editor = NULL;
    }
    if( app->net )
    {
        ToriRS_Network_Free(app->net);
        free(app->net);
        app->net = NULL;
    }
    UITree_EmitBufferFree(&app->emit);
    RS_WorldMapRender_Free(app->worldmap.render);
    app->worldmap.render = NULL;
    /* Before the host, which points at it. */
    RS_Chat_Free(&app->chat);
    RS_CS2Host_Free(&app->host);
    if( app->painter_buffer )
    {
        free(app->painter_buffer->commands);
        free(app->painter_buffer);
    }
    RS_Audio_Shutdown(&app->audio);
    /* The bed holds borrowed pointers into this table, so it has to outlive the
     * audio layer's teardown. */
    RS_Soundscapes_Free(&app->soundscapes);
    RS_Healthbars_Free(&app->healthbars);
    RS_EntitySync_Free(&app->esync);
    /* Frees any owned (non-root) views; the root slot only borrows the pair
     * freed just below. */
    WorldviewRegistry_Free(&app->worldviews);
    WevConfigTable_Free(&app->wev_configs);
    WorldBuilder_Free(app->world_builder);
    World_Free(app->world);
    VarPManager_Free(&app->varps);
    VarCManager_Free(&app->varcs);
    LootStore_Free(&app->loot);
    InvManager_Free(&app->invs);
    UITree_Free(app->tree);
    if( app->builder_active )
        UITreeBuilder_Free(&app->builder);
    UITreeSceneBridge_Free(&app->bridge);
    TorirsModelInstCache_Free(&app->model_inst_cache);
    ToriRS_Soft3D_Free(app->soft_chrome);
    free(app->panel_custom_pixels);
    app->panel_custom_pixels = NULL;
    app->panel_custom_pixel_capacity = 0;
    ToriRS_Soft3D_Free(app->soft);
    ToriDraw_SceneFree(app->scene);
    /* Only the pair matching cfg.cache_kind was ever created; both frees assert
     * on NULL, so the unused side must not be handed to them. */
    if( app->dat2_bc )
        dat2_buildcache_free(app->dat2_bc);
    if( app->dat1_bc )
        dat1_buildcache_free(app->dat1_bc);
    Platform_IO_Free(app->runner.px);
    if( app->dat2_disk )
        RSCache_Dat2DiskFree(app->dat2_disk);
    if( app->dat1_disk )
        RSCache_Dat1DiskFree(app->dat1_disk);
    ToriRS_TaskQueue_Free(app->exec_runner.queue);
    ToriRS_IO_Free(app->exec_runner.io);
    ToriRS_TaskQueue_Free(app->runner.queue);
    ToriRS_IO_Free(app->runner.io);
    /* After the queues: freeing a task releases its VM back into the pool. */
    CS2VM2_PoolDrain();
    /* Also after the queues. A parked entity-info task borrows the scratch and
     * hands it back from its _Free, so releasing it any earlier would leave that
     * _Free freeing a pointer this call already returned to the allocator. */
    Task_EntityInfoScratchFree(app);
    free(app->if_heads);
    free(app->if_player_models);
    /* All three of these were leaked. Only the hide array was freed, and its
     * two siblings -- plus every string the text store strdup'd -- were not.
     * They have owners now. */
    UIIfIntStore_Free(&app->if_hides);
    UIIfIntStore_Free(&app->if_colours);
    UIIfTextStore_Free(&app->if_texts);
    /* The IF_SETEVENTS store was never released here; it had no owner to ask.
     * It does now, so it is freed with the rest of the if_* tables. */
    UIIfEventTable_Free(&app->if_events);
    RevConfigRefs_Free(&app->revconfig_refs);
    UITree_RoleTableFree(&app->ui_roles);
}

