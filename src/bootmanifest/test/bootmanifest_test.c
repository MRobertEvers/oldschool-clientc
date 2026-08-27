#include "bootmanifest.h"

#include "app.h"
#include "executor_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
static int
test_setenv(const char* name, const char* value, int overwrite)
{
    if( !overwrite && getenv(name) )
        return 0;
    return _putenv_s(name, value);
}

static int
test_unsetenv(const char* name)
{
    return _putenv_s(name, "");
}

#define setenv test_setenv
#define unsetenv test_unsetenv
#endif

static int g_fail = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while( 0 )

/* Path passed with a directory component so relative values join against it. */
static char const* const FIXTURE = "bootmanifest/test/fixture_manifest.ini";

static void
test_load_fields(void)
{
    struct BootManifest bm;
    CHECK(BootManifest_LoadFile(&bm, FIXTURE) == 0);

    CHECK(bm.cache_kind == APP_CACHE_DAT2);
    CHECK(bm.cache_epoch == 2); /* RSCACHE_EPOCH_DAT2 */
    CHECK(bm.cache_game == 1);  /* RSCACHE_GAME_OLDSCHOOL */
    CHECK(bm.cache_revision == 233);
    CHECK(bm.cache_quirks_set == 1);
    CHECK(bm.cache_quirks == 0);
    CHECK(strcmp(bm.cache_dir, "bootmanifest/test/some/cache") == 0);
    CHECK(bm.spawn_x == 12);
    CHECK(bm.spawn_z == 34);

    CHECK(strcmp(bm.rev_name, "xrsps233") == 0);
    CHECK(strcmp(bm.transport, "ws") == 0);
    CHECK(strcmp(bm.host, "example.com") == 0);
    CHECK(bm.port == 1234);
    CHECK(strcmp(bm.ws_host, "ws.example.com") == 0);
    CHECK(bm.ws_port == 8080);
    CHECK(bm.client_version == 233);
    CHECK(strcmp(bm.server_scripts, "bootmanifest/test/compiled/scripts") == 0);
    CHECK(strcmp(bm.rsa_exp, "deadbeef") == 0);
    CHECK(strcmp(bm.rsa_mod, "cafef00d") == 0);
    CHECK(bm.jag_crc_set == 1);
    for( int i = 0; i < 9; i++ )
        CHECK(bm.jag_crc[i] == i);

    CHECK(bm.client_arg_count == 9);
    CHECK(strcmp(bm.client_args[0], "--offline") == 0);
    CHECK(strcmp(bm.client_args[1], "--window") == 0);
    CHECK(strcmp(bm.client_args[2], "1024x768") == 0);
    CHECK(strcmp(bm.client_args[3], "--user") == 0);
    CHECK(strcmp(bm.client_args[4], "Jane Doe") == 0);
    CHECK(strcmp(bm.client_args[5], "--pass") == 0);
    CHECK(strcmp(bm.client_args[6], "p;#&=, \"quoted\"") == 0);
    CHECK(strcmp(bm.client_args[7], "--revconfig") == 0);
    CHECK(strcmp(bm.client_args[8], "C:\\Program Files\\ui.ini") == 0);

    /* Build information the launcher reads back: which content lanes went into
     * the pack `scripts=` names. Read in file order, because the order they are
     * handed to sscompile is the order they were written. */
    CHECK(bm.lane_count == 2);
    CHECK(strcmp(bm.lanes[0], "scape2009_summoning") == 0);
    CHECK(strcmp(bm.lanes[1], "rs558_ancient_curses") == 0);
    CHECK(bm.lanes_error == 0);

    CHECK(bm.js5_enabled == 1);
    CHECK(strcmp(bm.js5_host, "js5.example.com") == 0);
    CHECK(bm.js5_port == 43594);
    CHECK(bm.js5_fallback_port_set == 1);
    CHECK(bm.js5_fallback_port == 443);
    CHECK(bm.js5_revision_set == 1);
    CHECK(bm.js5_revision == 233);

    CHECK(bm.ui_logic == APP_UI_LOGIC_CS2);
    CHECK(bm.chrome == 2 /* cache */);
    /* Absolute path passes through; relative joins against the manifest dir. */
    CHECK(strcmp(bm.revconfig_ui, "/abs/ui.ini") == 0);
    CHECK(strcmp(bm.revconfig_cache, "bootmanifest/test/rel/cache.ini") == 0);
    CHECK(bm.interface_id == 161);

    CHECK(bm.debug_action_count == 8);
    CHECK(strcmp(bm.debug_actions[0].name, "unlock_camera") == 0);
    CHECK(bm.debug_actions[0].target == APP_DEBUG_HOTKEY_CAMERA_UNLOCK);
    CHECK(strcmp(bm.debug_actions[2].name, "increase_paint") == 0);
    CHECK(bm.debug_actions[2].target == APP_DEBUG_HOTKEY_PAINT_MORE);
    CHECK(strcmp(bm.debug_actions[2].args, "step=1") == 0);
    CHECK(strcmp(bm.debug_actions[3].args, "id=7343") == 0);
    CHECK(strcmp(bm.debug_actions[4].args, "id=1265") == 0);
    CHECK(strcmp(bm.debug_actions[5].args, "id=74,height=92,delay=0") == 0);
    CHECK(strcmp(bm.debug_actions[6].args, "model=3081,seq=659") == 0);
    CHECK(bm.debug_hotkey_count == 8);
    CHECK(bm.debug_hotkeys[0].key == TORIRSK_U);
    CHECK(strcmp(bm.debug_hotkeys[0].action, "unlock_camera") == 0);
    CHECK(bm.debug_hotkeys[2].key == TORIRSK_COMMA);
    CHECK(strcmp(bm.debug_hotkeys[7].action, "show_debug_overlay") == 0);

    CHECK(bm.actor_ambient_set == 1);
    CHECK(bm.actor_ambient == 64);
    CHECK(bm.actor_attenuation_set == 1);
    CHECK(bm.actor_attenuation == 850);
    CHECK(bm.actor_light_set == 1);
    CHECK(bm.actor_light_x == -30);
    CHECK(bm.actor_light_y == -50);
    CHECK(bm.actor_light_z == -30);
    CHECK(bm.scene_ambient_set == 1);
    CHECK(bm.scene_attenuation == 768);
    CHECK(bm.scene_light_set == 1);
    CHECK(bm.scene_light_x == -50);
    CHECK(bm.scene_light_y == -10);
    CHECK(bm.scene_light_z == -50);
    CHECK(bm.npc_type_ambient_contrast_set == 1);
    CHECK(bm.npc_type_ambient_contrast == 1);
    CHECK(bm.player_head_ambient_set == 1);
    CHECK(bm.player_head_ambient == 128);
    CHECK(bm.features_painter_draw_distance == 90);
}

