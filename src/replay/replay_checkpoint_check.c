/*
 * TORIRS_REPLAY_CHECKPOINT_CHECK=K,N (replay/replay_checkpoint_check.h).
 */
#include "replay/replay_checkpoint_check.h"

#include "app.h"
#include "platform/net_transport.h"
#include "replay/replay_checkpoint.h"
#include "replay/replay_digest.h"
#include "ui/uitree.h"
#include "ui/uitree_hook.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/torirs_log.h"

/* Frames the session must stay checkpointable before it is copied: a tick's
 * held scripts and their follow-ups land over the frames after its fence. */
#define CKPT_CHECK_SETTLE_FRAMES 3
/* Frames to wait for that before the check gives up and says why. */
#define CKPT_CHECK_PATIENCE_FRAMES 20000

enum CkptCheckStage
{
    CKPT_CHECK_OFF = 0,
    CKPT_CHECK_WAIT_K,
    CKPT_CHECK_PLAY_1,
    CKPT_CHECK_PLAY_2,
    CKPT_CHECK_DONE,
};

static struct
{
    int parsed;
    enum CkptCheckStage stage;
    int tick;
    int n;
    int settle;
    int waited;
    struct ReplayCheckpoint* a;
    struct ReplayCheckpoint* b;
    struct ReplayDigest digest_b;
    /* The client clock (RS_CS2Host.client_clock) at B, for the report. */
    int clock_b;
    /* The view at B: not session state, but what world-projected scripts read. */
    int yaw_b, pitch_b, zoom_b, coord_b;
    /* The replay clock when A was taken: the reposition puts it back. */
    unsigned long long replay_clock_a;
    /* TORIRS_REPLAY_CHECKPOINT_TRACE: say what the wait is waiting on. */
    int trace;
} g_check;

static void
ckpt_check_parse(void)
{
    char const* env = getenv("TORIRS_REPLAY_CHECKPOINT_CHECK");

    g_check.parsed = 1;
    g_check.stage = CKPT_CHECK_OFF;
    if( !env || !env[0] )
        return;
    if( sscanf(env, "%d,%d", &g_check.tick, &g_check.n) != 2 || g_check.tick < 1 || g_check.n < 1 )
    {
        TORIRS_ERR("replay: TORIRS_REPLAY_CHECKPOINT_CHECK=%s is not K,N\n", env);
        return;
    }
    g_check.stage = CKPT_CHECK_WAIT_K;
    g_check.trace = getenv("TORIRS_REPLAY_CHECKPOINT_TRACE") != NULL;
    TORIRS_REPORT("replay: checkpoint check armed: tick %d, then %d ticks twice\n", g_check.tick, g_check.n);
}

/* The session has been checkpointable for CKPT_CHECK_SETTLE_FRAMES frames in
 * a row. Says why not when the wait runs out. */
static int
ckpt_check_settled(struct App* app)
{
    char const* why = NULL;

    if( !ReplayCheckpoint_CanTake(app, &why) )
    {
        g_check.settle = 0;
        if( g_check.trace && (g_check.waited == 10 || g_check.waited == 1000) )
            TORIRS_REPORT("replay: checkpoint trace: waiting at clock %d: %s\n", app->host.client_clock, why);
        if( ++g_check.waited % CKPT_CHECK_PATIENCE_FRAMES == 0 )
            TORIRS_REPORT("replay: checkpoint check still waiting after %d frames: %s\n", g_check.waited, why);
        return 0;
    }
    return ++g_check.settle >= CKPT_CHECK_SETTLE_FRAMES;
}

static int
ckpt_check_end(void)
{
    ReplayCheckpoint_Free(g_check.a);
    ReplayCheckpoint_Free(g_check.b);
    g_check.a = NULL;
    g_check.b = NULL;
    g_check.stage = CKPT_CHECK_DONE;
    return 1;
}

