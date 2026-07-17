#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/uitree_builder/uitree_builder.h"
#include "game/rs_cs2_host.h"
#include "inv/inv_manager.h"
#include "platform/platform_x_io.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.jan2026"
#define CONFIG_DIR "/Users/matthewevers/Documents/git_repos/3draster/config"
#define SCRIPT_DIR "/Users/matthewevers/Documents/git_repos/3draster/script"
#define UI_INI_PATH \
    "/Users/matthewevers/Documents/git_repos/3draster/v0/osrs/revconfig/configs/rev_245_2/rev_osrs_ui.ini"
#define UI_CACHE_INI_PATH \
    "/Users/matthewevers/Documents/git_repos/3draster/v0/osrs/revconfig/configs/rev_245_2/rev_osrs_ui_cache.ini"

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = CACHE_DIR;
    const char* config_dir = CONFIG_DIR;
    const char* script_dir = SCRIPT_DIR;
    const char* ui_ini = UI_INI_PATH;
    const char* cache_ini = UI_CACHE_INI_PATH;

    if( argc >= 2 )
        cache_dir = argv[1];
    if( argc >= 3 )
        ui_ini = argv[2];
    if( argc >= 4 )
        cache_ini = argv[3];

    struct ToriRS_IO* io = ToriRS_IO_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    assert(disk != NULL);
    struct PlatformX_IO* px = PlatformX_IO_New();
    assert(px != NULL);
    PlatformX_IO_InitDat2Disk(px, disk);
    PlatformX_IO_InitConfigPath(px, config_dir);
    PlatformX_IO_InitScriptPath(px, script_dir);

    struct UITree* tree = UITree_New(256);
    assert(tree);
    struct InvManager invs;
    InvManager_Init(&invs);
    struct VarPManager varps;
    VarPManager_Init(&varps);

    struct RS_CS2Host host;
    RS_CS2Host_Init(&host, tree, provider, &invs, &varps);

    struct UITreeBuilder builder;
    UITreeBuilder_InitEx(&builder, provider, tree, &invs, &host, ui_ini, cache_ini);

    struct ToriRS_Task* task = CreateTask_UITreeBuild(&builder);
    assert(task != NULL);
    ToriRS_TaskQueue_Add(queue, task);

    while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
    {
        PlatformX_IO_Process(px, io);
    }

    printf(
        "UITreeBuild done: components=%u roots=%d sprites=%d fonts=%d onloads=%d\n",
        tree->component_count,
        tree->root_index,
        builder.sprite_count,
        builder.font_count,
        builder.onload_count);

    UITreeBuilder_Free(&builder);
    UITree_Free(tree);
    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    dat2_buildcache_free(bc);

    (void)script_dir;
    return 0;
}
