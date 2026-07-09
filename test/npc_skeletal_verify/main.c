#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "ioqueue/libtorirs_io.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "platforms/platform_x/cachelib.h"
#include "platforms/platform_x/cachelib_platform.h"
#include "platforms/platform_x/cache_path_resolve.h"
#include "platforms/platform_x_io_reactor.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
run_npc_skeletal_verify(const char* cache_dir, int npc_id)
{
    struct RSCacheDat2DiskLib* cache = cachelib_new(CACHE_MODE_DAT2);
    struct LibToriPlatformX_IOReactor* reactor = NULL;
    struct ToriAuxLibCore* core = NULL;
    struct ToriAuxLibCache* tal_c = NULL;
    struct Task_ToriAuxLibCache_NpcAdd* load = NULL;
    struct LibToriRS_IOQueue io_queue;
    struct LibToriRS_IOContext ctx;
    int load_state = PT_WAITING;
    int failures = 0;

    if( !cache || cachelib_platform_init(cache, cache_dir) != 1 )
    {
        fprintf(stderr, "npc_skeletal_verify: failed to open cache %s\n", cache_dir);
        return 1;
    }

    reactor = LibToriPlatformX_IOReactorNew(cache);
    core = ToriAuxLibCore_New();
    tal_c = ToriAuxLibCache_New(TORIAUXLIBCACHE_MODE_DAT2, core);
    if( !reactor || !core || !tal_c )
    {
        fprintf(stderr, "npc_skeletal_verify: init failed\n");
        failures = 1;
        goto done;
    }

    load = Task_ToriAuxLibCache_NpcAdd_New(tal_c, npc_id);
    if( !load )
    {
        fprintf(stderr, "npc_skeletal_verify: NpcAdd_New failed\n");
        failures = 1;
        goto done;
    }

    memset(&io_queue, 0, sizeof(io_queue));
    io_queue.current_slot = -1;
    ctx.io = &io_queue;

    {
        int wait_run = -1;
        while( load_state != PT_ENDED && load_state != PT_EXITED )
        {
            if( wait_run >= 0 && !LibToriRS_IOQueueRunComplete(&io_queue, wait_run) )
            {
                LibToriPlatformX_IOReactorProcess(reactor, &io_queue);
                continue;
            }

            int run = LibToriRS_IOQueueBeginRun(&io_queue);
            load_state = Task_ToriAuxLibCache_NpcAdd_Run(load, &ctx);
            if( load_state == PT_YIELDED || load_state == PT_WAITING )
                wait_run = run;
            else
                wait_run = -1;

            if( wait_run >= 0 )
                LibToriPlatformX_IOReactorProcess(reactor, &io_queue);
        }
    }

    if( load_state == PT_EXITED )
    {
        fprintf(stderr, "npc_skeletal_verify: npc %d load exited early\n", npc_id);
        failures = 1;
        goto done;
    }

    {
        struct Dat2BuildCache* dat2_bc = dat2(tal_c);
        struct RSCacheDat2A_ConfigNpctype* npc =
            dat2_buildcache_npctype_get(dat2_bc, npc_id);
        int const anims[] = {
            npc ? npc->standing_animation : -1,
            npc ? npc->walking_animation : -1,
            npc ? npc->rotate180_animation : -1,
            npc ? npc->run_animation : -1,
            npc ? npc->idle_rotate_left_animation : -1,
            npc ? npc->rotate_right_animation : -1,
            npc ? npc->rotate_left_animation : -1,
        };

        for( int i = 0; i < (int)(sizeof(anims) / sizeof(anims[0])); i++ )
        {
            int seq_id = anims[i];
            struct RSCacheDat2A_ConfigSequence* seq;

            if( seq_id == -1 )
                continue;

            seq = dat2_buildcache_sequence_get(dat2_bc, seq_id);
            if( !seq )
            {
                fprintf(
                    stderr,
                    "npc_skeletal_verify: seq_id=%d missing from buildcache\n",
                    seq_id);
                failures++;
                continue;
            }

            if( seq->anim_maya_id >= 0 && seq->frame_count == 0 )
            {
                if( !ToriAuxLibCore_SkeletalAnimHas(core, seq->anim_maya_id) )
                {
                    fprintf(
                        stderr,
                        "npc_skeletal_verify: skeletal missing in core "
                        "(seq_id=%d anim_maya_id=%d)\n",
                        seq_id,
                        seq->anim_maya_id);
                    failures++;
                }
            }
        }
    }

    if( failures == 0 )
        printf("npc_skeletal_verify: npc %d OK (skeletal anims in core)\n", npc_id);

done:
    Task_ToriAuxLibCache_NpcAdd_Free(load);
    ToriAuxLibCache_Free(tal_c);
    ToriAuxLibCore_Free(core);
    LibToriPlatformX_IOReactorFree(reactor);
    cachelib_free(cache);
    return failures != 0;
}

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : cache_path_resolve_osrs_repo();
    int npc_id = argc > 2 ? atoi(argv[2]) : 12204;

    if( !cache_dir )
    {
        fprintf(stderr, "npc_skeletal_verify: cache path not found\n");
        return 1;
    }

    return run_npc_skeletal_verify(cache_dir, npc_id);
}
