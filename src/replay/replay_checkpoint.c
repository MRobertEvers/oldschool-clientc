/*
 * Session checkpoints (replay/replay_checkpoint.h): the domain table, the
 * moment test, and the cost report.
 */
#include "replay/replay_checkpoint.h"

#include "app.h"
#include "app/app_checkpoint.h"
#include "game/rs_chat.h"
#include "game/rs_cs2_host.h"
#include "game/rs_clientscript_queue.h"
#include "game/rs_loot_store.h"
#include "inv/inv_manager.h"
#include "mem/torirs_mem.h"
#include "task_runner.h"
#include "ui/uitree.h"
#include "ui/uitree_checkpoint.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log/torirs_log.h"

/* --- the domains ---------------------------------------------------------- */

/*
 * One row per module holding session state. `take` returns a private copy in
 * the module's own allocation group; `restore` puts a copy back, leaving it
 * reusable; `release` accepts NULL.
 */
struct ReplayCkptDomain
{
    char const* name;
    void* (*take)(struct App* app);
    void (*restore)(struct App* app, void const* copy);
    void (*release)(void* copy);
    size_t (*bytes)(void const* copy);
    int (*equal)(void const* a, void const* b, char* why, int why_cap);
};

/* Adapters from the App to each module's store. */
#define CKPT_MODULE_DOMAIN(tag, Type, Prefix, member)                                              \
    static void* ckpt_##tag##_take(struct App* app) { return Prefix##_CheckpointTake(member); }     \
    static void ckpt_##tag##_restore(struct App* app, void const* copy)                            \
    {                                                                                              \
        Prefix##_CheckpointRestore(member, (Type const*)copy);                                     \
    }                                                                                              \
    static void ckpt_##tag##_release(void* copy) { Prefix##_CheckpointFree((Type*)copy); }          \
    static size_t ckpt_##tag##_bytes(void const* copy)                                             \
    {                                                                                              \
        return Prefix##_CheckpointBytes((Type const*)copy);                                        \
    }                                                                                              \
    static int ckpt_##tag##_equal(void const* a, void const* b, char* why, int why_cap)            \
    {                                                                                              \
        return Prefix##_CheckpointEqual((Type const*)a, (Type const*)b, why, why_cap);             \
    }

CKPT_MODULE_DOMAIN(varps, struct VarPManager, VarPManager, &app->varps)
CKPT_MODULE_DOMAIN(varcs, struct VarCManager, VarCManager, &app->varcs)
CKPT_MODULE_DOMAIN(invs, struct InvManager, InvManager, &app->invs)
CKPT_MODULE_DOMAIN(chat, struct RS_Chat, RS_Chat, &app->chat)
CKPT_MODULE_DOMAIN(loot, struct LootStore, LootStore, &app->loot)
CKPT_MODULE_DOMAIN(cs2host, struct RS_CS2Host, RS_CS2Host, &app->host)
CKPT_MODULE_DOMAIN(uitree, struct UITree, UITree, app->tree)
#undef CKPT_MODULE_DOMAIN

/* The App's own field groups (app/app_checkpoint.c). */
#define CKPT_FIELDS_DOMAIN(tag, which)                                                             \
    static void* ckpt_##tag##_take(struct App* app) { return App_CheckpointFieldsTake(app, which); } \
    static void ckpt_##tag##_restore(struct App* app, void const* copy)                            \
    {                                                                                              \
        App_CheckpointFieldsRestore(app, which, copy);                                             \
    }

CKPT_FIELDS_DOMAIN(stats, APP_CKPT_FIELDS_STATS)
CKPT_FIELDS_DOMAIN(social, APP_CKPT_FIELDS_SOCIAL)
CKPT_FIELDS_DOMAIN(slots, APP_CKPT_FIELDS_SLOTS)
CKPT_FIELDS_DOMAIN(idk, APP_CKPT_FIELDS_IDK)
CKPT_FIELDS_DOMAIN(cs2queue, APP_CKPT_FIELDS_CS2QUEUE)
CKPT_FIELDS_DOMAIN(camera, APP_CKPT_FIELDS_CAMERA)
CKPT_FIELDS_DOMAIN(scalars, APP_CKPT_FIELDS_SCALARS)
#undef CKPT_FIELDS_DOMAIN

static void*
ckpt_ifstores_take(struct App* app)
{
    return App_CheckpointIfStoresTake(app);
}

static void*
ckpt_music_take(struct App* app)
{
    return App_CheckpointMusicTake(app);
}