static void
test_apply_to_executor_config(void)
{
    struct BootManifest bm;
    struct ToriRS_ExecutorConfig executor;

    CHECK(BootManifest_LoadFile(&bm, FIXTURE) == 0);
    ToriRS_ExecutorConfig_Init(&executor);
    BootManifest_ApplyToExecutorConfig(&bm, &executor);

    CHECK(executor.js5_enabled == 1);
    CHECK(strcmp(executor.js5_host, "js5.example.com") == 0);
    CHECK(executor.js5_port == 43594);
    CHECK(executor.js5_fallback_port_set == 1);
    CHECK(executor.js5_fallback_port == 443);
    CHECK(executor.js5_revision_explicit == 1);
    CHECK(executor.js5_revision == 233);
}

static void
test_apply_to_config(void)
{
    struct BootManifest bm;
    CHECK(BootManifest_LoadFile(&bm, FIXTURE) == 0);

    struct AppConfig cfg = { 0 };
    cfg.cache_kind = APP_CACHE_DAT1; /* seeded default the manifest should replace */
    BootManifest_ApplyToConfig(&bm, &cfg);

    CHECK(cfg.cache_kind == APP_CACHE_DAT2);
    CHECK(cfg.cache_identity_set == 1);
    CHECK(cfg.cache_revision == 233);
    CHECK(cfg.cache_dir && strcmp(cfg.cache_dir, "bootmanifest/test/some/cache") == 0);
    CHECK(cfg.rev_name && strcmp(cfg.rev_name, "xrsps233") == 0);
    CHECK(cfg.connect_target && strcmp(cfg.connect_target, "example.com") == 0);
    CHECK(cfg.connect_port == 1234);
    CHECK(cfg.client_version == 233);
    CHECK(cfg.net_server_scripts &&
          strcmp(cfg.net_server_scripts, "bootmanifest/test/compiled/scripts") == 0);
    CHECK(cfg.rsa_exp && strcmp(cfg.rsa_exp, "deadbeef") == 0);
    CHECK(cfg.jag_crc_set == 1);
    CHECK(cfg.ui_logic == APP_UI_LOGIC_CS2);
    CHECK(cfg.interface_id == 161);
    CHECK(cfg.spawn_x == 12);
    CHECK(cfg.spawn_z == 34);
    CHECK(cfg.debug_hotkey_count == 8);
    CHECK(cfg.debug_hotkeys[0].key == TORIRSK_U);
    CHECK(cfg.debug_hotkeys[0].target == APP_DEBUG_HOTKEY_CAMERA_UNLOCK);
    CHECK(cfg.debug_hotkeys[1].key == TORIRSK_I);
    CHECK(cfg.debug_hotkeys[1].target == APP_DEBUG_HOTKEY_PAINT_TOGGLE);
    CHECK(cfg.debug_hotkeys[2].key == TORIRSK_COMMA);
    CHECK(cfg.debug_hotkeys[2].target == APP_DEBUG_HOTKEY_PAINT_MORE);
    CHECK(strcmp(cfg.debug_hotkeys[2].args, "step=1") == 0);
    CHECK(cfg.debug_hotkeys[3].key == TORIRSK_8);
    CHECK(cfg.debug_hotkeys[3].target == APP_DEBUG_HOTKEY_SPAWN_NPC);
    CHECK(cfg.debug_hotkeys[3].target == APP_DEBUG_HOTKEY_SPAWN_NPC);
    CHECK(strcmp(cfg.debug_hotkeys[3].args, "id=7343") == 0);
    CHECK(cfg.debug_hotkeys[4].target == APP_DEBUG_HOTKEY_SPAWN_OBJ);
    CHECK(strcmp(cfg.debug_hotkeys[4].args, "id=1265") == 0);
    CHECK(cfg.debug_hotkeys[5].target == APP_DEBUG_HOTKEY_SPAWN_SPOTANIM);
    CHECK(strcmp(cfg.debug_hotkeys[5].args, "id=74,height=92,delay=0") == 0);
    CHECK(cfg.debug_hotkeys[6].target == APP_DEBUG_HOTKEY_SPAWN_PROJECTILE);
    CHECK(strcmp(cfg.debug_hotkeys[6].args, "model=3081,seq=659") == 0);
    CHECK(cfg.debug_hotkeys[7].key == TORIRSK_P);
    CHECK(cfg.debug_hotkeys[7].target == APP_DEBUG_HOTKEY_DEBUG_OVERLAY);

    CHECK(cfg.light_actor_ambient_set == 1);
    CHECK(cfg.light_actor_ambient == 64);
    CHECK(cfg.light_actor_attenuation == 850);
    CHECK(cfg.light_actor_set == 1);
    CHECK(cfg.light_actor_x == -30);
    CHECK(cfg.light_actor_y == -50);
    CHECK(cfg.light_actor_z == -30);
    CHECK(cfg.light_scene_set == 1);
    CHECK(cfg.light_scene_y == -10);
    CHECK(cfg.light_npc_type_ambient_contrast_set == 1);
    CHECK(cfg.light_npc_type_ambient_contrast == 1);
    CHECK(cfg.light_player_head_ambient_set == 1);
    CHECK(cfg.light_player_head_ambient == 128);

    CHECK(cfg.features_era && strcmp(cfg.features_era, "server_routed") == 0);
    CHECK(cfg.features_ground_click_nearest_set == 1);
    CHECK(cfg.features_ground_click_nearest == TORIRS_NEAREST_BOX10_RECT);
    /* The two permissive ground-click extensions. Every era table leaves them
     * off, so these keys are the only way a boot can ask for them — an
     * unparsed key would silently mean "reference behaviour" forever. */
    CHECK(cfg.features_ground_click_unbounded == 1);
    CHECK(cfg.features_ground_click_offmap == 1);
    CHECK(cfg.features_painter_draw_distance_set == 1);
    CHECK(cfg.features_painter_draw_distance == 90);
}

