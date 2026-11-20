/*
 * What the world tells the account database. See torirs_server_hiscoreslog.h.
 */
#include "torirs_server_hiscoreslog.h"

#include "mem/torirs_mem.h"
#include "torirs_server.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(TORIRSSERVER_HISCORESLOG_STATS == TORIRSSERVER_STAT_COUNT,
               "torirs_server_hiscoreslog.h TORIRSSERVER_HISCORESLOG_STATS");
_Static_assert(TORIRSSERVER_HISCORESLOG_VARP_QP < TORIRSSERVER_VARP_COUNT,
               "the quest-point varp is a player varp");

struct ToriRSServerHiscoresLog*
ToriRSServer_HiscoresLogCreate(void)
{
    struct ToriRSServerHiscoresLog* log =
        ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(struct ToriRSServerHiscoresLog));

    assert(log);
    return log;
}

void
ToriRSServer_HiscoresLogFree(struct ToriRSServerHiscoresLog* log)
{
    if( log )
        ToriMem_Free(ToriMem_ServerNet(), log);
}

/* The next record, named and stamped; the caller fills the rest. */
static struct ToriRSServerHiscoresRecord*
log_push(
    struct ToriRSServerHiscoresLog* log,
    enum ToriRSServerHiscoresLogKind kind,
    struct ToriRSServerPlayer const* player,
    long long now)
{
    struct ToriRSServerHiscoresRecord* record;

    assert(log->count < TORIRSSERVER_HISCORESLOG_CAP);
    record = &log->records[log->count++];
    memset(record, 0, sizeof(*record));
    record->kind = kind;
    snprintf(record->name, sizeof(record->name), "%s", player->account_name);
    snprintf(record->display, sizeof(record->display), "%s", player->display_name);
    record->time = now;
    return record;
}

/* The stats into `record`, and into the player's copy of what was last
 * queued. */
static void
log_stats(
    struct ToriRSServerHiscoresRecord* record,
    struct ToriRSServerPlayer* player)
{
    memcpy(record->xp_tenths, player->stat_xp_tenths, sizeof(record->xp_tenths));
    record->quest_points = ToriRSServer_Varp(player, TORIRSSERVER_HISCORESLOG_VARP_QP);
    memcpy(player->hiscores_log_xp_tenths, player->stat_xp_tenths,
           sizeof(player->hiscores_log_xp_tenths));
    player->hiscores_log_quest_points = record->quest_points;
}

void
ToriRSServer_HiscoresLogLogin(
    struct ToriRSServerHiscoresLog* log,
    struct ToriRSServerPlayer* player,
    long long now)
{
    struct ToriRSServerHiscoresRecord* record;

    assert(log);
    assert(player);
    record = log_push(log, TORIRSSERVER_HISCORESLOG_LOGIN, player, now);
    log_stats(record, player);
    record->admin = player->account_admin;
    player->hiscores_log_session_start = now;
    memcpy(player->hiscores_log_session_xp_tenths, player->stat_xp_tenths,
           sizeof(player->hiscores_log_session_xp_tenths));
}

void
ToriRSServer_HiscoresLogStats(
    struct ToriRSServerHiscoresLog* log,
    struct ToriRSServerPlayer* player,
    long long now)
{
    assert(log);
    assert(player);
    if( memcmp(player->hiscores_log_xp_tenths, player->stat_xp_tenths,
               sizeof(player->hiscores_log_xp_tenths)) == 0 &&
        player->hiscores_log_quest_points == ToriRSServer_Varp(player, TORIRSSERVER_HISCORESLOG_VARP_QP) )
        return;
    log_stats(log_push(log, TORIRSSERVER_HISCORESLOG_CHANGED, player, now), player);
}

void
ToriRSServer_HiscoresLogLogout(
    struct ToriRSServerHiscoresLog* log,
    struct ToriRSServerPlayer* player,
    long long now)
{
    struct ToriRSServerHiscoresRecord* record;

    assert(log);
    assert(player);
    record = log_push(log, TORIRSSERVER_HISCORESLOG_LOGOUT, player, now);
    log_stats(record, player);
    record->session_start = player->hiscores_log_session_start;
    memcpy(record->session_xp_tenths, player->hiscores_log_session_xp_tenths,
           sizeof(record->session_xp_tenths));
}

void
ToriRSServer_HiscoresLogEvent(
    struct ToriRSServerHiscoresLog* log,
    struct ToriRSServerPlayer* player,
    long long now,
    enum ToriRSServerAdventureKind event,
    int amount,
    const char* label)
{
    struct ToriRSServerHiscoresRecord* record;

    assert(log);
    assert(player);
    assert(label);
    record = log_push(log, TORIRSSERVER_HISCORESLOG_EVENT, player, now);
    record->event = event;
    record->amount = amount;
    snprintf(record->label, sizeof(record->label), "%s", label);
}
