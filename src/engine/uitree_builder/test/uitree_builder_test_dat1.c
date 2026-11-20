/*
 * End-to-end dat1 RevConfig build: opens the dat1 cache (cache254), runs
 * CreateTask_UITreeBuild with the rs245_2lc dat1 INIs, then asserts the
 * provider contents and dumps the baked chrome to a BMP.
 *
 * Run from src/: make test-uitree-builder-dat1 [DAT1_CACHE=../cache254]
 */
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/static_sprites.h"
#include "engine/torirs_types.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_builder/task_pack_assets_load.h"
#include "engine/uitree_builder/uitree_builder.h"
#include "engine/uitree_builder/uitree_builder_bake.h"
#include "engine/uitree_builder/uitree_builder_manifest.h"
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

/* The asset queue drained, then the compute queue run, until neither moves:
 * the jagfile decode is a background job its loader joins
 * (Task_Dat1ComponentPackLoad, CacheProvider::compute_queue), as App_RunOnce
 * drains the one and gives the other its slice. */
static void
drain_both(
    struct TaskRunner* runner,
    struct TaskRunner* background)
{
    for( int round = 0; round < 1000; round++ )
    {
        TaskRunner_Drain(runner);
        if( !background->queue->head )
            return;
        (void)TaskRunner_RunUntil(background, 0);
    }
    assert(!"drain_both: the queues never settled");
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
    char const* ui_ini = "engine/uitree_builder/test/rs245_2lc_dat1_ui.ini";
    char const* cache_ini = "engine/uitree_builder/test/rs245_2lc_dat1_cache.ini";

    ToriDraw_Init();

