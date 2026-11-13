/*
 * The client's half of replay session checkpoints (replay_checkpoint_play.h).
 */
#include "replay/replay_checkpoint_play.h"

#include "app.h"
#include "platform/net_transport.h"
#include "replay/replay_checkpoint.h"

#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include "log/torirs_log.h"

/* Frames a ready RESTORE may wait for the session to settle before the wait
 * is reported (it keeps waiting). */
#define CKPT_PLAY_RESTORE_PATIENCE 2000

static double
ckpt_play_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

void
ReplayCheckpointPlay_Frame(
    struct App* app,
    struct NetTransport* sock)
{
    static int restore_waited;
    static int trace = -1;
    struct ReplayCheckpoint const* restore;
    char const* why = NULL;
    int tick = 0;
    unsigned long long clock_ms = 0;

    assert(app);
    assert(sock);
    if( trace < 0 )
        trace = getenv("TORIRS_REPLAY_CHECKPOINT_TRACE") != NULL;
    restore = NetTransport_ReplayRestoreWanted(sock);
    if( restore )
    {
        static int refuse = -1;
        double t0;

        /* TORIRS_REPLAY_TEST_REFUSE_RESTORE=1: never restore, so the seek
         * stalls and the scrubber's watchdog has to fail it over to a restart
         * (the stall test, make replay-checkpoint-check). */
        if( refuse < 0 )
            refuse = getenv("TORIRS_REPLAY_TEST_REFUSE_RESTORE") != NULL;
        if( refuse )
            return;

        if( !ReplayCheckpoint_CanTake(app, &why) )
        {
            if( ++restore_waited % CKPT_PLAY_RESTORE_PATIENCE == 0 )
                TORIRS_REPORT("replay: a checkpoint restore has waited %d frames: %s\n", restore_waited, why);
            return;
        }
        restore_waited = 0;
        t0 = ckpt_play_now_ms();
        ReplayCheckpoint_Restore(app, restore);
        NetTransport_ReplayRestored(sock, ckpt_play_now_ms() - t0);
        return;
    }
    if( !NetTransport_ReplayCheckpointWanted(sock, &tick, &clock_ms) )
        return;
    if( !ReplayCheckpoint_CanTake(app, &why) )
    {
        /* A wanted checkpoint the session could not give: said under the
         * trace, the first refusal of a tick and every 256th frame after. */
        static int refused_tick = -1;
        static int refused_frames;

        if( refused_tick != tick )
        {
            refused_tick = tick;
            refused_frames = 0;
        }
        if( trace && refused_frames++ % 256 == 0 )
            TORIRS_REPORT("replay: checkpoint wanted at tick %d, refused (%d frames): %s\n", tick,
                          refused_frames, why ? why : "?");
        return;
    }
    {
        struct ReplayCheckpoint* cp = ReplayCheckpoint_Take(app);

        if( trace )
        {
            int count = 0;
            unsigned long long bytes = 0;

            NetTransport_ReplayCheckpointStats(sock, &count, &bytes);
            TORIRS_REPORT("replay: checkpoint taken at tick %d (%.1f MB); the store held %d (%.1f MB)\n", tick,
                          (double)ReplayCheckpoint_Bytes(cp) / (1024.0 * 1024.0), count,
                          (double)bytes / (1024.0 * 1024.0));
        }
        NetTransport_ReplayCheckpointAdd(sock, tick, clock_ms, cp);
    }
}