/* ApplyToConfig must only write fields the manifest actually set, so a
 * CLI-seeded value the manifest omits survives (precedence CLI > manifest). */
static void
test_partial_apply_preserves_unset(void)
{
    struct BootManifest bm;
    memset(&bm, 0, sizeof(bm));
    bm.cache_kind = -1; /* unset */
    /* Sentinel -1 matches BootManifest_LoadFile: unset means "do not write". */
    bm.spawn_x = -1;
    bm.spawn_z = -1;
    /* 0 is TORIRS_NEAREST_RING3_STEPS, a real model — the sentinel keeps a
     * zeroed manifest from reading as "override the era to the 2004 ring". */
    bm.features_ground_click_nearest = -1;
    /* Only a host is provided; everything else stays unset. */
    snprintf(bm.host, sizeof(bm.host), "manifesthost");

    struct AppConfig cfg = { 0 };
    cfg.cache_dir = "cli_cache_dir";
    cfg.connect_user = "cli_user";
    cfg.debug_hotkey_count = 1;
    cfg.debug_hotkeys[0].key = TORIRSK_Q;
    cfg.debug_hotkeys[0].target = APP_DEBUG_HOTKEY_CAMERA_FORWARD;
    BootManifest_ApplyToConfig(&bm, &cfg);

    /* Untouched: manifest set neither. */
    CHECK(cfg.cache_dir && strcmp(cfg.cache_dir, "cli_cache_dir") == 0);
    CHECK(cfg.connect_user && strcmp(cfg.connect_user, "cli_user") == 0);
    CHECK(cfg.connect_port == 0);
    CHECK(cfg.features_ground_click_nearest_set == 0);
    CHECK(cfg.features_painter_draw_distance_set == 0);
    CHECK(cfg.debug_hotkey_count == 1);
    CHECK(cfg.debug_hotkeys[0].key == TORIRSK_Q);
    CHECK(cfg.debug_hotkeys[0].target == APP_DEBUG_HOTKEY_CAMERA_FORWARD);
    /* Set: host became the connect target. */
    CHECK(cfg.connect_target && strcmp(cfg.connect_target, "manifesthost") == 0);

    struct ToriRS_ExecutorConfig executor;
    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    snprintf(executor.js5_host, sizeof(executor.js5_host), "cli-js5-host");
    executor.js5_port = 40000;
    executor.js5_fallback_port = 0;
    executor.js5_fallback_port_set = 1;
    executor.js5_revision = 240;
    executor.js5_revision_explicit = 1;
    bm.js5_enabled = -1;
    BootManifest_ApplyToExecutorConfig(&bm, &executor);
    CHECK(executor.js5_enabled == 1);
    CHECK(strcmp(executor.js5_host, "cli-js5-host") == 0);
    CHECK(executor.js5_port == 40000);
    CHECK(executor.js5_fallback_port_set == 1);
    CHECK(executor.js5_fallback_port == 0);
    CHECK(executor.js5_revision == 240);
    CHECK(executor.js5_revision_explicit == 1);
}

