#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_HISTORY_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_HISTORY_H

/*
 * A player account's session history: one line per login and per logout,
 * appended by torirsserver and read by the account site (io_server's
 * io_site.c), which is the only reason it exists.
 *
 *   <saves>/history/<name>.log
 *     1793112345 login
 *     1793114000 logout <xp_tenths of stat 0> ... <xp_tenths of stat 24>
 *
 * A logout carries the whole stat table as it was saved, so a session's gain
 * is the difference between two logouts and a skill's history is the column
 * over time. Append-only text: one write per line, so a crash leaves at worst
 * a torn last line, which the reader skips. Written only where passwords are
 * checked (a world whose accounts mean something); the embedded world, bots
 * and the selftest leave no history.
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

/** Append `<now> login`. A failure is logged and dropped: history is a record
 *  for the site, never a reason to refuse a login. */
void
ToriRSServer_HistoryLogin(
    const char* display_name,
    long long now);

/** Append `<now> logout` with the stat table's experience in tenths. */
void
ToriRSServer_HistoryLogout(
    const char* display_name,
    long long now,
    const int xp_tenths[TORIRSSERVER_HISTORY_STATS]);

/** The newest entries of the name's history into `out`, oldest first. 0 entries
 *  when there is no file yet. */
void
ToriRSServer_HistoryRead(
    const char* display_name,
    struct ToriRSServerHistory* out);

#endif