    struct RSCache_Dat1Disk* disk = RSCache_Dat1DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "SKIP: dat1 cache not found at %s\n", cache_dir);
        return 0;
    }

    /* Parallel, as the client's asset runner is: the builder fans its loads out
     * as siblings behind itself and joins them, which strict FIFO never steps. */
    struct TaskRunner runner = { .parallel = 1 };
    runner.io = ToriRS_IOBatch_New();
    runner.queue = ToriRS_TaskQueue_New();
    runner.px = PlatformX_IO_New();
    assert(runner.io && runner.queue && runner.px);
    PlatformX_IO_InitDat1Disk(runner.px, disk);
    struct TaskRunner background = { .parallel = 1, .role = TASK_RUNNER_ROLE_BACKGROUND };
    background.io = ToriRS_IOBatch_New();
    background.queue = ToriRS_TaskQueue_New();
    background.px = runner.px;
    assert(background.io);
    assert(background.queue);

    struct Dat1BuildCache* bc = dat1_buildcache_new();
    struct CacheProvider* provider = dat1_buildcache_as_provider(bc);
    CacheProvider_SetAssetQueue(provider, runner.queue);
    CacheProvider_SetComputeQueue(provider, background.queue);

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    assert(scene);
    struct UITreeSceneBridge bridge;
    UITreeSceneBridge_Init(&bridge, scene, provider);

    struct UITree* tree = UITree_New(256);
    assert(tree);
    struct InvManager invs;
    InvManager_Init(&invs);

    struct UITreeBuilder builder;
    UITreeBuilder_InitCS1(&builder, provider, tree, &invs, ui_ini, cache_ini);
    builder.bridge = &bridge;

    ToriRS_TaskQueue_Add(runner.queue, CreateTask_UITreeBuild(&builder));
    drain_both(&runner, &background);

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

    /* pix32, uncropped: the compass keeps its native 51x51 canvas so the
     * rotated blit pivots on its centre pixel. */
    struct ToriRS_Sprite* compass = sprite_by_name(provider, "compass");
    CHECK(compass != NULL, "compass (pix32) decoded");
    if( compass )
        CHECK(
            compass->frames[0].width == 51 && compass->frames[0].height == 51,
            "compass keeps its native 51x51 canvas");

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

    /* Overlay presence is a RevConfig property: the builder must bake exactly
     * one of each configured node, with no app-side injection required. */
    {
        int entity_overlays = 0;
        int crosses = 0;
        int hovertexts = 0;
        int minimenus = 0;
        for( uint32_t i = 0; i < tree->component_count; i++ )
        {
            switch( tree->components[i].type )
            {
            case UIELEM_BUILTIN_ENTITY_OVERLAY:
                entity_overlays++;
                break;
            case UIELEM_BUILTIN_CROSS:
                crosses++;
                break;
            case UIELEM_BUILTIN_HOVERTEXT:
                hovertexts++;
                break;
            case UIELEM_BUILTIN_MINIMENU:
                minimenus++;
                break;
            default:
                break;
            }
        }
        CHECK(entity_overlays == 1, "one RevConfig entity overlay node");
        CHECK(crosses == 1, "one RevConfig cross node");
        CHECK(hovertexts == 1, "one RevConfig hovertext node");
        CHECK(minimenus == 1, "one RevConfig minimenu node");
    }

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

    /* Sprites the client draws itself, bound to bridge slots. They have no
     * owning [component:] node, but every one of them IS declared as a
     * [sprite:] section -- that is the whole binding, and a slot whose section
     * is missing stays unbound. mapmarker/mapdots/scrollbar are the three that
     * used to be named only in C. */
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

    /*
     * A slot mount's models on a dat1 cache (step lo-mounts). The mount waits
     * for the pack and what a later packet reads of it
     * (CreateTask_PackAssetsLoadRequired), not its models; the client binds
     * those when they arrive. Find a dat1 interface that shows a model, mount
     * it the way the slot mount does, and establish where its model nodes
     * are: NOT in an interface group (a dat1 component id is flat, so the
     * group a dat2 id carries in its high half is 0 -- the sub mount's group
     * binder finds nothing), but under the slot, where
     * InterfaceOpen_SubtreeModelNodes finds them.
     */
    {
        struct RSCache_Dat1ConfigComponentList* list = dat1_buildcache_get_interfaces_list(bc);
        int iface = -1;
        int model_id = -1;

        for( int i = 0; list && i < list->components_count && iface <= 0; i++ )
        {
            struct RSCache_Dat1ConfigComponent* c = list->components[i];
            struct RSCache_Dat1ConfigComponent* root;
            int hops = 0;

            if( !c || c->modelType != 1 || c->model < 0 )
                continue;
            root = c;
            while( root->layer >= 0 && root->layer != root->id && root->layer < list->components_count &&
                   list->components[root->layer] && hops++ < 64 )
                root = list->components[root->layer];
            /* The model must not be in the cache yet (the build may have
             * loaded it for another pack). */
            if( CacheProvider_ModelHas(provider, c->model) || root->id <= 0 )
                continue;
            iface = root->id;
            model_id = c->model;
        }
        CHECK(iface > 0, "a dat1 interface that shows a model");
        if( iface > 0 )
        {
            struct UITreeNodeSpec spec;
            struct ToriRS_ComponentPack* pack;
            int32_t owner;
            int32_t nodes[64];
            int count;
            int wanting = -1;

            ToriRS_TaskQueue_Add(
                runner.queue, CreateTask_ComponentLoad(provider, uibuilder_pack_component_id(iface)));
            drain_both(&runner, &background);
            {
                struct ToriRS_Task* required = CreateTask_PackAssetsLoadRequired(provider, iface);
                if( required )
                    ToriRS_TaskQueue_Add(runner.queue, required);
                drain_both(&runner, &background);
            }
            pack = CacheProvider_ComponentPackGet(provider, iface);
            CHECK(pack != NULL, "its pack loaded");
            CHECK(!CacheProvider_ModelHas(provider, model_id), "the required load leaves its model out");

            memset(&spec, 0, sizeof(spec));
            spec.type = UIELEM_RS_LAYER;
            spec.component_id = (TORIRS_REVCONFIG_GROUP << 16) | 0x7ff0;
            spec.width = 512;
            spec.height = 334;
            owner = UITree_Push(tree, -1, &spec);
            assert(owner >= 0);
            if( pack )
                uitree_builder_bake_pack_under_owner(tree, &builder, pack, owner, -1);

            CHECK(!UITree_GroupPresent(tree, iface), "a dat1 interface's nodes are in no group of its id");
            CHECK(InterfaceOpen_BindGroupModels(tree, &bridge, iface) == 0,
                  "so the sub mount's group binder finds none of them");
            count = InterfaceOpen_SubtreeModelNodes(tree, owner, nodes, 64);
            CHECK(count > 0 && count <= 64, "the subtree under the slot finds its model widgets");
            for( int i = 0; i < count && i < 64; i++ )
            {
                struct InterfaceOpenModelWant wants[INTERFACE_OPEN_NODE_WANTS_MAX];
                int n = InterfaceOpen_NodeModelWants(tree, provider, &bridge, nodes[i], wants);
                for( int w = 0; w < n; w++ )
                    if( wants[w].kind == INTERFACE_OPEN_WANT_MODEL && wants[w].model_id == model_id )
                        wanting = nodes[i];
            }
            CHECK(wanting >= 0, "and says the widget waits for its model");

            /* The model arrives: the node binds it, and waits for nothing. */
            ToriRS_TaskQueue_Add(runner.queue, CreateTask_ModelLoad(provider, model_id));
            drain_both(&runner, &background);
            CHECK(CacheProvider_ModelHas(provider, model_id), "the model loaded");
            if( wanting >= 0 )
            {
                struct InterfaceOpenModelWant wants[INTERFACE_OPEN_NODE_WANTS_MAX];
                CHECK(InterfaceOpen_BindModelNode(tree, &bridge, wanting) == 1, "it binds onto the dat1 widget");
                CHECK(InterfaceOpen_NodeModelWants(tree, provider, &bridge, wanting, wants) == 0,
                      "which then waits for nothing");
            }
            printf("dat1 slot mount: iface %d, model %d, %d model widget(s) under the slot\n",
                   iface, model_id, count);
        }
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