/* The session digests (replay/replay_digest.h) of B and B2, printed. */
static void
ckpt_check_digests(
    struct ReplayDigest const* b,
    struct ReplayDigest const* b2)
{
    static int const k_session[] = { REPLAY_DIGEST_VARPS, REPLAY_DIGEST_INVS, REPLAY_DIGEST_STATS, REPLAY_DIGEST_UI,
                                     REPLAY_DIGEST_CS2 };
    char line[512];
    int at = 0;
    int same = 1;

    for( size_t i = 0; i < sizeof(k_session) / sizeof(k_session[0]); i++ )
    {
        int const d = k_session[i];

        same = same && b->v[d] == b2->v[d];
        at += snprintf(line + at, sizeof(line) - (size_t)at, " %s=%s", ReplayDigest_DomainName(d),
                       b->v[d] == b2->v[d] ? "same" : "DIFFERS");
    }
    TORIRS_REPORT("replay: checkpoint check digests%s (%s)\n", line, same ? "all session domains equal" : "differ");
}

int
ReplayCheckpointCheck_Frame(
    struct App* app,
    struct NetTransport* sock,
    int seeking)
{
    struct NetTransportReplayStatus st;
    char why[512];

    assert(app);
    assert(sock);
    if( !g_check.parsed )
        ckpt_check_parse();
    if( g_check.stage == CKPT_CHECK_OFF || g_check.stage == CKPT_CHECK_DONE || seeking )
        return 0;
    NetTransport_ReplayGetStatus(sock, &st);
    if( strcmp(st.phase, "playing") != 0 )
        return 0;

    if( g_check.stage == CKPT_CHECK_WAIT_K )
    {
        struct ReplayCheckpoint* a2;

        if( !st.held || st.tick != g_check.tick || !ckpt_check_settled(app) )
            return 0;
        g_check.a = ReplayCheckpoint_Take(app);
        g_check.replay_clock_a = st.clock_ms;
        ReplayCheckpoint_Restore(app, g_check.a);
        a2 = ReplayCheckpoint_Take(app);
        why[0] = '\0';
        if( ReplayCheckpoint_Equal(g_check.a, a2, why, (int)sizeof(why)) )
            TORIRS_REPORT("replay: checkpoint roundtrip tick=%d PASS\n", g_check.tick);
        else
            TORIRS_REPORT("replay: checkpoint roundtrip tick=%d FAIL %s\n", g_check.tick, why);
        ReplayCheckpoint_PrintCost(g_check.a, "A");
        ReplayCheckpoint_Free(a2);
        if( g_check.tick + g_check.n > st.tick_count )
        {
            TORIRS_REPORT("replay: checkpoint check tick=%d n=%d FAIL the recording has %d ticks\n", g_check.tick,
                          g_check.n, st.tick_count);
            return ckpt_check_end();
        }
        g_check.settle = 0;
        g_check.waited = 0;
        g_check.stage = CKPT_CHECK_PLAY_1;
        /* Pass 1 goes the way pass 2 will -- A restored, the world keyframed
         * at K, N ticks in full -- from a client that is AT K, so the two
         * passes differ in one thing only: what the session held outside A
         * (its K value here, its K+N value in pass 2). */
        NetTransport_ReplaySeekFromCheckpoint(sock, g_check.tick + g_check.n, g_check.a, g_check.tick,
                                              g_check.replay_clock_a, g_check.n);
        return 0;
    }

    if( g_check.stage == CKPT_CHECK_PLAY_1 )
    {
        if( !st.held || st.tick != g_check.tick + g_check.n || !ckpt_check_settled(app) )
            return 0;
        ReplayDigest_Take(app, &g_check.digest_b);
        g_check.b = ReplayCheckpoint_Take(app);
        g_check.clock_b = app->host.client_clock;
        g_check.yaw_b = app->views[0].orbit.yaw;
        g_check.pitch_b = app->views[0].orbit.pitch;
        g_check.zoom_b = app->views[0].world_cam_zoom;
        g_check.coord_b = app->host.local_coord;
        /* Back to K through the scrubber's own restore: the checkpoint put
         * back, the world keyframed at K by the ledger, every packet of the N
         * ticks played again (a pre-roll of N). */
        NetTransport_ReplaySeekFromCheckpoint(sock, g_check.tick + g_check.n, g_check.a, g_check.tick,
                                              g_check.replay_clock_a, g_check.n);
        g_check.settle = 0;
        g_check.waited = 0;
        g_check.stage = CKPT_CHECK_PLAY_2;
        return 0;
    }
    if( g_check.stage == CKPT_CHECK_PLAY_2 )
    {
        struct ReplayCheckpoint* b2;
        struct ReplayDigest digest_b2;

        if( !st.held || st.tick != g_check.tick + g_check.n || !ckpt_check_settled(app) )
            return 0;
        b2 = ReplayCheckpoint_Take(app);
        ReplayDigest_Take(app, &digest_b2);
        why[0] = '\0';
        if( ReplayCheckpoint_Equal(g_check.b, b2, why, (int)sizeof(why)) )
            TORIRS_REPORT("replay: checkpoint check tick=%d n=%d PASS (%zu bytes)\n", g_check.tick, g_check.n,
                          ReplayCheckpoint_Bytes(b2));
        else
            TORIRS_REPORT("replay: checkpoint check tick=%d n=%d FAIL %s (client clock %d at B, %d at B2)\n",
                          g_check.tick, g_check.n, why, g_check.clock_b, app->host.client_clock);
        /* Every domain that differs, not only the first. */
        for( int d = 0; d < REPLAY_CKPT_DOMAIN_COUNT; d++ )
            if( !ReplayCheckpoint_EqualDomain(g_check.b, b2, d, why, (int)sizeof(why)) )
                TORIRS_REPORT("replay: checkpoint check   %s: %s\n", ReplayCheckpoint_DomainName(d), why);
        ckpt_check_digests(&g_check.digest_b, &digest_b2);
        TORIRS_REPORT("replay: checkpoint check view: yaw %d pitch %d zoom %d coord %d at B, yaw %d pitch %d zoom "
                      "%d coord %d at B2\n",
                      g_check.yaw_b, g_check.pitch_b, g_check.zoom_b, g_check.coord_b, app->views[0].orbit.yaw,
                      app->views[0].orbit.pitch, app->views[0].world_cam_zoom, app->host.local_coord);
        ReplayCheckpoint_Free(b2);
        {
            /* TORIRS_REPLAY_CHECKPOINT_NODE=i: what node i of the live tree is,
             * for chasing a difference the check named. */
            char const* env = getenv("TORIRS_REPLAY_CHECKPOINT_NODE");
            int const idx = env ? atoi(env) : -1;

            if( idx >= 0 && (uint32_t)idx < app->tree->component_count )
            {
                struct UITreeComponent const* c = &app->tree->components[idx];

                TORIRS_REPORT("replay: checkpoint node %d: com 0x%08x %s pos %d,%d %dx%d modes %d/%d dyn %d "
                              "first_child %d next %d hooks %p plugin_owner %llu key %s\n",
                              idx, (unsigned)c->component_id, UITree_ComponentTypeStr(c->type), c->position.x,
                              c->position.y, c->position.width, c->position.height, c->position.x_mode,
                              c->position.y_mode, c->dynamic, c->first_child, c->next_sibling,
                              (void*)c->runtime_hooks, (unsigned long long)c->plugin_owner,
                              c->plugin_key ? c->plugin_key : "-");
                if( c->runtime_hooks )
                    for( int h = 0; h < UITree_HooksSlotCount(); h++ )
                        if( UITree_HooksSlotAtConst(c->runtime_hooks, h)->script_id > 0 )
                            TORIRS_REPORT("replay: checkpoint node %d hook %s script %d\n", idx,
                                          UITree_HooksSlotName(h),
                                          UITree_HooksSlotAtConst(c->runtime_hooks, h)->script_id);
            }
        }
        return ckpt_check_end();
    }
    return 0;
}