#define CKPT_ROW_MODULE(tag, name)                                                                 \
    { name, ckpt_##tag##_take, ckpt_##tag##_restore, ckpt_##tag##_release, ckpt_##tag##_bytes,     \
      ckpt_##tag##_equal }
#define CKPT_ROW_FIELDS(tag, name)                                                                 \
    { name,                                                                                        \
      ckpt_##tag##_take,                                                                           \
      ckpt_##tag##_restore,                                                                        \
      App_CheckpointFieldsFree,                                                                    \
      App_CheckpointFieldsBytes,                                                                   \
      App_CheckpointFieldsEqual }

static struct ReplayCkptDomain const k_domains[REPLAY_CKPT_DOMAIN_COUNT] = {
    [REPLAY_CKPT_VARPS] = CKPT_ROW_MODULE(varps, "varps"),
    [REPLAY_CKPT_VARCS] = CKPT_ROW_MODULE(varcs, "varcs"),
    [REPLAY_CKPT_INVS] = CKPT_ROW_MODULE(invs, "invs"),
    [REPLAY_CKPT_STATS] = CKPT_ROW_FIELDS(stats, "stats"),
    [REPLAY_CKPT_SOCIAL] = CKPT_ROW_FIELDS(social, "social"),
    [REPLAY_CKPT_SLOTS] = CKPT_ROW_FIELDS(slots, "slots"),
    [REPLAY_CKPT_IDK] = CKPT_ROW_FIELDS(idk, "idk"),
    [REPLAY_CKPT_CHAT] = CKPT_ROW_MODULE(chat, "chat"),
    [REPLAY_CKPT_LOOT] = CKPT_ROW_MODULE(loot, "loot"),
    [REPLAY_CKPT_CS2HOST] = CKPT_ROW_MODULE(cs2host, "cs2host"),
    [REPLAY_CKPT_CS2QUEUE] = CKPT_ROW_FIELDS(cs2queue, "cs2queue"),
    [REPLAY_CKPT_UITREE] = CKPT_ROW_MODULE(uitree, "uitree"),
    [REPLAY_CKPT_IFSTORES] = { "ifstores", ckpt_ifstores_take, App_CheckpointIfStoresRestore,
                               App_CheckpointIfStoresFree, App_CheckpointIfStoresBytes,
                               App_CheckpointIfStoresEqual },
    [REPLAY_CKPT_CAMERA] = CKPT_ROW_FIELDS(camera, "camera"),
    [REPLAY_CKPT_SCALARS] = CKPT_ROW_FIELDS(scalars, "scalars"),
    [REPLAY_CKPT_MUSIC] = { "music", ckpt_music_take, App_CheckpointMusicRestore, App_CheckpointMusicFree,
                            App_CheckpointMusicBytes, App_CheckpointMusicEqual },
};
#undef CKPT_ROW_MODULE
#undef CKPT_ROW_FIELDS

struct ReplayCheckpoint
{
    void* copy[REPLAY_CKPT_DOMAIN_COUNT];
    size_t bytes[REPLAY_CKPT_DOMAIN_COUNT];
    double take_ms[REPLAY_CKPT_DOMAIN_COUNT];
};

static double g_restore_ms[REPLAY_CKPT_DOMAIN_COUNT];
static double g_restore_total_ms;

static double
ckpt_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* --- the moment ----------------------------------------------------------- */

int
ReplayCheckpoint_CanTake(
    struct App const* app,
    char const** why)
{
    char const* reason = NULL;

    assert(app);
    if( !app->tree || app->app_state != APP_STATE_READY )
        reason = "an interface tree is being built";
    else if( app->exec_runner.queue->head )
        reason = "a packet or interface mount is executing";
    else if( app->runner.queue->head )
    {
        static char runner_why[96];

        snprintf(runner_why, sizeof(runner_why), "a task is pending on the asset runner (%.32s)",
                 app->runner.queue->head->name);
        reason = runner_why;
    }
    else if( app->runner.frame_settle_pending || app->runner_had_work || app->exec_runner_had_work )
        reason = "the frame's clientscript transaction has not settled";
    else if( app->server_tick_open )
        reason = "a server tick is half applied";
    else if( RS_ClientScriptQueue_Count(&app->pending_clientscripts) > 0 )
        reason = "a RUNCLIENTSCRIPT is held for the tick fence";
    else if( app->host.has_pending )
        reason = "a clientscript is yielded on a host request";
    else if( app->host.script_callback_running )
        reason = "a clientscript callback is running";
    else if( app->cs1_host.has_pending || app->cs1_host.has_awaited || app->cs1_eval_inflight )
        reason = "a CS1 evaluation is in flight";
    else if( app->inv_icon_reconcile_inflight )
        reason = "an inventory icon pass is in flight";
    else if( app->host.triggeroplocal_count > 0 || app->host.close_modal_requested ||
             app->host.resume_pausebutton_component_id != -1 || app->host.social_send_count > 0 )
        reason = "a clientscript's request to the App is not drained";
    if( why )
        *why = reason;
    return reason == NULL;
}

/* --- take, restore, free -------------------------------------------------- */

struct ReplayCheckpoint*
ReplayCheckpoint_Take(struct App* app)
{
    struct ReplayCheckpoint* cp;

    assert(app);
    assert(ReplayCheckpoint_CanTake(app, NULL));
    cp = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*cp));
    assert(cp);
    for( int d = 0; d < REPLAY_CKPT_DOMAIN_COUNT; d++ )
    {
        double const t0 = ckpt_now_ms();

        cp->copy[d] = k_domains[d].take(app);
        assert(cp->copy[d]);
        cp->take_ms[d] = ckpt_now_ms() - t0;
        cp->bytes[d] = k_domains[d].bytes(cp->copy[d]);
    }
    return cp;
}