static void
test_executor_js5_resolution(void)
{
    struct AppConfig app_cfg = { 0 };
    struct ToriRS_ExecutorConfig executor;
    char error[160];

    app_cfg.cache_kind = APP_CACHE_DAT2;
    app_cfg.cache_identity_set = 1;
    app_cfg.cache_revision = 239;
    app_cfg.connect_target = "oldschool.example.com";

    /* --js5 by itself inherits the effective game endpoint and cache revision. */
    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) == 0);
    CHECK(strcmp(executor.js5_host, "oldschool.example.com") == 0);
    CHECK(executor.js5_port == 43594);
    CHECK(executor.js5_fallback_port == 443);
    CHECK(executor.js5_revision == 239);

    /* --connect host:port is one endpoint to the game transport. JS5 inherits
     * both halves rather than handing the colon to DNS as part of the host. */
    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    app_cfg.connect_target = "oldschool.example.com:44000";
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) == 0);
    CHECK(strcmp(executor.js5_host, "oldschool.example.com") == 0);
    CHECK(executor.js5_port == 44000);
    CHECK(executor.js5_fallback_port == 0);
    app_cfg.connect_target = "oldschool.example.com";

    /* A custom primary gets no implicit fallback. */
    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    snprintf(executor.js5_host, sizeof(executor.js5_host), "cache.example.com");
    executor.js5_port = 40000;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) == 0);
    CHECK(executor.js5_fallback_port == 0);

    /* An explicit fallback, including explicit zero, beats the derived value. */
    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    executor.js5_fallback_port = 8443;
    executor.js5_fallback_port_set = 1;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) == 0);
    CHECK(executor.js5_fallback_port == 8443);

    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    executor.js5_fallback_port = 0;
    executor.js5_fallback_port_set = 1;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) == 0);
    CHECK(executor.js5_fallback_port == 0);

    /* Revision drift is rejected unless the manifest/CLI explicitly opted out. */
    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    executor.js5_revision = 240;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) != 0);
    executor.js5_revision_explicit = 1;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) == 0);
    CHECK(executor.js5_revision == 240);

    char overlong_host[TORIRS_EXECUTOR_JS5_HOST_MAX + 1];
    memset(overlong_host, 'x', sizeof(overlong_host) - 1);
    overlong_host[sizeof(overlong_host) - 1] = '\0';
    app_cfg.connect_target = overlong_host;
    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) != 0);

    ToriRS_ExecutorConfig_Init(&executor);
    executor.js5_enabled = 1;
    app_cfg.cache_kind = APP_CACHE_DAT1;
    CHECK(ToriRS_ExecutorConfig_ResolveJs5(&executor, &app_cfg, error, sizeof(error)) != 0);
}

