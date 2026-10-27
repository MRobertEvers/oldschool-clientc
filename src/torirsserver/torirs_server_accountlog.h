#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_ACCOUNTLOG_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_ACCOUNTLOG_H

/*
 * What the world tells the account database (torirs_server_accounts.h): a
 * login, a player's stats as they change, a logout, a line of the adventure
 * log. The account site's hiscores and history are built from these.
 *
 * The world only queues them. It is also compiled into the client and the
 * browser build, which have no database, so the records are plain structs in
 * a fixed queue the world owns (`ToriRSServer.account_log`) and the socket
 * host empties into the database after each tick
 * (ToriRSServer_AccountsApply). NULL `account_log` is a world that records
 * nothing: the embedded one, the selftest, bots, development servers -- every
 * world whose accounts mean nothing (no password check).
 *
 * Stats are not hooked where experience is granted: every
 * TORIRSSERVER_ACCOUNTLOG_FLUSH_TICKS each player's table is compared with the
 * one last queued, so any writer of experience is seen (an xp drop, a script's
 * stat change, ::maxstats) and a minute of training costs one record, not one
 * per action. The queue is emptied every tick and holds a record per player
 * per tick several times over; a full queue is a host that stopped emptying
 * it, which asserts.
 */

#include <stdint.h>

struct ToriRSServerPlayer;

/* torirs_server.h TORIRSSERVER_STAT_COUNT: every stat, Summoning too. */
#define TORIRSSERVER_ACCOUNTLOG_STATS 25
#define TORIRSSERVER_ACCOUNTLOG_CAP 64
#define TORIRSSERVER_ACCOUNTLOG_NAME_MAX 32
#define TORIRSSERVER_ACCOUNTLOG_LABEL_MAX 80
/* A minute: how stale the hiscores may be while someone is training. */
#define TORIRSSERVER_ACCOUNTLOG_FLUSH_TICKS 100
/* configs/all.varp varp101_qp. */
#define TORIRSSERVER_ACCOUNTLOG_VARP_QP 101

enum ToriRSServerAccountLogKind
{
    /** The login finished: the stats as loaded, and the admin flag. */
    TORIRSSERVER_ACCOUNTLOG_LOGIN = 1,
    /** The stats changed since the last record. */
    TORIRSSERVER_ACCOUNTLOG_CHANGED = 2,
    /** The logout save: the stats as saved, and the session's start. */
    TORIRSSERVER_ACCOUNTLOG_LOGOUT = 3,
    /** A line of the adventure log (`adventure_log`); `event` says which. */
    TORIRSSERVER_ACCOUNTLOG_EVENT = 4,
};

/* What an adventure log line is. Content names these (`adventure_log`'s
 * kind); a number the engine does not know aborts the script. */
enum ToriRSServerAdventureKind
{
    /** A quest completed: `amount` its quest points, `label` its name. */
    TORIRSSERVER_ADVENTURE_QUEST = 1,
};

struct ToriRSServerAccountRecord
{
    enum ToriRSServerAccountLogKind kind;
    /** The player's display name; the database keys it as the save is. */
    char name[TORIRSSERVER_ACCOUNTLOG_NAME_MAX];
    long long time;
    /** LOGIN, STATS, LOGOUT: the experience table, in tenths. */
    int xp_tenths[TORIRSSERVER_ACCOUNTLOG_STATS];
    /** LOGIN, STATS, LOGOUT. */
    int quest_points;
    /** LOGIN: staff, who are never ranked. */
    int admin;
    /** LOGOUT: when the session began, and the experience it began with. */
    long long session_start;
    int session_xp_tenths[TORIRSSERVER_ACCOUNTLOG_STATS];
    /** EVENT. */
    enum ToriRSServerAdventureKind event;
    int amount;
    char label[TORIRSSERVER_ACCOUNTLOG_LABEL_MAX];
};

struct ToriRSServerAccountLog
{
    struct ToriRSServerAccountRecord records[TORIRSSERVER_ACCOUNTLOG_CAP];
    int count;
};

/** A world's queue; free with ToriRSServer_AccountLogFree. */
struct ToriRSServerAccountLog*
ToriRSServer_AccountLogCreate(void);

void
ToriRSServer_AccountLogFree(struct ToriRSServerAccountLog* log);

/** A login finished: queue the stats as loaded, and start the session. */
void
ToriRSServer_AccountLogLogin(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now);

/** Queue the stats if they changed since the last record. */
void
ToriRSServer_AccountLogStats(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now);

/** The logout save: queue the stats as saved, and the session. */
void
ToriRSServer_AccountLogLogout(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now);

/** A line of the player's adventure log. */
void
ToriRSServer_AccountLogEvent(
    struct ToriRSServerAccountLog* log,
    struct ToriRSServerPlayer* player,
    long long now,
    enum ToriRSServerAdventureKind event,
    int amount,
    const char* label);

#endif