void
ReplayCheckpoint_Restore(
    struct App* app,
    struct ReplayCheckpoint const* cp)
{
    double const start = ckpt_now_ms();

    assert(app);
    assert(cp);
    assert(ReplayCheckpoint_CanTake(app, NULL));
    for( int d = 0; d < REPLAY_CKPT_DOMAIN_COUNT; d++ )
    {
        double const t0 = ckpt_now_ms();

        k_domains[d].restore(app, cp->copy[d]);
        g_restore_ms[d] = ckpt_now_ms() - t0;
    }
    App_CheckpointAfterRestore(app);
    g_restore_total_ms = ckpt_now_ms() - start;
}

void
ReplayCheckpoint_Free(struct ReplayCheckpoint* cp)
{
    if( !cp )
        return;
    for( int d = 0; d < REPLAY_CKPT_DOMAIN_COUNT; d++ )
        k_domains[d].release(cp->copy[d]);
    ToriMem_Free(ToriMem_Session(), cp);
}

/* --- what it holds -------------------------------------------------------- */

size_t
ReplayCheckpoint_Bytes(struct ReplayCheckpoint const* cp)
{
    size_t bytes;

    assert(cp);
    bytes = sizeof(*cp);
    for( int d = 0; d < REPLAY_CKPT_DOMAIN_COUNT; d++ )
        bytes += cp->bytes[d];
    return bytes;
}

int
ReplayCheckpoint_Equal(
    struct ReplayCheckpoint const* a,
    struct ReplayCheckpoint const* b,
    char* why,
    int why_cap)
{
    assert(a);
    assert(b);
    if( why && why_cap > 0 )
        why[0] = '\0';
    for( int d = 0; d < REPLAY_CKPT_DOMAIN_COUNT; d++ )
    {
        char field[512];

        field[0] = '\0';
        if( !k_domains[d].equal(a->copy[d], b->copy[d], field, (int)sizeof(field)) )
        {
            if( why && why_cap > 0 )
                snprintf(why, (size_t)why_cap, "%s: %s", k_domains[d].name, field[0] ? field : "(differs)");
            return 0;
        }
    }
    return 1;
}

int
ReplayCheckpoint_EqualDomain(
    struct ReplayCheckpoint const* a,
    struct ReplayCheckpoint const* b,
    int domain,
    char* why,
    int why_cap)
{
    assert(a);
    assert(b);
    assert(domain >= 0);
    assert(domain < REPLAY_CKPT_DOMAIN_COUNT);
    if( why && why_cap > 0 )
        why[0] = '\0';
    return k_domains[domain].equal(a->copy[domain], b->copy[domain], why, why_cap);
}

char const*
ReplayCheckpoint_DomainName(int domain)
{
    assert(domain >= 0);
    assert(domain < REPLAY_CKPT_DOMAIN_COUNT);
    return k_domains[domain].name;
}

size_t
ReplayCheckpoint_DomainBytes(
    struct ReplayCheckpoint const* cp,
    int domain)
{
    assert(cp);
    assert(domain >= 0);
    assert(domain < REPLAY_CKPT_DOMAIN_COUNT);
    return cp->bytes[domain];
}

double
ReplayCheckpoint_DomainTakeMs(
    struct ReplayCheckpoint const* cp,
    int domain)
{
    assert(cp);
    assert(domain >= 0);
    assert(domain < REPLAY_CKPT_DOMAIN_COUNT);
    return cp->take_ms[domain];
}

double
ReplayCheckpoint_LastRestoreMs(int domain)
{
    assert(domain >= 0);
    assert(domain < REPLAY_CKPT_DOMAIN_COUNT);
    return g_restore_ms[domain];
}

double
ReplayCheckpoint_LastRestoreTotalMs(void)
{
    return g_restore_total_ms;
}

void
ReplayCheckpoint_PrintCost(
    struct ReplayCheckpoint const* cp,
    char const* label)
{
    double take_total = 0.0;

    assert(cp);
    assert(label);
    for( int d = 0; d < REPLAY_CKPT_DOMAIN_COUNT; d++ )
    {
        take_total += cp->take_ms[d];
        TORIRS_REPORT("replay: checkpoint cost %s %-9s %9zu bytes  take %7.3f ms  restore %7.3f ms\n", label,
                      k_domains[d].name, cp->bytes[d], cp->take_ms[d], g_restore_ms[d]);
    }
    TORIRS_REPORT("replay: checkpoint cost %s total     %9zu bytes  take %7.3f ms  restore %7.3f ms\n", label,
                  ReplayCheckpoint_Bytes(cp), take_total, g_restore_total_ms);
}
