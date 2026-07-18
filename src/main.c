#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/uitree_builder/task_interface_open.h"
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
#define DEFAULT_INTERFACE_ID 630

static void
seed_inv_defaults(struct InvManager* invs)
{
    static int const k_worn_items[] = { 1153, 1007, 1725, 1333, 1115, 1201,
                                        1189, 1063, 1067, 2564, 882 };
    static int const k_backpack_items[] = { 1333 };

    assert(invs);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_WORN) >= 0);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_BACKPACK) >= 0);

    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_WORN,
        k_worn_items,
        NULL,
        (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0]))));
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BACKPACK,
        k_backpack_items,
        NULL,
        (int)(sizeof(k_backpack_items) / sizeof(k_backpack_items[0]))));
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = CACHE_DIR;
    const char* config_dir = CONFIG_DIR;
    const char* script_dir = SCRIPT_DIR;
    int interface_id = DEFAULT_INTERFACE_ID;

    if( argc >= 2 )
        cache_dir = argv[1];
    if( argc >= 3 )
    {
        interface_id = atoi(argv[2]);
        if( interface_id <= 0 )
        {
            fprintf(stderr, "invalid interface id: %s\n", argv[2]);
            return 1;
        }
    }

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

    seed_inv_defaults(&invs);

    struct RS_CS2Host host;
    RS_CS2Host_Init(&host, tree, provider, &invs, &varps);

    struct InterfaceOpenStats stats;
    memset(&stats, 0, sizeof(stats));

    struct ToriRS_Task* task =
        CreateTask_InterfaceOpen(provider, tree, &host, &invs, interface_id, &stats);
    assert(task != NULL);
    ToriRS_TaskQueue_Add(queue, task);

    while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
    {
        PlatformX_IO_Process(px, io);
    }

    printf(
        "InterfaceOpen done: iface=%d pack_components=%d tree_components=%u onloads=%d "
        "inv_hooks=%d var_hooks=%d\n",
        stats.interface_id,
        stats.pack_component_count,
        tree->component_count,
        stats.onload_count,
        host.inv_transmit_hook_count,
        host.var_transmit_hook_count);

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