/*
 * The web endpoint replaces host:port, and only that: everything else the
 * manifest applied stays put. Env wins over the manifest, and an unset pair
 * leaves the tcp endpoint alone — the case where a server answers WebSockets on
 * the same port it answers TCP.
 */
static void
test_web_endpoint(void)
{
    struct BootManifest bm;
    CHECK(BootManifest_LoadFile(&bm, FIXTURE) == 0);

    struct AppConfig cfg = { 0 };
    BootManifest_ApplyToConfig(&bm, &cfg);
    CHECK(cfg.connect_target && strcmp(cfg.connect_target, "example.com") == 0);
    CHECK(cfg.connect_port == 1234);

    BootManifest_ApplyWebEndpoint(&bm, &cfg);
    CHECK(cfg.connect_target && strcmp(cfg.connect_target, "ws.example.com") == 0);
    CHECK(cfg.connect_port == 8080);
    CHECK(cfg.client_version == 233); /* untouched */

    /* Env overrides the manifest. */
    setenv("TORIRS_WS_HOST", "env.example.com", 1);
    setenv("TORIRS_WS_PORT", "9001", 1);
    BootManifest_ApplyWebEndpoint(&bm, &cfg);
    CHECK(cfg.connect_target && strcmp(cfg.connect_target, "env.example.com") == 0);
    CHECK(cfg.connect_port == 9001);
    unsetenv("TORIRS_WS_HOST");
    unsetenv("TORIRS_WS_PORT");

    /* Neither set: the tcp endpoint stands, so a same-port server needs no
     * manifest change at all. */
    struct BootManifest bare;
    memset(&bare, 0, sizeof(bare));
    struct AppConfig tcp_only = { 0 };
    tcp_only.connect_target = "tcp.example.com";
    tcp_only.connect_port = 43594;
    BootManifest_ApplyWebEndpoint(&bare, &tcp_only);
    CHECK(tcp_only.connect_target && strcmp(tcp_only.connect_target, "tcp.example.com") == 0);
    CHECK(tcp_only.connect_port == 43594);
}

static void
test_required_identity_keys(void)
{
    struct BootManifest bm;
    CHECK(BootManifest_LoadFile(&bm, "bootmanifest/test/fixture_missing_epoch.ini") != 0);
}

static void
test_exact_manifest_tokens_preserved(void)
{
    struct BootManifest bm;
    CHECK(BootManifest_LoadFile(
              &bm, "bootmanifest/test/fixture_nested_manifest_arg.ini") == 0);
    CHECK(bm.client_arg_count == 3);
    CHECK(strcmp(bm.client_args[0], "--manifest") == 0);
    CHECK(strcmp(bm.client_args[1], "another.ini") == 0);
    CHECK(strcmp(bm.client_args[2], "") == 0);
    CHECK(bm.debug_action_count == 0);
    CHECK(bm.debug_hotkey_count == 0);
}

