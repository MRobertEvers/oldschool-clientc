/*
 * End-to-end dat1 RevConfig build: opens the dat1 cache (cache254), runs
 * CreateTask_UITreeBuild with the rev_245_2 dat1 INIs, then asserts the
 * provider contents and dumps the baked chrome to a BMP.
 *
 * Run from src/: make test-uitree-builder-dat1 [DAT1_CACHE=../cache254]
 */
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/static_sprites.h"
#include "engine/torirs_types.h"
#include "engine/uitree_builder/uitree_builder.h"
#include "engine/uitree_cmd_render.h"
#include "engine/uitree_scene_bridge.h"
#include "input/torirs_keymap.h"
#include "inv/inv_manager.h"
#include "task_runner.h"
#include "ui/uitree.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"

#include "toridraw.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg)                                       \
    do                                                         \
    {                                                          \
        if( cond )                                             \
        {                                                      \
            printf("PASS: %s\n", msg);                         \
        }                                                      \
        else                                                   \
        {                                                      \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            g_failures++;                                      \
        }                                                      \
    } while( 0 )

/* Minimal host: inventory tab selected, static sprites off the bridge. */
static int
test_host_request(
    void* user,
    struct UITreeHostRequest* req)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)user;

    if( req->kind == UITREE_HOST_GET_SELECTED_TAB )
        return 3;
    if( req->kind == UITREE_HOST_GET_SCROLLBAR_SCENE )
        return UITreeSceneBridge_ScrollbarSceneId(bridge);
    if( req->kind == UITREE_HOST_GET_STATIC_SPRITE_SCENE )
        return UITreeSceneBridge_StaticSpriteSceneId(
            bridge, (enum StaticSpriteSlot)req->u.static_sprite.slot);
    return 0;
}

static struct ToriRS_Sprite*
sprite_by_name(
    struct CacheProvider* provider,
    char const* name)
{
    int sprite_id = CacheProvider_SpriteIdByName(provider, name);
    if( sprite_id < 0 )
        return NULL;
    return CacheProvider_SpriteGet(provider, sprite_id);
}

