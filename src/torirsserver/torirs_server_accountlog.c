/*
 * What the world tells the account database. See torirs_server_accountlog.h.
 */
#include "torirs_server_accountlog.h"

#include "mem/torirs_mem.h"
#include "torirs_server.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(TORIRSSERVER_ACCOUNTLOG_STATS == TORIRSSERVER_STAT_COUNT,
               "torirs_server_accountlog.h TORIRSSERVER_ACCOUNTLOG_STATS");
_Static_assert(TORIRSSERVER_ACCOUNTLOG_VARP_QP < TORIRSSERVER_VARP_COUNT,
               "the quest-point varp is a player varp");

struct ToriRSServerAccountLog*
ToriRSServer_AccountLogCreate(void)
{
    struct ToriRSServerAccountLog* log =
        ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(struct ToriRSServerAccountLog));

    assert(log);
    return log;
}

void
ToriRSServer_AccountLogFree(struct ToriRSServerAccountLog* log)
{
    if( log )
        ToriMem_Free(ToriMem_ServerNet(), log);
}

/* The next record, named and stamped; the caller fills the rest. */
static struct ToriRSServerAccountRecord*
log_push(
    struct ToriRSServerAccountLog* log,
    enum ToriRSServerAccountLogKind kind,
    struct ToriRSServerPlayer const* player,
    long long now)
{
    struct ToriRSServerAccountRecord* record;

    assert(log->count < TORIRSSERVER_ACCOUNTLOG_CAP);
    record = &log->records[log->count++];
    memset(record, 0, sizeof(*record));
    record->kind = kind;
    snprintf(record->name, sizeof(record->name), "%s", player->display_name);
    record->time = now;
    return record;
}

/* The stats into `record`, and into the player's copy of what was last
 * queued. */
static void
log_stats(
    struct ToriRSServerAccountRecord* record,
    struct ToriRSServerPlayer* player)
{
    memcpy(record->xp_tenths, player->stat_xp_tenths, sizeof(record->xp_tenths));
    record->quest_points = player->varps[TORIRSSERVER_ACCOUNTLOG_VARP_QP];
    memcpy(player->account_log_xp_tenths, player->stat_xp_tenths,
           sizeof(player->account_log_xp_tenths));
    player->account_log_quest_points = record->quest_points;
}

void
ToriRSServer_AccountLogLogin(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now)
{
    struct ToriRSServerAccountRecord* record;

    assert(log);
    assert(player);
    record = log_push(log, TORIRSSERVER_ACCOUNTLOG_LOGIN, player, now);
    log_stats(record, player);
    record->admin = player->account_admin;
    player->account_log_session_start = now;
    memcpy(player->account_log_session_xp_tenths, player->stat_xp_tenths,
           sizeof(player->account_log_session_xp_tenths));
}

void
ToriRSServer_AccountLogStats(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now)
{
    assert(log);
    assert(player);
    if( memcmp(player->account_log_xp_tenths, player->stat_xp_tenths,
               sizeof(player->account_log_xp_tenths)) == 0 &&
        player->account_log_quest_points == player->varps[TORIRSSERVER_ACCOUNTLOG_VARP_QP] )
        return;
    log_stats(log_push(log, TORIRSSERVER_ACCOUNTLOG_CHANGED, player, now), player);
}

void
ToriRSServer_AccountLogLogout(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now)
{
    struct ToriRSServerAccountRecord* record;

    assert(log);
    assert(player);
    record = log_push(log, TORIRSSERVER_ACCOUNTLOG_LOGOUT, player, now);
    log_stats(record, player);
    record->session_start = player->account_log_session_start;
    memcpy(record->session_xp_tenths, player->account_log_session_xp_tenths,
           sizeof(record->session_xp_tenths));
}

void
ToriRSServer_AccountLogEvent(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now,
    enum ToriRSServerAdventureKind event,
    int amount,
    const char* label)
{
    struct ToriRSServerAccountRecord* record;

    assert(log);
    assert(player);
    assert(label);
    record = log_push(log, TORIRSSERVER_ACCOUNTLOG_EVENT, player, now);
    record->event = event;
    record->amount = amount;
    snprintf(record->label, sizeof(record->label), "%s", label);
}