static void
test_migrated_spawn_actions(void)
{
    static char const* const manifests[] = {
        "../manifests/manifest_rs254lc.ini",
        "../manifests/manifest_osrs239_summoning.ini",
        "../manifests/manifest_osrs239_packed.ini",
        "../manifests/manifest_osrs239.ini",
        "../manifests/manifest_rs634void.ini",
    };
    for( size_t i = 0; i < sizeof(manifests) / sizeof(manifests[0]); i++ )
    {
        struct BootManifest bm;
        struct AppConfig cfg = { 0 };
        CHECK(BootManifest_LoadFile(&bm, manifests[i]) == 0);
        BootManifest_ApplyToConfig(&bm, &cfg);
        /* The migrated spawn action is present and well-formed. Searched for
         * rather than asserted at index 0 with a count of 1: these manifests
         * carry other hotkeys too (a loc-editor toggle arrived later), and
         * pinning the total made this test fail for every manifest that grew
         * a binding -- which says nothing about the migration it is testing. */
        int spawn = -1;
        for( int h = 0; h < cfg.debug_hotkey_count; h++ )
            if( cfg.debug_hotkeys[h].target == APP_DEBUG_HOTKEY_SPAWN_NPC )
            {
                spawn = h;
                break;
            }
        CHECK(spawn >= 0);
        CHECK(cfg.debug_hotkeys[spawn].key == TORIRSK_8);
        CHECK(strncmp(cfg.debug_hotkeys[spawn].args, "id=", 3) == 0);
    }
}


/*
 * Where an ondemand world hydrates.
 *
 * The assertions are deliberately about SHAPE rather than an exact string.
 * Recomputing the expected path here would mean reimplementing
 * bm_user_home in the test, which tests nothing: the two copies would
 * agree with each other and both be wrong together. What is worth pinning is
 * what the caller depends on -- that a default appears, that it is anchored
 * rather than relative to the manifest, that it names this world, and that the
 * two ways of overriding it both still work.
 */
static void
test_cache_dir_default(void)
{
    struct BootManifest bm;

    /* Silence defaults to <home>/torirs_cache/<game>/<world>. */
    CHECK(BootManifest_LoadFile(
              &bm, "bootmanifest/test/fixture_cache_default.ini") == 0);
    CHECK(bm.cache_on_demand == 1);
    CHECK(bm.cache_dir_stated == 0);
    CHECK(bm.cache_dir[0] != '\0');
    /* The 239 client's shape -- ~/jagexcache/<game>/<mode>/ -- under our name:
     * <home>/torirs_cache/<game>/<world>/. */
    CHECK(strstr(bm.cache_dir, "torirs_cache") != NULL);
    /* Segmented by game, so a dat1 cache and a dat2 cache never meet. The
     * fixture states game=rs2. */
    CHECK(strstr(bm.cache_dir, "rs2") != NULL);
    /* ...and by world, so two ondemand manifests do not share a directory and
     * wipe each other's contents on every alternating boot. */
    CHECK(strstr(bm.cache_dir, "fixture_cache_default") != NULL);
    /* Under the home, not at the very start of it: something precedes it. */
    CHECK(strstr(bm.cache_dir, "torirs_cache") > bm.cache_dir);
    /* Anchored, not joined onto the manifest's directory. */
    CHECK(strstr(bm.cache_dir, "bootmanifest/test") == NULL);
    CHECK(bm.cache_dir[0] == '/' || bm.cache_dir[0] == '\\'
          || bm.cache_dir[1] == ':');

    /* An empty dir= is the opt-out and must not be defaulted over. */
    CHECK(BootManifest_LoadFile(
              &bm, "bootmanifest/test/fixture_cache_optout.ini") == 0);
    CHECK(bm.cache_on_demand == 1);
    CHECK(bm.cache_dir_stated == 1);
    CHECK(bm.cache_dir[0] == '\0');

    /* `~/x` is the home directory, not a directory named "~". */
    CHECK(BootManifest_LoadFile(
              &bm, "bootmanifest/test/fixture_cache_home.ini") == 0);
    CHECK(bm.cache_dir_stated == 1);
    CHECK(strchr(bm.cache_dir, '~') == NULL);
    CHECK(strstr(bm.cache_dir, "torirs-fixture/cache") != NULL);
    CHECK(strstr(bm.cache_dir, "bootmanifest/test") == NULL);

    /* An idb: location is a database name, not a path: it survives parsing
     * byte for byte rather than being joined onto the manifest's directory. */
    CHECK(BootManifest_LoadFile(
              &bm, "bootmanifest/test/fixture_cache_idb.ini") == 0);
    CHECK(bm.cache_dir_stated == 1);
    CHECK(strcmp(bm.cache_dir, "idb:torirs_cache/rs2/my-world") == 0);
    CHECK(BootManifest_CacheLocationIsIdb(bm.cache_dir) == 1);
    /* And a real directory is not mistaken for one. */
    CHECK(BootManifest_CacheLocationIsIdb("some/cache") == 0);

    /* A stated dir= still wins over the default, and a DISK world is left
     * alone entirely -- there, cache_dir is a cache to read, and inventing one
     * would point the client at an empty directory instead of failing. */
    CHECK(BootManifest_LoadFile(&bm, FIXTURE) == 0);
    CHECK(bm.cache_on_demand == 0);
    CHECK(strcmp(bm.cache_dir, "bootmanifest/test/some/cache") == 0);
}