int
main(
    int argc,
    char** argv)
{
    char const* cache_dir = argc > 1 ? argv[1] : "../cache254";
    char const* ui_ini = "engine/uitree_builder/test/rev_245_2_dat1_ui.ini";
    char const* cache_ini = "engine/uitree_builder/test/rev_245_2_dat1_cache.ini";

    ToriDraw_Init();

    struct RSCache_Dat1Disk* disk = RSCache_Dat1DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "SKIP: dat1 cache not found at %s\n", cache_dir);
        return 0;
    }

    struct TaskRunner runner = { 0 };
    runner.io = ToriRS_IO_New();
    runner.queue = ToriRS_TaskQueue_New();
    runner.px = PlatformX_IO_New();
    assert(runner.io && runner.queue && runner.px);
    PlatformX_IO_InitDat1Disk(runner.px, disk);

    struct Dat1BuildCache* bc = dat1_buildcache_new();
    struct CacheProvider* provider = dat1_buildcache_as_provider(bc);

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    assert(scene);
    struct UITreeSceneBridge bridge;
    UITreeSceneBridge_Init(&bridge, scene, provider);

    struct UITree* tree = UITree_New(256);
    assert(tree);
    struct InvManager invs;
    InvManager_Init(&invs);

    struct UITreeBuilder builder;
    UITreeBuilder_InitEx(&builder, provider, tree, &invs, NULL, ui_ini, cache_ini);
    builder.bridge = &bridge;

    ToriRS_TaskQueue_Add(runner.queue, CreateTask_UITreeBuild(&builder));
    TaskRunner_Drain(&runner);

    /* Jagfiles loaded through the CONFIGS-table IO path. */
    CHECK(dat1_buildcache_get_media_2d_graphics_jagfile(bc) != NULL, "media jagfile loaded");
    CHECK(dat1_buildcache_get_title_fonts_jagfile(bc) != NULL, "title jagfile loaded");
    CHECK(dat1_buildcache_get_interfaces_list(bc) != NULL, "interfaces list decoded");

    /* pix8 named sprite. */
    struct ToriRS_Sprite* invback = sprite_by_name(provider, "invback");
    CHECK(invback != NULL, "invback (pix8) decoded");
    if( invback )
        CHECK(
            invback->frames[0].width > 100 && invback->frames[0].height > 100,
            "invback has plausible dimensions");

    /* pix32 + INI crop. */
    struct ToriRS_Sprite* compass = sprite_by_name(provider, "compass");
    CHECK(compass != NULL, "compass (pix32) decoded");
    if( compass )
        CHECK(
            compass->frames[0].width == 34 && compass->frames[0].height == 34,
            "compass cropped to 34x34");

    /* Multi-frame atlas. */
    struct ToriRS_Sprite* sideicons = sprite_by_name(provider, "sideicons");
    CHECK(sideicons != NULL, "sideicons decoded");
    if( sideicons )
        CHECK(sideicons->frame_count == 13, "sideicons has 13 atlas frames");

    /* Flip transform variant registers under its own name. */
    CHECK(sprite_by_name(provider, "redstone1h") != NULL, "redstone1h (flip_h) decoded");

    /* Title fonts pinned at slots 0-3. */
    for( int slot = 0; slot < 4; slot++ )
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "font slot %d loaded", slot);
        CHECK(CacheProvider_FontHas(provider, slot), msg);
    }

    /* Interface component packs from the INI (inventory tab root 3213). */
    CHECK(CacheProvider_ComponentPackHas(provider, 3213), "inventory pack (3213) loaded");
    struct ToriRS_ComponentPack* inv_pack = CacheProvider_ComponentPackGet(provider, 3213);
    if( inv_pack )
        CHECK(inv_pack->component_count > 0, "inventory pack has components");

    CHECK(tree->component_count > 30, "baked tree has chrome nodes");

    /* [hotkey:…] bindings resolved onto real nodes. The INI binds 22 keys (F1
     * through F12 plus the digit row) and one more to the compass, which never
     * advertised select_tab — that last one must not survive. */
    {
        int f3_tab = -1;
        int z_bound = 0;
        int z_key = LibToriRS_OsrsKeyFromName("z");
        int f3_key = LibToriRS_OsrsKeyFromName("f3");

        int well_formed = 0;

        CHECK(tree->hotkey_count == 22, "hotkey bindings resolved");
        for( int i = 0; i < tree->hotkey_count; i++ )
        {
            struct UITreeComponent const* node = &tree->components[tree->hotkeys[i].node_index];
            if( tree->hotkeys[i].effect == UITREE_HOTKEY_EFFECT_SELECT_TAB &&
                node->type == UIELEM_BUILTIN_TAB_ICONS )
                well_formed++;
            if( tree->hotkeys[i].osrs_key == f3_key )
                f3_tab = node->u.tab_icon.tabno;
            if( tree->hotkeys[i].osrs_key == z_key )
                z_bound = 1;
        }
        CHECK(well_formed == tree->hotkey_count, "every binding is select_tab on a tab icon");
        CHECK(f3_tab == 2, "F3 bound to the quest tab");
        CHECK(!z_bound, "binding dropped: compass never advertised select_tab");
    }

    /* Client-hardcoded sprites bound to bridge slots (no owning INI node).
     * mapmarker/mapdots are absent from the RevConfig INI, so they exercise the
     * dat1 media-jagfile load path inside CreateTask_StaticSpritesLoad. */
    {
        static enum StaticSpriteSlot const k_expect[] = {
            STATIC_SPRITE_COMPASS,  STATIC_SPRITE_MAPEDGE, STATIC_SPRITE_MAPSCENE,
            STATIC_SPRITE_MAPFUNCTION, STATIC_SPRITE_HEADICONS, STATIC_SPRITE_HITMARKS,
            STATIC_SPRITE_MAPMARKER, STATIC_SPRITE_CROSS,   STATIC_SPRITE_MAPDOTS,
            STATIC_SPRITE_SCROLLBAR,
        };
        for( size_t i = 0; i < sizeof(k_expect) / sizeof(k_expect[0]); i++ )
        {
            char msg[80];
            snprintf(
                msg, sizeof(msg), "static sprite '%s' bound",
                StaticSprite_SlotName(k_expect[i]));
            CHECK(UITreeSceneBridge_StaticSpriteSceneId(&bridge, k_expect[i]) > 0, msg);
        }
        /* dat2-only archives stay unbound on a dat1 cache. */
        CHECK(
            UITreeSceneBridge_StaticSpriteSceneId(&bridge, STATIC_SPRITE_MOD_ICONS) < 0,
            "dat2-only mod_icons unbound on dat1");
    }

    /* Emit + render the fixed chrome to a BMP for visual inspection. */
    {
        struct UITreeHost ui_host;
        struct UITreeEmitBuffer emit;
        UITree_HostInit(&ui_host);
        ui_host.user = &bridge;
        ui_host.request = test_host_request;
        UITree_EmitBufferInit(&emit);
        UITree_EmitWalk(tree, &ui_host, &emit, -1);
        CHECK(emit.count > 0, "emit walk produced commands");

        int compass_cmds = 0;
        for( int i = 0; i < emit.count; i++ )
        {
            if( emit.cmds[i].kind == UITREE_EMIT_COMPASS && emit.cmds[i].scene_id > 0 )
                compass_cmds++;
        }
        CHECK(compass_cmds > 0, "compass emitted with a scene id");

        int wrote = UITreeCmd_WriteBmp(
            scene,
            emit.cmds,
            emit.count,
            "build/uitree_builder_dat1.bmp",
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H);
        CHECK(wrote == 0, "wrote build/uitree_builder_dat1.bmp");
        UITree_EmitBufferFree(&emit);
    }

    if( g_failures == 0 )
        printf("ALL PASS\n");
    else
        printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
