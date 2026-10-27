#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_HISTORY_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_HISTORY_H

/*
 * The session history files the account database replaced, read once.
 *
 *   <saves>/history/<name>.log
 *     1793112345 login
 *     1793114000 logout <xp_tenths of stat 0> ... <xp_tenths of stat 24>
 *
 * torirsserver appended a line per login and per logout until the account
 * database (accountserver/account_db.h) took over: its first open imports each
 * file into the per-day experience and the session table, and nothing writes
 * these any more. The reader keeps the newest TORIRSSERVER_HISTORY_MAX
 * entries and skips a torn line.
 */

#include <stddef.h>

/* The stat table a logout records; torirs_server.h TORIRSSERVER_STAT_COUNT. */
#define TORIRSSERVER_HISTORY_STATS 25
/* The most entries one read returns: the newest. */
#define TORIRSSERVER_HISTORY_MAX 512

enum ToriRSServerHistoryKind
{
    TORIRSSERVER_HISTORY_LOGIN = 1,
    TORIRSSERVER_HISTORY_LOGOUT = 2,
};

struct ToriRSServerHistoryEntry
{
    long long time;
    enum ToriRSServerHistoryKind kind;
    /** LOGOUT only: the experience saved, in tenths. */
    long long xp_tenths[TORIRSSERVER_HISTORY_STATS];
};

struct ToriRSServerHistory
{
    /** Oldest first. */
    struct ToriRSServerHistoryEntry entries[TORIRSSERVER_HISTORY_MAX];
    int count;
};

/** `<saves>/history/<name>.log`, or "" for a name that sanitises to nothing.
 *  Static storage, overwritten by the next call. */
const char*
ToriRSServer_HistoryPath(const char* display_name);

/** The newest entries of the name's history into `out`, oldest first. 0 entries
 *  when there is no file yet. */
void
ToriRSServer_HistoryRead(
    const char* display_name,
    struct ToriRSServerHistory* out);

#endif
