#include "bootmanifest.h"

#include "app.h"

#include <stdio.h>
#include <string.h>

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
    CHECK(bm.client_version == 233);
    CHECK(strcmp(bm.rsa_exp, "deadbeef") == 0);
    CHECK(strcmp(bm.rsa_mod, "cafef00d") == 0);
    CHECK(bm.jag_crc_set == 1);
    for( int i = 0; i < 9; i++ )
        CHECK(bm.jag_crc[i] == i);

    CHECK(bm.ui_logic == APP_UI_LOGIC_CS2);
    CHECK(bm.chrome == 2 /* cache */);
    /* Absolute path passes through; relative joins against the manifest dir. */
    CHECK(strcmp(bm.revconfig_ui, "/abs/ui.ini") == 0);
    CHECK(strcmp(bm.revconfig_cache, "bootmanifest/test/rel/cache.ini") == 0);
    CHECK(bm.interface_id == 161);

    CHECK(bm.spawn_npc_id == 7343);
    CHECK(bm.spawn_obj_id == 1265);
    CHECK(bm.spawn_spotanim_id == 74);
    CHECK(bm.spawn_spotanim_height == 92);
    CHECK(bm.spawn_spotanim_delay == 0);
    CHECK(bm.spawn_proj_model_id == 3081);
    CHECK(bm.spawn_proj_seq_id == 659);
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
    CHECK(cfg.rsa_exp && strcmp(cfg.rsa_exp, "deadbeef") == 0);
    CHECK(cfg.jag_crc_set == 1);
    CHECK(cfg.ui_logic == APP_UI_LOGIC_CS2);
    CHECK(cfg.interface_id == 161);
    CHECK(cfg.spawn_x == 12);
    CHECK(cfg.spawn_z == 34);
    CHECK(cfg.spawn_npc_id == 7343);
    CHECK(cfg.spawn_obj_id == 1265);
    CHECK(cfg.spawn_spotanim_id == 74);
    CHECK(cfg.spawn_spotanim_height == 92);
    CHECK(cfg.spawn_spotanim_delay == 0);
    CHECK(cfg.spawn_proj_model_id == 3081);
    CHECK(cfg.spawn_proj_seq_id == 659);
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
    bm.spawn_npc_id = -1;
    bm.spawn_obj_id = -1;
    bm.spawn_spotanim_id = -1;
    bm.spawn_spotanim_height = -1;
    bm.spawn_spotanim_delay = -1;
    bm.spawn_proj_model_id = -1;
    bm.spawn_proj_seq_id = -1;
    /* Only a host is provided; everything else stays unset. */
    snprintf(bm.host, sizeof(bm.host), "manifesthost");

    struct AppConfig cfg = { 0 };
    cfg.cache_dir = "cli_cache_dir";
    cfg.connect_user = "cli_user";
    cfg.spawn_npc_id = -1;
    BootManifest_ApplyToConfig(&bm, &cfg);

    /* Untouched: manifest set neither. */
    CHECK(cfg.cache_dir && strcmp(cfg.cache_dir, "cli_cache_dir") == 0);
    CHECK(cfg.connect_user && strcmp(cfg.connect_user, "cli_user") == 0);
    CHECK(cfg.connect_port == 0);
    CHECK(cfg.spawn_npc_id == -1);
    /* Set: host became the connect target. */
    CHECK(cfg.connect_target && strcmp(cfg.connect_target, "manifesthost") == 0);
}

static void
test_required_identity_keys(void)
{
    struct BootManifest bm;
    CHECK(BootManifest_LoadFile(&bm, "bootmanifest/test/fixture_missing_epoch.ini") != 0);
}

int
main(void)
{
    test_load_fields();
    test_apply_to_config();
    test_partial_apply_preserves_unset();
    test_required_identity_keys();

    if( g_fail )
    {
        fprintf(stderr, "bootmanifest_test: FAILED\n");
        return 1;
    }
    printf("bootmanifest_test: PASS\n");
    return 0;
}