/* Two paths, equal if they differ only in which separator spells them. */
static int
paths_equal(char const* a, char const* b)
{
    for( ; *a && *b; a++, b++ )
    {
        char ca = (*a == '\\') ? '/' : *a;
        char cb = (*b == '\\') ? '/' : *b;
        if( ca != cb )
            return 0;
    }
    return *a == *b;
}

/*
 * The same manifest, named with the separator Windows tools produce.
 *
 * Every other test here spells FIXTURE with forward slashes, which is how this
 * went unnoticed: bm_dirname searched for '/' alone, so a path spelled
 * `bootmanifest\\test\\fixture_manifest.ini` -- what launch.cmd builds, and
 * what any Windows caller would naturally pass -- yielded an EMPTY manifest
 * directory. Every relative value then resolved against the process's working
 * directory instead of the manifest's.
 *
 * That is not a cosmetic difference. `revconfig_ui` is stated relative to the
 * manifest, so it landed outside the repository and the client booted with no
 * gameframe layout at all -- a window with nothing in it but the fill colour,
 * and no error, because a manifest naming a file that is not there is simply a
 * manifest with no UI.
 *
 * Windows only: on POSIX a backslash is a legal character in a filename, and
 * splitting on it there would break a validly-named file.
 */
static void
test_backslash_manifest_path(void)
{
#if defined(_WIN32)
    struct BootManifest fwd;
    struct BootManifest back;

    CHECK(BootManifest_LoadFile(&fwd, FIXTURE) == 0);
    CHECK(BootManifest_LoadFile(
              &back, "bootmanifest\\test\\fixture_manifest.ini") == 0);

    /*
     * Compared with separators normalised, not byte for byte. A
     * backslash-spelled manifest yields a backslash-spelled directory, so the
     * join produces `bootmanifest\\test/some/cache` -- mixed, and opened by
     * every Windows API in this tree exactly like the all-forward-slash form.
     * What must hold is that the directory was FOUND and prefixed; rewriting
     * one separator into the other is not required and pinning it would fail
     * a correct implementation.
     */
    CHECK(paths_equal(fwd.cache_dir, back.cache_dir));
    CHECK(paths_equal(fwd.revconfig_ui, back.revconfig_ui));
    CHECK(paths_equal(fwd.revconfig_cache, back.revconfig_cache));

    /* And specifically: joined onto the manifest's directory, not left as the
     * bare relative value the manifest states -- which is what happened, and
     * what put revconfig_ui outside the repository. */
    CHECK(paths_equal(back.cache_dir, "bootmanifest/test/some/cache"));
#endif
}

int
main(void)
{
    test_load_fields();
    test_apply_to_config();
    test_apply_to_executor_config();
    test_partial_apply_preserves_unset();
    test_executor_js5_resolution();
    test_web_endpoint();
    test_required_identity_keys();
    test_exact_manifest_tokens_preserved();
    test_migrated_spawn_actions();
    test_cache_dir_default();
    test_backslash_manifest_path();

    if( g_fail )
    {
        fprintf(stderr, "bootmanifest_test: FAILED\n");
        return 1;
    }
    printf("bootmanifest_test: PASS\n");
    return 0;
}
